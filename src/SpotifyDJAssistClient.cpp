// Streams raw PCM audio to Home Assistant Assist and extracts the STT result.
#include "SpotifyDJAssistClient.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <mbedtls/base64.h>

#include "AppLog.h"
#include "Config.h"
#include "LogicHelpers.h"

namespace {
constexpr uint32_t ConnectTimeoutMs = 5000;
constexpr uint32_t AuthTimeoutMs = 5000;
constexpr uint32_t PipelineStartTimeoutMs = 5000;
constexpr uint32_t ResultTimeoutMs = 15000;
constexpr size_t MaxTextFrameBytes = 4096;

String joinPath(const String &left, const String &right) {
  if (left.endsWith("/") && right.startsWith("/")) {
    return left.substring(0, left.length() - 1) + right;
  }
  if (!left.endsWith("/") && !right.startsWith("/")) {
    return left + "/" + right;
  }
  return left + right;
}

String randomWebSocketKey() {
  uint8_t randomBytes[16];
  for (uint8_t index = 0; index < sizeof(randomBytes); index += 4) {
    const uint32_t value = esp_random();
    memcpy(randomBytes + index, &value, min<size_t>(4, sizeof(randomBytes) - index));
  }
  unsigned char encoded[32] = {};
  size_t encodedLength = 0;
  mbedtls_base64_encode(encoded, sizeof(encoded), &encodedLength, randomBytes, sizeof(randomBytes));
  return String(reinterpret_cast<char *>(encoded), encodedLength);
}

String trimCopy(String value) {
  value.trim();
  return value;
}
}  // namespace

void SpotifyDJAssistClient::begin(SpotifyDJDevice &device) {
  device_ = &device;
}

bool SpotifyDJAssistClient::start(String &message) {
  close();
  if (device_ == nullptr || !device_->isPaired()) {
    message = "No HA pairing";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    message = "WiFi disconnected";
    return false;
  }

  ParsedUrl url;
  if (!parseWebSocketUrl(url, message) ||
      !connectSocket(url, message) ||
      !sendHandshake(url, message) ||
      !authenticate(message) ||
      !startPipeline(message)) {
    close();
    return false;
  }

  connected_ = true;
  AppLog.println("[SpotifyDJ] Assist listening");
  return true;
}

bool SpotifyDJAssistClient::sendAudio(const uint8_t *data, size_t length) {
  if (!connected_ || sttHandlerId_ == 0 || data == nullptr || length == 0) {
    return false;
  }
  const size_t payloadBytes = Logic::voiceAudioPayloadBytes(length, Config::VoicePcmChunkBytes);
  if (payloadBytes == 0) {
    return false;
  }
  const size_t frameBytes = Logic::voiceAssistBinaryFrameBytes(length, Config::VoicePcmChunkBytes);
  if (frameBytes == 0 || frameBytes > 1 + Config::VoicePcmChunkBytes) {
    return false;
  }
  uint8_t frame[1 + Config::VoicePcmChunkBytes];
  // Home Assistant Assist expects the handler id as byte 0 of every binary audio frame.
  frame[0] = sttHandlerId_;
  memcpy(frame + 1, data, payloadBytes);
  return sendBinary(frame, frameBytes);
}

bool SpotifyDJAssistClient::finish(String &recognizedText, String &message) {
  if (!connected_ || sttHandlerId_ == 0) {
    message = "Assist not ready";
    close();
    return false;
  }

  // A single-byte binary frame signals end-of-audio for this STT handler.
  const uint8_t stopPayload[] = {sttHandlerId_};
  if (!sendBinary(stopPayload, sizeof(stopPayload))) {
    message = "Assist stop failed";
    close();
    return false;
  }

  const uint32_t startedAt = millis();
  while (millis() - startedAt < ResultTimeoutMs) {
    String payload;
    if (!readTextFrame(payload, ResultTimeoutMs - (millis() - startedAt))) {
      continue;
    }
    if (extractRecognizedText(payload, recognizedText) && !recognizedText.isEmpty()) {
      AppLog.print("[SpotifyDJ] Assist recognized chars=");
      AppLog.println(recognizedText.length());
      close();
      return true;
    }

    JsonDocument doc;
    if (!deserializeJson(doc, payload)) {
      const char *messageType = doc["type"] | "";
      const char *eventType = doc["event"]["type"] | "";
      if (strlen(eventType) > 0) {
        AppLog.print("[SpotifyDJ] Assist event: ");
        AppLog.println(eventType);
      }
      if (strcmp(messageType, "result") == 0 || strcmp(eventType, "run-end") == 0) {
        break;
      }
    }
  }

  message = "No speech recognized";
  close();
  return false;
}

void SpotifyDJAssistClient::close() {
  if (plainClient_.connected()) {
    plainClient_.stop();
  }
  if (secureClient_.connected()) {
    secureClient_.stop();
  }
  connected_ = false;
  secure_ = false;
  sttHandlerId_ = 0;
}

bool SpotifyDJAssistClient::parseWebSocketUrl(ParsedUrl &url, String &message) const {
  if (device_ == nullptr) {
    message = "No HA device";
    return false;
  }
  String base = device_->getHaUrl();
  base.trim();
  if (base.isEmpty()) {
    message = "No HA URL";
    return false;
  }

  if (base.startsWith("https://")) {
    url.secure = true;
    url.port = 443;
    base.remove(0, 8);
  } else if (base.startsWith("http://")) {
    url.secure = false;
    url.port = 80;
    base.remove(0, 7);
  } else {
    url.secure = false;
    url.port = 80;
  }

  const int slash = base.indexOf('/');
  String hostPort = slash >= 0 ? base.substring(0, slash) : base;
  String basePath = slash >= 0 ? base.substring(slash) : "";
  const int colon = hostPort.lastIndexOf(':');
  if (colon > 0) {
    url.host = hostPort.substring(0, colon);
    url.port = static_cast<uint16_t>(hostPort.substring(colon + 1).toInt());
  } else {
    url.host = hostPort;
  }
  url.path = joinPath(basePath, "/api/websocket");
  if (url.host.isEmpty()) {
    message = "Bad HA URL";
    return false;
  }
  return true;
}

bool SpotifyDJAssistClient::connectSocket(const ParsedUrl &url, String &message) {
  secure_ = url.secure;
  if (secure_) {
    secureClient_.setInsecure();
  }
  WiFiClient *socket = client();
  socket->setTimeout(ConnectTimeoutMs);
  AppLog.println("[SpotifyDJ] Assist websocket connect");
  if (!socket->connect(url.host.c_str(), url.port, ConnectTimeoutMs)) {
    message = "Assist connect failed";
    return false;
  }
  return true;
}

bool SpotifyDJAssistClient::sendHandshake(const ParsedUrl &url, String &message) {
  WiFiClient *socket = client();
  const String key = randomWebSocketKey();
  socket->print("GET " + url.path + " HTTP/1.1\r\n");
  socket->print("Host: " + url.host + "\r\n");
  socket->print("Upgrade: websocket\r\n");
  socket->print("Connection: Upgrade\r\n");
  socket->print("Sec-WebSocket-Key: " + key + "\r\n");
  socket->print("Sec-WebSocket-Version: 13\r\n\r\n");

  const uint32_t startedAt = millis();
  String statusLine;
  while (millis() - startedAt < ConnectTimeoutMs) {
    if (socket->available()) {
      statusLine = socket->readStringUntil('\n');
      break;
    }
    delay(5);
  }
  statusLine.trim();
  while (socket->connected()) {
    const String line = socket->readStringUntil('\n');
    if (trimCopy(line).isEmpty()) {
      break;
    }
  }
  if (statusLine.indexOf("101") < 0) {
    message = "Assist handshake failed";
    AppLog.print("[SpotifyDJ] Assist handshake: ");
    AppLog.println(statusLine);
    return false;
  }
  return true;
}

bool SpotifyDJAssistClient::authenticate(String &message) {
  String payload;
  if (!waitForJsonType("auth_required", AuthTimeoutMs, payload, message)) {
    return false;
  }

  JsonDocument auth;
  auth["type"] = "auth";
  auth["access_token"] = device_->getDeviceToken();
  String body;
  serializeJson(auth, body);
  if (!sendText(body)) {
    message = "Assist auth send failed";
    return false;
  }
  if (!waitForJsonType("auth_ok", AuthTimeoutMs, payload, message)) {
    return false;
  }
  AppLog.println("[SpotifyDJ] Assist auth_ok");
  return true;
}

bool SpotifyDJAssistClient::startPipeline(String &message) {
  JsonDocument doc;
  doc["id"] = nextMessageId_++;
  doc["type"] = "assist_pipeline/run";
  doc["start_stage"] = "stt";
  doc["end_stage"] = "stt";
  JsonObject input = doc["input"].to<JsonObject>();
  input["sample_rate"] = Config::VoiceSampleRate;
  const String pipelineId = device_->getAssistPipelineId();
  if (!pipelineId.isEmpty()) {
    doc["pipeline"] = pipelineId;
  }

  String body;
  serializeJson(doc, body);
  if (!sendText(body)) {
    message = "Assist pipeline send failed";
    return false;
  }

  const uint32_t startedAt = millis();
  while (millis() - startedAt < PipelineStartTimeoutMs) {
    String payload;
    if (!readTextFrame(payload, PipelineStartTimeoutMs - (millis() - startedAt))) {
      continue;
    }
    JsonDocument event;
    if (deserializeJson(event, payload)) {
      continue;
    }
    const char *messageType = event["type"] | "";
    const char *eventType = event["event"]["type"] | "";
    if (strlen(eventType) > 0) {
      AppLog.print("[SpotifyDJ] Assist event: ");
      AppLog.println(eventType);
    }
    if (strcmp(messageType, "result") == 0 && !(event["success"] | true)) {
      message = "Assist pipeline failed";
      return false;
    }
    extractSttHandlerId(payload);
    if (strcmp(eventType, "stt-start") == 0 && sttHandlerId_ != 0) {
      return true;
    }
  }
  message = "Assist STT timeout";
  return false;
}

bool SpotifyDJAssistClient::waitForJsonType(const String &type, uint32_t timeoutMs, String &payload, String &message) {
  const uint32_t startedAt = millis();
  while (millis() - startedAt < timeoutMs) {
    if (!readTextFrame(payload, timeoutMs - (millis() - startedAt))) {
      continue;
    }
    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
      continue;
    }
    const String found = doc["type"] | "";
    if (found == type) {
      return true;
    }
    if (found == "auth_invalid") {
      message = "Assist auth_invalid";
      AppLog.println("[SpotifyDJ] Assist auth_invalid");
      return false;
    }
  }
  message = "Assist " + type + " timeout";
  return false;
}

bool SpotifyDJAssistClient::readTextFrame(String &payload, uint32_t timeoutMs) {
  uint8_t opcode = 0;
  String text;
  if (!readFrame(opcode, text, timeoutMs)) {
    return false;
  }
  if (opcode == 0x8) {
    return false;
  }
  if (opcode != 0x1) {
    return false;
  }
  payload = text;
  return true;
}

bool SpotifyDJAssistClient::readFrame(uint8_t &opcode, String &textPayload, uint32_t timeoutMs) {
  WiFiClient *socket = client();
  const uint32_t startedAt = millis();
  auto readByte = [&](uint8_t &value) -> bool {
    while (millis() - startedAt < timeoutMs) {
      esp_task_wdt_reset();
      if (socket->available()) {
        value = socket->read();
        return true;
      }
      delay(2);
      yield();
    }
    return false;
  };

  uint8_t first = 0;
  uint8_t second = 0;
  if (!readByte(first) || !readByte(second)) {
    return false;
  }
  opcode = first & 0x0F;

  // Server-to-client frames are unmasked; this client only needs text frames from HA.
  uint64_t length = second & 0x7F;
  if (length == 126) {
    uint8_t b1 = 0;
    uint8_t b2 = 0;
    if (!readByte(b1) || !readByte(b2)) {
      return false;
    }
    length = (static_cast<uint16_t>(b1) << 8) | b2;
  } else if (length == 127) {
    length = 0;
    for (uint8_t index = 0; index < 8; index++) {
      uint8_t next = 0;
      if (!readByte(next)) {
        return false;
      }
      length = (length << 8) | next;
    }
  }
  if (length > MaxTextFrameBytes) {
    while (length-- > 0 && socket->connected()) {
      uint8_t ignored = 0;
      if (!readByte(ignored)) {
        break;
      }
    }
    return false;
  }

  textPayload = "";
  textPayload.reserve(static_cast<size_t>(length));
  for (uint64_t index = 0; index < length; index++) {
    uint8_t next = 0;
    if (!readByte(next)) {
      return false;
    }
    textPayload += static_cast<char>(next);
  }
  return true;
}

bool SpotifyDJAssistClient::sendText(const String &payload) {
  return sendFrame(0x1, reinterpret_cast<const uint8_t *>(payload.c_str()), payload.length());
}

bool SpotifyDJAssistClient::sendBinary(const uint8_t *data, size_t length) {
  return sendFrame(0x2, data, length);
}

bool SpotifyDJAssistClient::sendFrame(uint8_t opcode, const uint8_t *data, size_t length) {
  WiFiClient *socket = client();
  if (socket == nullptr || !socket->connected()) {
    return false;
  }
  uint8_t header[14] = {};
  size_t headerLength = 0;
  header[headerLength++] = 0x80 | (opcode & 0x0F);
  if (length < 126) {
    header[headerLength++] = 0x80 | static_cast<uint8_t>(length);
  } else if (length <= 0xFFFF) {
    header[headerLength++] = 0x80 | 126;
    header[headerLength++] = static_cast<uint8_t>(length >> 8);
    header[headerLength++] = static_cast<uint8_t>(length);
  } else {
    return false;
  }

  // RFC 6455 requires every client-to-server frame to be masked.
  uint8_t mask[4];
  const uint32_t randomMask = esp_random();
  memcpy(mask, &randomMask, sizeof(mask));
  memcpy(header + headerLength, mask, sizeof(mask));
  headerLength += sizeof(mask);
  if (socket->write(header, headerLength) != headerLength) {
    return false;
  }

  uint8_t buffer[256];
  size_t offset = 0;
  while (offset < length) {
    const size_t chunk = min(sizeof(buffer), length - offset);
    for (size_t index = 0; index < chunk; index++) {
      buffer[index] = data[offset + index] ^ mask[(offset + index) & 0x03];
    }
    if (socket->write(buffer, chunk) != chunk) {
      return false;
    }
    esp_task_wdt_reset();
    yield();
    offset += chunk;
  }
  return true;
}

bool SpotifyDJAssistClient::extractRecognizedText(const String &payload, String &recognizedText) {
  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    return false;
  }
  const char *eventType = doc["event"]["type"] | "";
  if (strlen(eventType) == 0) {
    return false;
  }
  const char *text = doc["event"]["data"]["stt_output"]["text"] | "";
  if (strlen(text) == 0) {
    text = doc["event"]["data"]["text"] | "";
  }
  if (strlen(text) == 0) {
    return false;
  }
  recognizedText = text;
  recognizedText.trim();
  return !recognizedText.isEmpty();
}

bool SpotifyDJAssistClient::extractSttHandlerId(const String &payload) {
  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    return false;
  }
  const int id = doc["event"]["data"]["runner_data"]["stt_binary_handler_id"] | 0;
  if (id <= 0 || id > 255) {
    return false;
  }
  sttHandlerId_ = static_cast<uint8_t>(id);
  AppLog.print("[SpotifyDJ] Assist STT binary handler: ");
  AppLog.println(sttHandlerId_);
  return true;
}

WiFiClient *SpotifyDJAssistClient::client() {
  return secure_ ? static_cast<WiFiClient *>(&secureClient_) : static_cast<WiFiClient *>(&plainClient_);
}
