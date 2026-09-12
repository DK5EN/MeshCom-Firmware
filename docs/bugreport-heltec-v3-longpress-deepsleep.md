# Bug report: Heltec V3 can no longer be switched off with the user button (v4.35t)

**Firmware:** MeshCom 4.35t (upstream `dev` 6edc7499, introduced by PR #1135 / fork commit `e242a4cf`)
**Board:** Heltec WiFi LoRa 32 V3 (reported); same code path on Heltec V2, V4, Wireless Stick V3, Wireless Tracker, TLORA V2.1.6, Heltec T114 (nRF52)
**Works on:** MeshCom 4.35s
**Date:** 2026-09-12
**Reporter:** field report via DK5EN
**Status:** fixed in `fork-main` `40c29e7f` (release wait gated on `--button on`), bench-confirmed on DK5EN-93 2026-09-12, upstream [PR #1140](https://github.com/icssw-org/MeshCom-Firmware/pull/1140)

## Symptom

On v4.35s a long press on the upper (PRG, GPIO0) button switches the node off:
the OLED goes dark and the node stays off until reset or power cycle.

On v4.35t the same long press no longer switches the node off. The OLED blinks
off for a moment and the node comes straight back up (full reboot, boot log
shows `[BOOT] RESET_REASON=5 DEEPSLEEP`).

## Steps to reproduce

1. Flash v4.35t on a Heltec V3, USB serial monitor attached.
2. Hold the PRG button for about one second.
3. Serial shows `[BOARD_HELTEC_V3]... GO to deepsleep`, then immediately a fresh
   boot with `RESET_REASON=5`.
4. Releasing the button earlier or later does not change the result.

## Root cause

v4.35t replaced the body of `--deepsleep` with the shared helper
`esp32EnterDeepSleep()` (`src/esp32/esp32_sleep.cpp`). The helper does what the
old code never did: it arms the user button as an ext1 wake source before
sleeping (lines 181-194):

```c
if (iButtonPin != 99)
{
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_sleep_enable_ext1_wakeup((1ULL << iButtonPin), ESP_EXT1_WAKEUP_ANY_LOW);
}
esp_deep_sleep_start();
```

The long-press path calls this helper while the button is still held down.
`PressLong()` in `src/onebutton_functions.cpp` is attached with
`btn.attachLongPressStart()` (line 316), which fires 800 ms into the press
(`OneButton::_press_ms`), not on release. From there the whole sequence up to
`esp_deep_sleep_start()` takes about 50 ms (the Vext-off delay on line 83).
The operator's finger is still on the button, GPIO0 is still LOW, and the ext1
wake condition is already true at sleep entry. The chip wakes up immediately
and reboots. From the outside the node "cannot be switched off".

On v4.35s no wake source was armed at all, so the node slept until reset. The
"switch off" behaviour the field relies on was in fact the absence of a wake
source, which is what PR #1135 deliberately fixed.

Wireless Paper and Vision Master E213 use the same long-press-start trigger and
also arm the button as wake source, but they do not show the bug: their sleep
path runs a full e-ink refresh plus about 300 ms of delays before sleeping, so
the button is normally released by then. That is timing luck, not a guard.

The nRF52 helper `nrf52EnterDeepSleep()` (`src/nrf52/nrf52_sleep.cpp` line 114)
has the same flaw: `systemOff(iButtonPin, LOW)` arms a LOW-level sense on the
button that is still held. Heltec T114 long press is affected the same way.

Possible secondary effect on GPIO0 boards (Heltec V2/V3/V4, TLORA): GPIO0 is
also the boot strapping pin. If the wake reset samples the strap while the
button is still LOW, the ROM enters serial download mode instead of the
application. The node then looks dead until a reset. Whether the ESP32-S3
samples the strap on a deep-sleep wake was not verified on the bench; the
immediate-wake reboot alone explains the report.

## Why the bench test did not catch it

PR #1135 was bench-verified on Heltec V3 (dk5en-93) with `--deepsleep` sent
over serial, then a button press to wake. That test never has the button held
at sleep entry. The long-press-to-sleep path itself was not exercised.

## Proposed fix

Wait for the button to be released before arming the wake source, in both
shared helpers. Minimal form in `esp32EnterDeepSleep()`, directly before the
`if (iButtonPin != 99)` block:

```c
// Long press arrives via attachLongPressStart(): the button is still held
// when we get here. An ext1 ANY_LOW wake on a pin that is already LOW fires
// immediately, so wait for the release first (bounded, in case the pin is
// stuck or the call came over serial with no button involved).
if (iButtonPin != 99)
{
    uint32_t t0 = millis();
    while (digitalRead(iButtonPin) == LOW && millis() - t0 < 10000)
        delay(10);
    delay(100);   // contact bounce after release
}
```

The same block belongs in `nrf52EnterDeepSleep()` ahead of
`systemOff(iButtonPin, LOW)`.

Alternative: attach `PressLong()` with `attachLongPressStop()` instead. That
changes the feel of the long press on every board (nothing happens until the
finger lifts) and does not cover a `--deepsleep` sent over serial or BLE while
someone happens to hold the button. The wait-for-release in the helpers is the
smaller and more robust change.

## Verification for the fix

1. Heltec V3: hold PRG for one second, release. Node goes dark and stays dark.
   Serial shows no reboot.
2. Press PRG again: node boots with `RESET_REASON=5 DEEPSLEEP`.
3. Hold PRG for five seconds without releasing: node goes dark at about one
   second, stays dark after release.
4. `--deepsleep` over serial with the button untouched: node sleeps at once
   (no 10 s wait, since the pin is HIGH).
5. Repeat 1-3 on Heltec T114 for the nRF52 helper.
