#!/usr/bin/env bash
set -euo pipefail

# The numeric GitHub repository ID survives an ownership transfer.
repository="$(gh api repositories/1258610489 --jq 'select(.id == 1258610489 and .visibility == "public") | .full_name')"
case "$repository" in
  pcvantol/djconnect-firmware|djconnect-music-dj/djconnect-firmware)
    printf '%s\n' "$repository"
    ;;
  *)
    echo "Unexpected DJConnect firmware distribution repository: $repository" >&2
    exit 1
    ;;
esac
