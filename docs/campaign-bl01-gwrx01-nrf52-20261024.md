# Campaign BL-01 + GWRX-01 nRF52 (2026-10-24)

Status: **W1 gate** (I1-I3 landed; advisor rework on the bench driver running; two rows in section 1 marked _new_ await the operator's confirmation).
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

The two _new_ rows were found by the W1 advisor (2026-10-24): own traffic that `--mesh` changes. Pinned as they are
(bMESH read allowlist); operator confirmation pending.

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
`addTxRingEntryCore()`'s `taskENTER_CRITICAL` (`txring_functions.cpp:672-847`). Open: the hardware run.

Bench setup: DK5EN-90 (RAK4631, Ethernet) gateway under test, INSTRUMENT build, `--srvip` to the mock;
DK5EN-92 second gateway; dependent node with `--nbrreport on`; 2 dBm, group 9999. Mock delays DK5EN-90's copy
(`--delay-client`) so it hears the partner first. Runs: count mode, then `--nbrrelay on` (operator 2026-10-24).
Pass: 10/10 server slots with a NEED line, at least one `CANCEL?` (count) / released slot without TX (on), no
stuck ring slot, no reboot, no heap drop. Restore all settings and normal images afterwards.

Residual risk (advisor item 6): `--srvip` is RAM only, `--gateway on` is persisted. A reboot during the run brings a node
up as a gateway on the real server. The driver watches for boot markers and sends `--gateway off` at once; the
operator watches as well.

## 3. Waves

| Wave | Content                                                                             | Status  |
| ---- | ----------------------------------------------------------------------------------- | ------- |
| W0   | RF-path inventory, mock recon, nRF52 concurrency check                              | done    |
| W1   | I1 bench driver + serial transport; I3 mock `--delay-client`; I2 BL-01 seam + tests | running |
| W2   | nRF52 bench (count, on) + BL-01 hardware check                                      | open    |
| W3   | Docs (BACKLOG, RESUME, 08 catalogue, `--mesh` help), commit                         | open    |

File ownership W1: I1 `tools/bench/gwrx_bench.py`, `tools/bench/gwrx_frame.py`, `tools/tests/test_gwrx_bench.py`;
I3 `tools/mock/meshcom_server.py`, `tools/mock/test_mock_server.py`, `tools/mock/README.md`; I2 `src/mesh_policy.h`,
the sites in section 1, `test/test_checkvia/test_checkvia.cpp`; orchestrator `platformio.ini`, docs, git.

Approvals (operator 2026-10-24): flash all bench nodes, DK5EN-14 over OTA; mock delay. Out of scope: DK5EN-98.
