# FORMAL-01: TLA+ and bounded model checking (CBMC/ESBMC) for the firmware

Goal: long-term stability (no lost, duplicated or torn frames, no stuck state, no deadlock, bounded resources) and
non-exploitability (no out-of-bounds access, no size underflow, no undefined behaviour on any untrusted input). The
checks in `../software-audit.md` that can be stated as a property become a model or a proof; the rest stays review and
test.

Operator 2026-11-23: "TLA+ jetzt ausbauen und dann noch CBMC/ESBMC für unseren C code mit einbauen. Ziel ist
Langzeitstabilität und non exploitability."

> [!IMPORTANT]
> **Superseded for the verification part (2026-11-23):** review `formal-review-20261123.md`, current setup and rules
> `../specs/README.md`. The heap models and several configurations named below were archived, renamed or removed.
> The library patch waves below are unaffected.

## Rules

- Verification only in waves 1-2: no edits in `src/`, `test/`, `platformio.ini`. A harness that needs a seam files a
  request; seams and fixes are wave 3, each with a regression test.
- Every property that holds has a mutation (TLA+ cfg or CBMC job with `EXPECT=fail` + `EXPECT_PROP`) that must turn
  red for the named reason. A green check without a red twin is not evidence.
- A real defect found today is recorded as an expected-red job (`defect_<id>.check`, `<spec>_today_*.cfg`) with the
  counterexample, so the gate stays green and the defect stays visible. The fix wave flips it to a pass job.
- CBMC/ESBMC always run ILP32 (`--32`) via `specs/cbmc/run_cbmc.sh`; a callee without a body is a harness error.
- Bounds: a proof is complete only up to its stated input length and unwind bound; both go in the results table.

## Tooling

- TLC: `specs/tla/run_tlc.sh` (`--all` now also picks up any cfg with the marker `\* tlc: spec=<Spec>.tla`).
- CBMC 6.11.0, ESBMC 8.7.0 (Homebrew). Runner `specs/cbmc/run_cbmc.sh <group>/<job>.check | --all | --list`.
- Template job: `specs/cbmc/conf_frame/` (parseConfFrame, pass + two mutations).
- Memory cap: `run_cbmc.sh` kills a job above `MEM_MB` resident (default 6144) and reports `memout` (a tool error);
  every job logs its peak RSS. Added after the session ran the 24 GB Mac out of memory on 2026-11-23. Gate runs
  stay sequential: one runner at a time, no parallel BMC agents.

## Waves

| wave | content                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  | status  |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------- |
| 0    | tools, runners, template job, this doc                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   | done    |
| 1    | 3 TLA+ writers + 4 CBMC/ESBMC writers, disjoint dirs (table below)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       | running |
| 2    | gate: `--all` both runners, README tables, regression.sh stage 2 hook                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    | open    |
| 1b   | libraries (operator 2026-11-23: suspects BLE lib, ETH stack, TCP/UDP, web server): (1) pinned versions vs upstream fixes/CVEs (running), (2) ESBMC boundary proofs with the library as adversary: NimBLE/Bluefruit onWrite, AsyncWebServer onBody/onUpload (index/len/total), UDP read, W5100S recv, (3) library-internal BMC: ESPAsyncWebServer request + multipart parser, W5100S driver incl. our patches, NimBLE ATT (prepare) write path; lwIP and the full NimBLE host out of scope (size; lwIP is fuzzed upstream), (4) TLA+: AsyncTCP task vs loop, NimBLE host task vs loop (F2-8), W5100S/LoRa SPI bus (F2-19) | open    |
| 3a   | library fixes: NimBLE backports (`tools/nimble_patch.py`), W5100S upstream fixes (`tools/w5100s_patch.py`), safeboot ESP32Async bump (safeboot envs + `src/safeboot/`), IDF 4.4 review (read-only)                                                                                                                                                                                                                                                                                                                                                                                                                       | running |
| 3b   | library patches from the review verifications (ownership below), rework done, gate with 3c/3d                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | running |
| 3    | seams (handleACK, readPhoneCommand length, UDP RX, url decode core) and fixes for confirmed defects, operator decides which                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              | open    |

### Wave 3b ownership (library patches, dispatched 2026-11-23)

| agent | owns                                                                      | targets                                                                        |
| ----- | ------------------------------------------------------------------------- | ------------------------------------------------------------------------------ |
| L1    | `tools/nimble_patch.py`, `tools/tests/test_nimble_patch.py`               | NB-04 deinit after failed stop, NB-05 init unwind + bounded sync, NB-06, NB-07 |
| L2    | `src/esp32/lwip_mtu_guard.*`, `test/test_lwip_mtu_guard/`                 | lwIP #2 DHCP MTU floor via `-Wl,--wrap` (link-time, prebuilt lib)              |
| L3    | `tools/arduino_esp32_patch.py`, `tools/tests/test_arduino_esp32_patch.py` | IDF #4 HTTPClient header cap, #5 alloc failure without abort, #8 hostByName    |
| L4    | `src/fw_tls_time.h`, `test/test_fw_tls_time/`, `src/esp32/mc_tls/*`       | mbedTLS #1 own certificate validity check, IDF #7 minimum TLS 1.2              |

Orchestrator: `platformio.ini` (two native envs added before dispatch; `--wrap` flags and the AE pre-script at the gate).
Not in 3b: NimBLE #1 (src, characteristic max length), lwIP #4/#5 dhcpserver override, mbedTLS #2 calloc cap,
IDF #3 DNS thread safety.

### Wave 3c+3d ownership (dispatched 2026-11-23, 3b gate aborted by operator, one gate for 3b+3c+3d)

| agent | owns                                                                                           | target                               | build env                              |
| ----- | ---------------------------------------------------------------------------------------------- | ------------------------------------ | -------------------------------------- |
| M1    | `src/esp32/idf_override/mdns.c`, `README.md`                                                   | IDF #1, lwIP #3/#6/#7/#9 (MD-0x)     | ttgo_tbeam                             |
| M2    | `src/esp32/idf_override/dhcpserver.c`, `README-dhcpserver.md`                                  | lwIP #4/#5, IDF #2 (DS-0x)           | E22_1262-DevKitC                       |
| M3    | `src/esp32/esp32_main.cpp` (BLE), `src/esp32/fw_update_net.*`                                  | NimBLE #1/#7, mbedTLS #2             | heltec_wifi_lora_32_V3                 |
| M4    | `src/esp32/mc_dns.*`, DNS calls in `aprs_is_client.cpp`, `udp_functions.cpp`                   | IDF #3                               | ttgo-lora32-v21, t_echo                |
| D1    | `udp_drain_esp32.cpp`, `udp_drain_nrf52.cpp`, `byte_fifo.*`, `test_byte_fifo`, bytefifo*       | F2-5 dup/loss                        | heltec_wireless_stick, wiscore_rak4631 |
| D2    | `src/extudp_functions.*` (not the DNS line), optional `ext_queue_core.h`                       | F2-14                                | ttgo_tbeam_SX1262, heltec_t114         |
| D3    | `src/net_console.*`, `test_net_console_gate`, netconsole*                                      | F2-10 slot takeover of a silent peer | E22_1262_S3-DevKitC-1-N16R8            |
| D4    | `aprs_functions.cpp` (encodeStartAPRS), `fw_update_assets.h`, their tests, cbmc aprs/fw_assets | F3-8, NUL in url                     | native only                            |

Orchestrator at the gate: NimBLE `-D` flags in `[esp32]`, new native env blocks, the extudp DNS line, golden baseline.

### Wave 3c plan (remaining reachable review findings, after 3b is committed)

Status table per finding: `review/formal01-20261123/README.md`. Feasibility (scout, 2026-11-23):

- DHCP server (lwIP #4/#5, IDF #2): `dhcpserver.c.obj` is a member of `liblwip.a`; a patched copy of the IDF 4.4.7
  source that defines all 9 externally referenced symbols replaces it at link time (member never pulled).
- mDNS (IDF #1 recursion, lwIP #6/#9): `mdns.c.obj` lives in `libmdns.a`; a patched `mdns.c` with the public API that
  ESPmDNS uses (12 functions) replaces it; it must not define the 6 `mdns_networking_lwip` symbols. Sources:
  `~/WebDev/formal01-review/review-lwip/code/idf/` (branch `review`) and `.../src/esp-idf/components/mdns/`.
- NimBLE #7: `-DMYNEWT_VAL_BLE_ATT_SVR_MAX_PREP_ENTRIES=<n>` and `-DMYNEWT_VAL_BLE_ATT_SVR_QUEUED_WRITE_TMO=<ms>`
  override the `#ifndef` defaults (64, 30000 ms; `esp_nimble_cfg.h:319-332`).
- NimBLE #1: src, characteristic max length and a static onWrite item.
- mbedTLS #2: `mbedtls_platform_set_calloc_free` exists in `libmbedcrypto.a`; already used at `fw_update_net.cpp:325`
  (PSRAM boards only).
- IDF #3: loop-task DNS callers use `dns_gethostbyname` directly (`extudp_functions.cpp:184`,
  `aprs_is_client.cpp:321`, `udp_functions.cpp:602`); blocking `hostByName` only in the AU task and instrument_probe.

### Wave 1 ownership

| agent | owns                                                                        | targets                                                                              |
| ----- | --------------------------------------------------------------------------- | ------------------------------------------------------------------------------------ |
| T1    | `specs/tla/txring.tla`, `txring*.cfg`, `traces/txring*`                     | TX slot pool (F2-4/N-14): addTxRingEntryCore, txRingBeginTx/FinishTx, txRingSweep    |
| T2    | `specs/tla/extqueue*`, `rxflags*`, their traces                             | queueExtern 2-slot ring (F2-14, open), ESP32 RX flag pair (F2-7, open)               |
| T3    | `specs/tla/bytefifo*`, `netconsole*`, their traces                          | byte_fifo gen counters (F2-5), net_console session/auth (F2-10)                      |
| C1    | `specs/cbmc/fw_assets/`                                                     | FwJsonScan (`src/fw_update_assets.h`): fwScanFeed, beginAsset/endAsset, fwScanResult |
| C2    | `specs/cbmc/aprs/`                                                          | decodeAPRS (uint8 path), encodeStartAPRS (F3-8), decodeAPRSPOS(const char*)          |
| C3    | `specs/cbmc/url_decode/`, `specs/cbmc/settings_decode/`                     | url_percent_decode (`url_decode.h`), settings_store decode_* / unescape_string       |
| C4    | `specs/cbmc/remote_cmd/`, `specs/cbmc/applink_ws/`, `specs/cbmc/byte_fifo/` | parseSmallInt/RM parsers, AppLink WS frame decoder, byte_fifo index arithmetic       |

Orchestrator owns: both runners, both READMEs, `tools/regression.sh`, this doc, BACKLOG/RESUME.

Deferred: NBR matrix lock model (G), dedup and deferred save (single writer, low value), parsers that need a seam
(wave 3).

## Results

(filled at the wave 2 gate)

## Wave 1b track 1: dependency audit (2026-11-23, read-only, links in the session report)

| dependency                        | pin                     | latest            | reached                             | verdict                       |
| --------------------------------- | ----------------------- | ----------------- | ----------------------------------- | ----------------------------- |
| NimBLE-Arduino                    | 2.2.3                   | 2.5.2             | yes, peripheral GATT, no bonding    | BUMP-RECOMMENDED              |
| RAK13800-W5100S (icssw fork)      | 091234f                 | RAK 1.0.4         | yes, nRF52 UDP/TCP/web/applink      | BUMP (cherry-pick ebea549)    |
| ESPAsyncWebServer (mathieucarbou) | 3.3.23                  | ESP32Async 3.12.1 | safeboot only, multipart OTA upload | BUMP for safeboot             |
| AsyncTCP (mathieucarbou)          | 3.2.14                  | ESP32Async 3.6.1  | safeboot only                       | CHECK (bump with the above)   |
| ElegantOTA                        | vendored, 3.0.0-derived | 4.0.0             | safeboot only                       | NOT-REACHED (no security fix) |
| Adafruit nRF52 core / Bluefruit   | 1.7.0                   | 1.7.0             | -                                   | current                       |
| arduino-esp32 2.0.17 / IDF 4.4    | espressif32 ^6.13.0     | 3.3.13 / 4.0.0    | WiFi/ETH/TLS                        | CHECK, EOL base, no backports |

- NimBLE 2.5.2 carries the upstream fix of our AU-19 patch (host timer deinit) plus NULL/use-after-free fixes (2.5.1,
  2.5.2) and the multi-mbuf notify loss (2.4.0, relevant with MSYS1_BLOCK_COUNT=4). Bump risk: NimBLEService::start
  deprecated, FreeRTOS port change, `tools/nimble_patch.py` must be re-validated (AU-20 flow-callout not confirmed fixed).
- W5100S RAK 1.0.3: OOB on `state[MAX_SOCK_NUM]` when socket allocation fails (4 HW sockets, MAX_SOCK_NUM 8) and an
  `execCmdSn` endless loop ending in a watchdog reset. Fork is 9 ahead / 3 behind; merge with the ETH-06 patch.
- ESPAsyncWebServer: GHSA-4phx-fcj6-46r4 (multipart boundary integer overflow, fixed 3.11.1), GHSA-8m8p-vhxc-jmjw
  (multipart NULL write, fixed 3.11.2) hit the safeboot OTA upload path; AsyncTCP abort() use-after-free fixed 3.5.0.
  Move to ESP32Async/* (registry rename, CONFIG flags change, RAM: safeboot is size-tight). AsyncTCP 3.6.1, not 3.6.0.
- IDF 4.4 lwIP/mbedTLS exposure unknown (advisories list 5.x ranges only). Not checked: NimBLE ble_hs_flow.c diff,
  mbedTLS CVEs of the 2.0.17 bundle, sdkconfig DNS/DHCP-server options, S140 SoftDevice advisories.
- Operator decisions 2026-11-23:
  - NimBLE: no bump (2.5.2 rejected). Backport every reachable fix into 2.2.3 as anchored patches in
    `tools/nimble_patch.py` (wave 3a).
  - W5100S: cherry-pick the upstream RAK fixes so the fork is current (wave 3a, as patches in `tools/w5100s_patch.py`).
  - Safeboot: bump to ESP32Async/ESPAsyncWebServer >= 3.11.2 and ESP32Async/AsyncTCP 3.6.1 (wave 3a).
  - arduino-esp32 2.0.17 / IDF 4.4: EXCEPTION, no migration ("die Aenderungen sind zu krass, das ueberlebt das
    Projekt nicht"; would need full-fleet hardware regression). Do not re-propose; lwIP/mbedTLS exposure is handled by
    targeted mitigations only.
  - arduino-esp32 2.0.17 / IDF 4.4 instead gets a code review (operator: "wir muessen da einen code review
    durchfuehren"): known fixes in IDF release/v4.4, esp-lwip 2.1.3-esp, mbedTLS 2.28.x vs. the bundled versions,
    plus a source review of the Arduino layer we reach (WiFiUDP, HTTPClient, WiFiClientSecure, ETH). The IDF parts are
    prebuilt libs: fix route there is mitigation in our code; the Arduino layer is source and can be patched by a
    pre-script like `tools/nimble_patch.py` (wave 3a, review running).
- Tracks 2-4 (boundary proofs, library-internal BMC, task models) still open.

## Wave 3c+3d results (2026-11-23, written, NOT built at the end, gate pending)

Operator 2026-11-23: no TLC/ESBMC runs unless explicitly requested; no board builds inside agents (builds only at
the gate). Models and BMC jobs below are written, not run.

- M1 mDNS: `src/esp32/idf_override/mdns.c` (IDF 4.4.7, byte-identical to the libmdns.a source), MD-01 depth cap 8
  (IDF #1), MD-02 (lwIP #9), MD-03 (#3), MD-04 (#7), MD-05 (#6). Gate: `nm -l` must place mdns_init in our file.
- M2 DHCP server: `src/esp32/idf_override/dhcpserver.c`, DS-01 malloc checks (#4), DS-02 bounded option walk (IDF #2),
  DS-03 unconfirmed leases 30 s + eviction at 8 stations (#5), DS-04 datagram length 240..1500 + full pbuf copy.
  Open: under a full DISCOVER+REQUEST flood the oldest confirmed lease is evicted (client re-asks) instead of
  refusing new MACs. Gate: map/nm must show our dhcps_start.
- M3 NimBLE #1: TX/RX max length 300, static onWrite item. #7: `MAX_PREP_ENTRIES=2`, `QUEUED_WRITE_TMO=5000`
  (long writes need MTU >= 155). mbedTLS #2: capped calloc during AU jobs on every ESP32 (24 KiB per allocation,
  128 KiB live), `src/fw_tls_alloc_cap.h`, native_fw_tls_alloc_cap 8/8 (before the build stop).
- M4 IDF #3: `src/esp32/mc_dns.*` runs dns_gethostbyname in the tcpip thread (tcpip_callback + semaphore); callers in
  aprs_is_client.cpp, udp_functions.cpp, extudp_functions.cpp (orchestrator).
- D1 F2-5: both drains use bf_peek_gen / bf_pop_if (one lock section each); cfgs bytefifo_fix_udp_{dup,loss},
  bytefifo_was_udp_{dup,loss}. native_byte_fifo 27/27.
- D2 F2-14: `src/ext_queue_core.h`, writer checks `used` before the copy, drops counted (`externQueueDrops()`);
  native_ext_queue (old writer: 3/4 fail, new: 4/4, run by hand before the stop). Open for the advisor: nRF52 has no
  lock around the ring, ordering only (as in the model).
- D3 F2-10: a new connection takes the handshake slot from a holder silent for 3 s (shutdown, generation counter);
  `src/net_console_slot.h`, 5 tests in test_net_console_gate (not run); cfgs netconsole_fix_slot,
  netconsole_mut_preemptactive, netconsole_was_slotlockout (not run, spec not parsed).
- D4 F3-8: copy capped at 249 B; NUL/control byte in a JSON string fails the asset scan. native_aprs_fuzz 13/13,
  native_fw_update_assets 24/24, native_aprs 222/222; BMC jobs enc_pass_widepath, enc_mut_widepath, pass_nulurl,
  mut_nulurl written, not run.

## Wave 3a results (2026-11-23, built, not on the bench, not committed)

- NimBLE 2.2.3 backports in `tools/nimble_patch.py`: NB-01 zero-length write leaves the old value readable
  (upstream 2b2a56c8), NB-02 failed realloc in `NimBLEAttValue::append()` leaves a fake capacity, heap overflow
  (7d0a3202), NB-03 ACL-full retry waits 10 ms (83d43378). AU-19 = upstream 823624df (kept, linked); AU-20 still
  unfixed upstream (kept). Every other listed fix is not in 2.2.3 or compiled out (client/scan/whitelist) or unused.
  Open: the 2.3.0 esp-nimble core update (~1300 lines) is not reviewed; in wave 1b (operator 2026-11-23: "ja, nimble core review
  in welle 1b mit aufnehmen"): peripheral, GATT server, ATT write, L2CAP paths only. Heltec V3 +16 B, T-Beam +12 B flash.
- W5100S: ETH-07 in `tools/w5100s_patch.py` ports RAKWireless ebea549 (20 `s >= MAX_SOCK_NUM` guards in socket.cpp,
  EthernetUdp.cpp, EthernetClient.cpp). The other upstream commits are not fixes. RAK4631 +128 B.
- Safeboot: ESP32Async/ESPAsyncWebServer 3.12.1 + ESP32Async/AsyncTCP 3.6.1, no code change, flash smaller (classic
  690208 B of 720896, S3 651520 B). Bench checklist: multipart upload of a real image on classic + S3, abort
  mid-upload then a second upload, 400 on a bad image, async_tcp stack 4096 under upload.

## IDF 4.4 review (2026-11-23, IDF v4.4.7, lwIP 2.1.2 esp a45be9e, mbedTLS 2.28.7)

No remote code execution path found for our config. Fix routes: (a) Arduino source patch by pre-script, (b)
mitigation in our code, (c) lib rebuild, (d) accept.

| #   | severity       | finding                                                                                                   | reach                     | route                                               |
| --- | -------------- | --------------------------------------------------------------------------------------------------------- | ------------------------- | --------------------------------------------------- |
| 1   | high (DoS)     | mDNS name decompression recursion without depth cap (`mdns.c:338-343`, = GHSA-7fpr-vjw5-2c86)             | LAN/AP, UDP 5353, no auth | (b) mDNS off by default, or patched mdns.c in build |
| 2   | med (DoS)      | DHCP server option parser reads past the packet (`dhcpserver.c:739-786`, = GHSA-g764-gwc3-75m5)           | AP clients (open softAP)  | (b) WPA2 password on the softAP                     |
| 3   | med            | lwIP DNS called outside tcpip_thread with core locking off (udp/extudp/aprs_is, Arduino hostByName)       | any DNS use               | (b) tcpip_callback / tcpip_api_call; (a) hostByName |
| 4   | med            | HTTPClient header lines without length cap or total deadline (`HTTPClient.cpp:1259`)                      | update server / MITM      | (a) cap, or (b) own header reader                   |
| 5   | med (low heap) | WiFiUDP/WiFiClient allocation failure aborts instead of failing (`WiFiUdp.cpp:227`, `WiFiClient.cpp:191`) | LAN traffic at low heap   | (a)                                                 |
| 6   | low-med        | mbedTLS 2.28.7 handshake on alloc/SHA failure (CVE-2025-27810, CVE-2026-50581)                            | MITM + induced alloc fail | (b) keep the heap gate before the handshake         |
| 7   | low            | TLS client allows TLS 1.0, RSA kx, CBC (`mc_ssl_client.cpp:64` defaults)                                  | MITM                      | (b) min TLS 1.2, ECDHE+AEAD suites                  |
| 8   | low            | hostByName callback writes the caller's stack after timeout (`WiFiGeneric.cpp:1551-1580`)                 | slow DNS                  | with #3                                             |
| 9   | low            | WiFiClient::connected() misses FIN                                                                        | LAN                       | (d) web loop capped at 2 s                          |
| 10  | low            | Content-Length via toInt, indexOf(':') -1 truthy                                                          | server                    | (d) handled by clen/maxBytes checks                 |

Not covered: WiFi/PHY blobs, safeboot (IDF 5.x), ETH_PHY_W5500 envs, Bluetooth, no exploit reproduction (#1, #2
by root-cause equivalence).

## Library reviews via `/code-review ultra` (operator 2026-11-23: one repo per library, three separate runs)

Local repos outside the firmware tree, `main` = README (threat model, our config, known items) + `config/sdkconfig.*`,
branch `review` = the exact shipped sources under `code/`:

| repo                                      | content                                                                             | diff         |
| ----------------------------------------- | ----------------------------------------------------------------------------------- | ------------ |
| `~/WebDev/formal01-review/review-nimble`  | NimBLE-Arduino 2.2.3 peripheral path incl. our patches (AU-19, AU-20, NB-01..03)    | 84576 lines  |
| `~/WebDev/formal01-review/review-lwip`    | esp-lwip a45be9e core + IDF 4.4.7 lwip port, dhcpserver, mdns.c, esp_netif/lwip     | 89077 lines  |
| `~/WebDev/formal01-review/review-mbedtls` | espressif/mbedtls 2b8e772 (2.28.7) TLS client, X.509/ASN.1, crypto + IDF 4.4.7 port | 111984 lines |

Operator starts each run (`/code-review ultra` on the `review` branch); findings come back here, get verified against
our code, and real ones become patches or mitigations in wave 3. Status: repos ready, runs not started.

### lwIP review run (operator, `/code-review high`, 2026-11-23): 9 claimed findings, verification running

1 ND6 Packet Too Big without 1280 floor -> frag size 0 -> endless loop (nd6.c:928); 2 DHCP option 26 MTU without
minimum -> same loop (dhcp.c:803); 3 mDNS NULL question list (mdns.c:3272); 4 DHCP server malloc unchecked
(dhcpserver.c:906, :912); 5 DHCP lease exhaustion on the open AP (dhcpserver.c:1319); 6 mDNS A/AAAA copy without
rdlength (mdns.c:3442, :3492); 7 mDNS TXT parser heap overflow during active query (mdns.c:3012); 8 ND6 LLA option
length 0 over-read (nd6.c:362 ff.); 9 mDNS compression pointer 1-byte over-read (mdns.c:338). Libraries are prebuilt:
fixes are mitigations in our code or single-object overrides.

### NimBLE review run (operator, `/code-review high`, 2026-11-23): 10 claimed findings, verification running

1 stack buffer sized to characteristic max length (512) in the write handler vs. 3072 B host stack
(NimBLEServer.cpp:645); 2 deinit(clearAll) frees server objects after a failed stop, use-after-free
(NimBLEDevice.cpp:962); 3 old host task deletes the new one on quick deinit/init (nimble_port_freertos.c:73);
4 partial init failure not undone, sync wait without timeout (NimBLEDevice.cpp:888, :928); 5 setValue over-read after
failed append (NimBLEAttValue.cpp:111); 6 our NB-01 writes m_attr_value[0] without NULL check (NimBLEAttValue.cpp:109);
7 four prepare writes hold all 4 msys buffers 30 s (ble_att_svr.c:2497); 8 stop counter never reset
(ble_hs_stop.c:263); 9 bond deleted early (bonding off); 10 getPeerInfo index (1 connection). NimBLE is built from
source: real findings become patches in `tools/nimble_patch.py` (wave 3).

### mbedTLS review run (operator, `/code-review high`, 2026-11-23): 8 claimed findings, verification running

1 `CONFIG_MBEDTLS_HAVE_TIME_DATE` off -> certificate validity dates never checked (x509.c:1056): expired leaked
certificate could serve an update; 2 unbounded allocation while parsing a certificate (~5000 empty name entries, ~100
KB) before verification (x509_crt.c:692, ssl_tls.c:2410); 3 S3 DMA AES leak on second allocation failure
(esp_aes.c:266); 4-8 port-layer bignum/GCM/CTR issues claimed unreachable from TLS. mbedTLS is prebuilt: mitigations
in `src/esp32/mc_tls/` (own validity check in a verify callback, heap gate, min version / suites).

#### lwIP verification (Opus, 2026-11-23): all 9 real in the code, 3 (+1) reachable in our firmware

- #2 DHCP option 26 MTU 20..27 -> `ip4_frag` endless loop -> WDT reset, on STA and ETH, at every renew (boot loop by
  a rogue DHCP server). Our GOT_IP clamp (`udp_functions.cpp:435`) covers only the first bind (race, plausible), not a
  renew with unchanged IP; ETH clamps once (`esp32_eth.cpp:116/143`). Mitigation: `-Wl,--wrap=ip4_frag` that restores
  a sane `netif->mtu` (>= 576) before `__real_ip4_frag`; same change can wrap `nd6_get_destination_mtu` (#1).
- #3 mDNS NULL deref needs a registered service: app registers none (`web_functions.cpp:447`); safeboot adds `http`
  (`safeboot/main.cpp:358`) on an open AP. Caveat: safeboot builds on IDF 5.3 (tasmota), whose mdns component differs
  from the reviewed 4.4.7 copy: re-check there before acting.
- #5 / #4 DHCP lease exhaustion / unchecked malloc on the open softAP (`udp_functions.cpp:733`, unconfigured node
  `esp32_main.cpp:1709`, safeboot): mitigation AP password + lower max_connection; dhcpserver.c object override optional.
- #6, #9 mDNS over-reads reachable (mDNS on by default, `bMDNSOK=true` `web_functions.cpp:372`): low / negligible.
- #1, #7, #8 not reachable: IPv6 never enabled on a netif; no mDNS queries in src.

#### mbedTLS verification (Opus, 2026-11-23): #1 and #2 reachable, rest real but not reachable

- Integrity chain: no Secure Boot, no image signature; the image is checked only by a sha256 from the release JSON
  (`fw_update_net.cpp:1294-1298`, refused :1397), fetched over the pinned api.github.com session; the CDN download may
  run unverified (`McTlsClient.cpp:108`). Whoever wins the api.github.com handshake controls URL and digest.
- #1 certificate validity never checked: confirmed in the prebuilt lib (`mbedtls_x509_time_is_past` disassembles to
  `movi a2,0; retw`, checked by the orchestrator). Medium: needs the key of an expired certificate under a pinned
  root (5 roots with the full-bundle retry), no CRL/OCSP either. Fix in `mc_ssl_client.cpp`: `mbedtls_ssl_conf_verify`
  callback with `now_eff = max(node UTC if valid (NTP/GPS/RTC), build epoch floor)`; EXPIRED if `valid_to < now_eff`,
  FUTURE only with a valid clock; skip the full-bundle retry on those flags. libc time is never set, so
  `HAVE_TIME_DATE` would not help even after a rebuild. AU is not blocked before time sync (floor still applies).
- #2 unbounded certificate parsing allocations before verification: low-medium, transient heap starvation by an
  on-path attacker. Fix option: capped `mbedtls_platform_set_calloc_free` wrapper armed only during an AU session
  (global hook, caveat).
- #3-#8 port-layer defects confirmed but not reachable in our TLS use (internal buffers, ECC, GCM nc_off 0); #6
  (RSA > 4096) only a watch item for the root list.

- Operator decision 2026-11-23: **no password on the softAP** ("kein Passwort auf dem SoftAP"). EXCEPTION, do not
  re-propose. Consequence: lwIP #4/#5 (DHCP server malloc, lease exhaustion) stay open over the AP; remaining options
  without a password: heap reserve before AP start, dhcpserver.c object override (NULL checks, lease reclaim),
  safeboot without the mDNS `http` service while the AP is up (lwIP #3, after re-checking the IDF 5.3 mdns version).

#### NimBLE verification (Opus, 2026-11-23)

- #1 write handler always puts `uint8_t buf[maxLen]` = 512 B on the 3072 B host stack (`NimBLEServer.cpp:641`), plus
  our 308 B `BleQueueItem` in onWrite (`esp32_main.cpp:491`): unpaired peer, every board, margin unmeasured. Fix in
  src: `createCharacteristic(..., MAX_MSG_LEN_PHONE)` for TX/RX and a static item in onWrite (~520 B), then measure
  `[BLE];hoststack;free` after 300 B long writes.
- #2 deinit(clearAll) frees characteristics after a failed stop, use-after-free on the next write (classic ESP32 force
  pause): patch `if (clearAll && rc == 0)`; src: on `!stopped` reboot when idle instead of rebuilding.
- #4 partial init failure leaves the controller enabled, every retry fails; host task creation unchecked; sync wait
  without timeout: BLE dead until reboot or AU job hangs holding `s_bleLife` (classic, low-heap resume). Patch unwind
  - bounded wait; src: reboot when idle after N failed resumes.
- #3 old host task deletes the new one: real race, window closed in production (classic, resume seconds later); the
  S3 panic in the docs is the AU-19 signature. Patch `vTaskDelete(NULL)` instead of `nimble_port_freertos_deinit()`.
- #8 stop counter never reset (2 s extra per pause after one timeout): one-line patch. #5 folded into NB-01.
  #7, #9, #10 no action.
- **#6: our NB-01 is unsafe** (NULL deref if the 21 B calloc failed; upstream 2b2a56c8 has the same flaw) and its
  stated reason does not apply (onWrite returns on length 0, `esp32_main.cpp:493`). Corrected body: NULL-guard the
  store and `return m_attr_len == len && (len == 0 || memcmp(...) == 0);`. Applied 2026-11-23 (`tools/nimble_patch.py`,
  upgrades a libdeps tree that holds the first form; regression tests in `tools/tests/test_nimble_patch.py`), NB-01
  not committed in its first form.
