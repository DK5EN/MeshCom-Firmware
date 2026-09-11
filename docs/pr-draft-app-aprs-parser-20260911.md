# PR draft: Meshcom-MobileApp, branch `aprs-parser-contract`

Target: `rainerfritz/Meshcom-MobileApp` `main`. Local branch `aprs-parser-contract` @ `578964b`,
based on `origin/main` `760f1f7`. Pushed to fork `DK5EN/Meshcom-MobileApp`, PR opened: https://github.com/rainerfritz/Meshcom-MobileApp/pull/8. English, because the app repo is English.

---

## Title

Extract a tested APRS frame parser and align it with the firmware wire contract

## Summary

The BLE frame handler (`src/hooks/MessageHandler.ts`) parsed position beacons with fixed string
offsets and `split("/")`, which drifted from what the firmware actually emits. This PR moves the
byte-level and APRS-text parsing into a pure module, `src/utils/AprsParser.ts`, covered by 23
vitest cases built from real on-air frames, and fixes the mismatches found while writing it. The
reference is the firmware's wire-format document, section 1.8 (position comment tail) and 4.3
(BLE notification layout):
`https://github.com/DK5EN/MeshCom-Firmware/blob/fork-main/docs/architecture/11-wire-format.md`

## What was wrong

| Area           | Before                                                                                                                                          | After                                                              |
| -------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------ |
| `msg_id`       | read big-endian (`getUint32(2, false)`); firmware writes little-endian                                                                          | `readMsgId()` little-endian; ids match node, gateway and MCProxy   |
| telemetry tail | `pos_text.slice(20)` then `split("/")`; a comment was fed into the key switch, and a comment starting with `N`+digit became the neighbour count | keys found by anchored regex over the tail; last occurrence wins   |
| `/N<n>`        | one digit only (`N12` gave 1)                                                                                                                   | 1..99                                                              |
| APRS symbol    | table char used only as a split delimiter; symbol char never extracted                                                                          | `symbol_table` and `symbol` persisted and shown in the map overlay |
| comment        | parsed, stored, never rendered                                                                                                                  | rendered in the overlay                                            |
| `/D= /U= /I=`  | unknown, silently dropped                                                                                                                       | `din` (8-bit string), `vbus`, `vcurrent`                           |
| node timestamp | only read when a via path existed                                                                                                               | read for every text/position frame, plausibility window kept       |
| HW ids 13..38  | `hwtable[id]` is `undefined`, stored as such                                                                                                    | `hwName()` falls back to `Unknown (id)`                            |
| unknown `TYP`  | dropped without a trace                                                                                                                         | logged                                                             |
| `date-fns`     | imported, not declared; fresh clone fails `tsc`                                                                                                 | declared in `package.json`                                         |

## Files

- `src/utils/AprsParser.ts` (new): `parsePositionPayload`, `readFrameTrailer`, `readMsgId`,
  `readNodeTimestampMs`, `decodeFlags`.
- `src/utils/AprsParser.test.ts` (new): fixtures include `4711.55N/01444.60E_MeshCom Zeltweg /B=089/A=002451`,
  `4825.35N\01147.19E-Standort/H=520m Dach/T=22.6/H=42.5/P=940.3` and a full 17-key beacon.
- `src/hooks/MessageHandler.ts`: position block replaced by one parser call; `msg_id`, timestamp,
  trailer, `hwName`, default branches.
- `src/utils/AppInterfaces.ts`, `src/DBservices/DataBaseService.ts`: five new `Positions` columns
  (`symbol_table`, `symbol`, `din`, `vbus`, `vcurrent`) via the existing `ALTER TABLE` upgrade
  pattern.
- `src/components/MapOverlay.tsx`, `src/pages/Map.tsx`: symbol and comment in the overlay.
- `src/store/HwTable.ts`: `hwName()`.
- `src/utils/TestMsgsPosis.ts`: fixtures gain the two new required fields.
- `package.json`, `package-lock.json`: `date-fns`.

## Compatibility notes

- Ids of messages stored before this version were read big-endian, so an ACK arriving after the
  update will not match those rows. New traffic is consistent.
- The DB upgrade adds columns only; nothing is dropped.
- No change to outbound frames.

## Verification

- `npx tsc --noEmit` clean, `npx vitest run` 24/24, `npm run build` ok.
- Not run on a phone against a live node yet.
- `npx eslint` cannot run on this tree: `.eslintrc.js` is CommonJS inside an ESM package and fails
  to load before any rule executes. Untouched in this PR.

## Open follow-ups (not in this PR)

- Render the APRS symbol as a marker glyph instead of text.
- Read `MOD`, `FW`, `LH` into the UI (parsed and logged only).
- Hashtag destinations (`{#TAG}`) once the firmware defines them.
