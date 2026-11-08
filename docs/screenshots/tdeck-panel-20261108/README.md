# T-Deck Plus panel photos, 2026-11-08

DK5EN-14 (T-Deck Plus, ST7789 IPS 320x240, RGB565), INSTRUMENT build with `--tdpattern` (DISP-PAT-01). iPhone RAW
(Lightroom, ISO 64, 1/100 s locked for every shot), converted to JPEG for this folder. Room light unless noted.

| File                                        | Content                                                               |
| ------------------------------------------- | --------------------------------------------------------------------- |
| `pattern-p0-grey-ramp-bl8.jpg`              | All 32 RGB565 grey steps, backlight 8                                 |
| `pattern-p1-near-white-black-bl8.jpg`       | Grey steps 24-31 and 0-7 as large patches                             |
| `pattern-p2-colour-ramps-dark-bl8.jpg`      | R, G, B, C, M, Y from dark to full colour                             |
| `pattern-p3-colour-ramps-light-bl8.jpg`     | The same hues from full colour to white                               |
| `pattern-p4-theme-pairs-bl8.jpg`            | Theme token pairs, light (left) and dark (right), "Aa" at 12/14/16 px |
| `chat-light-font-s.jpg`, `-m.jpg`, `-l.jpg` | Chat in light mode, font size S/M/L                                   |
| `chat-light-font-l-dark-room.jpg`           | Font size L in a dark room                                            |
| `chat-font-s-m-l-side-by-side.jpg`          | Crops of S, M and L at 2x                                             |

The photos show the sent bubble at `#CEF3CE`; it read as white on the panel and is `#8CD38C` since.

## Findings (linear RAW)

- **Near white the panel merges:** grey steps 29, 30 and 31 are one level; below step 28 every step separates. No fill
  can be told apart from white above step 28.
- **Near black every step separates** at every backlight level.
- **Reflection on the glass sets the black level in room light:** 91 % of the measured black at backlight 16, 99.5 %
  at backlight 1. Without it the panel reaches about 900:1 or more.
- **Uneven field:** about 2:1 brighter bottom right than top left (panel or viewing angle).
- **Light palette on the panel** (text 4.5:1, outline 3:1 required):

| Pair                       | BL16 | BL8  | BL4  | BL1 |
| -------------------------- | ---- | ---- | ---- | --- |
| Black text on white bubble | 16.9 | 14.4 | 11.2 | 5.3 |
| Muted on slate background  | 6.6  | 6.3  | 5.4  | 3.3 |
| White on header red        | 6.4  | 6.0  | 5.4  | 3.6 |
| 3 px outline vs background | 11.8 | 10.6 | 8.3  | 4.1 |

- **Recommendation:** backlight 4 or higher in room light; at 1 only muted text and the header fall short, and only
  with room light.

Palette table and panel notes: `docs/plan-tdeck-theme-20261107.md`. The analysis ran outside the repo (rawpy,
linear decode, homography registration, flat-field correction); the pattern command is `--tdpattern` in INSTRUMENT
builds.
