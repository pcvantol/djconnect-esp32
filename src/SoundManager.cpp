// I2S speaker cues. Uses generated square waves to avoid storing audio assets.
#include "SoundManager.h"

#include <AudioFileSourceBuffer.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioFileSourceID3.h>
#include <AudioFileSource.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutput.h>
#include <ESP_I2S.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <cstring>

#include "AppLog.h"
#include "Config.h"
#include "ScopedWatchdogPause.h"

static constexpr uint32_t MinVolumeTickIntervalMs = 90;

namespace {
I2SClass SpeakerI2S;
alignas(8) uint8_t Mp3DecoderArena[AudioGeneratorMP3::preAllocSize()];

void serviceAudioWatchdog() {
  ScopedWatchdogPause::resetIfAttached();
}

bool configureSpeakerRate(uint32_t hertz) {
  return SpeakerI2S.configureTX(hertz > 0 ? hertz : Config::SpeakerSampleRate,
                                I2S_DATA_BIT_WIDTH_16BIT,
                                I2S_SLOT_MODE_MONO);
}

bool writeSpeakerPcm(const void *data, size_t bytes) {
  return data != nullptr && bytes > 0 && SpeakerI2S.write(static_cast<const uint8_t *>(data), bytes) == bytes;
}

class ScopedLoopWatchdogPause {
public:
  ScopedLoopWatchdogPause() {
    if (esp_task_wdt_status(nullptr) == ESP_OK) {
      active_ = esp_task_wdt_delete(nullptr) == ESP_OK;
    }
  }

  ~ScopedLoopWatchdogPause() {
    if (active_) {
      if (esp_task_wdt_add(nullptr) == ESP_OK) {
        serviceAudioWatchdog();
      }
    }
  }

private:
  bool active_ = false;
};

class AudioFileSourceArduinoStream : public AudioFileSource {
public:
  AudioFileSourceArduinoStream(Stream &stream, const uint8_t *prefix, size_t prefixLength, int contentLength)
      : stream_(stream),
        prefix_(prefix),
        prefixLength_(prefixLength),
        contentLength_(contentLength > 0 ? static_cast<uint32_t>(contentLength) : 0),
        open_(true) {
    stream_.setTimeout(35);
  }

  uint32_t read(void *data, uint32_t len) override {
    if (!open_ || data == nullptr || len == 0) {
      return 0;
    }
    if (contentLength_ > 0 && position_ >= contentLength_) {
      open_ = false;
      return 0;
    }
    uint8_t *target = static_cast<uint8_t *>(data);
    uint32_t copied = 0;
    while (copied < len && prefixOffset_ < prefixLength_) {
      target[copied++] = prefix_[prefixOffset_++];
      position_++;
    }
    if (copied < len) {
      const uint32_t remainingByLength = contentLength_ > 0 ? contentLength_ - position_ : len - copied;
      const uint32_t wanted = min(len - copied, remainingByLength);
      uint32_t bodyCopied = 0;
      uint32_t idleStartedAt = millis();
      while (bodyCopied < wanted && millis() - idleStartedAt < 120) {
        const int available = stream_.available();
        if (available <= 0) {
          serviceAudioWatchdog();
          delay(1);
          yield();
          continue;
        }
        const uint32_t chunk = min(static_cast<uint32_t>(available), wanted - bodyCopied);
        if (chunk == 0) {
          break;
        }
        const int got = stream_.readBytes(target + copied, chunk);
        if (got <= 0) {
          break;
        }
        copied += got;
        bodyCopied += got;
        position_ += got;
        idleStartedAt = millis();
        if (contentLength_ > 0 && position_ >= contentLength_) {
          open_ = false;
          break;
        }
      }
    }
    serviceAudioWatchdog();
    yield();
    return copied;
  }

  bool close() override {
    open_ = false;
    return true;
  }

  bool isOpen() override {
    return open_;
  }

  uint32_t getSize() override {
    return contentLength_;
  }

  uint32_t getPos() override {
    return position_;
  }

private:
  Stream &stream_;
  const uint8_t *prefix_ = nullptr;
  size_t prefixLength_ = 0;
  size_t prefixOffset_ = 0;
  uint32_t contentLength_ = 0;
  uint32_t position_ = 0;
  bool open_ = false;
};

class DjConnectI2sMp3Output : public AudioOutput {
public:
  bool begin() override {
    running_ = true;
    samplesWritten_ = 0;
    return SetRate(hertz > 0 ? hertz : Config::SpeakerSampleRate);
  }

  bool SetRate(int hz) override {
    hertz = hz > 0 ? hz : Config::SpeakerSampleRate;
    return configureSpeakerRate(hertz);
  }

  bool SetBitsPerSample(int bits) override {
    return AudioOutput::SetBitsPerSample(bits);
  }

  bool SetChannels(int channels) override {
    return AudioOutput::SetChannels(channels);
  }

  bool ConsumeSample(int16_t sample[2]) override {
    if (!running_ || sample == nullptr) {
      return false;
    }
    MakeSampleStereo16(sample);
    const int32_t mixed = (static_cast<int32_t>(sample[LEFTCHANNEL]) + static_cast<int32_t>(sample[RIGHTCHANNEL])) / 2;
    const int16_t mono = Amplify(static_cast<int16_t>(mixed));
    if (!writeSpeakerPcm(&mono, sizeof(mono))) {
      return false;
    }
    samplesWritten_++;
    return true;
  }

  bool stop() override {
    running_ = false;
    return true;
  }

  uint32_t samplesWritten() const {
    return samplesWritten_;
  }

private:
  bool running_ = false;
  uint32_t samplesWritten_ = 0;
};
}  // namespace

void SoundManager::begin() {
  if constexpr (!Config::HasSpeaker) {
    AppLog.println("Speaker unavailable on board profile");
    return;
  }
  if (ready_) {
    return;
  }
  if (i2sMutex_ == nullptr) {
    i2sMutex_ = xSemaphoreCreateMutex();
    if (i2sMutex_ == nullptr) {
      AppLog.println("Speaker mutex failed");
      return;
    }
  }

  if (!installI2s()) {
    AppLog.println("Speaker I2S init failed");
    return;
  }

  queue_ = xQueueCreate(6, sizeof(Event));
  if (queue_ == nullptr) {
    AppLog.println("Speaker queue failed");
    return;
  }

  ready_ = true;
  xTaskCreatePinnedToCore(soundTask, "sound", 3072, this, 1, nullptr, 0);
}

bool SoundManager::installI2s() {
  if constexpr (!Config::HasSpeaker) {
    return false;
  }
  SpeakerI2S.setPins(Config::SpeakerBclkPin, Config::SpeakerLrclkPin, Config::SpeakerDataPin);
  SpeakerI2S.setTimeout(100);
  if (!SpeakerI2S.begin(I2S_MODE_STD, Config::SpeakerSampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    AppLog.println(String("Speaker I2S error: ") +
                   String(esp_err_to_name(static_cast<esp_err_t>(SpeakerI2S.lastError()))));
    return false;
  }
  AppLog.println(String("Speaker I2S ready: bclk=") + String(Config::SpeakerBclkPin) +
                 ", lrclk=" + String(Config::SpeakerLrclkPin) +
                 ", data=" + String(Config::SpeakerDataPin));
  return true;
}

void SoundManager::setVolumePercent(uint8_t percent) {
  volumePercent_ = constrain(percent, 25, 100);
}

void SoundManager::playStartup() {
  enqueue(Event::Startup);
}

void SoundManager::playVolumeTick(int direction) {
  const uint32_t now = millis();
  if (now - lastVolumeTickAt_ < MinVolumeTickIntervalMs) {
    return;
  }
  lastVolumeTickAt_ = now;
  enqueue(direction >= 0 ? Event::VolumeUp : Event::VolumeDown);
}

void SoundManager::playMenuTick(int direction) {
  enqueue(direction >= 0 ? Event::MenuRight : Event::MenuLeft);
}

void SoundManager::playButtonPress() {
  enqueue(Event::ButtonPress);
}

void SoundManager::playConfirm() {
  enqueue(Event::Confirm);
}

void SoundManager::playPreviousTrack() {
  enqueue(Event::PreviousTrack);
}

void SoundManager::playNextTrack() {
  enqueue(Event::NextTrack);
}

void SoundManager::playPongBounce() {
  enqueue(Event::PongBounce);
}

void SoundManager::playPongMiss() {
  enqueue(Event::PongMiss);
}

void SoundManager::playMenuOpen() {
  enqueue(Event::MenuOpen);
}

void SoundManager::playBack() {
  enqueue(Event::Back);
}

void SoundManager::playTurnOff() {
  enqueue(Event::TurnOff);
}

void SoundManager::playSoftReset() {
  enqueue(Event::SoftReset);
}

void SoundManager::playPttStart() {
  enqueue(Event::PttStart);
}

void SoundManager::playPttStartBlocking(uint32_t settleMs) {
  if (!beginAudioState(AudioState::Cue, 80)) {
    return;
  }
  playTone(1319, 42, 12);
  playSilence(8 + static_cast<uint16_t>(min<uint32_t>(settleMs, 500)));
  endAudioState();
}

void SoundManager::playPttStop() {
  enqueue(Event::PttStop);
}

void SoundManager::playHardReset() {
  enqueue(Event::HardReset);
}

void SoundManager::playBatteryWarning() {
  enqueue(Event::BatteryWarning);
}

void SoundManager::playSetupPrompt() {
  enqueue(Event::SetupPrompt);
}

void SoundManager::playChargingComplete() {
  enqueue(Event::ChargingComplete);
}

void SoundManager::playOtaStart() {
  enqueue(Event::OtaStart);
}

void SoundManager::playOtaProgress() {
  enqueue(Event::OtaProgress);
}

void SoundManager::playOtaComplete() {
  enqueue(Event::OtaComplete);
}

void SoundManager::playOtaFailed() {
  enqueue(Event::OtaFailed);
}

void SoundManager::setStreamActivityCallback(StreamActivityCallback callback, void *context) {
  streamActivityCallback_ = callback;
  streamActivityContext_ = context;
}

void SoundManager::requestStopStreaming() {
  streamStopRequested_ = true;
}

void SoundManager::notifyStreamActivity() {
  if (streamActivityCallback_ != nullptr) {
    streamActivityCallback_(streamActivityContext_);
  }
}

bool SoundManager::playWavStream(Stream &stream, int contentLength) {
  if (!ready_) {
    AppLog.println("Voice response audio skipped: speaker not ready");
    return false;
  }
  if (!beginAudioState(AudioState::StreamingWav, 250)) {
    AppLog.println("Voice response audio skipped: speaker busy");
    return false;
  }
  auto finish = [&](bool ok) {
    endAudioState();
    return ok;
  };
  if (contentLength > 0 && contentLength < 12) {
    return finish(false);
  }

  auto readExact = [&](uint8_t *target, size_t length) -> bool {
    serviceAudioWatchdog();
    return stream.readBytes(target, length) == length;
  };
  auto readLe16 = [](const uint8_t *data) -> uint16_t {
    return data[0] | (data[1] << 8);
  };
  auto readLe32 = [](const uint8_t *data) -> uint32_t {
    return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
  };

  uint8_t riff[12] = {};
  if (!readExact(riff, sizeof(riff)) ||
      memcmp(riff, "RIFF", 4) != 0 ||
      memcmp(riff + 8, "WAVE", 4) != 0) {
    AppLog.println("Voice response is not PCM WAV");
    return finish(false);
  }

  uint16_t audioFormat = 0;
  uint16_t channels = 0;
  uint32_t sampleRate = 0;
  uint16_t bitsPerSample = 0;
  uint32_t dataBytes = 0;
  bool foundFormat = false;
  bool foundData = false;
  uint32_t consumed = sizeof(riff);

  // Parse RIFF chunks instead of assuming the data block starts at byte 44.
  // HA-generated TTS WAV files may include extra metadata chunks before audio.
  while (contentLength <= 0 || consumed + 8 <= static_cast<uint32_t>(contentLength)) {
    uint8_t chunkHeader[8] = {};
    if (!readExact(chunkHeader, sizeof(chunkHeader))) {
      break;
    }
    consumed += sizeof(chunkHeader);
    const uint32_t chunkSize = readLe32(chunkHeader + 4);

    if (memcmp(chunkHeader, "fmt ", 4) == 0) {
      uint8_t fmt[16] = {};
      if (chunkSize < sizeof(fmt) || !readExact(fmt, sizeof(fmt))) {
        AppLog.println("Invalid voice WAV fmt chunk");
        return finish(false);
      }
      consumed += sizeof(fmt);
      audioFormat = readLe16(fmt);
      channels = readLe16(fmt + 2);
      sampleRate = readLe32(fmt + 4);
      bitsPerSample = readLe16(fmt + 14);
      foundFormat = true;
      for (uint32_t skip = sizeof(fmt); skip < chunkSize; skip++) {
        serviceAudioWatchdog();
        if (stream.read() < 0) {
          return finish(false);
        }
        consumed++;
      }
    } else if (memcmp(chunkHeader, "data", 4) == 0) {
      dataBytes = chunkSize;
      foundData = true;
      break;
    } else {
      for (uint32_t skip = 0; skip < chunkSize; skip++) {
        serviceAudioWatchdog();
        if (stream.read() < 0) {
          return finish(false);
        }
      }
      consumed += chunkSize;
    }

    if (chunkSize % 2 != 0) {
      if (stream.read() < 0) {
        return finish(false);
      }
      consumed++;
    }
  }

  if (!foundFormat || !foundData) {
    AppLog.println("Voice WAV missing fmt/data chunk");
    return finish(false);
  }
  if (audioFormat != 1 || channels != 1 || bitsPerSample != 16 || sampleRate == 0) {
    AppLog.println("Unsupported voice WAV format");
    return finish(false);
  }
  if (contentLength > 0 && consumed + dataBytes > static_cast<uint32_t>(contentLength)) {
    dataBytes = static_cast<uint32_t>(contentLength) - consumed;
  }

  AppLog.print("Voice WAV playback: sample_rate=");
  AppLog.print(sampleRate);
  AppLog.print(" bytes=");
  AppLog.println(dataBytes);
  configureSpeakerRate(sampleRate);
  uint8_t buffer[512];
  uint32_t remaining = dataBytes;
  while (remaining > 0) {
    serviceAudioWatchdog();
    notifyStreamActivity();
    if (streamStopRequested_) {
      AppLog.println("Voice WAV playback cancelled");
      break;
    }
    const size_t want = min(static_cast<uint32_t>(sizeof(buffer)), remaining);
    const size_t got = stream.readBytes(buffer, want);
    if (got == 0) {
      break;
    }
    writeSpeakerPcm(buffer, got);
    remaining -= got;
    yield();
  }
  configureSpeakerRate(Config::SpeakerSampleRate);
  return finish(remaining == 0);
}

bool SoundManager::playMp3Stream(const String &url) {
  if (!ready_) {
    AppLog.println("MP3 response audio skipped: speaker not ready");
    return false;
  }
  if (url.isEmpty()) {
    return false;
  }
  if (!beginAudioState(AudioState::StreamingMp3, 250)) {
    AppLog.println("MP3 response audio skipped: speaker busy");
    return false;
  }

  AudioFileSourceHTTPStream source(url.c_str());
  AudioFileSourceBuffer buffered(&source, 4096);
  AudioFileSourceID3 id3(&buffered);
  DjConnectI2sMp3Output output;
  output.SetGain(static_cast<float>(volumePercent_) / 100.0f);

  AudioGeneratorMP3 mp3(Mp3DecoderArena, sizeof(Mp3DecoderArena));
  const bool started = mp3.begin(&id3, &output);
  if (!started) {
    AppLog.print("MP3 decoder start failed, arena=");
    AppLog.print(sizeof(Mp3DecoderArena));
    AppLog.print(" free=");
    AppLog.print(heap_caps_get_free_size(MALLOC_CAP_8BIT));
    AppLog.print(" largest=");
    AppLog.println(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    id3.close();
    buffered.close();
    source.close();
    configureSpeakerRate(Config::SpeakerSampleRate);
    endAudioState();
    return false;
  }

  AppLog.println("DJ response MP3 playback started");
  ScopedLoopWatchdogPause watchdogPause;
  const uint32_t startedAt = millis();
  uint32_t lastSampleLogAt = millis();
  uint32_t lastSampleCount = 0;
  while (mp3.isRunning()) {
    serviceAudioWatchdog();
    notifyStreamActivity();
    if (streamStopRequested_) {
      AppLog.println("DJ response MP3 playback cancelled");
      break;
    }
    if (!mp3.loop()) {
      break;
    }
    if (millis() - lastSampleLogAt > 3000) {
      const uint32_t count = output.samplesWritten();
      AppLog.print("DJ response MP3 samples=");
      AppLog.println(count);
      if (count == lastSampleCount) {
        AppLog.println("MP3 playback stalled");
        break;
      }
      lastSampleCount = count;
      lastSampleLogAt = millis();
    }
    if (millis() - startedAt > Config::DjAudioIoTimeoutMs) {
      AppLog.println("MP3 playback timeout");
      break;
    }
    delay(1);
    yield();
  }
  mp3.stop();
  id3.close();
  buffered.close();
  source.close();
  output.stop();
  configureSpeakerRate(Config::SpeakerSampleRate);
  endAudioState();
  AppLog.println("DJ response MP3 playback finished");
  return output.samplesWritten() > 0;
}

bool SoundManager::playMp3Stream(Stream &stream, const uint8_t *prefix, size_t prefixLength, int contentLength) {
  if (!ready_) {
    AppLog.println("MP3 response audio skipped: speaker not ready");
    return false;
  }
  if (!beginAudioState(AudioState::StreamingMp3, 250)) {
    AppLog.println("MP3 response audio skipped: speaker busy");
    return false;
  }

  AudioFileSourceArduinoStream source(stream, prefix, prefixLength, contentLength);
  AudioFileSourceBuffer buffered(&source, 4096);
  AudioFileSourceID3 id3(&buffered);
  DjConnectI2sMp3Output output;
  output.SetGain(static_cast<float>(volumePercent_) / 100.0f);

  AudioGeneratorMP3 mp3(Mp3DecoderArena, sizeof(Mp3DecoderArena));
  const bool started = mp3.begin(&id3, &output);
  if (!started) {
    AppLog.print("MP3 decoder start failed, arena=");
    AppLog.print(sizeof(Mp3DecoderArena));
    AppLog.print(" free=");
    AppLog.print(heap_caps_get_free_size(MALLOC_CAP_8BIT));
    AppLog.print(" largest=");
    AppLog.println(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    id3.close();
    buffered.close();
    source.close();
    configureSpeakerRate(Config::SpeakerSampleRate);
    endAudioState();
    return false;
  }

  AppLog.println("DJ response MP3 playback started");
  ScopedLoopWatchdogPause watchdogPause;
  const uint32_t startedAt = millis();
  uint32_t lastSampleLogAt = millis();
  uint32_t lastSampleCount = 0;
  while (mp3.isRunning()) {
    serviceAudioWatchdog();
    notifyStreamActivity();
    if (streamStopRequested_) {
      AppLog.println("DJ response MP3 playback cancelled");
      break;
    }
    if (!mp3.loop()) {
      break;
    }
    if (millis() - lastSampleLogAt > 3000) {
      const uint32_t count = output.samplesWritten();
      AppLog.print("DJ response MP3 samples=");
      AppLog.println(count);
      if (count == lastSampleCount) {
        AppLog.println("MP3 playback stalled");
        break;
      }
      lastSampleCount = count;
      lastSampleLogAt = millis();
    }
    if (millis() - startedAt > Config::DjAudioIoTimeoutMs) {
      AppLog.println("MP3 playback timeout");
      break;
    }
    delay(1);
    yield();
  }
  mp3.stop();
  id3.close();
  buffered.close();
  source.close();
  output.stop();
  configureSpeakerRate(Config::SpeakerSampleRate);
  endAudioState();
  AppLog.println("DJ response MP3 playback finished");
  return output.samplesWritten() > 0;
}

void SoundManager::soundTask(void *parameter) {
  static_cast<SoundManager *>(parameter)->runTask();
}

void SoundManager::runTask() {
  Event event;
  for (;;) {
    if (xQueueReceive(queue_, &event, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    if (!beginAudioState(AudioState::Cue, 50)) {
      continue;
    }

    switch (event) {
      case Event::Startup:
        playTone(523, 70);
        playSilence(20);
        playTone(659, 70);
        playSilence(20);
        playTone(784, 110);
        break;
      case Event::VolumeUp:
        playTone(1047, 28, 14);
        break;
      case Event::VolumeDown:
        playTone(784, 28, 14);
        break;
      case Event::MenuRight:
        playTone(740, 18, 8);
        break;
      case Event::MenuLeft:
        playTone(622, 18, 8);
        break;
      case Event::ButtonPress:
        playTone(880, 20, 9);
        break;
      case Event::Confirm:
        playTone(988, 32, 10);
        break;
      case Event::PreviousTrack:
        playTone(740, 26, 11);
        playSilence(18);
        playTone(554, 34, 11);
        break;
      case Event::NextTrack:
        playTone(554, 26, 11);
        playSilence(18);
        playTone(740, 34, 11);
        break;
      case Event::PongBounce:
        playTone(740, 14, 6);
        break;
      case Event::PongMiss:
        playTone(330, 75, 14);
        playSilence(20);
        playTone(220, 120, 14);
        break;
      case Event::MenuOpen:
        playTone(1047, 36, 10);
        break;
      case Event::Back:
        playTone(587, 36, 10);
        break;
      case Event::TurnOff:
        playTone(784, 70, 12);
        playSilence(30);
        playTone(392, 110, 12);
        break;
      case Event::SoftReset:
        playTone(1175, 45, 16);
        playSilence(25);
        playTone(1568, 75, 16);
        break;
      case Event::PttStart:
        playTone(1319, 42, 12);
        break;
      case Event::PttStop:
        playTone(880, 42, 12);
        break;
      case Event::HardReset:
        // Deliberately unlike startup/soft reset: sharp descending alarm burst.
        playTone(988, 90, 24);
        playSilence(35);
        playTone(494, 120, 24);
        playSilence(35);
        playTone(247, 180, 24);
        break;
      case Event::BatteryWarning:
        playTone(880, 90, 20);
        playSilence(50);
        playTone(660, 160, 20);
        break;
      case Event::SetupPrompt:
        playTone(659, 70, 14);
        playSilence(30);
        playTone(880, 90, 14);
        break;
      case Event::ChargingComplete:
        playTone(784, 80, 16);
        playSilence(35);
        playTone(988, 80, 16);
        playSilence(35);
        playTone(1175, 140, 16);
        break;
      case Event::OtaStart:
        playTone(988, 55, 10);
        break;
      case Event::OtaProgress:
        playTone(1319, 18, 6);
        break;
      case Event::OtaComplete:
        playTone(1568, 75, 12);
        break;
      case Event::OtaFailed:
        playTone(370, 100, 14);
        break;
    }
    playSilence(8);
    endAudioState();
  }
}

void SoundManager::enqueue(Event event) {
  if (!ready_ || queue_ == nullptr || audioState_ != AudioState::Idle) {
    return;
  }
  xQueueSend(queue_, &event, 0);
}

bool SoundManager::takeI2s(uint32_t timeoutMs) {
  return i2sMutex_ != nullptr && xSemaphoreTake(i2sMutex_, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void SoundManager::releaseI2s() {
  if (i2sMutex_ != nullptr) {
    xSemaphoreGive(i2sMutex_);
  }
}

bool SoundManager::beginAudioState(AudioState state, uint32_t timeoutMs) {
  if (!takeI2s(timeoutMs)) {
    return false;
  }
  if (audioState_ != AudioState::Idle) {
    releaseI2s();
    return false;
  }
  audioState_ = state;
  if (state == AudioState::StreamingWav || state == AudioState::StreamingMp3) {
    streamStopRequested_ = false;
  }
  return true;
}

void SoundManager::endAudioState() {
  audioState_ = AudioState::Idle;
  releaseI2s();
}

void SoundManager::playTone(uint16_t frequency, uint16_t durationMs, uint8_t amplitude) {
  if (!ready_ || frequency == 0 || durationMs == 0) {
    return;
  }

  const size_t sampleCount = (Config::SpeakerSampleRate * durationMs) / 1000;
  const uint32_t halfPeriod = max<uint32_t>(1, Config::SpeakerSampleRate / (frequency * 2UL));
  const uint16_t scaledAmplitude = (static_cast<uint16_t>(amplitude) * volumePercent_) / 100;
  const int16_t level = static_cast<int16_t>(scaledAmplitude) * 512;
  int16_t buffer[128];
  size_t generated = 0;

  while (generated < sampleCount) {
    const size_t count = min(sampleCount - generated, static_cast<size_t>(128));
    for (size_t index = 0; index < count; index++) {
      const bool high = ((generated + index) / halfPeriod) % 2 == 0;
      buffer[index] = high ? level : -level;
    }
    writeSpeakerPcm(buffer, count * sizeof(int16_t));
    generated += count;
  }
}

void SoundManager::playSilence(uint16_t durationMs) {
  if (!ready_ || durationMs == 0) {
    return;
  }
  const size_t sampleCount = (Config::SpeakerSampleRate * durationMs) / 1000;
  int16_t buffer[128] = {};
  size_t generated = 0;
  while (generated < sampleCount) {
    const size_t count = min(sampleCount - generated, static_cast<size_t>(128));
    writeSpeakerPcm(buffer, count * sizeof(int16_t));
    generated += count;
  }
}
