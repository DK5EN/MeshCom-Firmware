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
