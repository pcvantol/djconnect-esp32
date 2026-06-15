#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/update_build_dependencies.sh [environment ...]

Updates the PlatformIO build toolchain before firmware builds:

  - upgrades PlatformIO Core;
  - updates globally installed PlatformIO packages/tools;
  - updates project packages for each requested environment.

Environment variables:
  PIO_BIN     PlatformIO executable. Defaults to the local user install, then pio.
  PROJECT_DIR Project directory. Defaults to the current directory.
  REPORT_DIR  Optional directory for before/after package lists and a diff report.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

PROJECT_DIR="${PROJECT_DIR:-$(pwd)}"
REPORT_DIR="${REPORT_DIR:-}"
PIO_BIN="${PIO_BIN:-/Users/pcvantol/.platformio/penv/bin/pio}"
[[ -x "$PIO_BIN" ]] || PIO_BIN="pio"

if [[ "$#" -eq 0 ]]; then
  ENVIRONMENTS=(t_embed_cc1101 esp32_s3_box3)
else
  ENVIRONMENTS=("$@")
fi

[[ -f "$PROJECT_DIR/platformio.ini" ]] || {
  echo "No platformio.ini found in $PROJECT_DIR" >&2
  exit 1
}

echo "Updating DJConnect build dependencies"
echo "  project: $PROJECT_DIR"
echo "  pio:     $PIO_BIN"
echo "  envs:    ${ENVIRONMENTS[*]}"
if [[ -n "$REPORT_DIR" ]]; then
  mkdir -p "$REPORT_DIR"
  echo "  report:  $REPORT_DIR/build-dependencies.diff"
fi

write_package_report() {
  local output="$1"
  {
    echo "# Global packages"
    "$PIO_BIN" pkg list -g || true
    for environment in "${ENVIRONMENTS[@]}"; do
      echo
      echo "# Project packages: $environment"
      "$PIO_BIN" pkg list -d "$PROJECT_DIR" -e "$environment" || true
    done
  } >"$output"
}

if [[ -n "$REPORT_DIR" ]]; then
  write_package_report "$REPORT_DIR/build-dependencies-before.txt"
fi

echo "+ $PIO_BIN upgrade"
"$PIO_BIN" upgrade

echo "+ $PIO_BIN pkg update -g"
"$PIO_BIN" pkg update -g

for environment in "${ENVIRONMENTS[@]}"; do
  echo "+ $PIO_BIN pkg update -d $PROJECT_DIR -e $environment"
  "$PIO_BIN" pkg update -d "$PROJECT_DIR" -e "$environment"
done

if [[ -n "$REPORT_DIR" ]]; then
  write_package_report "$REPORT_DIR/build-dependencies-after.txt"
  if diff -u "$REPORT_DIR/build-dependencies-before.txt" "$REPORT_DIR/build-dependencies-after.txt" >"$REPORT_DIR/build-dependencies.diff"; then
    echo "Build dependency versions unchanged."
  else
    echo "Build dependency versions changed; review $REPORT_DIR/build-dependencies.diff."
    echo "Update THIRD_PARTY_NOTICES.md and DESIGN_DECISIONS.md before publishing the release."
  fi
fi
