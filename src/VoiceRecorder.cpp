// Records mono 16-bit PCM audio from the T-Embed CC1101 PDM microphone.
#include "VoiceRecorder.h"

#include <ESP_I2S.h>
#include <LittleFS.h>

#include "AppLog.h"
#include "Config.h"

namespace {
// The PDM microphone is isolated on I2S0 so speaker playback can use I2S1.
I2SClass MicI2S;
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

  MicI2S.setPinsPdmRx(Config::MicrophoneClockPin, Config::MicrophoneDataPin);
  MicI2S.setTimeout(30);
  if (!MicI2S.begin(I2S_MODE_PDM_RX, Config::VoiceSampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    error_ = "Mic PDM init failed";
    AppLog.line(error_ + ": " + String(esp_err_to_name(static_cast<esp_err_t>(MicI2S.lastError()))));
    return false;
  }
  AppLog.line(String("Mic PDM ready: clk=") + String(Config::MicrophoneClockPin) +
              ", data=" + String(Config::MicrophoneDataPin));
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
  AppLog.println("Voice PCM recording started");
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
  bytesRead = MicI2S.readBytes(reinterpret_cast<char *>(buffer), capacity);
  if (bytesRead == 0 && MicI2S.lastError() != ESP_OK) {
    error_ = "Mic read failed";
    AppLog.line(error_ + ": " + String(esp_err_to_name(static_cast<esp_err_t>(MicI2S.lastError()))));
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
  bytesRead = MicI2S.readBytes(reinterpret_cast<char *>(buffer), capacity);
  if (bytesRead == 0 && MicI2S.lastError() != ESP_OK) {
    error_ = "Mic monitor read failed";
    AppLog.line(error_ + ": " + String(esp_err_to_name(static_cast<esp_err_t>(MicI2S.lastError()))));
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
  AppLog.println("Voice recording started");
  return true;
}

bool VoiceRecorder::update() {
  if (!recording_) {
    return true;
  }
  int16_t buffer[256];
  const size_t bytesRead = MicI2S.readBytes(reinterpret_cast<char *>(buffer), sizeof(buffer));
  if (bytesRead == 0 && MicI2S.lastError() != ESP_OK) {
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
  for (uint8_t index = 0; index < 8; index++) {
    update();
    delay(2);
  }
  recording_ = false;
  if (dataBytes_ == 0) {
    error_ = "No audio recorded";
    return false;
  }
  if (!rewriteWavHeader()) {
    return false;
  }
  AppLog.print("Voice WAV bytes: ");
  AppLog.println(wavSize());
  return true;
}

bool VoiceRecorder::stopRaw() {
  if (!recording_) {
    return false;
  }
  recording_ = false;
  AppLog.print("Voice PCM bytes: ");
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
