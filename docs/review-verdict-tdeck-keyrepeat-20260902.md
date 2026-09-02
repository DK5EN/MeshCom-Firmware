# TD-10 key auto-repeat — Fable Verdict (2026-09-02)

Review of `feat-tdeck-keyrepeat-20260902` @ `1068edef` (five finders, one adversarial
verifier against the LilyGo keyboard source and the ESP32 Arduino core 2.0.14 I2C HAL).

## Finding 1: Old keyboard yields an all-zero frame, never a short read or 0xFF padding

- **File:** `src/t-deck/tdeck_main.cpp:794-800`, `:1081-1089`; `src/t-deck/kbd_repeat.h:73-83`
- **Severity:** high
- **Failure scenario:** `requestFrom(0x55, 5)` returns 5 or 0, never 1..4
  (`esp32-hal-i2c.c:205-210`). A key-mode slave wrote one byte (already consumed) and the
  FIFO is reset per STOP, so the frame is `00 00 00 00 00`; the code treats all-zero as
  inconclusive, `support` stays UNKNOWN for the whole boot, three I2C transactions per key
  press, and `[KBD];rawprobe` prints on every key press.
- **Fix (K1):** expected-cell check: derive (col,row) of the pre-remap key byte from the
  keyboard matrix (base and symbol tables from the LilyGo source); arm only when that bit
  is set in the frame. Any byte > 0x7F (bit 7 never set, rowCount 7) counts as a probe
  failure. All-zero stays inconclusive without penalty. Probe log bounded to
  `KBD_RAW_PROBE_MAX + 2` lines per boot.

## Finding 2: Nothing sends key mode (0x04) at boot

- **File:** `src/t-deck/tdeck_main.cpp:364-368` (`checkKb`)
- **Severity:** high
- **Failure scenario:** ESP32 reset mid-hold (WDT, OTA) with the C3 still powered leaves
  it in raw mode; 1-byte reads then return column-0 bitmasks as characters.
- **Fix (K2):** `kbdRawMode(false)` once after the keyboard presence probe.

## Finding 3: Support verdict can regress YES → NO

- **File:** `src/t-deck/kbd_repeat.h:63-87`
- **Severity:** high
- **Failure scenario:** `probes` is never reset after a successful arm; a ghosted frame
  (> 3 bits, no matrix diodes) counts as a failure even after YES; one more bad frame
  disables repeat for the rest of the boot.
- **Fix (K3):** reset `probes` on a successful arm; once `support == YES`, failures only
  decline the arm.

## Finding 4: Arm binds key A to key B's matrix bits on a fast transition

- **File:** `src/t-deck/kbd_repeat.h:63-70`
- **Severity:** medium (B is delivered after the hold, not lost; A repeats under B's finger)
- **Fix:** covered by K1 (own-bit check).

## Finding 5: Focus change mid-hold redirects the repeats

- **File:** `src/t-deck/tdeck_main.cpp:816-852`; `event_functions.cpp:796, 836, 894`
- **Severity:** medium
- **Failure scenario:** any touch moves keypad focus (`indev_click_focus`); LVGL re-resolves
  the focused object on every repeat tick, so a held key keeps typing into whatever was
  touched.
- **Fix (K5):** remember `lv_group_get_focused()` at arm; end the hold (reason `focus`)
  when it changes.

## Finding 6: Test gaps

- **File:** `test/test_kbd_repeat/test_main.cpp`
- **Severity:** medium
- **Fix (K7):** cases for exactly 3 bits valid / 4 invalid, timeout at `T-1` still held,
  `since_ms` rollover (0xFFFFFF00 → 0x100), armed bit disappearing, armed bit moving to
  another column, expected-cell lookups (Backspace 4/3, Space 0/5, `a` 0/3, `#` = SYM
  table 0/0), the K3 rules. `kbdRepeatHold` returns a reason enum so the timeout logic
  exists once; `struct KbdRepeat` reordered to 16 B; `{0}` initialisers.

## Refuted or declined (do not re-investigate)

- Bounce during a steady hold (duplicate char + early release): contact mechanics only,
  negligible for a firm hold; a 2-miss grace would risk one extra Backspace repeat after
  release. Declined.
- Keys typed during a hold: the C3 buffers one char; all but the last are dropped. Inherent
  to raw mode, documented in the plan.
- I2C cost on every eligible key press (2 writes + 1 read ≈ 1 ms): accepted.
- Enter (0x0D) inserted as a literal CR: pre-existing, unrelated.
- Debug key-inject ring triggering I2C on a node with a keyboard: harmless.
- `[KBD]` lines unconditional: matches the `[KEY]` precedent; one line per hold end.
- C3 power cycling on ESP32 reset: not decidable from the repo; K2 makes it moot.
