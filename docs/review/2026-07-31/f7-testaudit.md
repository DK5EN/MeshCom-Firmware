# F7 — Test-strategy audit: would the proposed tests actually be able to fail?

Targets: `docs/architecture/06-test-strategy.md`, `docs/architecture/07-verification-infrastructure.md`.
All counts below are measured against the working tree at `1ba101f4`.

---

## Golden-vector feasibility

### Measured yield: **0 usable vectors from 17 logs**

The Layer-2 plan (06 §"Layer 2", 07 §7 scenarios 1–2) says: "parse
`tools/meshcom_monitor/*.log`, pair each raw hex dump with the `MH-LoRa:` decode line that
follows it". I measured that pairing directly.

| log                             | hex dumps | `MH-LoRa:` lines | **paired** |
| ------------------------------- | --------: | ---------------: | ---------: |
| `meshcom_2026-03-22_172422.log` |       670 |             4925 |      **0** |
| `meshcom_2026-03-22_172424.log` |         0 |             5794 |      **0** |
| `meshcom_2026-03-23_105629.log` |       109 |             1630 |      **0** |
| `meshcom_2026-03-23_105631.log` |         0 |              413 |      **0** |
| `meshcom_2026-03-23_134942.log` |         0 |              720 |      **0** |
| `meshcom_2026-03-23_155135.log` |         0 |              568 |      **0** |
| `meshcom_2026-03-23_155138.log` |         0 |                0 |      **0** |
| `meshcom_2026-03-23_155206.log` |         0 |                0 |      **0** |
| `meshcom_2026-03-23_155228.log` |      1042 |             9429 |      **0** |
| `meshcom_2026-03-23_194521.log` |         0 |               12 |      **0** |
| `meshcom_2026-03-23_200708.log` |         0 |                0 |      **0** |
| `meshcom_2026-03-23_200836.log` |         0 |               26 |      **0** |
| `meshcom_2026-03-23_203932.log` |         0 |                8 |      **0** |
| `meshcom_2026-03-23_210443.log` |         0 |               87 |      **0** |
| `meshcom_2026-03-23_220719.log` |         0 |              840 |      **0** |
| `meshcom_2026-03-24_073938.log` |         0 |               10 |      **0** |
| `meshcom_2026-03-24_074358.log` |         0 |             1183 |      **0** |
| **TOTAL**                       |  **1821** |       **25 645** |      **0** |

Pairing window tested at 1–5 following lines and again at 1–50. Zero at every width. The
1664 hits at width ≤ 50 are unrelated later frames, not the dumped one.

### Why it is zero, structurally

Every one of the 1821 hex dumps is a `[MC-DBG] CRC_PAYLOAD[255]:` line. There is not one
other per-frame hex dump in any of the 17 files:

```
$ grep -hoE '\[MC-DBG\] [A-Z_]+\[[0-9]+\]:' *.log | sort | uniq -c
1821 [MC-DBG] CRC_PAYLOAD[255]:
```

`CRC_PAYLOAD` is emitted at `src/esp32/esp32_main.cpp:3818`, inside
`if (state == RADIOLIB_ERR_CRC_MISMATCH)` (line 3781). That branch **returns without ever
calling `OnRxDone()`** — `OnRxDone()` is called only from `esp32_main.cpp:3778`, in the
`RADIOLIB_ERR_NONE / RADIOLIB_ERR_LORA_HEADER_DAMAGED` branch. `decodeAPRS()` is called at
`src/lora_functions.cpp:455`, inside `OnRxDone()`. So a frame that produces a hex dump is by
construction a frame that was never decoded and therefore has no `MH-LoRa:` line.

The frames are also corrupt by definition — CRC-failed. Representative context
(`meshcom_2026-03-23_155228.log:588-594`):

```
15:55:15.980  OnRxError
15:55:15.984  [MC-DBG] RX_RESTARTED src=after_crc_error state=0
15:55:15.990  [MC-DBG] CRC_ERROR rssi=-106 snr=-4 freq_err=61.0 size=255 ts=167164
15:55:16.059  [MC-DBG] CRC_PAYLOAD[255]: 40 AE FA 80 B0 14 44 46 32 53 49 ...
15:55:21.430  [MC-DBG] CHANNEL_UTIL rx=2476ms tx=0ms util=24%     <-- no MH-LoRa follows
```

### The doc's own worked example is a mismatched pair

06 lines 34–42 present an `MH-LoRa:` block and a hex string as if they were the same frame.
They are not, and both halves are traceable:

- The hex `21 50 35 A4 91 91 44 4B 35 45 4E 2D 39 38 2C 44 4C 37 4F 53 58 2D 31 3E 2A 21 …`
  is `meshcom_2026-03-22_172422.log:363`, at **17:26:21.918** — a `CRC_PAYLOAD[255]` dump
  preceded by `CRC_ERROR rssi=-119 snr=-12 freq_err=-464.0`.
- The `MH-LoRa: 062 @ x91A4354E H02 … DK5EN-98,DB0ED-99,DL2JA-2>H@R0;…` line is the same
  file at **17:24:32.263** — **109 seconds earlier**, a different `msg_id`, a different
  path, and payload type `@` (HEY) vs `!` in the hex.

The hex is visibly bit-damaged: `…45 23 23 4D 65 73 68 43 6F 6D AF 42 B5 B9 30 30 2F 41 3D
30 30 31 36 35 37` decodes as `E##MeshCom` + 4 garbage bytes + `00/A=001657` — i.e. the
`/B=1` of `/B=100/` has been destroyed.

The JSON in 06 lines 137–149 was then hand-built from that corrupt frame
(`"msg_source_call": "DL7OSX-1"`, `"lat": 48.4072` ← `4824.43N`, `"lon": 11.74` ←
`01144.40E`, `"alt": 1657` ← `A=001657`) with `"msg_fcs": "0D21"` and `"max_hop": 2`
borrowed from the _other_, unrelated packet. **The single illustrative vector in the plan is
a corrupt frame annotated with another frame's header fields.** Any extractor built to that
spec would generate ~1821 such artefacts and a test suite would happily go green on them.

### The dumps are unusable even as malformed-frame fixtures (as-is)

`checkRX()` declares `uint8_t payload[UDP_TX_BUF_SIZE+10];` — an **uninitialised stack
array** (`esp32_main.cpp:3710`) — and `size_t ibytes = UDP_TX_BUF_SIZE;` (3712, `=255`,
`src/configuration_global.h:64`). On CRC mismatch RadioLib does not write back a true
length, so `dump_len` is always the full 255:

```
$ grep -ho 'CRC_ERROR .*size=[0-9]*' *.log | ...
{'size=255': 1821}      # 1821 of 1821
```

Everything past the real frame is leftover stack. It is directly visible: every dump ends
with the byte sequence `5B 4D 43 2D 44 42 47 5D 20 52 58 5F 46 4C 41 47 5F 50 52 4F 43 45
53 53 20 74 73 3D …` = the ASCII string `[MC-DBG] RX_FLAG_PROCESS ts=…` from a previous
`printfdeb` call, plus recurring float constants (`A0 C0 CE 3F`, `78 26 CB 3F`).

This also corrects 06 line 151: the dumps are **not** "`RcvBuffer` prints that include stale
bytes beyond `msg_len`". They are `checkRX`'s stack buffer, and the tail is uninitialised
stack, not a previous frame. The true frame length is not recorded anywhere in the log, so
"the extractor must honour the length field" is not implementable for this data.

### Oracle-validity verdict — **the `MH-LoRa:` line is the decoder's own output**

Even if the hex existed, the proposed oracle is circular:

- `MH-LoRa:` is printed by exactly one call site: `src/lora_functions.cpp:496`
  `printBuffer_aprs((char*)"MH-LoRa", aprsmsg);`
- `printBuffer_aprs()` (`src/loop_functions.cpp:2953`) is a pure `printfdeb` of the fields of
  `struct aprsMessage aprsmsg`.
- `aprsmsg` is populated only by `decodeAPRS(RcvBuffer, size, aprsmsg)` at
  `src/lora_functions.cpp:455` — the function under test.

So a vector built this way encodes the assertion `decode_new(x) == decode_4.35p(x)`. It is a
**regression fence, not an oracle**. It cannot detect any decode defect that exists today;
today's behaviour becomes the specification. 06 never states this, and the framing —
"Real on-air traffic … with the decoded interpretation alongside", "**This is a
golden-vector corpus you already own**", "**Then the oracle exists**" (line 160) — asserts
the opposite.

Two further circularity problems the doc does not surface:

1. **Self-selecting corpus.** `decodeAPRS` returns `0x00` on FCS mismatch
   (`src/aprs_functions.cpp:414-430`), so no frame that fails the firmware's own integrity
   check ever reaches `printBuffer_aprs`. The corpus is exactly the set of frames the current
   decoder already accepts. (Confirming this: `grep -c "discarded, wrong FCS" *.log` = **0**
   across all 17 logs.)
2. **Lossy projection.** `printBuffer_aprs` emits `msg_len, payload_type, msg_id, max_hop,
msg_server, msg_track, msg_mesh, source_path, destination_path, payload, source_hw,
source_mod, fcs, fw_version, fw_sub_version, last_hw`. It does **not** emit `lat`, `lon`,
   `alt` — the three fields 06's JSON example asserts on. Those come from `decodeAPRSPOS()`
   (`src/aprs_functions.cpp:531`), whose output is not in the log stream at all. To fill them
   you must re-parse the payload text, i.e. run `decodeAPRSPOS` — the second function under
   test — again.

### What a genuine oracle would require

- **An independent implementation of the wire format.** A Python reference decoder written
  from `docs/` + the frame layout, not ported from `aprs_functions.cpp`. Disagreements
  between it and the firmware are then real findings in either direction. This is the only
  construction that can catch a pre-existing decode bug.
- **Hand-adjudicated vectors.** For each disagreement, a human decides which side is right
  and the answer is frozen into the vector file with a rationale field. ~30–60 hand-checked
  vectors covering the three payload types (`@` 12 140, `:` 7 774, `!` 5 731 occurrences)
  beat 1821 auto-generated ones.
- **A real capture path.** Add a `MC_TEST_HOOKS`-gated hex dump of the _accepted_ frame
  immediately before `decodeAPRS()` at `lora_functions.cpp:455`, tagged with the same
  `msg_id` the `MH-LoRa:` line carries. Then hex↔decode pairing is by key, not adjacency,
  and it covers good frames. Note this is a _new_ capture campaign — the existing 17 logs
  cannot be retro-fitted.
- **Explicit labelling.** Every vector derived from current output must carry
  `"oracle": "current-behaviour"` so nobody mistakes the suite for a correctness proof.

The 1821 CRC-failed dumps are still worth keeping, but only for one thing: **fuzz/robustness
fixtures** (scenario 3). Assert "does not crash, does not enqueue, returns `0x00`" — never
assert a decoded value from them.

---

## Scenario audit

Legend for "instrumentation present": ✅ marker exists and is reachable in the stated
environment · ⚠️ exists but wrong marker / wrong quantity / partly unreachable · ❌ not
reachable in the stated environment.

| #   | Can it fail?                    | Why / why not                                                                                                                                                                                                                                                                                                                                                                                                                                                                        | Instrumentation present?                                                                                                                               |
| --- | ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 1   | **No — cannot be built**        | 0 hex↔decode pairs exist (measured). If built from `MH-LoRa:` lines the expected value _is_ the decoder's output → detects regressions only, never a pre-existing bug.                                                                                                                                                                                                                                                                                                               | ❌ no good-frame hex dump exists anywhere in the firmware except `bDEBUG`-gated `printBuffer` for _undecodable_ frames (`lora_functions.cpp:687,1209`) |
| 2   | **Weakly**                      | Round-trip identity passes whenever decode/encode are inverse-consistent, including when both are wrong the same way. And `encodeAPRS` recomputes FCS (`aprs_functions.cpp:1085` `aprsmsg.msg_fcs = FCS_SUMME`) → the FCS field's round-trip is tautological by construction.                                                                                                                                                                                                        | ✅ native, no markers needed                                                                                                                           |
| 3   | **Partly**                      | The named markers `CRC_ERROR`/`ERR_PAYLOAD` are emitted at `esp32_main.cpp:3813/3857`, in `checkRX()` **before** `OnRxDone()`. `--inject` calls `OnRxDone` → those markers are structurally unreachable via the hook. The `no RX_DEDUP_ADD` half is falsifiable.                                                                                                                                                                                                                     | ⚠️ 1 of 3 named markers reachable via `--inject`                                                                                                       |
| 4   | **Yes**                         | Genuinely falsifiable — but note `--inject` supplies `size` itself, so the test controls the parameter under test; the vector must deliberately pass `size` > true frame length.                                                                                                                                                                                                                                                                                                     | ✅                                                                                                                                                     |
| 5   | **Yes**                         | `RX_DEDUP_DUP` (`lora_functions.cpp:1343`) + absence of `RELAY_QUEUED` (`:1181`). `expect_dut_not` is the load-bearing half. Best scenario in the catalogue.                                                                                                                                                                                                                                                                                                                         | ✅ both `bLORADEBUG`-gated, both observed in field logs (12 823 / 4330)                                                                                |
| 6   | **Yes, if not hard-coded**      | `MAX_DEDUP_RING` = **100** on S3/RAK4630, **70** native, **60** XML/SBUFFER, **10** TBEAM (`configuration_global.h:83,90,98,105,113`). A test hard-coding 71 or 101 is wrong on the other target.                                                                                                                                                                                                                                                                                    | ✅                                                                                                                                                     |
| 7   | **Yes**                         | `RELAY_QUEUED` present/absent.                                                                                                                                                                                                                                                                                                                                                                                                                                                       | ✅                                                                                                                                                     |
| 8   | **Yes**                         | `RELAY_LOOP_BLOCKED` (`lora_functions.cpp:1119`). Only **3** occurrences in 25 645 field frames → high-value, near-untested path.                                                                                                                                                                                                                                                                                                                                                    | ✅                                                                                                                                                     |
| 9   | **Yes, after H-03**             | `RING_OVERFLOW` (`:1593`), `RING_DROP_PRIO` (`:1555`), `RING_DROP_NEW` (`:1571`) all exist. **All three have 0 occurrences in all 17 logs** — no field baseline. `--dump ring` does not exist yet. `MAX_RING` = 20 (S3) vs 30 (native) vs 10 (TBEAM).                                                                                                                                                                                                                                | ⚠️ markers ✅, `--dump ring` ❌ (proposed only)                                                                                                        |
| 10  | **Yes**                         | `RING_PRIO` at write (`:1524`), `RING_TX_READ` at read (`:1647`) — ordering observable.                                                                                                                                                                                                                                                                                                                                                                                              | ✅                                                                                                                                                     |
| 11  | **No, as specified**            | The doc asserts on "`TX_GATE_ENTER` → `TX_START` deltas". `TX_GATE_ENTER` (`esp32_main.cpp:2340`) fires **after** the backoff has already elapsed — the gate is entered _because_ `(millis()-iReceiveTimeOutTime) >= csma_timeout` (`:2069`). The delta to `TX_START` (`:2434`) is IRQ-poll + 1–2 CAD scans (~30–100 ms), compared against a 3000–5850 ms expectation. Wrong quantity.                                                                                               | ⚠️ correct marker exists but is not the one named — see F7-6                                                                                           |
| 12  | **No**                          | Even with `MC_TEST_SEED`, the PRNG _stream position_ at any TX depends on how many prior draws happened. There are **10 sites** that consume a draw via `csma_compute_timeout*`/`csma_reset` (`lora_functions.cpp:402,1242,1983,2085`; `esp32_main.cpp:2062,2224,2322,2361,2415,2465`), driven by every RX, CAD result and timeout — i.e. by ambient RF. Backoff value is also unprinted on the CAD-free path.                                                                       | ❌ claim in 07 §2.1 is wrong                                                                                                                           |
| 13  | **Yes**                         | `CAD_BUSY` (`esp32_main.cpp:2470`) + absence of `TX_START`.                                                                                                                                                                                                                                                                                                                                                                                                                          | ✅                                                                                                                                                     |
| 14  | **No**                          | `CAD_FALSE_POSITIVE` (`esp32_main.cpp:2405`) fires when scan #1 says busy and scan #2 says free — the firmware disagreeing with **itself**, not with ground truth. And `--spectrum` cannot corroborate: `sx126x_spectral_scan` does `radio.beginFSK()` + `uploadPatch()` and must `radio.begin()` + `lora_setchip_meshcom()` to return to LoRa (`src/spectral_scan.cpp:53,62,93`) — it cannot run concurrently with RX on the same radio.                                            | ⚠️ marker exists, correlation unmeasurable                                                                                                             |
| 15  | **Yes**                         | `RETRANSMIT` (`:1906`), `ACK_RECEIVED` (`:448`). Rare in the field (13 / 25) → real coverage gain.                                                                                                                                                                                                                                                                                                                                                                                   | ✅                                                                                                                                                     |
| 16  | **Yes**                         | `RETRANSMIT_GIVEUP` (`:1895`) exists but has **0** occurrences in all 17 logs — never observed. Needs a forced-failure setup (driver powered down mid-exchange).                                                                                                                                                                                                                                                                                                                     | ✅                                                                                                                                                     |
| 17  | **Broken instrument**           | `ONRXDONE_STATS` is emitted only under `bLORADEBUG` (`esp32_main.cpp:1976`) — the same flag that makes `printfdeb` do a blocking `Serial.printf` inside the radio path, inflating the number it reports. Measured in the field with debug on: **2593 / 27 605 = 9.4 %** of `ONRXDONE_TIME` samples exceed 50 ms; max observed **1209 ms**. Also the counters reset on every emit (`:1978-1979`), so a harness matching "a line with `warn=0`" passes on ~93 % of windows regardless. | ⚠️ V-05 applies directly and the doc does not connect it to this scenario                                                                              |
| 18  | **Not as stated**               | `RX_IRQ_STALE` occurs **2302×** in normal field traffic — it is a routine deferral, not a fault. "No accumulation" has no threshold. `RX_IRQ_STALE_EARLY` (`esp32_main.cpp:2109`) has **0** occurrences ever.                                                                                                                                                                                                                                                                        | ⚠️ needs a rate bound, not a presence check                                                                                                            |
| 19  | **Yes**                         | Real round-trip through NVS (`save_settings()`, `src/esp32/esp32_flash.cpp:280`, ~60 `preferences.put*` calls).                                                                                                                                                                                                                                                                                                                                                                      | ✅                                                                                                                                                     |
| 20  | **No — tautological**           | `--lora` prints `getFreq()/getPower()/getBW()/getSF()/getCR()` (`command_functions.cpp:4211-4212`). All read `meshcom_settings.node_*` (`src/lora_setchip.cpp:81,96,117,126`) — the _same_ struct `--txsf/--txbw/--txcr` wrote. The SX1262 registers are never read back. If `lora_setchip_meshcom()` silently failed to program the chip, this test still passes.                                                                                                                   | ❌ asserts a variable equals itself                                                                                                                    |
| 21  | **Not automatable as written**  | "phone app" is not a harness. Also `MAX_MSG_LEN_PHONE` = 300 (`src/phone_commands.cpp:22`) vs `UDP_TX_BUF_SIZE` = 255 (`configuration_global.h:64`) — "arrives intact" is undefined for the 256–300 range.                                                                                                                                                                                                                                                                           | ❌                                                                                                                                                     |
| 22  | **No — manual procedure**       | No assertion, no exit code. Belongs in a release checklist, not a scenario catalogue.                                                                                                                                                                                                                                                                                                                                                                                                | ❌                                                                                                                                                     |
| 23  | **Not as stated**               | The heap line is printed only **when the value changed** (`esp32_main.cpp:3286-3291`), so the series is irregular, not a 60 s grid. "Does not trend down" has no threshold, no window, no slope bound.                                                                                                                                                                                                                                                                               | ⚠️ needs a quantified criterion                                                                                                                        |
| 24  | **Yes, but self-contradictory** | Falsifiable via `tools/ram_snapshot.py` diff. But "unchanged" is the wrong predicate — and 07 §3 itself requires a RAM/flash snapshot diff to prove `MC_TEST_HOOKS` is inert, which this scenario would flag as a failure. Needs a tolerance and a release-vs-release baseline.                                                                                                                                                                                                      | ✅ tooling exists                                                                                                                                      |
| 25  | **Yes**                         | Real. Note the count: `platformio.ini` + `variants/*/platformio.ini` define **34** `[env:…]`, of which 2 are the safeboot bootloader envs (`esp32-safeboot`, `esp32-S3-safeboot`) that the Heltec V3 upload command depends on. The matrix must build 34, not 32.                                                                                                                                                                                                                    | ✅                                                                                                                                                     |

**Summary: of 25 scenarios, 12 are genuinely falsifiable as written (2, 4, 5, 6, 7, 8, 10,
13, 15, 16, 19, 25); 6 are falsifiable only after a correction (3, 9, 11, 18, 23, 24); 7
cannot fail or cannot be built as specified (1, 12, 14, 17, 20, 21, 22).**

### Marker reachability cross-check

Every marker named in 07 §1.1 exists in `src/`. Two caveats the doc misses:

- **`RX_BUF_SWITCH` / `RX_BUF_OVERWRITE` / `RX_RESTART_EARLY` / `CAD_ABORT_BY_RX` and the
  early `[MC-SM] RX_LISTEN -> RX_PROCESS`** live at `lora_functions.cpp:331, 337, 358, 364,
371` — all inside `#if defined BOARD_RAK4630` (opens `:305`, closes `:375`) **and** inside
  `#ifdef LORA_ISR_DEBUG`. They are nRF52-only. 07 §1.1 lists "RX buffers — double-buffer
  discipline, no overwrite under load" as an assertable property of the bench, and 07 §3
  proposes `-D LORA_ISR_DEBUG` in `[env:heltec_wifi_lora_32_V3-test]`. **On the Heltec V3
  that flag unlocks exactly nothing** — all 5 sites are behind the RAK guard, and the ESP32
  RX path has no double buffer at all. (V-03 is right that the macro is dormant; it is wrong
  about which board can use it.)
- **`RX_BUF_RELEASE`** (`lora_functions.cpp:1249`) is likewise in a RAK-only block.

Markers never once emitted in 25 645 frames of field capture — no baseline exists for any of
them: `RING_OVERFLOW`, `RING_DROP_NEW`, `RING_DROP_PRIO`, `RETRANSMIT_GIVEUP`, `ERR_PAYLOAD`,
`HDR_DETECT`, `IRQ_POLL`, `RX_IRQ_STALE_EARLY`, `RX_OTHER_ERROR`, `CHECKRX`.

---

## Findings

### F7-1: The golden-vector corpus does not exist — measured yield is zero

**Target:** 06 §"What already exists that helps" item 1, 06 §Layer 2, 07 §1.5, 07 §7
scenarios 1–2, 07 §10 step 4.
**Severity: critical.** Layers 2–4 and the "before/after comparison" premise of
[05](../../architecture/05-rewrite-vs-refactor.md) all rest on this.

**Argument.** The plan's foundational asset is described as "17 captured sessions with raw
frame hex + decoded interpretation" and "**a golden-vector corpus you already own**". The
pairing it depends on has zero instances. The 1821 hex dumps are `CRC_PAYLOAD` — dumps taken
on the `RADIOLIB_ERR_CRC_MISMATCH` branch, which returns before `OnRxDone()` and therefore
before `decodeAPRS()`. A frame cannot be both dumped and decoded.

**Evidence.** Measured table above (0 pairs at windows of 5 and 50). Emit site
`src/esp32/esp32_main.cpp:3818` inside the branch opened at `:3781`; the only `OnRxDone()`
call is `:3778` in the sibling branch; `decodeAPRS()` at `src/lora_functions.cpp:455`.
`grep -hoE '\[MC-DBG\] [A-Z_]+\[[0-9]+\]:'` returns `CRC_PAYLOAD[255]` and nothing else.
The doc's own example pairs `…172422.log:363` (17:26:21.918, CRC-failed, rssi −119) with an
`MH-LoRa:` line from 17:24:32.263 — different frame, 109 s apart.

**Corrected plan.** Replace 07 §10 step 4 with:

1. Add a `MC_TEST_HOOKS`-gated hex dump of the accepted frame immediately before
   `decodeAPRS()` at `lora_functions.cpp:455`, keyed by `msg_id`.
2. Run a fresh capture session (a few hours of live traffic yields thousands of frames).
3. Pair by `msg_id`, not by line adjacency.
4. Separately, convert the 1821 existing `CRC_PAYLOAD` dumps into **robustness** fixtures for
   scenario 3 only, asserting `decodeAPRS() == 0x00` / no enqueue / no crash — never a field
   value. State in the file header that lengths are unknown (all dumps are 255).

### F7-2: The proposed oracle is the decoder's own output — it can only detect regressions

**Target:** 06 §Layer 2 step 2 ("A Unity test iterates every vector, calls `decodeAPRS()`,
asserts field-by-field"), 06 line 160 ("**Then the oracle exists.**").
**Severity: critical (conceptual).**

**Argument.** The `MH-LoRa:` line is `printBuffer_aprs()` applied to the struct that
`decodeAPRS()` just filled. Using it as `expect` makes the assertion
`decode_new(x) == decode_today(x)`. Every current decode defect is promoted to
specification, and the suite will go green over it forever. This is precisely the failure
mode the audit brief names — "golden files regenerated from current behaviour" — and 06
presents it as _the_ solution to the before/after problem without noting the limitation once.

Three compounding factors:

- The corpus is self-selecting: `decodeAPRS` returns `0x00` on FCS mismatch
  (`aprs_functions.cpp:414-430`), so only frames the current decoder already accepts ever
  produce an `MH-LoRa:` line. 0 `"discarded, wrong FCS"` lines in 17 logs.
- The projection is lossy: `lat`/`lon`/`alt` in 06's JSON are not in the `MH-LoRa:` format
  string at all; they require `decodeAPRSPOS()` (`aprs_functions.cpp:531`) — a second
  function under test.
- Round-trip (scenario 2) does not rescue it: `encodeAPRS` recomputes FCS
  (`aprs_functions.cpp:1085`), so that field round-trips by construction regardless of
  whether decode read it correctly.

**Corrected plan.** Add to 06 §Layer 2, before the build steps:

> **Oracle caveat.** `MH-LoRa:` is `printBuffer_aprs()` of the struct `decodeAPRS()`
> produced. Vectors derived from it pin _current_ behaviour: they catch regressions, not
> pre-existing defects, and they cannot cover frames the current decoder rejects. Tag every
> such vector `"oracle": "current-behaviour"`.

Then add the genuine-oracle track: an independent Python reference decoder written from the
frame layout, ~30–60 hand-adjudicated vectors across the three payload types (`@`/`:`/`!`),
each carrying a rationale. Budget: this is the real cost of Layer 2, not the 3–5 sessions in
06's effort table.

### F7-3: `--inject` cannot reach the RX error path — scenario 3's markers are unreachable

**Target:** 07 §3 H-01, 07 §7 scenario 3, 07 line 489 ("Scenarios 1–10 need **no radio at
all** once `--inject` exists").
**Severity: major.**

**Argument.** H-01 is a genuinely good hook and the doc's assessment of its value is broadly
right — but the boundary is drawn one function too high. On ESP32 the real path is:

```
setFlagReceive()  (ISR, esp32_main.cpp:487 — sets an atomic flag only)
  → receiveFlag → esp32loop → checkRX(bRadio)      (esp32_main.cpp:3691)
      → radio.readData(payload, ibytes)             (:3714)
      → branch on `state`                           (:3716 / :3781 / :3824)
          → OnRxDone(payload, ibytes, rssi, snr)    (:3778)   <-- H-01 enters here
```

Calling `OnRxDone()` from the serial-command context is **correct for context**: on ESP32
`setFlagReceive` is a bare flag-setter (`esp32_main.cpp:487-498`) and `OnRxDone` already runs
in the loop task, same as `checkSerialCommand()`. So H-01 does _not_ skip IRQ context in any
meaningful way on this board. What it skips is everything in `checkRX` above the call:

- `radio.readData()` and the RadioLib `state` dispatch — so `CRC_ERROR` (`:3813`),
  `CRC_PAYLOAD` (`:3818`), `RX_OTHER_ERROR` (`:3850`), `ERR_PAYLOAD` (`:3857`) are all
  **structurally unreachable via `--inject`**;
- `RX_RESTARTED src=after_readData` (`:3750`), the `LORA_DIO1` missed-edge recovery
  (`:3745-3750`), `is_receiving` bookkeeping, `ch_util_rx_accum` (`:3776`);
- the whole `RX_TIMEOUT_*` / `RX_IRQ_STALE*` / `receiveFlag` state machine (`:2069-2210`).

The nRF52 double-buffer (`rxPayloadCopy[2]`, `RX_BUF_SWITCH`/`RX_BUF_OVERWRITE`) is inside
`#if defined BOARD_RAK4630` (`lora_functions.cpp:305-375`) and does not exist on the Heltec
V3 at all, so "buffer switching" is not something the hook can skip on the bench board.

**Genuinely covered by H-01:** scenarios 4, 5, 6, 7, 8, 9, 10 — the dedup ring, hop budget,
relay decision, TX ring and priority ordering all live inside or downstream of `OnRxDone()`.
**Only appears covered:** scenario 3 (2 of its 3 named markers), and any claim about RX
restart / IRQ / CRC handling. Scenarios 1–2 are native and unaffected.

**Corrected plan.** Amend 07 §3 H-01 to state the boundary explicitly, and change line 489 to
"Scenarios 4–10 need no radio at all once `--inject` exists; scenario 3 is covered only for
frames that pass CRC — CRC/PHY rejection is above the hook and needs a radio or a second
hook." Optionally add **H-01b `--injectraw <hex> state=<n>`**, entering at `checkRX`'s state
dispatch instead, which would cover the CRC/error branches (~10 extra lines).

### F7-4: A native build silently selects a ring configuration that ships on no bench board

**Target:** 06 §Layer 1 (test target #4 explicitly names `MAX_MHEARD`), 07 §7 scenarios 6, 9.
**Severity: major.**

**Argument.** `configuration_global.h:79-116` selects six ring dimensions from a five-way
`#if` on `ENABLE_XML` / `ENABLE_SBUFFER` / `CONFIG_IDF_TARGET_ESP32S3 || BOARD_RAK4630` /
`ENABLE_TBEAM` / else. A `platform = native` build defines none of them and falls into the
`#else` (ESP32-classic) branch. Measured by preprocessing:

| configuration                            | MHEARD |  MHPATH |   RING | DEDUP_RING | LOG | RING_UDP |
| ---------------------------------------- | -----: | ------: | -----: | ---------: | --: | -------: |
| **native (no macros)**                   | **30** |  **40** | **30** |     **70** |  20 |       25 |
| ESP32-S3 / RAK4630 — _both bench boards_ | **80** | **100** | **20** |    **100** |  10 |       20 |
| `ENABLE_XML` / `ENABLE_SBUFFER`          |     50 |      50 |     20 |         60 |  20 |       20 |
| `ENABLE_TBEAM`                           |     10 |      10 |     10 |         10 |  10 |       10 |

Every dimension differs. A native `updateMheard()` test would exercise `MAX_MHEARD` = 30 — a
value used by neither Heltec V3 nor RAK4631. Scenario 6 needs 71 ids natively and 101 on the
bench; scenario 9 overflows at 31 natively and 21 on the bench. A suite that hard-codes
either number is silently wrong for the other, and 06 flags `MAX_MHEARD` as "varies per
board" without drawing the consequence.

Same problem one level up: `src/loop_functions.h:9-13` is `#ifdef ESP32 → esp32/esp32_flash.h
#else → nrf52/WisBlock-API.h`. A native build with no `ESP32` define takes the nRF52 branch
and pulls `LoRaWan-Arduino.h`, `nrf_nvic.h`, `mbed.h`, `rtos.h`, `bluefruit.h`, `NimBLE*`,
`ArduinoJson.h` (`src/nrf52/WisBlock-API.h:49-139`). Verified: the first native compile error
is `WisBlock-API.h:50: fatal error: 'LoRaWan-Arduino.h' file not found`. So the native env
must pick a platform _and_ a variant, and neither choice is currently declared anywhere.

**Corrected plan.** Add to 06 §Layer 1:

> The native environment must declare its configuration explicitly, e.g.
> `-D ESP32 -D CONFIG_IDF_TARGET_ESP32S3` plus `-I variants/heltec_wifi_lora_32_V3`, so it
> matches a board that is actually verified on the bench. Any test touching a ring dimension
> must read the constant (`MAX_MHEARD`, `MAX_RING`, `MAX_DEDUP_RING`) rather than hard-code
> it, and the ring tests should be parameterised over at least the S3 and the `#else` profile
> — otherwise 15 of 32 environments are covered by nothing.

### F7-5: The Layer-1 blocker is misdiagnosed — but it is _cheaper_ than the doc claims

**Target:** 06 §Layer 1 ("The blocker is not the algorithms — it is `#include
<configuration.h>` and `loop_functions_extern.h`"), 06 effort table ("~5–8 sessions").
**Severity: minor (accuracy), positive for the plan.**

**Argument.** I built the shim and measured the real closure. `configuration.h` and
`loop_functions_extern.h` are _not_ the blockers — `loop_functions_extern.h` needs only a
`portMUX_TYPE` typedef. The actual blockers, none of which 06 mentions, are:

1. `loop_functions.h:12` → `nrf52/WisBlock-API.h` on any build without `ESP32` defined;
2. `debugconf.h:6` → `net_console.h:28-30` → `Print.h`, `WiFi.h`, and `Stream` with virtual
   `available()/read()/peek()/flush()` (`net_console.h:58-64` uses `override`).

With `-DESP32`, one ~45-line `Arduino.h` (a `String` over `std::string`), a ~5-line
`Print.h`/`Stream`, a ~10-line `WiFi.h`, and a `portMUX_TYPE` typedef, **both files compile
clean**. Measured link closure (`nm -u`, C++ symbols demangled, libc/libstdc++ removed):

| file                 | globals to define                                                                                                                 | functions to stub                                                                                                                   |
| -------------------- | --------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| `aprs_functions.cpp` | `bDisplayCont`, `bDisplayInfo`, `bLORADEBUG`, `bMESH`, `BOARD_HARDWARE`, `meshcom_settings`, `MSerial` (7)                        | `checkRegexCall(String)`, `getMOD()`, `printAsciiBuffer(uint8_t*,int)` (3)                                                          |
| `via_functions.cpp`  | `bDisplayCont`, `bDisplayInfo`, `bGATEWAY`, `bMESH`, `bVIA`, `meshcom_settings`, `mheardCalls`, `mheardEpoch`, `mheardNCount` (9) | `getTimeString()`, `getUnixClock()`, `is_equ(const char*,const char*)`, `printfdeb(const char*,...)`, `printlndeb(const char*)` (5) |

That is ~16 globals and ~8 stubs for both files together. **This is 1–2 sessions, not 5–8**,
and no seam extraction is needed for these two targets. 06 should say so — the pessimistic
framing ("Seam extraction: split the pure logic out") risks the cheapest, highest-value layer
being deferred as expensive.

Caveat: this was measured for the two files in the brief. `loop_functions.cpp` (target #2,
`PositionToAPRS()`) and `lora_functions.cpp` (target #3, CSMA) were not measured and are
much larger; the seam-extraction advice may well hold there.

### F7-6: `randomSeed()` does not make the backoff reproducible, and the named marker measures the wrong interval

**Target:** 07 §2.1 ("the backoff sequence becomes byte-for-byte replayable, and you can
assert exact slot selection and exact retry timing"), 07 §3 H-02, 07 §7 scenarios 11–12,
07 §10 step 2, V-02.
**Severity: major.**

**Argument, part 1 — stream position.** Seeding fixes the _sequence_, not the _position in
it_. `random()` is drawn at exactly one place (`lora_functions.cpp:2076`) but reached from
**10 call sites**, every one of which is triggered by an ambient event:

`lora_functions.cpp:402` (OnRxDone, ACK path) · `:1242` (OnRxDone tail) · `:1983`
(`csma_reset` in `doTX`) · `:2085` (`csma_reset` body) · `esp32_main.cpp:2062` · `:2224` ·
`:2322` · `:2361` · `:2415` · `:2465`.

Every received frame, every CAD outcome, every RX timeout consumes one or more draws. Two
runs of the same scripted scenario will have consumed different numbers of draws by the time
the TX under test happens, unless the entire RF environment and all CAD verdicts are
bit-identical. CAD verdicts depend on real thermal noise. Scenario 12 ("exact backoff
sequence") is therefore **not achievable on a 2-node RF bench**, seeded or not.

Secondary risk worth verifying before relying on H-02: arduino-esp32's `random()` delegates
to newlib `rand()`, whose state lives in the per-task `_REENT` struct on ESP-IDF. A
`randomSeed()` in `esp32setup()` seeds the loop task only. All 10 sites above are in the loop
task today, so this is currently benign — but it makes H-02 fragile against any future move
of radio work to its own task, and the doc should say so.

**Argument, part 2 — wrong marker.** Scenario 11 asserts on "`TX_GATE_ENTER` → `TX_START`
deltas". `TX_GATE_ENTER` (`esp32_main.cpp:2340`) is only reached _after_ the backoff expired
(`:2069`: `if((uint32_t)(millis() - iReceiveTimeOutTime) >= csma_timeout)`). The
`TX_GATE_ENTER`→`TX_START` interval is IRQ-register polling plus one or two `radio.scanChannel()`
calls (`:2380`, `:2399`) — tens of milliseconds — and would be compared against a 3000–5850 ms
expectation. Guaranteed mismatch.

**The correct instrumentation already exists.** `esp32_main.cpp:2073-2074` prints
`RX_TIMEOUT_FIRE ts=%lu wait=%lu delta=%lu` where `wait` **is** `csma_timeout` — the selected
backoff, directly. Field logs confirm: `RX_TIMEOUT_FIRE ts=27560 wait=4500 delta=4600`
immediately followed by `TX_GATE_ENTER` (`meshcom_2026-03-23_194521.log`). It is the most
frequent marker in the corpus (59 908 occurrences), so the data is already there.

**Corrected plan.**

- Scenario 11 → assert on `RX_TIMEOUT_FIRE wait=` against
  `[base, base + slots×CSMA_SLOT_SIZE]` per priority. Fully falsifiable, needs **no radio and
  no second node** — demote it from "2-node bench" to `--inject`/single-board.
- Scenario 12 → either delete, or move it to the host-side `FakeRadio`/`FakeClock` layer
  (06 Layer 3) where the event sequence _is_ controllable and exact-sequence assertions are
  meaningful. On the bench, assert distribution: N samples of `wait=`, check the values are
  a subset of `{base + k×35 | k ∈ 0..slots}` and that all `slots+1` values occur.
- V-02 stays valid as written (`randomSeed()` really is never called), but its consequence
  should read "makes the _distribution_ reproducible", not "exact values".

### F7-7: Scenario 20 asserts a variable against itself

**Target:** 07 §7 scenario 20, 07 §5 ("RadioLib init and `--lora` parameter round-trip on the
real SX1262").
**Severity: major** — this is the on-device layer's flagship test.

**Argument.** `--lora` (`command_functions.cpp:4210-4212`) formats
`getFreq(), getPower(), getBW(), getSF(), getCR()`. All five read from `meshcom_settings.node_*`
(`src/lora_setchip.cpp:81, 96, 117, 126`), the same struct `--txfreq/--txpower/--txbw/--txsf/--txcr`
write. Nothing in the path touches an SX1262 register. The test asserts
`meshcom_settings.node_sf == meshcom_settings.node_sf`. The named failure — "RF parameters
actually applied" — is exactly the one it cannot detect: if `lora_setchip_meshcom()` returned
an error and left the chip at its previous SF, `--lora` would still report the new value.

**Corrected plan.** Either (a) add a `MC_TEST_HOOKS`-gated `--lorahw` that reads back via
RadioLib getters / register reads and prints both values, then assert equality of _chip_ and
_settings_; or (b) make it a 2-node on-air test: set node A to SF11, node B to SF12, assert
node B receives nothing, then re-match and assert it does. Option (b) needs no firmware
change and is genuinely falsifiable.

### F7-8: The whole event stream is produced by a function that re-interprets its own output as a format string

**Target:** 07 §1.1 (the event stream as the assertion substrate), §1.4 (`--debug csv` as
"the machine-readable output mode"), §6 (`bench_runner.py` matching the stream), §7 scenarios
3, 5–10 (which inject peer-controlled bytes).
**Severity: major** — it undermines the substrate every bench assertion stands on, and it is
also a robustness bug in shipping firmware.

**Argument.** `printfdeb()` (`src/printfdeb_functions.cpp:39-124`) does
`vsnprintf(temp, …, nformat, arg)` at `:96` and then, at `:118`, `Serial.printf(temp);` —
passing the _already-substituted_ result back in as a format string. Any `%` that arrived via
a `%s` argument is re-interpreted as a conversion specifier on the second pass. The `%%`
handling at `:48-59` operates on the _format_ string only and cannot see argument content.

This is directly reachable from the air: `printBuffer_aprs()` (`loop_functions.cpp:2953`)
passes `aprsmsg.msg_payload.c_str()` through `%s`, and payloads are peer-controlled. **672 of
25 645 `MH-LoRa:` lines in the existing captures already contain a `%`.** A crafted payload
with `%n` or a long `%999999f` reaches `Serial.printf` as a directive.

For the test plan this matters twice: (1) any bench assertion on a line containing received
text is parsing output that may have been mangled by its own content, so a "missing marker"
failure may be a formatting artefact rather than a behavioural one; (2) scenarios 3 and 5–10
inject arbitrary frames and none of them covers this input class, so the suite would go green
while the DUT can be crashed by a text message.

**Second problem, same section.** 07 §1.4 recommends `--debug csv --debug en` for "stable
separators". The `;`→space rewrite at `:62-80` applies **only to the format string** —
semicolons inside `%s` arguments pass through unchanged in both modes. APRS payloads contain
literal `;` routinely (`R=20;232;262;9;26244;26244;`): **15 783 of 25 645 `MH-LoRa:` lines
contain at least one `;`**. A CSV parse of the event stream will mis-field on ~62 % of frame
lines. The separator is not stable.

**Corrected plan.**

- Change `:118` to `Serial.print(temp);` (or `Serial.printf("%s", temp)`). One line, fixes the
  injection, and it is a prerequisite for trusting any bench result. Add an `--inject` vector
  with `%n%s%999999f` in the payload as the regression test.
- Amend 07 §1.4: `--debug csv` controls only the format string; payload text is not escaped.
  A harness must parse by marker prefix and field name, not by splitting on `;`. Add a
  scenario: "a received payload containing `;`, `%` and a newline does not corrupt the event
  stream".

### F7-9: `--save`-based snapshots are the flash wear the doc is trying to avoid

**Target:** 07 §1.2 last bullet, 07 §4.4.
**Severity: minor.**

**Argument.** 07 §1.2 warns "reflashing config on every test wears flash. Prefer `--save`-based
snapshots and restore". But `--save` **is** the flash write: `save_settings()`
(`src/esp32/esp32_flash.cpp:280+`) issues ~60 `preferences.put*` calls into NVS. §4.4 then
prescribes `--cleanflash` + config block + `--save` at the start of _each session_. The advice
is self-cancelling, and no scenario measures the cost.

**Corrected plan.** State the actual mechanism (ESP-IDF NVS skips writes of unchanged values,
so a repeated identical `--save` is cheap; a _changing_ config block is not). Add a scenario
tracking NVS wear over a long soak, and prefer configuring via commands **without** `--save`
for per-test setup, reserving `--save` for the once-per-session baseline.

### F7-10: `-D LORA_ISR_DEBUG` in the proposed Heltec V3 test env unlocks nothing

**Target:** 07 §1.1 "Gating" bullet 2, 07 §3 build-flags block, V-03.
**Severity: minor.**

**Argument.** All five `LORA_ISR_DEBUG` sites are `src/lora_functions.cpp:330, 336, 357, 363,
371`, and the enclosing `#if defined BOARD_RAK4630` opens at `:305` and closes at `:375`. The
macro is nRF52-only. 07 §3's `[env:heltec_wifi_lora_32_V3-test]` adds `-D LORA_ISR_DEBUG` and
07 §1.1 calls it "a pre-existing, undocumented test hook" for the bench. On the Heltec V3 the
ESP32 RX path has no double buffer, no `RX_BUF_SWITCH`, no `CAD_ABORT_BY_RX` and no
`RX_RESTART_EARLY` — the code those markers describe does not exist for that board.
Consequently 07 §1.1's "RX buffers — double-buffer discipline, no overwrite under load" is
not an assertable property of the proposed bench at all.

**Corrected plan.** Move `-D LORA_ISR_DEBUG` to a `[env:wiscore_rak4631-test]`, and move the
"RX buffers" row of 07 §1.1 into the "Not covered" column of 07 §8.

### F7-11: Scenario 17 is a broken instrument, and scenario 24 contradicts 07 §3

**Target:** 07 §7 scenarios 17, 24; 07 §3 design rule 2; V-05.
**Severity: minor** (both correctable in place).

**Argument (17).** `ONRXDONE_STATS` is emitted only under `bLORADEBUG`
(`esp32_main.cpp:1976`), and `bLORADEBUG` is what makes `printfdeb` do a blocking
`Serial.printf` from inside `OnRxDone`. The budget can only be measured in the mode that
breaks it. Measured across the 17 captures (all taken with debug on): **2593 of 27 605
`ONRXDONE_TIME` samples exceed 50 ms (9.4 %); maximum 1209 ms**; 2419 of 35 188
`ONRXDONE_STATS` windows report `warn > 0`. So the assertion `warn=0` fails on current
firmware — but the counters reset on every emit (`:1978-1979`), so a harness that greps for
"a line with `warn=0`" passes on ~93 % of windows regardless of behaviour. Both readings are
wrong.

**Argument (24).** 07 §3 rule 2 requires proving `MC_TEST_HOOKS` is inert via "a RAM/flash
snapshot diff against the release build". Scenario 24 asserts the RAM/flash footprint is
"unchanged by a refactor". Adding the hooks changes it. As written the two requirements
cannot both be satisfied.

**Corrected plan.** Scenario 17 → assert "**no** `ONRXDONE_STATS` line in the run has
`warn > 0`" (all windows, not the last one), and record the debug-on baseline (9.4 %, max
1209 ms) as the known-current number so the assertion starts as a _documented failure_ rather
than a false green. Better: make the budget measurable with the stream off — buffer
`onrxdone_max_ms` and expose it via H-05 counters, which are read on demand outside the radio
path. Scenario 24 → assert against the _release_ build of the previous commit, with an
explicit tolerance, and exclude `MC_TEST_HOOKS` builds.

### F7-12: `pio test` cannot upload to the Heltec V3 — the custom upload command targets the wrong binary

**Target:** 07 §5 (the whole section), 07 §10 step 7.
**Severity: major** — 07 §5 calls this "the most under-used capability available"; as specified
it does not run at all.

**Argument.** Four independent blockers, each verified.

1. **The upload command is hard-coded to the non-test binary.**
   `variants/heltec_wifi_lora_32_V3/platformio.ini:4` sets `upload_protocol = custom`
   (inherited, `platformio.ini:163`) with:

   ```
   upload_command = pio pkg exec -p "tool-esptoolpy" -- esptool -b 921600 write_flash
     0x0000  ${platformio.build_dir}/${this.__env__}/bootloader.bin
     0xE000  otadata.bin
     0x8000  ${platformio.build_dir}/${this.__env__}/partitions.bin
     0x10000 safeboot-s3.bin
     0xC0000 ${platformio.build_dir}/${this.__env__}/firmware.bin
   ```

   PlatformIO's test runner reuses the env's upload mechanism, so this exact command runs.
   But `pio test` emits its binary to `.pio/build/<env>/<test_name>/firmware.bin` — one
   directory deeper than `${platformio.build_dir}/${this.__env__}/firmware.bin`. The command
   would flash a **stale non-test firmware**, or fail outright if the env was never built
   normally. A test run that flashes the wrong image and then reads Unity output that never
   comes is the worst possible failure mode: it looks like a timeout, not a harness bug.
   It also unconditionally rewrites `otadata.bin` and the safeboot image on every single test
   invocation.

2. **`setup()`/`loop()` collision.** `src/main.cpp:22` and `:52` define both. PIO 6 defaults
   to `test_build_src = no`, so out of the box `src/` is not compiled into the test binary —
   meaning **no module under test is linked** and the tests can only exercise code they
   `#include` directly. Turning on `test_build_src = yes` produces a duplicate-symbol error
   against the Unity runner's own `setup`/`loop`. The needed filter is
   `build_src_filter = ${esp32.src_filter} -<main.cpp>` — note it must interpolate the
   **legacy** key `src_filter`, because `[esp32]` uses that name (`platformio.ini:136-147`);
   the repo already relies on this in `variants/t_deck/platformio.ini:9-10`.

3. **No `test_*` configuration exists.** `grep` for `test_port|test_speed|test_framework|
test_build_src|test_ignore|test_filter` across `platformio.ini` and all
   `variants/*/platformio.ini` returns **0 hits**. 07 §5's snippet is aspirational, not a diff.
   Its `test_port = /dev/cu.usbserial-XXXX` is also the wrong device class — see (4).

4. **`test_port` is not usable with this upload path, though the serial itself is fine.**
   `esptool` in the custom command is invoked with **no `--port`**, so PIO's `--test-port` is
   never threaded through. Separately, the doc's §9 correction about USB is right and matters
   here: the env sets `-DARDUINO_USB_MODE=1` with `-DARDUINO_USB_CDC_ON_BOOT=1` **commented
   out** (`variants/heltec_wifi_lora_32_V3/platformio.ini:27-28`), so `Serial` binds to UART0
   (TX 43 / RX 44), and `Serial.begin(MONITOR_SPEED)` runs at `esp32_main.cpp:608` with a
   5-second `while (!Serial …)` block at `:615-617`. Unity results at 115200 on UART0 are
   readable — the transport is fine, the _upload_ is what is broken.

**Flash headroom is not a blocker — the doc's only unexamined worry is the one that is fine.**
`partitions-4MB-safeboot.csv` gives `app` (ota_0) at offset `768K = 0xC0000`, size
`3324K = 3 403 776 bytes`; `safeboot` (factory) at `64K = 0x10000`, size `704K = 720 896`.
Latest recorded build (`docs/ram-comparison-20260517.md:156`, commit `e9edf0df`) is
**1 492 589 B = 43.9 %**, leaving **1.91 MB free**. A Unity test firmware fits comfortably.

**Corrected plan.** Replace 07 §5's snippet with something that can actually run:

```ini
[env:heltec_v3-hwtest]
extends = env:heltec_wifi_lora_32_V3
test_framework = unity
test_build_src = yes
build_src_filter = ${esp32.src_filter} -<main.cpp>
test_speed = 115200
build_flags = ${env:heltec_wifi_lora_32_V3.build_flags} -D MC_TEST_HOOKS -D MC_TEST_SEED=12345
; upload_command must be overridden — the inherited one points at the non-test firmware.bin
upload_command = pio pkg exec -p "tool-esptoolpy" -- esptool -b 921600 --port $UPLOAD_PORT
    write_flash 0xC0000 $SOURCE
```

(`$SOURCE` resolves to the actual built image; dropping the bootloader/partition/safeboot
writes is both correct — they are unchanged between runs — and much faster.) Verify on
hardware before promoting 07 §10 step 7 out of "medium effort".

**Also correct in passing:** V-01 is substantively right (`heltec_wifi_lora_32_V3` has no
`monitor_filters`), but the count is off — **11** variant envs set
`monitor_filters = esp32_exception_decoder`, plus the 2 safeboot envs, not "12 other
variants".

---

## Missing test categories

Ordered by consequence. None appear in 06 or 07.

1. **`millis()` rollover / 49.7-day wrap — surveyed, and the code is _mostly_ safe with a
   real broken minority.** The dominant idiom `(uint32_t)(millis() - timer) >= x` is used at
   **70 sites** and is correct: I checked the declarations and every primary loop timer
   (`posinfo_timer`, `csma_timeout`, `iReceiveTimeOutTime`, `heyinfo_timer`,
   `telemetry_timer`, `hb_timer`, `web_timer`, `retransmit_timer`, `ring_status_timer`,
   `ch_util_timer`, `heapMonTimer`, …) is `unsigned long`/`uint32_t`
   (`src/loop_functions_extern.h:22, 181, 211, 243-244, 253, 284-293`;
   `src/esp32/esp32_main.cpp:465, 476, 479, 3280`). A further 30 uncast `millis() - X` sites
   also have 32-bit-unsigned operands and are therefore fine.

   The broken minority is the **absolute-deadline** form — **36 assignments
   `X = millis() + N` feeding 17 comparison sites `millis() > X` / `millis() < X`**, which is
   not rollover-safe. Verified examples compiled for the Heltec V3:
   - `src/esp32/esp32_main.cpp:3171-3173` — `if(rebootAuto > 0) { if (millis() > rebootAuto)
{ … ESP.restart(); } }`, with `rebootAuto = millis() + 5 * 1000;` set at **21 sites** in
     `src/command_functions.cpp` (e.g. `:1538, 1558, 2110`). Near the wrap the deadline wraps
     to a small value and the reboot fires immediately instead of after 5 s.
   - `src/esp32/esp32_main.cpp:3151-3153` — `if (millis() > DisplayOffWait)`, set at
     `src/loop_functions.cpp:2178` `DisplayOffWait = millis() + (30 * 1000);`.
   - `src/esp32/esp32_main.cpp:580` / `:593` — `if (millis() > check_temperature)` /
     `check_temperature = millis() + 1000;`.
   - `src/gps_functions.cpp:368-370, 391, 414, 416, 421` — `while (millis() < startTimeout)`
     with `startTimeout = millis() + 500;`. Across the wrap the condition is false on entry
     and the GPS autobaud wait windows silently collapse to zero.
   - `src/clock.cpp:72` with `:118-119` — `u32Next_m = u32Start_m + 1000; … if (millis() >
u32Next_m)`.
   - `src/esp32/esp32_main.cpp:3008` — `(millis() > 100000 && millis() < 130000 && bPosFirst)`,
     an absolute-uptime window that re-opens after every wrap.

   Two width/signedness defects also exist, both outside the V3 `src_filter` but live on other
   targets: `time_t timeout = millis();` at `src/nrf52/nrf52_main.cpp:443` compared at `:447`
   (on arm-none-eabi `time_t` is `__int_least64_t`, so the delta is **not** mod-2³²), and the
   explicit narrowing `(millis() - (int)posinfo_timer)` at `src/loop_functions.cpp:1350`
   assigned into an `int`.

   None of this is covered by any scenario, and it is unobservable for 49 days.
   **Test:** a `MC_TEST_HOOKS` base-offset hook (H-04) initialised to `0xFFFFF000` at boot,
   then run the normal scenario suite across the wrap; plus a native test over the deadline
   sites once they are extracted. This is the single highest-value missing test, and H-04 —
   which 07 §3 rates "optional, lower priority" — is exactly what enables it. The cheap
   permanent fix is to convert the 17 comparison sites to the subtraction form the rest of the
   codebase already uses.

2. **Format-string / hostile-payload robustness.** See F7-8. Inject payloads containing `%n`,
   `%s`, `%99999f`, `;`, `\n`, `\0`, 8-bit and UTF-8 bytes, and a 255-byte payload with no
   terminator. Assert: no crash, event stream stays parseable, no field bleeds across markers.
3. **Watchdog and forced recovery.** `esp_task_wdt_add(NULL)` at `esp32_main.cpp:603`,
   `esp_task_wdt_reset()` at `:1745`, and a TX watchdog printing `[MC-WDT] TX_WATCHDOG fired
after %lums` at `:2035`. No scenario exercises either. **Test:** stall the TX path (H-01
   plus a hook that suppresses `OnTxDone`) and assert `[MC-WDT] TX_WATCHDOG` fires within
   `TX_WATCHDOG_MS` (15 000, `configuration_global.h:144`) and the node returns to RX.
4. **OTA and rollback.** `src/safeboot/ElegantOTA.cpp` (`Update.begin()` at `:79, 89, 156,
166`) and `esp_ota_set_boot_partition(partition)` at `command_functions.cpp:617`. Nothing
   in the catalogue covers upload of a corrupt image, power loss mid-write, or whether a bad
   image can be rolled back. `esp_ota_mark_app_valid_cancel_rollback` does not appear in
   `src/` — worth confirming whether rollback is armed at all. The Heltec V3's two-image
   layout (safeboot + app) is exactly what makes this testable on the bench.
5. **Power-loss during flash write.** Cut USB power during `--save` / during OTA, reboot,
   assert the node comes up with either the old or the new config — never a corrupt NVS that
   bricks boot. Cheap with a USB power switch; catastrophic in the field if wrong.
6. **Brownout.** No `brownout`/`rtc_cntl` handling appears anywhere in `src/`. Battery-powered
   nodes at low voltage are a real deployment condition and the detector's default behaviour
   (reset loop) is untested.
7. **RAM exhaustion under sustained load.** Scenario 23 watches the heap trend but nothing
   drives it. **Test:** `--inject` at line rate for an hour with `MAX_RING`, `MAX_DEDUP_RING`
   and `MAX_MHEARD` all saturated, plus BLE connected and WiFi/UDP up, asserting
   `ESP.getMaxAllocHeap()` (already printed at `esp32_main.cpp:3291`) stays above a floor.
   Fragmentation, not free-heap total, is the failure mode.
8. **Variant-boundary coverage.** 07 §8 honestly lists what 2× Heltec V3 misses, but no
   scenario targets the boundaries. The compile-time fan-out is the risk: 6 ring dimensions ×
   5 configuration branches (F7-4), `USE_NEW_BATT` defined in **15 of 31** variants, 4 display
   stacks, 3 radio families. **Cheap mitigation:** a build-time assertion test — compile a
   tiny native TU per variant that `static_assert`s the ring dimensions and buffer sizes are
   internally consistent (`MAX_DEDUP_RING >= MAX_RING`, `UDP_TX_BUF_SIZE <= MAX_MSG_LEN_PHONE`,
   etc.) across all 34 envs. Catches the class of bug the bench structurally cannot.
9. **Concurrency / atomics.** `receiveFlag`, `bEnableInterruptReceive`, `transmittedFlag`,
   `scanFlag` are `std::atomic<bool>` (`esp32_main.cpp:461-473`) and `iWrite`/`iRead` were
   recently made atomic (commit `8009aa19`). `onrxdone_max_ms` / `onrxdone_warn_count`
   (`lora_functions.cpp:105-106`) are **not** atomic. No scenario stresses the ISR/loop
   boundary. `--inject` cannot cover this — it runs in loop context by construction (F7-3),
   which is precisely the race it cannot reproduce. This needs real RF or a dedicated hook.
10. **BLE/WiFi/UDP task interference with radio timing.** 07 §2 lists `millis()` granularity
    and the live network as determinism threats but never mentions that WiFi, BLE (NimBLE) and
    the web server run as separate FreeRTOS tasks that preempt the loop task. Any timing
    scenario (11, 13, 15, 17) must either pin the connectivity state or assert with a margin
    that covers preemption. **Test:** run scenario 11 with (a) WiFi off/BLE off, (b) WiFi on +
    BLE paired + `--webserver on`, and compare the `wait=`→`TX_GATE_ENTER` jitter
    distributions. If they differ materially, every other timing bound needs widening.
11. **Net-console path parity.** 07 §1.3 recommends driving one node over the net console, but
    no scenario asserts that `commandAction(..., ble)` produces identical results across the
    three I/O channels. A harness that drives via TCP and a firmware that behaves differently
    there is a silent false green.

---

## Verification notes

Everything above is measured against the working tree at `1ba101f4`. Specifically:

- Log counts come from a script over all 17 files in `tools/meshcom_monitor/`; the pairing
  measurement was run at adjacency windows of 5 and of 50 lines with identical results.
- The native-build closure (F7-5) was **not inferred** — I wrote the shim
  (`Arduino.h`/`Print.h`/`WiFi.h` + a `portMUX_TYPE` typedef), compiled both
  `src/aprs_functions.cpp` and `src/via_functions.cpp` to objects with
  `g++ -std=gnu++17 -DESP32 -DUNIT_TEST`, and read the undefined-symbol lists off the objects
  with `nm -u`. Shim and objects are in this session's scratchpad.
- The ring-dimension table in F7-4 comes from preprocessing `configuration_global.h` under
  each macro set (`gcc -E -P`), not from reading the `#if` ladder.
- The `pio test` and `millis()` survey facts (F7-12, missing-category 1) were produced by a
  parallel investigation and then spot-checked directly: I re-read
  `partitions-4MB-safeboot.csv`, `variants/heltec_wifi_lora_32_V3/platformio.ini:1-8`,
  `src/main.cpp:22,52`, `test/`, `src/esp32/esp32_main.cpp:3168-3182` and `:579-594`,
  `src/gps_functions.cpp:61,368-371`, `docs/ram-comparison-20260517.md:156`, and the full
  `monitor_filters` grep. All confirmed; the one correction I made was the V-01 variant count
  (11, not 12).

**Not verified, flagged as open risk:** whether the corrected `[env:heltec_v3-hwtest]` in
F7-12 actually uploads and returns Unity results — that needs a board in hand. And the
Layer-1 closure measurement (F7-5) covers only the two files in scope; `loop_functions.cpp`
(06 target #2) and `lora_functions.cpp` (06 target #3) were not measured and may well justify
the seam extraction 06 proposes.
