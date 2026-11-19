#!/usr/bin/env bash
set -euo pipefail

if [[ "${GITHUB_REPOSITORY_ID:-}" != 1258611332 ]]; then
  echo "Unexpected DJConnect ESP32 source repository ID." >&2
  exit 1
fi
case "${GITHUB_REPOSITORY:-}" in
  djconnect-music-dj/djconnect-esp32)
    printf '%s\n' "$GITHUB_REPOSITORY"
    ;;
  *)
    echo "Unexpected DJConnect ESP32 source repository name." >&2
    exit 1
    ;;
esac
