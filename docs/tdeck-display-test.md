# T-Deck Plus — colour and geometry display test (TM-41)

`--disptest` drives the operator's colour/geometry sequence on the T-Deck Plus panel and lets the
bench harness assert the result **blind**, with no camera and no operator. Everything below was
measured on DK5EN-14 (`/dev/cu.usbmodem1101`, env `t_deck_plus`) on 2026-08-30.

## 1. What it proves — and what it does not

The panel does not drive MISO, so `tft.readRect()` returns a constant and `--screencrc` is not a
valid instrument on this hardware ([`tdeck-findings-20260828.md`](tdeck-findings-20260828.md) §
"Panel readback"). The assertion therefore sits one step earlier, on the flush path:

| Question                                                       | Answered by `--disptest`                |
| -------------------------------------------------------------- | --------------------------------------- |
| Did the firmware build the intended frame, pixel for pixel?    | **Yes** — CRC32 per frame, 516 frames   |
| Did exactly those bytes reach `tft.pushColors()`?              | **Yes** — the CRC covers that block     |
| Did the SPI transfer complete without a stall or a lost frame? | Partly — a stall shows in the step time |
| Do the pixels light up on the glass, in the right colours?     | **No** — that needs an operator's eye   |

So a PASS means: the right pixels were handed to the panel. An operator may watch the sequence
(and should, once, to confirm colour order and rotation direction), but the test runs unattended
either way.

## 2. Running it

```sh
# the whole sequence, every frame CRC-checked against the host renderer
python3 tools/bench/tdeck_harness.py --scenario disptest

# one phase, coarser growth steps
python3 tools/bench/tdeck_harness.py --scenario disptest \
        --disptest-phase square --disptest-stride 7

# straight over the console, without the harness
--disptest                      # = --disptest full 1
--disptest circle 4
```

`disptest` is part of `--scenario all`. The scenario re-renders every frame with
`tdeck_parse.disptest_frame()` and compares CRC by CRC; the summary reports steps passed, the first
mismatching step, the device-side duration and the frame rate.

## 3. The sequence

| Phase      | Frames (stride 1) | Content                                                                                                             |
| ---------- | ----------------- | ------------------------------------------------------------------------------------------------------------------- |
| `invert`   | 2                 | eight horizontal colour bars, then the same frame complemented — the whole screen inverts                           |
| `colors`   | 10                | full-screen red, yellow, green, blue, magenta, then the same five complemented (cyan, blue, magenta, yellow, green) |
| `square`   | 160               | black square on white, growing from the centre, 1 px per step, until it covers the screen                           |
| `circle`   | 200               | white circle on black, growing from the centre, 1 px per step, until the corners are covered                        |
| `triangle` | 144               | white triangle (vertices on r = 100 px) rotating 3 turns clockwise, then 3 counter-clockwise, 15° per step          |

Two deliberate deviations from the operator's wording, both so that the frame stays CRC-checkable:

- **"Invert the whole screen"** is done in RAM (bars → complemented bars), not with the panel's
  `INVON` command. A panel-side inversion changes nothing the driver can see, so it would be
  invisible to the assertion. The same applies to the complementary colour pass.
- **The stride is selectable.** `1` is the operator's "one pixel per step" and is the default; the
  square and circle phases accept a stride of up to 64 for a quick check (`--disptest square 7`
  runs 23 frames in 1.5 s). The triangle always uses 24 steps per turn.

## 4. How the two sides stay identical

Both renderers work in **doubled pixel coordinates** `X = 2x-(W-1)`, `Y = 2y-(H-1)`, so the screen
centre is exactly `(0,0)` and every test is integer-exact:

| Shape    | Inside test                                                                |
| -------- | -------------------------------------------------------------------------- |
| square   | `abs(X) <= 2h and abs(Y) <= 2h` (h = 1..160 px)                            |
| circle   | `X*X + Y*Y <= (2r)*(2r)` (r = 1..200 px)                                   |
| triangle | all three integer edge functions `>= 0`, winding pinned by the signed area |

No floating point is involved anywhere: the rotation uses a literal 24-entry table of
`round(1024*sin(2*pi*i/24))` that exists twice, in `src/t-deck/tdeck_debug.cpp` (`DT_SIN24`) and in
`tools/bench/tdeck_parse.py` (`DISPTEST_SIN24`), and vertices are `(200*sin) >> 10` — an arithmetic
shift, which floors in both C and Python. TFT_eSPI's own `fillCircle()`/`fillTriangle()` are
deliberately **not** used: their exact pixel set cannot be reproduced on the host.

The firmware tests every pixel; the host computes the row spans (500 frames in 0.06 s). That the
two pick the same pixels is a unit test, not an assumption
(`test_span_renderer_matches_the_firmware_pixel_test`, which brute-forces the firmware's per-pixel
loop in Python and compares whole frames).

**Byte order** is the other half of the contract. `LV_COLOR_16_SWAP=1`
(`variants/t_deck_plus/lv_conf.h`) plus `pushColors(..., swap=false)` means memory byte order is
wire byte order, so a frame is RGB565 **high byte first**, row-major, 320x240 = 153600 bytes. The
five solid fills pin this down: red must be `f8 00` per pixel, and its CRC is
`zlib.crc32(b"\xf8\x00" * 76800)`.

## 5. Measured (DK5EN-14, 2026-08-30)

| Metric                          | Value                                                               |
| ------------------------------- | ------------------------------------------------------------------- |
| Full sequence                   | 516 frames, **42.4 s** on the device, 12.2 frames/s                 |
| Per frame (render + CRC + push) | min 64 ms, p50 69 ms, max 119 ms                                    |
| Frame size                      | 153600 B (76800 px), one `pushColors()` per step                    |
| `--disptest square 7`           | 23 frames, 1.5 s, 14.8 frames/s                                     |
| `--disptest triangle`           | 144 frames, 17.0 s                                                  |
| Result                          | 516/516 CRCs matched the host renderer, first run after the WDT fix |

The 66 ms are not all SPI: each step also fills 76800 pixels in PSRAM and CRC32s 153600 bytes.
"One pixel per step at full frames" is therefore feasible — the whole sequence fits in well under a
minute — which is why stride 1 is the default.

## 6. Operational notes

- The sequence runs **synchronously on the loop task**: LoRa, WiFi and the GUI are not serviced for
  its duration (`[INSTR-LOOP];gap;ms;42730` after a full run — expected, not a defect).
- Because the whole run sits inside one `loop()` iteration, the Arduino loop task never reaches its
  own `esp_task_wdt_reset()`. `--disptest` feeds the task watchdog itself once per frame; without
  that the node aborts after 5 s (seen on the bench at square step 61, TWDT → reboot).
- While the test runs, `disp_flush()` reports every LVGL area as flushed but pushes nothing, so
  LVGL cannot fight the test for the panel. At the end the screen is invalidated and repainted:
  the panel is never left inverted, black or holding a test frame (verified with `--framedump`
  right after a run — the normal message UI comes back).
- `tft_on()` is called at the start and the end, so the 30 s panel timeout cannot darken the screen
  mid-sequence and the timer restarts afterwards.
- Memory: one 150 KB PSRAM frame buffer plus a 1 KB CRC table, both allocated for the run and freed
  at the end. Nothing permanent.

## 7. Files

| File                              | Role                                                          |
| --------------------------------- | ------------------------------------------------------------- |
| `src/t-deck/tdeck_debug.cpp`      | `tdeck_dbg_disptest()`, `dt_render()`, the `[DISPTEST]` lines |
| `src/t-deck/tdeck_main.cpp`       | `disp_flush()` yields the panel while the test runs           |
| `src/command_functions.cpp`       | `--disptest [phase] [stride]` dispatch                        |
| `tools/bench/tdeck_parse.py`      | `[DISPTEST]` parser + the host reference renderer             |
| `tools/bench/tdeck_harness.py`    | `--scenario disptest`                                         |
| `tools/bench/test_tdeck_parse.py` | parser cases, renderer unit tests, firmware-equivalence check |

## 8. Line format

```
[DISPTEST];begin;phase;<s>;stride;<n>;w;320;h;240;steps;<n>;ms;<millis>
[DISPTEST];step;<phase>;n;<i>;crc;<hex8>;px;76800;ms;<n>
[DISPTEST];end;steps;<n>;ms;<n>
[DISPTEST];err;<reason>[;<detail>]
```

`n` counts within the phase, `crc` is the CRC32 (zlib/IEEE) of the exact byte block handed to
`tft.pushColors()`, `ms` on a step line is render + CRC + push for that frame.
