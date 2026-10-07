# Verdict: STORAGE / PLATFORM cluster (verifier, fork-dev 02314ce2)

> **ARCHIVED 2026-10-07.** closed verdict of the shipped RM GUI campaign, folded into `../rm-gui/impl-plan.md`. Body unchanged.

Read now: src/counters_store.h, src/nrf52/nrf52_flash.cpp, src/nrf52/settings_store_nrf52.cpp,
src/esp32/esp32_flash.cpp, src/main.cpp, src/esp32/esp32_main.cpp, src/nrf52/nrf52_main.cpp,
src/web_functions/web_functions.cpp, web_commonServer.h, Adafruit core InternalFileSystem.cpp +
flash_cache.c, docs/adr-remote-hmac.md, docs/bench/w3-baseline/README.md, docs/BACKLOG.md MEM-04,
tools/resource_baseline.json, .pio/build/wiscore_rak4631/firmware.hex (parsed, no pio run).

### storage-1 No cross-task race for web POSTs -> CONFIRMED (LOW, informational)

- Web server is polled: WiFiServer/EthernetServer wrappers (web_commonServer.h:18,35), `web_server.available()` + inline handler in loopWebserver() (web_functions.cpp:314-345).
- ESP32: loop() -> esp32loop() (main.cpp:101) -> loopWebserver() (esp32_main.cpp:4541). nRF52: setup() moves the loop into the "mcloop" task (8 kB stack, main.cpp:19-25,67) -> nrf52loop() -> loopWebserver() (nrf52_main.cpp:2594). rm_runtime.h:40-42 states loop-task-only.
- So a store write from a web POST runs in the loop task on both platforms; safe against rmDrain()/rmSendCommand().
- Fix: a "loop task only" comment on the new API is enough; the xTaskGetCurrentTaskHandle assert is gold-plating.

### storage-2 Web handler blocks the loop during store write -> DOWNGRADED (LOW)

- 3 s absolute body deadline already exists (web_functions.cpp:636); rmSndSave() already writes flash on every Send (rm_runtime.cpp:669). One more write on an explicit Save click is the same order of cost; save_settings() from other web handlers (web_functions.cpp:864) already does more.
- On RAK the handler runs under bSPI_ETH_Active=true (nrf52_main.cpp:2591-2595), so a page erase (~85 ms via sd_flash_page_erase) extends RX-deaf time a little. Tolerable for a user click.
- Fix: as finder says, Save and Send are separate POSTs; send-by-slot only READS the store. Nothing else needed.

### storage-3 nRF52 two-slot scheme does not extend to a multi-record store -> DOWNGRADED (MEDIUM)

- The slot IDEA extends; its validity and ordering rules do not: validity = trailing LF (nrf52_flash.cpp:220), order = numeric max (:239), write target = smaller value (:246). None is meaningful for a 3 x (call+key) record.
- Failure scenario overstated: littlefs v1 (lfs.h:24, LFS_VERSION 0x00010006) commits file data only on sync/close, so a torn write normally yields an empty/old file, not "half old/half new". The real corruption source is the page-cache erase (see missed-1), which produces garbage that an LF check may or may not catch.
- Fix (sound, cheapest): fixed-size binary record with magic + version + u32 seq + CRC32 (crc32_buf, crc32_util.h:47); load = valid CRC with highest seq; save into the invalid slot, else the lower-seq slot; seq = max+1. Leave rm_hwm/rm_snd files untouched.

### storage-4 nRF52 rename defect / append quirk; reuse only the no-rename pattern -> CONFIRMED (MEDIUM); fix partly unsound

- Real defect: writeFileAtomic() temp-then-rename (settings_store_nrf52.cpp:204-293) failed its rename onto an existing SMALL file on every save on DK5EN-90 (docs/adr-remote-hmac.md:144-148, "rename_failed_twice"); on the settings file it also produced false negatives (settings_store_nrf52.cpp:242-275). Do not use writeFileAtomic for the new store.
- Safe pattern = rmSlotSave() (nrf52_flash.cpp:242-266): remove (FILE_O_WRITE appends, :253), open, write, flush, close, read back and verify.
- "First save writes BOTH slots" is unnecessary: with A valid and B absent, the next save picks B (:246 logic, same in the seq variant), so A stays intact automatically; a loss on the very first save loses nothing that existed.
- Fix: full-record read-back (memcmp of all bytes, CRC ok) before returning true; false -> JSON `{"ok":false,"err":"store"}`.

### storage-5 ESP32: three NVS keys not atomic as a set; use one blob -> DOWNGRADED (LOW, recommend blob anyway)

- Existing RM values are one putUInt per key with own begin/end (esp32_flash.cpp:317-355). Per-slot keys with per-slot Save/Delete actions are not torn in any harmful way; only a "forget all" could half-complete, and a re-click fixes it.
- One putBytes blob is still the better choice: same record/codec/CRC as nRF52, one version byte, one length check. NVS partition is 20 kB (partition csv), a ~150 B blob is ~6 entries.
- Fix: namespace "RmNodes", key "nodes", putBytes(...) == sizeof(rec) as success, getBytesLength() != sizeof(rec) or bad CRC/version -> empty.

### storage-6 Survival rules (clear_flash, --cleanflash, layout bump, import, format) -> CONFIRMED (MEDIUM)

- ESP32 clear_flash() clears only "Credentials" (esp32_flash.cpp:500-514); "Counters" survives by design. nRF52 flash_reset() removes only the legacy blob and keyed store (nrf52_flash.cpp:680-683), counters kept (:684-687); InternalFS.format() fallback (:716-720) wipes every file incl. /rm_* and BLE bonds.
- --cleanflash AND a FLASH_STRUCT_VERSION mismatch take the SAME branch: esp32_main.cpp:1146-1154 -> clear_flash(); nrf52_main.cpp:553-559 -> flash_reset(). Hooking the wipe there therefore also wipes saved nodes on a layout bump; that matches node_passwd (also in Credentials/settings), so it is consistent.
- Config import does NOT reset anything: configImportJson + save_settings() + reboot (web_functions.cpp:854-881). Saved nodes are neither exported nor restored by it.
- Fix: own namespace/files (NOT "Counters", NOT the shipped /rm_hwm, /rm_snd); one rmNodesWipe() called from clear_flash() and flash_reset() (covers --cleanflash, layout bump, format fallback) plus a "Forget all saved nodes" button. Document: unencrypted at rest; derived key = credential for the target.

### storage-7 Flash wear -> DOWNGRADED (LOW)

- Saves are user clicks; NVS is wear-levelled; nRF52 already writes rm_snd on every Send (rm_runtime.cpp:669). Fix: write only on explicit Save, skip when byte-identical (one compare against the freshly loaded record). Rate limit is unnecessary.

### storage-8 nRF52 filesystem budget / ENOSPC -> REFUTED

- LittleFS block size on InternalFS is 128 B, not 4 kB: LFS_BLOCK_SIZE 128, 7 x 4 kB = 224 blocks (InternalFileSystem.cpp:34-35,109-112).
- Measured on DK5EN-90: `content_blocks;29;of;224` (docs/bench/w3-baseline/README.md:151, again :335/:362). Two ~150 B slot files cost ~4 blocks (+ dir metadata), ~2 % of the volume.
- Fix: none beyond storage-4's read-back (an ENOSPC shows up as a failed write/read-back -> "store failed"). No free-space gate, no measurement campaign.

### storage-9 RAK4631 flash headroom fine -> CONFIRMED (LOW), caveat corrected

- Parsed .pio/build/wiscore_rak4631/firmware.hex (built 2026-10-05 23:07, 3 min before fbba3f46): 596 188 B from 0x26000 to 0xB78DC = 73.1 % of 815 104 (app area ends at InternalFS 0xED000). RAK builds with -Os now (variants/wiscore_rak4631/platformio.ini:26-37); tools/resource_baseline.json (665 816, 2026-09-11) is the stale -Ofast figure. ~219 kB headroom.
- Caveat was wrong: the tight E22_XML-DevKitC line is IRAM (4 028 B, 68 B under the 4 096 B watch line), DRAM has 24 936 B headroom (BACKLOG.md:299 MEM-04). A ~150 B DRAM record and NVS calls (flash-resident code) do not touch IRAM.
- Fix: no static cache anyway (load on demand into a loop-local record, wipe after use); include E22_XML-DevKitC in the gate build as usual.

### storage-10 RAK web GUI availability depends on runtime flags -> DOWNGRADED (LOW), evidence partly wrong

- `bWEBSERVER = node_sset2 & 0x0040` (nrf52_main.cpp:641) is the only switch; line 698 `bWEBSERVER = true` is inside a commented-out `/* NRF52 no WiFi` block (:694-), not a live path.
- Web start needs bWEBSERVER AND RAK13800 hardware (neth.hasETHHardware, nrf52_main.cpp:1164,1195-1197) AND an IP (:2571); loopWebserver() also returns without IP (web_functions.cpp:318-329). No mDNS (ESP32-only, web_commonServer.h:12).
- "Must compile out where web_functions is not built" is moot: BOARD_RAK4630 is defined for ALL nRF52 envs via nrf52_base (platformio.ini:124), so T114/T-Echo compile web_functions.cpp too and simply never start it (no W5100S).
- Field reality: RAK as a MANAGING node needs a wired LAN; most RAK field nodes have none. RAK as a TARGET is unaffected (LoRa path).
- Fix: page text "reach the RAK by IP"; no new guards.

### storage-12 / tests-altitude-7 Native-test strategy (pure codec + thin adapters) -> CONFIRMED (MEDIUM)

- counters_store.h is interface-only with per-platform .cpp; host doubles exist: test/test_nrf52_settings_paths/stubs/Adafruit_LittleFS.h + InternalFileSystem.h, test/test_esp32_settings_nvs, test/test_esp32_flash_lifecycle.
- Fix (sound): header-only codec (encode/validate/choose-slot/apply-edit) tested natively with torn/garbage bytes at every offset, equal seq, both invalid, wrong version/length; one nRF52 adapter round-trip on the LittleFS stub, one ESP32 on the Preferences stub. Bench: reboot persistence on DK5EN-1 and DK5EN-90; power cut during save is not simulated, say so. tests-altitude-7's "4th slot rejected / duplicate call" cases belong to the codec too.

### security-8 Saved slots vs factory reset, clear_flash, config import -> CONFIRMED (MEDIUM), duplicate of storage-6

- Same evidence as storage-6; (iv) "failed save must report an error, not 200" is right and is covered by storage-4's read-back. Fix: as storage-6. No conflict with storage-6; both say wipe with settings, not with counters.

### protocol-6 (storage part) Key-based entry point vs 5-slot key book -> DESIGN-DECISION

- s_sent keeps one key copy per sent command, newest first, 5 entries, expiry wipe (rm_runtime.cpp:44,254-264,694-697). 3 stored keys add ~96 B RAM: negligible, confirmed.
- The proposed "store only the slot index and look the key up at reply time" breaks when the slot is edited/deleted while a reply is pending (reply then fails verification) and adds a second key-lookup path. Cheaper sound variant: keep today's copy-per-send; the new entry point is rmSendCommandKey(dst, key32, ...) and the password variant becomes derive-and-call. Eviction of the oldest pending reply after 5 quick sends is existing behaviour; the RM_RATE_MS per-target limit (rm_runtime.cpp:623) already throttles it.

## Missed by all finders (verified)

### missed-1 Adafruit flash page cache voids littlefs' torn-write guarantee (MEDIUM)

- littlefs sees 128 B blocks, but the core erases by writing 0xFF into a RAM page cache (InternalFileSystem.cpp:76-85) and every flush does sd_flash_page_erase + program of a whole 4 kB page (flash_cache.c:79-95). A power cut in that window blanks up to 32 neighbouring blocks: possibly BOTH slots and the directory metadata at once.
- Consequence: two slots reduce, but do not eliminate, total loss. Design must treat "both slots invalid" as a normal, safe state (empty list, UI says "no saved nodes, re-enter"), never as a reason to format, and the store must fail closed (no key -> no send). Same exposure as the existing settings and rm_hwm files.

### missed-2 Wipe hook covers layout bumps too (LOW, decision)

- Because --cleanflash and a FLASH_STRUCT_VERSION change share one branch (esp32_main.cpp:1146-1154, nrf52_main.cpp:553-559), a future layout bump silently forgets saved nodes along with node_passwd. Acceptable, but say it in release notes when FLASH_STRUCT_VERSION changes.

## Recommended store layout

```
// src/rm_nodes_store.h (header-only codec, native-tested)
struct RmNodeSlot { char call[10]; uint8_t key[32]; uint8_t used; };          // 43 B
struct RmNodesRec {
    uint32_t magic;   // 'RMN1'
    uint8_t  ver;     // 1
    uint8_t  nslots;  // 3
    uint16_t rsv;
    uint32_t seq;     // nRF52 slot ordering; ESP32 keeps it for symmetry
    RmNodeSlot s[3];
    uint32_t crc;     // crc32_buf over all preceding bytes
};                    // ~145 B, static_assert the size
```

- ESP32: Preferences namespace "RmNodes", key "nodes", one putBytes; load requires getBytesLength()==sizeof, magic, ver, CRC, else empty.
- nRF52: /rm_nodes.a and /rm_nodes.b, no rename. Save: target = invalid slot, else lower seq; seq = max+1; remove, write, flush, close, read back and memcmp the whole record. Load: highest seq with valid CRC, else empty.
- API (loop task only): `bool rmNodesLoad(RmNodesRec&)`, `bool rmNodesSave(const RmNodesRec&)` (skips when byte-identical), `bool rmNodesWipe()`. No static cache: load into a loop-local record per request, wipe it (hmac wipe) before return.
- Wipe: rmNodesWipe() from clear_flash() and flash_reset() and a "Forget all" button. Not in "Counters", not in config export/import.
- Send path reads only; Save/Delete are separate POSTs; failures return `{"ok":false,"err":"store"}`.

## Top 5 confirmed for the user

1. nRF52: never use writeFileAtomic/rename for the new store; use the remove-write-readback pattern of rmSlotSave() with a seq+CRC record (storage-3/4).
2. Flash wipes: store saved nodes in their own namespace/files and wipe them in clear_flash()/flash_reset() plus a "Forget all" button; they are credentials, not counters (storage-6, security-8).
3. Both slots can die together (4 kB page-cache erase): "no saved nodes" must be a safe, visible state, and a failed save must report an error (missed-1, storage-4).
4. Put the codec in a header-only module and test it natively with torn bytes; host stubs for LittleFS and Preferences already exist (storage-12, tests-altitude-7).
5. Platform budget is not a blocker: RAK at 73.1 % flash (-Os), FS at 29/224 blocks, E22_XML's tight line is IRAM, which the store does not touch; the RAK web GUI exists only with RAK13800 Ethernet, --webserver on and an IP (storage-8/9/10).
