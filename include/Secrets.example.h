#pragma once

// Copy this file to include/Secrets.h for optional build-time flags only.
// WiFi and Spotify credentials are provisioned through the setup portal and stored in NVS.

// Optional. Use "NL", "US", etc. Leave empty to let Spotify infer it.
#define SPOTIFY_MARKET "NL"

// Keep this at 0 for normal use. Set to 1 only as a temporary debug fallback.
#define SPOTIFY_ALLOW_INSECURE_TLS 0
