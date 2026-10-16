# Campaign AU-13: auto-update TLS on PSRAM boards (2026-10-16)

Resume point for the AU-13 fix. Approved by the operator 2026-10-17 ("go") with the default answers
to the open questions (classic PSRAM boards included, one DK5EN-92 measurement run), one `--update install` stage run on DK5EN-14, delivery on `fork-dev` only.

## Defect

AU-13: the auto-update check fails on T-Deck Plus with a TLS error. mbedTLS in the prebuilt
arduino-esp32 2.0.14 libraries allocates internal RAM only (`CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC`,
caps 0x804); the T-Deck leaves 57-84 KB internal free and a handshake needs about 56 KB. PSRAM
(4.6 MB free) is never used. `heapMark()` and `heapGateOk()` in `src/esp32/fw_update_net.cpp`
measure `MALLOC_CAP_8BIT`, which includes PSRAM, so the logs look healthy and the gate never
refuses.

- Field: OE5HWN-16, build 20261011, `[AU];tls;-32512` on a 16717 B record buffer (dev channel,
  automatic check 27 s after boot).
- Bench DK5EN-14 (`tools/bench/runs/harness/au_tdeck14_*20261016-23*.log`): warm node passes prod
  and dev checks with internal min 3216 B; right after reboot the dev check fails `tls -1` with 5
  internal/DMA alloc failures (124-232 B, caps 0x80c) and WiFi drops 11 s later (reason 4).
  Internal min 2740 B. This run is the "before" of the bench regression test.

## Fix

1. PSRAM-first mbedTLS allocator via `mbedtls_platform_set_calloc_free()`, installed once, on
   `BOARD_HAS_PSRAM` builds when `psramFound()`; internal fallback; free = `heap_caps_free`. Only
   those builds have PSRAM in the heap: without the flag `esp32-hal-psram.h` undefines
   `CONFIG_SPIRAM` and Arduino never runs `psramInit()`. So T-Beam Supreme (8 MB PSRAM, no flag)
   runs internal-only, and every `ttgo_tbeam*` env unflags it (`variants/ttgo_tbeam/platformio.ini:46-48`):
   classic keeps 76000/17000 by construction. Supreme's unused PSRAM is a separate backlog item.
2. Pure policy helpers in `src/fw_update.h`, host-tested in `test/test_fw_update`.
3. `heapGateOk()` measures internal RAM; PSRAM boards additionally need a PSRAM block >= 17000.
   Classic without the allocator keeps 76000/17000.
4. `heapMark()` appends `;ifree;..;imin;..;iblk;..` (no parser consumes `[AU];heap`).
5. AU task stack and the 4 KB staging block stay internal (flash writes disable the cache).

## Waves

| Wave    | Owner                     | Files / resource                                                                          | Status                                                         |
| ------- | ------------------------- | ----------------------------------------------------------------------------------------- | -------------------------------------------------------------- |
| W1      | implementer, Sonnet med   | `src/fw_update.h`, `test/test_fw_update/test_main.cpp`, `src/esp32/fw_update_net.cpp`     | done                                                           |
| Gate    | orchestrator              | `tools/regression.sh --stage 1,2`; builds t_deck_plus, heltec_wifi_lora_32_V3, ttgo_tbeam | PASS                                                           |
| Advisor | general-purpose, Opus med | read-only diff review                                                                     | APPROVED; test case + doc applied, Supreme item refuted        |
| W2      | orchestrator              | DK5EN-14 USB, DK5EN-1, DK5EN-92                                                           | DK5EN-14 PASS 10/10 + stage run; DK5EN-1/-92 not run (offline) |
| Docs    | orchestrator              | BACKLOG, RESUME; commit on fork-dev                                                       | done                                                           |

## W2 bench protocol

- DK5EN-14 after the fix: flash over USB with explicit port; 10 consecutive runs of reboot,
  `--updchan dev`, `--update check` right after `[BOOT];ready`. Pass: 10/10 `check;tag;`,
  alloc fails 0, no WiFi drop. Record `imin`; set the PSRAM-board internal floor from it.
- DK5EN-14: one `--update install` stage run (autoupdate off: stages, does not apply).
- DK5EN-1 (no PSRAM): one check, unchanged behaviour.
- DK5EN-92 (classic, PSRAM off by build): one check, unchanged behaviour.
- Restore `--updchan prod` on every node. No LoRa traffic.

## W2 results (2026-10-17)

- DK5EN-14, 10 x (reboot, dev check after `[BOOT];ready`): 10/10, alloc fails 0, no WiFi drop,
  `[AU];tlsalloc;psram` every boot. Internal: ifree 71.4 KB at job start, imin 55.7-56.6 KB (before
  the fix: 2.7 KB and a failed handshake). `FWNET_MIN_IFREE_PSRAM` stays 24000 (16 KB peak + 8 KB).
  Logs: `tools/bench/runs/harness/au13_dk5en14_20261017-001728/`.
- DK5EN-14 `--update install` (mode off): v4.40a.10.16 staged, 1568419 B in 22.3 s, imin 54.9 KB,
  alloc fails 0 (`au13_dk5en14_install_20261017-003008.log`). The staged record stays on the node.
- DK5EN-1 and DK5EN-92: offline, not run. Advisor item "T-Beam Supreme" refuted: without
  `BOARD_HAS_PSRAM`, `esp32-hal-psram.h:24-31` undefines `CONFIG_SPIRAM`, so PSRAM is never
  initialised there.
