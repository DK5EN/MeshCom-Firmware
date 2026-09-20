import math

def frac(x, nd=4):
    return round(x, nd)

print("=== 2.1 Stern ===")
A = 1+1+1+1+1
print("A importance (sum of 5x1):", A)
B = 1/5
print("B importance (1/5):", B)

print("\n=== 2.2 Lineare Kette ===")
A = 1/2
B = 1/1 + 1/2
C = 1/2 + 1/2
D = 1/2 + 1/1
E = 1/2
print("A:", A, "B:", B, "C:", C, "D:", D, "E:", E)

print("\n=== 2.3 Dichtes Mesh ===")
each = 5*(1/5)
print("each:", each)

print("\n=== 2.4 Bridge ===")
A = 1/3+1/3+1/6
X = 6*(1/3)
print("A:", A, "X:", X)

print("\n=== 2.5 Zwei Bridges ===")
A = 1/4+1/4+1/7+1/7
X = 6*(1/4) + 1/7
print("A:", frac(A), "X:", frac(X))

print("\n=== 2.6 Berg-Hub vs Stadt-Node ===")
berg_vals = [1,1,2,1,3,2,1,2,1,3]
berg_imp = sum(1/v for v in berg_vals)
print("Berg-Hub importance:", frac(berg_imp,4), "n=", len(berg_vals))

stadt_vals = [8,10,12,9,11,8,10,9,12,10]
stadt_imp = sum(1/v for v in stadt_vals)
print("Stadt-Node importance:", frac(stadt_imp,4), "n=", len(stadt_vals))
print("ratio berg/stadt:", berg_imp/stadt_imp)

print("\n=== 3 Mixed mode example ===")
known = [1/5, 1/3, 1/8]
imp1 = sum(known) + 3*1.0
print("Importance (3 known + 3 unknown@1.0):", frac(imp1,4))

known2 = [1/5,1/3,1/8,1/7,1/4,1/10]
imp2 = sum(known2)
print("Importance (all 6 known):", frac(imp2,4))

print("\n=== 4.2 / 4.3 slot mapping ===")
RELAY_TOTAL_SLOTS = 10
RELAY_JITTER_WIDTH = 3
IMP_CAP = 8.0

def slot_start_for(imp):
    imp_capped = min(imp, IMP_CAP)
    ratio = imp_capped / IMP_CAP
    ss = int((1.0-ratio)*(RELAY_TOTAL_SLOTS-RELAY_JITTER_WIDTH))
    return ratio, ss

for imp in [8.0, 7.2, 5.0, 2.0, 1.0, 0.5, 0.2]:
    ratio, ss = slot_start_for(imp)
    print(f"imp={imp}: ratio={ratio:.4f} slot_start={ss} slots=[{ss},{ss+1},{ss+2}]")

print("\nEdge cases:")
for imp in [0.0, 0.01, 0.03, 0.1, 7.99, 8.0, 8.5, 100.0]:
    ratio, ss = slot_start_for(imp)
    print(f"imp={imp}: ratio={ratio:.4f} raw=(1-ratio)*7={ (1-ratio)*7:.4f} slot_start={ss}")

print("\nmax possible slot_start (imp->0):")
ratio, ss = slot_start_for(0.0)
print("ratio=0 -> (1-0)*7=7.0 -> int=7 -> slot_start=7 -> slots [7,8,9]")

print("\n=== jitter ms and backoff for table 4.3 ===")
CSMA_SLOT_SIZE=35
def jitter_range(ss):
    lo = ss*CSMA_SLOT_SIZE
    hi = (ss+2)*CSMA_SLOT_SIZE
    return lo,hi
for imp in [8.0,7.2,5.0,2.0,1.0,0.5,0.2]:
    ratio, ss = slot_start_for(imp)
    lo,hi = jitter_range(ss)
    print(f"imp={imp} slot_start={ss} jitter={lo}..{hi} backoff(base4500)={4500+lo}..{4500+hi}")

print("\n=== 4.4 retry table ===")
base0=4500
base1 = base0*5//6  # integer division as in code: base * 5 / 6 in C (int)
base1f = base0*5/6
base2 = base0*2//3
base2f = base0*2/3
print("base0", base0, "base1 int", base1, "base1 float", base1f, "base2 int", base2, "base2 float", base2f)

for imp,ssrange in [(7.2,(0,2)),(5.0,(2,4)),(2.0,(5,7)),(1.0,(6,8)),(0.2,(6,8))]:
    ratio, ss = slot_start_for(imp)
    lo,hi = ss, ss+2
    for attname, b in [("Att0",4500),("Att1",3750),("Att2",3000)]:
        print(f"imp={imp} {attname} base={b} range={b+lo*35}..{b+hi*35}")

print("\n=== Vorsprung claims ===")
# Berg-Hub slot0 vs Bridge slot5 (105ms claim in 4.3), and slot0 vs cluster slot6 (210ms claim in 4.4/4.5)
print("slot0 vs slot5 (Bridge) delta ms:", (5-0)*35)
print("slot0 vs slot6 (Cluster/Blatt) delta ms:", (6-0)*35)
print("slot2 vs slot5 delta ms (Berg-Hub slot2 min vs Bridge slot5 min):", (5-2)*35)

print("\n=== dedup ring rotation ===")
rate = 4.2  # msg/min
for slots in [60,70,100,200,256]:
    minutes = slots/rate
    print(f"slots={slots} rate={rate}/min -> rotation {minutes:.2f} min")

print("\n=== jitter today (Prio3) ===")
print("random(0, CSMA_PRIO_SLOTS_3+1)=random(0,11) range 0..10 slots x35 = 0..350ms")
print("new scheme max slot=9 -> 9*35=", 9*35, "ms; with 3-slot window max slot could be 9? check slot_start max")

print("\n=== IMP_CAP alt check for 3.1 gate scenario ===")
# 'Importance = NC_self' claim: if all neighbors unknown, each contributes 1.0, sum = NC_self count (number of active neighbors)
print("If active neighbor count == NC_self by definition, and all unknown contribute 1.0, importance = count of active neighbors = NC_self. Consistent IF NC_self counted the same neighbor set used in the sum (same 1h window).")

print("\n=== integer fixed point check ===")
for nc in [1,2,3,5,7,8,9,10,11,12,20,99]:
    print(f"nc={nc} float 100/nc={100/nc:.3f} int 100//nc={100//nc}")

print("\n=== integer truncation worst-case relative error ===")
worst = None
for nc in range(1,100):
    exact = 100.0/nc
    trunc = 100//nc
    if trunc>0:
        relerr = (exact-trunc)/exact
    else:
        relerr = 1.0
    if worst is None or relerr>worst[0]:
        worst = (relerr, nc, exact, trunc)
print("worst relative error case:", worst)

# check a handful
for nc in [3,7,8,9,11,12,13,17]:
    exact=100/nc; trunc=100//nc
    print(f"nc={nc} exact={exact:.3f} trunc={trunc} relerr={ (exact-trunc)/exact*100:.1f}%")

print("\n=== Node B (4.5) slot check ===")
imp=1.5
ratio = min(imp,8.0)/8.0
ss = int((1-ratio)*7)
print("Imp=1.5 -> ratio=",ratio," slot_start=",ss," slots=",[ss,ss+1,ss+2], "range=", 4500+ss*35, "..", 4500+(ss+2)*35)
print("ADR claims: Slots 6..8 -> 4710..4780ms (INCORRECT per formula)")

print("\n=== hop count % sum ===")
print(10.5+19.0+26.8+23.7+20.7)

print("\n=== 210ms 'immer' worst case check ===")
# Hub slots 0..2, Blatt slots 6..8
hub_slots=[0,1,2]
blatt_slots=[6,7,8]
mindelta = min(b-h for h in hub_slots for b in blatt_slots)
maxdelta = max(b-h for h in hub_slots for b in blatt_slots)
print("min delta slots:", mindelta, "-> ms:", mindelta*35)
print("max delta slots:", maxdelta, "-> ms:", maxdelta*35)
print("center-to-center (slot0 vs slot6):", (6-0)*35)

print("\n=== 4.7a spreizung for TOTAL=16,20 ===")
WIDTH=3
for TOTAL in [10,16,20]:
    max_slot_start = TOTAL-WIDTH-1
    max_slot = max_slot_start+(WIDTH-1)
    naive = TOTAL*35
    correct = max_slot*35
    print(f"TOTAL={TOTAL}: max_slot_start={max_slot_start} max_slot={max_slot} naive(TOTAL*35)={naive}ms correct={correct}ms")
