#include "NetworkActivity.h"

#include "AppLog.h"
#include "Config.h"
#include "NetworkActivityLogic.h"

NetworkActivity::NetworkActivity(const char *name, uint32_t slowWarningMs)
    : name_(name == nullptr ? "network" : name),
      startedAt_(millis()),
      slowWarningMs_(slowWarningMs) {
  AppLog.print("Network start: ");
  AppLog.println(name_);
}

NetworkActivity::~NetworkActivity() {
  if (!finished_) {
    finishError("left without explicit result");
  }
}

void NetworkActivity::configureHttp(HTTPClient &http, uint32_t connectTimeoutMs, uint32_t ioTimeoutMs) {
  NetworkActivityLogic::configureHttp(http, connectTimeoutMs, ioTimeoutMs);
}

void NetworkActivity::configureDefaultHttp(HTTPClient &http) {
  configureHttp(http, Config::HttpConnectTimeoutMs, Config::HttpIoTimeoutMs);
}

void NetworkActivity::configureLongHttp(HTTPClient &http) {
  configureHttp(http, Config::HttpConnectTimeoutMs, Config::HttpLongIoTimeoutMs);
}

void NetworkActivity::finish(int httpCode, const String &message) {
  const uint32_t elapsed = millis() - startedAt_;
  AppLog.print("Network done: ");
  AppLog.print(name_);
  AppLog.print(" code=");
  AppLog.print(httpCode);
  AppLog.print(" ms=");
  AppLog.print(elapsed);
  if (NetworkActivityLogic::isSlow(elapsed, slowWarningMs_)) {
    AppLog.print(" slow");
  }
  if (!message.isEmpty()) {
    AppLog.print(" ");
    AppLog.print(message);
  }
  AppLog.println();
  finished_ = true;
}

void NetworkActivity::finishOk(const String &message) {
  finish(200, message);
}

void NetworkActivity::finishError(const String &message) {
  finish(-1, message);
}
