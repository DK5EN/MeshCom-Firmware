# Campaign WEB-WDT-01: web send buffer and watchdog feeds

Resume point for the multi-wave work. Plan (German, operator copy):
`~/Desktop/plan-web-sendepuffer-watchdog.md`, base `2ba37b10` (v4.40a.10.19).

## Goal

No TASK_WDT reboot from building a web page, however large the table or slow the browser. A buffered,
bounded send loop in `CommonWebClient` (abort on 2 s stall / 15 s page budget / send error) plus watchdog
feeds in the Path, MHeard and neighbour-matrix builders.

## Operator decisions (2026-10-19)

- Task WDT stays at 5 s. `WEB_TX_PAGE_MAX_MS` 15 s.
- nRF52 gets the same abort logic: `socketSend()` of RAK13800-W5100S waits without timeout
  (`socket.cpp:435-445`), so the send loop uses `availableForWrite()` there.
- The buffer begins lazily on the first `write()` (covers the config export, which sends its own header);
  `txEnd()` at request end and in `stop()`.
- Flash approval for all bench nodes (USB and OTA). DK5EN-98 is not touched. DK5EN-14 has no console password.
- Upstream PR later, separately. Done 2026-10-20: #1224 (`pr/web-wdt-1217`, `204bb7ad`, Fixes #1217).

## Waves

| Wave | Content                                                               | Files                                                                                                                     | State                    |
| ---- | --------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- | ------------------------ |
| W1   | `web_txbuf.h` + 20 host tests; feeds Path/MHeard/matrix; stall client | `web_txbuf.h`, `test/test_web_txbuf/`, `platformio.ini`, `web_functions.cpp` (3 pages), `tools/bench/web_stall_client.py` | committed `3b54c452`     |
| W2   | Wiring: `CommonWebClient` overrides, `web_txbuf.cpp`, request end     | `web_commonServer.h`, `web_txbuf.cpp`, `web_functions.cpp` (request path)                                                 | committed `34974374`     |
| W3   | Gate, advisor, bench runs A-F, docs                                   | hardware, `docs/`, `tools/bench/`                                                                                         | done (Chrome check open) |

W1 notes:

- Feeds: 7 new call sites (mheard per card; path after collect, after sort, per record; matrix header
  columns and the two compute pre-passes). Plan lines ~3003/3038/3093 are inner loops of the already fed
  row loop, no feed added there.
- Orchestrator fix in `webTxSendLoop()`: the stall window starts at loop entry, so page compute time between
  two flushes no longer counts as a client stall (test `test_compute_gap_is_not_stall`, red before, green after).

## Gate and advisor

- Gate on W2: `tools/regression.sh --stage 1,2` (Unity 76 envs 2713/2713, jsdom, pytest; golden
  `variant-ini-effective` updated for the new env), builds `heltec_wifi_lora_32_V3`, `ttgo_tbeam`,
  `wiscore_rak4631`, `t_deck_plus`, `E22_XML-DevKitC` all pass, `[WEB];tx;` strings x3 in every image.
- Advisor (Opus) on W1+W2: APPROVED. Applied: nRF52 closed socket -> `error` abort instead of a 2 s stall;
  comment fixes. Noted, not changed: RAK lib `socketSend` still waits for `SEND_OK` without timeout (bench E
  showed no hang); lwIP ENOMEM aborts the page like the core's own `write()`.

## Bench (2026-10-19)

Tools: `tools/bench/web_stall_client.py` (reboot = uptime from `--info` going backwards, plus
`RESET_REASON`), `tools/bench/web_throttle.sh` (sudo, pf/dummynet delay on the nodes' port-80 replies).
macOS ignores a small `SO_RCVBUF`, so a non-reading client alone never blocks the node; a pure bandwidth
limit came out bursty; a reply delay pins the node's 5760 B lwIP send window.

| Run | Node / build                       | Load                         | Result                                                                                        |
| --- | ---------------------------------- | ---------------------------- | --------------------------------------------------------------------------------------------- |
| A   | DK5EN-1, QRT INSTRUMENT `1a6a3704` | path page, 2.5 s reply delay | **TASK_WDT**: `RESET_REASON=6 TASK_WDT LAST_LOOP_SECTION=web` (uptime 10.2 h -> reboot 09:33) |
| B   | DK5EN-1, fix                       | `/`, setup, 2.5 s delay      | survived, `abort=stall` after 14340 / 15776 B, uptime continuous                              |
| C   | DK5EN-1, -14, -90, fix             | `/`, setup, 1.2 s delay      | survived; setup complete (24527 / 24106 / 21550 B); `/` cut by `budget` at 15 s (87 / 73 kB)  |
| D   | DK5EN-92 (T-Beam), fix             | `/`, setup, 2.5 s delay      | survived, `abort=stall`; idle heap 67748 B vs baseline 69332 B (-1584, tolerance 3072): pass  |
| -   | DK5EN-14 (T-Deck), fix             | `/`, setup, 2.5 s delay      | survived, `abort=stall` after 5744 B                                                          |
| E   | DK5EN-90 (RAK4631), fix            | `/`, setup, 2.5 s delay      | answer ends after 2048 B (one W5100S buffer), node answers right after, no hang               |
| F   | DK5EN-1, fix, no delay             | all 14 pages                 | complete, `[WEB];tx;done`: `/` 94193 B in 501 ms (68 flushes), setup 106 ms, spectrum 4312 ms |

Pages end with `</html>` on DK5EN-1, -14, -90; sizes stable over two reads. The path page was only 3 kB
right after a reboot (matrix refilling) and does not block then; B-E therefore use `/` and setup. The Chrome
pass of run F is open (extension not connected).

T-Beam idle heap (`heap_idle.py --host`, STAT samples, 620 s): 67748 B, pass; release 10.19 measured 67304 B.
