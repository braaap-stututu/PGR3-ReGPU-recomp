import sys,struct
x=open(sys.argv[1],'rb').read(); img=open(sys.argv[2],'rb').read(); base=0x82000000
n=struct.unpack('>I',x[20:24])[0]
opts=dict(struct.unpack('>II',x[24+i*8:32+i*8]) for i in range(n))
o=opts[0x103FF]; size,strsz,cnt=struct.unpack('>III',x[o:o+12])
names=[s.decode() for s in x[o+12:o+12+strsz].split(b'\0') if s]
p=o+12+strsz
for i in range(cnt):
    lsz=struct.unpack('>I',x[p:p+4])[0]
    ver,minver=struct.unpack('>II',x[p+0x1c:p+0x24])
    ni,c=struct.unpack('>HH',x[p+0x24:p+0x28])
    recs=struct.unpack('>%dI'%c,x[p+0x28:p+0x28+4*c])
    ords=[]; 
    for r in recs:
        v=struct.unpack('>I',img[r-base:r-base+4])[0]
        ords.append(((v>>24),v&0xFFFF))
    print('LIB %s ver %08X min %08X records %d imports %d'%(names[ni],ver,minver,c,len(ords)))
    ords=sorted(set(o for t,o in ords if t in (0,1)) ); print(len(ords),'unique'); print(' '.join('%X'%o for o in ords))
    p+=lsz
