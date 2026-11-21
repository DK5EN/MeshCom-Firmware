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

| Wave | Content                                                                                        | State |
| ---- | ---------------------------------------------------------------------------------------------- | ----- |
| 1    | Haiku recon: B/C fix commits and closure evidence, A re-verify at HEAD; Sonnet plan for P-0380 | done  |
| 2    | Sonnet plans per group: display, possym, aprs, ethnet, gwroute, ota, misc                      | done  |

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

Wave 2 (2026-11-21):

- Nine plans for the other 20 open tickets, two Opus advisor passes. Six approved with minor edits; display paging and
  autosymbol/NEXT reworked (browse depth none for slot 5 and an out-of-bounds write at boot; two wrong test values).
- 20 Implement rows in `BACKLOG.md` 3.2; desk status ready with plan and backlog id for all 20 tickets.

| Plan                                        | Tickets -> backlog id                                                            |
| ------------------------------------------- | -------------------------------------------------------------------------------- |
| plan-nrf52-dumpsettings-secrets-20261121.md | P-0380 SEC-DUMP-01                                                               |
| plan-display-paging-20261121.md             | P-0021 DISP-PAGE-01, P-0384 DISP-PAGE-02, P-0059 DISP-PAGE-03, P-0345 BTN-OFF-01 |
| plan-gps-autosymbol-nextpos-20261121.md     | P-0383 GPS-AUTOSYM-01, P-0166 -02, P-0167 -03, P-0081 POSINFO-NEXT-01            |
| plan-aprs-text-limits-20261121.md           | P-0044 APRS-TXT-01, P-0386 -02, P-0179 -03                                       |
| plan-rak-eth-dns-netmode-20261121.md        | P-0154 RAK-ETH-DNS-01, P-0163 RAK-NETMODE-01                                     |
| plan-gateway-routing-20261121.md            | P-0381 GW-GROUP-01, P-0382 GW-DM-01, P-0210 GW-CALL-01                           |
| plan-ota-safeboot-gate-20261121.md          | P-0209 OTA-SAFEBOOT-01                                                           |
| plan-tbeam1w-txpower-20261121.md            | P-0030 TBEAM1W-PWR-01                                                            |
| plan-dht-probe-20261121.md                  | P-0351 DHT-PROBE-01                                                              |
| plan-web-tempoffsetoutdoor-20261121.md      | P-0385 WEB-TEMPOFF-01                                                            |

## Operator decisions 2026-11-21 (interview)

Recorded in each plan under "Operator decisions 2026-11-21".

- P-0380: mask `bt_code` and `node_pwd` in the dump.
- P-0209: D1 (b) warn and proceed, no refusal (against the recommendation); D2 a, D3 a, D4 a, D5 reuse.
- Gateway: D1 A, D2 A, D3 A, D4 A, D5 A.
- APRS: D1 (a) 25 bytes, D2 no name, D3 A, D4 B0.
- P-0030: fix for every SX1262/LLCC68 board; no bench above 2 dBm (no setup).
- Display: rank, counter on arrival (Wireless Paper), first-click rule Wireless Paper only, `PAGE_MAX` 6.
- Autosymbol also without TRACK (against the recommendation); no extra beacon; P-0081 part 2 considered fixed.
- Ethernet: nRF52 honours `--setowndns`; T114 prints `none`. DHT: stop after three probes; remove `dht_name_matches`.
- New row POSINFO-LAG-01 (beacon about 30 s late).
- Desk: P-0258 closed already-implemented (group tabs on the Messages page, d46e2904, v4.35t); P-0159 closed by
  operator decision (mitigation 2e874921). P-0167 stays open (ready): its own defect, ships with P-0166.

## Open / needs operator

- Accept the three close proposals P-0053, P-0104, P-0125 in the desk (the agent cannot accept its own proposals).
- P-0167: reopen the problem file at the next sync (it says closed as duplicate of P-0166; desk is ready).
- Problem files in `telegram/problems/` were not edited: sync them from the desk.
