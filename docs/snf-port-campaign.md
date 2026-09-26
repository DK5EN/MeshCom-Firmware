# S&F port to feature-snf — campaign state

Started 2026-09-26. Ports the DM store-and-forward stages from `fork-main` onto
`feature-snf`, branched from `feature-neighbour-matrix` `0d4b914c` (neo line, upstream base
`e4a2393f`). Resume rule: this file is the authority, not the git log.

Source of truth for the feature itself: `fork-main:docs/dm-transport-impl-plan-20260913.md`
(stage table, decisions D1-D8) and the stage plans next to it.

## Status

| Wave | Content                                                             | Status          | Commit |
| ---- | ------------------------------------------------------------------- | --------------- | ------ |
| 0    | branch, this doc, neo path lists (NBR gap)                          | done 2026-09-26 |        |
| 1    | new modules + host tests, shared headers, `env:native`              | open            |        |
| 2    | stage 0 (without 0.1) + stage 2.1 hooks                             | open            |        |
| 3    | stage 1 outbox + ladder (`--dmretry`)                               | open            |        |
| 4    | stage 3 store node + stage 4 custody notice                         | open            |        |
| 5    | docs over, CHANGELOG/BACKLOG, all-env build, RAM snapshot same-base | open            |        |

Per-wave gate: files exist; host suite (native envs only, never bare `pio test`);
`test/golden/help_parity_lint.py`; 7 lead envs clean and sequential; string scan of the S3 and
RAK images; `/fable-review` advisor pass; one commit. No hardware, no push in this campaign.

## Operator decisions (2026-09-26)

| ID  | Decision                                                                                                                                |
| --- | --------------------------------------------------------------------------------------------------------------------------------------- |
| P1  | ACK msg_id: keep the target's `msgid_counter.*` high-water design. Stage 0.1 (millis() minting in `SendAckMessage()`) is NOT ported.    |
| P2  | `--store on` + `--mesh off`: store delivery and the `:sto` notice stay independent of `bMESH`, as on fork-main.                         |
| P3  | S&F is meant to reach neo: every new S&F src/test file goes into `tools/neo/paths` (code: CORE, tests: K19) in the commit that adds it. |
| P4  | Boards as fork-main: `ENABLE_MSGSTORE` on ESP32-S3 + RAK4631 only; dm_stats/dedup/outbox on every board.                                |
| P5  | Stage 2.2 stays deferred; upstream 5efa2171 (repeat-counter MSB, dormant on fork-main) is not ported.                                   |

## Port rules (from the recon)

- Port the post-rework version of each stage, never the first cut: 4a569e6f (airgap teardown,
  echo scope), 1595542c (the ack stops the ladder OUTSIDE `checkOwnTx`), 96050d72 (the ESP32
  tick is the main-radio `if(bRadio)` one), 150b0a4a (`:sto` on both server ingress paths).
- `aprsMessage` fields are `char[]` on the target: every `String` call in a hook becomes a
  `src/mc_text.h` helper.
- Server ingress lives in `src/esp32/udp_frame_esp32.cpp` / `src/nrf52/udp_frame_nrf52.cpp`,
  not `udp_functions.cpp` / `nrf_eth.cpp`; `env:native_udp_frame_twin` must learn the new modules.
- `addTxRingEntry(Once)` carries `kind/need/alone` (NBR); S&F slots use `RING_KIND_OTHER`.
- `mheard_functions.*` is gone (ccb3ec23): the store set's heard age comes from
  `nbrFind()` + `nbrMhGet()` (`src/nbr_views.h`), minute resolution, 12 h window unchanged.
- Commands match exact tokens; every `--help` line sits under its handler's `#if` guard.
- Host tests stay in `env:native` as on fork-main (pure modules, `build_src_filter`).

## Neo path lists

Wave 0 found the whole NBR branch unregistered, not only `nbr_matrix.*`: 19 code paths (CORE) and
19 test paths (K19) since `e4a2393f`. All added; README counts updated (744 / 281 / 463). Separately,
`fork-neo-test` itself lacks `src/mask_secret.h` and `src/mheard_throttle.h` in its lists — not
touched here.
