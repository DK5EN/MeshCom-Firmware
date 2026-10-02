# W3c (one settings struct, counters, member gate) -- advisor verdict, 2026-09-15

Independent advisor pass over the uncommitted W3c diff before its commit on `dry-unification`.
Verdict was REWORK; everything below is resolved in the same commit.

## Finding 1: a BLE settings write rewound `node_msgid` (medium) -- FIXED

- `src/nrf52/ble_settings_v1.cpp` `bleSettingsFromV1()` copied `node_msgid` inbound and
  `nrf52_ble.cpp` applies the whole converted struct. The app writes back the image it read
  earlier, so k ids were handed out twice and the next high-water save persisted the rewind.
- Fix: inbound copy removed; the legacy-blob path in `nrf52_flash.cpp` copies the counter
  explicitly (there the blob is the only source). Tests: the v1 poison case now poisons
  `node_msgid`, the round-trip case asserts the exception explicitly.

## Finding 2: downgrade -> reconfigure -> upgrade rewound the counter via `/counters.txt` (low-medium) -- FIXED

- On the legacy path the blob is the newest truth (official firmware advances the counter in it);
  a counters file left by an earlier run of this firmware is older. `countersLoad()` took the
  file. Fix: the legacy path discards the counters file before `countersLoad()`.
  Test `test_rewritten_legacy_blob_counter_beats_stale_counters_file` (blob 700, file 300,
  expect the block after 700); fails with the discard removed (800 expected, 400 seen).

## Finding 3: "no CRC recorded" == "blob changed" is a one-way decision (low) -- DOCUMENTED

Right for every fleet node (no release ever wrote a keyed store) and for RAK-90; wrong once for
a pre-W3c dev image never re-flashed with a release. Stated in the CRC comment block of
`nrf52_flash.cpp` and in BACKLOG.

## Finding 4: settings reset keeps the counter (low) -- DOCUMENTED AS A DECISION

`flash_reset()` (nRF52) and `clear_flash()` (ESP32) no longer reset `node_msgid`. Kept on
purpose: a reset is not a reason to replay ids into every neighbour's dedup ring. Comments in
both functions.

## Finding 5: stale comments -- FIXED

`settings_store.h` banner, `settings_store_nrf52.cpp` sizing narrative, `test/golden/nodes/README.md`.

## Finding 6: ESP32 `countersLoad()` ignored NVS failures (low) -- FIXED

Failed namespace open / write-back now print `[SETST];counters;namespace_open;failed` and
`[SETST];counters;load_writeback;failed`.

## Refuted (do not re-investigate)

- Per-platform defaults, array bounds and the T-Deck guard are identical to the two old structs
  at HEAD, member by member; the ESP32 seed block is untouched.
- `s_ble_settings_v1` equals the struct of v4.35t, fork-main, v4.35t.09.13 and upstream/dev
  (132 rows, only `unsigned int` vs `uint32_t` on `node_gpsbaud`); sizeof pinned at 2000 on ARM.
- The regenerated golden image differs from HEAD's only by the zeroed removed slots and the
  index-seeded fill shift.
- No serial line format changed; only additions (`legacy_rewritten`, `counters;save;overflow`)
  and `%ld` -> `%u` for the same values. No tool parses `[SETST]` lines.
- Stack on the 4 kB loop task: peak unchanged (one 2 kB `default_settings` in `flash_reset()`,
  the v1 image is a file-scope static).
- `msgIdAfterLoad()` ordering correct on both platforms and both nRF52 paths.
- Preferences and LittleFS stubs model the real semantics (namespaces, `isKey`, `putInt` size,
  open-on-missing false).
