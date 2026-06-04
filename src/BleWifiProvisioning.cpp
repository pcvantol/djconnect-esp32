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
      AppLog.println("BLE provisioning payload received");
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
  const String name = deviceId.isEmpty() ? "SpotifyDJ Setup" : "SpotifyDJ " + deviceId.substring(suffixStart);
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
  BLEDevice::deinit(true);
  activeProvisioning = nullptr;
  started_ = false;
  payloadPending_ = false;
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

void BleWifiProvisioning::receivePayload(const String &payload) {
  pendingPayload_ = payload;
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
