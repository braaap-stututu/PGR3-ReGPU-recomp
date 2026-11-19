"""pgr3_leaves.py out/pgr3.bin out/pgr3.sections <hex address>... [--toml]
Sizes leaf functions without .pdata (early-XDK leaves; Q5) in the code gaps around the given addresses.

A gap is the code between two .pdata functions, with zero padding trimmed. Real function starts in it are
the gap start plus every address something calls (`bl`) or points to (a data word, or a lis+addi/ori pair).
Each start's function runs to the next start (padding trimmed). Jump-table cases and branch targets aren't
starts, so a leaf with a table stays in one piece. Prints config lines for config/pgr3.toml."""
import bisect, struct, sys

args = [a for a in sys.argv[1:] if a != '--toml']
img = open(args[0], 'rb').read()
base = 0x82000000
secs = {}
for l in open(args[1]):
    p = l.split()
    if len(p) == 4 and p[3] in ('code', 'data'):
        secs[p[0]] = (int(p[1], 16), int(p[2], 16), p[3] == 'code')
code = [(s, s + n) for s, n, c in secs.values() if c]
data = [(s, s + n) for k, (s, n, c) in secs.items() if not c and k not in ('.pdata', '.reloc')]


def W(a):
    return struct.unpack_from('>I', img, a - base)[0]


def in_code(a):
    return any(s <= a < e for s, e in code)


ps, pn, _ = secs['.pdata']
pd = []
for o in range(ps - base, ps - base + pn, 8):
    b, k = struct.unpack_from('>II', img, o)
    if b:
        pd.append((b, ((k >> 8) & 0x3FFFFF) * 4))
pd.sort()
pst = [p[0] for p in pd]

starts = set()
for s, e in code:
    words = struct.unpack_from('>%dI' % ((e - s) // 4), img, s - base)
    for i, w in enumerate(words):
        a = s + 4 * i
        if w >> 26 == 18 and w & 3 == 1:                 # bl
            d = w & 0x3FFFFFC
            if d & 0x2000000:
                d -= 0x4000000
            starts.add((a + d) & 0xFFFFFFFF)
        if w >> 26 == 15 and (w >> 16) & 31 == 0:        # lis rD,hi ; addi/ori rD,rD,lo
            rd = (w >> 21) & 31
            for j in range(i + 1, min(i + 8, len(words))):
                x = words[j]
                if x >> 26 in (14, 24) and (x >> 16) & 31 == rd:
                    lo = x & 0xFFFF
                    if x >> 26 == 14 and lo & 0x8000:
                        lo -= 0x10000
                    t = (((w & 0xFFFF) << 16) + lo) & 0xFFFFFFFF
                    # A jump table's address (indexed with lwzx/lhzx/lbzx right after) is not a function.
                    table = any(words[c] >> 26 == 31 and ((words[c] >> 1) & 0x3FF) in (23, 87, 279)
                                for c in range(j + 1, min(j + 5, len(words))))
                    if not table:
                        starts.add(t)
                    break
for s, e in data:
    for a in range(s, e - 3, 4):
        w = W(a)
        if w & 3 == 0 and in_code(w):
            starts.add(w)

seen = set()
for a in [int(x, 16) for x in args[2:]]:
    i = bisect.bisect_right(pst, a) - 1
    lo, hi = pd[i][0] + pd[i][1], pd[i + 1][0]
    if (lo, hi) in seen:
        continue
    seen.add((lo, hi))
    while lo < hi and W(lo) == 0:
        lo += 4
    # An inline jump table (its first word is a code address) is data, not a start.
    gs = sorted({lo} | {x for x in starts if lo < x < hi and x & 3 == 0 and not in_code(W(x))})
    print('    # gap %08X..%08X (between .pdata %08X and %08X)' % (lo, hi, pd[i][0], pd[i + 1][0]))
    for k, f in enumerate(gs):
        end = gs[k + 1] if k + 1 < len(gs) else hi
        while end > f and W(end - 4) == 0:
            end -= 4
        print('    { address = 0x%08X, size = 0x%X },' % (f, end - f))
