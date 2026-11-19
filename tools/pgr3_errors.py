"""pgr3_errors.py out/pgr3.bin out/pgr3.sections out/check.txt out/ppc
Groups the check's unresolved branches and run-offs by the .pdata function around them, so function
boundaries can be fixed in config/pgr3.toml: for each group, the .pdata function (if any) that covers
the erroring pieces and the branch targets, or the span a leaf without .pdata would need."""
import re, struct, sys, bisect, glob
img = open(sys.argv[1], 'rb').read(); base = 0x82000000
secs = {}
for l in open(sys.argv[2]):
    p = l.split()
    if len(p) == 4 and p[3] in ('code', 'data'): secs[p[0]] = (int(p[1], 16), int(p[2], 16))
ps, pn = secs['.pdata']
pd = []
for o in range(ps - base, ps - base + pn, 8):
    b, k = struct.unpack_from('>II', img, o)
    if b: pd.append((b, ((k >> 8) & 0x3FFFFF) * 4))
pd.sort(); starts = [p[0] for p in pd]
def pdata_of(a):
    i = bisect.bisect_right(starts, a) - 1
    if i >= 0 and pd[i][0] <= a < pd[i][0] + pd[i][1]: return pd[i]
    return None
fsizes = {}
for path in glob.glob(sys.argv[4] + '/ppc_func_mapping.cpp'):
    for m in re.finditer(r'\{ 0x([0-9A-F]+), ', open(path).read()): fsizes[int(m.group(1), 16)] = 1
errs = []
for l in open(sys.argv[3]):
    m = re.match(r'\s+ERROR in sub_([0-9A-F]+): branch to (?:loc_)?(?:0x)?([0-9A-Fa-f]+)', l)
    if m: errs.append((int(m.group(1), 16), int(m.group(2), 16)))
    m = re.match(r'\s+runs off its end: sub_([0-9A-F]+)', l)
    if m: errs.append((int(m.group(1), 16), None))
groups = {}
for f, t in errs:
    p = pdata_of(f)
    key = p if p else ('leaf', f & ~0xFFF)
    groups.setdefault(key, []).append((f, t))
for k, v in sorted(groups.items(), key=lambda x: x[1][0][0]):
    fs = sorted({f for f, _ in v}); ts = sorted({t for _, t in v if t})
    tp = sorted({str(['%08X+%X' % pdata_of(t) if pdata_of(t) else 'none'][0]) for t in ts})
    if k[0] == 'leaf':
        print('NO .pdata  pieces %s  targets %s  target pdata %s' % (' '.join('%08X' % f for f in fs), ' '.join('%08X' % t for t in ts), tp))
    else:
        print('pdata %08X+%X  pieces %s  targets %s  target pdata %s' % (k[0], k[1], ' '.join('%08X' % f for f in fs), ' '.join('%08X' % t for t in ts[:8]), tp))
