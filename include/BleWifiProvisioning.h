// BLE WiFi provisioning endpoint used while the setup portal is active.
#pragma once

#include <Arduino.h>

class BLECharacteristic;

class BleWifiProvisioning {
public:
  // Starts BLE advertising with one writable provisioning characteristic.
  void begin(const String &deviceId);

  // Stops BLE and frees controller memory after provisioning mode exits.
  void end();

  // Returns one pending JSON payload written over BLE, if available.
  bool pollPayload(String &payload);

  // Called by the BLE characteristic callback when a phone writes provisioning JSON.
  void receivePayload(const String &payload);

  // Updates the readable/notifiable status characteristic for HA Bluetooth Proxy flows.
  void setStatus(const String &state, const String &message = "");

private:
  bool started_ = false;
  String pendingPayload_;
  bool payloadPending_ = false;
  BLECharacteristic *statusCharacteristic_ = nullptr;
};
