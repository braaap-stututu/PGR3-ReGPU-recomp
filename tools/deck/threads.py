#!/usr/bin/env python3
"""threads.py [seconds] [top]: CPU % of a core per thread of the running pgr3.exe and the wineserver over a window."""
import os, subprocess, sys, time
S = float(sys.argv[1]) if len(sys.argv) > 1 else 10
TOP = int(sys.argv[2]) if len(sys.argv) > 2 else 20
TCK = os.sysconf('SC_CLK_TCK')
pids = [subprocess.run(['pgrep', '-x', n], capture_output=True, text=True).stdout.split()[:1] for n in ('pgr3.exe', 'wineserver')]
pids = [p[0] for p in pids if p]

def snap():
    out = {}
    for p in pids:
        for t in os.listdir('/proc/%s/task' % p):
            try:
                st = open('/proc/%s/task/%s/stat' % (p, t)).read()
                name = open('/proc/%s/task/%s/comm' % (p, t)).read().strip()
            except OSError:
                continue
            f = st[st.rindex(')') + 2:].split()
            out[t] = (name, int(f[11]) + int(f[12]))
    return out

a, t0 = snap(), time.time()
time.sleep(S)
b, t1 = snap(), time.time()
w = t1 - t0
rows = sorted(((b[t][1] - a[t][1]) / TCK / w * 100, b[t][0], t) for t in b if t in a)
rows.reverse()
for pct, name, t in rows[:TOP]:
    if pct > 0.05:
        print('%6.1f%% %-16s %s' % (pct, name, t))
print('total %.0f%% of a core over %.1f s' % (sum(r[0] for r in rows), w))
