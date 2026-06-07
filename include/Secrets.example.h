#pragma once

// Copy this file to include/Secrets.h for optional build-time flags only.
// WiFi is provisioned through the setup portal and stored in NVS.
// Playback-backend credentials live in Home Assistant, not on the ESP.

// Legacy compatibility flag. Leave empty; backend market selection belongs in Home Assistant.
#define SPOTIFY_MARKET ""

// Keep this at 0 for normal use. Set to 1 only as a temporary debug fallback.
#define SPOTIFY_ALLOW_INSECURE_TLS 0
