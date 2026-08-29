# Upstream sync 2026-08-29 — Fable Verdict

Scope: `git diff fc83554e upstream/dev` (upstream `dev` `2cb6bb4d`, PRs #1104-#1112; net 5 files,
+18/-43). Six finders (correctness, memory safety, timing, altitude, protocol, test coverage),
every load-bearing claim verified against the `upstream/dev` tree by the orchestrator. Review only;
no code changed. Rules applied: `docs/codequality-rules.md` (BND, RACE, SPI, MEM).

## Finding UP-01: `serializeJson` bound is the JSON's own length again

- **File:** `src/mheard_functions.cpp:348,351` and `:656,658` (upstream/dev)
- **Severity:** medium (latent) — was high while `PP`/`SRC`/`GW` existed
- **Rule:** BND-03 "validate length against the buffer, not against the payload"
- **Failure scenario:** `bleBuffer` is `uint8_t[300]` on the stack. `serializeJson(mhdoc,
bleBuffer+1, measureJson(mhdoc)+1)` lets the document, not the buffer, set the write limit.
  Worst case today, verified with a compiled probe against the vendored ArduinoJson 7.4.3: the
  13-key document with a 119-character source callsign (see UP-06) reaches 284 of 299 bytes —
  15 bytes of headroom. Any added key (exactly what #1091/#1093 did) or a longer value writes past
  the frame on the stack; `addBLEOutBuffer`/`addBLEComToOutBuffer` clamp `len` afterwards, too
  late. The revert of #1090 removed the one call site the 08-28 issue doc had praised as correct.
- **Fix:** `size_t n = serializeJson(mhdoc, bleBuffer+1, sizeof(bleBuffer)-1);` and pass `n+1`
  on, both sites; native regression test "document longer than the buffer is truncated, canary
  intact" (ArduinoJson into a native env + no-op `addBLEOutBuffer` stub — no existing harness
  covers `mheard_functions.cpp`). Goes into our next PR, like #1102.

## Finding UP-02: `delay(40)` x2 in `add_map_point()` on the LoRa RX path

- **File:** `src/t-deck/lv_obj_functions.cpp:1757,1765`; caller chain
  `OnRxDone()` (`src/lora_functions.cpp:1151`) -> `tdeck_add_pos_point()` (`:3564`) ->
  `add_map_point()`; and `refresh_map()` (`:1875-1894`, loop to `MAX_POINTS` = 30)
- **Severity:** high
- **Rule:** RACE — "never block"; the file's own deferred-display pattern (`bPendingDisplayPos`,
  `flushDeferredDisplayUpdates()` in `esp32_main.cpp:1822`) exists for exactly this and is bypassed
- **Failure scenario:** every position beacon from an already-known station costs 80 ms of blocked
  loop task inside the RX handler (on ESP32 `OnRxDone` runs on the loop task, catalogue C-01 — not
  an ISR, but the RX/relay critical path). Zoom or MAP-tab switch runs `refresh_map()`: up to
  30 x 80 ms = 2.4 s frozen inside one LVGL click callback, LoRa unserviced. `delay(19)` -> `40` is
  a widened band-aid for the lost-flush race (`tdeck-findings-20260828.md` §1) applied per point
  deletion instead of once per bus hand-over.
- **Fix:** drop both delays; the NULL-immediately fix (UP-04) plus the bus mitigation cover the
  original symptom. Folds into timing campaign TM-05/TM-08 (`BACKLOG.md` §3.8f).
- **Status 2026-08-29: FIXED.** The G01 hunk had already removed the `delay(40)` pair on our tree;
  the remaining `delay(10)` in the slot-recycling branch is gone too. Verified with
  `tdeck_harness.py --scenario map --map-stations 40` (recycle branch reached, 40/40, no crash).

## Finding UP-03: map projection scales x and y differently

- **File:** `src/t-deck/tdeck_sdmap.cpp:304-305`; `src/t-deck/lv_obj_functions.cpp:1468-1470`
- **Severity:** high (correctness), plus codequality "magic numbers" (320, 320, -32 untied to
  `SDMAP_TILE_PX` / `LV_HOR_RES`)
- **Failure scenario:** `*x = (int16_t)((xf - floor(xf)) * 320) - 32;` but
  `*y = (int16_t)((yf - floor(yf)) * SDMAP_TILE_PX);` (256), while the whole image is scaled 1.25x
  by `lv_img_set_zoom(map_ta, 320)`. The x axis gets the 1.25 factor twice, the y axis once: station
  markers land at the wrong height at every zoom level, and x can go negative (previous contract
  0..255). `set_map()` later resets `map_x/map_y` to `SDMAP_TILE_PX` without re-zooming.
- **Fix:** none upstream-side; our viewport composition (`db298c49`, `center_err 0/0` at every
  zoom) replaces this. **Merge trap:** two of the four map hunks (label width, commented
  `lv_obj_align`) apply onto our tree without a conflict marker — review the merged map code by
  hand, do not trust the absence of conflicts.

## Finding UP-04: G01 fixed upstream, identical intent to ours

- **File:** `src/t-deck/lv_obj_functions.cpp:1758,1766` (`map_point[ip] = NULL`,
  `map_point_label[ip] = NULL` after `lv_obj_del`)
- **Severity:** none (positive) — keep Kurt's hunk, drop ours from the PR scope.

## Finding UP-05: `I` register at 239 of 244 characters with six group calls

- **File:** `src/command_functions.cpp:4961-4990`
- **Severity:** low, watch item
- **Failure scenario:** with `FWDATE` removed the document is 215 chars (no group calls) to 239
  chars (all six `GCB` slots) against the 244-char budget of `addBLEComToOutBuffer`. Compliant, but
  the next key added upstream truncates it mid-value again (the 82db3d41 failure mode).
- **Fix:** none now; the structural fix is the frame-budget check from
  `issue-ble-i-register-mtu-20260828.md` §4.

## Finding UP-06 (pre-existing, found on the way): callsign regex accepts 119-character strings

- **File:** `src/regex_functions.cpp:9` (`^[0-9A-Z]?[A-Z]?[0-9]+[A-Z]...` — `[0-9]+` unbounded);
  `src/aprs_functions.cpp:194,292` (path segments capped at 120), `cConcat2[255]`
- **Severity:** medium — input validation; not introduced by this diff
- **Failure scenario:** a frame with source-last `D111...1A` (119 chars) passes `checkRegexCall()`,
  becomes `mh_callsign`, and reaches every consumer that assumes `MAX_CALL_LEN` (20): the MH JSON
  (UP-01 headroom), `mheardCalls[]`, display strings. Which fixed-size consumer breaks first is not
  yet traced.
- **Fix:** bound the regex (`[0-9]{1,2}`-style, or a length check `<= MAX_CALL_LEN` before the
  match) — separate small PR with a native test; trace consumers first.

## Informational

- `src/lora_functions.cpp:598` `mh_path_payload = ""` is vestigial after the revert (real value
  still set at `:706` before `updateHeyPath()`); harmless.
- `FWDATE` has no consumer in this repo's docs or tools; removing it costs the app a build date
  (#1103 is dead), nothing else.
- MH document is a constant ~156 chars now; the `PL`-dependent overflow from the 08-28 issue doc
  is gone with the fields.
- Test coverage of the five touched areas: none. Nearest suite `test/test_hey_report.cpp` is sound
  (no unfalsifiable assertions, no over-mocking) but stops at the string-building layer.

## Refuted claims (do not re-investigate)

- "UP-01 overflows `bleBuffer` today" — refuted: worst case 284 <= 299 bytes with the current 13
  keys (probe against ArduinoJson 7.4.3). Latent, not live.
- "`delay(40)` runs in interrupt context" — refuted: `OnRxDone` is called from the loop task on
  ESP32 (catalogue C-01). It is the RX critical path, which is bad enough; the ISR framing is wrong.
- "`map_no_data_label` width 320 is dead code" — refuted: it is live, just a panel-width literal.
- "`mh_sourcecallsign` / `mh_destinationpath` are dangling after the revert" — refuted: both still
  used internally (`updateMheardPath()`).
