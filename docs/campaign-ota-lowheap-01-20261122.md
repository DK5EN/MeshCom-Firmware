# Campaign: OTA-LOWHEAP-01 (2026-11-22)

ESP32-S3 without PSRAM (Heltec V3, T-Beam Supreme) refuses Auto Update with "Not enough free memory" after long
uptime. Fork issue DK5EN/MeshCom-Firmware#3, analysis [rca-ota-lowheap-01-20261122.md](rca-ota-lowheap-01-20261122.md).
Subsumes AU-24 and AU-25. Base `fork-dev` `d7503fb1`. Auto Update is fork-only firmware: no upstream PR in this
campaign (it goes with upstream wave 6).

State: **wave 1 running (2026-11-22 evening): overnight bench run (red first), CI RAM budget.**

## Code facts (scout, 2026-11-22, against `fork-dev`)

- Gate `heapGateEval()` `src/esp32/fw_update_net.cpp:701`; S3: `FWNET_MIN_FREE` 0 (`:128-132`), largest block 17000
  (`:121`), `twoRecBufProbe()` `:648` takes two 16717 B blocks at once (`src/fw_update.h:876`).
- BLE pause `AU_BLE_PAUSE` is 1 only for `CONFIG_IDF_TARGET_ESP32` (`src/esp32/fw_update_net.h:240-244`); pause is
  `esp32BlePause()` `src/esp32/esp32_main.cpp:670`. Only the forced pause has an S3 crash note (`:725-726`, NimBLE
  host task panics on deinit/reinit).
- Job restart `fwJobRestartDue()` `src/fw_update.h:1101` needs `bleDown`, so it never fires on S3
  (`fw_update_net.cpp:1523-1534`).
- `twPurge()` `:689` and `shedHold()` `:738-752` already run on S3.
- STAT line (`src/setlog_lines.cpp:85-99`) has `heap=` only, no largest block.
- `tools/resource_watch.py` runs only as `--self-test` (`tools/regression.sh:240`); not in CI. Baseline 2026-09-11.
  `meshcom-ci.yml` captures no `RAM:` line and uploads no `.elf`/`.map`.
- No "update pending, run after reboot" mechanism exists; NVS holds only the staged download record.
- Host tests: `native_fw_update` (`test/test_fw_update`, covers `heapGateEval`/`fwJobRestartDue` helpers),
  `native` (`test_au_ui_state`), `native_au_schedule`.

## Waves

| Wave | Who                                  | Exclusive files                                                                                                                        | Verification                                            | State |
| ---- | ------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------- | ----- |
| 1a   | RAM-HUNT, implementer Sonnet high    | read-only on `src/`; writes only `docs/rca-ota-lowheap-01-20261122.md` section 3.2 addendum; worktree at the 09-11 SHA by orchestrator | `nm_symsum.py` diff 09-11 vs HEAD Heltec V3, top 20     | plan  |
| 1b   | CI-BUDGET, implementer Sonnet medium | `tools/resource_watch.py`, `tools/resource_baseline.json`, `.github/workflows/meshcom-ci.yml`, its pytest                              | pytest of `resource_watch`, `ci-proof-*` tag run        | plan  |
| 1c   | STAT-IBLK, implementer Sonnet medium | `src/setlog_lines.{h,cpp}`, STAT fill site `src/loop_functions.cpp` ~4100, setlog Unity test, log parsers that split STAT              | own native env, fail-before test                        | plan  |
| 2    | orchestrator, bench (single)         | DK5EN-1 only, INSTRUMENT build of HEAD with 1c                                                                                         | R6: 1 h web bursts + forced checks, largest-block curve | plan  |
| 3a   | S3-RESCUE, implementer Sonnet medium | `src/esp32/fw_update_net.{h,cpp}`, `src/esp32/esp32_main.cpp` BLE pause block, `test/test_fw_update`                                   | `native_fw_update`, Heltec V3 + T-Deck Plus build       | plan  |
| 3b   | FRESH-BOOT, after 3a (shares files)  | `src/fw_update.h` pending-flag logic, `fw_update_net.cpp` job entry, `esp32_main.cpp` setup hook, `test/test_fw_update`                | same                                                    | plan  |
| 3c   | RAM-RECLAIM (only if 1a finds waste) | files named by 1a                                                                                                                      | 13 boards, RAM delta per class                          | plan  |
| 4    | orchestrator                         | gate `--stage 1,2`, 13 boards, Opus advisor, bench T3 on DK5EN-1 (S3) and DK5EN-92 (classic), docs, release decision                   |                                                         | plan  |

Wave 1 has three disjoint writers; 1b and 1c hold no shared build target (1b is Python, 1c one native env); 1a builds
in its own worktree, sequentially behind the pio lock. Wave 3a and 3b share `fw_update_net.cpp` and `esp32_main.cpp`:
serialized.

## Operator decisions 2026-11-22

- D1 Measure and reproduce first (red), fix after: no fix wave before the overnight run has shown the refusal.
- D2 No reserved TLS buffers (about 34 KB less heap all the time; other allocations would fail first, with reboots
  under load as the likely result).
- D3 CI static RAM budget: fail above +1 KB per env over the committed baseline; an env without a baseline fails.
- D4 Overnight run on all bench nodes with the current INSTRUMENT build of `fork-dev` (`--heaplog 60`): DK5EN-1
  (Heltec V3, S3 without PSRAM), DK5EN-14 (T-Deck Plus, S3 with PSRAM), DK5EN-92 (T-Beam, classic), DK5EN-90 (RAK,
  heap curve only). DK5EN-98 as read-only reference (`meshlogger --no-debug-flags`, BLE connected to McApp; no
  command, no flash).
- D5 DK5EN-1 runs as gateway and transmits normally at 2 dBm for this night (exception to the no-broadcast rule,
  operator 2026-11-22).
- D6 Trigger 2026-11-23 06:00 from the Mac over the 2323 console: per ESP32 node `--update check`, `--update install`
  (stages only, never `apply`), `--update forget`, three attempts 2 min apart. Red = `lowheap` refusal.
- Operator hypothesis "heard messages queue up in RAM without a BLE client": three scouts found no unbounded growth.
  Phone ring (`loop_functions.cpp:834`) is a fixed 2-3 KB byte ring that evicts the oldest frame; NimBLE buffers
  nothing without a client (`esp32_main.cpp:2635`); all tables are fixed arrays, all malloc sites paired. Remaining
  suspects are fragmentation sources: per-page 1.4 KB `web_txbuf` block (`web_txbuf.cpp:113`), lwIP TIME_WAIT pcbs
  (purged only when the gate refuses, `fw_update_net.cpp:689`), the nightly TLS checks (about 56 KB in many pieces),
  plus the 21 KB static growth. The overnight curves decide.

## Operator decisions 2026-11-22, option A (static tables on ESP32-S3 without PSRAM)

Basis: [ota-lowheap-ram-hunt-20261122.md](ota-lowheap-ram-hunt-20261122.md) (09-11 reproduced to the byte at
`291f375a`, +21,224 B fully attributed; 71 % upstream, 6,080 B fork-only; whole classic tier -29,736 B measured).
Scope: S3 boards without PSRAM only; PSRAM boards and the RAK4631 keep the large tier.

- D7 (1C) Msgstore mailbox (13,547 B with the STOR announce tables): allocate only when the store mode is on, taking
  effect after a reboot (allocation on a fresh heap); with the mode off (factory default, `msgstore.cpp:33`) nothing
  is reserved. Refusal message if the allocation fails.
- D8 (2B) UDP outage hold `UDP_HOLD_BYTES` 6,144 -> 1,024 B (about 10 instead of 60 held texts on a gateway),
  saving 5,120 B.
- D9 (3A) Neighbour matrix stays at 128 rows (14,248 B).
- D10 (4B) `RING_BYTES_PHONE` and `RING_BYTES_UDP` 3,072 -> 2,048 B each, saving 2,048 B.
- Expected total on a V3 with the mailbox off: about 20.7 KB (13,547 + 5,120 + 2,048), close to the 09-11 level.
- Order (D1): implemented only after the overnight run has shown the refusal (red); then fix B (update after a fresh
  boot) and these cuts in one fix wave, each with a test that is red before.

## Wave 1 dispatch (2026-11-22)

- 1-bench (Sonnet medium): INSTRUMENT builds, flash -1/-14/-92/-90, gateway on -1, loggers to
  `tools/bench/runs/harness/ota-lowheap-night-20261122/`, trigger `tools/bench/ota_lowheap_trigger.py` at 06:00.
- 1b (Sonnet medium): CI RAM budget in `meshcom-ci.yml` + `resource_watch.py`; baseline refresh by the orchestrator
  from a `ci-proof-*` run at the gate.
- 1a (RAM hunt, symbol diff 09-11 vs HEAD) and 1c (largest block in the STAT line) start after the bench builds
  are done (one pio at a time; 1c edits `src/`, which must not land in tonight's image).

## Log

- 2026-11-22: analysis moved from the Desktop into `docs/`, reporter call signs replaced by "Field 1/2". Backlog row
  `OTA-LOWHEAP-01` (Implement), AU-24 and AU-25 point to it. Scout verified the code facts above.

## Wave 1b result (2026-11-22, not committed yet, waits for the stage 1,2 gate)

- `resource_watch.py gate` + CI step after `pio run` (`meshcom-ci.yml`, build log via `tee`); fails above baseline +
  1024 B, on a missing baseline entry, and on an env without RAM/Flash line. `update --log` refreshes from CI logs.
- Orchestrator fix: the header regex only knew `(platform:`; real CI logs print `(board: ...; platform: ...)`, so the
  first version saw 5 of 33 envs. Test `test_gate_reads_real_ci_header_format` red before, green after.
- Baseline refreshed from CI run `44939256914` (`v4.40a.11.22`, 33 envs, gate green on that log). The refresh drops
  the 2026-09-11 linker-region entries (`dram0_0_seg`, `iram0_0_seg`); they were informational (`dram` reads the map),
  and `snap` re-adds them from local builds.
- Evidence from the real log against the 09-11 baseline: every S3 env +20 to +25 KB, classic ESP32 -5 to -7 KB,
  nRF52 +7.5 to +15 KB. Next release tag: red if `fork-dev` grew more than 1 KB per env since 11.22.

## Wave 1 bench setup (2026-11-22 14:30, done)

- INSTRUMENT images of `1184177d` (`MC_AU_DEV_CHANNEL=1`, ELF markers `INSTR-HEAP` 5/4, `heaplog` 10/5): DK5EN-1 .78
  (USB, gateway on, connected to the server, 2 dBm), DK5EN-92 .61 (USB), DK5EN-14 .77 (WiFi OTA), DK5EN-90 .80 (DFU;
  nRF52 prints `not_available_on_this_board`, no heap telemetry). AU mode notify everywhere, TX 2 dBm, identity
  guard ok. DK5EN-98 .65 untouched, logger `--no-debug-flags`.
- Loggers until 2026-11-23 07:30 in `tools/bench/runs/harness/ota-lowheap-night-20261122/<call>/`;
  `reboot_watch.py` re-sends `--heaplog 60` after a reboot and logs `reboot-events.log`; `caffeinate` keeps the Mac
  awake. Trigger `tools/bench/ota_lowheap_trigger.py` armed for 2026-11-23 06:00 (DK5EN-1, -92, -14; refuses -98
  and -90; no `apply`). Dry run on DK5EN-92 passed (login, PAUSE hand-over, parsing).
- First readings (internal heap, bytes):

  | Node                   | Image            | Uptime | Free    | Min ever | Largest block |
  | ---------------------- | ---------------- | ------ | ------- | -------- | ------------- |
  | DK5EN-1 Heltec V3 (S3) | INSTRUMENT HEAD  | 586 s  | 98,412  | 77,572   | 86,004        |
  | DK5EN-98 Heltec V3     | CI 11.22, BLE up | 17 h   | 103,020 | -        | - (STAT only) |
  | DK5EN-14 T-Deck Plus   | INSTRUMENT HEAD  | 507 s  | 60,176  | 45,436   | 51,188        |
  | DK5EN-92 T-Beam        | INSTRUMENT HEAD  | 497 s  | 56,876  | 36,444   | 55,284        |

  Reading: the V3 idles near 98 KB free, about 21 KB below the old 119 KB comment (matches the static growth). The
  largest block at boot (86 KB) is far above the gate's 2 x 16.7 KB, so a fresh V3 passes; the refusal needs the
  heap to break up over uptime. The night decides.

## Evening check and web load (2026-11-22 19:50)

- After 4.7-5.5 h, no reboot on any node. Total free heap is flat on every node: no leak.
- The largest block drops exactly once, at the first automatic AU check 10-40 min after boot (TLS session), and is
  flat afterwards:

  | Node                 | Free (range)   | Largest block boot -> now | Min ever (during the AU check) |
  | -------------------- | -------------- | ------------------------- | ------------------------------ |
  | DK5EN-1 Heltec V3    | 96,572-100,132 | 86,004 -> 61,428          | 14,404                         |
  | DK5EN-98 Heltec V3   | 92,080-97,988  | 86,004 -> 59,380          | 11,052                         |
  | DK5EN-92 T-Beam      | 52,224-57,032  | 55,284 -> 36,852          | 9,840                          |
  | DK5EN-14 T-Deck Plus | 41,396-63,664  | 55,284 -> 21,492..38,900  | 30,320 (TLS buffers in PSRAM)  |

- Prognosis without web load: the V3 keeps about 61 KB in one block, far above the gate's 2 x 16.7 KB, so the 06:00
  trigger would likely stage cleanly (green). The field failures came through the web GUI (RCA H1/H2), which the
  console-only trigger lacks.
- Operator decision 19:50: open the web GUI of all 5 nodes (DK5EN-1, -92, -14, -98, -90) in Chrome and cycle the
  pages overnight. Each tab runs an in-page cycler (`loadPage()` every 60 s through info, messages, mailbox, wx, pins,
  position, mheard, path, neighbours, rxlog, setup; spectrum and remote left out; GET only, nothing sent on air).
  First effect: free heap -2 to -3 KB on the V3s, -14 KB on the T-Deck. The cycler dies if a tab reloads or Chrome
  discards it; check the tab counters (`window.__mcCycN`) in the morning.

## Ratchet test: repeated AU checks (2026-11-22 21:20-21:36, DK5EN-1, under web load)

- Question (operator): does each AU check leave something behind, so the largest block shrinks check by check?
- Method: 3 x `--update check` over the net console, 5 min apart, no install
  (`tools/bench/runs/harness/ota-lowheap-night-20261122/ratchet.summary`).

  | Round | Free before -> after | Largest block before -> after | Lowest free during TLS | Min ever |
  | ----- | -------------------- | ----------------------------- | ---------------------- | -------- |
  | 1     | 96,280 -> 96,492     | 61,428 -> 61,428              | 22,912                 | 12,556   |
  | 2     | 96,036 -> 96,036     | 61,428 -> 61,428              | 22,500                 | 10,904   |
  | 3     | 96,492 -> 96,040     | 61,428 -> 61,428              | 22,876                 | 10,904   |

- Result: no ratchet. The split at the first check after boot (86,004 -> 61,428) happens once; later checks reuse the
  same holes and return all memory. The TLS session leaves about 22.5 KB free at its peak every time. The lower
  min-ever (14.4 -> 10.9 KB) is the web load (about 3 KB) overlapping the TLS peak, not a leak.
- Consequence: repeated nightly checks alone do not explain the field refusal on the bench V3. Remaining candidates:
  what the download (install) itself allocates on top of the check, a phone on BLE (H2), or heap use the bench
  does not have (more neighbours, mailbox in use). The 06:00 run tests the download under web load.

## Advisor verdict on the block split and plan for 2026-11-23 (operator: option A, 22:00)

- Fable advisor, two claims checked: the ESP32-S3 heap is TLSF (`libheap.a` exports 22 `tlsf_*` symbols), and every
  largest-block reading has the form n x 1024 - 12 (size-class rounding). The "12 KB / 24 KB" steps are an artifact;
  the real split sits 23.5-26.6 KB into the old 86 KB block, caused by about 0.5 KB of long-lived objects created
  during the first TLS session. Top suspects: lazy mutexes of the hardware crypto (SHA/AES/MPI), WiFi per-peer
  state on the first TCP stream. The AU code frees everything it allocates (`fw_update_net.cpp:843-888`).
- The split is very likely not the field cause: 61 KB + 24 KB hold both 16.7 KB record buffers, and "warm reboot did
  not help, power cycle did" cannot be a heap-layout effect (a warm reboot rebuilds the heap). The tight number on the
  bench is total free during TLS (14.4 KB first check, 22.5 KB later).
- Plan, after the 06:00 run is read:
  1. Splitter owner: flash DK5EN-1 with the INSTRUMENT build that adds `--heapwatch on|off|dump|clear` (link-time
     `--wrap` of `heap_caps_*`, records ptr, size, task, return PCs) and `--heapgap` (map of free blocks and hexdump
     of the small gaps between them). Boot, `--heapwatch on`, `--heapgap`, `--update check`, wait 60 s,
     `--heapwatch dump`, `--heapgap`; resolve PCs with `addr2line` against the saved ELF.
  2. Field mechanism: DK5EN-1 on the 11.21 image, phone on BLE and the update page open. Compare warm reboot against
     power cycle; read the exact refusal token, the `[AU];refuse;lowheap;...` line, the alloc-fail line, and free /
     largest block 30 s after boot.
  3. Margin: step `--heapeat <kb>` up until the check, then the download, refuses.

## 2026-11-23: night result, heap map instruments, red on the bench, TLA+

- **06:00 trigger:** no lowheap refusal. DK5EN-1 staged 3/3 after 15.7 h under web load (free 97 KB, block 61 KB);
  DK5EN-14 (PSRAM) 3/3; DK5EN-92 console dropped at the check, no reboot. The free heap of DK5EN-1 stayed at 93-100
  KB all night; what fell was the low-water mark: 82 -> 14.4 KB at the first AU check (14:30), 10.9 KB at the ratchet
  checks, **2.9 KB during the first download** (06:01). Every AU TLS session takes 85-97 KB for a short time.
- **Loggers stopped 22:22:** the Mac briefly had < 500 MB free and `tools/meshlogger.py` exits by design below
  `--minfree`. Fixed: it now keeps the console, pauses writing node lines and resumes 200 MB above the limit
  (regression test `TestMeshloggerDiskLow`, red before, green after).
- **Instruments (INSTRUMENT builds, ESP32 only):** `src/instrument_heap.cpp` (heap_caps_* wrap from the first
  allocation, live table, call sites with backtrace, 5 KB step events `[INSTR-STEP]` reported when free moved a
  full step from the last report, heap map `--heapmap`, `--heapsites`, `--heapstep`), `src/instrument_probe.cpp`
  (`--heapprobe list|<id>|all`: AU check, DNS, bare TLS handshake, WiFi scan, every web page into a discard sink).
  Two Opus advisor passes, all items fixed. Static cost on the V3: 23 KB (live table 10 KB, sites 10 KB); T-Beam
  build uses 512/64. Host tool `tools/bench/heaptimeline.py` (timeline HTML, steps, sites resolved via addr2line,
  map, probes; 15 tests). Flash note: the fork's ESP32 layout has safeboot at 0x10000 and the app at 0xC0000 (see
  the variant `upload_command`); flashing the app at 0x10000 boot-loops the node.
- **Red on the bench (DK5EN-1, heapwatch image, 23 KB less free):** AU check gate ok at free 60 KB / block 51 KB,
  then TLS fails: `lowheap_tls -32512` (16717 B record calloc) or `-10368` (53 failed X509 allocs, low-water 588
  B). Bare TLS handshake to api.github.com: peak 67.6 KB. DNS 15 KB, WiFi scan 10 KB, web pages 4.5-7 KB peak and
  ~3 KB held. DK5EN-92: AU refuses `lowheap_ble_off`, free 40 KB.
- **TLA+ (`specs/tla/`, TLC, 57 cfgs, all as expected):** heap.tla H1-H8 hold with mutations; heap_frag.tla today
  violates F2 (gate passes, TLS fails), F3, F4, F6 (32 KB dry-run alloc after the download), F7 and F8p (update from
  the web GUI with its own tab plus a phone, the field report). Static reservation fixes F8p but starves the phone
  (F8s); sweep PoolN x R: no reservation satisfies both. The budget does not close.
- **Fable advisor budget (V3, normal image):** permanent 132 KB + AU/TLS 74 + tab 8 + phone ~6 + reserve 8 = 228 KB
  vs ~225 KB usable. Prebuilt mbedTLS has no buffer knob (SSL_MAX_CONTENT_LEN 16384, no asymmetric/dynamic, checked
  in the framework sdkconfig). ssl_client parses the 5 roots before it allocates the records. BLE controller 31.7
  KB, NimBLE 10 KB, system task stacks 23 KB; 62 KB had no owner (site table overflow).
- **Operator decision (2026-11-23): A + B.** A = admission rules during an AU session (S3 total-free rule, twPurge
  before the first gate, session flag, web 503 except AU status, no WiFi scan). B = fork copy of the TLS client
  (records first, one root per host, free CA chain after the handshake). Not chosen now: BearSSL-class client (C),
  update after a fresh boot without BLE (D).
- **Wave 2 (dispatched 2026-11-23):** 2a owner table + site recycling (`src/instrument_heap.*`), 2b fix A
  (`src/fw_update.h`, `src/esp32/fw_update_net.cpp`, `web_functions.cpp`, `test/test_fw_update`), 2c fix B
  (`src/esp32/mc_tls/`, `src/fw_update_roots.h`, `test/test_fw_update_roots`). Orchestrator integrates McTlsClient
  into fw_update_net.cpp at the gate. Run folder: `tools/bench/runs/harness/ota-lowheap-heapmap-20261123/`.

## 2026-11-23: A/B test, chain verification on the download host (switch removed)

- **Result: no measurable saving.** Turning off TLS chain verification for the asset CDN host
  (`*.githubusercontent.com`) during the download does not lower the heap peak. The switch is removed again, it was
  never committed; full verification stays on every host.
- **Setup:** DK5EN-1 (Heltec V3), INSTRUMENT image with a RAM-only bench switch (`--update dlverify on|off`) and the
  version stamp 11.20, so that release v4.40a.11.22 counts as newer. 6 real downloads, alternating on/off; every
  switch command was acknowledged by the node (`[AU];dlverify;on` / `off_cdn`). Usage = free at the pre-download
  gate minus the low-water mark during the download.

  | Chain verification | Usage per run (KB) | Mean    | Download time | Failed allocs | Staged |
  | ------------------ | ------------------ | ------- | ------------- | ------------- | ------ |
  | on                 | 65.9 / 70.1 / 64.9 | 67.0 KB | ~11.0 s       | 0             | 3/3    |
  | off (CDN host)     | 66.9 / 66.1 / 66.9 | 66.6 KB | ~10.5 s       | 0             | 3/3    |

- The 0.4 KB difference is far below the run-to-run spread (5 KB).
- Discarded: an earlier round (`abtest2.summary`, `abtest-void/`) never downloaded, because the image carried the
  build date and 11.22 was not newer. A run without a real download now aborts the test as invalid.
- Likely cause, not measured: the peak sits in the data transfer, not in the handshake. The root certificate is
  already freed after the handshake (fix B), the server chain is parsed either way; what remains are the two 16.7 KB
  TLS record buffers plus the inflate buffer. Only a rebuild of mbedTLS itself reaches the record buffers.
- Evidence: `tools/bench/runs/harness/ota-lowheap-heapmap-20261123/abtest3.summary`.
- **Next: knockout runs R0-R7 on DK5EN-1** (one feature off per run; free heap, heap per owner, check + download).
  R0 baseline, R1 web server off, R2 KISS off, R3 AppLink off, R4 gateway + EXTUDP off, R5 mailbox off, R6 GPS /
  display / sensors off (control), R7 everything that can be switched off. B1 image with `DISABLE_BLE`, B2 without
  BLE, AppLink and KISS. R4 runs on DK5EN-1 only with an operator OK (it interrupts the gateway); without it, R4
  runs on the T-Beam only.

## 2026-11-23: knockout runs R0-R7 (round 1, `tools/bench/ota_lowheap_knockout.py`)

Operator OK 2026-11-23 for DK5EN-1 and DK5EN-92, R4 included. Each run toggles one feature against the node's own
baseline (on -> off, off -> on), reboots, samples idle heap, runs AU check + install (stage only), restores the
baseline (`restore=ok` on both nodes). Round 1 download values on DK5EN-1 are void after R0 (the staged image was
not dropped, install answered `already_staged`); round 2 (`-v2`, with `--update unstage`) repeats them.

| Run | Feature (toggle)       | V3 idle free (KB) | V3 delta | T-Beam idle free (KB) | T-Beam delta |
| --- | ---------------------- | ----------------- | -------- | --------------------- | ------------ |
| R0  | baseline               | 100.0             | -        | 47.5                  | -            |
| R1  | web server off         | 106.1             | +6.1     | (see below)           |              |
| R2  | KISS on                | 99.6              | -0.4     | 47.0                  | -0.4         |
| R3  | AppLink on             | 95.5              | -4.5     | 43.2                  | -4.3         |
| R4  | gateway off            | 100.0             | 0.0      | already off (repeat)  | 0.0          |
| R5  | mailbox (store own) on | 100.0             | 0.0      | not readable          | -            |
| R6  | display on (control)   | 100.0             | 0.0      | 47.5                  | 0.0          |
| R7  | everything on -> off   | 106.1             | +6.1     | (see below)           |              |

- AU check usage on the V3 stays at 59-61 KB in every run: no feature changes the AU's own need.
- Mailbox: switching the store mode changes nothing, the 13.5 KB tables are static (D7: allocate only when on).
- Gateway mode holds no extra heap on the V3.
- **T-Beam with the web server off (R1, R7, repro with serial capture):** the gate passes at 76.4 KB, the
  download runs to the end (1,240,686 B in 17.9 s), then `[AU];refuse;nomem`: 2 failed allocations, max 32768 B
  (`heap_caps_realloc`), largest block 22.5 KB. Cause: the dry-run inflate check after the download needs the 32 KB
  dictionary (`TINFL_LZ_DICT_SIZE`, `fw_update_net.cpp:1218`) as one block (TLA+ F6). The console closes because
  fix A sheds it during the job (`[AU];shed;services;console;1`), not a reset. Evidence:
  `tools/bench/runs/harness/ota-lowheap-knockout-20261123/tbeam-r1-serial.log`.
- Levers for the T-Beam to decide (operator): allocate the 32 KB dictionary before the download while the block
  exists, or verify the inflate stream during the download; plus web server off during the AU session (+6 KB).

### Round 2 (`ota-lowheap-knockout-20261123-v2/`, with `--update unstage`, values in KiB)

| Run | Toggle            | V3 idle | V3 dl gate | V3 dl low | V3 dl used | T-Beam idle | T-Beam result              |
| --- | ----------------- | ------- | ---------- | --------- | ---------- | ----------- | -------------------------- |
| R0  | baseline          | 97.6    | 83.1       | 14.7      | 68.3       | 46.4        | refused `lowheap_ble_off`  |
| R1  | web server off    | 103.6   | 89.1       | 20.7      | 68.4       | 52.4        | gate ok, download, `nomem` |
| R2  | KISS on           | 97.2    | 82.4       | 16.7      | 65.8       |             |                            |
| R3  | AppLink on        | 93.3    | 78.7       | 12.9      | 65.8       |             |                            |
| R4  | gateway off       | 95.8    | 82.0       | 12.8      | 69.3       |             |                            |
| R5  | mailbox on        | 97.7    | 83.1       | 16.8      | 66.3       |             |                            |
| R6  | display on        | 97.7    | 83.1       | 13.9      | 69.2       |             |                            |
| R7  | web + gateway off | 103.6   | 89.1       | 20.0      | 69.1       | 52.5        | gate ok, download, `nomem` |

- The download needs 66-69 KiB in every configuration (spread ~3.5 KiB); features only shift the starting level.
  The only switchable lever is the web server (+6 KiB); AppLink costs 4.3 KiB when on.
- Download breakdown from the T-Beam INSTR-STEP trace: 2 x 16717 B mbedTLS record buffers (33.4 KB, half of the
  total), TLS session + handshake ~10-15 KB (largest block down to 6.6 KB), WiFi RX in flight ~4-6 KB (1458 B
  alloc / 1460 B free alternate, no pile-up: flash is not the bottleneck, the TCP window caps it), loopTask
  1460 B blocks ~6 KB (site unresolved, "site 63"), download buffers ~3 KB.
- Operator 2026-11-23: patch the mbedTLS output record buffer to 4096 B (input stays 16 KB: the CDN sends 16 KB
  records). A calloc hook cannot do it (ssl_free/reset zeroize the full compiled length); object override of the
  mbedTLS TUs compiled with `MBEDTLS_SSL_OUT_CONTENT_LEN=4096`, like `src/esp32/idf_override/mdns.c`. Expected
  saving ~12.3 KB per TLS session on every ESP32.
- T-Beam: built with `BOARD_HAS_PSRAM`, but `psram 0`, `pfree 0` at runtime: PSRAM missing or not detected; the TLS
  records land in internal RAM. To check on the board.

## 2026-11-23 late: mbedTLS 4 KB output buffer + TLS/dictionary arena (written, NOT built, NOT committed)

Session stopped before the measurement campaign (operator). Uncommitted in the tree:

- **mbedTLS output record 16717 -> 4429 B** (`MBEDTLS_SSL_OUT_CONTENT_LEN=4096`, input stays 16384): object override
  `src/esp32/idf_override/mbedtls/` (ssl_tls.c, ssl_msg.c, ssl_cli.c, ssl_srv.c from v2.28.7, shim
  `mc_mbedtls_out4k.h`, `#error` guard `mc_mbedtls_check.h`), `README-mbedtls.md`. At 16384 the vendored objects are
  identical to `libmbedtls_2.a` of 3.20017.241212 (symbols, sizes, disassembly). `mc_ssl_send` now loops over partial
  writes (`src/esp32/mc_tls/mc_ssl_client.cpp`); `WiFiClientSecure` users (ESP32-audioI2S) have no loop: requests
  must stay < 4096 B. Expected: -12288 B per TLS session.
- **(a') arena** (operator go 2026-11-23): `src/fw_tls_arena.h`, `test/test_fw_tls_arena` (14/14),
  `src/esp32/fw_update_net.cpp`: 33440 B per AU job (allocated after the BLE pause, :1737; released :1749), hosts
  the job task's record buffers via tlsCalloc/tlsFree, then lends 32 KB as the dry-run dictionary
  (`[AU];dry;dict;arena`). PSRAM boards unchanged. `heapGateEval` counts the arena as free (gate otherwise refuses).
  Risk: at the install-job start of the measured T-Beam case the largest block was 22.5 KB (< 33440) after a separate
  check job; the design assumes the check job's records in the arena no longer fragment the heap. Measure.
- Not done: advisor review of both, board builds, gate, golden baseline (new env `native_fw_tls_arena`), bench.
  Done in the next section.

## 2026-11-23 evening: gate and bench of the 4 KB output buffer + arena (`469bb942`); two stops

**Committed on fork-dev (`469bb942`).** On the bench the check job runs with the arena on both boards. The T-Beam
install job gets no arena (`[AU];arena;none`), and the download now fails on every image tested, including
images built before today's commits. Both need an operator decision; measurement stopped there.

- **Advisor (Opus, 2 passes):** approved after rework. Changes from the review:
  - The arena is now 32768 B = max(in 16720 + out 4432, dictionary). Before it was 33440 B.
  - Without an arena the probe tests in 16717 + out 4429 (`FWNET_TLS_OUT_BUF`, `src/fw_update.h`).
  - On a restart the idle arena is handed to the new task (`arenaDetach`, log `[AU];arena;ok;32768;adopt`).
  - A link marker `mc_mbedtls_out4k_marker` makes a missing override fail the link.
  - New log lines: `[AU];arena;badfree;N`, `[AU];arena;rec;hit;H;miss;M`, `[AU];dry;dict;heap;arena;..;live;..;lent;..`.
  - Two new tests (alignment, gap). Both fail when the rounding is mutated out.
- **Consequence the advisor found (accepted):** while an arena exists it holds the dictionary's 32 KB from the job
  start, so the 4 KB output record does not lower the job peak. The 12 KB saving applies only without an arena and
  to TLS outside the AU job. The target "V3 download peak about 12 KB lower" does not hold with the arena.
- **Gate:** 7 board builds green. `tools/regression.sh --stage 1,2` PASS (130 envs, 4243 cases). The golden
  variant-ini baseline was regenerated (new env `native_fw_tls_arena`).
- **Link proof** for `heltec_wifi_lora_32_V3`, `ttgo_tbeam`, `T-ETH-ELITE_1262` and `t_deck_pro` (esp32 and esp32s3):
  - From `libmbedtls_2.a` only `net_sockets` and `ssl_ciphersuites` are pulled.
  - The `mbedtls_ssl_setup` literals are 0x414d (16717, in) and 0x114d (4429, out).
  - The marker symbol is present in all four images.
- **Measurement images:**
  - The T-Beam with the full heapwatch table overflows DRAM by 13 KB. It was built with
    `-DHW_OWNER_ONLY=1 -DHW_LIVE_N=256`; its static DRAM is 5.2 KB below the morning's heapwatch image.
  - On the V3 the heapwatch image costs 22 KB (idle 75.6 KB; the gate refuses even the check). It was
    discarded and replaced by a plain `INSTRUMENT_ENABLED=1` image, as in round 2.
  - Images and ELFs: `tools/bench/runs/harness/ota-lowheap-arena-20261123/` (not committed).
- **Tool fix:** `ota_lowheap_knockout.py` now survives a console shed in the check job too (`au_job`).
  `console_shed` is a bit field: 1 = check, 2 = install.

| Node / run          | Idle free | Check job                                                                                  | Install job                                                                     |
| ------------------- | --------- | ------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------- |
| T-Beam R0 (web on)  | 52.7 KB   | block 51.2 KB after the BLE pause, `arena;ok`; gate 43044 + 32768 = 75812 < 76000, refused | not reached (`lowheap_ble_off`)                                                 |
| T-Beam R1 (web off) | 59.0 KB   | start block 43.0 KB, `arena;ok`, gate ok, `rec;hit;2;miss;0`, lowest 24.3 KB               | start block **27.6 KB, `arena;none`**; gate ok 79.1 KB; download fails `tls 51` |
| V3 R0 (web on)      | 100.3 KB  | `arena;ok`, gate ok, hit 2 miss 0; need with arena 58.7 KB (before 59-61)                  | `arena;ok`, gate ok; download fails `tls 52` after 10 s, 0 failed allocs        |
| V3 R1 (web off)     | 106.4 KB  | `arena;ok`, need 61.2 KB                                                                   | `arena;ok`; download fails `tls 51`                                             |

- **Stop 1, T-Beam install job without arena:** the risk named before the run. The check job's session splits the
  big block (43.0 -> 27.6 KB) between the jobs, so the arena (32 KB) does not fit at the install start.
  Options for the operator:
  - Keep the check job's arena alive and hand it to the install job (same mechanism as the restart adopt). This
    is the recommendation: the block exists at the check start.
  - Fallback (b), the streaming check (operator 2026-11-23).
- **Stop 2, the download fails on every image, not caused by `469bb942`:** about 10 s after the gate, with no
  failed allocation.
  - `tls 51/52` is not a TLS error. `McTlsClient::connectTo` stores the positive return of `mc_ssl_connect` (the socket
    fd) in `_lastError`, and `httpFail` prints it. This logging bug predates the change (new finding).
  - Bisect on the V3: `38041491` (before `469bb942`) and `6bbca7a2` (before FORMAL-01 `9696a3ba`) fail the
    same way. Round 2 downloaded fine at 17:39-18:10.
  - Only one session's records were allocated (`hit;2`), so the redirect hop never started. The first GET to
    github.com gets no usable answer.
  - From the Mac the same URL works (302 to `release-assets.githubusercontent.com`, 200, 1205064 B), also with
    TLS 1.2 and HTTP/1.1. The redirect URL is 947 characters and now carries a `jwt=` parameter; the response
    header is 5.8 KB. Assumption, not proven: a change on the GitHub side after 18:10.
  - Next step: a V3 image with HTTPClient/TLS debug output.
- **T-Beam PSRAM (open item closed):** `variants/ttgo_tbeam/platformio.ini` removes `-DBOARD_HAS_PSRAM` on purpose via
  `build_unflags` (R3-02 / MEM-04: `psramInit` costs 4427 B of IRAM). The image links neither `psramInit` nor
  `esp_spiram_init`. `psram 0` is by design; the 4 MB chip is present (earlier measurement in the ini comment).
- Fleet after the run: DK5EN-1 on the plain INSTRUMENT image of `469bb942`, DK5EN-92 on the heapwatch image of
  `469bb942`, both identity-guard ok, baseline restored (`restore=ok`).
- Evidence: `tools/bench/runs/harness/ota-lowheap-arena-20261123/` (`knockout.summary` T-Beam with the heapwatch V3
  run, `v3-plain/`, `v3-bisect-38041491/`, `v3-bisect-6bbca7a2/`; raw logs and `tbeam-serial.log` are local).

### Re-measurement on the plain INSTRUMENT image (19:50, operator: keep the 32 KB allocation)

- **Stop 1 resolved, it was the measurement image.** T-Beam on a plain `INSTRUMENT_ENABLED=1` image of `469bb942`
  (no heapwatch table; static DRAM 4.6 KB lower than the heapwatch image):
  - Install job after the BLE pause: largest block 42996 B (web on) and 49140 B (web off). The 32 KB buffer is
    allocated in both runs.
  - Gate passes with the web server on: 44660 + 32768 = 77428 >= 76000.
  - Check-job need: 53908 B (web on), 54588 B (web off).
  - No hand-over between jobs is needed; the design stays one 32 KB allocation per job (operator).
- **Rule:** measurement images are plain INSTRUMENT from now on. The heapwatch table changes both free heap and
  fragmentation.
- **Naming (operator):** "arena" is replaced by plain terms with the next code change: `src/fw_au_buf.h`, logs
  `[AU];buf32;ok|none`, `[AU];dry;dict;buf32`. Docs: "reserved 32 KB buffer".
- Still open: the download failure (stop 2); with it, the image check after the download.
- Breakdown per board: Desktop `ota-lowheap-heapmap-20261123.html`, section 1b. Evidence: `ota-lowheap-arena-20261123/tbeam-plain/`.
