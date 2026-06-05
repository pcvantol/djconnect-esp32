// Records mono 16-bit PCM audio from the T-Embed CC1101 PDM microphone.
#include "VoiceRecorder.h"

#include <LittleFS.h>
#include <driver/i2s.h>

#include "AppLog.h"
#include "Config.h"

namespace {
// The PDM microphone is isolated on I2S0 so speaker playback can use I2S1.
constexpr i2s_port_t MicI2sPort = I2S_NUM_0;
const char *VoiceWavPath = "/voice_ptt.wav";
constexpr size_t WavHeaderBytes = 44;

void writeLe16(File &file, uint16_t value) {
  file.write(static_cast<uint8_t>(value & 0xFF));
  file.write(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void writeLe32(File &file, uint32_t value) {
  file.write(static_cast<uint8_t>(value & 0xFF));
  file.write(static_cast<uint8_t>((value >> 8) & 0xFF));
  file.write(static_cast<uint8_t>((value >> 16) & 0xFF));
  file.write(static_cast<uint8_t>((value >> 24) & 0xFF));
}
}  // namespace

bool VoiceRecorder::begin() {
  if (ready_) {
    return true;
  }
  if (!LittleFS.begin(true)) {
    error_ = "LittleFS unavailable";
    return false;
  }

  const i2s_config_t i2sConfig = {
      .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
      .sample_rate = Config::VoiceSampleRate,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = 256,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0,
  };
  const i2s_pin_config_t pinConfig = {
      .mck_io_num = I2S_PIN_NO_CHANGE,
      .bck_io_num = Config::MicrophoneClockPin,
      .ws_io_num = I2S_PIN_NO_CHANGE,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = Config::MicrophoneDataPin,
  };

  esp_err_t result = i2s_driver_install(MicI2sPort, &i2sConfig, 0, nullptr);
  if (result != ESP_OK) {
    error_ = "Mic I2S init failed";
    AppLog.print(error_);
    AppLog.print(": ");
    AppLog.println(esp_err_to_name(result));
    return false;
  }
  result = i2s_set_pin(MicI2sPort, &pinConfig);
  if (result != ESP_OK) {
    i2s_driver_uninstall(MicI2sPort);
    error_ = "Mic I2S pin failed";
    AppLog.print(error_);
    AppLog.print(": ");
    AppLog.println(esp_err_to_name(result));
    return false;
  }
  result = i2s_set_clk(MicI2sPort, Config::VoiceSampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
  if (result != ESP_OK) {
    i2s_driver_uninstall(MicI2sPort);
    error_ = "Mic I2S clock failed";
    AppLog.println(error_);
    return false;
  }
  i2s_zero_dma_buffer(MicI2sPort);
  ready_ = true;
  return true;
}

bool VoiceRecorder::startRaw() {
  if (!ready_ && !begin()) {
    return false;
  }
  dataBytes_ = 0;
  startedAt_ = millis();
  recording_ = true;
  error_ = "";
  i2s_zero_dma_buffer(MicI2sPort);
  AppLog.println("[SpotifyDJ] voice PCM recording started");
  return true;
}

bool VoiceRecorder::readPcmChunk(uint8_t *buffer, size_t capacity, size_t &bytesRead) {
  bytesRead = 0;
  if (!recording_) {
    return true;
  }
  if (!ready_) {
    error_ = "Mic unavailable";
    return false;
  }
  const esp_err_t result = i2s_read(MicI2sPort, buffer, capacity, &bytesRead, 0);
  if (result != ESP_OK) {
    error_ = "Mic read failed";
    AppLog.print(error_);
    AppLog.print(": ");
    AppLog.println(esp_err_to_name(result));
    return false;
  }
  if (bytesRead > 0) {
    dataBytes_ += bytesRead;
  }
  if (dataBytes_ > Config::VoiceMaxWavBytes) {
    error_ = "Audio too large";
    return false;
  }
  return true;
}

bool VoiceRecorder::readMonitorChunk(uint8_t *buffer, size_t capacity, size_t &bytesRead) {
  bytesRead = 0;
  if (recording_) {
    return true;
  }
  if (!ready_ && !begin()) {
    return false;
  }
  const esp_err_t result = i2s_read(MicI2sPort, buffer, capacity, &bytesRead, 0);
  if (result != ESP_OK) {
    error_ = "Mic monitor read failed";
    AppLog.print(error_);
    AppLog.print(": ");
    AppLog.println(esp_err_to_name(result));
    return false;
  }
  return true;
}

bool VoiceRecorder::start() {
  if (!ready_ && !begin()) {
    return false;
  }
  LittleFS.remove(VoiceWavPath);
  writePlaceholderHeader();
  if (!LittleFS.exists(VoiceWavPath)) {
    error_ = "Voice file open failed";
    return false;
  }
  dataBytes_ = 0;
  startedAt_ = millis();
  recording_ = true;
  error_ = "";
  i2s_zero_dma_buffer(MicI2sPort);
  AppLog.println("[SpotifyDJ] voice recording started");
  return true;
}

bool VoiceRecorder::update() {
  if (!recording_) {
    return true;
  }
  int16_t buffer[256];
  size_t bytesRead = 0;
  const esp_err_t result = i2s_read(MicI2sPort, buffer, sizeof(buffer), &bytesRead, 0);
  if (result != ESP_OK) {
    error_ = "Mic read failed";
    return false;
  }
  if (bytesRead == 0) {
    return true;
  }
  if (WavHeaderBytes + dataBytes_ + bytesRead > Config::VoiceMaxWavBytes) {
    error_ = "Audio too large";
    return false;
  }
  File file = LittleFS.open(VoiceWavPath, "a");
  if (!file) {
    error_ = "Voice append failed";
    return false;
  }
  const size_t written = file.write(reinterpret_cast<uint8_t *>(buffer), bytesRead);
  file.close();
  if (written != bytesRead) {
    error_ = "Voice write failed";
    return false;
  }
  dataBytes_ += bytesRead;
  return true;
}

bool VoiceRecorder::stop() {
  if (!recording_) {
    return false;
  }
  recording_ = false;
  for (uint8_t index = 0; index < 8; index++) {
    update();
    delay(2);
  }
  if (dataBytes_ == 0) {
    error_ = "No audio recorded";
    return false;
  }
  if (!rewriteWavHeader()) {
    return false;
  }
  AppLog.print("[SpotifyDJ] voice WAV bytes: ");
  AppLog.println(wavSize());
  return true;
}

bool VoiceRecorder::stopRaw() {
  if (!recording_) {
    return false;
  }
  recording_ = false;
  AppLog.print("[SpotifyDJ] voice PCM bytes: ");
  AppLog.println(dataBytes_);
  if (dataBytes_ == 0) {
    error_ = "No audio recorded";
    return false;
  }
  return true;
}

bool VoiceRecorder::abort() {
  recording_ = false;
  LittleFS.remove(VoiceWavPath);
  return true;
}

bool VoiceRecorder::isRecording() const {
  return recording_;
}

bool VoiceRecorder::isReady() const {
  return ready_;
}

uint32_t VoiceRecorder::elapsedMs() const {
  return recording_ ? millis() - startedAt_ : 0;
}

size_t VoiceRecorder::wavSize() const {
  return WavHeaderBytes + dataBytes_;
}

String VoiceRecorder::wavPath() const {
  return VoiceWavPath;
}

String VoiceRecorder::error() const {
  return error_;
}

void VoiceRecorder::writePlaceholderHeader() {
  File file = LittleFS.open(VoiceWavPath, "w");
  if (!file) {
    return;
  }
  for (size_t index = 0; index < WavHeaderBytes; index++) {
    file.write(static_cast<uint8_t>(0));
  }
  file.close();
}

bool VoiceRecorder::rewriteWavHeader() {
  File file = LittleFS.open(VoiceWavPath, "r+");
  if (!file) {
    error_ = "Voice header failed";
    return false;
  }
  const uint32_t byteRate = Config::VoiceSampleRate * 2;
  file.seek(0);
  file.write(reinterpret_cast<const uint8_t *>("RIFF"), 4);
  writeLe32(file, 36 + dataBytes_);
  file.write(reinterpret_cast<const uint8_t *>("WAVEfmt "), 8);
  writeLe32(file, 16);
  writeLe16(file, 1);
  writeLe16(file, 1);
  writeLe32(file, Config::VoiceSampleRate);
  writeLe32(file, byteRate);
  writeLe16(file, 2);
  writeLe16(file, 16);
  file.write(reinterpret_cast<const uint8_t *>("data"), 4);
  writeLe32(file, dataBytes_);
  file.close();
  return true;
}
