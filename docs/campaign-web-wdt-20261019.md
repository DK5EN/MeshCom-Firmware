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
- Upstream PR later, separately.

## Waves

| Wave | Content                                                               | Files                                                                                                                     | State     |
| ---- | --------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- | --------- |
| W1   | `web_txbuf.h` + 20 host tests; feeds Path/MHeard/matrix; stall client | `web_txbuf.h`, `test/test_web_txbuf/`, `platformio.ini`, `web_functions.cpp` (3 pages), `tools/bench/web_stall_client.py` | committed |
| W2   | Wiring: `CommonWebClient` overrides, `web_txbuf.cpp`, request end     | `web_commonServer.h`, `web_txbuf.cpp`, `web_functions.cpp` (request path)                                                 | open      |
| W3   | Gate, advisor, bench runs B-F, docs                                   | hardware, `docs/`                                                                                                         | open      |

W1 notes:

- Feeds: 7 new call sites (mheard per card; path after collect, after sort, per record; matrix header
  columns and the two compute pre-passes). Plan lines ~3003/3038/3093 are inner loops of the already fed
  row loop, no feed added there.
- Orchestrator fix in `webTxSendLoop()`: the stall window starts at loop entry, so page compute time between
  two flushes no longer counts as a client stall (test `test_compute_gap_is_not_stall`, red before, green after).

## Run A (pre-fix reproduction) -- not reproduced from macOS

DK5EN-1 (QRT INSTRUMENT build of `1a6a3704`), pages are 8-14 kB (neighbours 8093 B / path 14071 B / mheard
7925 B, normal reads 0.3-0.8 s). `--stall` from the Mac with SO_RCVBUF 1024 and 64: node survived 20-25 s, no
abort. macOS does not shrink the advertised window to SO_RCVBUF; node lwIP send buffer is 5760 B, so the Mac
absorbs the whole page and `write()` never blocks. A Linux container (honours SO_RCVBUF, 2304 B) cannot reach
the LAN (OrbStack, also with `--network host`). Options for a real "before": pf blackhole on the Mac (sudo,
operator), a Linux host on the LAN, or an INSTRUMENT matrix fill to pages > ~150 kB.

## Gate (planned)

`tools/regression.sh --stage 1,2`; builds `heltec_wifi_lora_32_V3`, `ttgo_tbeam`, `wiscore_rak4631`,
`t_deck_plus`, `E22_XML-DevKitC`; jsdom `webgui_*`.
