// Helpers for parsing Spotify OAuth provisioning payloads from Home Assistant.
#pragma once

#include <ArduinoJson.h>

struct SpotifyProvisioningCredentials {
  const char *clientId = nullptr;
  const char *refreshToken = nullptr;
  const char *market = nullptr;

  bool complete() const {
    return clientId != nullptr && clientId[0] != '\0' &&
           refreshToken != nullptr && refreshToken[0] != '\0';
  }
};

namespace SpotifyProvisioning {

inline const char *firstNonEmpty(const char *first, const char *second, const char *third, const char *fourth) {
  if (first != nullptr && first[0] != '\0') return first;
  if (second != nullptr && second[0] != '\0') return second;
  if (third != nullptr && third[0] != '\0') return third;
  if (fourth != nullptr && fourth[0] != '\0') return fourth;
  return nullptr;
}

inline SpotifyProvisioningCredentials parseCredentials(JsonVariantConst payload) {
  SpotifyProvisioningCredentials credentials;
  if (!payload.is<JsonObjectConst>()) {
    return credentials;
  }

  JsonVariantConst spotify = payload["spotify"];
  credentials.refreshToken = firstNonEmpty(
      spotify["spotify_refresh_token"] | static_cast<const char *>(nullptr),
      spotify["refresh_token"] | static_cast<const char *>(nullptr),
      payload["spotify_refresh_token"] | static_cast<const char *>(nullptr),
      payload["refresh_token"] | static_cast<const char *>(nullptr));
  credentials.clientId = firstNonEmpty(
      spotify["spotify_client_id"] | static_cast<const char *>(nullptr),
      spotify["client_id"] | static_cast<const char *>(nullptr),
      payload["spotify_client_id"] | static_cast<const char *>(nullptr),
      payload["client_id"] | static_cast<const char *>(nullptr));
  credentials.market = firstNonEmpty(
      spotify["spotify_market"] | static_cast<const char *>(nullptr),
      spotify["market"] | static_cast<const char *>(nullptr),
      payload["spotify_market"] | static_cast<const char *>(nullptr),
      payload["market"] | static_cast<const char *>(nullptr));
  return credentials;
}

}  // namespace SpotifyProvisioning
