#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

bash -n release.sh
bash -n scripts/cleanup_old_releases.sh
bash -n scripts/capture_device_screens.sh
bash -n scripts/extract_release_changelog.sh
bash -n scripts/update_build_dependencies.sh
test -s DESIGN_DECISIONS.md
grep -q "Technical Design Decisions" DESIGN_DECISIONS.md
grep -q "Frameworks, Libraries And Third-Party Dependencies" DESIGN_DECISIONS.md
grep -q "Release Maintenance Rule" DESIGN_DECISIONS.md
grep -q "embedded OTA TLS CA/certificate" README.md
grep -q "GitHub OTA TLS CA/certificate bundle" AGENTS.md
grep -q "pcvantol/djconnect/SYNC_PROMPTS.md" README.md
grep -q "pcvantol/djconnect/SYNC_PROMPTS.md" AGENTS.md
grep -q "pcvantol/djconnect/SYNC_PROMPTS.md" DESIGN_DECISIONS.md
grep -q "GitHub OTA CA/certificate bundle" DESIGN_DECISIONS.md
release_notes_output="$(scripts/extract_release_changelog.sh v3.1.21 CHANGELOG.md)"
for old_prompt in SYNC_PROMPTS.md HA_SYNC_PROMPT.md ESP_SYNC_PROMPT.md IOS_MACOS_APP_HANDOFF.md APPLE_APP_SYNC_PROMPTS.md docs/SYNC_PROMPTS.md; do
  if [ -e "$old_prompt" ]; then
    echo "sync prompts are canonical only in pcvantol/djconnect/SYNC_PROMPTS.md; remove $old_prompt" >&2
    exit 1
  fi
done
echo "$release_notes_output" | grep -q "Stability and web portal polish release"
echo "$release_notes_output" | grep -q "Delayed automatic playback polling after boot/pairing"
if echo "$release_notes_output" | grep -q "## v3.1.20"; then
  echo "release changelog extraction included the next release heading" >&2
  exit 1
fi
test -s PRODUCT_ROADMAP.md
grep -q "Release Cycle Rule" PRODUCT_ROADMAP.md
grep -q "Production Release Must-Haves" PRODUCT_ROADMAP.md
grep -q "Killer Features" PRODUCT_ROADMAP.md
grep -q "Premium / Paid Feature Candidates" PRODUCT_ROADMAP.md
grep -q 'DefaultHomeAssistantUrl = "http://homeassistant.local:8123"' include/Config.h
grep -q "DefaultHomeAssistantUrl" src/DisplayManager.cpp
grep -q "http://homeassistant.local:8123" README.md

# Repository hygiene: generated release assets must stay out of source control.
git check-ignore -q release/djconnect-lilygo-t-embed-s3-v3.0.0.bin
if git ls-files --error-unmatch release/firmware_manifest.json >/dev/null 2>&1; then
  echo "release artifacts must not be tracked in the source repo" >&2
  exit 1
fi

# Asset snapshots catch stale branding in the embedded display and web portal resources.
echo "8b3f3962f3a842893cabad2f4903f9a76bdced467f1dc66ed7398db9b527308a  assets/esp_display/embedded/djconnect_icon_160x160.rgb565" | shasum -a 256 -c -
echo "d10eb3ed736e2c704e998bdc8fde2e34852e684822139553f68fe91bcc45860b  assets/esp_display/embedded/djconnect_icon_170x170.rgb565" | shasum -a 256 -c -
echo "c990edc52a6957ae399411a809819c394a1d829c13745f871ec2444a92c017e5  assets/esp_display/embedded/djconnect_splash_170x320.rgb565" | shasum -a 256 -c -
echo "7ac9db165cb7d5578838bac5475524634cd070ceb44fbd69685cd1d6ba8e9f3c  assets/website/site.webmanifest" | shasum -a 256 -c -
echo "97d1a6567cc3c61d8cdb4c2ca05ed37adfd6bdc112efeb90177c4bba7095f476  assets/website/icon-192.png" | shasum -a 256 -c -
echo "34e667173f5fe834f9282d11ab5a1f4f5fa2225010388f50a6f5bf7d72e2d460  assets/website/favicon-32.png" | shasum -a 256 -c -

# The DJConnect rebrand should not regress to old product names or old 2.x firmware assets.
if rg -n --glob "!test/native/test_release.sh" "SpotifyDJ|spotifydj|spotify_dj|SPOTIFYDJ|Spotify DJ|spotify-dj|/api/spotify_dj|X-SpotifyDJ|set_play_mode|\bha_url\b|djconnect-[0-9A-Fa-f]{12}" \
  README.md DESIGN_DECISIONS.md CHANGELOG.md AGENTS.md HANDOFF.md TODO.md LICENSE THIRD_PARTY_NOTICES.md src include test postman .github release.sh scripts; then
  echo "old product/endpoint reference found" >&2
  exit 1
fi

if rg -n --glob "!test/native/test_release.sh" "spotifydj-device|releases/download/v2\.[0-9]+\.[0-9]+|tag/v2\.[0-9]+\.[0-9]+|Release .*v2\.[0-9]+\.[0-9]+|firmware v2\.[0-9]+\.[0-9]+" \
  README.md DESIGN_DECISIONS.md CHANGELOG.md AGENTS.md HANDOFF.md TODO.md LICENSE THIRD_PARTY_NOTICES.md src include test postman .github release.sh scripts; then
  echo "old 2.x firmware release reference found" >&2
  exit 1
fi

if rg -n --glob "!test/native/test_release.sh" "djconnect-device-v|djconnect-device-esp32-s3-box-3|\"asset\": \"djconnect-device|\"url\": \"https://github.com/pcvantol/djconnect-firmware/releases/download/.*/djconnect-device" \
  README.md DESIGN_DECISIONS.md CHANGELOG.md AGENTS.md HANDOFF.md TODO.md LICENSE THIRD_PARTY_NOTICES.md src include test postman .github release.sh scripts; then
  echo "old single-device firmware asset reference found" >&2
  exit 1
fi

dry_run_output="$(./release.sh 98.76.54 --dry-run)"
echo "$dry_run_output" | grep -q "version: 98.76.54"
echo "$dry_run_output" | grep -q "tag:     v98.76.54"
echo "$dry_run_output" | grep -q "channel: stable"
echo "$dry_run_output" | grep -q "min HA:  98.76.0"
echo "$dry_run_output" | grep -q "max HA:  <98.77.0"
echo "$dry_run_output" | grep -q "djconnect-lilygo-t-embed-s3-v98.76.54.bin"
echo "$dry_run_output" | grep -q "djconnect-esp32-s3-box-3-v98.76.54.bin"
echo "$dry_run_output" | grep -q "Would update and upgrade PlatformIO Core, global packages/tools and project packages"
echo "$dry_run_output" | grep -q "Would write release/build-dependencies.diff and require THIRD_PARTY_NOTICES.md / DESIGN_DECISIONS.md review"
echo "$dry_run_output" | grep -q "Would build t_embed_cc1101 and esp32_s3_box3 in parallel with isolated PLATFORMIO_BUILD_DIR roots"
echo "$dry_run_output" | grep -q "DJCONNECT_VERSION=98.76.54 / DJCONNECT_VERSION_TAG=v98.76.54"
echo "$dry_run_output" | grep -q "Would commit, tag and push source repo"
echo "$dry_run_output" | grep -q "GitHub Actions will build/publish"
grep -q '"$RELEASE_DIR/$LILYGO_ASSET"' release.sh
grep -q '"$RELEASE_DIR/$BOX3_ASSET"' release.sh
grep -q '"$RELEASE_DIR/$MANIFEST"' release.sh
grep -q 'scripts/update_build_dependencies.sh "${RELEASE_BOARDS\[@\]}"' release.sh
grep -q 'scripts/update_build_dependencies.sh "${{ matrix.env }}"' .github/workflows/release-firmware.yml
grep -q 'scripts/extract_release_changelog.sh "${{ needs.release-info.outputs.version_tag }}" CHANGELOG.md > release-notes.md' .github/workflows/release-firmware.yml
grep -q 'body_path: release-notes.md' .github/workflows/release-firmware.yml
grep -q 'release/${{ needs.release-info.outputs.lilygo_bin }}' .github/workflows/release-firmware.yml
grep -q 'release/${{ needs.release-info.outputs.box3_bin }}' .github/workflows/release-firmware.yml
grep -q 'release/${{ needs.release-info.outputs.manifest }}' .github/workflows/release-firmware.yml
if grep -q '^[[:space:]]*release/\*$' .github/workflows/release-firmware.yml; then
  echo "GitHub release workflow must not publish every generated release artifact" >&2
  exit 1
fi
grep -q 'scripts/extract_release_changelog.sh "$TAG" CHANGELOG.md > "$RELEASE_NOTES_FILE"' release.sh
grep -q -- '--notes-file "$RELEASE_NOTES_FILE"' release.sh
grep -q 'THIRD_PARTY_NOTICES.md and DESIGN_DECISIONS.md before publishing' scripts/update_build_dependencies.sh
if grep -q 'GH_RELEASE_ARGS+=(.*"$RELEASE_DIR"/\\*)' release.sh; then
  echo "GitHub release upload must not glob every local release artifact" >&2
  exit 1
fi

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
echo "$beta_output" | grep -q "min HA:  98.76.0"
echo "$beta_output" | grep -q "max HA:  <98.77.0"
echo "$beta_output" | grep -q "djconnect-lilygo-t-embed-s3-beta-v98.76.55.bin"
echo "$beta_output" | grep -q "djconnect-esp32-s3-box-3-beta-v98.76.55.bin"
echo "$beta_output" | grep -q "firmware_manifest_beta.json"

beta_prerelease_output="$(./release.sh 98.76.57 --channel beta --gh-release --dry-run)"
echo "$beta_prerelease_output" | grep -q "Would mark the GitHub release as a prerelease"

beta_tag_output="$(./release.sh v98.76.56-beta --dry-run)"
echo "$beta_tag_output" | grep -q "tag:     v98.76.56-beta"
echo "$beta_tag_output" | grep -q "channel: beta"

override_output="$(./release.sh 98.76.58 --min-ha-integration 98.76.2 --max-ha-integration 98.77.3 --dry-run)"
echo "$override_output" | grep -q "min HA:  98.76.2"
echo "$override_output" | grep -q "max HA:  <98.77.3"

fake_gh_dir="$(mktemp -d)"
trap 'rm -rf "$fake_gh_dir"' EXIT
cat > "$fake_gh_dir/gh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
args="$*"
if [[ "$1 $2" == "release list" ]]; then
  if [[ "$args" == *"-beta"* ]]; then
    printf 'v1.0.0-beta\tfalse\ttrue\n'
    printf 'v1.1.0-beta\tfalse\ttrue\n'
  else
    printf 'v1.0.0\tfalse\tfalse\n'
    printf 'v1.1.0\tfalse\tfalse\n'
  fi
elif [[ "$1" == "api" ]]; then
  if [[ "$args" == *"-beta"* ]]; then
    printf 'v1.0.0-beta\n'
    printf 'v1.1.0-beta\n'
  else
    printf 'v1.0.0\n'
    printf 'v1.1.0\n'
  fi
elif [[ "$1 $2" == "run list" ]]; then
  printf '100\t2026-01-02T00:00:00Z\tcompleted\tsuccess\tmain\tRelease firmware\n'
  printf '99\t2026-01-01T00:00:00Z\tcompleted\tsuccess\tmain\tRelease firmware\n'
else
  echo "unexpected fake gh call: $*" >&2
  exit 1
fi
EOF
chmod +x "$fake_gh_dir/gh"
cleanup_beta_output="$(PATH="$fake_gh_dir:$PATH" scripts/cleanup_old_releases.sh --repo owner/repo --channel beta --keep 1 --dry-run)"
echo "$cleanup_beta_output" | grep -q "Channel: beta"
echo "$cleanup_beta_output" | grep -q "v1.1.0-beta"
echo "$cleanup_beta_output" | grep -q "v1.0.0-beta"

echo "release script tests passed"
