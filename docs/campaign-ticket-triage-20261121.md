# Campaign: firmware ticket triage 2026-11-21

Work the 59 open firmware tickets of the telegram incident desk (component firmware, desk status
not closed) that the 2026-11-21 check (pin `96aaa67c72`, v4.40a.11.21) gave a verdict: close
what is done, plan what is not. Problem files live in `~/homeautomation/telegram/problems/`
(read-only here; the operator syncs them). Desk writes go through the `tgreader` MCP, by the
orchestrator at the gates only.

## Scope

| Group | Verdict                   | Tickets                                                                                                                                                                         |
| ----- | ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A     | open (21)                 | P-0021 0030 0044 0059 0081 0154 0163 0166 0167 0179 0209 0210 0345 0351 0380 0381 0382 0383 0384 0385 0386                                                                      |
| B     | fixed-code / fixed-proven | P-0031 0032 0050 0096 0098 0099 0103 0108 0111 0112 0113 0121 0122 0124 0152 0159 0168 0169 0190 0196 0202 0205 0215 0225 0226 0227 0230 0340 0350 0356 0363 / P-0100 0127 0148 |
| C     | by-design / not-ours      | P-0053 0104 0125 / P-0258                                                                                                                                                       |

## Waves

| Wave | Content                                                                                        | State   |
| ---- | ---------------------------------------------------------------------------------------------- | ------- |
| 1    | Haiku recon: B/C fix commits and closure evidence, A re-verify at HEAD; Sonnet plan for P-0380 | done    |
| 2    | Sonnet plans per group: display, possym, aprs, ethnet, gwroute, ota, misc                      | running |

Wave 2 groups: display (P-0021 0384 0345 0059), possym (P-0166 0167 0383 0081), aprs (P-0044
0386 0179), ethnet (P-0154 0163), gwroute (P-0381 0382 0210), ota (P-0209), misc (P-0030 0351
0385).

## Decisions taken

- Backlog ids use the 3.2 table's AREA-NN style, checked unique.
- P-0124 is closed in the desk as already-implemented; its backlog row WEB-LATLON-01 (native
  test) stays open.

## Done

Wave 1 (2026-11-21, HEAD `c85e5805`):

- A re-verify: all 21 group A tickets still open at HEAD (no commit since the pin touched the cited code).
- B: 33 tickets closed in the desk as already-implemented with linked fix commits, every sha checked as an ancestor of
  `fork-dev` and not reverted; the Opus advisor corrected the links of P-0032, P-0190, P-0205 and P-0230 (more than one
  commit) and named the carrier squash for P-0100, P-0113, P-0122, P-0225 and P-0356 (originals not in `fork-dev`).
- C: close proposals (not-a-bug) for P-0053, P-0104, P-0125.
- P-0380: plan `plan-nrf52-dumpsettings-secrets-20261121.md`, advisor rework applied (BLE image row was stale, AppLink
  row, test counts 32 -> 36), backlog row SEC-DUMP-01, desk status ready.

## Open / needs operator

- P-0159 (T-Deck flush): not closed. Only a workaround exists (NOP flush, `tdeck_main.cpp:597`), root cause unknown.
- P-0258: not closed. "not-ours" holds only for McApp; the request (group filter in the node's own web message list,
  `web_functions.cpp` ~3850-3917) is an unmet firmware feature. Suggest needs-info or a feature row.
- P-0167: problem file says duplicate of P-0166, desk status new; closed in the desk as duplicate at gate 2.
- P-0380: mask `bt_code` in the dump (plan default) although `--info` shows it (CQ-01)? Include the legacy `node_pwd`?
