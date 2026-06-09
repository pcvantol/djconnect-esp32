// BLE WiFi provisioning endpoint used while the setup portal is active.
#include "BleWifiProvisioning.h"

#include <BLEDevice.h>
#include <BLE2902.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#include "AppLog.h"

namespace {
constexpr char ServiceUuid[] = "7f705000-9f8f-4f1a-9b5f-570071fd0001";
constexpr char ProvisionCharacteristicUuid[] = "7f705001-9f8f-4f1a-9b5f-570071fd0001";
constexpr char StatusCharacteristicUuid[] = "7f705002-9f8f-4f1a-9b5f-570071fd0001";
BleWifiProvisioning *activeProvisioning = nullptr;
constexpr size_t MaxProvisioningPayloadBytes = 512;

bool looksLikeCompleteJsonObject(const String &payload) {
  bool inString = false;
  bool escaped = false;
  bool started = false;
  int depth = 0;
  for (size_t index = 0; index < payload.length(); index++) {
    const char c = payload[index];
    if (!started) {
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        continue;
      }
      if (c != '{') {
        return true;
      }
      started = true;
      depth = 1;
      continue;
    }
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        inString = false;
      }
      continue;
    }
    if (c == '"') {
      inString = true;
    } else if (c == '{') {
      depth++;
    } else if (c == '}') {
      depth--;
      if (depth == 0) {
        return true;
      }
    }
  }
  return false;
}

class ProvisionCharacteristicCallbacks : public BLECharacteristicCallbacks {
public:
  void onWrite(BLECharacteristic *characteristic) override {
    if (activeProvisioning == nullptr || characteristic == nullptr) {
      return;
    }
    const std::string value = characteristic->getValue();
    String payload;
    payload.reserve(value.size());
    for (char c : value) {
      payload += c;
    }
    if (!payload.isEmpty()) {
      activeProvisioning->receivePayload(payload);
      AppLog.print("BLE provisioning payload chunk received, bytes=");
      AppLog.println(payload.length());
    }
  }
};
}  // namespace

void BleWifiProvisioning::begin(const String &deviceId) {
  if (started_) {
    return;
  }
  activeProvisioning = this;
  const int suffixStart = deviceId.length() > 4 ? static_cast<int>(deviceId.length()) - 4 : 0;
  const String name = deviceId.isEmpty() ? "DJConnect Setup" : "DJConnect " + deviceId.substring(suffixStart);
  BLEDevice::init(name.c_str());
  BLEServer *server = BLEDevice::createServer();
  BLEService *service = server->createService(ServiceUuid);
  BLECharacteristic *provision = service->createCharacteristic(
      ProvisionCharacteristicUuid,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  provision->setCallbacks(new ProvisionCharacteristicCallbacks());
  statusCharacteristic_ = service->createCharacteristic(
      StatusCharacteristicUuid,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  statusCharacteristic_->addDescriptor(new BLE2902());
  statusCharacteristic_->setValue("{\"state\":\"ready\",\"message\":\"Write WiFi JSON\"}");
  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(ServiceUuid);
  advertising->setScanResponse(true);
  advertising->start();

  started_ = true;
  AppLog.print("BLE provisioning started as ");
  AppLog.println(name);
}

void BleWifiProvisioning::end() {
  if (!started_) {
    return;
  }
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  if (advertising != nullptr) {
    advertising->stop();
  }
  activeProvisioning = nullptr;
  started_ = false;
  payloadPending_ = false;
  receiveBuffer_ = "";
  pendingPayload_ = "";
  statusCharacteristic_ = nullptr;
  AppLog.println("BLE provisioning stopped");
}

bool BleWifiProvisioning::pollPayload(String &payload) {
  if (!payloadPending_) {
    return false;
  }
  payload = pendingPayload_;
  pendingPayload_ = "";
  payloadPending_ = false;
  return true;
}

bool BleWifiProvisioning::isStarted() const {
  return started_;
}

void BleWifiProvisioning::receivePayload(const String &payload) {
  String chunk = payload;
  chunk.trim();
  if (chunk.startsWith("{")) {
    receiveBuffer_ = "";
  }
  receiveBuffer_ += payload;

  if (receiveBuffer_.length() > MaxProvisioningPayloadBytes) {
    pendingPayload_ = receiveBuffer_;
    receiveBuffer_ = "";
    payloadPending_ = true;
    setStatus("error", "WiFi JSON too large");
    return;
  }

  if (!looksLikeCompleteJsonObject(receiveBuffer_)) {
    setStatus("receiving", "Receiving WiFi JSON");
    return;
  }

  pendingPayload_ = receiveBuffer_;
  receiveBuffer_ = "";
  payloadPending_ = true;
  setStatus("received", "WiFi JSON received");
}

void BleWifiProvisioning::setStatus(const String &state, const String &message) {
  if (statusCharacteristic_ == nullptr) {
    return;
  }

  String payload = "{\"state\":\"";
  payload += state;
  payload += "\"";
  if (!message.isEmpty()) {
    String escaped = message;
    escaped.replace("\\", "\\\\");
    escaped.replace("\"", "\\\"");
    payload += ",\"message\":\"";
    payload += escaped;
    payload += "\"";
  }
  payload += "}";
  statusCharacteristic_->setValue(payload.c_str());
  if (started_) {
    statusCharacteristic_->notify();
  }
}
