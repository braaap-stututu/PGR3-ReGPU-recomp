#!/bin/bash
# Deck: PGR3's AI-driven benchmark race (src/hooks/grid_test.cpp, as tools/benchmark.ps1) through Proton on out/user-route
# (profile restored from out/bench/baseline, never out/user). The game closes itself; out/bench/<label>-<time>/ gets
# pgr3.log, result.json and result.txt (tools/bench_summary.py), and one line goes to out/bench/deck-runs.txt.
#   tools/deck/bench.sh <label> [seconds=40] [VAR=value ...]   PGR3_VK=0: the D3D12 build (default Vulkan)
# Defaults: 1280x800 (the Deck's panel), MSAA 1x, uncapped (max_fps 1000, vsync off), settle 5 s after GO.
set -u
ROOT="$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)"
LABEL="$1"; SECONDS_="${2:-40}"; shift; [ $# -gt 0 ] && shift
DEFBUILD=deckvk; [ "${PGR3_VK:-1}" = 0 ] && DEFBUILD=deck
EXE="${PGR3_EXE:-$ROOT/build/$DEFBUILD/pgr3.exe}"
PROTON="${PGR3_PROTON:-$HOME/.steam/steam/steamapps/common/Proton 11.0}"
PREFIX="${PGR3_PREFIX:-$HOME/.local/share/pgr3-regpu/prefix}"
RUN="$ROOT/out/run"; USERD="$ROOT/out/user-route"
winpath() { printf 'Z:%s' "$(readlink -f "$1" | tr '/' '\\')"; }
if pgrep -x pgr3.exe >/dev/null; then echo "pgr3.exe is running: close it first" >&2; exit 1; fi
rm -rf "$USERD/profiles"; mkdir -p "$USERD/profiles"; cp -a "$ROOT/out/bench/baseline/." "$USERD/profiles/"
mkdir -p "$RUN" "$PREFIX"; rm -f "$RUN/pgr3.log"
( cd "$RUN" && env STEAM_COMPAT_DATA_PATH="$PREFIX" STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.steam/steam" WINEDEBUG=-all \
    PROTON_USE_XALIA=0 PGR3_USER_DIR="$(winpath "$USERD")" REGPU_DEBUG=1 REGPU_DEBUG_PORT=7342 REGPU_UPLOAD_RING_AUTO=0 \
    PGR3_RESOLUTION=1280x800 PGR3_FULLSCREEN=0 PGR3_MSAA=1 PGR3_AA=off PGR3_VSYNC=0 PGR3_MAX_FPS=1000 \
    PGR3_BENCHMARK="$SECONDS_" PGR3_BENCHMARK_SETTLE=5 "$@" \
    "$PROTON/proton" run "$EXE" "$(winpath "$ROOT/game/disc")" ) </dev/null >"$RUN/proton.out" 2>&1
# (proton run can return before the game ends: wait for it, up to the race's length and a margin.)
for i in $(seq 1 $((SECONDS_ + 240))); do pgrep -x pgr3.exe >/dev/null || break; sleep 1; done
pkill -x pgr3.exe 2>/dev/null && sleep 3
DIR="$ROOT/out/bench/$LABEL-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$DIR"; cp "$RUN/pgr3.log" "$DIR/"
SUMMARY=$(python3 -I "$ROOT/tools/bench_summary.py" "$DIR/pgr3.log" "$DIR/result.json" 2>&1 | tr '\n' ' ')
echo "$SUMMARY" > "$DIR/result.txt"
echo "$(date +%H:%M) $LABEL ($(basename "$EXE" .exe) in $DEFBUILD $*): $SUMMARY" | tee -a "$ROOT/out/bench/deck-runs.txt"
