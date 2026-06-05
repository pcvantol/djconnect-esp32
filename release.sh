#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  ./release.sh 2.3.0 [--dry-run] [--publish-firmware-repo <path>] [--no-gh-release]
  ./release.sh v2.3.0 [--dry-run] [--publish-firmware-repo <path>] [--no-gh-release]

Environment:
  PIO_BIN                 PlatformIO executable override.
  FIRMWARE_RELEASE_REPO   GitHub release repo, default pcvantol/spotify-dj-firmware.
EOF
}

fail() {
  echo "release.sh: $*" >&2
  exit 1
}

run() {
  echo "+ $*"
  if [[ "$DRY_RUN" == "false" ]]; then
    "$@"
  fi
}

run_in_dir() {
  local dir="$1"
  shift
  echo "+ (cd $dir && $*)"
  if [[ "$DRY_RUN" == "false" ]]; then
    (cd "$dir" && "$@")
  fi
}

sha256_file() {
  local file="$1"
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$file" | awk '{print $1}'
  elif command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$file" | awk '{print $1}'
  else
    fail "neither shasum nor sha256sum is available"
  fi
}

file_size() {
  local file="$1"
  if stat -f%z "$file" >/dev/null 2>&1; then
    stat -f%z "$file"
  else
    stat -c%s "$file"
  fi
}

replace_version_examples() {
  local version="$1"
  local tag="$2"
  local files=()
  local current_version=""
  local current_tag=""

  if [[ -f CHANGELOG.md ]]; then
    current_tag="$(grep -E '^## v[0-9]+\.[0-9]+\.[0-9]+' CHANGELOG.md | head -n 1 | awk '{print $2}' || true)"
    current_version="${current_tag#v}"
  fi

  [[ -f platformio.ini ]] && files+=(platformio.ini)
  while IFS= read -r file; do files+=("$file"); done < <(find src include -maxdepth 1 -type f \( -iname 'version.*' -o -iname '*version*.*' \) 2>/dev/null || true)
  [[ -f README.md ]] && files+=(README.md)
  [[ -f CHANGELOG.md ]] && files+=(CHANGELOG.md)
  [[ -f AGENTS.md ]] && files+=(AGENTS.md)

  if [[ ${#files[@]} -eq 0 ]]; then
    return
  fi

  if [[ -n "$current_version" && "$current_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    run perl -0pi -e "s/\\Q$current_tag\\E/$tag/g; s/(?<!v)\\b\\Q$current_version\\E\\b/$version/g;" "${files[@]}"
  else
    echo "No current release version found in CHANGELOG.md; skipping docs/example version replacement."
  fi
}

write_manifest() {
  local manifest="$1"
  local version="$2"
  local asset="$3"
  local sha="$4"
  local size="$5"
  cat > "$manifest" <<EOF
{
  "version": "$version",
  "device": "spotifydj-device",
  "asset": "$asset",
  "sha256": "$sha",
  "size": $size,
  "min_ha_integration": "$MIN_HA_INTEGRATION"
}
EOF
}

VERSION_ARG=""
DRY_RUN=false
NO_GH_RELEASE=false
PUBLISH_FIRMWARE_REPO=""
MIN_HA_INTEGRATION="1.0.0"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run)
      DRY_RUN=true
      shift
      ;;
    --no-gh-release)
      NO_GH_RELEASE=true
      shift
      ;;
    --publish-firmware-repo)
      [[ $# -ge 2 ]] || fail "--publish-firmware-repo requires a path"
      PUBLISH_FIRMWARE_REPO="$2"
      shift 2
      ;;
    --min-ha-integration)
      [[ $# -ge 2 ]] || fail "--min-ha-integration requires a version"
      MIN_HA_INTEGRATION="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    -*)
      fail "unknown option: $1"
      ;;
    *)
      [[ -z "$VERSION_ARG" ]] || fail "only one version argument is allowed"
      VERSION_ARG="$1"
      shift
      ;;
  esac
done

[[ -n "$VERSION_ARG" ]] || { usage; exit 1; }

VERSION="${VERSION_ARG#v}"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "version must be semantic X.Y.Z, got '$VERSION_ARG'"
TAG="v$VERSION"
ASSET="spotifydj-device-$TAG.bin"
MANIFEST="firmware_manifest.json"
RELEASE_DIR="release"
FIRMWARE_RELEASE_REPO="${FIRMWARE_RELEASE_REPO:-pcvantol/spotify-dj-firmware}"
PIO_BIN="${PIO_BIN:-/Users/pcvantol/.platformio/penv/bin/pio}"
[[ -x "$PIO_BIN" ]] || PIO_BIN="pio"

[[ -f platformio.ini && -d src && -d include && -d .git ]] || fail "run this script from the spotify-dj-app repo root"

echo "SpotifyDJ firmware release"
echo "  version: $VERSION"
echo "  tag:     $TAG"
echo "  asset:   $ASSET"
echo "  dry-run: $DRY_RUN"

if git rev-parse "$TAG" >/dev/null 2>&1; then
  fail "local tag already exists: $TAG"
fi

if [[ "$DRY_RUN" == "false" ]]; then
  if git ls-remote --exit-code --tags origin "refs/tags/$TAG" >/dev/null 2>&1; then
    fail "remote tag already exists on origin: $TAG"
  fi
fi

if [[ "$DRY_RUN" == "true" ]]; then
  echo "Would update release examples/version references in platformio.ini, version files, README, CHANGELOG and AGENTS if present."
  echo "Would build with SPOTIFYDJ_VERSION=$VERSION and SPOTIFYDJ_VERSION_TAG=$TAG."
  echo "Would copy firmware to $RELEASE_DIR/$ASSET and write $RELEASE_DIR/$MANIFEST."
  echo "Would commit, tag and push source repo."
  if [[ -n "$PUBLISH_FIRMWARE_REPO" ]]; then
    echo "Would publish assets to firmware repo path: $PUBLISH_FIRMWARE_REPO."
  fi
  if [[ "$NO_GH_RELEASE" == "false" ]]; then
    echo "Would create GitHub release in $FIRMWARE_RELEASE_REPO if gh is available."
  fi
  exit 0
fi

replace_version_examples "$VERSION" "$TAG"

export SPOTIFYDJ_BUILD_FLAGS="-DSPOTIFYDJ_VERSION=\\\"$VERSION\\\" -DSPOTIFYDJ_VERSION_TAG=\\\"$TAG\\\""
run "$PIO_BIN" run -e t_embed_cc1101

FIRMWARE_BIN="$(find .pio/build -path '*/firmware.bin' -type f | sort | head -n 1)"
[[ -n "$FIRMWARE_BIN" ]] || fail "no PlatformIO firmware.bin found"

mkdir -p "$RELEASE_DIR"
run cp "$FIRMWARE_BIN" "$RELEASE_DIR/$ASSET"
SHA256="$(sha256_file "$RELEASE_DIR/$ASSET")"
SIZE="$(file_size "$RELEASE_DIR/$ASSET")"
write_manifest "$RELEASE_DIR/$MANIFEST" "$VERSION" "$ASSET" "$SHA256" "$SIZE"

run git add .
run git commit -m "Release SpotifyDJ firmware $TAG"
run git tag "$TAG"
run git push origin main
run git push origin "$TAG"

if [[ -n "$PUBLISH_FIRMWARE_REPO" ]]; then
  [[ -d "$PUBLISH_FIRMWARE_REPO" ]] || fail "firmware repo path does not exist: $PUBLISH_FIRMWARE_REPO"
  [[ -d "$PUBLISH_FIRMWARE_REPO/.git" ]] || fail "firmware repo path is not a git repo: $PUBLISH_FIRMWARE_REPO"
  run cp "$RELEASE_DIR/$ASSET" "$PUBLISH_FIRMWARE_REPO/$ASSET"
  run cp "$RELEASE_DIR/$MANIFEST" "$PUBLISH_FIRMWARE_REPO/$MANIFEST"
  run_in_dir "$PUBLISH_FIRMWARE_REPO" git add "$ASSET" "$MANIFEST"
  run_in_dir "$PUBLISH_FIRMWARE_REPO" git commit -m "Release SpotifyDJ firmware $TAG"
  if ! (cd "$PUBLISH_FIRMWARE_REPO" && git rev-parse "$TAG" >/dev/null 2>&1); then
    run_in_dir "$PUBLISH_FIRMWARE_REPO" git tag "$TAG"
  fi
  run_in_dir "$PUBLISH_FIRMWARE_REPO" git push origin main
  run_in_dir "$PUBLISH_FIRMWARE_REPO" git push origin "$TAG"
fi

if [[ "$NO_GH_RELEASE" == "false" ]]; then
  if command -v gh >/dev/null 2>&1; then
    run gh release create "$TAG" \
      --repo "$FIRMWARE_RELEASE_REPO" \
      --title "SpotifyDJ firmware $TAG" \
      "$RELEASE_DIR/$ASSET" "$RELEASE_DIR/$MANIFEST"
  else
    echo "gh not found; skipping GitHub release creation"
  fi
fi

echo "Release prepared: $TAG"
