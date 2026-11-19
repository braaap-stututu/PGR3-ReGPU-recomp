"""pgr3_gaps.py out/pgr3.bin out/ppc <hex address>...
For each address outside .pdata: the code gap between the surrounding .pdata functions (zero padding
trimmed), how many functions the recompiler made in it, and where its blr/bctr instructions are.
Used to size leaf functions without .pdata (early-XDK leaves that hold jump tables)."""
import bisect, re, struct, sys

img = open(sys.argv[1], 'rb').read()
base = 0x82000000
PDATA, PDATA_SIZE = 0x82128000, 0x15C88
pd = []
for o in range(PDATA - base, PDATA - base + PDATA_SIZE, 8):
    b, k = struct.unpack_from('>II', img, o)
    if b:
        pd.append((b, ((k >> 8) & 0x3FFFFF) * 4))
pd.sort()
st = [p[0] for p in pd]
funcs = sorted(int(m, 16) for m in re.findall(r'\{ 0x([0-9A-F]+),', open(sys.argv[2] + '/ppc_func_mapping.cpp').read()))


def W(a):
    return struct.unpack_from('>I', img, a - base)[0]


for a in [int(x, 16) for x in sys.argv[3:]]:
    i = bisect.bisect_right(st, a) - 1
    lo, hi = pd[i][0] + pd[i][1], pd[i + 1][0]
    while lo < hi and W(lo) == 0:
        lo += 4
    e = hi
    while e > lo and W(e - 4) == 0:
        e -= 4
    inside = [f for f in funcs if lo <= f < e]
    ends = ['%X%s' % (x, 'c' if W(x) == 0x4E800420 else '') for x in range(lo, e, 4) if W(x) in (0x4E800020, 0x4E800420)]
    print('%08X: gap %08X..%08X (0x%X) funcs %d ends %s' % (a, lo, e, e - lo, len(inside), ' '.join(ends)))
