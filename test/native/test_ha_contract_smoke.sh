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

search_repo() {
  local pattern="$1"
  if command -v rg >/dev/null 2>&1; then
    rg -n --glob "!test/native/test_ha_contract_smoke.sh" "$pattern" "${SEARCH_PATHS[@]}"
  else
    git grep -n -E -- "$pattern" -- "${SEARCH_PATHS[@]}" ':!test/native/test_ha_contract_smoke.sh'
  fi
}

search_contract_paths() {
  local pattern="$1"
  if command -v rg >/dev/null 2>&1; then
    rg -n --glob "!test/native/test_ha_contract_smoke.sh" --glob "!test/native/test_release.sh" "$pattern" src include postman
  else
    git grep -n -E -- "$pattern" -- src include postman
  fi
}

require_pattern() {
  local pattern="$1"
  local path="$2"
  if command -v rg >/dev/null 2>&1; then
    rg -q "$pattern" "$path"
  else
    git grep -q -E -- "$pattern" -- "$path"
  fi
}

if search_repo \
  "sensor\\.djconnect_(spotify_status|playback_available|queue|playlists|outputs)|number\\.djconnect_volume|select\\.djconnect_(sound_output|repeat_state)|switch\\.djconnect_shuffle|\\bdjconnect_(volume|shuffle|repeat_state|sound_output|spotify_status|playback_available|queue|playlists|outputs)\\b"; then
  echo "removed Home Assistant playback entity dependency found" >&2
  exit 1
fi

if search_repo \
  "/api/djconnect/v1/ask_dj([^[:alnum:]_/.-]|$)|djconnect\\.ask_dj"; then
  echo "legacy raw Ask DJ API route or Home Assistant service found" >&2
  exit 1
fi

if search_repo \
  "spotify_search_query|last_spotify_search"; then
  echo "legacy provider-specific Ask DJ search field alias found" >&2
  exit 1
fi

if search_repo \
  "Track Insight|track insight|track_insight"; then
  echo "ESP32 firmware must not depend on app-client Track Insight APIs" >&2
  exit 1
fi

if search_repo \
  "/api/djconnect/(pair|command|status|voice)([^[:alnum:]_/.-]|$)"; then
  echo "legacy unversioned Home Assistant DJConnect API route found" >&2
  exit 1
fi

require_pattern 'request\["client_type"\] = device_.*getClientType\(\)' src/SpotifyClient.cpp
require_pattern '/api/djconnect/v1/command' src/SpotifyClient.cpp
require_pattern 'request\["payload_type"\] = "command"' src/SpotifyClient.cpp
require_pattern 'request\["command"\] = "set_shuffle"' src/SpotifyClient.cpp
require_pattern 'proxyCommand\("set_repeat", normalized\)' src/SpotifyClient.cpp
require_pattern 'request\["client_type"\] = device_->getClientType\(\)' src/DJConnectPairing.cpp
require_pattern '/api/djconnect/v1/pair' src/DJConnectPairing.cpp
require_pattern '/api/djconnect/v1/status' src/DJConnectPairing.cpp
require_pattern '/api/djconnect/v1/ask_dj/message' README.md
require_pattern '/api/djconnect/v1/ask_dj/history\?since_revision=<number>' README.md
require_pattern '/api/djconnect/v1/ask_dj/history/clear' README.md
require_pattern 'client_message_id' README.md
require_pattern 'history_revision' README.md
require_pattern 'clear_revision' README.md
require_pattern 'music_search_query' README.md
require_pattern 'last_music_search' README.md
require_pattern 'request\["ha_pairing_status"\] = "paired"' src/DJConnectPairing.cpp
require_pattern 'request\["screen_brightness_percent"\]' src/DJConnectPairing.cpp
require_pattern 'request\["speaker_volume_percent"\]' src/DJConnectPairing.cpp
require_pattern 'request\["wake_word_enabled"\]' src/DJConnectPairing.cpp
require_pattern 'request\["turn_off_after_ms"\]' src/DJConnectPairing.cpp
require_pattern 'isDjConnectVersionMismatch\(426, "version_mismatch"\)' test/native/test_logic.cpp
require_pattern '!Logic::isHomeAssistantPairingInvalidStatus\(426\)' test/native/test_logic.cpp
require_pattern '!Logic::isHomeAssistantPairingInvalidError\("version_mismatch"\)' test/native/test_logic.cpp
require_pattern '/api/djconnect/v1/voice' src/VoiceHttpClient.cpp

if search_contract_paths \
  "set_play_mode|refresh_token|client_secret|client_id"; then
  echo "legacy playback mode flow or backend credential reference found in firmware contract paths" >&2
  exit 1
fi

echo "Home Assistant contract smoke tests passed"
