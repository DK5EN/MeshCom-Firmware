"""Assemble results.json from work/*.json (run after all analysis scripts)."""
import json, time; 
from common import NODES, NOLOG, OUTD, STANDALONE, cancel_node, load, median
W=lambda f: json.load(open(OUTD/'work'/f))
led=W('ledger.json'); stat=W('statagg.json'); opp=W('opportunity.json'); bnd=W('bounds.json'); alone=W('alone.json'); alone2=W('alone2.json'); alone3=W('alone3.json')
loops=W('loops.json'); own=W('ownfate.json'); txw=W('txwait.json'); cet=W('cet.json'); ackf=W('ackfwd.json')
# node clock offset
off={}
for n in NODES:
    d=[]
    for e in load(n):
        if e['k']=='rx' and e.get('nt'):
            h,m,s=(int(x) for x in e['nt'].split(':'))
            lt=time.localtime(e['t']); d.append(((lt.tm_hour*3600+lt.tm_min*60+lt.tm_sec)-(h*3600+m*60+s)+43200)%86400-43200)
    off[n]=median(d)
print('node clock minus logger offsets (s, node = logger - x):',off)
nodes={}
for n in NODES+STANDALONE:
    ph=led[n]['per_hour']
    peak=max(ph.items(),key=lambda kv: sum(v for k,v in kv[1].items() if k.startswith('tx_')))[0] if n not in STANDALONE else None
    nodes[n]={'totals':led[n]['totals'],'per_hour':ph,'relay_fate':led[n]['relay_fate'],'relay_fate_by_type':led[n]['relay_fate_by_type'],
              'relay_queue_wait_median_s':led[n]['relay_wait_median_s'],'stat_windows':stat[n],'cancel_opportunity':opp[n],
              'own_beacon_fate':own[n],'tx_wait':txw[n],'cet':cet[n],'ack_forwarding':ackf[n],'loops':loops[n],'alone_hist':alone[n],'alone_classes':alone2[n],
              'persistent_alone':alone3.get(n),'node_clock_offset_s':off.get(n),'peak_tx_hour':peak}
    if n in bnd: nodes[n]['opportunity_airtime']=bnd[n]
network={'redundancy':W('redundancy2.json'),'redundancy_v1_nodes_per_id':W('redundancy.json')['nodes_per_id_all'],'link_probability':W('linkp.json'),
 f'cancel_eval_{cancel_node()}':W('cancel_eval.json'),'loss':W('loss.json'),'gateway':W('gateway.json'),'ping_share':W('pingshare.json'),'cluster_opportunity_airtime':bnd['_cluster'],'clockcheck':W('clockcheck.json')}
for n in NOLOG:
    kinds={}
    for e in load(n):
        kinds[e['k']]=kinds.get(e['k'],0)+1
    nodes[n]={'events_by_kind':kinds,'unavailable':{
        'relay_fate_cancel_opportunity_ledger':'no [LOG] RLY/TX/RX lines (--setlog on missing): relay decisions and queue waits not reconstructable',
        'redundancy_link_probability_loss':'node left out of the cross-node steps; its RX/TX records are missing',
        'stat_windows':'no [LOG] STAT lines'}}
json.dump({'nodes':nodes,'network':network},open(OUTD/'results.json','w'),indent=1)
print('ok',(OUTD/'results.json').stat().st_size)
