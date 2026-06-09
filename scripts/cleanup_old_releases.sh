#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/cleanup_old_releases.sh --repo OWNER/REPO [options]

Deletes old GitHub releases, matching git tags and old workflow runs, keeping
the newest semantic version releases and newest workflow runs by default.

Options:
  --repo OWNER/REPO       GitHub repository, for example pcvantol/djconnect-firmware.
  --keep N                Number of newest semver releases/tags to keep. Default: 1.
                          Use 0 to delete all semver releases/tags.
  --keep-runs N           Number of newest workflow runs to keep. Defaults to --keep.
                          Use 0 to delete all listed workflow runs.
  --dry-run               Show what would be deleted without changing anything.
  --execute               Actually delete releases, tags and workflow runs.
  --yes                   Do not prompt before destructive deletion.
  -h, --help              Show this help.

Examples:
  scripts/cleanup_old_releases.sh --repo pcvantol/djconnect-firmware --dry-run
  scripts/cleanup_old_releases.sh --repo pcvantol/djconnect-firmware --keep 2 --execute
  scripts/cleanup_old_releases.sh --repo pcvantol/djconnect-firmware --keep 1 --keep-runs 5 --execute

Notes:
  - Requires GitHub CLI: gh auth login
  - Only semver tags/releases like v2.7.3 are considered.
  - The kept releases are selected by semantic version, not by upload date.
  - Workflow runs are kept by newest created date.
EOF
}

repo=""
keep_count=1
keep_runs=""
mode="dry-run"
assume_yes=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo)
      repo="${2:-}"
      shift 2
      ;;
    --keep)
      keep_count="${2:-}"
      shift 2
      ;;
    --keep-runs)
      keep_runs="${2:-}"
      shift 2
      ;;
    --dry-run)
      mode="dry-run"
      shift
      ;;
    --execute)
      mode="execute"
      shift
      ;;
    --yes)
      assume_yes=true
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ -z "$repo" || ! "$repo" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]]; then
  echo "Error: --repo OWNER/REPO is required." >&2
  exit 2
fi

if [[ ! "$keep_count" =~ ^[0-9]+$ ]]; then
  echo "Error: --keep must be zero or a positive integer." >&2
  exit 2
fi

if [[ -z "$keep_runs" ]]; then
  keep_runs="$keep_count"
fi

if [[ ! "$keep_runs" =~ ^[0-9]+$ ]]; then
  echo "Error: --keep-runs must be zero or a positive integer." >&2
  exit 2
fi

if ! command -v gh >/dev/null 2>&1; then
  echo "Error: GitHub CLI 'gh' is required." >&2
  exit 1
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

releases_file="$tmp_dir/releases.tsv"
tags_file="$tmp_dir/tags.txt"
keep_file="$tmp_dir/keep.txt"
delete_releases_file="$tmp_dir/delete_releases.tsv"
delete_tags_file="$tmp_dir/delete_tags.txt"
workflow_runs_file="$tmp_dir/workflow_runs.tsv"
delete_workflow_runs_file="$tmp_dir/delete_workflow_runs.tsv"

echo "Reading releases from $repo..."
gh release list --repo "$repo" --limit 1000 --json tagName,isDraft,isPrerelease \
  --jq '.[] | select(.tagName | test("^v[0-9]+\\.[0-9]+\\.[0-9]+$")) | [.tagName, .isDraft, .isPrerelease] | @tsv' \
  > "$releases_file"

echo "Reading tags from $repo..."
gh api "repos/$repo/git/matching-refs/tags/v" --paginate \
  --jq '.[]?.ref | sub("^refs/tags/"; "") | select(test("^v[0-9]+\\.[0-9]+\\.[0-9]+$"))' \
  > "$tags_file"

all_versions_file="$tmp_dir/all_versions.txt"
{
  cut -f1 "$releases_file"
  cat "$tags_file"
} | sort -uV > "$all_versions_file"

total_versions="$(wc -l < "$all_versions_file" | tr -d ' ')"
if [[ "$keep_count" -eq 0 ]]; then
  : > "$keep_file"
elif [[ "$total_versions" -eq 0 ]]; then
  : > "$keep_file"
else
  tail -n "$keep_count" "$all_versions_file" > "$keep_file"
fi

awk 'NR==FNR { keep[$1]=1; next } !($1 in keep)' "$keep_file" "$releases_file" > "$delete_releases_file"
awk 'NR==FNR { keep[$1]=1; next } !($1 in keep)' "$keep_file" "$tags_file" > "$delete_tags_file"

echo "Reading workflow runs from $repo..."
gh run list --repo "$repo" --limit 1000 \
  --json databaseId,displayTitle,headBranch,createdAt,status,conclusion \
  --jq '.[] | [.databaseId, .createdAt, .status, (.conclusion // ""), .headBranch, .displayTitle] | @tsv' \
  > "$workflow_runs_file"

if [[ "$keep_runs" -eq 0 ]]; then
  cp "$workflow_runs_file" "$delete_workflow_runs_file"
else
  awk -v keep="$keep_runs" 'NR > keep' "$workflow_runs_file" > "$delete_workflow_runs_file"
fi

echo
echo "Keeping newest $keep_count version(s):"
if [[ -s "$keep_file" ]]; then
  sed 's/^/  /' "$keep_file"
else
  echo "  none"
fi

echo
echo "Releases to delete:"
if [[ -s "$delete_releases_file" ]]; then
  cut -f1 "$delete_releases_file" | sed 's/^/  /'
else
  echo "  none"
fi

echo
echo "Tags to delete:"
if [[ -s "$delete_tags_file" ]]; then
  sed 's/^/  /' "$delete_tags_file"
else
  echo "  none"
fi

echo
echo "Workflow runs to delete:"
if [[ -s "$delete_workflow_runs_file" ]]; then
  awk -F '\t' '{ printf "  %s  %s  %s  %s  %s\n", $1, $2, $3, $4, $6 }' "$delete_workflow_runs_file"
else
  echo "  none"
fi

if [[ "$mode" != "execute" ]]; then
  echo
  echo "Dry run only. Re-run with --execute to delete."
  exit 0
fi

if [[ "$assume_yes" != true ]]; then
  echo
  read -r -p "Delete the releases, tags and workflow runs listed above from $repo? Type DELETE to continue: " confirmation
  if [[ "$confirmation" != "DELETE" ]]; then
    echo "Aborted."
    exit 1
  fi
fi

while IFS=$'\t' read -r tag _draft _prerelease; do
  [[ -z "$tag" ]] && continue
  echo "Deleting release $tag..."
  gh release delete "$tag" --repo "$repo" --yes
done < "$delete_releases_file"

while IFS= read -r tag; do
  [[ -z "$tag" ]] && continue
  echo "Deleting tag $tag..."
  gh api --method DELETE "repos/$repo/git/refs/tags/$tag" >/dev/null
done < "$delete_tags_file"

while IFS=$'\t' read -r run_id _created_at _status _conclusion _branch _title; do
  [[ -z "$run_id" ]] && continue
  echo "Deleting workflow run $run_id..."
  gh run delete "$run_id" --repo "$repo" >/dev/null
done < "$delete_workflow_runs_file"

echo "Done."
