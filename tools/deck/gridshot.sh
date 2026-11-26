#!/bin/bash
# Deck: boot PGR3 through Proton to the frozen London grid (PGR3_GRID_TEST=1) on out/user-route (profile restored
# from out/bench/baseline first, never out/user), take a screenshot through ReGPU's debug server, close the game.
#   tools/deck/gridshot.sh <out.png> [VAR=value ...]     PGR3_EXE=<exe> (default build/deckvk/pgr3.exe, Vulkan; PGR3_VK=0: build/deck)
set -u
ROOT="$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)"
OUTPNG="$(readlink -m "$1")"; shift
DEFBUILD=deckvk; [ "${PGR3_VK:-1}" = 0 ] && DEFBUILD=deck   # Vulkan by default; PGR3_VK=0: the Direct3D 12 build
EXE="${PGR3_EXE:-$ROOT/build/$DEFBUILD/pgr3.exe}"
PROTON="${PGR3_PROTON:-$HOME/.steam/steam/steamapps/common/Proton 11.0}"
PREFIX="${PGR3_PREFIX:-$HOME/.local/share/pgr3-regpu/prefix}"
RUN="$ROOT/out/run"; LOG="$RUN/pgr3.log"; USERD="$ROOT/out/user-route"
CTL="$ROOT/thirdparty/ReGPU/tools/regpuctl.py"
winpath() { printf 'Z:%s' "$(readlink -f "$1" | tr '/' '\\')"; }
if pgrep -x pgr3.exe >/dev/null; then echo "pgr3.exe is running: close it first" >&2; exit 1; fi
rm -rf "$USERD/profiles"; mkdir -p "$USERD/profiles"; cp -a "$ROOT/out/bench/baseline/." "$USERD/profiles/"
mkdir -p "$RUN" "$PREFIX"; rm -f "$LOG"
WAIT="${PGR3_SHOT_WAIT:-8}"
(
  cd "$RUN" || exit 1
  env STEAM_COMPAT_DATA_PATH="$PREFIX" STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.steam/steam" WINEDEBUG=-all \
      PROTON_USE_XALIA=0 PGR3_USER_DIR="$(winpath "$USERD")" PGR3_GRID_TEST=1 REGPU_DEBUG=1 REGPU_DEBUG_PORT=7342 \
      REGPU_UPLOAD_RING_AUTO=0 PGR3_RESOLUTION=1280x720 PGR3_FULLSCREEN=0 PGR3_MSAA=1 PGR3_AA=off PGR3_VSYNC=0 \
      PGR3_MAX_FPS=60 "$@" \
      "$PROTON/proton" run "$EXE" "$(winpath "$ROOT/game/disc")" >"$RUN/proton.out" 2>&1
) </dev/null >/dev/null 2>&1 &
for i in $(seq 1 120); do pgrep -x pgr3.exe >/dev/null && break; sleep 0.5; done
ok=0
for i in $(seq 1 ${PGR3_BOOT_TIMEOUT:-300}); do
  grep -q 'grid test: frozen' "$LOG" 2>/dev/null && { ok=1; break; }
  pgrep -x pgr3.exe >/dev/null || break
  sleep 1
done
if [ $ok = 1 ]; then
  sleep "$WAIT"
  mkdir -p "$(dirname "$OUTPNG")"
  REGPU_DEBUG_PORT=7342 python3 -I "$CTL" shot "Z:$(printf '%s' "$OUTPNG" | tr '/' '\\')" || echo "shot failed" >&2
fi
[ -n "${PGR3_KEEP:-}" ] && { echo "left running (grid frozen: $ok)"; exec >/dev/null 2>&1; exit 0; }
pkill -x pgr3.exe; sleep 3; pkill -9 -x pgr3.exe 2>/dev/null
wait
[ -f "$OUTPNG" ] && echo "shot: $OUTPNG" || { echo "no shot (grid frozen: $ok)"; tail -20 "$LOG" 2>/dev/null; }
