import re
s=open('/Users/martinwerner/Desktop/Nachbarschaftsmatrix-Kantenpool.html',encoding='utf-8').read()
i=s.find('Anhang B: Schnappschuss')
pre=re.search(r'<pre>X,Y,Summe\n(.*?)</pre>',s[i:],re.S).group(1)
rows=dict(re.findall(r'<tr><td class=num>(\d+)</td><td>([A-Z0-9-]+)</td>',s[i:i+6000]))
rows={int(k):v for k,v in rows.items()}
E={}
for l in pre.strip().splitlines():
    x,y,c=map(int,l.split(',')); E[(x,y)]=c   # y heard x
print('edges',len(E))
# #N check: #N(row) = count of edges with Y=row
for r in sorted(rows):
    print(r,rows[r],'#N=',sum(1 for (x,y) in E if y==r),end='; ')
print()
ME=1
hears=lambda m:{x for (x,y) in E if y==m}       # stations m heard
heardBy=lambda m:{y for (x,y) in E if x==m}     # stations that heard m
D=hears(ME)          # I heard them (direct)
HM=heardBy(ME)       # they heard me
print('D (I heard)',sorted(D),'HM (heard me)',sorted(HM))
K=D&HM; print('K=D&HM',[rows[k] for k in sorted(K)])
N2=set().union(*[hears(m) for m in D])-D-{ME}
print('N2',len(N2),sorted(N2))
for m in sorted(K):
    ex=[x for x in N2 if m in hears_x for hears_x in [None]] if False else None
excl={m:sorted(x for x in N2 if {k for k in K if x in hears(k)}=={m}) for m in K}
for m in sorted(K): print(rows[m],'hears N2:',sorted(hears(m)&N2),'exclusive:',excl[m])
# feeder rule for m=DL2JA-2: F in K, F!=m, 'm hoert F' = edge (F,m) = m heard F
m=2
print('Feeder candidates (edge F->m = DL2JA-2 heard F):',{rows[F]:E.get((F,m),0) for F in sorted(K-{m})})
print('Reverse (edge m->F = F heard DL2JA-2):',{rows[F]:E.get((m,F),0) for F in sorted(K-{m})})
# reverse-direction evidence for coverage: x heard m  (edge (m,x))
for mm in sorted(K):
    print(rows[mm],'N2 stations with evidence they heard it:',sorted(x for x in N2 if (mm,x) in E))
print('N2 stations with any evidence of hearing a K member:',sorted(x for x in N2 if any((k,x) in E for k in K)))
print('stations that heard DL2UD-1:',[rows[y] for (x,y) in E if x==4])
