# Campaign: release gate 2026-11-06

Resume point. Everything still needed before the next release. Predecessor: `docs/campaign-bench-fixes-20261106.md`.

## Operator decisions (2026-11-06)

- T-Beam heap comes first: "Check now" refuses there with "Not enough free memory".
- `AU_AUTO_INSTALL` stays 0 (D14). The TLS gate stays at 76000 B.
- The T-Deck header no longer showing "phone connected" and "WiFi linked" is accepted.
- Push of `fork-dev` and a `ci-proof-*` draft build are approved for W4.
- Soak: 1 h on CI images, heap compared with `v4.40a.11.01`.
- `CHARS-02` closed as accepted, no Latin-2 font.
- `AU-22`: `ck=` may read -1 when a check ran before the node clock was set.
- `TD-34`: one brief to find the `--disptest` cause; otherwise it stays in the backlog, out of this release (INSTRUMENT only).
- Bench setup changed: DK5EN-14 is on the bench for the Safeboot update; DK5EN-1 is reachable over WiFi only.

## Starting evidence for the heap

DK5EN-92 with BLE paused: 81.5 KB and pass without a browser (00:54); 71.7-74.5 KB, then 67.1-68.6 KB and `lowheap_ble_off`
during the GUI pass (`tools/bench/runs/harness/gui-20261106/t-beam-92.log`). The GUI log has INSTRUMENT output, so it is not a
clean figure. Idle heap: 11.01 flat at 68.7 KB (300 s, 600 s); later builds fell 66.1 to 58.9 KB, and 59.8 to 51.2 KB in the GUI
pass. Reproduce on a non-INSTRUMENT image first.

## Waves

| Wave | Work                                                                                    | Who                      | Status |
| ---- | --------------------------------------------------------------------------------------- | ------------------------ | ------ |
| W0   | Heap root cause on DK5EN-92: idle curves 11.01 against HEAD, with and without a browser | orchestrator, bench      | done   |
| W1   | Heap fix: idle flat within 1 KB over 30 min, 10 of 10 "Check now" with a browser open   | orchestrator, advisor    | done   |
| W2   | AU-22, EXT-PEER-01, WEB-PIN-01, TD-34, each with a regression test                      | 3 implementers, advisor  | done   |
| W3   | RX-TX-02: sender frame counts against the tool count, to TEST at 2 dBm or less          | orchestrator, bench      | done   |
| W4   | Push, `ci-proof-*` draft build, CI images on the four bench nodes                       | orchestrator             | done   |
| W5   | Chrome GUI pass (RAK, T-Deck, T-Beam, Setup grid), Safeboot `/ota`, manipulated         | orchestrator, serialized | open   |
|      | `.autoupdate`, slow client, EXTUDP, RM round trip, Ethernet                             |                          |        |
| W6   | 1 h soak on CI images, heap compared with 11.01                                         | orchestrator, background | open   |

Operator: BLE and RAK secrets with a real phone (checklist from W5); slow client dummynet needs sudo; the RM round trip may
need the RM password file.

## Next

W5, serialized on the bench. All four nodes run the CI images of `44075ef3`: DK5EN-92 (`.71`), DK5EN-14 (`.68`), DK5EN-1 (`.73`, WiFi only) and DK5EN-90
(`.66`). Addresses are DHCP: confirm with `identity_guard.py` first. The draft is deleted, so the CI images and `.autoupdate` files are only in
`ci-images/` in the session scratchpad; if that is gone, re-run a `ci-proof-*` build for a soak on the same images. The W2 probe for the W5 EXTUDP run is
`w2_probe_after.py` in the same scratchpad: register query replies as the probe, `--serial` for the RAK. W5 also runs a peer change during a WiFi drop on
ESP32.

W5 tools: GUI by Chrome, logs to `tools/bench/runs/harness/gui-20261106/`. Safeboot `tools/webflash.py --bin`. Manipulated `.autoupdate`: no bench tool
exists, upload a corrupted copy through `/ota` by hand or script. Slow client `tools/bench/web_throttle.sh` (sudo, operator) with `web_stall_client.py`,
recipe `docs/campaign-web-wdt-20261019.md:50`. EXTUDP `docs/bench-extudp-regression.md`. RM `tools/remote_cmd.py`. Ethernet `tools/bench/eth03_probe.py`, no
recipe on file. W6 uses `tools/bench/heap_idle.py`; the 11.01 figure (68.7 KB) exists only in this doc, no raw log.

## W2 file ownership

| Agent | Item                    | Exclusive files                                                                                                                  |
| ----- | ----------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| A     | AU-22                   | `src/esp32/fw_update_net.*`, `src/esp32/rm_au_esp32.cpp`, `src/fw_update.h`, `test/test_fw_update`                               |
| B     | EXT-PEER-01, WEB-PIN-01 | `src/extudp_functions.*`, `src/web_functions/web_setup.cpp`, `web/mc.js`, `tools/webgui_pins_test.js`, `test/test_extudp_target` |
| C     | TD-34                   | `src/t-deck/*`, `src/esp32/esp32_main.cpp` (`--ble` path, `--disptest` if needed)                                                |
| Orch  | Hotspots                | `src/command_functions.cpp`, `platformio.ini`, `tools/regression.sh`, `docs/BACKLOG.md`, `RESUME.md`                             |

## Wave log

- W0 (2026-11-06, DK5EN-92, release images, logs `w0-*`): HEAD idle flat at 64.1 KB over 30 min, no leak (`w0-idle-head.json`). Against
  11.01 (68.7 KB) that is -4.6 KB, above the 3 KB tolerance: 2.5 KB static (`s_udpHoldStore` 1 KB, `wsParams` 980 B, register query
  264 B), the rest dynamic. Three causes for the refusal, see `AU-23`. The `TIME_WAIT` cap is about 16 sockets. The gate reading right
  after the BLE stop is up to 10 KB low until the idle task has freed deleted stacks.
- W1 (2026-11-06): `AU-23` fixed. Fail-before: Chrome tour on unfixed HEAD refused at 73.6 KB; released 11.01 refused 1 of 10 in
  `au_gate_burst.py` (split heap). Pass-after: 20 of 20, 12 by restart, 0 reboots, 0 allocation failures; GUI tour plus Download staged
  and verified 1.27 MB in 14 s. Rejected: a wait or a BLE cycle on the same stack (no change, 15 of 20); `constexpr` for `wsParams`
  (does not compile, lambda conversion). Open: idle -4.6 KB against 11.01 goes to W6 for the baseline decision. A
  `webflash.py` reboot into Safeboot applies a staged image first and the upload then fails: clear or apply a stage before flashing.
- W2 (2026-11-06, logs `w2-20261106/`): `AU-22`, `EXT-PEER-01`, `WEB-PIN-01` fixed, `TD-34` `--ble off` icon fixed; `--disptest` stays in the
  backlog. Fail-before on unfixed DK5EN-92: the reply stayed at the old peer and missed the new one (`probe-before-92.json`), `mcpout` on
  an input pin answered `returncode` 0 (`mcpout-before-92.json`). Pass-after on DK5EN-92 and DK5EN-90, console and web
  (`probe-after-*.json`): peer moves, `none` then valid, refused target, RAK hostname; `mcpout` 422; RAK with Ethernet uninitialised stayed
  alive (`n20-rak.summary`). Icon checked by eye. Gate: Unity 101 envs, 3602 cases; stage 2 pass; 6 builds; RAK flash 82.4 %. Two advisor
  rework rounds; the small second list was applied by the orchestrator without a third pass, its scans mutation-tested. Accepted: a RAK
  hostname peer no longer serves inbound traffic (release note). A refused target blanks the peer in RAM only; the old peer returns after
  a reboot unless a later save persists the blank. Not run: WiFi down during a peer change on ESP32, `ck=` on a node.
- W2 addendum: `--ble on` turns the icon yellow again, seen by the operator.
- W3 (2026-11-06, `rx_stall_A_20261106-174753.json`, `-175117.json`): `RX-TX-02` settled, no firmware loss. 20 frames at 8 s: 20 of 20 at both receivers.
  60 at 3 s with a 130 s settle: 59 queued, 59 on air, 59 at the T-Deck, 56 at the T-Beam. The T-Beam made only 4 TX lines in that run and two of the 3
  misses sit next to relayed copies, so channel load is the likely cause, not proven. Replay of the 11-06 log: requested 60, ring refused 20, on air 22,
  still queued 18. Tool only, no firmware change, so no advisor pass. Stage 2 pass, 21 tool tests, fail-before 4. The drain wait itself ran only in the
  host tests. Burst frames reach the real mesh through relays (DK5EN-98, DL2JA-2): group TEST, but it loads the channel.
- W4 (2026-11-06): `fork-dev` pushed at `44075ef3`. `ci-proof-20261106-44075ef3` run 42258106988 green, 68 assets; draft and tag deleted afterwards. All four
  nodes run the CI images, build strings seen on the node and found in the image: DK5EN-92 17:08:29, DK5EN-14 17:34:44, DK5EN-1 17:19:23, DK5EN-90 17:10:31
  (UTC). Identity guard passed on all four, 2 dBm. The RAK took the CI `wiscore_rak4631.zip` by serial DFU with the 1200 touch ("Device programmed.")
  instead of the UF2, which needs a hand on the reset button. `--update forget` answered `[AU];forget;ok` only on DK5EN-1; DK5EN-92 and DK5EN-14 sent no
  `[AU]` line over 2323 within 3 s, so a stage on them is not proven cleared, though both OTA flashes verified. No code change, so no gate run and no advisor pass.
