# T-Deck-14 (DK5EN-14): touch and keyboard dead, repeated crashes — 2026-09-11

Status: **open, cause unknown.** Not reproduced on any other node. The device is
currently unusable as a UI: touch does not respond, the keyboard does not reach
the screen, and it has crashed several times. The trackball still works.

This document records what was measured, what was ruled out, and what the next
step is. It deliberately does not name a cause, because the leading hypothesis
was tested and failed.

## Symptoms, as reported and observed

| Symptom                            | Source                               |
| ---------------------------------- | ------------------------------------ |
| Touch does not respond             | operator, repeatedly                 |
| Keyboard does not reach the screen | operator                             |
| Crashes / freezes, several times   | operator                             |
| Trackball still works              | operator                             |
| UI does not repaint on input       | operator ("screen does not come up") |

## What the device reports about itself

Everything the firmware can check about the input hardware says it is fine:

```
[BOOT];init;sd;1;touch;1;touch_tries;1;kb;1;psram_buf;1;t_ms;3833
[INIT]...TFT: OK      [INIT]...Touch: OK      [INIT]...Keyboard: OK
[TFT];on;ms;3046;was_sleeping;0
[TFT];sleeping;0;bl;16          <- 16 is MAXIMUM (BRIGHTNESS_STEPS 16)
```

- The GT911 touch controller is found on the **first** attempt, every boot.
- **Keypresses reach the firmware.** Pressing keys produces
  `[KEY];20;ms;…;src;kbd`, `[KEY];68;…`, `[KEY];6a;…`. So the keyboard
  controller works, the I2C bus it shares with the touch controller works, and
  the firmware is reading it. What fails is downstream of that.
- The panel is awake at full brightness, and `tdeck_harness.py --scenario
disptest` (TM-41, every pushed frame CRC-checked) **passes**.
- `tdeck_harness.py --scenario tabs,nav,input` also passes — the LVGL input
  chain accepts injected events.
- The node's stored configuration is **byte-identical to its vault backup**
  except `node_alt` and `node_lon`, which the GPS rewrites. Nothing was left
  misconfigured.

## The one hard anomaly

Multi-second stalls, every one of them inside LVGL, across three boots:

| uptime | gaps > 250 ms | worst stall |
| -----: | ------------: | ----------: |
|   26 s |             1 |      2.69 s |
|   86 s |             6 |      3.39 s |
|  175 s |             1 |      2.85 s |

A UI frozen for three seconds at a time looks exactly like dead touch from the
outside. Whether the stall is the cause or another symptom is not established.

## What was ruled out

**The `INSTRUMENT_ENABLED` image is not the cause.** That was the leading
hypothesis and it was wrong. The reasoning was plausible — the instrument build
adds per-section `Serial.printf` into the loop, on the T-Deck that loop is the
LVGL path, and this project already records that `Print::printf` mallocs above
64 B and that per-line heap churn breaks timing-sensitive subsystems. The node
was reflashed to a **shipping** `t_deck_plus` image (verified: zero `INSTR`
output, `srvip`/`instreset` absent from the ELF, touch/keyboard/TFT all init
OK, one boot in a 60 s window). **It crashed again and touch is still dead.**

**No panic or backtrace has been captured.** Every reset seen on the wire was
`rst:0x15 (USB_UART_CHIP_RESET)` — caused by opening the serial port, i.e. by
the diagnosis itself. That is a methodological problem: each attempt to look at
the device destroys the state that would explain it.

## What is not known

- Whether this is hardware (touch flex, GT911, keyboard controller) or firmware.
- Whether it predates 2026-09-11. The node was driven all evening for BLE,
  UDP-1990 and EXTUDP captures, all of which passed — but **none of them looks
  at the screen**, so an already-broken UI would not have been noticed. The
  operator reports the device was working before.
- Whether the LVGL stalls are cause or symptom.

## Next step, in order

1. **Capture a real crash.** Attach `tools/meshlogger.py` or an equivalent
   continuous reader _before_ the crash and leave it attached, so the panic and
   backtrace survive. Opening the port at crash time resets the device and
   destroys the evidence — that is why there is no backtrace in this document.
   Symbolize with `tdeck_harness.py --elf`.
2. **Flash an official upstream release** (not a fork build) to separate fork
   firmware from hardware. If it still fails, the fork is exonerated.
3. **Inspect the touch flex connector** physically. The GT911 answering at init
   but never reporting a touch is consistent with a marginal connection.
4. Only then look at the LVGL stall.

## Consequence for the DRY campaign

T-Deck-14's G0 captures (BLE, UDP-1990, EXTUDP) were taken on the instrument
image, on a node now known to be unhealthy, and the node has since been
reflashed to a shipping image. **All three must be re-taken once the device is
repaired**, and until then they should not be used as a G1 comparison baseline.
The other three nodes are unaffected.

The T-Deck UI checklist (test plan H11) cannot be filled in at all in this
state.
