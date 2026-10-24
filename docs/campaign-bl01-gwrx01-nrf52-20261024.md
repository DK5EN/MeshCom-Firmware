# Campaign BL-01 + GWRX-01 nRF52 (2026-10-24)

Status: **W2 done** (nRF52 bench passed, BL-01 hardware check passed, all nodes restored; section 4). Next: W3 docs.
Run with `/orchestrate-waves`. Resume point for this campaign.

## 1. BL-01: `--mesh off` semantics (operator decision 2026-10-24)

Decision: document and pin with tests, no behaviour change. `--mesh` means packet repeating, not protocol
participation. A mesh-off node repeats no frame of another node, and stays a full participant with its own frames.

| Traffic                                                                  | `--mesh off` | Site (by label; lines after the BL-01 include)       |
| ------------------------------------------------------------------------ | ------------ | ---------------------------------------------------- |
| Relay of foreign POS, text, HEY (`rx_relay`)                             | stopped      | `checkMesh()` (only call), `rx_relay`                |
| Relay of foreign ACK (`rx_ack_fwd`)                                      | stopped      | `handleACK()` `rx_ack_fwd` (`meshTxAllowed`)         |
| Own HEY, own ACK (`SendAckMessage`), own messages, ping/pong             | sent         | `loop_functions.cpp` 5542, 3775, 3879, 4278          |
| Gateway ACK (`rx_dm_ack_gw`, `rx_dm_ack_new`)                            | sent         | `OnRxDone` `if(bGATEWAY)` block                      |
| Gateway upload of foreign HEY with own signal report                     | sent         | "HEY always send to Server" block                    |
| Gateway upload of own HEY echo                                           | sent         | `heyEchoShouldUpload()`                              |
| Gateway server downlink (`udp_rx`, gated by `bUDPtoLoraSend`)            | sent         | `udp_frame_esp32.cpp:584`, `udp_frame_nrf52.cpp:569` |
| Store node delivery (`mbox`) and custody notice (`sto`)                  | sent         | `glueDeliver()`, `glueNotify()`                      |
| _new_ Own frames: header bit 0x10 ("mesh on") set                        | not set      | `aprs_functions.cpp` encode, `if(bMESH)` 0x10        |
| _new_ NBR report, `--nbrreport auto`: sent when mesh off and gateway off | sent (more)  | `loop_functions.cpp` `bSend = (!bMESH && !bGATEWAY)` |

The two _new_ rows were found by the W1 advisor (2026-10-24). Operator 2026-10-24: header bit 0x10 stays as it is;
`--nbrreport` pinned to `auto`. Production builds already enforce that (`src/prod_lock.h` LOCKED list and
`PRODLOCK_SSET4_CLEAR_NBR`, `docs/production-locked-settings.md`); only INSTRUMENT builds can still change it. Open:
whether INSTRUMENT builds lose the switch too (the switch also exists on upstream/dev).

Rationale (operator): an ACK is not repeated because the forward frame was not repeated either; a foreign HEY is
not repeated because the node is no valid RF path; with gateway on, the node answers every heard HEY through the
server upload with its own signal report.

Test seam: no native env compiles `lora_functions.cpp` or `msgstore_glue.cpp`, the sites are `static`. Plan: a
behaviour-neutral `src/mesh_policy.h` with one predicate per row, called from the sites, pinned in
`test/test_checkvia` (`native_parsers`), plus a call-site check. Hardware check in W2: DK5EN-1 gateway with
`--mesh off` still sends `rx_dm_ack_gw` for a TEST group message and relays no foreign POS.

## 2. GWRX-01 on the nRF52 gateway path

Fix `257e5e2b` + `0281dce4` (both platforms). W0 code check (2026-10-24): on nRF52 the loop-task enqueue is
safe against the LORA-task `OnRxDone` scans: `nbrServerNeed()` reads the matrix under `NBR_LOCK()`
(`vTaskSuspendAll`, `nbr_matrix.cpp:43`, :2940), the slot with kind/need/alone is written inside
`addTxRingEntryCore()`'s `taskENTER_CRITICAL` (`txring_functions.cpp:672-847`). Hardware run passed in W2 (section 4).

Bench setup: DK5EN-90 (RAK4631, Ethernet) gateway under test, INSTRUMENT build, `--srvip` to the mock;
DK5EN-92 second gateway; dependent node with `--nbrreport on`; 2 dBm, group 9999. Mock delays DK5EN-90's copy
(`--delay-client`) so it hears the partner first. Runs: count mode, then `--nbrrelay on` (operator 2026-10-24).
Pass: 10/10 server slots with a NEED line, at least one `CANCEL?` (count) / released slot without TX (on), no
stuck ring slot, no reboot, no heap drop. Restore all settings and normal images afterwards.

Residual risk (advisor item 6): `--srvip` is RAM only, `--gateway on` is persisted. A reboot during the run brings a node
up as a gateway on the real server. The driver watches for boot markers and sends `--gateway off` at once; the
operator watches as well.

## 3. Waves

| Wave | Content                                                                             | Status |
| ---- | ----------------------------------------------------------------------------------- | ------ |
| W0   | RF-path inventory, mock recon, nRF52 concurrency check                              | done   |
| W1   | I1 bench driver + serial transport; I3 mock `--delay-client`; I2 BL-01 seam + tests | done   |
| W2   | nRF52 bench (count, on) + BL-01 hardware check                                      | done   |
| W3   | Docs (BACKLOG, RESUME, 08 catalogue, `--mesh` help), commit                         | open   |

File ownership W1: I1 `tools/bench/gwrx_bench.py`, `tools/bench/gwrx_frame.py`, `tools/tests/test_gwrx_bench.py`;
I3 `tools/mock/meshcom_server.py`, `tools/mock/test_mock_server.py`, `tools/mock/README.md`; I2 `src/mesh_policy.h`,
the sites in section 1, `test/test_checkvia/test_checkvia.cpp`; orchestrator `platformio.ini`, docs, git.

Approvals (operator 2026-10-24): flash all bench nodes, DK5EN-14 over OTA; mock delay. Out of scope: DK5EN-98.

## 4. W2 result (2026-10-24)

Evidence: `tools/bench/runs/harness/gwrx01-w2-20261024.summary`. All images built from `a7b21de5`.

- **GWRX-01 nRF52: passed.** DK5EN-90 (RAK4631, Ethernet) and DK5EN-92 as gateways on the mock, DK5EN-1 as
  dependent. Count mode: NEED case B on every DK5EN-90 server slot (6/6), one `CANCEL?`. `--nbrrelay on`:
  NEED B 7/7, 5 slots released by `CANCEL` without TX, 2 transmitted (partner copy came after own TX). No
  reboot, no ring drop, heap flat (92808 to 92796 B).
- **BL-01 hardware: passed.** DK5EN-1 (gateway on, `--mesh off`, normal image) sent `rx_dm_ack_gw` for a
  group-9999 text from DK5EN-92 and repeated none of its text, position or HEY (`RLY q=nomesh`); all three
  were uploaded, the HEY with its own signal report.
- **Bench recipe, learned:** the dependent needs an INSTRUMENT image, since production builds pin
  `--nbrreport auto`. `--delay-client` must stay below the partner's dwell (about 5.9 s on DK5EN-92): at 10 s
  DK5EN-90 heard the RF copy first and dropped its server copy as a duplicate (0 slots); 3 s gave 6 to 7
  slots out of 10.
- **Side observation (upstream, not BL-01):** `rx_dm_ack_gw` and `rx_dm_ack_new` carried the same msg id
  (`millis()` read twice in one millisecond). Candidate BACKLOG row in W3.
- Restore: all three nodes on normal `a7b21de5` images, original settings, verified by `--info`.
