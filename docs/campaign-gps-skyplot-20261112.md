# Campaign GPS-SKY: T114 GPS page from upstream, shared sky plot, T-Deck GPS view

Started 2026-11-12. Resume point for this multi-wave work.

## Goal

- Bring upstream PRs #1264-#1267 (T114 status page, graphic GPS page with sky plot, button fixes) into
  `fork-dev`.
- Review them, add the missing host tests, fix the AGE bug.
- Reuse the sky plot on the T-Deck as a GPS view.

## Operator decisions (2026-11-12)

- No separate upstream PR for the AGE fix. It goes into a larger upstream PR later.
- GSV on for the T-Deck: yes.
- T-Deck placement: merge the GPS view with the existing Position content. Preferred: a toggle that
  switches between the sky plot and the old text view; acceptable: scrolling down shows the old text.
- Kurt's gateway question (OE1KBC-12 auto RF-only): answered, no automatic switch. Evidence in the
  session scratchpad, not in the repo.

## Waves

| Wave | Content                                                                                                            | Owner                            | Status      |
| ---- | ------------------------------------------------------------------------------------------------------------------ | -------------------------------- | ----------- |
| 1    | Merge `upstream/dev` (07e75a32) into `fork-dev`, resolve 4 conflicts, gate                                        | orchestrator                     | in progress |
| 2    | Move pure T114 logic to `src/gps_skyplot.{h,cpp}`, AGE guard with regression test, `native_gps_skyplot` env     | implementer Sonnet medium + advisor Opus | open        |
| 3    | T-Deck GPS view: GSV on, feed parser, LVGL sky plot with toggle to the old text view, photo on DK5EN-14          | implementer Sonnet medium + advisor Opus | open        |

## Wave 1 conflict resolutions

- `.github/workflows/meshcom-ci.yml`: fork's `set -e` block kept, upstream's `LilyGo_T3_S3_V1_3`
  bootloader rename and artifact line added.
- `src/gps_functions.cpp` PCAS03: upstream's T114 GSV branch kept, both branches write through
  `GPS_UART`.
- `src/loop_functions.cpp`: T114 GPS page block placed in `sendDisplayTime()` before the CLK-01
  `sendDisplayClockLine()` call. Added a guard in `sendDisplayTrack()`: in track mode the open GPS
  page is updated instead of the per-second clock line re-sending the track page over it.
- `src/nrf52/nrf52_main.cpp`: CSMA-03 loop delay kept; on T114 the delay is split into steps of at
  most 10 ms with `btn.tick()` and `WZ_GPS_Feed()` each step.

## Review findings on upstream code

- **AGE shows 4294967 s without a fix.** TinyGPS++ `age()` returns `ULONG_MAX` when the location is
  invalid; `t114_gps_page.cpp` prints `age_ms / 1000` unguarded. Fix in wave 2.
- No tests in the upstream PRs. Test candidates: `gsvParseLine` (checksum, empty groups, talker),
  `satFresh`/`gsvStore` (6 s expiry, slot reuse, millis rollover), `snrColor` (35/20 dB-Hz), sky
  projection, AGE formatter.

## Open

- T114 bench check of the merged button/GPS page behaviour: no T114 on the bench.
