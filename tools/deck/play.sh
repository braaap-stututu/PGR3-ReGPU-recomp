#!/bin/bash
# Steam Deck: starts the Deck build of PGR3 (build/deck/pgr3.exe, cross-built in the regpu distrobox) through Proton,
# like play.cmd: saves and settings in out/user (settings.ini), the log in out/run/pgr3.log, debug server on 7342.
# Deck defaults (override any with VAR=value arguments, or PGR3_DECK_DEFAULTS=0 to use settings.ini as it is):
#   1280x800 (the Deck's screen), MSAA 1x, frame cap 60 with vsync, guest vblank 240 Hz.
#   tools/deck/play.sh                      Deck defaults
#   tools/deck/play.sh PGR3_MSAA=2          one setting changed
# Desktop Mode: run it from a terminal. Game Mode: add it to Steam as a non-Steam game.
set -u
ROOT="$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)"
# The Vulkan ReGPU build (build/deckvk, tools/deck/README.md) by default; PGR3_VK=0: the Direct3D 12 one (build/deck).
DEFBUILD=deckvk; [ "${PGR3_VK:-1}" = 0 ] && DEFBUILD=deck
EXE="${PGR3_EXE:-$ROOT/build/$DEFBUILD/pgr3.exe}"
PROTON="${PGR3_PROTON:-$HOME/.steam/steam/steamapps/common/Proton 11.0}"
PREFIX="${PGR3_PREFIX:-$HOME/.local/share/pgr3-regpu/prefix}"
winpath() { printf 'Z:%s' "$(readlink -f "$1" | tr '/' '\\')"; }
[ -x "$PROTON/proton" ] || { echo "Proton not found at $PROTON (set PGR3_PROTON)" >&2; exit 1; }
[ -f "$EXE" ] || { echo "$EXE is missing: build it first (see tools/deck/README.md)" >&2; exit 1; }
mkdir -p "$PREFIX" "$ROOT/out/run" "$ROOT/out/user"
DECK=()
if [ "${PGR3_DECK_DEFAULTS:-1}" != 0 ]; then
  # Guest vblank 240 Hz (4x the cap; the port's default is 2x): PGR3 presents on every second vblank, so a frame that
  # just misses its slot waited 8.3 ms at 120 Hz. AI race at cap 60, 2+2: 56.1/57.4 -> 59.2/59.2 fps, frames over
  # 16.7 ms 1228/1200 -> 1099/1085, p99 the same or lower (session 6). (The desktop measured the opposite: Deck only.)
  DECK=(PGR3_RESOLUTION=1280x800 PGR3_FULLSCREEN=1 PGR3_MSAA=1 PGR3_MAX_FPS=60 PGR3_VSYNC=1 PGR3_VBLANK_HZ=240)
fi
cd "$ROOT/out/run" || exit 1
exec env STEAM_COMPAT_DATA_PATH="$PREFIX" STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.steam/steam" WINEDEBUG=-all \
    PROTON_USE_XALIA=0 PGR3_USER_DIR="$(winpath "$ROOT/out/user")" REGPU_DEBUG=1 REGPU_DEBUG_PORT=7342 \
    "${DECK[@]}" "$@" "$PROTON/proton" run "$EXE" "$(winpath "$ROOT/game/disc")"
