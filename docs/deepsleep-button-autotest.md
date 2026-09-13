# Automated test for the Heltec V3 long-press deep sleep (DS-03)

Date: 2026-09-13. Status: plan, not implemented.

## Outcome

Yes, the long-press deep-sleep path can be tested automatically. For the Heltec V3 no SwitchBot is
needed: the PRG button is GPIO0, and the CP2102 auto-program circuit lets the USB DTR line pull
that pin LOW. A DTR pulse from the bench script is an electrically identical button press with
millisecond-exact timing. OE3LCR used exactly this to wake a Wireless Paper in the PR #1135 thread;
the bench Heltec V3 (DK5EN-93, `/dev/cu.usbserial-0001`) is a CP2102 board.

## The bug this test guards

DS-03, fixed in fork commit `40c29e7f`, shipped upstream in PR #1140. The long press reaches
`--deepsleep` via OneButton `attachLongPressStart()`, 800 ms into the press with the button still
held. Since PR #1135 the shared sleep helpers arm that same pin as ext1 wake source, so the wake
condition was already true at sleep entry: the node rebooted at once (`RESET_REASON=5 DEEPSLEEP`)
and could not be switched off from the outside. Fix: `esp32WaitButtonRelease()` in
`src/esp32/esp32_sleep.cpp` waits for HIGH plus 100 ms debounce before arming.

The defect is a timing race between a held button and the wake arming. The regression test is a
timed hold, which DTR reproduces deterministically.

## Test sequence (Heltec V3, DK5EN-93)

1. Open the port once with `dtr=False, rts=False` and keep it open for the whole cycle. The Heltec
   resets on every port open (see memory `bench-fleet-ports`), so never reopen mid-test. The CP2102
   is powered from USB and stays enumerated while the ESP32 sleeps.
2. Send `--button on`. The bench default is off; with it off `init_onebutton()` never configures the
   pin and no long press exists (`[OBUT]...One Button GPIO(0) not activated` in the boot log).
3. Assert DTR for about 2 s, RTS untouched. That holds GPIO0 LOW past the 800 ms long-press
   threshold and past sleep entry, the exact field condition. Expect
   `[BOARD_HELTEC_V3]... GO to deepsleep` on the console. Then release DTR.
4. Fail if any boot output appears within 15 s of the release. Buggy firmware reboots within a
   second with reset reason DEEPSLEEP.
5. Assert DTR for about 200 ms, release. Expect a boot log with `RESET_REASON=8 DEEPSLEEP` (or
   `=5` on classic ESP32) and `wake: 3` (EXT1). Confirm the radio is alive afterwards, e.g. with
   `--mheard` or `--info`.
6. Run two cycles back to back. That is what the manual proof for PR #1140 did.

Rules:

- Never assert RTS together with DTR. That combination toggles EN and yields a plain power-on
  reset (`wake: 0`), which proves nothing about deep sleep.
- Keep the wake pulse short. GPIO0 is a strapping pin; a long LOW across a real reset would enter
  download mode.
- Pass/fail on the console log, not on timing alone: early boot output after step 3 = regression,
  missing `wake: 3` after step 5 = wake path broken.

## Implementation sketch

- New scenario `tools/bench/deepsleep_button.py`, built on `tools/bench/serial_session.py`
  (already opens with dtr/rts False). Needs one port handle for the full cycle, a DTR hold
  helper (`s.dtr = True; sleep(hold); s.dtr = False`), and a line reader with a deadline.
- Preconditions: mesh and gateway off on the node (no bench traffic to the live network), shipping
  image is enough, no `INSTRUMENT_ENABLED` needed.
- Cleanup: send `--button off` again if the bench default should stay off.
- Optional: run against the pre-fix image once to prove the test fails before and passes after.

## Where DTR does not reach, and what to use instead

DTR only reaches GPIO0 on serial-bridge boards. Not covered:

- native-USB boards (Vision Master E213, T-Deck, T-Beam Supreme): no DTR-to-IO0 circuit;
- boards whose button is not GPIO0: T114 (GPIO42), RAK4631 (WB_IO6), the T-Beam family.

Preferred actuator for those: a Pi GPIO through an optocoupler or small MOSFET soldered across the
button pads, driven from rpizero or dk5en-14. Same exact timing as DTR, same script.

SwitchBot Bot: fallback for a board that should not be soldered. Drawbacks: coarse press timing,
control through BLE or the cloud API with second-level latency, fragile mounting of a mechanical
finger on a 4 mm tactile switch on a bare dev board.

## Limit of both approaches

Neither DTR nor an actuator on the pads exercises the physical switch and its debounce. That is
acceptable here: what broke was the firmware sequencing, not the button.
