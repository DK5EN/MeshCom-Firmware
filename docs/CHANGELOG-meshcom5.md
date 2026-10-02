# MeshCom 5 topology changelog

Branch `feature-neighbour-matrix`, stages 1 to 3 of the topology concept
(`docs/meshcom5-topologie/`). Base: `fork-neo-test` at upstream `e4a2393f`. Stage 4 (automatic
via) is not included. Engineering record and measurements: `docs/meshcom5-campaign.md`.

## Upgrade note -- read before flashing

The boot migration of the old `node_sset4` bits is gone. A node that ran an earlier
neighbour-matrix build (before `ebcf7f0b`) reads its old NBR bits as KISS bits and comes up with
KISS/TCP on, KISS TX on and no auth. Right after flashing such a node, send:

```
--kiss tx off
--kiss meta off
--kiss off
```

and set `--nbrdebug`, `--nbrrelay` and `--nbrsym` again as wanted. Nodes that never ran the
neighbour matrix are not affected.

## One topology store instead of three tables

1. The MHeard table, the path table (`mheard_functions.cpp`) and the dense neighbour matrix are
   replaced by one store: the neighbour matrix with an edge pool (`src/nbr_matrix.*`), a query
   layer on top of it (`src/nbr_views.*`), direct-neighbour slots and a horizon table for senders
   beyond two hops. Web MHeard and path pages, `--mheard`, `--path`, `--neighbours`, the phone MH
   frames and the T-Deck and T-Deck Pro tables all read from it.
2. Capacity: 64 neighbours and 256 edges on classic ESP32, 128 neighbours and 512 edges on S3 and
   nRF52 (was 13 or 21 matrix rows, and 10 to 80 MHeard entries depending on the board). Every
   size is a per-family constant under `#ifndef` in `src/configuration_global.h`, so a board can
   be reduced with one `-D` if the heap gets tight in the field.
3. RAM is lower than before on every family despite the larger capacity (static stores incl.
   relay rings, bytes): classic 5,989 -> 5,848, E22_XML 9,599 -> 5,848, S3 14,823 -> 14,504,
   nRF52 16,575 -> 14,504.

## Behaviour changes on air

4. NCNT is now symmetric: a neighbour counts when I heard it in the last 60 min and it either
   heard me (within 12 h) or its link is strong enough to assume symmetry, and no complete HN
   report from it contradicts that. The value on air (`R<n>`, `/N`, HEY signal group, HN
   `R<heard>`, telemetry) is capped at 99; web and console show the uncapped value. Older
   firmware sends the one-sided MHeard count of the last hour, which is usually larger, so mixed
   networks show different numbers for the same site.
5. Coverage and relay decisions (`--nbrrelay count|on`) use counted edges: an edge covers only
   when its counter reaches 10 % of the strongest comparable edge (a single hit in 12 h no longer
   counts), counters halve every 90 min, and the SNR of a directly heard neighbour is the mean of
   the last 8 frames instead of the last value.
6. A frame from the server has its destination path reset to the destination before the via
   check, in both UDP handlers (ESP32 and nRF52). A via set in another region names nodes nobody
   here hears; only the node's own via (`node_via`) is applied afterwards.

## Phone app

7. The MH frame (`0x44`, `TYP "MH"`) keeps its 13 fields in the old order and appends 7 new ones:
   `AGE`, `HM` (heard me), `ROLE`, `EX`, `NB`, `GW`, `VIA`. The 244-byte limit (`BLE_JSON_PAYLOAD_MAX`) is unchanged; if a
   frame would be longer, the new fields drop first, so existing apps see no difference.
8. Live MH frames go out at most once per neighbour and minute. The list on connect is newest
   first and covers 12 h. No MH frame is sent without a valid clock, as before.

## Web, console, display

9. `--mheard` and `--path` print one `key=value` line per entry instead of the box table. The web
   path page is a real table with an age column.
10. The web info page shows every switch of the settings page, grouped as on the settings page,
    plus the NBR switches. KISS TX without auth is marked. A lint in the golden selftest fails
    when a settings switch is missing from the info page.
11. On nRF52, an unknown neighbour position shows as `NA` instead of `nan`.

## Persistence (T-Deck, T-Deck Pro)

12. With persist-to-SD on, the topology is saved to `/topo.dat` every 10 minutes and loaded at
    boot; a file written by another callsign is discarded. The old `/mheard.dat` and
    `/mhpath.dat` are deleted once. Bench: after a reboot the T-Deck Plus showed its neighbours
    and paths again within 78 s.

## Diagnostics

13. `--nbrcheck` checks the neighbour masks against the edge pool and prints
    `-> ok` or `-> INCONSISTENT`. With `--nbrdebug on` the same check logs a `[NBR]|CHECK` line
    once a minute.
14. New log lines `EVICT-E`, `EVICT-H`, `EVICT-X`, `ECHO`, `CHECK` and `DROP|SYMBUF` (nRF52);
    masks in `NEED`/`CANCEL`/`REFUSE` are 16 or 32 hex digits. Format: `docs/nbr-logformat.md`.
    `tools/nbrlog.py` reports them, including CHECK violations in its summary.

## Verification

- Host tests for the matrix, views, phone frame and consistency check; a replay of the DK5EN-98
  capture 21.-24.09. against a frozen copy of the old code (0 decision mismatches in 2,697
  frame groups); a host env built with `-O2 -ffast-math` to mirror the nRF52 compiler flags.
- 34 of 35 firmware envs build; `esp32-external-radio` needs its overlay, as before.
- Bench: T-Deck Plus (reboot keeps topology), Heltec V3, T-Beam v1.2, RAK4631 concurrency stress
  (2 runs, 0 consistency violations, no reset, heap stable).
- Field run on DK5EN-1 and DK5EN-98 since 2026-09-26; evaluation pending.

## Known limits

- The server upload still carries the received via of a relayed frame; resetting it needs a copy
  of the frame and is left for stage 4.
- `isnan()`/`isfinite()` checks in `bmx280.cpp`, `onewire_functions.cpp` and `config_json.cpp`
  are compiled away on nRF52 (`-Ofast`); not changed here.
