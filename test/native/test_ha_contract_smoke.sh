#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

SEARCH_PATHS=(
  README.md
  DESIGN_DECISIONS.md
  CHANGELOG.md
  AGENTS.md
  HANDOFF.md
  TODO.md
  src
  include
  test
  postman
  .github
  release.sh
  scripts
)

if rg -n --glob "!test/native/test_ha_contract_smoke.sh" \
  "sensor\\.djconnect_(spotify_status|playback_available|queue|playlists|outputs)|number\\.djconnect_volume|select\\.djconnect_(sound_output|repeat_state)|switch\\.djconnect_shuffle|\\bdjconnect_(volume|shuffle|repeat_state|sound_output|spotify_status|playback_available|queue|playlists|outputs)\\b" \
  "${SEARCH_PATHS[@]}"; then
  echo "removed Home Assistant playback entity dependency found" >&2
  exit 1
fi

if rg -n --glob "!test/native/test_ha_contract_smoke.sh" \
  "/ask_dj/history/clear|ask_dj/history|Track Insight|track insight|track_insight" \
  "${SEARCH_PATHS[@]}"; then
  echo "ESP32 firmware must not depend on app-client Ask DJ history or Track Insight APIs" >&2
  exit 1
fi

if rg -n --glob "!test/native/test_ha_contract_smoke.sh" \
  "/api/djconnect/(pair|command|status|voice)([^[:alnum:]_/.-]|$)" \
  "${SEARCH_PATHS[@]}"; then
  echo "legacy unversioned Home Assistant DJConnect API route found" >&2
  exit 1
fi

rg -q 'request\["client_type"\] = device_.*getClientType\(\)' src/SpotifyClient.cpp
rg -q '/api/djconnect/v1/command' src/SpotifyClient.cpp
rg -q 'request\["payload_type"\] = "command"' src/SpotifyClient.cpp
rg -q 'request\["command"\] = "set_shuffle"' src/SpotifyClient.cpp
rg -q 'proxyCommand\("set_repeat", normalized\)' src/SpotifyClient.cpp
rg -q 'request\["client_type"\] = device_->getClientType\(\)' src/DJConnectPairing.cpp
rg -q '/api/djconnect/v1/pair' src/DJConnectPairing.cpp
rg -q '/api/djconnect/v1/status' src/DJConnectPairing.cpp
rg -q 'request\["ha_pairing_status"\] = "paired"' src/DJConnectPairing.cpp
rg -q 'request\["screen_brightness_percent"\]' src/DJConnectPairing.cpp
rg -q 'request\["speaker_volume_percent"\]' src/DJConnectPairing.cpp
rg -q 'request\["wake_word_enabled"\]' src/DJConnectPairing.cpp
rg -q 'request\["turn_off_after_ms"\]' src/DJConnectPairing.cpp
rg -q 'isDjConnectVersionMismatch\(426, "version_mismatch"\)' test/native/test_logic.cpp
rg -q '!Logic::isHomeAssistantPairingInvalidStatus\(426\)' test/native/test_logic.cpp
rg -q '!Logic::isHomeAssistantPairingInvalidError\("version_mismatch"\)' test/native/test_logic.cpp
rg -q '/api/djconnect/v1/voice' src/VoiceHttpClient.cpp

if rg -n --glob "!test/native/test_ha_contract_smoke.sh" --glob "!test/native/test_release.sh" \
  "set_play_mode|refresh_token|client_secret|client_id" \
  src include postman; then
  echo "legacy playback mode flow or backend credential reference found in firmware contract paths" >&2
  exit 1
fi

echo "Home Assistant contract smoke tests passed"
