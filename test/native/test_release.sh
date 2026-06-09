#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

bash -n release.sh
bash -n scripts/cleanup_old_releases.sh

# Repository hygiene: generated release assets must stay out of source control.
git check-ignore -q release/djconnect-device-v3.0.0.bin
if git ls-files --error-unmatch release/firmware_manifest.json >/dev/null 2>&1; then
  echo "release artifacts must not be tracked in the source repo" >&2
  exit 1
fi

# The DJConnect rebrand should not regress to old product names or old 2.x firmware assets.
if rg -n --glob "!test/native/test_release.sh" "SpotifyDJ|spotifydj|spotify_dj|SPOTIFYDJ|Spotify DJ|spotify-dj|/api/spotify_dj|X-SpotifyDJ|set_play_mode|\bha_url\b|djconnect-[0-9A-Fa-f]{12}" \
  README.md CHANGELOG.md AGENTS.md HANDOFF.md HA_SYNC_PROMPT.md TODO.md LICENSE THIRD_PARTY_NOTICES.md src include test postman .github release.sh scripts; then
  echo "old product/endpoint reference found" >&2
  exit 1
fi

if rg -n --glob "!test/native/test_release.sh" "spotifydj-device|releases/download/v2\.[0-9]+\.[0-9]+|tag/v2\.[0-9]+\.[0-9]+|Release .*v2\.[0-9]+\.[0-9]+|firmware v2\.[0-9]+\.[0-9]+" \
  README.md CHANGELOG.md AGENTS.md HANDOFF.md HA_SYNC_PROMPT.md TODO.md LICENSE THIRD_PARTY_NOTICES.md src include test postman .github release.sh scripts; then
  echo "old 2.x firmware release reference found" >&2
  exit 1
fi

dry_run_output="$(./release.sh 98.76.54 --dry-run)"
echo "$dry_run_output" | grep -q "version: 98.76.54"
echo "$dry_run_output" | grep -q "tag:     v98.76.54"
echo "$dry_run_output" | grep -q "channel: stable"
echo "$dry_run_output" | grep -q "Would build with DJCONNECT_VERSION=98.76.54"
echo "$dry_run_output" | grep -q "Would commit, tag and push source repo"
echo "$dry_run_output" | grep -q "GitHub Actions will build/publish"

set +e
invalid_output="$(./release.sh 98.76 --dry-run 2>&1)"
invalid_status=$?
set -e

if [[ "$invalid_status" -eq 0 ]]; then
  echo "invalid version unexpectedly succeeded" >&2
  exit 1
fi
echo "$invalid_output" | grep -q "version must be semantic"

beta_output="$(./release.sh 98.76.55 --channel beta --dry-run)"
echo "$beta_output" | grep -q "version: 98.76.55"
echo "$beta_output" | grep -q "tag:     v98.76.55-beta"
echo "$beta_output" | grep -q "channel: beta"
echo "$beta_output" | grep -q "djconnect-device-beta-v98.76.55.bin"
echo "$beta_output" | grep -q "firmware_manifest_beta.json"

beta_tag_output="$(./release.sh v98.76.56-beta --dry-run)"
echo "$beta_tag_output" | grep -q "tag:     v98.76.56-beta"
echo "$beta_tag_output" | grep -q "channel: beta"

echo "release script tests passed"
