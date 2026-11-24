# Verdict: T-Beam AU "fail;nomem" after the download (advisor, 2026-11-24)

Read-only review of `fork-dev` @ `e2ad38c8`, logs `ota-lowheap-arena-20261123/fix2-series/serial-DK5EN-92.log`
(6 check + install pairs, 08:44-08:49), `heaptrace-tbeam2/map-idle-chains.json`, the T-Beam ELF
`fix2-instr-ttgo_tbeam.elf` (heap region tables decoded from it) and the framework libs (IDF 4.4.7).

## Bottom line

- The dry run needs **32,768 B (dictionary) + 11,412 B (DryMem) = 44,180 B** at the same time.
- After WiFi start the T-Beam never offers that: the largest block ever seen is **42,996 B**, and once the
  32 KB buffer is taken the next-largest block is **5,620-13,300 B (usually 7,156 B)**.
- So no allocation order, no hold and no BLE handling on the running heap makes the current dry run
  deterministic. The fix must either shrink the dry run so it fits inside the AU buffer, or run it where
  memory is plentiful. Recommended: shrink it (8 KB deflate window, everything inside the AU buffer).

## Facts the verdicts rest on

### Heap regions of the classic ESP32 (decoded from the ELF, IDF 4.4.7)

`soc_memory_regions` / `reserved_region_*` of `fix2-instr-ttgo_tbeam.elf`; each line is a separate TLSF heap,
an allocation can never span two of them. TLSF control overhead is about 3.3 KB per heap
(113,840 - 110,580 = 3,260).

| Heap (address range)  | Bytes   | Type   | Largest block when empty | Notes                                  |
| --------------------- | ------- | ------ | ------------------------ | -------------------------------------- |
| 0x3FFB2730-0x3FFB6388 | 15,448  | DRAM   | ~12.1 KB                 | BT classic EM, released by NimBLE init |
| 0x3FFB6388-0x3FFB8000 | 7,288   | DRAM   | ~4 KB                    | between BT EM and BT bss               |
| 0x3FFB9A20-0x3FFBDB28 | 16,648  | DRAM   | ~13.3 KB (13,300 seen)   | between BT bss and BT misc             |
| 0x3FFD9778-0x3FFE0000 | 26,760  | DRAM   | ~23.4 KB                 | after the app .bss                     |
| 0x3FFE0440-0x3FFE3F20 | 15,072  | D/IRAM | ~11.8 KB                 | between ROM PRO and APP data           |
| 0x3FFE4350-0x40000000 | 113,840 | D/IRAM | **110,580 B**            | the only place a 32 KB block can exist |

- `[HEAP] (init)` 142,808 free / 110,580 largest and `(ble)` 110,580 largest: the big region is still whole
  before WiFi. `(wifi)` 63,476, `(udp)` 57,332; at runtime 25-43 KB.
- `heap_caps_malloc` tries the DRAM-type heaps first (type 0 has `MALLOC_CAP_8BIT|DEFAULT` at priority 0;
  D/IRAM only at priority 1). Small long-lived objects therefore fill the small DRAM heaps; the big region
  takes WiFi's static buffers, task stacks and every request the small heaps cannot serve.
- Allocator: TLSF (`libheap.a`: `multi_heap_malloc_impl` -> `tlsf_malloc`), heap poisoning LIGHT.

### What the series shows (DK5EN-92, 2026-11-24)

| Pair | Check start iblk (BLE paused) | Install start iblk | buf32 at install | After download                        |
| ---- | ----------------------------- | ------------------ | ---------------- | ------------------------------------- |
| 1    | 42,996                        | 26,612             | none             | dict 32768 from heap fails            |
| 2    | 36,852                        | 25,588             | none             | dict 32768 from heap fails            |
| 3    | 34,804                        | 36,852             | ok, rec hit 4    | `dry;dict;buf32`, DryMem 11,412 fails |
| 4    | 36,852                        | 38,900             | ok, rec hit 4    | `dry;dict;buf32`, DryMem 11,412 fails |
| 5    | 26,612 (buf32 none)           | 34,804             | ok, rec hit 4    | `dry;dict;buf32`, DryMem 11,412 fails |
| 6    | 34,804                        | 34,804             | ok               | (series end)                          |

- BLE pause raises free from ~43 KB to ~76 KB but never the largest block: the freed BLE memory sits in the
  small DRAM heaps (after buf32: `pregate;iblk` 13,300 once, then 5,620-7,156).
- The job task stack lands in the big block: resumed 36,852 -> next install start 26,612 = **exactly
  10,240 B = `FWNET_TASK_STACK` (classic)**. BLE resume also eats into it (check end 42,996 -> resumed 36,852).

## Verdicts on the four ideas

### 1. "Reserve the 32 KB at boot, while the heap is fresh" - PARTLY

- TRUE that the block is there at boot: 110,580 B contiguous in 0x3FFE4350-0x40000000 until WiFi init;
  even a single 44,180 B block (dict + DryMem) is obtainable then.
- FALSE as a fix, because the reservation is permanent and the T-Beam cannot pay for it:
  - Idle free today is ~43 KB (BLE and web on, `[AU];heap;start` lines), 48.7 KB in the trace image.
  - 32 KB reserved leaves ~11-17 KB idle; 44 KB leaves almost nothing. WiFi RX bursts (1.6 KB buffers), the web
    server (TIME_WAIT pcbs alone ~3.6 KB per browser session), a BLE phone connect (NimBLE + 5 KB phone rings),
    mDNS and the gateway UDP all allocate from that remainder. The field "lowheap" turns into random malloc
    failures outside AU.
  - `buf32Held()` counts the block only while a job task owns it (`s_buf32Task != nullptr`); between jobs a
    boot-reserved block would be invisible to the gate unless that is changed too.
  - "Classic only" does not change the arithmetic: the classic ESP32 is the board that cannot afford it.
  - Lending the block to nobody else is exactly what makes it a permanent loss; lending it to others makes it
    unrecoverable (no compaction, see 2).
- Two boot blocks (32 KB + 11.4 KB) cost the same 44 KB.
- Lazy reservation at the first check and never giving it back = the same idle cost from the first check on
  (see 4).

### 2. "Defrag the heap - reorder the allocations" - FALSE as compaction, PARTLY as placement

- Compaction is impossible: TLSF on raw C/C++ pointers, no handles, no relocation; and the six heaps are
  physically separate, so even a perfectly compacted heap could not merge the 33 KB that BLE frees in the
  small DRAM heaps with the big region.
- What placement can do (concrete to this code):
  - Allocate long-lived big things early and in a fixed order (done at boot already, not under our control for
    WiFi/NimBLE internals).
  - Keep transient big users out of the big block: the fwnet task stack (10,240 B) is created per job and
    lands in the big block (measured, above). A static stack (`xTaskCreateStaticPinnedToCore`, .bss) would
    stop that, but costs 10.5 KB permanently.
  - Never free the big block between its uses (= the hold, see 4).
  - Serve short-lived buffers from a pool that was taken while the block existed (= the AU buffer, already done).
  - Make the working set smaller so it fits what is reliably there (recommended fix below).
- None of these can create the missing 44 KB block after WiFi start; at best they keep the 32-43 KB block
  intact.

### 3. "The BLE restart between check and download should not happen - why does it?" - TRUE (it is avoidable), but not the cause of the remaining failure

- Why it happens: `--update check` and `--update install` (and the web buttons `updcheck` / `updinstall`,
  `web_nodefunctioncalls.cpp:50-100`) start two separate jobs via `fwNetStart`. Each unguarded TLS job pauses
  BLE itself (`esp32BlePause`, `fw_update_net.cpp:1827-1837`) and resumes it at its end
  (`blePaused && !guarded` -> `esp32BleResume`, `:1899-1909`); `buf32Release()` runs before that (`:1878`).
  The install job then pauses BLE again.
- What already exists: `auBleGuardRequest(true)` (`esp32_main.cpp:2963`) runs a guarded CHECK and, when the
  result is newer + installable, chains the DOWNLOAD (`auGuardTick` -> `FWG_CHAIN_DOWNLOAD`); BLE stays down
  until `auBleGuardRelease`. Today only the web "pause Bluetooth and check" path after `lowheap_phone`
  (`web_nodefunctioncalls.cpp:66-67`) and the D16 periodic guarded check (chain = false) use it.
- Should BLE stay paused from check through download when an install follows: yes (one session, one pause).
  In notify mode, where a human installs hours later, BLE must come back after the check; then the install
  runs as its own guarded CHECK + chained DOWNLOAD (one extra API call, ~5 s).
- But keeping BLE paused does NOT fix the failure: pairs 3-5 had BLE paused and buf32 held inside the same
  install job and still failed at DryMem 11,412.

### 4. "Hold the 32 KB and never hand it back" - PARTLY

- Where it is handed back: `buf32Release()` at the end of every CHECK/DOWNLOAD job (`fw_update_net.cpp:1878`),
  before the BLE resume; only the restart path keeps it (`buf32Detach` / adopt, `:1872`, `:469-484`).
- Holding from the check into the install job removes the `buf32;none` lottery (pairs 1-2). It also means
  BLE, on resume, cannot take a slice of the big block (42,996 -> 36,852 in pair 1).
- Holding "forever" costs 32 KB at idle from the first check on: with BLE resumed after the check, free
  would be ~43 KB - 32 KB = ~11 KB, the same failure mode as idea 1 (and in notify mode the hold lasts until a
  human installs).
- Holding alone does not fix the T-Beam: in pairs 3-5 the buffer was held (`rec;hit;4`, `dry;dict;buf32`) and
  the 11,412 B DryMem had no block: with buf32 taken the largest remaining block is 5.6-13.3 KB.
- Correct scope of the hold: one AU session (guarded CHECK -> chained DOWNLOAD -> end of dry run), released at
  the session end. No idle cost.

## Recommended fix: the dry run fits inside the AU buffer (8 KB deflate window)

### Idea

- The dictionary only has to be as large as the deflate window. Build the `.autoupdate` assets with an
  8 KB window (zlib wbits 13). Then dict 8,192 + DryMem 11,412 (rounded 11,424) = **19,616 B**, which fits in
  the AU buffer next to nothing else, because the TLS records (21,152 B) are gone by then.
- The dry run then allocates nothing from the heap. Whoever got the AU buffer for the download is
  guaranteed to get the dry run too.
- The buffer only has to be 21,152 B (the records) instead of 32,768 B. The smallest install-start block
  measured is 25,588 B, so the buffer is also obtainable at every measured install start.

### Asset size cost (measured, python zlib level 9 on the bench images)

| Image                          | wbits 15 (today) | wbits 14 (16 KB) | wbits 13 (8 KB)     |
| ------------------------------ | ---------------- | ---------------- | ------------------- |
| ttgo_tbeam.bin (11.21)         | 1,256,003        | 1,267,584        | 1,273,605 (+1.40 %) |
| heltec_wifi_lora_32_V3 (11.21) | 1,194,924        | 1,205,771        | 1,211,701 (+1.40 %) |

T-Beam stage layout still fits: ota_0 3,403,776 - 1,273,605 -> stage offset ~2,129,920 >= running image
~1.95 MB (margin ~185 KB, was ~202 KB).

### Compatibility: keep the first byte 0x78

The byte 0x78 is a contract in three places that cannot all be updated: Safeboot's upload sniff
(`src/safeboot/ElegantOTA.cpp:437`, field Safeboots are not OTA-updatable), the Safeboot page
(`src/safeboot/ota.html:746`) and `tools/webflash.py:388`. An honest wbits-13 header starts with 0x58 and would
be taken as a raw image there. So:

- The asset keeps a valid 32 KB header byte, `0x78`, and signals "window is 8 KB" in the informational
  FLEVEL bits: header **`0x78 0x5E`** (FLEVEL 1; legacy assets are `0x78 0xDA`, FLEVEL 3). RFC 1950: FLEVEL is
  "not needed for decompression". The deflate body and the Adler-32 trailer are those of the wbits-13 stream.
- Every existing decoder inflates it unchanged: Safeboot's ROM tinfl with its 32 KB dict (declared window
  32 KB <= dict), Python zlib, the browser DecompressionStream; all sniffers see 0x78.
- The app's dry run, on `0x78 0x5E`, hands tinfl a header patched in RAM to `0x58 0xC3` (what zlib emits for
  wbits 13; FCHECK valid), so tinfl accepts the 8 KB dictionary. Flash is not touched (SHA-256 and CRC were
  already checked on the flash bytes).
- Safety: should an asset claim 8 KB but contain a longer distance, tinfl (wrapping buffer) produces wrong bytes,
  the Adler-32 trailer fails, the dry run refuses (`inflate_chk`). It can refuse a mislabelled good asset, never
  pass a bad one. `make_autoupdate.py` additionally proves the claim at build time.

### Exact changes

1. `tools/make_autoupdate.py`
   - `compress()`: `zlib.compressobj(9, zlib.DEFLATED, 13)`, then replace the 2 header bytes with `b"\x78\x5e"`.
   - `verify()`: accept header `78 5e`; inflate twice: as is with wbits 15 (what Safeboot does) and with the
     header patched to `58 c3` under `zlib.decompressobj(13)` (fails with "invalid distance too far back" if any
     distance exceeds 8 KB); both byte-exact with the input.
   - Update the module docstring (header, window) and `test/test_make_autoupdate.py` (new header, the 8 KB
     proof, a negative case: a wbits-15 body under a `78 5e` header must fail `verify`).
   - `tools/webflash.py`: no change (its own compression stays wbits 15; sniff on 0x78 still holds).
2. `src/fw_update.h` (pure, host-tested)
   - `inline uint32_t fwZWindowFor(uint8_t cmf, uint8_t flg)`: `0x78 0x5E` -> 8192; any other valid zlib header ->
     `1u << (8 + (cmf >> 4))`; invalid -> 0.
   - `inline bool fwDryFits(size_t bufLen, uint32_t win, size_t stateLen)`:
     `win + fwAuBufRound(stateLen) <= bufLen`.
3. `src/esp32/fw_update_net.cpp`
   - `stageDryRun()` (~1368-1447): read the first 2 bytes of the stage (`stageFlash(STAGE_READ, sg.off, ...)`),
     `win = fwZWindowFor()`. If `win == 8192` and the AU buffer exists and `fwDryFits(s_buf32.len, win,
sizeof(DryMem))`: `fwAuBufTakeWhole(&s_buf32, win + round(sizeof(DryMem)))`, `dict = buf`,
     `m = (DryMem *)(buf + win)`, log `[AU];dry;buf32;win;8192;state;buf32`; no heap allocation; return the block
     with `fwAuBufReturnWhole` and do not `heap_caps_free` either pointer. Otherwise the current code unchanged
     (legacy 32 KB assets, PSRAM boards), with `win` passed to `fwInflateStream` instead of the constant.
   - `DryRd`: a `patchHdr` flag; when `pos == 0` and set, overwrite `dst[0..1]` with `0x58 0xC3` after the read.
   - `buf32Acquire()` (~469-502), classic only (`#if AU_BLE_PAUSE`): if 32,768 is not available, try
     `FWNET_BUF32_RECS` (21,152) and log `[AU];buf32;ok;21152;small`. The 32 KB tier stays first so legacy assets
     keep today's behaviour. S3: unchanged.
   - `static_assert(8192 + FWNET_BUF32_RND(sizeof DryMem) <= FWNET_BUF32_RECS)` next to the existing asserts
     (DryMem is local to `stageDryRun`; hoist the struct to file scope for the assert).
4. Session hold (operator idea 3 + 4, scoped), classic only, separate small step after 1-3:
   - `--update install` and web `updinstall` on `AU_BLE_PAUSE` boards: start `auBleGuardRequest(true)` (guarded
     CHECK + chained DOWNLOAD) instead of a bare `FWJ_DOWNLOAD`.
   - At the guarded CHECK's end, when a chain is pending, keep the idle AU buffer (`buf32Detach()` instead of
     `buf32Release()`); the chained DOWNLOAD adopts it in `buf32Acquire()` (path exists). Release at the DOWNLOAD
     end and on every guard release (`auBleGuardRelease`: release if still detached) so nothing is held at idle.
   - Not needed for the nomem fix itself (the 21 KB tier is obtainable at every measured start); it makes the
     buffer independent of what happens between the jobs.
5. Docs: `docs/campaign-ota-lowheap-01-20261122.md` (wave entry), `docs/safeboot-ota-contract.md` (asset header
   `78 5e` = 8 KB window), release notes.

### How DryMem is covered

Inside the AU buffer at offset 8,192 (16-byte aligned), dict at offset 0. No heap allocation in the dry run on any
board for new assets. Legacy assets (`78 da`): unchanged path (dict = buffer, DryMem on the heap) - the T-Beam
cannot install those, exactly as today.

### Memory cost

- Idle on the T-Beam: **0 B** (nothing reserved; the buffer exists only during a job / guarded session).
- Static RAM: a few dozen bytes of code constants, no .bss.
- Job peak: unchanged with the 32 KB tier; 11.6 KB lower when the 21 KB tier is taken.
- Gate: unchanged (buffer bytes come out of `fr` and are added back by `buf32Held()`; the sum stays the same).

### Risks

| Risk                                                               | Mitigation                                                                                             |
| ------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------ |
| Takes effect only for releases built after the tool change         | State it in the release notes; the T-Beam installs the first new-format release and every later one    |
| FLEVEL signalling is a private convention                          | Documented in the Safeboot contract; misuse can only cause a refusal, never a wrong install (Adler-32) |
| Header patch in RAM goes wrong                                     | Host test of `fwZWindowFor`; bench line `dry;buf32;win;8192`; legacy path untouched                    |
| Assets 17-18 KB larger (+1.4 %)                                    | Layout margin checked (~185 KB on the T-Beam); download time +1.4 %                                    |
| S3 behaviour changes for new assets (DryMem no longer on the heap) | Strictly less heap use; if zero S3 delta is required, gate the in-buffer path with `AU_BLE_PAUSE`      |
| `STAGELAN` (INSTRUMENT) is not a TLS job, gets no buffer           | Acceptance must use a real DOWNLOAD (dev channel release with new assets)                              |

### Log lines that prove it on the bench

- `[AU];buf32;ok;32768` or `[AU];buf32;ok;21152;small` at the install start (never `buf32;none`).
- `[AU];dl;bytes;...`, then `[AU];dry;buf32;win;8192;state;buf32` (new), no `dry;dict;heap`.
- `[AU];alloc;end;fails;0` for the install job.
- `[AU];stage;ok;tag;<tag>;zlen;<~1.27 MB>`.
- With step 4: `[AU];bleguard;on`, one `ble;pause`, `buf32;ok;...;adopt` in the DOWNLOAD, one `ble;resume;ok`
  after `bleguard;off;done`.

### Acceptance test

1. Host: `tools/regression.sh --stage 1,2` green, incl. new `fwZWindowFor` / `fwDryFits` cases and
   `test_make_autoupdate.py` (8 KB proof and the negative case).
2. Release: one dev-channel release cut with the new tool; `verify_release_assets.py` green; header of every
   `.autoupdate` = `78 5e`.
3. DK5EN-92 (T-Beam, plain INSTRUMENT image, web on, BLE on, `MC_AU_DEV_CHANNEL=1`): 10 x (`--update check`,
   `--update install`, `--update unstage`, `--update forget`) without reboot: 10/10 `[AU];stage;ok`, 0
   `fail;nomem`, 0 failed allocs; idle free and largest block after the series within ~1 KB of before; web GUI
   loads; a phone connects over BLE after the series; `ble;resume;ok` after every job.
4. DK5EN-1 (Heltec V3): the same 10 rounds, 10/10 `stage;ok` (no regression), plus 1 round against a legacy
   `78 da` asset (prod channel) still stages (32 KB path).
5. One real handover on DK5EN-92 of the new-format asset: Safeboot (sbver 3) applies it, the node boots the new tag.
6. One manual Safeboot web upload of the new `.autoupdate` on a field Safeboot: `[SAFEBOOT];ota;format;zlib`, OK.

## Alternatives considered

| Option                                                                                                                         | Deterministic | Idle cost | Why not recommended                                                                                                                                                                   |
| ------------------------------------------------------------------------------------------------------------------------------ | ------------- | --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Boot reservation 32 KB or 44 KB (idea 1)                                                                                       | yes           | 32-44 KB  | Leaves 0-17 KB idle on the T-Beam                                                                                                                                                     |
| Hold buf32 forever (idea 4)                                                                                                    | no            | 32 KB     | DryMem still fails (pairs 3-5); idle ~11 KB after the check                                                                                                                           |
| Guard session + hold only (ideas 3+4 scoped)                                                                                   | no            | 0         | Fixes `buf32;none`, not DryMem                                                                                                                                                        |
| Static 11.4 KB DryMem + session hold                                                                                           | yes           | 11.4 KB   | ~25 % of the T-Beam idle free, the campaign fought for 1-2 KB (D8-D10)                                                                                                                |
| Dry run on a verify boot before the handover (app reboots, runs the dry run before BLE/WiFi with 110 KB free, then hands over) | yes           | 0         | Firmware-only and works for today's assets, but a new reboot step and NVS state next to the "hand"/"nocap" logic in `bootCheckOnce`; the fallback if the asset format must not change |
| Smaller inflater state (uzlib, or miniz tinfl with fewer fast-lookup bits)                                                     | partly        | 0         | New decoder next to Safeboot's ROM tinfl; still needs 32 KB dict for today's assets                                                                                                   |
| WiFi deinit for the dry run                                                                                                    | no            | 0         | Gateway/web outage, WiFi re-init in a running app, block size not proven                                                                                                              |
| "BLE arena" (NimBLE + controller allocations from one boot block, lent to AU during the pause)                                 | yes           | ~0        | Invasive (NimBLE and IDF allocation hooks); long-term structural option                                                                                                               |
| Honest wbits-13 header (0x58)                                                                                                  | yes           | 0         | Breaks the 0x78 sniff of field Safeboots, the Safeboot page and webflash.py                                                                                                           |
