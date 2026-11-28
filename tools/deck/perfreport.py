# Names perf samples of pgr3.exe under Wine: IPs inside the exe's image are looked up in its PDB with llvm-symbolizer
# (the image loads at its preferred base); others are grouped by DSO. Prints, per thread, the share of samples per
# function.   pgr3profile.py <perf script output: tid comm ip dso> <pgr3.exe>
import collections, re, subprocess, sys

samples, exe = sys.argv[1], sys.argv[2]
rows = []
for line in open(samples, errors="replace"):
    m = re.match(r"\s*(.+?)\s+(\d+)\s+([0-9a-f]+)\s+\((.*)\)\s*$", line)
    if not m:
        continue
    comm, tid, ip, dso = m.group(1).strip(), m.group(2), int(m.group(3), 16), m.group(4)
    rows.append((comm, tid, ip, dso))

in_exe = sorted({ip for _, _, ip, dso in rows if dso.endswith("pgr3.exe") or (0x140000000 <= ip < 0x150000000)})
names = {}
if in_exe:
    p = subprocess.run(["llvm-symbolizer", "--obj=" + exe, "--functions=linkage", "--no-inlines", "--output-style=GNU"],
                       input="\n".join(hex(ip) for ip in in_exe), capture_output=True, text=True)
    out = p.stdout.split("\n")
    # GNU style: function line, then file:line line, per address
    for i, ip in enumerate(in_exe):
        f = out[2 * i] if 2 * i < len(out) else "?"
        names[ip] = f if f and f != "??" else "pgr3.exe+?"

def label(ip, dso):
    if ip in names:
        return names[ip]
    base = dso.rsplit("/", 1)[-1] if dso else "?"
    return f"[{base}]"

by_thread = collections.defaultdict(collections.Counter)
totals = collections.Counter()
for comm, tid, ip, dso in rows:
    key = f"{comm} ({tid})"
    by_thread[key][label(ip, dso)] += 1
    totals[key] += 1
grand = sum(totals.values())
print(f"{grand} samples")
for key, n in totals.most_common(12):
    print(f"\n== {key}: {n} samples ({100 * n / grand:.1f}%)")
    for name, c in by_thread[key].most_common(14):
        print(f"  {100 * c / n:5.1f}%  {name[:150]}")
