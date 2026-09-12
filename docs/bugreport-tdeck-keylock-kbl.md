# Bug report: T-Deck keyboard backlight turns on while keylock is engaged

**Firmware:** MeshCom 4.35t (also present in upstream `dev`)
**Board:** LilyGO T-Deck / T-Deck Plus
**Date:** 2026-09-11
**Reporter:** field report via DK5EN

## Symptom

With the keylock engaged (SYM+K) and the keyboard backlight switched off, the
keyboard backlight comes back on every time a new message arrives. It goes off
again after the display idle timeout, then repeats with the next message.

## Steps to reproduce

1. Switch the keyboard backlight off (KBL button in the tab bar, or leave it off).
2. Press SYM+K to engage the keylock. Display and keyboard light go off.
3. Receive a message (group or DM).
4. Observe: the display wakes (intended) and the keyboard backlight lights up
   at level 150 (not intended).

## Root cause

`tft_on()` in `src/t-deck/lv_obj_functions.cpp` (line 2132) contains an
inverted condition. When the display wakes it turns the keyboard backlight on
if the keylock is **active**:

```c
// Force sync keyboard backlight
if (meshcom_settings.node_keyboardlock)
{
    if (bDEBUG)
        Serial.println("[TDECK]...tft_on: turn on keyboard backlight");

    // turn on keyboard backlight
    setKeyboardBacklight(150);
}
```

Before commit `583782a9b` ("v4.35p keyboard light switch", 2026-03-22) the code
read:

```c
if (!meshcom_settings.node_keyboardlock) {
    if (kbd_light_on)
    {
        setKeyboardBacklight(255);
        ...
```

The refactor flipped the sign and dropped the light-on check, so the keylock
flag now acts as a "keyboard light on" flag.

## Why it only shows on message arrival

All other wake sources are guarded in `src/t-deck/tdeck_main.cpp`:

| Wake source | Guard                                   | Line                      |
| ----------- | --------------------------------------- | ------------------------- |
| Keyboard    | `if(!node_keyboardlock) tft_on();`      | 1053                      |
| Trackball   | `if (!node_keyboardlock) tft_on();`     | 1270                      |
| Touch       | `if (!node_keyboardlock) { tft_on(); }` | 1394-1396                 |
| Message     | `tft_on();` unconditional               | lv_obj_functions.cpp 4184 |

With the keylock engaged, only the message path (`msg_focus_and_alert()`)
reaches `tft_on()`, and only then does the inverted branch fire. Waking the
display on a message while locked is intended; lighting the keyboard is not.

Sequence:

1. SYM+K sets `node_keyboardlock`, `tft_off()` runs and calls
   `setKeyboardBacklight(0)`.
2. Message arrives, `msg_focus_and_alert()` calls `tft_on()`, the inverted
   branch calls `setKeyboardBacklight(150)`.
3. Display idle timer expires, `tft_off()` switches the light off again.
4. Next message: repeat.

## The block is redundant

`tft_on()` calls `resetBrightness()` first, and `setBrightness()` in
`src/t-deck/tdeck_helpers.cpp` (line 111) already syncs the keyboard light from
the real setting:

```c
// Sync keyboard backlight if not locked
if(meshcom_settings.node_kbllightlock)
{
    uint8_t kbl_val = (value >= BRIGHTNESS_STEPS) ? 255 : (value * 16);
    setKeyboardBacklight(kbl_val);
}
```

So the operator's keyboard-light setting (`node_kbllightlock`) is honored by
`setBrightness()` regardless of the block in `tft_on()`.

## Proposed fix

Delete the block in `tft_on()` (lines 2131-2139 of
`src/t-deck/lv_obj_functions.cpp`). Alternatively, if a forced sync is wanted,
guard it with the correct flags:

```c
if (meshcom_settings.node_kbllightlock && !meshcom_settings.node_keyboardlock)
    setKeyboardBacklight(150);
```

Either variant keeps the keyboard dark while the keylock is engaged and leaves
the normal (unlocked) behaviour unchanged.

## Verification

- Keylock on, KBL off, message arrives: display wakes, keyboard stays dark.
- Keylock off, KBL on, message arrives: display and keyboard light both on.
- Keylock off, KBL off, message arrives: display on, keyboard dark.
- KBL toggle via tab-bar button still switches the light immediately.
