# Evidence Pack: NC-Importance ADR Review (2026-08-22)

Review target: `/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/docs/adr-nc-importance-backoff.md` (Rev. 2)
Firmware tree: `/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main` (branch v4.35p_prio)

## Production data source and method

- MeshCom production instance meshmap.oevsv.at (mcmap proxy), queried via its MCP API 2026-08-22.
- 24h HEY harvest window: 2026-08-21T14:12 UTC to 2026-08-22T14:12 UTC, from `interlink.log`
  RAW datagrams (`"type":"hey"` JSON lines). 134,576 raw lines harvested, 96,074 unique frames
  after line-dedup (a resume overlap produced exact duplicate lines).
- Files in this directory (`/private/tmp/claude-501/-Users-martinwerner-WebDev-MeshCom-Firmware-DEV-Main/c8cb9f1a-57b2-4601-94e1-df9f1d704ef8/scratchpad/`):
  - `heys.jsonl` — raw harvested HEY feed lines: `{type, path, rssi, nct, msg_id, gw, timestamp}`
  - `fleet.json` — all 1430 node DB records (call, hardware, firmware, lastSeen, ...)
  - `linkload24.json` / `linkload7d.json` — full-network directed HEY-path segment loads
    (`fromCall, toCall, traversals, relayed, origins`), 24h and 7d windows
  - `analyze_heys.py` -> `heys_analysis.txt` — main 24h analysis (distributions, leaderboards,
    importance simulation, slot mapping)
  - `analyze_deep.py` -> `deep_analysis.txt` — relay mheard segments, IMP_CAP alternatives
    (8/4/3/2), top-relay slot concordance, zombie-vs-wrap spans, spike attribution
  - `analyze_links.py` — link-load aggregation script
  - `mcp.py` / `harvest.py` — data acquisition scripts (curl JSON-RPC to the MCP endpoint)

## Feed semantics (from mcmap `docs://hey-format` resource, verified against firmware)

- Feed `path` = `Origin[,Relay1,...,LastTx]`; a 1-element path with empty `rssi` is a gateway
  SELF-UPLOAD (no RF measurement). 63.8% of frames are self-uploads.
- Feed `nct` = origin's reported mheard count (`R<NC>;` payload, firmware >= 4.35n;
  `sendHey()` at `src/loop_functions.cpp:4273`).
- `rssi` string: 3-value segments = `mheard,rssi,snr` appended by relays >= 4.35n;
  2-value mid-segments = old-firmware relays; final 2-value segment = reporting gateway's
  own reception (server-appended from UDP DATA header).
- Blind spots (must be stated wherever feed numbers are used): at most one report per
  transmitted frame network-wide (gateway internet race); `S1` flag stops further gateway
  uploads; trickle suppresses HEYs of well-connected nodes; feed traffic is NOT channel
  occupancy/airtime.
- Feed availability: 3 short INTERLINK outages 2026-08-21 ~17:22-18:15 UTC explain the
  17:00-hour dip (1903 frames vs ~4000 normal). Uptime 97.5% over the window.

## Headline numbers (from heys_analysis.txt / deep_analysis.txt — verify there)

- Network: 1430 nodes in DB, 1323 distinct HEY origins in 24h, 505 gateways registered,
  ~1370-1440 stations active (trailing 3h gauge). ~3842 HEY frames/h average in feed.
- Fleet firmware: 4.35p = 78.2%; >= 4.35n (sends R<NC>) ≈ 81%; older ≈ 17-19%.
- Fleet hardware: TLORA 32%, HELTEC V3 20%, TBEAM V1.2 16%, TBEAM V1.1 9% (T-Beam class
  has MAX_MHEARD=10 in current firmware, ESP32 classic 30, S3/nRF52 80).
- nct distribution over 1323 origins (latest frame): 15.3% report 0; of the 1121 with
  nct>0: min 1, p25 2, median 3, p75 7, p90 11, max 36. 20.0% of all origins report nct=1.
- Path lengths: 1: 63.8%, 2: 19.8%, 3: 9.8%, 4: 4.5%, 5: 2.1%, 6: 9 frames.
- Importance simulation (508 nodes observed as receivers; graph: consecutive path pairs
  X->Y = "Y heard X"; contribution 1/nct if origin nct>0 else 1.0):
  percentiles 10/25/50/75/90/95/99 = 0.17/0.34/0.95/1.77/3.11/3.95/6.52, max 9.24.
  Only 4 nodes >= IMP_CAP 8.
- Slot mapping (ADR 4.2, slot_start = (1-min(imp,CAP)/CAP)*7):
  CAP=8 -> 60.4% of nodes in slots 6..8, 0.8% in slots 0..2.
  CAP=4 -> 37.6% back, 7.5% front. CAP=3 -> 28.9% back, 14.2% front.
- Top-20 real relays (by mid-path appearances) landing in slots 0..2: CAP=8: 8/20,
  CAP=3: 17/20. Notable outlier: IU5CZN-10, relay load rank 9 (489 appearances) but
  importance 0.66 (rank 304/508) — dense-cluster workhorse the formula sends to back slots.
- known_ratio (share of a node's observed neighbors whose nct is known >0): >=50% for
  98.6% of the 508 nodes; 100% for 70.7%. The ADR 3.1 gate would be OPEN nearly everywhere
  today.
- Relay mheard (3-value segments): 38,767 segments from 397 relays; latest per relay:
  median 5, p90 14, max 35. 96 relays report >= 9 (saturation candidates for
  MAX_MHEARD=10 boards).
- Duplicates: feed DUP/NEW = 0.70 (lower bound on RF duplication; BergLog RF-side was 1.57).
  msg_id spans: 1368 msg_ids reappear over 10-60 min (zombie candidates);
  867 msg_ids span >1h (likely msg_id counter wrap, NOT zombies — msg_id =
  (_GW_ID & 0x3FFFFF)<<10 | node_msgid, small per-node counter).
- Loop paths (repeated callsign inside one path): 104 of 96,074 (0.11%), e.g.
  `DB0MGN-1,DB0DOL-1,DB0TVI-1,DB0FTS-1,DB0TVI-1`.
- Storm evidence: hour 2026-08-22 04:00 UTC has 9377 frames (2.4x normal); origin
  IU4KCH-26 alone sent 5912 HEY frames that hour (1.6/s; 9 the hour before).
- Trickle reality (docs/hey-supp.md, 5 nodes, 9.5h): mean 53% HEY suppression; steady
  state 1 own HEY per 57-170 min.

## Firmware code baseline (spot-verified 2026-08-22 on v4.35p_prio)

- `csma_compute_timeout_prio()` at `src/lora_functions.cpp:2153`: per-prio base/slots,
  retry reduction 5/6 and 2/3, rapid-fire CSMA_RAPID_RX_MS=100 at attempt >= CSMA_MAX_ATTEMPTS.
- `src/configuration_global.h`: CSMA_SLOT_SIZE=35 (:221), CSMA_PRIO_BASE_3=4500 (:268),
  CSMA_PRIO_SLOTS_3=10 (:275). MAX_MHEARD: 50 default, 80 S3/nRF52 (:180), 10 T-Beam (:187),
  30 ESP32 classic (:195). MAX_DEDUP_RING: 60 default, 100 S3/nRF52, 10 T-Beam, 70 ESP32.
- `getMheardCount()` `src/mheard_functions.cpp:556`: 1h window over mheardEpoch.
- `sendHey()` `src/loop_functions.cpp:4273`: payload `"R" + getMheardCount() + ";"`.
- NC ingest channels: position `/N` -> `aprspos.ncnt` -> `mheardNCount[]`
  (`src/lora_functions.cpp:645-676`); HEY -> `updateHeyPath()`.
