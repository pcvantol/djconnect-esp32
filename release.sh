#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  ./release.sh 3.0.0 [--channel stable|beta] [--dry-run] [--publish-firmware-repo <path>] [--gh-release]
  ./release.sh v3.0.0 [--channel stable|beta] [--dry-run] [--publish-firmware-repo <path>] [--gh-release]

Environment:
  PIO_BIN                 PlatformIO executable override.
  PIO_JOBS                PlatformIO build jobs per firmware environment, default 1.
  FIRMWARE_RELEASE_REPO   GitHub release repo, default pcvantol/djconnect-firmware.
  --min-ha-integration    Override default X.Y.0 integration requirement derived from the release version.
  --max-ha-integration    Override default X.(Y+1).0 exclusive integration ceiling derived from the release version.
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

default_min_ha_integration_for() {
  local version="$1"
  echo "${version%.*}.0"
}

default_max_ha_integration_for() {
  local version="$1"
  local major minor patch
  IFS=. read -r major minor patch <<< "$version"
  echo "$major.$((minor + 1)).0"
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

asset_name_for() {
  local board="$1"
  local version="$2"
  if [[ "$CHANNEL" == "beta" ]]; then
    case "$board" in
      t_embed_cc1101) echo "djconnect-lilygo-t-embed-s3-beta-v$version.bin" ;;
      *) fail "unknown release board: $board" ;;
    esac
  else
    case "$board" in
      t_embed_cc1101) echo "djconnect-lilygo-t-embed-s3-v$version.bin" ;;
      *) fail "unknown release board: $board" ;;
    esac
  fi
}

write_manifest() {
  local manifest="$1"
  local version="$2"
  local tag="$3"
  local lilygo_asset="$4"
  local lilygo_sha="$5"
  local lilygo_size="$6"
  cat > "$manifest" <<EOF
{
  "version": "$version",
  "version_tag": "$tag",
  "channel": "$CHANNEL",
  "min_ha_integration": "$MIN_HA_INTEGRATION",
  "max_ha_integration": "$MAX_HA_INTEGRATION",
  "firmwares": [
    {
      "board": "t_embed_cc1101",
      "device": "lilygo-t-embed-s3",
      "asset": "$lilygo_asset",
      "sha256": "$lilygo_sha",
      "size": $lilygo_size
    }
  ]
}
EOF
}

VERSION_ARG=""
DRY_RUN=false
GH_RELEASE=false
PUBLISH_FIRMWARE_REPO=""
MIN_HA_INTEGRATION=""
MAX_HA_INTEGRATION=""
CHANNEL="stable"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run)
      DRY_RUN=true
      shift
      ;;
    --gh-release)
      GH_RELEASE=true
      shift
      ;;
    --no-gh-release)
      GH_RELEASE=false
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
    --max-ha-integration)
      [[ $# -ge 2 ]] || fail "--max-ha-integration requires a version"
      MAX_HA_INTEGRATION="$2"
      shift 2
      ;;
    --channel)
      [[ $# -ge 2 ]] || fail "--channel requires stable or beta"
      CHANNEL="$2"
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
VERSION="${VERSION%-beta}"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "version must be semantic X.Y.Z, got '$VERSION_ARG'"
[[ "$CHANNEL" == "stable" || "$CHANNEL" == "beta" ]] || fail "channel must be stable or beta"
if [[ "$VERSION_ARG" == *-beta ]]; then
  CHANNEL="beta"
fi
if [[ -z "$MIN_HA_INTEGRATION" ]]; then
  MIN_HA_INTEGRATION="$(default_min_ha_integration_for "$VERSION")"
fi
if [[ -z "$MAX_HA_INTEGRATION" ]]; then
  MAX_HA_INTEGRATION="$(default_max_ha_integration_for "$VERSION")"
fi
TAG="v$VERSION"
MANIFEST="firmware_manifest.json"
if [[ "$CHANNEL" == "beta" ]]; then
  TAG="v$VERSION-beta"
  MANIFEST="firmware_manifest_beta.json"
fi
RELEASE_BOARDS=(t_embed_cc1101)
ASSET="$(asset_name_for t_embed_cc1101 "$VERSION")"
RELEASE_DIR="release"
FIRMWARE_RELEASE_REPO="${FIRMWARE_RELEASE_REPO:-pcvantol/djconnect-firmware}"
PIO_BIN="${PIO_BIN:-/Users/pcvantol/.platformio/penv/bin/pio}"
PIO_JOBS="${PIO_JOBS:-1}"
[[ -x "$PIO_BIN" ]] || PIO_BIN="pio"

[[ -f platformio.ini && -d src && -d include && -d .git ]] || fail "run this script from the djconnect-app repo root"

echo "DJConnect firmware release"
echo "  version: $VERSION"
echo "  tag:     $TAG"
echo "  channel: $CHANNEL"
echo "  min HA:  $MIN_HA_INTEGRATION"
echo "  max HA:  <$MAX_HA_INTEGRATION"
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
  echo "Would update and upgrade PlatformIO Core, global packages/tools and project packages for t_embed_cc1101 before building."
  echo "Would write $RELEASE_DIR/build-dependencies.diff and require THIRD_PARTY_NOTICES.md / DESIGN_DECISIONS.md review if dependency versions changed."
  echo "Would build t_embed_cc1101 with isolated PLATFORMIO_BUILD_DIR root and DJCONNECT_VERSION=$VERSION / DJCONNECT_VERSION_TAG=$TAG."
  echo "Would copy firmware to $RELEASE_DIR/$ASSET and write $RELEASE_DIR/$MANIFEST."
  echo "Would commit, tag and push source repo."
  if [[ -n "$PUBLISH_FIRMWARE_REPO" ]]; then
    echo "Would publish assets to firmware repo path: $PUBLISH_FIRMWARE_REPO."
  fi
  echo "Would push tag $TAG."
  if [[ -n "$PUBLISH_FIRMWARE_REPO" ]]; then
    echo "Would update the public firmware repo; verify or create the GitHub firmware release after publish."
  else
    echo "If GitHub Actions release publishing is configured, the pushed tag may publish public firmware assets."
  fi
  if [[ "$GH_RELEASE" == "true" ]]; then
    echo "Would also create GitHub release in $FIRMWARE_RELEASE_REPO locally because --gh-release was passed."
    if [[ "$CHANNEL" == "beta" ]]; then
      echo "Would mark the GitHub release as a prerelease."
    fi
  fi
  exit 0
fi

replace_version_examples "$VERSION" "$TAG"

export DJCONNECT_BUILD_FLAGS="-DDJCONNECT_VERSION=$VERSION -DDJCONNECT_VERSION_TAG=$TAG -DDJCONNECT_RELEASE_BUILD=1 -Os"
mkdir -p "$RELEASE_DIR"
RELEASE_BUILD_ROOT=".pio/build-release/$TAG"
rm -rf "$RELEASE_BUILD_ROOT"
mkdir -p "$RELEASE_BUILD_ROOT"

echo "+ REPORT_DIR=$RELEASE_DIR scripts/update_build_dependencies.sh ${RELEASE_BOARDS[*]}"
REPORT_DIR="$RELEASE_DIR" scripts/update_build_dependencies.sh "${RELEASE_BOARDS[@]}"

LILYGO_ASSET=""
LILYGO_SHA256=""
LILYGO_SIZE=""

build_firmware_board() {
  local board="$1"
  local build_root="$RELEASE_BUILD_ROOT/$board"
  local log_file="$RELEASE_DIR/build-$board.log"
  echo "+ PLATFORMIO_BUILD_DIR=$build_root $PIO_BIN run -e $board -j $PIO_JOBS"
  (
    export PLATFORMIO_BUILD_DIR="$build_root"
    "$PIO_BIN" run -e "$board" -j "$PIO_JOBS"
  ) >"$log_file" 2>&1
}

BUILD_PIDS=()
BUILD_PID_BOARDS=()
for board in "${RELEASE_BOARDS[@]}"; do
  build_firmware_board "$board" &
  BUILD_PIDS+=("$!")
  BUILD_PID_BOARDS+=("$board")
done

BUILD_FAILED=false
for index in "${!BUILD_PIDS[@]}"; do
  pid="${BUILD_PIDS[$index]}"
  board="${BUILD_PID_BOARDS[$index]}"
  if wait "$pid"; then
    echo "Build completed: $board"
  else
    BUILD_FAILED=true
    echo "Build failed: $board" >&2
    echo "---- $RELEASE_DIR/build-$board.log tail ----" >&2
    tail -n 80 "$RELEASE_DIR/build-$board.log" >&2 || true
  fi
done

if [[ "$BUILD_FAILED" == "true" ]]; then
  fail "one or more firmware builds failed"
fi

for board in "${RELEASE_BOARDS[@]}"; do
  asset="$(asset_name_for "$board" "$VERSION")"
  firmware_bin="$RELEASE_BUILD_ROOT/$board/$board/firmware.bin"
  [[ -f "$firmware_bin" ]] || fail "no PlatformIO firmware.bin found for $board"
  run cp "$firmware_bin" "$RELEASE_DIR/$asset"
  asset_sha="$(sha256_file "$RELEASE_DIR/$asset")"
  asset_size="$(file_size "$RELEASE_DIR/$asset")"
  printf '%s  %s\n' "$asset_sha" "$asset" > "$RELEASE_DIR/$asset.sha256"
  case "$board" in
    t_embed_cc1101)
      LILYGO_ASSET="$asset"
      LILYGO_SHA256="$asset_sha"
      LILYGO_SIZE="$asset_size"
      ;;
  esac
done

write_manifest \
  "$RELEASE_DIR/$MANIFEST" \
  "$VERSION" \
  "$TAG" \
  "$LILYGO_ASSET" \
  "$LILYGO_SHA256" \
  "$LILYGO_SIZE"

run git add .
if git diff --cached --quiet; then
  echo "No source changes to commit for $TAG; continuing with tag creation."
else
  run git commit -m "Release DJConnect firmware $TAG"
fi
run git tag "$TAG"
run git push origin main
run git push origin "$TAG"

if [[ -n "$PUBLISH_FIRMWARE_REPO" ]]; then
  [[ -d "$PUBLISH_FIRMWARE_REPO" ]] || fail "firmware repo path does not exist: $PUBLISH_FIRMWARE_REPO"
  [[ -d "$PUBLISH_FIRMWARE_REPO/.git" ]] || fail "firmware repo path is not a git repo: $PUBLISH_FIRMWARE_REPO"
  for board in "${RELEASE_BOARDS[@]}"; do
    asset="$(asset_name_for "$board" "$VERSION")"
    run cp "$RELEASE_DIR/$asset" "$PUBLISH_FIRMWARE_REPO/$asset"
    run cp "$RELEASE_DIR/$asset.sha256" "$PUBLISH_FIRMWARE_REPO/$asset.sha256"
  done
  run cp "$RELEASE_DIR/$MANIFEST" "$PUBLISH_FIRMWARE_REPO/$MANIFEST"
  run_in_dir "$PUBLISH_FIRMWARE_REPO" git add "$LILYGO_ASSET" "$LILYGO_ASSET.sha256" "$MANIFEST"
  run_in_dir "$PUBLISH_FIRMWARE_REPO" git commit -m "Release DJConnect firmware $TAG"
  if ! (cd "$PUBLISH_FIRMWARE_REPO" && git rev-parse "$TAG" >/dev/null 2>&1); then
    run_in_dir "$PUBLISH_FIRMWARE_REPO" git tag "$TAG"
  fi
  run_in_dir "$PUBLISH_FIRMWARE_REPO" git push origin main
  run_in_dir "$PUBLISH_FIRMWARE_REPO" git push origin "$TAG"
fi

if [[ "$GH_RELEASE" == "true" ]]; then
  if command -v gh >/dev/null 2>&1; then
    GH_RELEASE_ARGS=(release create "$TAG" --repo "$FIRMWARE_RELEASE_REPO" --title "DJConnect firmware $TAG")
    if [[ "$CHANNEL" == "beta" ]]; then
      GH_RELEASE_ARGS+=(--prerelease)
    fi
    RELEASE_NOTES_FILE="$RELEASE_DIR/release-notes.md"
    if [[ "$DRY_RUN" == "false" ]]; then
      scripts/extract_release_changelog.sh "$TAG" CHANGELOG.md > "$RELEASE_NOTES_FILE"
    fi
    GH_RELEASE_ARGS+=(--notes-file "$RELEASE_NOTES_FILE")
    GH_RELEASE_ARGS+=(
      "$RELEASE_DIR/$LILYGO_ASSET"
      "$RELEASE_DIR/$LILYGO_ASSET.sha256"
      "$RELEASE_DIR/$MANIFEST"
    )
    run gh "${GH_RELEASE_ARGS[@]}"
  else
    echo "gh not found; skipping GitHub release creation"
  fi
else
  if [[ -n "$PUBLISH_FIRMWARE_REPO" ]]; then
    echo "Skipping local GitHub release creation; public firmware repo assets were pushed. Verify or create the GitHub firmware release."
  else
    echo "Skipping local GitHub release creation. If GitHub Actions release publishing is configured, verify that the pushed tag published firmware assets."
  fi
fi

echo "Release prepared: $TAG"
