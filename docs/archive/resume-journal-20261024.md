> **Archived 2026-10-24.** Entries moved verbatim out of `docs/RESUME.md` when it was cleaned up on
> 2026-10-24. Hashes, IPs and "open" markers are as of the day written. Earlier journal:
> [`resume-journal-20261005-20261023.md`](resume-journal-20261005-20261023.md).

# RESUME journal 2026-10-23 .. 2026-10-24

## "Open now" bullets as of 2026-10-24

- **Campaign BL-01 + GWRX-01 nRF52 (2026-10-24): closed.** BL-01 `--mesh off` decided and pinned (`deea597f`),
  GWRX-01 benched on nRF52 (DK5EN-90) and ESP32, bench driver `tools/bench/gwrx_bench.py`. New: GWACK-01 (gateway
  ACK pair can share one msg id). Record: [campaign-bl01-gwrx01-nrf52-20261024.md](campaign-bl01-gwrx01-nrf52-20261024.md).
- **AU-19 fix (candidate `v4.40a.10.23`):** NimBLE host-task panic in the BLE pause of `--update check` on
  the classic ESP32; plan and waves in [campaign-bench-20261023.md](campaign-bench-20261023.md), section 6. The
  remaining #1235 hardware checks (Heltec V3, T-Beam) run against `fork-dev`; findings are fixed on `fork-dev`, no PR.
- **AU-20 fixed (candidate next release):** `tools/nimble_patch.py` deinits the NimBLE flow timer before its
  second init (about 50 B per BLE pause gone, benched on DK5EN-92); no upstream report (old pinned version). See
  [campaign-au20-ble-pause-heap.md](campaign-au20-ble-pause-heap.md). Side result: the AU-19 patch survives 100 forced
  pauses on the S3.
- **AU-18 and UDP-04 fixed (next release):** heap gate probes both TLS record buffers (`9b43b4f0`), duplicate server
  positions no longer take a dedup slot (`6e6e7e40`); RX-DEAF-01 fixed (one TX-gate poll hit left an ESP32 node deaf until reboot; A+B fix benched); nRF52 sibling
  RX-DEAF-02 fixed defensively (latent: PREAMBLE IRQ masked in RX; forced-latch bench on DK5EN-90).
  See [campaign-au18-udp02-rxdeaf.md](campaign-au18-udp02-rxdeaf.md).
- **Release `v4.40a.10.22`** (tag on `8a329702`, CI run 39954657798): UDP-03, upstream #1215/#1225/#1234. 66
  assets, 27 `.autoupdate` re-inflate ok, tag string and T-Deck field-command scan ok; idle heap DK5EN-92 69068 B
  (baseline 69332 B, pass); web flasher v4.40a.10.22 (30 boards, 0 mismatches), 10.21 folder pruned; presentation
  synced. Tagged with the 10-22 date so a same-day fix can be `v4.40a.10.23`. DK5EN-92 runs a local release build
  of the tag commit; the other nodes not yet flashed.
- **Bench session 2026-10-23** ([campaign-bench-20261023.md](campaign-bench-20261023.md)): AU-17/AU-14, #1235 on
  hardware, RM-NVS-01, BL-01, RX stall #1229 = WEB-RX-01 ([campaign-rx-stall-1229.md](campaign-rx-stall-1229.md),
  fixed `5fdd4b8d`, W3 bench 0 frames lost, not released).

## Session line

- **2026-10-19/20:** releases v4.40a.10.20 and v4.40a.10.21; AU-16 root cause and fix (field-verified);
  WEB-WDT-01, WEB-STOP-01, RM-NVS-01 diagnostics; web flasher WF-01..WF-05; upstream PR #1224.
