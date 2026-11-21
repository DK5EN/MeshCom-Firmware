# Campaign AppLink (2026-11-21): resume point

Merged into `fork-dev` on 2026-11-21 (merge wave below); the worktree `~/WebDev/MeshCom-Firmware-applink` and
branch `feat/applink` (from `02097c59`) are removed. Contract: `docs/applink-api-contract.md`. Plan:
`docs/applink-plan-firmware.md`.

## Standing operator instructions

- **No `pio` at all** (another session builds in the main checkout). Host checks only with clang:
  `<scratchpad>/hosttest.sh <test_dir> <src.cpp...>` (Unity copied to the scratchpad, ASan+UBSan).
  Board code (ESP32, nRF52) stays uncompiled until the operator's build is done.
- **No bench run** in this campaign.
- **No commits until the full gate** (`tools/regression.sh --stage 1,2` plus every board env) has
  run. Work stays uncommitted in this worktree.
- Nothing posted on upstream #1276. Decisions: name AppLink, newest login wins, key `--passwd`.

## Waves

| Wave | Content                                                                                     | State                                                                                                                                                                       |
| ---- | ------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| W0   | audit: phone-session sites, settings bit, signatures                                        | done                                                                                                                                                                        |
| W1   | host cores: ws/sha1 (I1), auth+phone_link+harness (I2)                                      | done after advisor rework (Host/key/426, close codes, lockout prune, test gaps): ws 31, auth 18, link 6, harness 28                                                         |
| W2   | integration: phone path (I3), commands + web GUI (I4)                                       | done after 2 advisor rounds: setting moved to node_sset2 0x0010 + marker 0x2000; nRF52 BLE down edge guarded (phoneLinkBleDownEndsSession); help/handlers under HAS_APPLINK |
| W3   | engine `applink_conn` (I5), ESP32 glue (I6), nRF52 glue (I7), Python client (I8)            | done after 2 advisor rounds; last item (stale 8002 chip socket when AppLink is switched off during an IP outage) fixed by the orchestrator, unbuilt; conn 24, client 12     |
| W4   | docs: test-suite-map, Web-API doc, wire-format link, BACKLOG APPLINK-01, RESUME, plan state | done                                                                                                                                                                        |
| Gate | pio regression + all board builds, merge into fork-dev                                      | done 2026-11-21, see "Merge wave" below                                                                                                                                     |

## W0 results (design fixed by them)

- `isPhoneReady` stays "phone session ready" for BLE and LAN: ~40 PHONE sites route replies via
  `commandAction(iphone==1)` into the shared phone rings, which the drain sends to the owner's sink.
- Guard only the BLE resets: `bleSessionReset()` (ESP32 callbacks, `esp32_main.cpp:363`) and
  `nrf52_ble.cpp:220/247/268` must leave `isPhoneReady`, `config_to_phone_prepare`, `conffin_sent`,
  `bAckInfo` alone while `PL_NET` owns. The owner is changed only in the loop.
- AU/OTA "phone held" guards read `isPhoneReady`: a LAN session also holds them. Accepted.
- Inbound LAN frames: the glue runs in the loop and calls `readPhoneCommand(frame, PL_NET)`
  directly; the BLE queue is not used.
- Setting: `node_sset4` bit `0x0100` (free), runtime `bAPPLINK`. nRF52 layout copies sset4 whole.
- RAK13800: `EthernetServer::available()`/`accept()` re-listen (`EthernetServer.cpp:74,109`). The
  nRF52 glue manages its one socket with `socketBegin/socketListen/socketStatus` directly.
- nRF52 nonce: `sd_rand_application_vector_get` (see `seedRandomFromHw()`, `nrf52_main.cpp:546`).
- Web GUI on/off rows (`web_setup.cpp`) go through `commandAction`, so the console refusals
  (no password, RAK ext-UDP exclusion) apply to the web GUI unchanged.

## Orchestrator-owned files

`platformio.ini` (native envs), `src/applink.h` (platform API), this doc.

## Known residual risks

- Stale settings bits (ACCEPTED by the operator 2026-11-21): a node that left both `--gpsdebug on`
  (sset2 0x0010, until 2026-04) and `--checkmesh on` (0x2000, released v4.35p May-June 2026) boots
  with AppLink on if `--passwd` is set. Boot prints `[APPLINK] on (port 8002)`; pinned in
  `test_config_json`.

- ESP32: a NimBLE onDisconnect reading the owner while the loop switches it to PL_NET can reset
  `isPhoneReady` (window of a few instructions, same class as the old BLE race). LAN client then
  needs a reconnect.
- `hw` in the challenge: the board name from `nbrHardwareName()` (contract), the number only where the table has
  none (operator 2026-11-21). T-Deck Plus builds send `TDECK+` (short-name table on T-Deck builds), not `TDECK_PLUS`.
- RAK: boot clears `bAPPLINK` when ext-UDP is on but keeps the saved bit; after `--extudp off` AppLink stays off
  until `--applink on` or a reboot.

## Merge wave (2026-11-21)

- AppLink applied onto `fork-dev` (`018e9cd0`, after ETH-06) without a branch commit; only `docs/RESUME.md` conflicted.
- First compile: one fix, `applink_esp32.cpp` included `loop_functions_extern.h` before `loop_functions.h`.
- Operator decisions applied: `hw` = board name, number as fallback (`alHwLabel()`, `test_applink_auth`); `--applink`
  on T114/T-Echo answers "AppLink not available on this board" (help line unguarded, help parity clean).
- ETH-06 x AppLink: `initETH_HW()` calls `applinkChipReset()` before the W5100S reset (the reset and DHCP run inside
  one loop pass, so `applinkLoop()` never saw "no IP" and kept a socket index the chip may have handed to the gateway).
- Advisor (Opus) REWORK, verified and fixed: a CLOSED session slot can be taken by `Ethernet.socketBegin()` (web
  server, gateway UDP, DHCP, NTP), which zeroes `EthernetServer::server_port[s]`; AppLink then closed the other
  user's socket. `alOwnsSock()` (RAM claim `== AL_PORT`) now gates `alCloseSocket()`, `alIoWrite()`, `applinkLoop()`
  and `applinkChipReset()`. Pins: `tools/tests/test_w5100s_patch.py` (3 AppLink tests, all fail before).
- Static cost (pio size line; nRF52 RAM is the static figure): RAK4631 RAM +3.9 kB, flash +11.7 kB; Heltec V3
  RAM +5.4 kB, flash +12.3 kB; T-ETH-Elite RAM +5.4 kB, flash +11.6 kB; T114 and T-Echo +8 B RAM, +0.6 kB flash.
  Runtime heap of the ESP32 session not measured.
- RAK sockets (by design, not measured): gateway UDP, web server, DHCP/NTP while active, and ext-UDP **or** AppLink.
- `esp32-external-radio` does not build locally (`#error` needs `EXTERNAL_RADIO_HOST/PORT` from a build overlay);
  untouched by this wave.

## Next step

- Bench: AppLink on a Heltec V3 and on a RAK4631+RAK13800 with `tools/bench/applink_client.py`, BLE capture diff,
  socket census on the RAK, a forced W5100S reset with a session up.
- App side with the app maintainer (contract `hw` field now a name).

## Open after the gate

- RAM/flash cost per board, socket census on the RAK, bench verification (contract and plan W6).
