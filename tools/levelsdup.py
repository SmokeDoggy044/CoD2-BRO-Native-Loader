#!/usr/bin/env python3
"""Level-section functions with the same body (template code the levels' compiler instantiated once per level).

Bodies are compared as disassembly with every address/large immediate masked, except the engine-interface slots
(0x00922xxx), so copies that differ only in strings, hashes or callee addresses group together. Writes
build/levels_groups.csv (group, size, module, address, name) and prints a summary.
usage: tools/levelsdup.py   (about 40 s; objdump per function)
"""
import sys,csv,collections,re,bisect,hashlib
sys.path.insert(0,'tools')
import fnpack
f,_=fnpack.load_names(); st=sorted(f); img=fnpack.Image()
rows=[r for r in csv.DictReader(open('tools/data/progress.csv')) if r['section']=='levels']
H=collections.defaultdict(list); sizes={}
hexre=re.compile(r'0x[0-9a-f]{5,}')
for r in rows:
    a=int(r['address'],16); i=bisect.bisect_right(st,a); n=(st[i]-a) if i<len(st) else 0
    b=img.read(a,n); sizes[a]=n
    toks=[]
    for _,_,t in fnpack.disassemble(img,a,n):
        toks.append(hexre.sub(lambda m: m.group(0) if m.group(0).startswith('0x922') else 'X', t))
    H[hashlib.md5('\n'.join(toks).encode()).hexdigest()].append((r['module'],r['name'],a))
groups=sorted(H.values(),key=len,reverse=True)
dupfun=sum(len(g) for g in groups if len(g)>1)
print('functions',len(rows),'unique bodies',len(groups),'in dup groups',dupfun,'groups>1',sum(1 for g in groups if len(g)>1))
print('unique bytes',sum(sizes[g[0][2]] for g in groups),'of',sum(sizes.values()))
for g in groups[:25]: print(len(g),sizes[g[0][2]],g[0][1],'|',next((x[1] for x in g if not x[1].startswith('FUN_')),''))
pro=[g for g in groups if any(m=='prologue' for m,_,_ in g)]
print('prologue bodies',len(pro),'shared with another level',sum(1 for g in pro if any(m!='prologue' for m,_,_ in g)))
with open('build/levels_groups.csv','w',newline='') as out:
    w=csv.writer(out); w.writerow(['group','size','module','address','name'])
    for n,g in enumerate(groups):
        for m,name,a in g: w.writerow([n,sizes[a],m,f'{a:08x}',name])
