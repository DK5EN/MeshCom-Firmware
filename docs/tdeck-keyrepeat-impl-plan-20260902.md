# Implementation plan TD-10 — T-Deck key auto-repeat (Backspace, Space, alpha keys)

**Status: APPROVED 2026-09-02, not started.** Supersedes the "parked" state of
[`tdeck-backspace-autorepeat-20260831.md`](tdeck-backspace-autorepeat-20260831.md)
(concept, §1–§6 still valid). Operator extended the scope on 2026-09-02 from Backspace
to Backspace + Space + the alpha keys. Execution with `/orchestrate-waves` in its own
worktree. Backlog row: [`BACKLOG.md`](BACKLOG.md) TD-10. Code sites verified against
`v4.35p_prio` @ `16c0733f`; `src/t-deck/` is byte-identical to `upstream/dev` @
`6a613547`.

## 1. Decisions (operator, 2026-09-02)

| Topic          | Decision                                                                                                                                                                            |
| -------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Worktree base  | `v4.35p_prio`. PR cut afterwards as a patch onto `upstream/dev` (§8).                                                                                                               |
| Keys           | Backspace, Space, every alpha key **including its shifted / SYM / numeric remap** (the repeat re-sends the already-remapped key). Enter and every SYM special combo never repeat.   |
| Old keyboards  | **Ship with runtime degradation.** The keyboard firmware's raw mode exists only since LilyGo commit `1eb6fb0e` (2025-06-11). Keyboards without it behave exactly as today.          |
| Bench          | Operator runs the proof himself later, from the written procedure in §7. The code waves do not wait for it.                                                                         |
| Repeat cadence | LVGL defaults: 400 ms delay, 100 ms rate (`lib/lvgl/src/hal/lv_hal_indev.h:37,41`). No override in `tdeck_main.cpp:505-515`; a follow-up may tune it once the operator has felt it. |

## 2. Protocol facts (from `Xinyuan-LilyGO/T-Deck` `examples/Keyboard_ESP32C3/Keyboard_ESP32C3.ino` @ master, read 2026-09-02)

The scout could not find the keyboard source locally; it was fetched from GitHub and
read line by line. This section is the source of truth for the implementer.

- I2C slave `0x55`, host side `Wire` on SDA 18 / SCL 8 (`variants/t_deck/configuration.h:101-102`).
- Commands (`onReceive`): `0x01 <duty>` backlight (already used by
  `setKeyboardBacklight()`, `src/t-deck/tdeck_helpers.cpp:138-144`), `0x02 <duty>`
  Alt+B default duty, **`0x03` raw mode on, `0x04` key mode off**. Unknown commands hit
  `default: break;` — an old firmware ignores `0x03`/`0x04` silently.
- `onRequest` in raw mode writes **5 bytes, one per column `col = 0..4`**, each
  `val |= (lastValue[col][row] << row)` for `row = 0..6`. `lastValue` is
  `digitalRead(row) == LOW` with the column driven low, so **bit set = key pressed
  (active-high in the frame)**. In key mode it writes one byte: the pending char or
  `0x00`.
- Matrix (`keyboard[col][row]`): col 0 = `q w SYM a ALT SPACE MIC`, col 1 =
  `e s d p x z LSHIFT`, col 2 = `r g t RSHIFT v c f`, col 3 = `u h y ENTER b n j`,
  col 4 = `o l i BACKSPACE $ m k`. So **Backspace = byte 4 bit 3 (0x08), Space = byte 0
  bit 5 (0x20), Enter = byte 3 bit 3, SYM = byte 0 bit 2, ALT = byte 0 bit 4, shifts =
  byte 1 bit 6 / byte 2 bit 3.**
- The keyboard MCU keeps scanning (`readMatrix()`, ~35 ms per full scan because of the
  1 ms settle per cell) and keeps composing chars while in raw mode; a char pressed
  during the raw window is delivered on the first key-mode request after `0x04`.
- Old firmware answering a 5-byte request: it writes one byte, the ESP32 master reads
  the rest as `0xFF` (SDA idle high). A first frame like `00 FF FF FF FF` therefore
  marks "unsupported"; so does an all-zero frame (finger already lifted, or no
  response) — in that case the probe is retried on the next eligible key press, at
  most `KBD_RAW_PROBE_MAX` (3) times before the feature is marked unsupported for this
  boot.

## 3. Design

Everything hangs on `keypad_read()` (`src/t-deck/tdeck_main.cpp:782-1011`), LVGL's
keypad read callback (30 ms period, `src/t-deck/lv_conf.h:84`). Today it reports
`LV_INDEV_STATE_PR` in exactly the poll where a byte arrived (`:1002`) and
`LV_INDEV_STATE_REL` in the next one (`:1007`), so LVGL's repeat branch
(`lib/lvgl/src/core/lv_indev.c:471-513`, catch-all `lv_group_send_data(g, data->key)`
at `:511-513` — printable keys included, textarea inserts them via
`lv_textarea.c:896-897`, Backspace deletes via `:886-887`) is never reached. The whole
feature is: keep reporting `PR` with the same `data->key` while the matrix still shows
the key down.

### 3.1 Pure state machine — `src/t-deck/kbd_repeat.h` (header-only, no Arduino)

```c
#define KBD_RAW_FRAME_LEN     5
#define KBD_RAW_TIMEOUT_MS    5000
#define KBD_RAW_PROBE_MAX     3
#define KBD_RAW_MAX_BITS      3      /* key + up to two modifiers */

enum kbd_raw_support { KBD_RAW_UNKNOWN = 0, KBD_RAW_YES, KBD_RAW_NO };

struct KbdRepeat {
    uint8_t  mask[KBD_RAW_FRAME_LEN];
    uint32_t since_ms;
    uint32_t key;          /* the remapped act_key being repeated */
    uint8_t  support;      /* enum kbd_raw_support */
    uint8_t  probes;
    bool     active;
};

/* frame plausibility: 1..KBD_RAW_MAX_BITS bits set, no 0xFF byte */
static inline bool kbdRawFrameValid(const uint8_t *f);
/* first frame after a key: arm the window; returns false when the frame disqualifies it */
static inline bool kbdRepeatArm(struct KbdRepeat *s, const uint8_t *f, uint32_t key, uint32_t now);
/* subsequent frames: true while (f & mask) == mask and now - since < timeout */
static inline bool kbdRepeatHold(struct KbdRepeat *s, const uint8_t *f, uint32_t now);
static inline void kbdRepeatClear(struct KbdRepeat *s);
```

`kbdRepeatArm` sets `support = KBD_RAW_YES` on the first valid frame; an invalid frame
increments `probes`, and `probes >= KBD_RAW_PROBE_MAX` sets `support = KBD_RAW_NO`.
No I2C, no `millis()` inside — the caller passes `now`. This is what the native test
exercises.

### 3.2 Eligibility — which key presses open a raw window

Evaluated after the existing remaps and specials in `keypad_read()`, right before
`last_key = act_key` (`:994`):

- `!bSPEC && !meshcom_settings.node_keyboardlock` (the branch that reports `PR` today),
- `act_key != 0x0D` (Enter — the C3 sends `0x0D`, not `LV_KEY_ENTER`; repeating it
  would insert CRs), `act_key != 0x0C` (Alt+C), `act_key != 0x00`,
- not `0x2e` when `lv_tabview_get_tab_act(tv)` is neither 1 nor 7 (SYM+M mute toggles
  without setting `bSPEC`, `:984-991`),
- `s.support != KBD_RAW_NO`.

Map-tab keys (TD-08, `:796-831`) and the SYM combos `0x22/0x27/0x2b/0x2d/0x21` set
`bSPEC` and are excluded by the first rule. The value repeated is `act_key` **after**
the `iKeyBoardType` remap (`:839-925`), so upper / numeric / symbol modes repeat what
they inserted; a mode change mid-hold keeps the old char (accepted).

### 3.3 I2C helpers — `tdeck_main.cpp`, next to `keypad_get_key()` (`:749`)

```c
static void     kbdRawMode(bool on);                 /* beginTransmission(0x55); write(on ? 0x03 : 0x04); endTransmission(); */
static bool     kbdRawRead(uint8_t *frame);          /* requestFrom(0x55, 5); false unless exactly 5 bytes arrived */
```

Same pattern as `setKeyboardBacklight()`. Every `kbdRawMode(true)` is paired with a
`kbdRawMode(false)` on every exit path (hold ends, timeout, read failure, invalid
first frame, `node_keyboardlock` set while holding).

### 3.4 Flow inside `keypad_read()`

```
if (s.active) {                                  // a hold is in progress
    uint8_t f[5];
    bool ok = kbdRawRead(f) && kbdRepeatHold(&s, f, millis()) && !node_keyboardlock;
    if (ok) { data->state = PR; data->key = s.key; return; }
    kbdRawMode(false); logRepeatEnd(); kbdRepeatClear(&s);
    data->state = REL; data->key = last_key; return;
}
act_key = keypad_get_key();                      // unchanged path
... existing remaps / specials ...
if (eligible(act_key)) {
    uint8_t f[5];
    kbdRawMode(true);
    if (kbdRawRead(f) && kbdRepeatArm(&s, f, act_key, millis())) { /* window armed */ }
    else kbdRawMode(false);                      // degrade: one char per press, as today
    logProbeOnce(f, s.support);
}
last_key = act_key; data->state = PR (or REL as today); data->key = last_key;
```

The PR reported in the arming poll is the same PR LVGL gets today, so the first
character is inserted exactly once; LVGL then sees consecutive PRs and starts
repeating after 400 ms.

### 3.5 Log lines (raw `Serial.printf`, survive `--debug off`, one line each)

- `[KBD];rawprobe;<b0> <b1> <b2> <b3> <b4>;key;<hex>;support;<0|1>` — once per boot,
  on the first probe result. This is the operator's evidence that the keyboard firmware
  has raw mode (or not).
- `[KBD];repeat;key;<hex>;held_ms;<n>;reason;<release|timeout|lock|i2c>` — at the end of
  every hold. `held_ms` ≥ 400 proves LVGL had time to repeat.

The existing `[KEY];<hex>;ms;…;src;kbd` line (`:774`) stays; during a hold no new
`[KEY]` lines appear because LVGL, not the keyboard, generates the repeats.

### 3.6 What does not change

`keypad_get_key()`, the debug key-inject ring (`:672-682`), the TD-08 map dispatch,
`setKeyboardBacklight()`, the LVGL group / focus handling in `event_functions.cpp`,
the touch path. Boards other than T-Deck / T-Deck Plus do not compile
`src/t-deck/` at all.

## 4. File ownership and waves

Worktree: `/Users/martinwerner/WebDev/mc-keyrepeat`, branch
`feat-tdeck-keyrepeat-20260902`, pinned to the `v4.35p_prio` SHA carrying this plan.

### Wave 1 — one implementer (Sonnet/high)

Exclusive: `src/t-deck/tdeck_main.cpp`, `src/t-deck/kbd_repeat.h`,
`test/test_kbd_repeat/test_main.cpp`, `platformio.ini` (only `[env:native]`
`test_filter` — `kbd_repeat.h` is header-only, nothing to add to `build_src_filter`).
Verification: `pio test -e native -f test_kbd_repeat`, `pio run -e t_deck_plus`,
`pio run -e t_deck`, `grep -c 'kbdRawMode(false)' src/t-deck/tdeck_main.cpp` ≥ number
of `kbdRawMode(true)` exit paths named in §3.3, `git status --porcelain` shows only the
four files.

**Gate (orchestrator):** diff read; all native envs; `t_deck`, `t_deck_plus`, plus
`heltec_wifi_lora_32_V3` and `wiscore_rak4631` to prove nothing leaks outside
`src/t-deck/`; `tools/ram_snapshot.py` against `tools/resource_baseline.json`
(expected delta: ≤ 16 B DRAM on the two T-Deck envs, 0 elsewhere). Commit.

### Wave 2 — `/fable-review`, fix wave if needed, commit.

### Wave 3 — docs (one implementer) and PR text

Exclusive: `docs/tdeck-backspace-autorepeat-20260831.md` (status line → implemented,
pointer here), `docs/BACKLOG.md` TD-10 row, `docs/CHANGELOG-stability.md`, this plan's
status line. PR text German, `docs/pr-tdeck-keyrepeat-draft-20260902.md` via
`/submit-pr --dry-run`, marked "Bench-Nachweis durch Operator ausstehend" until §7 is
done.

## 5. Tests (native, `test/test_kbd_repeat`)

1. `kbdRawFrameValid`: `00 00 00 00 08` (Backspace) valid; `20 00 00 00 00` (Space)
   valid; `04 00 00 00 08` (SYM+Backspace) valid; `00 FF FF FF FF` invalid;
   `00 00 00 00 00` invalid; `FF FF FF FF FF` invalid; four bits set invalid.
2. Arm + hold: arm with Backspace frame, hold returns true for the same frame at
   `now + 100`, true when an extra modifier bit appears, false when the Backspace bit
   drops.
3. Timeout: hold returns false at `since + KBD_RAW_TIMEOUT_MS`.
4. Degradation: three invalid arms → `support == KBD_RAW_NO`; one valid arm →
   `KBD_RAW_YES` immediately.
5. `kbdRepeatClear` leaves `support` untouched (the decision is per boot, not per hold).

Fails-before: the header does not exist, every case fails to compile — the behavioural
fails-before is the bench (§7), where today's firmware deletes one character per press.

## 6. Risks

- **Raw mode absent on DK5EN-14.** Then the probe line shows `support;0` and the node
  behaves as before; the feature is still correct for newer keyboards. The concept's
  "backlight works, so raw mode works" inference is **wrong** (`0x01` dates from
  2024-12-25, `0x03` from 2025-06-11) and is corrected in the concept doc in Wave 3.
- **I2C on the shared bus.** The 5-byte request runs in `lv_task_handler()` context on
  the main loop, like today's 1-byte request; the touch controller shares `Wire`. No
  new bus contention beyond one extra transaction per poll during a hold.
- **Stale char after `0x04`.** A key pressed during the hold arrives one poll after the
  window closes — accepted (concept §5).
- **Runaway repeat.** Bounded by the 5 s watchdog and by the mask check on every poll;
  a lost I2C read closes the window immediately.

## 7. Bench procedure for the operator (DK5EN-14, `/dev/cu.usbmodem1101`)

1. Flash: `pio run -d /Users/martinwerner/WebDev/mc-keyrepeat -e t_deck_plus -t upload`
   (port opens reboot the node; that is expected).
2. Open the serial log (`tools/serial_monitor.py` or `tools/bench/serial_session.py`),
   `--debug off` is fine — the `[KBD]` lines are unconditional.
3. Go to the message tab (tab 1), tap the text field, type `abc`.
4. **Hold Backspace ~2 s.** Expected: all three characters disappear; log shows
   `[KBD];rawprobe;… ;support;1` once and `[KBD];repeat;key;08;held_ms;≥1500;reason;release`.
   If the probe line says `support;0`, note the five bytes and stop — the keyboard
   firmware has no raw mode; the node behaves as before.
5. **Hold `a` ~1 s** → a run of `a`; **hold Space** → a run of spaces; switch to
   `ABC` mode via the on-screen button and hold `a` → run of `A`.
6. **Hold Enter** → exactly one Enter (no repeat line for key `0d`).
7. **SYM+K** (keyboard lock) still toggles; while locked, holding a key does nothing
   and no `[KBD];repeat` line appears.
8. Type a normal sentence at speed: no dropped or doubled characters.
9. Paste the `[KBD]` lines into the TD-10 backlog row; Wave 3's PR draft gets the
   sentence "Bench-Nachweis erbracht am <Datum>" or the `support;0` finding.

## 8. PR cut

`git checkout -b pr-tdeck-keyrepeat-<date> upstream/dev && git diff v4.35p_prio...feat-tdeck-keyrepeat-20260902 -- src | git apply --3way`;
`git diff upstream/dev --stat` must list exactly `src/t-deck/tdeck_main.cpp` and
`src/t-deck/kbd_repeat.h`. Native test and `platformio.ini` stay in the fork.
