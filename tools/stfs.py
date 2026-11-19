import sys,struct,os
# Minimal STFS (LIVE/PIRS/CON) reader: list/extract
def u24le(b): return b[0]|b[1]<<8|b[2]<<16
class Stfs:
    def __init__(s,p):
        s.f=open(p,'rb'); h=s.f.read(0xA000)
        s.hdr_size=struct.unpack('>I',h[0x340:0x344])[0]
        vd=h[0x379:0x379+0x24]
        s.block_sep=vd[2]; s.ft_blocks=struct.unpack('<H',vd[3:5])[0]; s.ft_start=u24le(vd[5:8])
        s.total=struct.unpack('>I',vd[0x14:0x18])[0]
        s.first=((s.hdr_size+0xFFF)&~0xFFF)
        s.shift = 0 if (s.block_sep&1) else 1  # female/male package: 1 or 2 hash tables per level
    def blk_off(s,b):
        base=(b+0xAA)//0xAA
        if s.first==0xB000: pass
        a=b+(base<<s.shift)
        if b>=0xAA: a+=((b+0x70E4)//0x70E4)<<s.shift
        if b>=0x70E4: a+=((b+0x4AF768)//0x4AF768)<<s.shift
        return s.first+a*0x1000
    def hash_off(s,b):
        # level-0 hash table position for block b
        rec=(b//0xAA)*(0xAB<<s.shift if False else 0)
        n=b//0xAA
        a=n*0xAA + (n<<s.shift)
        if b>=0xAA: a+=((b//0x70E4)+1)<<s.shift
        if b>=0x70E4: a+=1<<s.shift
        return s.first+a*0x1000
    def next_blk(s,b):
        o=s.hash_off(b)+(b%0xAA)*0x18
        s.f.seek(o); e=s.f.read(0x18); return u24le(bytes([e[0x17],e[0x16],e[0x15]]))
    def read_blk(s,b):
        s.f.seek(s.blk_off(b)); return s.f.read(0x1000)
    def files(s):
        out=[]; b=s.ft_start
        for i in range(s.ft_blocks):
            d=s.read_blk(b)
            for j in range(0,0x1000,0x40):
                e=d[j:j+0x40]; nl=e[0x28]&0x3F
                if nl==0: continue
                name=e[:nl].decode('latin1'); flags=e[0x28]
                consec=bool(flags&0x40); isdir=bool(flags&0x80)
                nb=u24le(e[0x29:0x2C]); start=u24le(e[0x2F:0x32])
                parent=struct.unpack('>H',e[0x32:0x34])[0]; size=struct.unpack('>I',e[0x34:0x38])[0]
                out.append(dict(name=name,dir=isdir,consec=consec,nb=nb,start=start,parent=parent,size=size))
            b=s.next_blk(b)
        return out
    def path(s,fl,e):
        p=e['name']
        while e['parent']!=0xFFFF: e=fl[e['parent']]; p=e['name']+'/'+p
        return p
    def read(s,e):
        out=bytearray(); b=e['start']; left=e['size']
        for i in range(e['nb']):
            d=s.read_blk(b); out+=d[:min(left,0x1000)]; left-=0x1000
            b = b+1 if e['consec'] else s.next_blk(b)
        return bytes(out)
if __name__=='__main__':
    st=Stfs(sys.argv[1]); fl=st.files()
    for e in fl:
        p=st.path(fl,e)
        print(('D ' if e['dir'] else 'F ')+'%10d %s'%(e['size'],p))
        if len(sys.argv)>2 and not e['dir']:
            dst=os.path.join(sys.argv[2],p); os.makedirs(os.path.dirname(dst),exist_ok=True)
            open(dst,'wb').write(st.read(e))
