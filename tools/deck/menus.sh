#!/bin/bash
# Deck: drive PGR3's menus through the debug server (port 7342) for checking other cities and modes (session 6,
# the user asked for it: "check the other modes/tracks"; CLAUDE.md otherwise allows only the grid route). Always on
# out/user-route, restored from out/bench/baseline unless NORESET=1; never the user's profile. Vulkan build by
# default (PGR3_VK=0: D3D12). Shots go to $OUT (default out/shots/deck).
#   tools/deck/menus.sh menu [VAR=value ...]          boot to the main menu, leave the game running
#   tools/deck/menus.sh shot <name> [KEY ...]         press keys (0.8 s apart), wait $WAIT s (3), shoot <name>.png
#   tools/deck/menus.sh race <city> <night> <name> [mode]
#        Playtime > Play on this Xbox 360 > mode (0 Street Race ... 5 Cone Capture) > city (0 Tokyo, 1 Las Vegas,
#        2 New York, 3 London, 4 Nurburgring) > first route > lighting (night 1 = night) > lobby + 1 opponent > race;
#        $SHOTS shots 1.5 s apart (24), throttle held from the 14th, and a contact sheet <name>_sheet.png.
#        (Cone Capture has no lobby: its extra START pauses the race; press A to continue.)
set -u
ROOT="$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)"; cd "$ROOT"
OUT="${OUT:-$ROOT/out/shots/deck}"; mkdir -p "$OUT"
export REGPU_DEBUG_PORT=7342
C() { python3 -I thirdparty/ReGPU/tools/regpuctl.py "$@"; }
p() { for k in "$@"; do C press "$k" >/dev/null; sleep 1.2; done; }
shot() { C shot "Z:$(printf '%s' "$OUT/$1.png" | tr '/' '\\')" >/dev/null; }
menu() {
  if pgrep -x pgr3.exe >/dev/null; then C quit >/dev/null; for i in $(seq 30); do pgrep -x pgr3.exe >/dev/null || break; sleep 1; done; fi
  if [ -z "${NORESET:-}" ]; then rm -rf out/user-route/profiles; mkdir -p out/user-route/profiles; cp -a out/bench/baseline/. out/user-route/profiles/; fi
  (tools/deck/play.sh PGR3_DECK_DEFAULTS=0 PGR3_USER_DIR="Z:$(printf '%s' "$ROOT/out/user-route" | tr '/' '\\')" PGR3_RESOLUTION=1280x720 \
     PGR3_FULLSCREEN=0 PGR3_MSAA=1 PGR3_AA=off PGR3_MAX_FPS=60 PGR3_VSYNC=0 REGPU_UPLOAD_RING_AUTO=0 "$@" </dev/null >/dev/null 2>&1 &)
  for i in $(seq 90); do C ping >/dev/null 2>&1 && break; sleep 1; done
  sleep 25; for i in 1 2 3 4 5 6; do C press START >/dev/null; sleep 3; done
  C press A >/dev/null; sleep 4
}
case "${1:-}" in
  menu) shift; menu "$@" ;;
  shot) n=$2; shift 2; for k in "$@"; do C press "$k" >/dev/null; sleep 0.8; done; sleep "${WAIT:-3}"; shot "$n"; echo "$OUT/$n.png" ;;
  race)
    city=$2; night=$3; name=$4; mode=${5:-0}
    menu
    [ -n "${PRE_RACE:-}" ] && eval "$PRE_RACE"   # e.g. PRE_RACE='C poke 826486B4 01000000' (a debug variable before the race loads)
    p DOWN A; sleep 2; p A; sleep 2; for i in $(seq 1 "$mode"); do p DOWN; done; p A; sleep 2
    for i in $(seq 1 "$city"); do p DOWN; done; p A; sleep 2; p A; sleep 2
    [ "$night" = 1 ] && p RIGHT
    sleep 1; p START; sleep 2; p A DOWN A; sleep 1; p START
    mkdir -p "$OUT/$name"; rm -f "$OUT/$name"/*
    for i in $(seq -w 1 "${SHOTS:-24}"); do sleep 1.5; shot "$name/s$i"; [ "$i" = 14 ] && C trigger R 0.8 >/dev/null; done
    C release all >/dev/null
    (cd "$OUT/$name" && ffmpeg -v error -y -pattern_type glob -i 's*.png' -vf "scale=320:-1,tile=6x4" -frames:v 1 "$OUT/${name}_sheet.png")
    echo "$OUT/${name}_sheet.png" ;;
  *) sed -n 2,16p "$0"; exit 1 ;;
esac
