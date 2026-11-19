"""import_names.py <out/imports.txt> <xam_table.inc> <xboxkrnl_table.inc> <runtime src dir>...
Names PGR3's import ordinals and reports which ones each given runtime implements (by name in its source)."""
import re, sys, os
def table(p):
    t={}
    for m in re.finditer(r'XE_EXPORT\(\s*\w+\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(\w+)', open(p).read()): t[int(m.group(1),16)]=m.group(2)
    return t
tabs={'xam.xex':table(sys.argv[2]),'xboxkrnl.exe':table(sys.argv[3])}
srcs=[]
for d in sys.argv[4:]:
    txt=''
    for r,_,fs in os.walk(d):
        for f in fs:
            if f.endswith(('.cpp','.h','.inc')): txt+=open(os.path.join(r,f),encoding='utf-8',errors='replace').read()
    srcs.append(('PGR4' if 'PGR4' in d else 'FM3' if 'FM3' in d else d, txt))
for line in open(sys.argv[1]):
    lib,kind,*ords=line.split()
    for o in ords:
        o=int(o,16); n=tabs[lib].get(o,'?%X'%o)
        have=[name for name,t in srcs if re.search(r'\b'+re.escape(n)+r'\b',t)]
        print('%-12s %-8s %03X %-40s %s'%(lib,kind,o,n,'+'.join(have) if have else 'MISSING'))
