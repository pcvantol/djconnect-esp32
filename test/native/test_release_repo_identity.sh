#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
lab="$(mktemp -d -t djc-release-repo-test.XXXXXXXX)"
trap 'rm -rf "$lab"' EXIT
cat > "$lab/gh" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
[[ "$#" == 4 && "$1" == api && "$2" == repositories/1258610489 && "$3" == --jq ]] || exit 2
jq -r "$4" <<< "$REPOSITORY_FIXTURE"
MOCK
chmod +x "$lab/gh"
export PATH="$lab:$PATH"
accept='{"id":1258610489,"visibility":"public","full_name":"djconnect-music-dj/djconnect-firmware"}'
export REPOSITORY_FIXTURE="$accept"
[[ "$(bash "$root/scripts/resolve_firmware_release_repo.sh")" == djconnect-music-dj/djconnect-firmware ]]
for fixture in '{"id":1,"visibility":"public","full_name":"djconnect-music-dj/djconnect-firmware"}' '{"id":1258610489,"visibility":"private","full_name":"djconnect-music-dj/djconnect-firmware"}' '{"id":1258610489,"visibility":"public","full_name":"pcvantol/djconnect-firmware"}' '{"id":1258610489,"visibility":"public","full_name":"other/djconnect-firmware"}'; do
  export REPOSITORY_FIXTURE="$fixture"
  if bash "$root/scripts/resolve_firmware_release_repo.sh" >/dev/null 2>&1; then echo 'Rejected distribution identity was accepted' >&2; exit 1; fi
done
printf '%s\n' 'Firmware distribution identity tests passed'
