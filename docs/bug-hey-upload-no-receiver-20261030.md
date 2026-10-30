# Firmware bug: a gateway's HEY upload does not name the receiving gateway

- **Reported:** 2026-10-30, from mcmap (meshmap.oevsv.at) issue
  [DK5EN/map#5](https://github.com/DK5EN/map/issues/5)
- **Firmware:** 4.40a. The code is identical on `upstream/oe1kbc_v4.40a` (`10203967`) and on
  `fork-dev` (`73d60110`)
- **Repo for the fix:** `~/WebDev/MeshCom-Firmware-DEV-Main` (`fork-dev`). Read its `CLAUDE.md`
  first: branch model, host gate, bench rules
- **Severity:** about 44 % of all HEY reception reports reaching the server cannot be attributed
  to a receiver, so no RF link can be drawn from them

## Problem in one paragraph

When a gateway hears another station's HEY over RF, it appends its own signal report
(`NCT,RSSI,SNR;`) to the payload and uploads the frame to the MeshCom server. It does **not**
append its own callsign to the source path. The path the server publishes still names only
the origin (plus any RF relays), so the published report says "someone heard X at −107 dBm"
without saying who. Consumers (meshmap, any INTERLINK client) need at least two path elements
to form an edge, so these reports are thrown away. For links where the **origin** is a gateway,
this report is usually the only evidence of the link, so gateway-to-gateway links are almost
never shown.

## Evidence (production INTERLINK feed, 2026-10-30)

The server no longer dedups HEY by `msg_id`, so every copy arrives. HEY `A3F15360` from gateway
`HB9VQQ-5` arrived three times:

```json
{"type":"hey","path":"HB9VQQ-5","rssi":"3,129,-14;","nct":8,"msg_id":"A3F15360","gw":0,"timestamp":1793351887}
{"type":"hey","path":"HB9VQQ-5","rssi":"12,107,-5;","nct":8,"msg_id":"A3F15360","gw":0,"timestamp":1793351887}
{"type":"hey","path":"HB9VQQ-5","rssi":"","nct":8,"msg_id":"A3F15360","gw":0,"timestamp":1793351921}
```

- The two copies with a report are receptions by two different gateways (neighbour counts 3
  and 12). Neither gateway is named.
- The third copy is the origin's own self-upload (empty report). That one is correct.
- `12,107,-5` is almost certainly `HB9VQQ-4`: its neighbour count is 12–13, and the operator's
  MHeard list shows `HB9VQQ-5` at about −6 dB. The feed cannot prove this, which is the bug.

**Scale:** in about 9 h of feed (00:38–09:16 CET), 23,221 HEY frames had a single-element path
and a non-empty report. 29,755 had a multi-element path.

**It happens for every gateway, old firmware included.** A gateway older than 4.40a uploads the
raw payload, and the server appends a 2-value `rssi,snr` tail from the `DATA` header. The call
is still missing: `{"type":"hey","path":"DL1LAA-12","rssi":"133,-14;",…}`.

**No HEY path ends in the uploading gateway.** With `HB9VQQ-4`, `HB9VQQ-5`, `HB9VQQ-11`,
`DL1GWN-12`, `OE5XLM-12` or `OE5HWN-12` as the last element there are 0 matches in the whole
window. In a sample of twelve 2-element HEYs, no last element is in the server's gateway list.
The named elements after the origin are RF relays (`HB9VQQ-2`, `HB9BBE-18`, `HB9TVJ-12`,
`DB0TOD-13`, `DL4ML-2`, `DB0SWS-12`, all `gw:0`). Counting segments confirms it:
`"path":"HB9VQQ-4,DB0TOD-13","rssi":"10,117,-12;2,136,-19;"` carries two reports for one named
hop, and the second belongs to the unnamed uploader.

**Contrast with `pos`:** for `pos` frames the server does name the uploading gateway:
`{"type":"pos","src":"HB9BBE-18,HB9VQQ-4",…,"rssi":-106,"snr":-2}`. The server knows the
uploader for every frame, because `addNodeData()` puts it in the `DATA` header
(`src/udp_functions.cpp:1531`). It just does not write it into HEY paths.

## Root cause in the code

`src/lora_functions.cpp` (line numbers are identical on `fork-dev` and upstream 4.40a):

- **2413–2447, gateway upload branch:** `if(bGATEWAY && (!aprsmsg.msg_server ||
aprsmsg.payload_type == '@'))`. For `'@'` it calls `appendHeySignalReport(...)`, sets
  `bHeyReportAppended`, re-encodes `RcvBuffer` with `encodeAPRS()`, then calls
  `addNodeData(RcvBuffer, size, rssi, snr)` at 2445. `aprsmsg.msg_source_path` is never touched,
  so the uploaded path does not contain the gateway.
- **2548–2558, relay branch:** this is where the node's own call is appended to the path
  (short path: `last,own`; long path: `…,own`). It runs **after** the upload and only when the
  frame is relayed over RF.
- **2502–2512, loop detection:** a relay is skipped when the node's own call is already in the
  path (`RELAY_LOOP_BLOCKED own_call_in_path`). The fix must not put the call into `aprsmsg`
  before this check, or the gateway stops relaying the HEY over RF.

History: the in-payload report on upload was added in `0cac4aea` (2026-08-27). Before that the
gateway uploaded the raw RF buffer. Neither version ever added the gateway's call.

## Required change

In the `'@'` part of the gateway upload branch, build the **upload copy** with the gateway's own
call appended to the source path, after the signal report, so the uploaded frame reads exactly
like an RF relay hop:

```
path:   <origin>[,<relays>...],<own call>
report: ...;<NCT>,<RSSI>,<SNR>;     (last segment = this gateway's reception, already appended today)
```

Constraints:

1. **Do not mutate `aprsmsg.msg_source_path`** used by the relay branch further down. Use a
   local copy of the `aprsMessage` (or save and restore the path) for the upload encode only.
   Otherwise the loop detection at 2502 blocks the RF relay, and the relay branch appends the
   call a second time.
2. **The signal-report behaviour stays as it is.** The report is appended once (the
   `bHeyReportAppended` guard at 2557 already skips the relay-side append). Whether the relay
   copy carries the report is not part of this fix.
3. **Use long-path semantics for the upload** (append; do not apply `bSHORTPATH` truncation).
   The server and consumers need the full chain.
4. **Respect `MC_PATH_LEN`.** If appending would overflow, upload the frame unchanged (today's
   behaviour) rather than truncating mid-callsign. Same spirit as the length guard in
   `appendHeySignalReport()`.
5. **Only received HEYs are affected.** The gateway's own HEY (`sendHey()`,
   `src/loop_functions.cpp`) is a self-upload and must stay single-element with an empty report.
   Consumers use exactly that shape to recognise self-reports.
6. **`pos` and text uploads are out of scope.** The server already names the uploader there.
   Do not change them.

## Tests

- Host: extend `test/test_hey_report/test_hey_report.cpp` (env `native_aprs`), or add a sibling
  suite, covering:
  - the encoded upload frame for a direct reception has path `ORIG,OWN` and the report as the
    last segment;
  - a relayed reception gives `ORIG,RELAY,OWN`;
  - the relay copy's path is unchanged by the upload step;
  - path-length overflow falls back to the unmodified frame;
  - the gateway's own `sendHey()` frame is unaffected.
- Gate: `tools/regression.sh --stage 1,2`. Never bare `pio test`; one `pio` process at a time.
- Field acceptance: see the next section.

## Field verification

The whole chain can be traced end to end: device console, uploaded frame, and the published
frame on the receiving server.

**Test gateway:** a gateway **with a LoRa radio**, flashed with the fix, that hears HEYs from
at least one other **gateway** — the gateway-to-gateway case this fix is about. `DK5EN-99` does
not qualify (no LoRa radio). `DK5EN-98` is the production gateway, so it may only be used with
explicit OK from DK5EN. Bench rules from the firmware `CLAUDE.md` apply.

**Recommended origins (owner, 2026-10-30): `DL2JA` and `DB0ED`.**

- `DL2JA-2` (Marzling, HELTEC V3, 4.40a, gateway, nbcnt 19) is the ideal origin. On 2026-10-30
  each of its HEYs reached the server 5–6 times, every copy without a receiver name, e.g.
  `E9FB717A`: `1,110,-1;`, `6,112,-4;`, `4,112,-1;`, `1,118,-17;`, `11,123,-17;`, `2,88,-13;`.
  After the fix, the flashed gateway's copy must stand out as `"path":"DL2JA-2,<own call>"`.
- `DL2JA-3` (FSG Hut, 4.40a, gateway) is a second gateway origin.
- `DB0ED` has no row in meshmap's node table, so it was not checked from here.

Before flashing, confirm in the console that the test gateway actually receives HEYs from one of
these.

**Three observation points:**

1. **Device: the test gateway's console capture on `rpizero.local`.** With `--loradebug on`, a
   received HEY shows as `RX-LoRa2 … @ x<msg_id> <orig>>HG@…` and its upload as
   `TX-UDP … @ x<msg_id>`; the `[DATA]` line shows the header with the gateway's call. The
   uploaded payload must now carry the path `<orig>,<own call>` and the report as the last
   segment. There must be no `RELAY_LOOP_BLOCKED own_call_in_path` for that `msg_id`.
2. **Published frame: the `oe-link` server** (production mcmap host). This is the receiving end
   of the INTERLINK feed, in `/var/log/mcmap/interlink.log` via `ssh oe-link`, or via the
   mcmap-prod MCP `logs_grep` on log `interlink`. For the same `msg_id`, expect
   `{"type":"hey","path":"<orig>,<own call>","rssi":"…;<nct>,<rssi>,<snr>;",…}`.
3. **Consumer: mcmap.** The next log lines after that frame show
   `edge: <orig> -> <own call>  snr=… rssi=…`, and the link appears on meshmap.oevsv.at.

**Pass criteria:**

- At least one received HEY is published with the test gateway as the last path element.
  Match the `msg_id` between console and `interlink.log`.
- The call appears exactly once: neither the firmware nor the server appends it twice.
- The server still publishes the frame (`TX-UDP` in the console has a matching line in
  `interlink.log`).
- The gateway still relays over RF: no `RELAY_LOOP_BLOCKED` for those frames.
- The gateway's own HEYs stay single-element with an empty report.

## Upstream server (separate track, not for this fix)

The cleaner fix is on the MeshCom server. It already has the uploader's call in the `DATA` header
and writes it into `pos` paths. Doing the same for HEY would also cover every gateway still on
older firmware, which the firmware fix cannot reach. Raise it with the server maintainer; it does
not block the firmware change. If the server later does this too, constraint 1's "no double
call" check in the field test is what catches the interaction.

Also out of scope, also server-side: `pos` frames are still deduplicated by `msg_id`. A gateway's
own position upload always arrives first, so neighbours' relayed copies of a gateway's `pos`
(the only copies carrying RF metrics) are dropped. That is the second half of the operator's
report in DK5EN/map#5.

## References

- mcmap finding (German): `~/WebDev/mcmap/docs/findings/data-gateway-link-blindspot.md`. §11 is
  the 2026-10-30 re-measurement this report is based on.
- HEY wire format: `~/WebDev/mcmap/docs/reference/data-hey-format.md`.
- mcmap edge extraction (path length ≥ 2): `~/WebDev/mcmap/proxy/src/interlink/ingest.ts`,
  `applyHey`.
