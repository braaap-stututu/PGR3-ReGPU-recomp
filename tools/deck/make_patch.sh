#!/bin/bash
# Deck port of tools/make_patch.ps1: make_patch.sh <name> [ReGPU|ReXenon]
# Writes patches/<name>.patch = thirdparty/<repo>'s working tree minus (HEAD + every patch listed in tools/setup.ps1),
# built in a scratch index (CLAUDE.md "Patches"). With <name> = "-", only prints the diff stat (stack check).
set -euo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)"
NAME="$1"; REPO="${2:-ReGPU}"
VAR=regpuPatches; [ "$REPO" = ReGPU ] || VAR=rexenonPatches
mapfile -t STACK < <(awk -v v="\$$VAR = @(" 'index($0,v)==1{f=1;next} f&&/^\)/{f=0} f' "$ROOT/tools/setup.ps1" | sed -n "s/^[[:space:]]*'\([^']*\)'.*/\1/p")
IDX="$(mktemp -u "${TMPDIR:-/tmp}/pgr3-mkpatch-XXXXXX.idx")"
trap 'rm -f "$IDX"' EXIT
export GIT_INDEX_FILE="$IDX"
git -C "$ROOT/thirdparty/$REPO" read-tree HEAD
for p in "${STACK[@]}"; do
  git -C "$ROOT/thirdparty/$REPO" apply --cached "$ROOT/patches/$p.patch" || { echo "patch $p does not apply" >&2; exit 1; }
done
if [ "$NAME" = - ]; then
  git -C "$ROOT/thirdparty/$REPO" -c core.autocrlf=false diff --stat; echo "(${#STACK[@]} patches in the stack)"; exit 0
fi
git -C "$ROOT/thirdparty/$REPO" -c core.autocrlf=false diff --no-color --no-ext-diff > "$ROOT/patches/$NAME.patch"
echo "wrote patches/$NAME.patch ($(wc -l < "$ROOT/patches/$NAME.patch") lines) on top of ${#STACK[@]} patches"
