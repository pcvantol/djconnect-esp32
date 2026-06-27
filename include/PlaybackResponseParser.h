#pragma once

#include <ArduinoJson.h>

#include "AppState.h"

namespace PlaybackResponseParser {

inline bool firstBool(JsonVariantConst payload, const char *primary, const char *secondary, bool fallback) {
  if (payload[primary].is<bool>()) {
    return payload[primary].as<bool>();
  }
  if (secondary != nullptr && payload[secondary].is<bool>()) {
    return payload[secondary].as<bool>();
  }
  return fallback;
}

inline JsonArrayConst outputArray(JsonVariantConst source) {
  if (source["outputs"].is<JsonArrayConst>()) {
    return source["outputs"].as<JsonArrayConst>();
  }
  if (source["data"]["outputs"].is<JsonArrayConst>()) {
    return source["data"]["outputs"].as<JsonArrayConst>();
  }
  if (source["result"]["outputs"].is<JsonArrayConst>()) {
    return source["result"]["outputs"].as<JsonArrayConst>();
  }
  if (source["playback"]["outputs"].is<JsonArrayConst>()) {
    return source["playback"]["outputs"].as<JsonArrayConst>();
  }
  return JsonArrayConst();
}

inline JsonArrayConst deviceArray(JsonVariantConst source) {
  if (source["devices"].is<JsonArrayConst>()) {
    return source["devices"].as<JsonArrayConst>();
  }
  if (source["items"].is<JsonArrayConst>()) {
    return source["items"].as<JsonArrayConst>();
  }
  if (source["data"]["devices"].is<JsonArrayConst>()) {
    return source["data"]["devices"].as<JsonArrayConst>();
  }
  if (source["data"]["items"].is<JsonArrayConst>()) {
    return source["data"]["items"].as<JsonArrayConst>();
  }
  if (source["result"]["devices"].is<JsonArrayConst>()) {
    return source["result"]["devices"].as<JsonArrayConst>();
  }
  if (source["result"]["items"].is<JsonArrayConst>()) {
    return source["result"]["items"].as<JsonArrayConst>();
  }
  if (source["playback"]["devices"].is<JsonArrayConst>()) {
    return source["playback"]["devices"].as<JsonArrayConst>();
  }
  return JsonArrayConst();
}

inline JsonArrayConst preferredOutputArray(JsonVariantConst source) {
  JsonArrayConst outputs = outputArray(source);
  return !outputs.isNull() ? outputs : deviceArray(source);
}

inline String backendErrorMessage(JsonVariantConst error, const String &fallback) {
  if (error.is<const char *>()) {
    return error.as<const char *>();
  }
  if (error.is<JsonObjectConst>()) {
    return error["message"] | error["code"] | fallback;
  }
  return fallback;
}

inline void applyBackendSummary(JsonVariantConst source, MusicBackendSummary &summary) {
  JsonVariantConst backendSource = source;
  if (source["backend"].is<JsonObjectConst>()) {
    backendSource = source["backend"];
  } else if (source["music"].is<JsonObjectConst>()) {
    backendSource = source["music"];
  } else if (source["data"].is<JsonObjectConst>() &&
             (source["data"]["music_backend"].is<const char *>() || source["data"]["backend"].is<JsonObjectConst>())) {
    backendSource = source["data"];
  } else if (source["result"].is<JsonObjectConst>() &&
             (source["result"]["music_backend"].is<const char *>() || source["result"]["backend"].is<JsonObjectConst>())) {
    backendSource = source["result"];
  }

  summary.backend = backendSource["music_backend"] | backendSource["id"] | summary.backend;
  summary.name = backendSource["music_backend_name"] | backendSource["name"] | summary.name;
  summary.available = firstBool(backendSource, "music_backend_available", "available", summary.available);
  summary.revision = backendSource["music_backend_revision"] | backendSource["revision"] | summary.revision;
  summary.error = backendErrorMessage(backendSource["music_backend_error"], summary.error);
  if (summary.error.length() == 0) {
    summary.error = backendErrorMessage(backendSource["error"], summary.error);
  }

  JsonVariantConst capabilities = backendSource["music_backend_capabilities"].is<JsonObjectConst>()
                                      ? backendSource["music_backend_capabilities"]
                                      : backendSource["capabilities"];
  if (!capabilities.isNull()) {
    summary.capabilities.supportsSearch = capabilities["supports_search"] | summary.capabilities.supportsSearch;
    summary.capabilities.supportsQueue = capabilities["supports_queue"] | summary.capabilities.supportsQueue;
    summary.capabilities.supportsOutputs = capabilities["supports_outputs"] | summary.capabilities.supportsOutputs;
    summary.capabilities.supportsFavorites = capabilities["supports_favorites"] | summary.capabilities.supportsFavorites;
    summary.capabilities.supportsRecentlyPlayed =
        capabilities["supports_recently_played"] | summary.capabilities.supportsRecentlyPlayed;
    summary.capabilities.supportsTopItems = capabilities["supports_top_items"] | summary.capabilities.supportsTopItems;
  }

  JsonVariantConst target = backendSource["music_target_player"].is<JsonObjectConst>()
                                ? backendSource["music_target_player"]
                                : backendSource["target_player"];
  if (!target.isNull()) {
    summary.targetPlayerId = target["id"] | summary.targetPlayerId;
    summary.targetPlayerName = target["name"] | summary.targetPlayerName;
  }
}

inline String queueContextUri(JsonVariantConst source) {
  const char *keys[] = {"context_uri", "contextUri", "queue_context", "queueContext"};
  auto findIn = [&](JsonVariantConst container) -> String {
    if (container.isNull()) {
      return "";
    }
    for (const char *key : keys) {
      JsonVariantConst field = container[key];
      if (field.is<const char *>()) {
        const char *value = field.as<const char *>();
        if (value != nullptr && value[0] != '\0') {
          return value;
        }
      }
    }
    return "";
  };

  String value = findIn(source);
  if (value.length() > 0) {
    return value;
  }
  value = findIn(source["playback"]);
  if (value.length() > 0) {
    return value;
  }
  value = findIn(source["queue"]);
  if (value.length() > 0) {
    return value;
  }
  value = findIn(source["data"]);
  if (value.length() > 0) {
    return value;
  }
  value = findIn(source["data"]["queue"]);
  if (value.length() > 0) {
    return value;
  }
  value = findIn(source["result"]);
  if (value.length() > 0) {
    return value;
  }
  value = findIn(source["result"]["queue"]);
  if (value.length() > 0) {
    return value;
  }
  return "";
}

inline JsonArrayConst queueArray(JsonVariantConst source) {
  if (source["queue"].is<JsonArrayConst>()) {
    return source["queue"].as<JsonArrayConst>();
  }
  if (source["items"].is<JsonArrayConst>()) {
    return source["items"].as<JsonArrayConst>();
  }
  if (source["queue"]["items"].is<JsonArrayConst>()) {
    return source["queue"]["items"].as<JsonArrayConst>();
  }
  if (source["data"]["queue"].is<JsonArrayConst>()) {
    return source["data"]["queue"].as<JsonArrayConst>();
  }
  if (source["data"]["items"].is<JsonArrayConst>()) {
    return source["data"]["items"].as<JsonArrayConst>();
  }
  if (source["data"]["queue"]["items"].is<JsonArrayConst>()) {
    return source["data"]["queue"]["items"].as<JsonArrayConst>();
  }
  if (source["result"]["queue"].is<JsonArrayConst>()) {
    return source["result"]["queue"].as<JsonArrayConst>();
  }
  if (source["result"]["items"].is<JsonArrayConst>()) {
    return source["result"]["items"].as<JsonArrayConst>();
  }
  if (source["result"]["queue"]["items"].is<JsonArrayConst>()) {
    return source["result"]["queue"]["items"].as<JsonArrayConst>();
  }
  return JsonArrayConst();
}

inline bool isUnsupportedBackendCapability(JsonVariantConst response) {
  return response["success"].is<bool>() &&
         !response["success"].as<bool>() &&
         response["error"].is<const char *>() &&
         String(response["error"].as<const char *>()) == "unsupported_backend_capability";
}

}  // namespace PlaybackResponseParser
