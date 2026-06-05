#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

bash -n release.sh

dry_run_output="$(./release.sh 98.76.54 --dry-run)"
echo "$dry_run_output" | grep -q "version: 98.76.54"
echo "$dry_run_output" | grep -q "tag:     v98.76.54"
echo "$dry_run_output" | grep -q "Would build with SPOTIFYDJ_VERSION=98.76.54"
echo "$dry_run_output" | grep -q "Would commit, tag and push source repo"

set +e
invalid_output="$(./release.sh 98.76 --dry-run 2>&1)"
invalid_status=$?
set -e

if [[ "$invalid_status" -eq 0 ]]; then
  echo "invalid version unexpectedly succeeded" >&2
  exit 1
fi
echo "$invalid_output" | grep -q "version must be semantic"

echo "release script tests passed"
