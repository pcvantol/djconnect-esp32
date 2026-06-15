// mDNS advertising for Home Assistant DJConnect discovery.
#include "DJConnectDiscovery.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "AppLog.h"

bool DJConnectDiscovery::begin(DJConnectDevice &device) {
  device_ = &device;
  if (running_) {
    updateTxtRecords();
    return true;
  }
  if (WiFi.status() != WL_CONNECTED) {
    AppLog.println("mDNS skipped: WiFi disconnected");
    return false;
  }

  if (!MDNS.begin(device.getDeviceId().c_str())) {
    AppLog.println("mDNS begin failed");
    return false;
  }

  MDNS.addService("djconnect", "tcp", 80);
  running_ = true;
  updateTxtRecords();

  AppLog.print("mDNS URL: ");
  AppLog.println(device.getLocalUrl());
  return true;
}

void DJConnectDiscovery::updateTxtRecords() {
  if (!running_ || device_ == nullptr) {
    return;
  }

  MDNS.addServiceTxt("djconnect", "tcp", "name", device_->getDeviceName());
  MDNS.addServiceTxt("djconnect", "tcp", "device_id", device_->getDeviceId());
  MDNS.addServiceTxt("djconnect", "tcp", "client_type", device_->getClientType());
  MDNS.addServiceTxt("djconnect", "tcp", "version", device_->getFirmwareVersion());
  MDNS.addServiceTxt("djconnect", "tcp", "paired", device_->isPaired() ? "true" : "false");
  MDNS.addServiceTxt("djconnect", "tcp", "api", "/api/device");
  MDNS.addServiceTxt("djconnect", "tcp", "model", device_->getModel());
}

bool DJConnectDiscovery::isRunning() const {
  return running_;
}
