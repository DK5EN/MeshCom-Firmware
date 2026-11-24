# AU-29 shortlist (advisor / verifier): T-Beam TASK_WDT during `--update install`

Verdict: **CONFIRMED, root mechanism = the staging erase in `stagePrepare()`** (src/esp32/fw_update_net.cpp:1510-1519 at HEAD 6ca39d7d).
It is not a regression of 878ebef4..HEAD: the same erase loop exists unchanged since e4886037/b7e99ffc, and the crash
hits images from 09:46 (pre-878ebef4), CI release 11.21 and HEAD alike, while HEAD also survives it. The outcome per run
is stochastic (about 4 crashes in 11 T-Beam installs that passed the gate).

Review basis: committed HEAD source, framework sdkconfig (framework-arduinoespressif32/tools/sdk/esp32/sdkconfig),
IDF v4.4.7 sources fetched from github.com/espressif/esp-idf (cache_utils.c, esp_flash_api.c, spi_flash_os_func_app.c,
spi_flash_chip_generic.c, task_wdt.c; copies in scratchpad/idf447/), bench logs under tools/bench/runs/harness/.

## 1. Mechanism, checked against IDF 4.4.7 source

| Claim (F3)                                      | Check                                                                                                                                                                                                                                                                                                                                                         | Result            |
| ----------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------- |
| Private chip uses the default SPI1 OS functions | `stageFlashArm()` copies `*esp_flash_default_chip->os_func` (only `region_protected` nulled) and keeps `os_func_data`: start/end = `spi1_start`/`spi1_end` -> `cache_disable()` -> `g_flash_guard_default_ops.start()` = `spi_flash_disable_interrupts_caches_and_other_cpu`                                                                                  | confirmed         |
| One 64 KB step = one bracket                    | `FW_STAGE_ALIGN 0x10000` (fw_update.h:268) makes every step block-aligned; `esp_flash_erase_region` (esp_flash_api.c:611-651) does start -> `erase_block` -> (len_remain==0) break -> end. `erase_block` polls `wait_idle` inside the bracket (spi_flash_chip_generic.c:196-218). No erase suspend on ESP32.                                                  | confirmed         |
| Other core spins for the whole bracket          | cache_utils.c:162 `esp_ipc_call(other_cpu, spi_flash_op_block_func)`; that func (l.108-127) does `vTaskSuspendAll()`, `esp_intr_noniram_disable()`, busy-waits `s_flash_op_complete`. Caller raises itself to `configMAX_PRIORITIES-1`; `CONFIG_ESP_IPC_USES_CALLERS_PRIORITY=y` so ipc0 runs at 24.                                                          | confirmed         |
| CPU1 is blocked too                             | Caller side `vTaskSuspendAll()` on CPU1 for the bracket. Bench: loopTask sections of 248/259/265/268 ms, 510-524 ms (2 blocks), 745-776 ms (3), 1185 ms (4) during the erase (tbeam-r1-serial.log l.735-764, 3314-3322, 4949-4958; chan-check crash run l.~400 `section_ms 259`).                                                                             | confirmed         |
| `CONFIG_SPI_FLASH_YIELD_DURING_ERASE` helps     | =y, 10 ms / 2 ticks. `on_spi1_check_yield` only runs between brackets (before the next start). With one block per call it adds one `vTaskDelay(2)` before each block after the first. It cannot split a 260-295 ms block.                                                                                                                                     | refuted as relief |
| Gap per block                                   | `vTaskDelay(1)` (HZ 1000: 0-1 ms) + IDF yield `vTaskDelay(2)` = about 2-3 ms of CPU0 per ~280 ms block, about 1 %                                                                                                                                                                                                                                             | confirmed         |
| fwnet core / prio                               | `FWNET_CORE 1` (fw_update_net.cpp:177-180), prio 1. The blackout lands on CPU0.                                                                                                                                                                                                                                                                               | confirmed         |
| TWDT 5 s, IDLE0 subscribed, CPU1 idle not       | sdkconfig l.988-992: `TIMEOUT_S=5`, `CHECK_IDLE_TASK_CPU0=y`, CPU1 not set, `PANIC=y`. IDLE0 feeds via idle hook (task_wdt.c:78). TWDT ISR allocated with flags 0 (task_wdt.c:214) = not IRAM: it fires when interrupts come back, so "CPU 0: ipc0" (inside `spi_flash_op_block_func` after `esp_intr_noniram_enable`) or the first task CPU0 picks ("wifi"). | confirmed         |
| CPU0 competitors in the gap                     | wifi task pinned core 0 (23), esp_timer (22), tcpip core 0 (18), main/arduino events core 1. BT controller pinned core 0 but deinitialised by the BLE pause. IDLE0 (0) runs only if all of them are blocked.                                                                                                                                                  | confirmed         |

## 2. Timing, checked against the logs

- Erase duration on the T-Beam: gate ok t_ms 98239 -> first fwnet alloc of the HTTP/TLS path t_ms 103846 (tbeam-r1-serial.log
  l.727-769) = **5.6 s** for 1240686 B = 19 blocks, about 260 ms per block plus gaps; same 5.6 s in the 17:45/17:49 runs.
  The loop section times above (259 ms per block) confirm it directly. 5.6 s > 5 s TWDT.
- Crash timing: chan-check (HEAD) gate ok 23:36:45.625 -> WDT 23:36:51.654 (6.0 s, log batching up to 1 s); 11.21 run 2
  install 07:56:51.31 -> WDT 07:56:57.33; run 1 ble pause 07:55:15.9 -> WDT 07:55:21.96. All inside the erase window, before any
  TLS byte; no download write has happened yet.

### Survive / crash tally (T-Beam installs that passed the gate)

| run                   | image                  | console | result                                        |
| --------------------- | ---------------------- | ------- | --------------------------------------------- |
| 17:35:26 knockout     | build 09:46 (pre)      | up      | **TASK_WDT** (reconnect shows RESET_REASON=6) |
| 17:38 / 17:45 / 17:49 | 13:04 (~878ebef4)      | shed    | survive, dl 1240686 B                         |
| 19:07:41 arena        | instr 18:55 (HEAD-era) | up      | survive (tls 51 later)                        |
| 19:52:34 / 19:55:25   | plain 19:48 (HEAD-era) | up      | survive (tls 52 / 51)                         |
| 23:33:22 chan-check   | plain HEAD             | up      | survive (conn -7 at +10 s)                    |
| 23:36:45 chan-check   | plain HEAD             | up      | **TASK_WDT** (wifi)                           |
| 07:55:15 / 07:56:51   | CI 11.21               | up      | **TASK_WDT** (ipc0) x2                        |

7 survive, 4 crash. HEAD both survives and crashes; the 09:46 image crashed too. So "878ebef4 good / later bad" is sampling,
not a code change.

## 3. Suspects

| #         | Suspect                                                                  | Verdict                                            | Evidence                                                                                                                                                                                                                                                              |
| --------- | ------------------------------------------------------------------------ | -------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| S1        | Stage erase loop (19 x 64 KB brackets, 2-3 ms gaps, 5.6 s) starves IDLE0 | **CONFIRMED**                                      | Section 1 and 2. Necessary cause; whether IDLE0 slips into one gap decides the run.                                                                                                                                                                                   |
| S2        | Net console up (no service shed) fills the gaps                          | REFUTED as discriminator, possible contributor     | 4 HEAD-era runs survived with the console up (19:07, 19:52, 19:55, 23:33). The 3 "good" shed runs are not explained by the shed alone.                                                                                                                                |
| S3        | Loop-task NVS/flash ops in the gaps (first beacon at ~100 s uptime)      | UNRESOLVED (low)                                   | Crashes at 100.9-104.7 s uptime; but 17:38 survived at 98 s with a POS beacon inside the erase.                                                                                                                                                                       |
| S4        | WiFi task backlog after each blackout                                    | UNRESOLVED (contributor, same as S1 load)          | "CPU 0: wifi" in the HEAD crash; no link-down. Not an independent trigger.                                                                                                                                                                                            |
| S5        | BLE pause / controller deinit (NB-04..07)                                | REFUTED                                            | CHECK pauses BLE identically and never resets; 11.21 has no NB-04..07 and resets; gate settle lets IDLE0 run between pause and erase; WDT is 5 s after erase start, not after the pause. NB-06 is behaviourally identical to `nimble_port_freertos_deinit` (F4 S-04). |
| S6        | Private `esp_flash_t` copy breaks yield bookkeeping                      | REFUTED                                            | `os_func_data` shared, same guard; yield cannot split a block anyway; per-block time steady at ~260 ms.                                                                                                                                                               |
| S7        | Asset growth pushed the erase over 5 s                                   | UNRESOLVED (plausible why-now)                     | 10.16 asset 1221908 B and 11.22 1240686 B are both 19 blocks; margin is per-block time, not count.                                                                                                                                                                    |
| S8/S9/S14 | ipc0 prio inheritance, fwnet on CPU1, TWDT config                        | CONFIRMED as mechanism detail, not separate causes | Section 1.                                                                                                                                                                                                                                                            |
| S10       | `nvsClearRecord()` before the erase                                      | REFUTED as cause                                   | Tens of ms next to 5.6 s; present in surviving runs.                                                                                                                                                                                                                  |
| S11       | buf32 fallback / probe                                                   | REFUTED                                            | 11.21 has no buf32 and crashes; malloc runs on CPU1.                                                                                                                                                                                                                  |
| S12       | HEAD-only CPU0 code (mc_dns, lwIP wrap, idf_override mdns/dhcpserver)    | REFUTED                                            | 11.21 and the 09:46 image lack them and crash. DNS runs after the erase.                                                                                                                                                                                              |
| S13       | INSTRUMENT heap trace                                                    | REFUTED                                            | 11.21 crash log has no INSTR lines.                                                                                                                                                                                                                                   |
| W         | Flash WRITE path during the download (dlFeed, 4 KB per call)             | REFUTED for AU-29, low residual risk               | Crash precedes the first write. One 4 KB `esp_flash_write` = 16 pages, about 10 ms bracket; IDF yields between 8 KB chunks; network waits give CPU0 long gaps; 3 full 17-18 s downloads survived.                                                                     |
| F4        | "dev install on HEAD erased in < 2 s"                                    | REFUTED for T-Beam                                 | On the T-Beam, HEAD conn -7 came 10 s after gate ok (23:33:22 -> 23:33:32), i.e. after the full 5.6 s erase. The < 2 s figure is the V3 (ESP32-S3, different flash).                                                                                                  |

## 4. Fix recommendation

**Recommended (least invasive): keep 64 KB block erases, replace the 1-tick yield by a real sleep.**

```cpp
// fw_update_net.cpp, stagePrepare(): erase loop
#define FWNET_ERASE_GAP_MS 20u   // CPU0 idle time between block erases: each 64 KB block holds CPU0 in ipc0 for ~260-300 ms
...
        vTaskDelay(pdMS_TO_TICKS(FWNET_ERASE_GAP_MS));   // was vTaskDelay(1)
```

- Effect: 19 gaps of 20 ms instead of 2-3 ms. After a 300 ms blackout the CPU0 backlog (wifi, esp_timer, tcpip) is a few ms,
  so IDLE0 runs in (nearly) every gap; worst-case IDLE0 gap drops from > 5 s to about one or two blocks (0.3-0.6 s).
  Cost: +0.4 s on a 5.6 s erase. No change to erase semantics, record invariants or the write path.
- Optional hardening (only if the bench shows a gap > 1 s): wait in the gap until IDLE0 has run, bounded:
  register `esp_register_freertos_idle_hook_for_cpu(cb, 0)` for the erase (cb sets a volatile flag, returns true), and loop
  `vTaskDelay(pdMS_TO_TICKS(5))` until the flag is set or 100 ms passed; deregister afterwards.

Rejected alternatives:

- 4 KB sector steps: sector erase costs about 45-60 ms per 4 KB versus ~260 ms per 64 KB, so 303 sectors take about 15 s
  instead of 5.6 s, and each still blacks out CPU0. Only shorter brackets, much longer total. 32 KB steps are not available:
  `esp_flash_erase_region` only knows sector (4 KB) and 64 KB block erase, so a 32 KB request becomes 8 sector erases.
- Interleave erase with the download (erase each 64 KB just ahead of the write): spreads the blackouts over the 17 s download,
  but puts 260-300 ms CPU0 stalls inside a live TLS/TCP session (RX buffer overrun, retransmits) and changes stage ordering;
  more code and more risk for the same gain.
- Unsubscribe IDLE0 from the TWDT during the erase (`esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(0))` / `esp_task_wdt_add`):
  hides the starvation instead of removing it, needs a guaranteed re-add on every exit path, and leaves CPU0 at ~1 % for 5.6 s
  (WiFi, LoRa-gateway UDP). Fallback only.

Regression test (per CLAUDE.md): a native Unity test cannot reproduce IDLE0 starvation. Pin the invariant instead: move the
step and gap into `fw_update.h` (`FW_ERASE_STEP`, `FWNET_ERASE_GAP_MS`) and add a native test that asserts
`FWNET_ERASE_GAP_MS >= 10` and that the worst-case modelled IDLE0 gap (step blackout 300 ms) is < 5 s; the real proof is the bench run below.

## 5. Bench proof

Log lines (production, cheap; one before, one after the loop):

```
[AU];erase;start;len;1241088;blk;19
[AU];erase;ok;ms;5980;maxblk;301;maxgap;24
```

(`ms` = whole loop, `maxblk` = longest single `stageFlash(STAGE_ERASE)` via `esp_timer_get_time()`, `maxgap` = longest inter-block wait.)

INSTRUMENT-only addition: CPU0 idle-hook stamp, logged as `;idle0max;<ms>` = longest time between two IDLE0 hook runs
during the loop. Expectation: before the fix `idle0max` > 5000 in crashing runs (and > 1000 in surviving ones); after it
`idle0max` < 700.

Acceptance on DK5EN-92 (T-Beam, BLE paused, console UP, no shed, install started at ~100 s uptime like the crashes):
10 consecutive `--update install` runs, 0 TASK_WDT, every run shows `erase;ok` and `idle0max` < 1000. Control: the same
10-run series on the unfixed image should reproduce about 1 in 3 resets.
