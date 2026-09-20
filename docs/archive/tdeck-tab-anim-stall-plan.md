# T-Deck Plus: Tab switch stalls mid-animation after Send

Implementation plan, 2026-09-05. Branch: `fork-main`. Scope: firmware only, `src/t-deck/`.

## 1. Defect

After sending a message the display does not return cleanly to the message tab. The
screen freezes on an intermediate frame: the message list (tab 0) is visible on the left,
the input page (tab 1, "Type Message" and "To Call or Group") on the right, both shifted by
roughly half the screen width. New messages keep rendering into the left half. Only a menu
tab change recovers the view.

## 2. Root cause

| Step | Location                                                | What happens                                                                                                                  |
| ---- | ------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| 1    | `src/t-deck/event_functions.cpp:734`                    | Send handler calls `lv_tabview_set_act(tv, 0, LV_ANIM_ON)`. Same pattern at `:674` (Save Setting).                            |
| 2    | `lib/lvgl/src/core/lv_obj_scroll.c:303`                 | LVGL starts a 200 to 400 ms horizontal scroll animation on the tabview content.                                               |
| 3    | `src/t-deck/lv_obj_functions.cpp:611`                   | The tabview content has `lv_obj_set_scroll_dir(..., LV_DIR_NONE)` to block swiping.                                           |
| 4    | `lib/lvgl/src/widgets/lv_btn.c:67`, `lv_textarea.c:834` | Every button and textarea carries `LV_OBJ_FLAG_SCROLL_ON_FOCUS` by default.                                                   |
| 5    | `lib/lvgl/src/core/lv_indev.c:893`, `:1066`             | A pointer press on a focusable widget other than the last pressed one focuses it while the indev state is still PRESSED.      |
| 6    | `lib/lvgl/src/core/lv_obj.c:803`                        | FOCUSED plus SCROLL_ON_FOCUS runs `lv_obj_scroll_to_view_recursive` up the parent chain, reaching the tabview content.        |
| 7    | `lib/lvgl/src/core/lv_obj_scroll.c:784`                 | `scroll_area_into_view` deletes the running scroll animation unconditionally and sends SCROLL_END.                            |
| 8    | `lib/lvgl/src/core/lv_obj_scroll.c:795`                 | The replacement scroll is zeroed because of `LV_DIR_NONE`. The content stays exactly where the animation stood.               |
| 9    | `lib/lvgl/src/extra/widgets/tabview/lv_tabview.c:327`   | The tabview's SCROLL_END rescue (`set_act` again) returns early because the active indev is pressed. Nothing ever re-scrolls. |

Trigger condition: a second touch or trackball click within the animation window, landing on a
focusable widget that differs from the previous press target. Because the page moves under the
finger, the "To Call or Group" textarea slides under the Send button position after about 30 px
of travel, so a double tap, touch bounce or trackball switch bounce on Send is enough. Touch and
trackball are separate indevs with separate `last_pressed`, so Send via trackball followed by a
touch also qualifies.

Ruled out: lost TFT flush after SD access (NOP mitigation is on by default,
`src/t-deck/tdeck_debug.cpp:135`); blocked main loop (LVGL animations are time based and jump
to the end after a pause).

## 3. Fix

### 3.1 Code change (minimal)

Replace `LV_ANIM_ON` with `LV_ANIM_OFF` at the two remaining animated tab switches:

- `src/t-deck/event_functions.cpp:734` (Send handler, both the accepted and refused send path
  reach this line)
- `src/t-deck/event_functions.cpp:674` (Save Setting handler)

Every other `lv_tabview_set_act` call in the fork already uses `LV_ANIM_OFF`
(`lv_obj_functions.cpp:335`, `:1883`, `:3267`, `:3294`, `:4194`, `tdeck_main.cpp:278`,
`tdeck_debug.cpp:387`). With an immediate jump there is no animation to kill and no window for
the stall.

Side effect to keep in mind: `lv_tabview_set_act` does not emit `LV_EVENT_VALUE_CHANGED`. The
fork's `tabview_event_cb` (`event_functions.cpp:884`) is only reached through the tabview's own
SCROLL_END handler. With `LV_ANIM_OFF` the SCROLL_END still fires synchronously inside
`lv_obj_scroll_by`, so the `msg_controls` hide/show logic at `:934` behaves as before. No
change needed there.

### 3.2 Rejected alternatives

- Keep the animation and clear `LV_OBJ_FLAG_SCROLL_ON_FOCUS` on `text_input`, `dm_callsign`
  and the three buttons in `msg_controls`. Closes the input-page path but not the message-page
  path (bubble delete buttons, msg tab bar entries both carry the flag). More code, partial fix.
- Restore `LV_DIR_HOR` on the tabview content so the replacement scroll can run. Re-enables
  accidental swiping between tabs, which the fork deliberately blocked.
- Patch `lv_tabview.c` or `lv_obj_scroll.c`. Touching the vendored LVGL tree is out of scope
  for an upstream PR.

## 4. Verification

Hardware: T-Deck Plus DK5EN-14 on `/dev/cu.usbmodem1101` (port open reboots the node, see
memory note). Build env: `pio run -e t_deck_plus` (envs in
`variants/`).

1. Build, flash, confirm boot.
2. Reproduce first on the unfixed build to prove the instrument: type a message, tap Send, and
   within about 300 ms tap again on the upper right (the moving textarea) or on the Send
   position. Expect the split screen. Repeat 10 times, record how many stall.
3. Flash the fixed build, repeat the same 10 double taps. Expect 0 stalls, message tab fully
   shown each time.
4. Regression checks:
   - Send with trackball click, then touch immediately.
   - Refused send (BP-07/BP-09 QRT/QTA path): the tab still switches and the "NOT SENT" bubble
     is visible.
   - Save Setting on the SET tab returns to the message tab, settings persist after reboot.
   - Keyboard repeat still ends on refocus (K5 path in `tdeck_main.cpp:855` is unaffected).
5. Bench build gate on the other T-Deck envs (`t_deck`, `t_deck_pro`) plus one ESP32 and the
   RAK4631 env to confirm nothing else references the changed lines.

No unit test exists for the LVGL layer (`env:native` does not compile `src/t-deck/`). The
regression proof is the 10x manual double-tap protocol above; record the counts in the PR text.

## 5. Delivery

- One commit on `fork-main`:
  `fix(tdeck): switch tabs without animation after Send and Save Setting (TD-xx)`
- Assign the next free TD number in `docs/08-defect-catalogue.md` and add the entry (both the
  STATUS box and the wave table, per the docs-per-wave rule).
- Add a line to `release-notes.md` under the unreleased section.
- PR against upstream `dev` with the German description: files, the two changed lines, the
  causal chain from section 2, and the manual test counts. Kurt reviews and merges.

## 6. Open questions

- Which firmware version was on the photographed device? If it predates the flushfix default,
  a second stall path (lost flush) may coexist. Check the boot banner before flashing.
- Whether to also drop the `LV_ANIM_ON` scroll on `msg_list` (`lv_obj_functions.cpp:2926`,
  `:2997`). Those animate a different object and cannot reach the tabview content, so they are
  left alone in this plan.
