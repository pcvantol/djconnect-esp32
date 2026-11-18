#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/extract_release_changelog.sh vX.Y.Z [CHANGELOG.md]

Prints the changelog body for the requested release tag. For beta tags, the
exact beta heading is preferred and the matching stable heading is used as a
fallback.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

tag="${1:-}"
changelog="${2:-CHANGELOG.md}"

if [[ -z "$tag" ]]; then
  usage >&2
  exit 2
fi

if [[ ! -f "$changelog" ]]; then
  echo "Changelog not found: $changelog" >&2
  exit 1
fi

base_tag="${tag%-beta}"
tmp_file="$(mktemp)"
trap 'rm -f "$tmp_file"' EXIT

extract_section() {
  local wanted="$1"
  awk -v wanted="$wanted" '
    BEGIN { in_section = 0; found = 0 }
    /^## / {
      if (in_section) {
        exit
      }
      if ($2 == wanted) {
        in_section = 1
        found = 1
        next
      }
    }
    in_section {
      print
    }
    END {
      if (!found) {
        exit 1
      }
    }
  ' "$changelog"
}

if ! extract_section "$tag" > "$tmp_file"; then
  if [[ "$base_tag" != "$tag" ]]; then
    extract_section "$base_tag" > "$tmp_file"
  else
    echo "No changelog section found for $tag in $changelog" >&2
    exit 1
  fi
fi

perl -0pi -e 's/\A\s+//; s/\s+\z/\n/' "$tmp_file"
if [[ ! -s "$tmp_file" ]]; then
  echo "Changelog section for $tag is empty in $changelog" >&2
  exit 1
fi

cat "$tmp_file"
