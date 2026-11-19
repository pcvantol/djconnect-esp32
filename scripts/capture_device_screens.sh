#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/capture_device_screens.sh --host 192.168.1.109 [--out /tmp/djconnect-screens] [--ssh user@host]

Captures all known DJConnect device screens through the local ESP debug API.
Local dev/vdev firmware allows this without a bearer token. Release firmware
keeps screenshots protected and requires --token.

Options:
  --host <host-or-ip>   ESP hostname/IP, without scheme.
  --out <dir>           Output directory. Default: /tmp/djconnect-screens.
  --ssh <target>        Run curl on an SSH host on the same LAN and stream BMPs back.
  --token <token>       Bearer token for release firmware.
EOF
}

fail() {
  echo "capture_device_screens.sh: $*" >&2
  exit 1
}

HOST=""
OUT_DIR="/tmp/djconnect-screens"
SSH_TARGET=""
TOKEN=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --host)
      [[ $# -ge 2 ]] || fail "--host requires a value"
      HOST="$2"
      shift 2
      ;;
    --out)
      [[ $# -ge 2 ]] || fail "--out requires a value"
      OUT_DIR="$2"
      shift 2
      ;;
    --ssh)
      [[ $# -ge 2 ]] || fail "--ssh requires a value"
      SSH_TARGET="$2"
      shift 2
      ;;
    --token)
      [[ $# -ge 2 ]] || fail "--token requires a value"
      TOKEN="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      fail "unknown argument: $1"
      ;;
  esac
done

[[ -n "$HOST" ]] || { usage; exit 1; }
command -v sips >/dev/null 2>&1 || fail "sips is required for BMP -> PNG conversion"

mkdir -p "$OUT_DIR"

SCREENS=(
  "now-playing"
  "current-song"
  "queue"
  "playlists"
  "outputs"
  "menu"
  "games"
  "paddle-rally"
  "meteor-run"
  "sky-dash"
  "maze-chase"
  "help"
  "settings"
  "brightness"
  "dim-timeout"
  "sleep-timeout"
  "language"
  "theme"
  "log-level"
  "speaker-volume"
  "shuffle"
  "repeat"
  "about"
  "logs"
)

curl_cmd() {
  local method="$1"
  local url="$2"
  local output="${3:-}"
  local -a args=(curl -fsS --max-time 10 -X "$method")
  if [[ -n "$TOKEN" ]]; then
    args+=(-H "Authorization: Bearer $TOKEN")
  fi
  if [[ -n "$output" ]]; then
    args+=(-o "$output")
  fi
  args+=("$url")

  if [[ -n "$SSH_TARGET" ]]; then
    if [[ -n "$output" ]]; then
      ssh -o BatchMode=yes "$SSH_TARGET" "$(printf '%q ' "${args[@]}")" > "$output"
    else
      ssh -o BatchMode=yes "$SSH_TARGET" "$(printf '%q ' "${args[@]}")"
    fi
  else
    "${args[@]}"
  fi
}

echo "Capturing ${#SCREENS[@]} screens from http://$HOST"
for screen in "${SCREENS[@]}"; do
  echo "- $screen"
  curl_cmd POST "http://$HOST/api/device/debug/screen?screen=$screen" >/dev/null
  sleep 0.25
  bmp="$OUT_DIR/$screen.bmp"
  png="$OUT_DIR/$screen.png"
  curl_cmd GET "http://$HOST/api/device/screenshot.bmp" "$bmp"
  sips -s format png "$bmp" --out "$png" >/dev/null
done

echo "Screens written to $OUT_DIR"
