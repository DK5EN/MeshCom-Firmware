# Campaign: AU-18 heap gate, UDP-02 dedup slot, RX-DEAF-01 latch

Status: **W1-W3 done 2026-10-23** (AU-18 `9b43b4f0`, UDP-04 `6e6e7e40` on fork-dev, not released). Open: RX-DEAF-01
forced-latch test (W4, operator decision). Resume point for this campaign. Section 6 is the progress log.

## 1. Goal

| Item       | Goal                                                                                     | Kind               |
| ---------- | ---------------------------------------------------------------------------------------- | ------------------ |
| AU-18      | Pre-TLS gate refuses a heap that cannot hold both 16717 B mbedTLS record buffers         | fix, fork-dev      |
| UDP-02     | A duplicate server position (0x21) no longer takes a second dedup slot or counts `newid` | fix, fork-dev      |
| RX-DEAF-01 | Measure whether one TX-gate IRQ-poll hit latches `is_receiving` and leaves the node deaf | bench, then decide |
| LDBG-01    | Operator decision only (section 5)                                                       | decision           |

## 2. Findings (code, 2026-10-23, HEAD `722dfee2`)

- **AU-18:** `fwNetHeapGate()` (`src/fw_update.h:775-783`) tests total free and the single largest block against
  `FWNET_MIN_BLOCK` 17000 (`src/esp32/fw_update_net.cpp:91`); `heapGateEval()` (`fw_update_net.cpp` ~602-619) feeds
  it. mbedTLS takes two 16717 B record buffers; bench DK5EN-92 passed the gate with one 32756 B block and failed on the
  second buffer (`-32512`). Fix route: a probe allocation of two 16717 B internal blocks on the classic path (freed at
  once), pass only if both succeed; the free-heap floor stays. The PSRAM branch keeps its rule (PSRAM is large).
- **UDP-02:** `src/esp32/udp_frame_esp32.cpp:273-279`, branch `msg_type_b == 0x21`, calls `addLoraRxBuffer()` and
  `stat_newid.fetch_add(1)` without `bUdpMsgIsNew` (set at :220). The nRF52 twin has no early insert. Fix: gate the two
  lines on `bUdpMsgIsNew`; display and phone copy stay ungated (order pinned by `test_udp_frame_twin`). ID clash: the
  older `7b65233a` also says UDP-02 (255-byte datagram, fixed).
- **RX-DEAF-01:** the TX gate (`src/esp32/esp32_main.cpp` ~3824-3837) sets `is_receiving = true` on a HEADER_VALID /
  PREAMBLE_DETECTED poll hit. `checkRXRead()` (:5296) returns while it is set; only a successful read, `OnRxDone()` and
  the park drain clear it; the RX-timeout reset (:3534-3625) does not. Static reading: one hit -> no further reads until
  reboot. Upstream since `13113d1c` (Mar 2026), no field report, so the bench decides.

## 3. Waves

| Wave | Content                                                                                                                                                                                                                                                                      | Who                          | Model / effort | Est. cost / time |
| ---- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------- | -------------- | ---------------- |
| W1a  | AU-18 probe in `heapGateEval()`, gate function takes the probe result, host tests in `test_fw_update` (fail before, pass after)                                                                                                                                              | implementer A                | Sonnet medium  | ~$2, 25 min      |
| W1b  | UDP-02 gate on `bUdpMsgIsNew`, regression case in `test_udp_frame_twin` (duplicate 0x21: no slot, no `newid`)                                                                                                                                                                | implementer B                | Sonnet medium  | ~$1.5, 20 min    |
| W1c  | RX-DEAF-01 bench, parallel to W1a/b (no pio, no files): DUT DK5EN-92 (current INSTRUMENT image, `--loradebug on`), DK5EN-1 sender; both `--loratx 20 <ms>` to `TEST` at 2 dBm, airgap off. Count `[MC-DBG] IRQ_POLL ... TX_ABORT` hits, check frames received after each hit | orchestrator                 | Opus medium    | ~$3, 45-60 min   |
| W2   | Gate `tools/regression.sh --stage 1,2`, release builds of the 7 targets (symbol/string check of the probe), advisor on the W1a+W1b diff, commit per item                                                                                                                     | orchestrator + advisor       | Opus medium    | ~$4, 40 min      |
| W3   | AU-18 on hardware: DK5EN-92 release build, 3 real update checks still pass, probe line in the log. The failing path cannot be forced on the bench; it is proven by the host test                                                                                             | orchestrator                 | Opus medium    | ~$2, 30 min      |
| W4   | RX-DEAF-01 only if W1c shows a latch: fix (clear the flag once the TX abort is handled, or in the RX-timeout reset), twin test, bench before/after. Planned after W1c                                                                                                        | implementer (after decision) | Sonnet medium  | -                |

## 4. File ownership

| Wave | Exclusive files                                                                                                                                         | Shared resource                                                             |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------- |
| W1a  | `src/fw_update.h` (gate functions ~760-785 only), `src/esp32/fw_update_net.cpp` (`heapGateEval` and its constants), `test/test_fw_update/test_main.cpp` | pio via the scratchpad lock wrapper: `native_fw_update`, `ttgo_tbeam`       |
| W1b  | `src/esp32/udp_frame_esp32.cpp` (0x21 branch only), `test/test_udp_frame_twin/test_udp_frame_twin.cpp`                                                  | pio via the lock wrapper: `native_udp_frame_twin`, `heltec_wifi_lora_32_V3` |
| W1c  | logs `tools/bench/runs/harness/rxdeaf-*`                                                                                                                | DK5EN-92, DK5EN-1, their 2323 consoles (no pio)                             |
| W2   | `docs/BACKLOG.md`, `docs/RESUME.md`, `docs/architecture/08-defect-catalogue.md`, this doc                                                               | all envs (gate)                                                             |

## 5. Open questions

1. LDBG-01: (a) `--loradebug on` sets only `bLORADEBUG` (changes upstream behaviour), (b) keep the coupling and
   restore `bDisplayRetx` at boot, (c) leave it and note in the bench docs that loradebug output costs print time.
   Recommended: (c).
2. W1c transmits: `--loratx` bursts to group `TEST` at 2 dBm from DK5EN-92 and DK5EN-1, airgap off. DK5EN-98 hears
   them and relays as in the RX-stall bench. OK?
3. AU-18: probe allocation (recommended) or the stricter one-block rule (block >= 2 x 16717)?
4. UDP-02: rename to UDP-04 to end the ID clash?
5. Upstream: UDP-02 and RX-DEAF-01 are upstream code. Fix on fork-dev first, PR later (usual model)?

## 6. Progress log

- 2026-10-23: plan written.
- 2026-10-23: operator answers: LDBG-01 option (c) (closed, note in `docs/loradebug-serial-output.md`); W1c transmit
  approved; AU-18 two-block allocation test; UDP-02 renamed UDP-04; fork-dev only, a larger PR later. W1a, W1b, W1c
  dispatched.
- W1b UDP-04: 0x21 early insert and `stat_newid` gated on `bUdpMsgIsNew`; behavioural twin test fails before (2 ring
  entries), passes after. Advisor APPROVED. Commit `6e6e7e40`.
- W1a AU-18: `twoRecBufProbe()` in `heapGateEval()`, `fwNetHeapGate(..., twoBufOk)`, `FWNET_TLS_REC_BUF 16717u`,
  refuse line `;twobuf;<0|1|2>`; host test fails before (old API), 111/111 after. Advisor rework (verified, applied by
  the orchestrator, about 15 lines): hold a job block `FWNET_BLK + 2048` during the probe; save/restore the alloc-fail
  counters around it. Advisor re-review APPROVED (residual: client context and CA chain not modelled; a miss still
  fails cleanly with `-32512`). Commit `9b43b4f0`.
- W1c RX-DEAF-01 bench (DUT DK5EN-92 INSTRUMENT `--loradebug on`, sender DK5EN-1, 6 rounds of mutual
  `--loratx 20 1000/1100` to `TEST` at 2 dBm, then a sender-only tail): DUT 54 TX, 69 TX-gate entries, 87 CAD scans,
  **0 `IRQ_POLL ... TX_ABORT` hits**; about 75 sender frames received (direct and via DK5EN-98), RX alive to the end
  (`tools/bench/runs/harness/rxdeaf-dut.log`). The CAD before the gate catches a busy channel, so the poll only fires
  when a header starts between CAD and gate: too rare to trigger on purpose. Latch not shown, not excluded.
- W2: gate `tools/regression.sh --stage 1,2` PASS (2755/2755 Unity) on W1a+W1b; after the AU-18 rework (ESP-only
  file) `native_fw_update` 111/111 and release builds of the 7 targets (ESP32 images carry `;twobuf;`).
- W3 AU-18 hardware: DK5EN-92 release build, 3 x `--update check`: gate passed (pause-time largest block 51188 / 36852
  / 47092 B), tag found, `alloc;end;fails;0`, BLE resumed each time (`w3-au18-checks.log`).
- Next (W4, needs operator OK): forced-latch test for RX-DEAF-01: an INSTRUMENT-only command sets `is_receiving = true`
  once (what a poll hit does), then the bench checks whether the DUT still receives. About 10 lines of bench code plus a
  10-minute run.
