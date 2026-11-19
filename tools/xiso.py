import sys, struct, os
MAGIC=b'MICROSOFT*XBOX*MEDIA'
def open_iso(p):
    f=open(p,'rb')
    for base in (0, 0xFD90000, 0x18300000, 0x2080000):
        f.seek(base+0x10000)
        if f.read(20)==MAGIC:
            root_sec,root_size=struct.unpack('<II',f.read(8))
            return f,base,root_sec,root_size
    raise SystemExit('no XDVDFS')
def walk(f,base,sec,size,path=''):
    f.seek(base+sec*2048); data=f.read(size)
    seen=set(); stack=[0]; out=[]
    while stack:
        off=stack.pop()
        if off in seen or off+14>len(data): continue
        seen.add(off)
        l,r,s,sz,attr,nl=struct.unpack('<HHIIBB',data[off:off+14])
        if l==0xFFFF: continue
        name=data[off+14:off+14+nl].decode('latin1')
        if l: stack.append(l*4)
        if r: stack.append(r*4)
        full=path+'/'+name
        if attr&0x10:
            if sz: out+=walk(f,base,s,sz,full)
        else: out.append((full,s,sz))
    return out
if __name__=='__main__':
    f,base,rs,rz=open_iso(sys.argv[1])
    files=walk(f,base,rs,rz)
    if sys.argv[2]=='list':
        for n,s,z in sorted(files): print(z,n)
        print(len(files),'files',sum(z for _,_,z in files),'bytes',file=sys.stderr)
    else:
        dest=sys.argv[3]; want=sys.argv[4:]  # empty = all
        for n,s,z in files:
            if want and n.lstrip('/').lower() not in [w.lower() for w in want]: continue
            p=os.path.join(dest,n.lstrip('/')); os.makedirs(os.path.dirname(p),exist_ok=True)
            f.seek(base+s*2048); left=z
            with open(p,'wb') as o:
                while left:
                    c=f.read(min(left,1<<24)); o.write(c); left-=len(c)
