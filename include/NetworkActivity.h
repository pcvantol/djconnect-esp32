#pragma once

#include <Arduino.h>
#include <HTTPClient.h>

// Small guard around long network calls: every activity gets explicit HTTP timeouts
// and a bounded log line with duration/result when it finishes.
class NetworkActivity {
public:
  explicit NetworkActivity(const char *name, uint32_t slowWarningMs = 1000);
  ~NetworkActivity();

  static void configureHttp(HTTPClient &http, uint32_t connectTimeoutMs, uint32_t ioTimeoutMs);
  static void configureDefaultHttp(HTTPClient &http);
  static void configureLongHttp(HTTPClient &http);

  void finish(int httpCode, const String &message = "");
  void finishOk(const String &message = "");
  void finishError(const String &message);

private:
  const char *name_;
  uint32_t startedAt_;
  uint32_t slowWarningMs_;
  bool finished_ = false;
};
