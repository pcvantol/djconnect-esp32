// mDNS advertising for Home Assistant SpotifyDJ discovery.
#include "SpotifyDJDiscovery.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "AppLog.h"

bool SpotifyDJDiscovery::begin(SpotifyDJDevice &device) {
  device_ = &device;
  if (running_) {
    updateTxtRecords();
    return true;
  }
  if (WiFi.status() != WL_CONNECTED) {
    AppLog.println("[SpotifyDJ] mDNS skipped: WiFi disconnected");
    return false;
  }

  if (!MDNS.begin(device.getDeviceId().c_str())) {
    AppLog.println("[SpotifyDJ] mDNS begin failed");
    return false;
  }

  MDNS.addService("spotifydj", "tcp", 80);
  running_ = true;
  updateTxtRecords();

  AppLog.print("[SpotifyDJ] mDNS URL: ");
  AppLog.println(device.getLocalUrl());
  return true;
}

void SpotifyDJDiscovery::updateTxtRecords() {
  if (!running_ || device_ == nullptr) {
    return;
  }

  MDNS.addServiceTxt("spotifydj", "tcp", "name", device_->getDeviceName());
  MDNS.addServiceTxt("spotifydj", "tcp", "device_id", device_->getDeviceId());
  MDNS.addServiceTxt("spotifydj", "tcp", "version", device_->getFirmwareVersion());
  MDNS.addServiceTxt("spotifydj", "tcp", "paired", device_->isPaired() ? "true" : "false");
  MDNS.addServiceTxt("spotifydj", "tcp", "api", "/api/device");
  MDNS.addServiceTxt("spotifydj", "tcp", "model", device_->getModel());
}

bool SpotifyDJDiscovery::isRunning() const {
  return running_;
}
