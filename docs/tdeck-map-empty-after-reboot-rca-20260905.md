# TD-15: T-Deck map shows only one or two stations after a reboot -- RCA 2026-09-05

Operator report: only DL3NCU-1 (and the own node) on the map, while the mheard list of the
gateway next to it shows DL2JA-1/-2, DG7RJ-8, DF2SI-12, DB0ED-99 with coordinates.

## Root cause

Map markers are RAM-only and come from one frame type.

- `tdeck_add_pos_point()` is called only for `MSG_TYPE_POSITION` frames
  (`src/lora_functions.cpp:1263-1283`). HEY frames (`@`) carry a position too -- that is where
  the mheard lat/lon of the neighbours come from (`mheard_functions.cpp:844`) -- but the map
  ignores them.
- `savePosPersistence()` / `loadPosPersistence()` (`lv_obj_functions.cpp:3860-3960`) persist
  the POS table rows (`position_ta`) to `/pos.dat`, not the marker arrays
  (`map_pos_call/lat/lon`). After a boot `loadPosPersistence()` refills the table only;
  `refresh_map()` iterates `map_pos_call[]`, which is empty until new position frames arrive.
- Position beacons run at the 30 min POSINFO interval. DK5EN-14 was rebooted about ten times
  today (flashing, every port open resets the chip), so the map was repeatedly reset to
  "whoever beaconed since the last boot". The 12:52 boot log shows four position frames in the
  first seven minutes (DL2JA-2, DB0HOB-12, DL2JA-1, DB0ED-99) against dozens of HEYs.

Not a regression: none of today's changes touch this path. It is the pre-existing behaviour,
made visible by the reboot cadence of a bench day.

## Fix candidates (not implemented)

1. Feed the map from HEY frames as well: call `tdeck_add_pos_point()` for `@` frames with a
   valid position, same guard as the mheard update. Cheapest, largest effect (HEYs every few
   minutes).
2. Rebuild markers from the persisted POS table at boot: after `loadPosPersistence()` parse the
   table rows (call, lat/lon text) back into `map_pos_*` and call `refresh_map()` once the map
   tab is first shown. Note the table stores a formatted `%.2lf` text, so restored markers are
   accurate to ~1 km; storing the raw doubles alongside would fix that.
