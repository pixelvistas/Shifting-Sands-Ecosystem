#!/usr/bin/env bash
# apply_addon_patches.sh
#
# Copies the modified addon files mirrored under _addon_patches/ into an
# openFrameworks installation, backing up whatever is replaced.
#
# Usage:
#   tools/apply_addon_patches.sh /path/to/of_0.12.1
#   tools/apply_addon_patches.sh /path/to/of_0.12.1 --dry-run
#   tools/apply_addon_patches.sh /path/to/of_0.12.1 --cross-platform-only
#
# --cross-platform-only applies just the ofxImGui fixes and skips the
# Windows/Kinect-1473 libfreenect work, which is unnecessary on Linux.

set -euo pipefail

OF_ROOT="${1:-}"
shift || true

DRY_RUN=0
XPLAT_ONLY=0
for arg in "$@"; do
  case "$arg" in
    --dry-run)              DRY_RUN=1 ;;
    --cross-platform-only)  XPLAT_ONLY=1 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

if [[ -z "$OF_ROOT" || ! -d "$OF_ROOT/addons" ]]; then
  echo "Usage: $0 /path/to/openFrameworks [--dry-run] [--cross-platform-only]" >&2
  echo "  (expected to find an 'addons' directory under that path)" >&2
  exit 2
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$REPO_ROOT/_addon_patches"
STAMP="$(date +%Y%m%d-%H%M%S)"
BACKUP="$REPO_ROOT/_addon_backup-$STAMP"

[[ -d "$SRC" ]] || { echo "no _addon_patches directory at $SRC" >&2; exit 1; }

echo "openFrameworks: $OF_ROOT"
echo "patch source:   $SRC"
[[ $DRY_RUN -eq 1 ]] && echo "MODE: dry run (nothing will be written)"
[[ $XPLAT_ONLY -eq 1 ]] && echo "MODE: cross-platform fixes only (ofxImGui)"
echo

count=0; skipped=0
while IFS= read -r -d '' file; do
  rel="${file#"$SRC"/}"

  if [[ $XPLAT_ONLY -eq 1 && "$rel" != ofxImGui/* ]]; then
    skipped=$((skipped + 1)); continue
  fi

  dest="$OF_ROOT/addons/$rel"

  if [[ ! -f "$dest" ]]; then
    echo "  NEW  $rel"
  else
    if cmp -s "$file" "$dest"; then
      echo "  ==   $rel (already applied)"
      continue
    fi
    echo "  MOD  $rel"
  fi

  if [[ $DRY_RUN -eq 0 ]]; then
    if [[ -f "$dest" ]]; then
      mkdir -p "$(dirname "$BACKUP/$rel")"
      cp -p "$dest" "$BACKUP/$rel"
    fi
    mkdir -p "$(dirname "$dest")"
    cp -p "$file" "$dest"
  fi
  count=$((count + 1))
done < <(find "$SRC" -type f -print0)

echo
if [[ $DRY_RUN -eq 1 ]]; then
  echo "$count file(s) would change; $skipped skipped."
else
  echo "$count file(s) written."
  [[ -d "$BACKUP" ]] && echo "originals backed up to: $BACKUP"
fi

cat <<'NOTE'

Reminder: the libfreenect transplant and the ssize_t guard are Windows /
Kinect-1473 workarounds. On Linux, use the stock ofxKinect libfreenect with
udev rules and pass --cross-platform-only instead.
NOTE