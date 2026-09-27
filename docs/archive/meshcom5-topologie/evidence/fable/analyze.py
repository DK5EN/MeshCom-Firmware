import re

own_ids = []
with open('own_pos.log') as f:
    for line in f:
        m = re.search(r'\sx([0-9A-Fa-f]{8})\s', line)
        if m:
            own_ids.append(m.group(1))

print("own POS count:", len(own_ids), "unique:", len(set(own_ids)))

# build echo map: msgid -> list of (path_list, line)
echo_by_id = {}
with open('combined_window.log') as f:
    for line in f:
        if '[LOG]' not in line:
            continue
        m = re.search(r'\sx([0-9A-Fa-f]{8})\s+H\d\d\s+S\d\s+T\d\s+M\d\d\s+([A-Za-z0-9\-,]+)>', line)
        if not m:
            continue
        msgid = m.group(1)
        path = m.group(2).split(',')
        echo_by_id.setdefault(msgid, []).append((path, line.strip()))

no_echo = 0
with_echo = 0
first_hand = {}
second_only = {}
appears_any = {}
per_id_detail = {}

for oid in own_ids:
    entries = echo_by_id.get(oid, [])
    # filter to those where path[0] == DK5EN-98 (i.e., we are origin) and path length>=2
    relevant = [e for e in entries if len(e[0])>=2 and e[0][0]=='DK5EN-98']
    if not relevant:
        no_echo += 1
        per_id_detail[oid] = []
        continue
    with_echo += 1
    seen_first = set()
    seen_any = set()
    for path, line in relevant:
        # path like DK5EN-98, X, Y, Z...
        first = path[1]
        seen_first.add(first)
        for tok in path[1:]:
            seen_any.add(tok)
    for st in seen_first:
        first_hand[st] = first_hand.get(st,0)+1
    for st in seen_any:
        appears_any[st] = appears_any.get(st,0)+1
    per_id_detail[oid] = (seen_first, seen_any)

print("own POS with >=1 echo:", with_echo, "without echo:", no_echo)

# second-hand-only = appears in seen_any but not seen_first, counted per own-POS-id
second_only_count = {}
for oid, detail in per_id_detail.items():
    if not detail:
        continue
    seen_first, seen_any = detail
    for st in seen_any:
        if st not in seen_first:
            second_only_count[st] = second_only_count.get(st,0)+1

print("\nNeighbour | first-hand (own POS count) | second-hand-only (own POS count)")
all_st = set(first_hand) | set(second_only_count)
for st in sorted(all_st, key=lambda s: -(first_hand.get(s,0))):
    fh = first_hand.get(st,0)
    so = second_only_count.get(st,0)
    print(f"{st:12s} {fh:4d} {so:4d}")
