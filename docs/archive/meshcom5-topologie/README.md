# MeshCom 5 topology concept — archive

> **Status: superseded on 2026-09-25.** The current, consolidated paper is
> `docs/meshcom5-topologie/meshcom5-topologie.html` (Fassung 2). It folds in every paper below,
> all 16 verified findings of the fable review of Fassung 1, and the root cause for DB0ED-99
> showing as redundant (R1). Read that paper. The files here are the record of how it got there.

## Superseded papers

| File                                                                  | What it was                                                                                   |
| --------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| `Nachbarschaftsmatrix-Kantenpool.html` (+ `.build.py`, `.json`)       | Edge-pool concept, 2026-09-24: pool, masks, Fall A/B, roles, 32-platform RAM, path-table flag |
| `Nachbarzahl-NCNT-Befund.html`                                        | NCNT findings B1–B6, 2026-09-24                                                               |
| `Nachbarzaehler-Zeitreihe.html`                                       | mcmap neighbour-count time series, Fassung 1, 2026-09-24                                      |
| `Zwei-Hop-Nachbarschaft.html`                                         | mcmap 2-hop neighbourhood and roles, Fassung 2, 2026-09-24 (supersedes the time-series paper) |
| `MeshCom5-Topologie-als-Quelle-v1.html` (+ `.build.py`, `.body.html`) | MeshCom 5 topology concept, Fassung 1, 2026-09-24                                             |
| `MeshCom5-Topologie-als-Quelle-v1-verdict.md`                         | Fable review of Fassung 1: 16 findings, 16 refuted claims                                     |

The build scripts reference their original Desktop paths and are kept as a record, not to be run.

## Evidence (`evidence/`)

Scripts reference the session scratchpad and the rpizero capture by absolute path. The raw logs
are not in the repo; they live on rpizero under `~/meshlog/dk5en-98/2026-09-21.log` to
`2026-09-24.log` (capture ended 2026-09-24 09:28).

| Path                                               | Content                                                                                                       |
| -------------------------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| `finder-A.md` … `finder-I.md`                      | Nine independent finders on Fassung 1, one angle each                                                         |
| `verify-V1a.md` … `verify-V5.md`                   | Six adversarial verifiers; only what survived them entered the verdict                                        |
| `rca/xrules.py`, `rca/xr_*.py`                     | Coverage-rule replay (today's single hit, 60 min, traffic share 5/10/20 %) behind paper Anhang A.1 and Abb. 8 |
| `rca/xrules-output-share-*.txt`                    | Replay outputs; `xrules-series.json` is the input of Abb. 8                                                   |
| `rca/*-20260924-0855.html`, `rca/*-20260925*.html` | Neighbours, path, MHeard and info pages of DK5EN-98 as fetched                                                |
| `rca/fleet-firmware-20260925.json`                 | mcmap fleet_firmware census behind paper A.6 and the stage-4 deferral                                         |
| `v1b/`                                             | Echo first-hand/second-hand and named-set echo rates (paper Anhang A.2), fixture checks                       |
| `v2_snr.py`, `v2_trickle.py`                       | SNR per neighbour and trickle-reset replay (paper A.4)                                                        |
| `fable/v3_*.py`                                    | Horizon counts and hop-count spread (paper A.3)                                                               |
| `fable/v4/mhlen.cpp`, `rx.cpp`, `hz.c`             | MH frame length with the firmware's ArduinoJson, callsign regex, horizon meta size (paper A.5)                |
| `fable/struct_test*.c`, `rowsize.c`                | Struct sizes on xtensa and arm (the 40 B vs 36 B row)                                                         |
| `v1a_extract.sh`                                   | Extracts the source of each 4.35 tag used for the via behaviour windows (paper 4.11); re-run to reproduce     |
| `h2t.py`                                           | HTML-to-text helper used for the deduction passes                                                             |
