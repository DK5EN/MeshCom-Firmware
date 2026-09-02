# Implementation plan TD-10 — T-Deck key auto-repeat (Backspace, Space, alpha keys)

**Status: IMPLEMENTED 2026-09-02 (wave 1 + review fixes K1–K7), operator bench
§7 pending.** Supersedes the "parked" state of
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
- **Old firmware answering a 5-byte request (corrected during review — the 0xFF
  assumption below was wrong and is gone from the design):** on the ESP32 Arduino
  core, `Wire.requestFrom(0x55, 5)` returns exactly 5 bytes or 0, never a short read
  (`esp32-hal-i2c.c:205-210`). A key-mode slave has already written its one pending
  byte for the plain 1-byte poll, and its I2C TX FIFO is reset on every STOP, so the
  5-byte raw-mode request comes back as `00 00 00 00 00` — not `0xFF`-padded; `0xFF`
  padding never occurs on the wire and is only ever synthesised locally as the
  stand-in for a 0-byte (failed) read (§3). An old keyboard therefore reads as an
  **inconclusive** all-zero frame: `support` stays `KBD_RAW_UNKNOWN` for the whole
  boot (it is not counted as a probe failure and never regresses to `KBD_RAW_NO`),
  the probe is re-attempted on every eligible key press at a cost of one extra ~1 ms
  I2C transaction, and the arm never succeeds (an all-zero frame can never carry the
  expected bit), so the node's behaviour is unchanged: exactly one character per key
  press, no functional regression on old hardware.

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

As built, with two additions the original draft did not have: `kbdExpectedCell()`
(the K1 fix, §3.2 below) and a `kbd_hold` result enum so the reason ladder in
`keypad_read()` does not have to re-derive "timeout vs. released" from raw booleans
(K7).

```c
#define KBD_RAW_FRAME_LEN     5
#define KBD_RAW_TIMEOUT_MS    5000
#define KBD_RAW_PROBE_MAX     3
#define KBD_RAW_MAX_BITS      3      /* key + up to two modifiers */

enum kbd_raw_support { KBD_RAW_UNKNOWN = 0, KBD_RAW_YES, KBD_RAW_NO };
enum kbd_hold         { KBD_HOLD_ACTIVE = 0, KBD_HOLD_RELEASED, KBD_HOLD_TIMEOUT };

/* uint32_t members first, pointer next, bytes last: 20 B on the 32-bit target. */
struct KbdRepeat {
    uint32_t since_ms;
    uint32_t key;          /* the remapped act_key being repeated */
    void    *focus;        /* lv_obj_t* focused at arm time (K5); NULL on native */
    uint8_t  mask[KBD_RAW_FRAME_LEN];
    uint8_t  support;      /* enum kbd_raw_support */
    uint8_t  probes;
    bool     active;
};

/* Maps a delivered char byte back to the physical (col,row) that produced it —
 * the K1 fix; see the tables and rules in §3.2. */
static inline bool kbdExpectedCell(uint8_t ch, uint8_t *col, uint8_t *row);
/* frame plausibility: 1..KBD_RAW_MAX_BITS bits set, every byte <= 0x7F */
static inline bool kbdRawFrameValid(const uint8_t *f);
/* first frame after a key: arm only if valid AND (col,row) is set (K1) */
static inline bool kbdRepeatArm(struct KbdRepeat *s, const uint8_t *f, uint32_t key,
                                uint8_t col, uint8_t row, uint32_t now);
/* subsequent frames: ACTIVE while (f & mask) == mask and now - since < timeout */
static inline enum kbd_hold kbdRepeatHold(struct KbdRepeat *s, const uint8_t *f, uint32_t now);
static inline void kbdRepeatClear(struct KbdRepeat *s);
```

`kbdRepeatArm` sets `support = KBD_RAW_YES` and resets `probes = 0` on a successful
arm (K3a: a good arm forgives earlier scattered failures so they can never add up to
`KBD_RAW_NO`). A rejected frame is either **inconclusive** (all-zero — see §3.2, not
counted) or a **probe failure** (`probes++`); once `probes >= KBD_RAW_PROBE_MAX` (3),
`support` becomes `KBD_RAW_NO` — but only while `support != KBD_RAW_YES` (K3b: once a
keyboard has proven itself, a later ghosted or wrong-cell frame only declines that one
arm, it never regresses the boot's verdict back down). No I2C, no `millis()` inside —
the caller passes `now`. This is what the native test exercises (39 cases, §5).

### 3.2 Expected-cell check (K1) and probe rules

`kbdRawFrameValid` alone is not enough: on a fast key-to-key transition the _previous_
key's frame can still look plausible (1-3 bits, no byte > 0x7F) while showing a
completely different cell, which would arm the repeat for the wrong key. The fix
transcribes the LilyGo matrix — `KBD_BASE[5][7]` and `KBD_SYM[5][7]` in
`kbd_repeat.h`, straight from `keyboard[col][row]` / `keyboard_symbol[col][row]` in
`Keyboard_ESP32C3.ino` — and `kbdExpectedCell()` maps the **pre-remap** byte
`keypad_get_key()` returned (before `iKeyBoardType` upper/numeric/symbol remapping)
back to `(col, row)`: Backspace and Space have no character cell and are
special-cased (`4/3`, `0/5`); an uppercase letter folds to its lowercase base cell;
everything else is looked up in `KBD_BASE` then `KBD_SYM`. `kbdRepeatArm` only arms
when that bit is actually set in the frame (`f[col] & (1 << row)`).

Probe outcome per frame, in order:

1. **All-zero frame → inconclusive, no penalty.** The char can arrive up to one LVGL
   poll plus one C3 matrix scan (~65 ms) after the physical press, so a quick tap is
   often already released by the time the first raw frame is read — and, per the
   corrected §2, a key-mode (old-firmware) slave answers a 5-byte request with five
   zeros as well. Neither case may exhaust the probe budget.
2. **A byte > 0x7F, or the frame fails the bit-count check, or the frame is non-zero
   but missing the expected bit → probe failure**, `probes++`.
3. **`probes >= KBD_RAW_PROBE_MAX` (3) → `support = KBD_RAW_NO`** — but only while
   `support != KBD_RAW_YES` already (K3b).
4. **Any frame that passes the expected-bit check → `support = KBD_RAW_YES`,
   `probes = 0`** (K3a), regardless of how many failures preceded it.

### 3.3 Eligibility — which key presses open a raw window

Evaluated after the existing remaps and specials in `keypad_read()`, right before
`last_key = act_key`:

- `!bSPEC && !meshcom_settings.node_keyboardlock` (the branch that reports `PR` today),
- `act_key != 0x00`, `act_key != 0x0D` (Enter — the C3 sends `0x0D`, not
  `LV_KEY_ENTER`; repeating it would insert CRs), `act_key != 0x0C` (Alt+C),
- not `0x2e` when `lv_tabview_get_tab_act(tv)` is neither 1 nor 7 (SYM+M mute toggles
  without setting `bSPEC`),
- `kbdExpectedCell()` on the **raw** (pre-remap) key returns a cell — Enter/Alt combos
  and non-ASCII bytes have none,
- `s.support != KBD_RAW_NO`.

Map-tab keys (TD-08) and the SYM combos `0x22/0x27/0x2b/0x2d/0x21` set `bSPEC` and are
excluded by the first rule. The value repeated (`s.key`) is `act_key` **after** the
`iKeyBoardType` remap, so upper / numeric / symbol modes repeat what they inserted; the
cell looked up for the K1 check is the **raw**, pre-remap key, matching what the
keyboard MCU's matrix actually reports. A mode change mid-hold keeps the old char
(accepted).

### 3.4 I2C helpers — `tdeck_main.cpp`, next to `keypad_get_key()`

```c
static void kbdRawMode(bool on);        /* beginTransmission(0x55); write(on ? 0x03 : 0x04); endTransmission(); */
static bool kbdRawRead(uint8_t *frame); /* requestFrom(0x55, 5); false unless exactly 5 bytes arrived */
```

Same pattern as `setKeyboardBacklight()`. Every `kbdRawMode(true)` is paired with a
`kbdRawMode(false)` on every exit path (hold ends, timeout, read failure, invalid
first frame, `node_keyboardlock` set while holding, focus change).

**Boot state (K2).** Nothing else in the C3 firmware or the host ever sends `0x04`
on its own; if the ESP32 resets (WDT, OTA) while the C3 is mid-hold and still
powered, it stays in raw mode across the reset, and a later plain 1-byte
`keypad_get_key()` poll would then read a column bitmask as if it were a character.
`setup()` calls `kbdRawMode(false)` once, immediately after `checkKb()` detects a
keyboard, to force key mode at every boot regardless of how the previous boot ended.

### 3.5 Flow inside `keypad_read()`

```
if (s.active) {                                  // a hold is in progress
    uint8_t f[5];
    bool i2c_ok = kbdRawRead(f);
    enum kbd_hold h = i2c_ok ? kbdRepeatHold(&s, f, millis()) : KBD_HOLD_RELEASED;
    bool locked = node_keyboardlock;
    bool refocused = (lv_group_get_focused(...) != s.focus);      // K5
    if (i2c_ok && h == ACTIVE && !locked && !refocused) {
        data->state = PR; data->key = s.key; return;
    }
    reason = !i2c_ok ? "i2c" : locked ? "lock" : refocused ? "focus"
           : h == TIMEOUT ? "timeout" : "release";
    kbdRawMode(false); logRepeatEnd(reason); kbdRepeatClear(&s);
    data->state = REL; data->key = last_key; return;
}
act_key = keypad_get_key();                      // unchanged path
... existing remaps / specials ...
if (eligible(raw_key, act_key)) {                // §3.3
    uint8_t f[5] = {0};
    kbdRawMode(true);
    if (!kbdRawRead(f)) memset(f, 0xFF, 5);       // failed read -> counts as a probe failure
    if (kbdRepeatArm(&s, f, act_key, col, row, millis()))
        s.focus = lv_group_get_focused(...);      // K5: remember for the hold
    else
        kbdRawMode(false);                        // degrade: one char per press, as today
    logProbeOnce(f, act_key, s.support);           // §3.6, capped
}
last_key = act_key; data->state = PR (or REL as today); data->key = last_key;
```

The PR reported in the arming poll is the same PR LVGL gets today, so the first
character is inserted exactly once; LVGL then sees consecutive PRs and starts
repeating after 400 ms. A `kbdRawRead()` failure (0-byte return, distinct from an
on-the-wire all-zero frame) is filled with `0xFF` bytes before being handed to
`kbdRepeatArm`, so it always counts as a probe failure rather than the inconclusive
all-zero case.

### 3.6 Log lines (raw `Serial.printf`, survive `--debug off`, one line each)

- `[KBD];rawprobe;<b0> <b1> <b2> <b3> <b4>;key;<hex>;support;<0|1|2>` — printed only
  while the verdict is still open (`support == KBD_RAW_UNKNOWN` _before_ the probe),
  capped at `KBD_RAW_PROBE_MAX + 2` (5) lines per boot so an old keyboard — whose
  verdict never closes (§2, §3.2) — cannot spam the log on every key press. Legend:
  `0` = unknown, `1` = yes (`KBD_RAW_YES`), `2` = no (`KBD_RAW_NO`). This is the
  operator's evidence that the keyboard firmware has raw mode (or not).
- `[KBD];repeat;key;<hex>;held_ms;<n>;reason;<release|timeout|lock|i2c|focus>` — at the
  end of every hold. `held_ms` ≥ 400 proves LVGL had time to repeat. `focus` (K5)
  means a touch moved keypad focus mid-hold; `i2c` means the raw-mode read failed.

The existing `[KEY];<hex>;ms;…;src;kbd` line stays; during a hold no new `[KEY]` lines
appear because LVGL, not the keyboard, generates the repeats.

### 3.7 What does not change

`keypad_get_key()`, the debug key-inject ring, the TD-08 map dispatch,
`setKeyboardBacklight()`, the LVGL group / focus handling in `event_functions.cpp`
(only read from, at arm and on every hold poll — K5), the touch path. Boards other
than T-Deck / T-Deck Plus do not compile `src/t-deck/` at all.

## 4. File ownership and waves

Worktree: `/Users/martinwerner/WebDev/mc-keyrepeat`, branch
`feat-tdeck-keyrepeat-20260902`, pinned to the `v4.35p_prio` SHA carrying this plan.

### Wave 1 — one implementer (Sonnet/high)

Exclusive: `src/t-deck/tdeck_main.cpp`, `src/t-deck/kbd_repeat.h`,
`test/test_kbd_repeat/test_main.cpp`, `platformio.ini` (only `[env:native]`
`test_filter` — `kbd_repeat.h` is header-only, nothing to add to `build_src_filter`).
Verification: `pio test -e native -f test_kbd_repeat`, `pio run -e t_deck_plus`,
`pio run -e t_deck`, `grep -c 'kbdRawMode(false)' src/t-deck/tdeck_main.cpp` ≥ number
of `kbdRawMode(true)` exit paths named in §3.4, `git status --porcelain` shows only the
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

### Review outcome

`/fable-review` findings and dispositions: [`review-verdict-tdeck-keyrepeat-20260902.md`](review-verdict-tdeck-keyrepeat-20260902.md)
(five finders, one adversarial verifier against the LilyGo keyboard source and the
ESP32 Arduino core 2.0.14 I2C HAL, reviewed at `1068edef`). Six findings (K1-K3, K5,
K7 plus the boot fix K2) landed in the fix wave folded into `src/t-deck/kbd_repeat.h`
and `src/t-deck/tdeck_main.cpp` and are reflected throughout §2-§3 above; seven other
claims were investigated and explicitly refuted or declined (listed in the verdict's
"Refuted or declined" section) and are not re-investigated.

## 5. Tests (native, `test/test_kbd_repeat/test_main.cpp`, 39 cases)

1. **Struct layout** (1 case): `sizeof(struct KbdRepeat) <= 24`.
2. **`kbdRawFrameValid`** (10 cases): single-bit Backspace/Space valid; two-bit
   SYM+Backspace valid; exactly `KBD_RAW_MAX_BITS` (3) bits valid; four bits across
   bytes invalid; old-firmware idle-high (`00 FF FF FF FF`) invalid; all-zero invalid;
   all-`0xFF` invalid; four bits in one byte invalid; bit 7 set invalid.
3. **`kbdExpectedCell`** (7 cases, K1): Backspace → 4/3, Space → 0/5, lowercase `a` →
   0/3, uppercase `A` folds to the same base cell, `#` from the symbol table (0/0),
   `$` from the base table (4/4), unknown bytes (`0x00`, Enter `0x0D`, Alt+C `0x0C`,
   non-ASCII `0xE4`) all rejected.
4. **Arm + hold** (5 cases): same-frame hold active; hold stays active when an extra
   modifier bit appears; hold releases when the key bit drops; hold releases when the
   armed bit moves to another column (finger rolled from Backspace onto Enter, one bit
   set either way); hold on an unarmed state returns `RELEASED`.
5. **Timeout** (4 cases): active one ms before the boundary; `TIMEOUT` exactly at
   `since + KBD_RAW_TIMEOUT_MS`; timeout wins over a simultaneously-released frame;
   `since_ms`/`now` survive a `millis()` rollover (`0xFFFFFF00` → `0x100`).
6. **Arm rejects the wrong cell** (3 cases, K1): a fast key-A-to-key-B transition does
   not bind A to B's bits and counts as a probe failure; an own-bit frame with an
   extra modifier (`A` + LSHIFT) still arms; a bit-7 frame is rejected and counted.
7. **Degradation and K3** (8 cases): three invalid (0xFF-padded) arms mark `NO`; one
   valid arm marks `YES` immediately; an all-zero frame does not count as a probe, not
   even across 20 in a row; three genuinely 0xFF-padded frames do mark `NO`; a valid
   arm after two failures still marks `YES`; a successful arm resets the probe counter
   to 0 (K3a); ten wrong-bit frames after a `YES` verdict never regress it back down
   (K3b).
8. **`kbdRepeatClear`** (1 case): leaves `support` (`YES`) untouched — the verdict is
   per boot, not per hold — while clearing `active`, `key`, `focus` and `mask`.

Fails-before: the header did not exist before the implementation wave, every case
failed to compile — the behavioural fails-before is the bench (§7), where the
unmodified firmware deletes one character per press with no repeat.

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
4. **Hold Backspace ~2 s.** Expected on a raw-mode keyboard: all three characters
   disappear; log shows `[KBD];rawprobe;… ;support;1` once (the frame's byte 4 has bit
   3 set) and `[KBD];repeat;key;08;held_ms;≥1500;reason;release`.

   **Negative result — old keyboard firmware.** The probe lines show
   `00 00 00 00 00;key;08;support;0` (up to `KBD_RAW_PROBE_MAX + 2` = 5 of them, one
   per Backspace press, never more per boot — see §3.2/§3.6) and `support` never
   becomes `1` or `2`; no `[KBD];repeat` line ever appears, because the raw window
   never arms. Nothing else changes: each hold still deletes exactly one character,
   matching today's firmware. Report this as the finding, verbatim, with the five
   bytes and the `support` value from the log — that is the expected, correct
   degradation, not a bug.

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
