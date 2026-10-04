<!-- Recon report by a read-only Sonnet scout, 2026-10-04, fork-dev a9e64cf6. Line references are a snapshot; re-verify before editing. -->

# Scout report: upstream #1188 (S&F for PMs arriving via the central server), HEAD a9e64cf6 (5899e12b on top)

BLUF: The mailbox already treats "own base call, any SSID" as eligible and is not gateway-gated; the only
missing piece for the core ask is a store hook in the two GATE handlers. All three concept gaps are still open
and unchanged. Core S&F sources (msgstore*, sto_notice*, UDP handlers except WEB-04 status calls) have zero
logic changes since 42dbf03a. All paths below are relative to /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main.

## Findings

### 1. Existing S&F (store node)

- Files: src/msgstore.cpp (787 l, platform-neutral core), src/msgstore_api.h (131), src/msgstore_glue.cpp (217,
  firmware env), src/msgstore_settings.{cpp,h} (persistence of SETTINGS only), src/sto_notice.{h,cpp} (notice
  build/parse, sender-side holder table). Tests: test/test_msgstore/test_main.cpp, test/test_sto_notice,
  test/test_udp_frame_twin (covers :sto ingress, no mailbox hook).
- Board availability: `#define ENABLE_MSGSTORE 1` only in the ESP32-S3 || BOARD_RAK4630 block
  (src/configuration_global.h:319). Classic ESP32 compiles nothing; commands answer `[STORE];unavailable`
  (docs/commands-store-node.md:3-6). No PSRAM use: table is static BSS (src/msgstore.cpp:23).
- Mailbox: `static struct MsgStoreEntry s_entries[MSGSTORE_SLOTS_MAX]` (msgstore.cpp:23) + `s_capblocked[50]` (:30).
  MSGSTORE_SLOTS_MAX=50, default 50 (msgstore_api.h:19-20), payload max 160 B (:21), call 10 B (:22), hold
  default 24 h, max 168 h (:24-25), list 16 calls (:23). Entry fields (msgstore_api.h:42-63): src, dst, nnn,
  plen, pcrc, payload[161], stored_ms, next_ms, attempt, cycles, state, notice, gen. By my layout count ~204 B/slot
  (header comment says ~190 B) => ~10.2 kB. Measured: +12.7-13.1 kB RAM on S3/RAK incl. other S&F code
  (docs/snf-port-campaign.md:121-135, "~10.5 kB is the table").
- IMPORTANT: the mailbox stores the STRIPPED TEXT + (src, dst, nnn), NOT the binary frame. Delivery rebuilds a frame
  (glueDeliver, msgstore_glue.cpp:93-142: fresh msg_id=millis(), path `SENDER,OWNCALL`, max_hop 0, no server flag).
  Consequence: a server frame cannot be "stored unchanged"; the original msg_id, hw/mod bytes, path are lost.
- Store hooks (all in OnRxDone, destination-is-not-us branch, src/lora_functions.cpp):
  - presence: :1418-1419 `if(no comma in msg_source_path) msgstorePresence(msg_source_call)` (direct frame from dst,
    ANY frame type that reaches this block; exact-match `strcmp(dst, call)`, msgstore.cpp:427; needs >=60 s since
    stored_ms, :431; then HELD->ARMED with 5-60 s jitter, :434-441).
  - purge: :1845-1852 `:ack` -> msgstoreOnAck(source, destination, nnn) (exact dst/src match, msgstore.cpp:402-405).
  - store: :1854-1874 conditions: not `*`, not group, no `:rej`, not `{`-prefixed control frame, not peer delivery
    (F6), not rx_pn_repeat (E2), msgstoreEligible(dst); then needs `{` tag at pos>0 -> msgstoreStore(src, dst, nnn,
    stripped payload).
  - peer cancel: :1879-1880.
  - whole block runs only for frames new to the dedup ring (`setlogCountDedup(rx_is_new)`, :1498; rx_is_new :955).
- Eligibility (msgstore.cpp:279-303): OWN = sameBaseCall(dst, own) (:287, base = text before first '-', helper at
  :83-105, file-static); LIST = csv entry with SSID matches exactly, without SSID matches any SSID (:110-149);
  HEARD = direct edge in neighbour matrix <12 h (glueHeardAgeMs, msgstore_glue.cpp:56-66).
- msgstoreStore (msgstore.cpp:305-389): rejects len 0/>160, src==own, dst==own (exact, :315-318); same
  (src,dst,nnn)+same crc -> refresh stored_ms/HELD (:333-343); same key different payload -> replace (:345-359);
  else first free slot, notice=1 if `--storenotice on` (:380); full -> dropped_slots++, never evicts (:386-388).
- `:sto` notice: queued by notice flag, sent from msgstoreLoop (msgstore.cpp:586-604) via glueNotify
  (msgstore_glue.cpp:160-198): text frame, src=own call, dst=original sender, payload "%-9.9s:sto%03u %s"
  (sto_notice.h:5-10), max_hop = max_hop_text, enqueued with addTxRingEntryOnce(...,"sto"). LoRa ONLY: no
  insertOwnTx, no addLoraRxBuffer, no addNodeData (comment :159). Competes with deliveries under the same node gates.
- Retrieval trigger: presence only (direct frame from dst, see above). No HEY-specific logic; any directly heard frame
  (text, pos, HEY, ack) from the exact dst call arms it. Server-injected frames never reach the hook (design T8,
  docs/dm-stage3-wave-plan-20260914.md:150-155,196-202).
- Ladder/delivery (msgstoreLoop, msgstore.cpp:468-665): 9 steps 40 s apart, +60 s after steps 3 and 6, cooldown 1 h
  (msgstore_api.h:28-31); gates: >=30 s between actions, <=20 actions/h, bp_state==QUIET, util <=25 %
  (msgstore.cpp:553-562). Deliver = hop 0, TX ring slot DONE (no ring retransmit). Stops on :ackNNN (purge) or peer
  delivery. Mailbox delivery is never uploaded to the server (docs/dm-transport-impl-plan-20260913.md:391 T-3.5).
- Expiry: `stored_ms` older than hold time -> FREE (msgstore.cpp:483-494); refresh resets stored_ms.
- Dedup against ring: indirectly - hook is inside the dedup-new branch; (src,nnn,crc) refresh inside the mailbox.
  Second layer dm_dedup only for dst==own (lora_functions.cpp:1755).
- Persistence: mailbox is RAM-only (docs/commands-store-node.md:66-68). Settings persist: ESP32 NVS keys store_*
  (msgstore_settings.cpp:48,63), nRF52 LittleFS /msgstore.cfg (:84).
- Runs in loop task: msgstoreLoop at src/esp32/esp32_main.cpp:4180, loop_actions_esp32.cpp:66,
  loop_actions_nrf52.cpp:53.

### 2. Gateway server path

- Datagram split on first 4 bytes: ESP32 src/esp32/udp_frame_esp32.cpp:60 handleUdpFrame_esp32, GATE at :101; called
  from getMeshComUDP (src/udp_functions.cpp:197). nRF52 src/nrf52/udp_frame_nrf52.cpp handleUdpFrame_nrf52, called
  from src/nrf52/nrf_eth.cpp:498. Both gated on bGATEWAY (only called from the gateway-on branch).
- Binary layout: GATE + raw mesh frame; `memcpy(convBuffer, inc+4)` (esp32:137) then decodeAPRS(convBuffer) (:168).
  Same layout as RF/UDP-DATA body (docs/architecture/11-wire-format.md:445-450). Header "DATA" upload is 36 B
  (:419-440), GATE inbound has only the 4-byte indicator.
- Text branch: ESP32 :297-485, nRF52 :260-467. Branch condition `dst=="*" || dst==own(strcmp exact) || group`
  (esp32:309, nrf52:272). A PM for the BASE call with a different SSID is NOT in that branch: it keeps `{NNN` in
  payload, bUDPtoLoraSend stays true, and it is queued ONCE: `addTxRingEntry(convBuffer,..,"udp_rx")`
  esp32:505 / nrf52:495 (no retransmission, 0xFF/DONE), after is_new_packet (esp32:488 var bUdpMsgIsNew from :206;
  nrf52:477) and checkOwnTx==-1; id registered with addLoraRxBuffer (esp32:520, nrf52:505), insertOwnTx (:503/:493).
- Before queueing, handler appends own call to source path (esp32:280-281), sets msg_server, 0x20 flag on BLE copy,
  resets dest path (:257), checkVia. The two handlers are twins, guarded by test/test_udp_frame_twin.
- Recognising "PM for own base call": needs a new helper; destination_call is a plain char[MC_CALL_LEN_Z] "CALL-SSID"
  (esp32:68, via decodeAPRS). No SSID field. Same-base logic exists only as file-static sameBaseCall in
  msgstore.cpp:98; msgstoreEligible(dst) already does it for mode own. Not found elsewhere in src.
- Frames from server that are exact own dst are consumed (displayed/acked, never sent to LoRa: esp32:312-313).
- NO rx_pn_repeat equivalent in either UDP handler (grep pnVariantIds|checkOwnRx in udp_frame_*: none). An XOR retry
  copy (docs/pn-retry-server.md) arriving via server has a different msg_id -> is_new true -> would refresh stored_ms
  if hooked without a PN-variant check. RF path handles this at lora_functions.cpp:963-981 and :1859.

### 3. Re-verification of the three gaps (git log 42dbf03a..HEAD per file)

- Store hook on server path: STILL MISSING. grep msgstore in src/esp32/udp_frame_esp32.cpp, src/nrf52/udp_frame_nrf52.cpp,
  src/udp_functions.cpp: no hits. Unchanged.
- Feedback to server: STILL MISSING. glueNotify has no addNodeData (msgstore_glue.cpp:159,193). Unchanged.
- Routing by server: unknown, outside tree; no new evidence. BACKLOG SNF-GW "Review the concept with Kurt before any
  implementation" (docs/BACKLOG.md:278).
- Changed files since 42dbf03a: udp_frame_esp32.cpp and udp_frame_nrf52.cpp (8c905df4, WEB-04: own_msg_status calls only,
  7 lines changed each, no mailbox/dedup logic); lora_functions.cpp (8c905df4, a6ddb456, 9d82ab5d, 8e6b8c9f; no mbox lines
  touched, only shifted +4..+20 lines); loop_functions.cpp (WEB-04, TD-11, RTC, TZ). msgstore*, sto_notice*, udp_functions.cpp,
  udp_frame.h, nrf_eth.cpp, dedup_functions.cpp, pn_retry.h: no commits.

### 4. "Own callsign, different SSID"

- Own call: `meshcom_settings.node_call` char[10] "CALL-SSID" (src/meshcom_settings.h:70), read in glue via glueOwnCall
  (msgstore_glue.cpp:44-47). No separate SSID/base field.
- DM-to-me test in RF path: exact `strcmp(destination_call, node_call)` (lora_functions.cpp:1604, 2195); UDP path same
  (esp32:309/312/393/426, nrf52:272/275/442). So DK5EN-92 destination at node DK5EN-90 is "not for me" everywhere
  and falls into the relay/store branch. Exact-match also in msgstoreOnAck (msgstore.cpp:402) and msgstorePresence (:427).
- Base-call match needed: ALREADY done by msgstoreEligible in mode `own` (msgstore.cpp:287) and by msgstoreStore
  (excludes only exact own, :317). Mode `own` therefore delivers the issue's semantics once the hook exists on the server
  path. Gaps: (a) mailbox call width is 10 (CALL_MAX) vs MC_CALL_LEN_Z 21 in the handlers (truncation of >9-char calls
  is silent, copyCall msgstore.cpp:69-80); (b) a call without SSID as node_call ("DK5EN") vs destination "DK5EN-0"?:
  baseCall treats both as base "DK5EN" -> would match; check whether that is wanted; (c) sameBaseCall is static, a
  shared helper is needed only if the hook wants an early cheap test (not required: call msgstoreEligible).

### 5. Feedback to the server / echo guard / dedup

- Upload path: addNodeData (src/udp_functions.cpp:1307-1336) builds "DATA"+gwid(8 hex)+call(9)+ver(4+1)+rssi(4)+snr(4)+"03"
  - raw frame, pushes to udpOutRing (addUdpOutBuffer :1242, len cap 255; uint8 length in bf_push). Used for own
    TX frames with rssi/snr 0 at loop_functions.cpp:4511 (was :4470), 4595, 5135, 5191, 5274, 5368, 5906 and for RF
    receive at lora_functions.cpp:2183. Gate: bGATEWAY && node_hasIPaddress. Frame built by ring-only senders
    (glueNotify, sendNbrReport, mailbox deliver) is NOT uploaded unless the caller does it (loop_functions.cpp:5488-5492).
- `:sto` upload: would be `encodeAPRS` buffer + addNodeData(buf,len,0,0) in glueNotify; glueNotify currently builds
  the frame into a local `buf` (msgstore_glue.cpp:182-183) so the buffer exists. Glue runs in loop task (msgstoreLoop),
  so no new task context for addUdpOutBuffer (ring locks itself, udp_functions.cpp:1247-1252). A `:sto` carries a fresh
  msg_id=millis() and is not in own-TX table or dedup ring (not inserted): if uploaded and the server reflects it
  back to this gateway, the node would treat it as a new foreign frame (relay once). Decide whether to insertOwnTx +
  addLoraRxBuffer as the normal send path does (loop_functions.cpp:4488-4495).
- Echo-guard facts: (i) server-origin frame we relay to LoRa carries msg_server=true (esp32:283), so other gateways do
  NOT re-upload it (lora_functions.cpp:2151 `!aprsmsg.msg_server`). (ii) The store node's OWN hop-0 delivery has no
  server flag; any gateway hearing it directly uploads it as a new msg_id, the server may route it back as GATE with a
  new id -> hook sees it as new, refreshes stored_ms (concept 3.2 valid). (iii) In the handlers the own call is appended
  to the source path BEFORE any hook placed after esp32:281 / nrf52 equivalent, so a "path contains own call" guard must
  run on the decoded path before the append (esp32:280) or test the original path buffer.
- Dedup consequences: hook must sit inside the is_new gate (esp32 var bUdpMsgIsNew :206, nrf52 inline :477) and ahead of
  or beside addLoraRxBuffer; a copy of the same PM arriving later via RF is then deduped before the RF hook
  (lora_functions.cpp:1498), so no double store. Ring size S3/RAK = 100 (configuration_global.h:321); same (src,dst,nnn)
  refreshes inside the mailbox. Server `:ack` frames for held DMs: the server-ack path for dst != own is not parsed in
  the UDP handlers at all, so msgstoreOnAck must be called there too (concept 3.1 row 1) else a PM acked via server (by B on
  another gateway) stays held until expiry.

### 6. Memory and timing budget

- No docs/ram-comparison-*.md exists. tools/resource_baseline.json (dated 2026-09-11, stale per memory note): S3
  ram_total 327680 / used ~115764; RAK4631 ram_total 248832 / used 88844 (static, pre-S&F). docs/snf-port-campaign.md:121-135:
  RAK static RAM 92820 B after S&F; Heltec/T-Deck S3 ~113996/134404. Runtime free heap: meshcom5-campaign.md:126 reports
  148-149 kB free at the monitor point on S3 and classic with the neighbour matrix; no RAK free-heap figure in docs.
  `--store` prints `[STORE];heap;<bytes>` on enabling (command_functions.cpp:270-279).
- Mailbox constants: 50 slots x ~204 B (see 1). Gateway nodes are NOT excluded: msgstoreLoop and the hooks have no
  bGATEWAY condition (grep: msgstore*.cpp/h contain no "gateway" use outside comments). The only gateway-related
  interaction is the existing 60 s delay in msgstorePresence (msgstore.cpp:431) after storing.
- UDP out ring: RING_BYTES_UDP 3072 on S3/RAK (configuration_global.h:~323), 2048 classic. UDP_TX_BUF_SIZE 255.
- Timing: ladder 9 steps / 40 s; 20 actions/h node cap, shared between deliveries and `:sto` notices
  (msgstore.cpp:555-562). A burst of server PMs for the sibling SSID can raise notice_blocked.
- nRF52 task context (docs/architecture/09-concurrency-map.md, memory note nrf52-onrxdone-runs-in-lora-task): OnRxDone in
  16 kB LORA task, GATE handler in loop task; msgstoreStore from both is a new writer combination (the `gen` counter in
  msgstore_api.h:56-62 only covers hook-vs-loop). ESP32: both in loop task (confirm on bench).

### 7. Server and app side (out of firmware scope, paper must name)

- Server: (a) route PMs addressed to base call X-n to gateways that are store nodes for base call X, not only to
  gateways that recently uploaded X-n (today's rule unknown; concept 4 M1-M4 measure it). (b) Optionally accept a new
  gateway->server datagram (concept STOR) or tolerate a synthetic HEY from the store node; define behaviour for unknown
  4-byte indicators. (c) Understand a `:sto` text from a gateway as custody, forward it to the PM sender like any DM (it is
  a normal text frame: dst = sender, no `{`, no `:ack`); keep dedup with mask 0xFFFFF3FF for XOR retry copies
  (docs/pn-retry-server.md). (d) Do not treat a hop-0 mailbox delivery uploaded by a third gateway as a new PM
  (it has a new msg_id; source = original sender, path SENDER,STORECALL).
- App/clients: (docs/client-integration-store-forward.md:30-70): new status 0x04 "held" with holder call on the 0x41
  frame; precedence acked > failed > held > heard/gateway; `:sto` consumed (not displayed) by new firmware on both
  ingress paths, shown as plain DM on old firmware (docs/commands-store-node.md:69-80). For internet-side clients (mc-chat,
  MCProxy, web app) the sender must parse `:stoNNN` at byte 9 of a text from the holder and render "held by <holder>";
  without it the user sees a stray short chat line. Wire format doc (docs/architecture/11-wire-format.md) does NOT
  describe `:sto` or the store node at all (grep :sto|custody|held: only gateway ack text); a wire-format subsection
  would be needed for the paper's server-facing contract.

## Delta vs the 2026-10-02 concept (docs/snf-gateway-concept-20261002.md, based on 42dbf03a)

| Concept reference                                                       | Now                                                                                | Verdict                                                                              |
| ----------------------------------------------------------------------- | ---------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------ |
| sendKEEP udp_functions.cpp:1261, addNodeData :1307                      | same                                                                               | unchanged                                                                            |
| msgstoreEligible msgstore.cpp:279                                       | same                                                                               | unchanged                                                                            |
| udp_rx enqueue ESP32 :502, nRF52 :492                                   | esp32:505, nrf52:495                                                               | +3 lines (WEB-04)                                                                    |
| RF store hook "1851", purge "1841", peer cancel "1870", presence "1414" | presence :1419, purge :1851, store cond :1854-1860 / call :1871, peer cancel :1880 | numbers were already imprecise at 42dbf03a (it had 1415/1842/1862/2174); now +4..+20 |
| exclusions "1844-1851"                                                  | :1854-1860                                                                         | shifted +10                                                                          |
| own-text upload loop_functions.cpp:4470                                 | :4511                                                                              | shifted +41                                                                          |
| RF upload block lora_functions.cpp:2142-2174                            | :2151-2185 (addNodeData :2183)                                                     | shifted +9..+11                                                                      |
| three gaps                                                              | all open, no change                                                                | confirmed                                                                            |

- No semantic drift in msgstore, sto_notice or UDP handlers. WEB-04 (8c905df4) changed only own-msg status writes
  (setOwnMsgStatus / ownMsgStatusSet at esp32:339-341,407; nrf52:303-305,387), irrelevant to the hook.
- Concept omissions found now: (1) concept 3.1 excludes "PN repeat copy" but neither UDP handler has a PN-variant check
  (needs pnVariantIds/checkOwnRx before the hook); (2) the mailbox stores text, not the frame, so "store unchanged" is not
  possible and the original msg_id is not kept (delivery uses a fresh id); (3) own-call append to the path happens before
  the place where an echo guard would sit; (4) a server-side `:ackNNN` for held PMs (dst != own) is not parsed in the UDP
  handlers, so msgstoreOnAck must be added there too (concept 3.1 row 1 covers it); (5) `:sto` not registered in own-TX/
  dedup ring; (6) a hook-less gateway holding mode `own` already holds PMs for the sibling SSID that arrive via RF - the
  new work is only the server ingress.

## Open questions for the operator

1. Scope: only mode `own` (the issue text) or also `list`/`heard` on the server path? `heard` on server frames is safe by
   construction (needs a live direct edge); `list` only narrows, no 12 h announce rule exists in code today.
2. Should the server-path hook store when the PM destination equals a node that is directly heard right now (one-shot LoRa
   copy is sent anyway, presence ladder waits 60 s)? Default in the concept: yes.
3. Suppress the LoRa copy of `:sto` when the PM arrived via GATE (sender is internet-side)? Concept default: keep both.
4. Upload `:sto` to the server: also register it in own-TX table / dedup ring so a server reflection is dropped?
5. Is Kurt/server maintainers' routing known? M1-M4 (concept section 4) still unmeasured; stages 3a/3b depend on it.
6. Does the hold time stay `--storetime` (24 h default) or does a gateway need a different default? RAM-only mailbox:
   a gateway reboot (watchdog, OTA) drops held PMs without notice (documented warning only, commands-store-node.md:66).
7. Hardware: store+gateway exists only on S3 and RAK4631 (Ethernet on RAK). Is a classic-ESP32 gateway intentionally excluded?
8. Should the issue's "same callsign different SSID" also cover the case where the sibling is on the SAME gateway mesh
   region but reached by hop>1 (mailbox delivery is hop 0 only, needs a direct presence frame)?
9. Test coverage: native twin test (test/test_udp_frame_twin) has no mailbox stub; accept adding `msgstore` link stub or a
   hook seam as in sto_notice?

## Suggested file ownership for an implementation (no file under two tasks)

- T1 shared hook helper + eligibility (new): src/msgstore_hook.h (new, header-only: path/PN-repeat/exclusion test and
  stripped-payload extraction, used by RF and both UDP handlers) ; src/lora_functions.cpp (replace inline conditions at
  :1845-1880 with the helper; RF-only peer-delivery stays) ; test/test_msgstore_hook (new).
- T2 ESP32 GATE hook: src/esp32/udp_frame_esp32.cpp (insert before the own-call append at :280, inside bUdpMsgIsNew).
- T3 nRF52 GATE hook: src/nrf52/udp_frame_nrf52.cpp (same place, twin) ; test/test_udp_frame_twin/test_main.cpp (agreement cases).
- T4 `:sto` upload + echo/dedup registration: src/msgstore_glue.cpp (glueNotify) ; src/udp_functions.cpp only if a helper
  is needed (otherwise untouched) ; test/test_msgstore (glue stub) .
- T5 core adjustments if any (nRF52 cross-task writer, helper export `msgstoreSameBase`): src/msgstore.cpp, src/msgstore_api.h
  (only if T1 needs it; sole owner).
- T6 settings/commands for stage 3 (`--storeannounce`, optional): src/command_functions.cpp, src/msgstore_settings.cpp/.h,
  src/web_functions/web_setup.cpp, src/web_functions/web_functions.cpp (not needed for stage 1).
- T7 docs: docs/snf-gateway-concept-*.md (line refs update), docs/commands-store-node.md, docs/client-integration-store-forward.md,
  docs/CHANGELOG-snf.md, docs/architecture/11-wire-format.md (add S&F section), docs/BACKLOG.md, docs/test-suite-map.md.
- Overlap check: lora_functions.cpp only T1; udp_frame_* split T2/T3; msgstore.cpp/api only T5; msgstore_glue.cpp only T4.
  T1 depends on the helper API before T2/T3 can start (wave order T1/T5 -> T2/T3/T4 -> T7). Do not touch src/dedup_functions.cpp
  or pn_retry.h (read only). Gate: tools/regression.sh --stage 1,2; one pio process at a time.
