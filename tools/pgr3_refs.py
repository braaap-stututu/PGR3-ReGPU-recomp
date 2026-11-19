"""pgr3_refs.py out/pgr3.bin <hex function>...: string constants (lis+addi) and bl targets inside each .pdata function."""
import struct, sys
img = open(sys.argv[1], 'rb').read(); b = 0x82000000
W = lambda a: struct.unpack_from('>I', img, a - b)[0]
sizes = {}
for o in range(0x128000, 0x128000 + 0x15C88, 8):
    st, k = struct.unpack_from('>II', img, o)
    if st: sizes[st] = ((k >> 8) & 0x3FFFFF) * 4
def cstr(a):
    o = a - b
    if not 0 <= o < len(img): return None
    e = img.find(b'\0', o, o + 120); s = img[o:e]
    return s.decode('latin1') if e > o and all(32 <= c < 127 for c in s) else None
for arg in sys.argv[2:]:
    start = int(arg, 16); n = sizes.get(start, 0x200)
    print('== %08X +%X' % (start, n)); regs = {}
    for a in range(start, start + n, 4):
        w = W(a); op = w >> 26
        if op == 15: regs[(w >> 21) & 31] = (w & 0xFFFF) << 16
        elif op == 14 and ((w >> 16) & 31) in regs:
            imm = w & 0xFFFF; imm -= 0x10000 if imm & 0x8000 else 0
            t = (regs[(w >> 16) & 31] + imm) & 0xFFFFFFFF; s = cstr(t)
            if s: print('  %08X -> %08X "%s"' % (a, t, s))
        elif op == 18 and w & 3 == 1:
            d = w & 0x3FFFFFC; d -= 0x4000000 if d & 0x2000000 else 0
            print('  %08X bl %08X' % (a, (a + d) & 0xFFFFFFFF))
