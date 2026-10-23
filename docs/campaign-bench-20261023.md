# Campaign: bench session 2026-10-23 (hottest-first items + RX stall #1229)

Status: **running**. Resume point for this bench session; the RX stall work has its own plan in
[campaign-rx-stall-1229.md](campaign-rx-stall-1229.md). Section 5 is the progress log.

## 1. Goal

Run every hottest-first item that a measurement can move, on one bench session, after release
`v4.40a.10.22`:

| Item         | Question the bench answers                                                                                  | Nodes                             |
| ------------ | ----------------------------------------------------------------------------------------------------------- | --------------------------------- |
| Release gate | Idle heap of the release image within 3072 B of the 69332 B baseline                                        | DK5EN-92                          |
| AU-17        | Does the T-Beam v1.2 still miss the 76000 B TLS heap gate on 10.22 (the 10-18/19 heap levers gave 86 KB)?   | DK5EN-92                          |
| AU-14        | Does a classic-ESP32 AU download still trip `TASK_WDT` in `lora_rx` with LoRa traffic arriving?             | DK5EN-92 (+ DK5EN-1 as RF source) |
| WEB-RX-01    | Does a stalled web client lose LoRa frames (#1229)? W1 of the RX stall plan                                 | DK5EN-1 DUT, DK5EN-92, DK5EN-14   |
| #1235        | The PR build on hardware: LED heartbeat watched, `[HEAP]` boot marks, NVS line                              | DK5EN-1, DK5EN-90, DK5EN-92       |
| RM-NVS-01    | Fill NVS: do the `--info` NVS line and the RM "remember password" failure match the E22 field report?       | DK5EN-1                           |
| BL-01        | `--mesh off` on a gateway: count what still goes out on the gateway DM-ACK and DM-custody paths (data only) | DK5EN-1 gateway, DK5EN-90 / -92   |

Decision-only items (no bench run in this session): GWRX-01, BL-03 / GPS-05b, BL-04 / REBOOT-98 (watch),
gw_servers FR/US rows.

## 2. Nodes and access

- On USB: DK5EN-1 (Heltec V3, CP2102 `0001`), DK5EN-92 (T-Beam v1.2, CH9102 `573C000584`), DK5EN-90
  (RAK4631 `230D6EBB3266D20E`). DK5EN-14 (T-Deck Plus) on WiFi only.
- Flash OK (operator 2026-10-23): DK5EN-1, DK5EN-92, DK5EN-90 over USB; DK5EN-14 over the air only (auto mode
  blocks OTA for the agent: the operator runs `tools/webflash.py`). DK5EN-98 untouched.
- End state: every bench node back on the `v4.40a.10.22` release image.

## 3. Order

1. Release `v4.40a.10.22` (heap gate on DK5EN-92, tag, CI, flasher).
2. AU-17 / AU-14 on DK5EN-92 against the published 10.22 (dev channel), DK5EN-1 sending to group `TEST`.
3. RX stall W1a (harness, agent) in parallel with RM-NVS-01 and BL-01 on DK5EN-1 / DK5EN-90.
4. RX stall W1b (bench, operator sudo for `web_throttle.sh`), then go/no-go.
5. #1235 build on DK5EN-1 / -90 / -92.
6. Restore all nodes to 10.22.

## 4. Rules

Bench traffic only to group `TEST` or a DM to an own node; never broadcast; never a foreign callsign; TX at most
2 dBm; `tools/bench/identity_guard.py` before any test; the 2323 console takes one client at a time; one `pio`
process at a time; harness logs under `tools/bench/runs/harness/`.

## 5. Progress log

- **2026-10-23:** campaign set up, operator answers recorded (campaign-rx-stall-1229.md, header). Release gate
  green (77 envs, 2741 Unity, pytest 1190).
- **2026-10-23 release:** `v4.40a.10.22` published (CI run 39954657798, 66 assets, re-inflate 27/27, flasher 30
  boards 0 mismatches). Idle heap DK5EN-92 69068 B vs 69332 B baseline, pass
  (`tools/bench/runs/harness/heap-idle-v4.40a.10.22.json`). Note: on DK5EN-92 the USB port open resets the
  node (CH9102), and with `--setlog on` the only heap sample is the 300 s STAT line, so `heap_idle.py` needs
  `--host` and `--boot-timeout-s 330`.
- **AU-17 not reproduced on 10.22:** three dev-channel checks on DK5EN-92 (release build), free heap with BLE
  paused 82204 / 87956 B, gate (76000 B) passed, `alloc fails 0`, check reached GitHub every time
  (`tools/bench/runs/harness/au17-check-20261023.log`); a further eight checks on the INSTRUMENT build 80-90 KB.
  Fixed since the 10.19 heap levers.
- **RM-NVS-01 data:** NVS on the bench nodes is about half full (`--info`): DK5EN-1 used 299 / free 331 / total
  630 / ns 10, DK5EN-14 304 / 326 / 630 / 9, DK5EN-92 297 / 333 / 630 / 10. The RM node blob needs about 5
  entries; the NVS-full hypothesis needs a node with many leftover keys.
- **AU-14 (running):** download of `v4.40a.10.22` (1249156 B, 13.4-13.9 s) on DK5EN-92 twice (release build
  10.21, INSTRUMENT build tagged 10.21) with three to four `TEST` texts from DK5EN-1 arriving during the
  download: no reboot. A release build has no `--update forget`, and a staged record survives `forget`, so each
  further download needs apply + reflash; automated cycles 11-14 running.
- **#1235 on RAK4631 (DK5EN-90):** PR build `098c2cfd` flashed (DFU over USB); `--board led on` accepted,
  `--info` `BOARD LED: on`, `CHARGE LED: not fitted`; operator confirmed the green LED flashes every 2 s.
- **New AU-18 (cycle 11, INSTRUMENT build):** gate passed (free 83816 B, block 32756 B >= 17000), mbedTLS failed
  `-32512` on the second 16717 B record buffer; the block rule covers one buffer, mbedTLS needs two. Clean failure,
  no reboot (`tools/bench/runs/harness/au14-loop-20261023.log`, cycle 11). BACKLOG AU-18.
- **AU-14 cycles 11-14 (INSTRUMENT build tagged 10.21, apply + reflash per cycle):** cycle 11 AU-18; cycles 12
  and 13 downloaded (14.0 / 14.6 s) with 3-4 `TEST` texts arriving, no reboot; cycle 14: **DK5EN-92 rebooted with
  `RESET_REASON=4 PANIC`** during or right after `--update check`, before the install (2323 socket broke). No USB
  capture was running (the CH9102 port open resets the node), so the backtrace is lost; the cycle log was not
  written. Follow-up: check-only loop with a continuous USB capture (`au14-checks-usb-20261023.log`), ELF of the
  build kept for addr2line.
- **AU-19 found (check-only loop with USB capture):** checks 1-7 fine (BLE-paused heap 80-85 KB, block 34-47 KB);
  check 8 panicked: `InstrFetchProhibited`, `PC 0x00000000`, backtrace `NimBLEDevice::host_task()`
  (`NimBLEDevice.cpp:835`), right after `[AU];heap;start` = inside `esp32BlePause()` (`NimBLEDevice::deinit(true)`).
  With cycle 14: 2 panics in about 35 BLE pauses. Every classic-ESP32 check pauses BLE and 10.19+ checks daily, so
  field nodes are exposed. AU-14 (TASK_WDT once on 10-17) not reproduced in 4 downloads; AU-19 is the likelier
  real defect. BACKLOG AU-19.
