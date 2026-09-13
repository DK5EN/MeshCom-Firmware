import re, json, collections
lines = [l.rstrip('\n') for l in open('BG7OSL_raw_all.txt') if l.strip()]
def unhex(s):
    b = bytearray()
    i = 0
    out = bytearray()
    for m in re.finditer(r'<0x([0-9a-f]{2})>|(.)', s, re.S):
        if m.group(1): out.append(int(m.group(1),16))
        else: out.extend(m.group(2).encode('utf-8'))
    return out.decode('utf-8', 'replace')
pk = []
for l in lines:
    m = re.match(r'(\S+ \S+) CEST: (\S+?)>([^:]+):(.*)$', l)
    if not m: print('NOPARSE', l[:80]); continue
    ts, src, path, body = m.groups()
    dup = ' [Duplicate position packet]' in body
    body = body.replace(' [Duplicate position packet]','')
    body = unhex(body).replace('\xa0',' ')
    server = path.split(',')[-1]
    typ = body[:1]
    d = dict(ts=ts, src=src, path=path, server=server, body=body, typ=typ, dup=dup)
    if typ == ':':
        mm = re.match(r':([^:]{9}):(.*)$', body)
        if mm:
            d['to'] = mm.group(1).strip(); d['msg'] = mm.group(2)
            mid = re.search(r'\{(\w+)$', d['msg'])
            d['msgid'] = mid.group(1) if mid else None
    pk.append(d)
print('total', len(pk))
print('by type', collections.Counter(p['typ'] for p in pk))
print('by server', collections.Counter(p['server'] for p in pk))
print('by date', collections.Counter(p['ts'][:10] for p in pk))
inv = [p for p in pk if p.get('msg','').startswith('INVITE ')]
print('invites', len(inv), 'span', min(p['ts'] for p in inv), max(p['ts'] for p in inv))
print('invite groups', collections.Counter(p['msg'].split()[1] for p in inv))
targets = sorted(set(p['to'] for p in inv))
print('unique targets', len(targets))
base = collections.Counter(re.split(r'-', t)[0] for t in targets)
print('unique base calls', len(base))
def prefix(c):
    m = re.match(r'([A-Z]{1,2}\d?)', c); return m.group(1) if m else c[:2]
pre = collections.Counter(prefix(t) for t in targets)
print('prefix top', pre.most_common(40))
print('first letters', sorted(collections.Counter(t[0] for t in targets).items()))
print('msgids', min(int(p['msgid']) for p in inv if p['msgid'] and p['msgid'].isdigit()), max(int(p['msgid']) for p in inv if p['msgid'] and p['msgid'].isdigit()))
print('per-second', sorted(collections.Counter(p['ts'] for p in inv).items()))
print('invite servers', collections.Counter(p['server'] for p in inv))
db = [p for p in pk if p.get('to','').startswith('DB0') or p.get('to','').startswith('DK5EN') or p.get('to','').startswith('OE')]
print('DB0/OE targets', len(db), [p['to'] for p in db][:60])
# non-invite messages sent
oth = [p for p in pk if p['typ']==':' and not p.get('msg','').startswith('INVITE ')]
print('other msgs', len(oth))
for p in oth: print(' ', p['ts'], p['to'], '|', p['msg'])
# statuses
st = collections.Counter(p['body'] for p in pk if p['typ']=='>')
print('status', st.most_common(10))
pos = [p for p in pk if p['typ']=='!']
print('positions', len(pos), 'dups', sum(p['dup'] for p in pos))
print('pos sample', pos[0]['body'], '|', pos[-1]['body'])
print('pos unique coords', collections.Counter(p['body'][:19] for p in pos).most_common(5))
# per hour counts
print('per hour', sorted(collections.Counter(p['ts'][:13] for p in pk).items()))
json.dump(dict(targets=targets, prefixes=pre.most_common(), inv_n=len(inv), pk_n=len(pk)), open('analysis.json','w'))
