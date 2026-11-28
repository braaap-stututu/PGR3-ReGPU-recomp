#!/bin/bash
# CPU profile of the AI benchmark race (Linux perf; SteamOS ptrace_scope=1, so perf starts bench.sh as its child; the
# host perf from ReGPU-V/work/tools/perf-host, as the FM4 port's fm4profile.sh). Records PSECS seconds once the race is
# measuring. Report per thread: out/profile-<label>/report.txt.   tools/deck/profile.sh <label> [perf seconds=20] [VAR=value ...]
set -u
LABEL="${1:?label}"; PSECS="${2:-20}"; shift; [ $# -gt 0 ] && shift
D="$(dirname "$(readlink -f "$0")")"; ROOT="$(cd "$D/../.." && pwd)"
P="/home/deck/claude projects/ReGPU-V/work/tools/perf-host"
OUT="$ROOT/out/profile-$LABEL"; rm -rf "$OUT"; mkdir -p "$OUT"; mkfifo "$OUT/ctl" "$OUT/ack"
rm -f "$ROOT/out/run/pgr3.log"
"$P/ld.so" --library-path "$P/lib" "$P/perf" record -F 1999 -D -1 --control "fifo:$OUT/ctl,$OUT/ack" -o "$OUT/perf.data" -- \
    "$D/bench.sh" "prof-$LABEL" $((PSECS + 25)) "$@" > "$OUT/bench.txt" 2>&1 &
BPID=$!
exec 3<>"$OUT/ctl" 4<>"$OUT/ack"
for i in $(seq 1 400); do grep -q "benchmark: measuring" "$ROOT/out/run/pgr3.log" 2>/dev/null && break; sleep 1; done
sleep 5
echo enable >&3; read -t 5 -u 4 a
sleep "$PSECS"
echo disable >&3; read -t 5 -u 4 a
wait $BPID
"$P/ld.so" --library-path "$P/lib" "$P/perf" script -i "$OUT/perf.data" -F tid,comm,ip,dso > "$OUT/samples.txt" 2>/dev/null
distrobox enter regpu -- python3 -I "$D/perfreport.py" "$OUT/samples.txt" "${PGR3_EXE:-$ROOT/build/deckvk/pgr3.exe}" > "$OUT/report.txt"
head -120 "$OUT/report.txt"
