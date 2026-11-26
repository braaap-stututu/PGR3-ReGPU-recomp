#!/bin/bash
# Deck: the AI benchmark race at the user's capture settings (1280x720 fullscreen, vsync 60, on-screen timer), on the
# route profile out/user-route (restored from out/bench/baseline, never out/user), with live ReGPU switch flips at set
# times after GO, for A/B tests the user watches on a capture card (one setting per lap; London laps are ~62 s).
#   tools/deck/race_ab.sh <name> [VAR=value ...]
#   SWITCHES="<s after GO>:<NAME>=<0|1> ..."   default: lap 2 texture copies on the draw thread, lap 3 the old
#                                             occlusion-query end rule (session 8's two suspects for the flashes)
# The log goes to out/logs/<name>.log, the flips (with their log time) to out/logs/<name>.flips.txt.
set -u
ROOT="$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)"; cd "$ROOT" || exit 1
PROTON="${PGR3_PROTON:-$HOME/.steam/steam/steamapps/common/Proton 11.0}"; PREFIX="${PGR3_PREFIX:-$HOME/.local/share/pgr3-regpu/prefix}"
USERD="$ROOT/out/user-route"; RUN="$ROOT/out/run"; NAME="$1"; shift; LOG="$RUN/pgr3.log"
CTL="$ROOT/thirdparty/ReGPU/tools/regpuctl.py"
SWITCHES="${SWITCHES:-62:REGPU_SYNC_TEXTURE_COPIES=1 124:REGPU_SYNC_TEXTURE_COPIES=0 124:REGPU_ZPD_STRICT_END=1}"
ctl() { REGPU_DEBUG_PORT=7342 timeout 30 python3 -I "$CTL" "$@"; }
winpath() { printf 'Z:%s' "$(readlink -f "$1" | tr '/' '\\')"; }
logt() { tail -1 "$LOG" | sed -E 's/^\[ *([0-9.]+)\].*/\1/'; }
pgrep -x pgr3.exe >/dev/null && { echo "pgr3.exe is running: close it first" >&2; exit 1; }
kscreen-doctor --dpms on >/dev/null 2>&1; sleep 2   # (a sleeping display holds vsync presents to 1 fps)
rm -rf "$USERD/profiles"; mkdir -p "$USERD/profiles" "$ROOT/out/logs"; cp -a "$ROOT/out/bench/baseline/." "$USERD/profiles/"
[ -f "$LOG" ] && mv "$LOG" "$RUN/pgr3.prev.log"
FLIPS="$ROOT/out/logs/$NAME.flips.txt"; : > "$FLIPS"
( cd "$RUN" && exec kde-inhibit --power --screenSaver env STEAM_COMPAT_DATA_PATH="$PREFIX" STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.steam/steam" \
  WINEDEBUG=-all PROTON_USE_XALIA=0 PGR3_USER_DIR="$(winpath "$USERD")" REGPU_DEBUG=1 REGPU_DEBUG_PORT=7342 \
  PGR3_RESOLUTION=1280x720 PGR3_FULLSCREEN=1 PGR3_MSAA=1 PGR3_MAX_FPS=60 PGR3_VSYNC=1 PGR3_VBLANK_HZ=240 \
  PGR3_BENCHMARK=${BENCH_S:-200} PGR3_BENCHMARK_SETTLE=5 "$@" \
  "$PROTON/proton" run "$ROOT/build/deckvk/pgr3.exe" "$(winpath "$ROOT/game/disc")" ) </dev/null >"$RUN/proton.out" 2>&1 &
for i in $(seq 1 480); do grep -q "benchmark race: GO" "$LOG" 2>/dev/null && break; sleep 0.5; done
GO=$(grep "benchmark race: GO" "$LOG" | sed -E 's/^\[ *([0-9.]+)\].*/\1/'); [ -n "$GO" ] || { echo "no GO (is the display asleep?)" | tee -a "$FLIPS"; exit 1; }
echo "GO at timer $GO s" | tee -a "$FLIPS"
declare -A done_sw
while pgrep -x pgr3.exe >/dev/null && ! grep -q "benchmark: done" "$LOG"; do
  T=$(logt)
  for sw in $SWITCHES; do
    at=${sw%%:*}; kv=${sw#*:}
    if [ -z "${done_sw[$sw]:-}" ] && awk -v t="$T" -v g="$GO" -v a="$at" 'BEGIN{exit !(t-g>=a)}'; then
      ctl switch "${kv%%=*}" "${kv#*=}" >/dev/null; done_sw[$sw]=1; echo "timer $T s: $kv" | tee -a "$FLIPS"
    fi
  done
  sleep 0.5
done
for i in $(seq 1 120); do pgrep -x pgr3.exe >/dev/null || break; sleep 1; done
cp "$LOG" "$ROOT/out/logs/$NAME.log"; grep "benchmark: frames" "$LOG" | tee -a "$FLIPS"
