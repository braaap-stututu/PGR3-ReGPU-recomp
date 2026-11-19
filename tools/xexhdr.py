import sys,struct
d=open(sys.argv[1],'rb').read()
magic,flags,pe_off,res,sec_off,nopt=struct.unpack('>4sIIIII',d[:24])
print('magic',magic,'flags %08X'%flags,'pe_off %X'%pe_off,'sec_off %X'%sec_off)
names={0x2FF:'resource info?',0x3FF:'base file format',0x5FF:'delta patch',0x10001:'entry point',0x10100:'image base',0x103FF:'import libraries',0x18002:'checksum/timestamp',0x183FF:'original PE name',0x200FF:'static libraries',0x20104:'TLS info',0x20200:'default stack',0x20301:'default fs cache',0x20401:'default heap',0x40006:'execution info',0x40201:'game ratings?',0x30000:'system flags',0x40310:'game ratings',0x40404:'LAN key',0x406FF:'multidisc media ids',0x407FF:'alt title ids',0x80020:'?',0x2FF:'resource info',0x4004:'?'}
opts={}
for i in range(nopt):
    k,v=struct.unpack('>II',d[24+i*8:32+i*8]); opts[k]=v
    print('opt %08X %-22s %08X'%(k,names.get(k,''),v))
if 0x40006 in opts:
    o=opts[0x40006]; mid,ver,bver,tid=struct.unpack('>IIII',d[o:o+16])
    print('media %08X version %08X base %08X title %08X'%(mid,ver,bver,tid), 'plat/exe/disc', d[o+16:o+24].hex())
if 0x183FF in opts:
    o=opts[0x183FF]; l=struct.unpack('>I',d[o:o+4])[0]; print('PE name',d[o+4:o+l].split(b'\0')[0])
if 0x200FF in opts:
    o=opts[0x200FF]; l=struct.unpack('>I',d[o:o+4])[0]
    for p in range(o+4,o+l,16):
        n=d[p:p+8].split(b'\0')[0].decode(); a,b,c,e=struct.unpack('>HHHH',d[p+8:p+16]); print('  lib %-10s %d.%d.%d.%d'%(n,a,b,c,e&0x7fff))
if 0x103FF in opts:
    o=opts[0x103FF]; size,strsz,cnt=struct.unpack('>III',d[o:o+12]); s=d[o+12:o+12+strsz].split(b'\0')
    print('import libs:',[x.decode() for x in s if x])
if 0x10100 in opts: print('image base %08X'%opts[0x10100])
print('entry %08X'%opts.get(0x10001,0))
if 0x3FF in opts:
    o=opts[0x3FF]; l,enc,comp=struct.unpack('>IHH',d[o:o+8]); print('encryption',enc,'compression',comp)
print('security: image size %08X'%struct.unpack('>I',d[sec_off+4:sec_off+8])[0])
