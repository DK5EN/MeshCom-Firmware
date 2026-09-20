# T-Deck-14 (DK5EN-14): touch and keyboard dead, "repeated crashes" — 2026-09-11

Status: **cause found, fixed, bench-verified and confirmed by eye 2026-09-11
evening (operator: touch works again).** Not a hardware
fault, not a crash, not the instrument image. The node's persisted **keyboard
lock** flag (`node_keyboardlock`, SYM+K on the T-Deck keyboard) was on. The
firmware had no way to read or clear it other than the keyboard combo itself,
and its effects look exactly like a dead touch panel plus a device that "keeps
crashing".

## Symptoms, as reported and observed

| Symptom                            | Source                               |
| ---------------------------------- | ------------------------------------ |
| Touch does not respond             | operator, repeatedly                 |
| Keyboard does not reach the screen | operator                             |
| Crashes / freezes, several times   | operator                             |
| Trackball still works              | operator                             |
| UI does not repaint on input       | operator ("screen does not come up") |

## Cause

`meshcom_settings.node_keyboardlock` was `true`. It is toggled by SYM+K in
`keypad_read()` (`src/t-deck/tdeck_main.cpp`), persisted as `node_kblock` in
NVS by the next `save_settings()`, and read back at every boot. With the flag
set:

- `touchpad_read()` polls the GT911 but reports nothing to LVGL and never calls
  `tft_on()` — **touch is dead and cannot wake the panel**.
- `keypad_read()` reads the key (that is the `[KEY];…;src;kbd` line) but
  reports `LV_INDEV_STATE_REL` and skips `tft_on()` — **keys are logged, never
  typed, and cannot wake the panel**.
- `mouse_read()` is not gated — **the trackball still works and still wakes
  the panel**.
- The panel times out after 30 s idle (`[TFT];off;…;idle_ms;30005`). With
  touch and keyboard unable to wake it, a dark screen that ignores every tap
  and key is a **"crash"** to anyone holding the device. The trackball wakes
  it, which is the "sometimes it comes back".

Every "crash" and "freeze" reported matches this. Every actual reset seen on the
wire was `rst:0x15 (USB_UART_CHIP_RESET)` from a port open. No panic occurred.

Live proof on the running shipping image, over the 2323 console and then over
USB with the logger attached (no reset, uptime continuous from 538 s to 800 s):

```
...KBD raw-mode unknown ...KEYLOCK on              <- --info, both transports
[KEY];79;ms;706041;src;kbd  [KEY];74;…  [KEY];67;…  <- operator typing, nothing shown
[TFT];off;ms;713916;idle_ms;30005                   <- panel times out
[TFT];on;ms;719684;was_sleeping;1                   <- trackball wakes it
```

## Why it was missed

- `--info` already printed `KEYLOCK on` (TD-10 added it), but nobody read that
  field while chasing the stall and the reset reasons.
- The "configuration byte-identical to its vault backup" check compares the
  web `POST /config` export, which does **not** contain `node_kblock`. The
  flag lives only in NVS.
- Nothing on the screen indicates the lock (the Setup switch for it is
  commented out in `lv_obj_functions.cpp`).
- How the flag got set is not recorded. SYM+K is a one-combo toggle on a
  handheld that was carried around all evening; a `--key` injection cannot
  produce it (the corpus injects `1`, `999999`, `abc`, the harness `bench73`,
  none of which map to `0x27` in any keyboard mode).

## Fix

`--keylock on/off` in `src/command_functions.cpp`, T-Deck and T-Deck Plus only,
outside the instrument guard so a shipping image has it. `off` clears the flag,
calls `tft_on()` and saves; `on` mirrors the SYM+K path. Listed in `--help`.
`--info` continues to report the state.

Bench, DK5EN-14, 2026-09-11 evening:

- Shipping image with the fix, flashed over WiFi: `--keylock off` over USB
  answered `...KEYLOCK off`, `--info` agrees, and the flag stays off across
  `--reboot`.
- Regression scenario `tools/bench/tdeck_harness.py --scenario keylock`
  (needs an instrument image for `--key`): PASS on the instrument build of
  the same tree. It asserts the command exists, that `on` blanks the panel,
  that a key is read but does not wake the panel while locked, that `off`
  wakes it, and that `--info` reports `off`. On a pre-fix image the first
  step answers `wrong command` and the scenario fails.
- Operator confirmation by eye after the unlock: touch responds again.
- The node was left on the instrument image with the fix (build
  `Sep 11 2026 / 19:54:12`), unlocked, per Decision 2. The shipping image
  with the fix is in the scratchpad if the operator prefers it.

## The LVGL stalls

Multi-second gaps inside LVGL (2.69 s, 3.39 s, 2.85 s across three boots) were
the only hard anomaly in the earlier analysis. They are **not** the cause of
this bug and were not investigated further here. They remain a separate lead;
if they are still seen after the lock is cleared, file them on their own.

## TD-17: attaching without resetting

The reason no crash was ever captured was methodological: every port open
reset the node. That turned out to be a property of how the port was opened,
not of the board:

- **`dtr=True, rts=False` on open attaches to a running T-Deck without a
  reset** and output flows immediately. Verified twice: uptime continuous
  across the attach (538 s to 735 s), commands answered.
- **`dtr=False, rts=False` on open resets the node** (`rst:0x15`, uptime back
  to 12 s). This is what `serial_session.py` and `tdeck_harness.py` do, which
  is why every earlier look at the device cost its state.
- Port numbers follow the USB socket. On this evening the T-Deck was
  `/dev/cu.usbmodem2101` and the RAK4631 `/dev/cu.usbmodem101`; the first
  logger attach landed on the RAK, whose replies only appear once DTR is
  high. Identify by sending `--info` and reading the callsign, never by port
  number.

`tools/bench/usb_logger.py` is the continuous logger: opens once with DTR
high, timestamps every line, re-opens only if the OS drops the device (marker
`### lost` / `### attached`), and takes commands from a file so nothing else
ever has to open the port. Proven this evening across a WiFi OTA (safeboot
plus app reboot) and a `--reboot` without losing a line. Attach it before the
next suspected crash and leave it running.

## Consequence for the DRY campaign

T-Deck-14's G0 captures (BLE, UDP-1990, EXTUDP) were taken with the lock on.
The lock touches only the LVGL input path, none of those three surfaces, so
the captures are **valid** and stay the G1 baseline. The T-Deck UI checklist
(H11) can be filled in now.
