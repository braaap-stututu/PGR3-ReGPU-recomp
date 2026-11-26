#!/bin/bash
# Deck (session 8): AI race at 720p (the user's capture settings), every frame recorded raw (640x360 RGBA) in back-to-back bursts of 120.
#   race_scan.sh <outdir> [VAR=value ...]       BENCH_S: measured seconds (default 200 = the whole 3-lap race)
#   SWITCHES="<sec after GO>:<NAME>=<0|1> ..."   live switch flips during the race (A/B within one race)
ROOT="$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)"; cd "$ROOT" || exit 1
PROTON="$HOME/.steam/steam/steamapps/common/Proton 11.0"; PREFIX="$HOME/.local/share/pgr3-regpu/prefix"
USERD="$ROOT/out/user-route"; RUN="$ROOT/out/run"; OUT="$1"; shift; LOG="$RUN/pgr3.log"
CTL="$ROOT/thirdparty/ReGPU/tools/regpuctl.py"
ctl() { REGPU_DEBUG_PORT=7342 timeout 90 python3 -I "$CTL" "$@"; }
winpath() { printf 'Z:%s' "$(readlink -f "$1" | tr '/' '\\')"; }
logt() { tail -1 "$LOG" | sed -E 's/^\[ *([0-9.]+)\].*/\1/'; }
pgrep -x pgr3.exe >/dev/null && { echo "pgr3.exe is running"; exit 1; }
kscreen-doctor --dpms on >/dev/null 2>&1; sleep 2   # (a sleeping display holds vsync presents to 1 fps)
rm -rf "$USERD/profiles" "$OUT"; mkdir -p "$USERD/profiles" "$OUT"; cp -a "$ROOT/out/bench/baseline/." "$USERD/profiles/"
rm -f "$LOG"
( cd "$RUN" && env STEAM_COMPAT_DATA_PATH="$PREFIX" STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.steam/steam" WINEDEBUG=-all \
  PROTON_USE_XALIA=0 PGR3_USER_DIR="$(winpath "$USERD")" REGPU_DEBUG=1 REGPU_DEBUG_PORT=7342 \
  PGR3_RESOLUTION=1280x720 PGR3_FULLSCREEN=1 PGR3_MSAA=1 PGR3_MAX_FPS=60 PGR3_VSYNC=1 PGR3_VBLANK_HZ=240 \
  PGR3_BENCHMARK=${BENCH_S:-200} PGR3_BENCHMARK_SETTLE=5 "$@" \
  "$PROTON/proton" run "$ROOT/build/deckvk/pgr3.exe" "$(winpath "$ROOT/game/disc")" ) </dev/null >"$RUN/proton.out" 2>&1 &
for i in $(seq 1 480); do grep -q "benchmark race: GO" "$LOG" 2>/dev/null && break; pgrep -x pgr3.exe >/dev/null || [ $i -lt 20 ] || break; sleep 0.5; done
GO=$(grep "benchmark race: GO" "$LOG" | sed -E 's/^\[ *([0-9.]+)\].*/\1/'); [ -n "$GO" ] || { echo "no GO"; exit 1; }
echo "GO at log $GO" | tee "$OUT/bursts.txt"
declare -A done_sw
b=0
while pgrep -x pgr3.exe >/dev/null && ! grep -q "benchmark: done" "$LOG"; do
  T=$(logt); REL=$(awk -v t="$T" -v g="$GO" 'BEGIN{printf "%.1f", t-g}')
  for sw in ${SWITCHES:-}; do
    at=${sw%%:*}; kv=${sw#*:}
    if [ -z "${done_sw[$sw]:-}" ] && awk -v r="$REL" -v a="$at" 'BEGIN{exit !(r>=a)}'; then
      ctl switch ${kv%%=*} ${kv#*=} >/dev/null; done_sw[$sw]=1; echo "switch $kv at GO+$REL" | tee -a "$OUT/bursts.txt"
    fi
  done
  echo "b$b start log $T (GO+$REL)" >> "$OUT/bursts.txt"
  ctl shots 120 "$(winpath "$OUT")\\b$b raw" >/dev/null || echo "b$b failed" >> "$OUT/bursts.txt"
  b=$((b+1))
done
for i in $(seq 1 120); do pgrep -x pgr3.exe >/dev/null || break; sleep 1; done
grep "benchmark: frames" "$LOG" | tee -a "$OUT/bursts.txt"
