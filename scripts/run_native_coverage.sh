#!/usr/bin/env bash
set -euo pipefail

# Produce a Cobertura report for the host-native firmware logic suite.  This
# intentionally covers only production headers exercised by test_logic.cpp;
# Arduino/ESP-IDF hardware modules remain verified by the PlatformIO build and
# live ESP qualification rather than being misrepresented as host coverage.

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR=${DJCONNECT_NATIVE_COVERAGE_BUILD_DIR:-"${ROOT_DIR}/.coverage/native"}
REPORT_PATH=${DJCONNECT_NATIVE_COVERAGE_REPORT:-"${ROOT_DIR}/release/native-coverage/djconnect-esp32-native-coverage.xml"}
COMPILER=${CXX:-c++}
GCOV_TOOL=${DJCONNECT_NATIVE_COVERAGE_GCOV:-gcov}

if ! command -v "${COMPILER}" >/dev/null 2>&1; then
  echo "C++ compiler not found: ${COMPILER}" >&2
  exit 2
fi
if ! command -v gcovr >/dev/null 2>&1; then
  echo "gcovr is required (install with: python3 -m pip install gcovr)" >&2
  exit 2
fi

mkdir -p "${BUILD_DIR}" "$(dirname "${REPORT_PATH}")"
"${COMPILER}" -std=c++17 --coverage -O0 -g \
  -I"${ROOT_DIR}/include" \
  -I"${ROOT_DIR}/.pio/libdeps/t_embed_cc1101/ArduinoJson/src" \
  "${ROOT_DIR}/test/native/test_logic.cpp" \
  -o "${BUILD_DIR}/djconnect_native_coverage_tests"
"${BUILD_DIR}/djconnect_native_coverage_tests"

gcovr \
  --root "${ROOT_DIR}" \
  --object-directory "${BUILD_DIR}" \
  --gcov-executable "${GCOV_TOOL}" \
  --filter "${ROOT_DIR}/include/(LogicHelpers|DeviceCommandParser|NetworkActivityLogic|DJConnectMenuModel|PlaybackResponseParser)\\.h" \
  --exclude "${ROOT_DIR}/test/.*" \
  --cobertura-pretty \
  --output "${REPORT_PATH}"

echo "Native ESP coverage report: ${REPORT_PATH}"
