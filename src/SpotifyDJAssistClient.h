// Home Assistant Assist WebSocket client for push-to-talk STT.
#pragma once

#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include "SpotifyDJDevice.h"

class SpotifyDJAssistClient {
public:
  void begin(SpotifyDJDevice &device);

  bool start(String &message);
  bool sendAudio(const uint8_t *data, size_t length);
  bool finish(String &recognizedText, String &message);
  void close();

private:
  struct ParsedUrl {
    bool secure = false;
    String host;
    uint16_t port = 80;
    String path;
  };

  bool parseWebSocketUrl(ParsedUrl &url, String &message) const;
  bool connectSocket(const ParsedUrl &url, String &message);
  bool sendHandshake(const ParsedUrl &url, String &message);
  bool authenticate(String &message);
  bool startPipeline(String &message);
  bool waitForJsonType(const String &type, uint32_t timeoutMs, String &payload, String &message);
  bool readTextFrame(String &payload, uint32_t timeoutMs);
  bool readFrame(uint8_t &opcode, String &textPayload, uint32_t timeoutMs);
  bool sendText(const String &payload);
  bool sendBinary(const uint8_t *data, size_t length);
  bool sendFrame(uint8_t opcode, const uint8_t *data, size_t length);
  bool extractRecognizedText(const String &payload, String &recognizedText);
  bool extractSttHandlerId(const String &payload);
  WiFiClient *client();

  SpotifyDJDevice *device_ = nullptr;
  WiFiClient plainClient_;
  WiFiClientSecure secureClient_;
  bool secure_ = false;
  bool connected_ = false;
  uint32_t nextMessageId_ = 1;
  uint8_t sttHandlerId_ = 0;
};
