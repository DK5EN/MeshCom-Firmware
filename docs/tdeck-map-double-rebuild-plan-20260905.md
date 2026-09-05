# TD-14: map recomposed twice when the map tab is picked from the tab bar -- implementation plan

Status: plan, not implemented. Branch: `fork-main`. Scope: `src/t-deck/` only.

## Symptom

Picking the map tab from the tab bar costs two full SD map recompositions back to back,
each 470-750 ms with the main loop blocked (cursor and touch frozen): first for the view with
the bar still visible (294x140 px), then for the view after the bar collapsed (294x182 px).
Evidence: `roll_msg.log` 11:33:58.999 and 11:33:59.642, nothing between the two
`[ SDMAP ]...Karte zusammengesetzt` lines.

## What is known

- `tabview_event_cb()` case 3 (`src/t-deck/event_functions.cpp:918-934`) calls
  `sdmap_refresh()` while the tab bar is still visible, then `refresh_map()`, and only at the
  end `tdeck_hide_tab_menu()`. That order alone explains the 140 px rebuild.
- `sdmap_refresh()` (`src/t-deck/tdeck_sdmap.cpp:291-299`) takes the viewport from
  `lv_obj_get_content_height(parent)` on every call and always re-reads and re-decodes the
  tiles; there is no "nothing changed" early return.
- The trigger of the second (182 px) rebuild is not proven. `tdeck_set_tab_menu_visible()`
  only toggles the HIDDEN flag; `lv_tabview` in this tree sends `LV_EVENT_VALUE_CHANGED` only
  for a new tab (`lib/lvgl/.../lv_tabview.c:351`). Candidates: the 30 s tile-boundary check in
  `esp32_main.cpp:3246` landing right after the switch, or a second `VALUE_CHANGED` from the
  content relayout. Step 1 settles it.

## Steps

1. **Attribute every rebuild.** Add a `const char *why` argument to `sdmap_refresh()` (or a
   file-static tag set by each caller) and print it in the `Karte zusammengesetzt` line
   (`from=tab|zoom|pan|recenter|setmap|beacon|boundary`). Reproduce the tab-bar pick on
   DK5EN-14 once; the log then names the second caller. No behaviour change.
2. **Hide the bar before the map is built.** In `tabview_event_cb()` case 3 move
   `tdeck_hide_tab_menu()` (or an explicit `tdeck_set_tab_menu_visible(false)` +
   `lv_obj_update_layout()`) ahead of `sdmap_refresh()`, so the first rebuild already uses the
   182 px viewport. The generic `tdeck_hide_tab_menu()` at the end of the callback stays for the
   other tabs.
3. **Dedupe guard in `sdmap_refresh()`.** Early-return when zoom, centre tile, sub-tile offset
   and viewport size equal the previous call and a composed bitmap exists. This removes the
   second rebuild whatever its caller is, and also absorbs the duplicate that step 1 may reveal
   (boundary check right after a switch). Keep a `force` path for `set_map()` (tile set
   changed) and for `--mapzoom`.
4. **Bench.** Extend `tdeck_harness.py --scenario msg_roll --msgroll-tab 3` with a tab-bar pick
   step: `--drawer on`, `--ball` the cursor onto the map tab button, `--ball click 1`, and
   assert `sdmap_rebuilds_total_ms` grows by exactly one entry. Baseline on the unfixed build
   must show two.
5. **Docs.** BACKLOG TD-14 status, CHANGELOG item, German PR text (two small hunks in
   `event_functions.cpp` and `tdeck_sdmap.cpp`, upstream candidate).

## Out of scope

The tile cache itself (TD-09): after this plan a tab-bar pick still costs one rebuild, zoom and
pan still cost one each. That is the next item, not this one.
