# Bench session 2026-09-12: W3 upgrade baseline and the EXT-01 confirmation

Two jobs, one session, on the USB bench fleet. Nothing here transmitted: mesh
and gateway were off on the node under test, every injection went in on the RX
path with `--injectraw`, no frame was sourced from a foreign callsign, and no
destination was `*` (group `9999` and the telemetry path `100001` only).

## 1. The pre-cutover settings baseline

`W3`'s acceptance criterion is that a node configured on the previous firmware
keeps every setting across the upgrade. That is only checkable against a
recorded "before", so here it is -- the full `configExportJson` output of each
node while it is still running a `FLASH_STRUCT_VERSION 20260724` image:

| node       | board     | file                            | layout   | fw    | fields |
| ---------- | --------- | ------------------------------- | -------- | ----- | -----: |
| `DK5EN-90` | RAK4631   | `rak90-config-20260912.json`    | 20260724 | 4.35t |    103 |
| `DK5EN-92` | T-Beam    | `tbeam92-config-20260912.json`  | 20260724 | 4.35t |    107 |
| `DK5EN-93` | Heltec V3 | `heltec93-config-20260912.json` | 20260724 | 4.35t |    107 |

**These counts are also an independent check on the `W3` schema.** The export
walks `CFG_FIELD_LIST`, and the row counts derived from the source
(`624e8f50`) are 101 common + 2 nRF52 / 6 ESP32 platform rows -- so 103 on the
RAK and 107 on both ESP32 boards is exactly what the table predicts. Live
hardware and the static count agree.

After the cutover, re-export from the same three nodes and diff against these
files. A field that changes value, disappears, or arrives with a different
spelling is a migration defect.

## 2. `EXT-01` -- confirmed, and its impact is smaller than the row claimed

Injected three frames into `DK5EN-93` (`--extudp on`, EXT IP `192.168.68.58`),
built from corpus `f006` with fresh message ids; the frame builder's self-check
is that rebuilding `f006` unchanged reproduces its committed FCS.

| #   | frame             | expected                     | observed                            |
| --- | ----------------- | ---------------------------- | ----------------------------------- |
| A   | `DK5EN-98>9999`   | datagram on 1799             | datagram, `[EXT] Out: ... Len: 153` |
| B   | `DK5EN-98>100001` | `[EXT] Out:  Len: 0` + reset | **exactly that**, no datagram       |
| C   | `DK5EN-98>9999`   | (the open question)          | datagram, `Len: 151`                |

**The mechanism is confirmed as read.** A `0x3A` frame addressed to the
telemetry path `100001` builds no JSON, reaches `UdpExtern.write(c_json, 0)`
with an empty buffer, prints the predicted marker, and the failed write calls
`resetExternUDP()` -- visible in the log as the socket immediately
re-announcing itself.

**But it does not leave EXTUDP dead.** The row said the reset "tears the
socket down and clears `hasExternIPaddress`", which reads as an outage. It is
not one: `resetExternUDP()` clears the flag and then calls `startExternUDP()`
again, guarded by `bEXTUDP && strlen(node_extern) > 7` -- the same condition
that was required to reach the bug in the first place, so the re-establish
always fires. Frame C proves it end to end: the very next message goes out
normally.

So the real cost is **a full UDP socket teardown and rebuild for every
telemetry text frame the node hears**, not a loss of export. On a gateway in a
telemetry-carrying mesh that is continuous churn, and it opens a `stop()`/
`begin()` window on a socket the node also listens on -- worth fixing, but it
is not the fleet-visible outage the row implied. The fix is unchanged: an
early return before the send block.

Serial capture: `ext01-heltec93-serial-20260912.txt`.

## Bench state left behind

- `DK5EN-93` carries an `INSTRUMENT_ENABLED=1` build (needed for
  `--injectraw`), not a stock image. `--extudp` was set back to `off` and mesh
  stayed off, so its settings are as found.
- `DK5EN-90` and `DK5EN-92` were read only -- not reflashed, not reconfigured.
  **Leave `DK5EN-90` on its current image**: it is the article for the `W3`
  upgrade proof and has to make that jump from a real 20260724 node.

## 3. The boot-2 loss, diagnosed and closed -- 2026-09-12 evening

`W3`'s open item 1. One bench session on `DK5EN-90`, one cause, fixed and
re-proven on the same node.

**The cause is newlib-nano's printf.** `settings_store.cpp` encoded every
integer with `"%lld"` / `"%llu"`. The three nRF52 environments link
**newlib-nano**, which is built without `_WANT_IO_LONG_LONG`: it parses the
first `l`, does not recognise the second as a length modifier, and emits the
rest of the conversion **literally**. So the migration boot wrote a
well-formed settings file in which every numeric field read `ld` or `lu`:

    node_power=ld    max_hop_text=ld    bt_code=ld    send_repeat_time=lu

`encode()` reported the right byte count, the write and the rename succeeded,
and `--info` still looked correct because RAM held what the legacy blob had
just supplied. The loss appeared one reboot later, when `decode()` rejected
those values and left the fields at their struct defaults.

**That also explains the split** the verdict called its sharpest open lead.
The survivors were not surviving: `sanitize_loaded_settings()` repairs the six
radio parameters and the `SANITIZE_STR` list from defaults on every boot, so
those came back looking untouched, while the plain integers it does not name
stayed at their defaults. Six for six, and no exotic code path needed.

**Why no test could see it.** The host's libc formats `%lld` correctly, so the
same code round-trips perfectly under `pio test` while losing the whole
configuration on hardware. `-Wformat` is no help either: `%lld` with a
`long long` is exactly right. It is a property of the libc that gets linked.

**Fix:** `encode_u64` / `encode_i64` convert by hand, with no printf in the
path at all. `test/golden/nano_printf_lint.py` rejects `%ll` / `%j` / `%q`
integer conversions anywhere in the nRF52 source set (derived from
`[nrf52_base]`'s `build_src_filter`, not hard-coded), and
`test_encoded_integers_are_plain_decimal_text` pins the emitted bytes rather
than only the round trip.

**Re-proven on `DK5EN-90`**, and this time across reboots, which is what the
`W3` acceptance criterion actually needs:

| step                       | result                                                                      |
| -------------------------- | --------------------------------------------------------------------------- |
| store dump before the fix  | every integer `ld` / `lu` (`w3-store-broken-20260912.txt`)                  |
| configuration restored     | `POST /config`, 103 fields applied from the vault backup                    |
| reboots 1-3 after the fix  | `[SETST];path;keyed;fields_set=109;unknown_keys=0;malformed_lines=0`        |
| store dump after the fix   | real decimal integers, 0 fields reading `ld`/`lu` (`w3-store-fixed-...txt`) |
| `GET /config.json` vs base | **103 of 103 fields identical to `rak90-config-20260912.json`, zero diffs** |

**One open lead from the same session, not fixed:** the boot immediately after
the reflash printed `[SETST];save;rename_failed;bytes=1527` twice, and
`settingsStoreSave()` returned false both times -- a silent failure, because
234 `save_settings()` call sites ignore the return. It has not recurred in any
later boot (every save since reports `save;ok`). The untested hypothesis is
space: the filesystem then held the legacy blob (2 000 B), the old store
(1 554 B) and the new temp file (1 527 B) at once on a 28 672 B LittleFS, and
`lfs_rename` has to allocate metadata. The instrument that would settle it is
a free-space figure in the marker.

**Bench state after this session:** `DK5EN-90` now runs the post-fix image
(`wiscore_rak4631`, hand-rolled integer encoder), its keyed store is populated
and correct, and its configuration is the baseline one. The "leave `DK5EN-90`
on its current image" note in the section above is spent -- it existed to keep
a real 20260724 node available for the upgrade jump, and the jump has now been
made and measured.

## 4. `save;rename_failed` chased -- space refuted, instrument in place

2026-09-13, same node. The lead from section 3 was two `rename_failed` markers
on the boot right after a reflash, with `settingsStoreSave()` returning false
both times and nothing on the console to say why.

**The space hypothesis is refuted by measurement.** `--dumpsettings` now prints
a filesystem inventory, and on `DK5EN-90` it reads:

    [SETST];fs;dump;file;/MeshCom-RAK;2000
    [SETST];fs;dump;file;/MeshCom-Settings-Store;1546
    [SETST];fs;dump;total;files;2;dirs;3;bytes;3546;content_blocks;29;of;224

29 of 224 blocks in file content, with a temp file adding ~13 more during a
save. The filesystem was nowhere near full, so `lfs_rename` did not fail for
want of space. (The inventory also confirms the struct is **2 000 B on the
device**, as the Fable verdict said against the 2 008 B host-ABI figure.)

**It did not reproduce.** Two further DFU reflashes, each with the port caught
from the first byte of boot: every save reports `save;ok` or
`save;skipped_unchanged`. "The boot after a reflash" is not a trigger on its
own.

**What is in place for the next occurrence**, since it cannot be forced today:

- the inventory above, printed on any rename failure BEFORE the temp file is
  cleaned up -- the state as it actually was, not after tidying;
- **one retry** of the rename, reported as `save;rename_retry_ok` or
  `save;rename_failed_twice`. That single line separates the two explanations
  left standing -- a transient flash error while the SoftDevice owns the radio
  versus something persistent about the destination -- and it cannot lose
  anything: the temp file is intact and the live path still holds its old
  content either way.

Both paths are regression-tested natively and mutation-verified
(`test_rename_failure_keeps_old_file_and_reports_the_filesystem`,
`test_rename_failure_recovers_on_the_retry`): remove the retry and the second
test fails, walk the tree two levels instead of three and the first one fails
because it stops seeing `/adafruit/bond_prph/`.

## 5. Flash wear: the message-id high-water mark

2026-09-13. `node_msgid` had to be persisted (a node that restarts its counter
at 0 replays ids into every neighbour's dedup ring) and was being persisted on
every originated frame -- on the nRF52 a ~1.5 kB file write plus a rename on a
28 KB filesystem, once per frame, from eight call sites in
`loop_functions.cpp`.

`src/msgid_counter.h` replaces that with a high-water mark: the counter reaches
flash only at multiples of `kMsgIdPersistStep` (100), and on load the stored
value is advanced by a whole step **and written back once**. Ids that an
unclean shutdown may have used without recording them are treated as spent, so
none is ever handed out twice; the cost drops from one write per frame to one
per hundred frames plus one per boot.

The write-back at load is the half that is easy to leave out and fatal to leave
out -- without it flash still names the previous block while the node hands out
ids from the new one. Two tests hold it down, both mutation-verified:
`test_msgid_counter`'s property case walks every crash point across two blocks,
and `test_load_advances_the_msgid_block_and_writes_it_back` drives the real
`init_flash()` with an otherwise-valid configuration (so the pre-existing
"something was corrected" write cannot mask it).

**Measured on `DK5EN-90`:**

| boot                 | `node_msgid` in the store | writes during boot                          |
| -------------------- | ------------------------: | ------------------------------------------- |
| after the reflash    |                       124 | one `save;ok`, the rest `skipped_unchanged` |
| after a plain reboot |                       224 | one `save;ok`, the rest `skipped_unchanged` |

124 was 24 + one step: the value the previous firmware had persisted per frame,
advanced by the new scheme and written back. +100 per boot with no frames sent
is exactly the design.

Not verified on hardware: that an originated frame no longer writes. It follows
from the eight edited call sites and is covered natively, but proving it on the
bench means transmitting from a node on the live 433 MHz mesh, which is not
worth the risk of a mis-addressed test frame.

## 6. The ESP32 half of the upgrade proof -- `DK5EN-93`, 2026-09-13

`W3`'s acceptance criterion is one keyed store on **both** platforms, so the
proof `DK5EN-90` gave the nRF52 half was owed on an ESP32 node too. This is it.

**Node:** `DK5EN-93`, Heltec V3, `192.168.68.69`, CP2102 on
`/dev/cu.usbserial-0001`. Pre-flash `Flash-Version 20260724`.

**Method.** Back up to the vault first (`backup_nodes.py --node
dk5en-93=192.168.68.69`), capture `GET /config.json` as the baseline, flash
`heltec_wifi_lora_32_V3` with the schema-driven store, then compare the same
export after the migration boot and again after a plain reboot. The vault copy
is the recovery path if the cutover had lost anything; the masked copy is
`test/golden/nodes/dk5en-93/settings-base.json`.

**Result -- 104 of 107 settings byte-identical, across two boots:**

| comparison                   | identical | differing | non-GPS drift |
| ---------------------------- | --------: | --------: | ------------- |
| migration boot vs. pre-flash |       104 |         3 | none          |
| plain reboot vs. pre-flash   |       104 |         3 | none          |

The three that move are `node_lat`, `node_lon` and `node_alt`, and they are
live GPS rather than persistence: the boot log's own NMEA sentence reads
`4824.45454,N,01144.29887,E` with altitude `471.4 m`, and the altitude jitters
`504 -> 474 -> 501` across the two captures. A persistence fault does not
wander; a GPS altitude does. Credentials (`node_lpwd`, `node_pwd`,
`node_passwd`, `node_webpwd`, `bt_code`) are unchanged in both comparisons.

Boot 2 is reported separately because that is where the nRF52 half failed
before the newlib-nano fix -- boot 1 looked clean there too. On `DK5EN-93`
boot 2 printed `[INIT]...FLASH layout 20260724 ok, build 20260910`, no
`[FLASH]...%d setting(s) out of range`, and no
`save_settings() REFUSED` (the new guard against saving during a load; it not
firing is what "the load path no longer writes" looks like from outside).

**What this does NOT prove.** The 25 NVS keys with no JSON export -- the
T-Deck UI block and the flash bookkeeping fields -- are invisible to
`GET /config.json` and so are outside this comparison entirely. `DK5EN-93` is
a Heltec and carries none of the T-Deck block, so proving those needs a T-Deck
run. Same trap as `TD-19`, where a "byte-identical to its vault backup" check
missed `node_kblock` because the export does not carry it.

## 7. The three-node W3 upgrade run -- 2026-09-16

The run section 1 was recorded for: `DK5EN-90` (RAK4631), `DK5EN-93` (Heltec
V3) and `DK5EN-14` (T-Deck Plus) on the table at once, each flashed from a
`FLASH_STRUCT_VERSION 20260724` image to the `W3c` build on `dry-unification`.

**Two of the three pass outright. `W3` does not close**, because the nRF52
migration boot logged `legacy_migration_failed` rather than the
`legacy_migrated` the plan expected -- see "The one real failure" below.

### The baselines in section 1 had drifted and could not be used

Diffed against the live nodes before anything was flashed:

| node       | vs `*-config-20260912.json`                                                                                                                             |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `DK5EN-93` | 4 fields differ (`node_alt`, `node_lon`, `node_pingmax`, `node_sset`)                                                                                   |
| `DK5EN-90` | 13 fields differ -- the node had been reset to defaults since (callsign, `node_sset`, `node_name`, `node_atxt`, `node_utcof`, the five `node_gcb*` ...) |

A stale "before" produces exactly the failure mode this check exists to catch,
so fresh pre-flash exports were taken minutes before each flash and are the
article of proof here: `*-config-20260916-pre.json`, with the post-migration
exports alongside as `*-config-20260916-post.json`. The 09-12 files stay for
the history sections above; **use the 09-16 pair for any re-run.**

### The RAK baseline was too empty to fail, so it was seeded first

70 of `DK5EN-90`'s 103 exported fields read `0` or `""`. A migration bug whose
symptom is "fields revert to defaults" cannot be detected by a field that is
already at its default, so four distinctive values were set before the
baseline was captured: `node_name=Martin-W3`, `node_atxt=W3-BENCH-RAK90`,
`node_utcof=2`, `bt_code=424242`.

**Set them one command per session.** Eleven `--set*` commands sent ~1 s apart
in one session reset the node to factory defaults mid-sequence (callsign back
to `XX0XXX-00`): every one of those handlers ends in `save_settings()`, so that
is eleven back-to-back flash writes, with a LoRa `PONG` being serviced in the
middle. Restored and verified field-by-field (104 preserved, 0 changed) before
any measurement was taken. Note also that `--pingcall <call>` **transmits** --
it pinged `DK5EN-93` -- which is not obvious from the name.

### Results

| node                   | settings across the upgrade                               | boot 2                                                       | verdict                         |
| ---------------------- | --------------------------------------------------------- | ------------------------------------------------------------ | ------------------------------- |
| `DK5EN-90` RAK4631     | 102 preserved, 0 changed, 2 removed by design             | `path;keyed;fields_set=105;unknown_keys=0;malformed_lines=0` | settings pass, **marker fails** |
| `DK5EN-93` Heltec V3   | 105 preserved, 0 lost, 3 live-GPS movers and nothing else | clean, no `[SETST];counters;...;failed`, no "out of range"   | pass                            |
| `DK5EN-14` T-Deck Plus | **107 of 107**, 0 lost / 0 changed / 0 added              | 107 of 107, `--persiststat` identical                        | pass                            |

The two removed on the RAK are `send_repeat_time` and `auto_join`, gone from
the struct in the D1-04 merge (`src/config_json.h`, "nRF52: nothing." note).
They are nRF52-only export keys, which is why no ESP32 node shows them.

### The one real failure: the nRF52 migration boot

`DK5EN-90`, first boot on the new image
(`rak90-migration-boot-20260916.txt`):

    [SETST];path;legacy_rewritten          <- expected
    [SETST];path;keyed_absent
    [SETST];save;ok;bytes=15               <- /counters.txt
    [SETST];save;encoded;bytes=1458
    [SETST];save;rename_failed;bytes=1458
    [SETST];fs;rename_failed;file;/MeshCom-RAK;2000
    [SETST];fs;rename_failed;file;/counters.txt;15
    [SETST];fs;rename_failed;file;/MeshCom-Settings-Store;1458
    [SETST];fs;rename_failed;total;files;3;dirs;3;bytes;3473;content_blocks;29;of;224
    [SETST];save;rename_failed_twice;bytes=1458
    [SETST];path;legacy_migration_failed   <- expected legacy_migrated

**This is the recurrence section 4 built its instrument for, and the answer it
gives is "not transient".** The retry that exists to separate a transient flash
error from something persistent about the destination fired and failed too.
Space is refuted again by the inventory it prints: 29 of 224 content blocks,
the same figure as 2026-09-13.

**The node still ended up correct, but not because the migration worked.**
Boot 1 went on to log `save;skipped_unchanged;bytes=1458` -- the keyed store
already held the current values, written by the _old_ firmware before the
flash, so the failed rename had nothing to overwrite. Boots 2, 3 and a later
reflash all load `path;keyed` cleanly with 105 fields. **A node whose keyed
store was stale or absent when the rename failed would have kept the stale
copy**, and nothing in the log would have said so beyond this one marker.

A plain reflash does **not** reproduce it: the fourth boot went straight to
`path;keyed` with no `legacy_rewritten` and every save reporting `ok`. The
trigger is the migration boot specifically -- the one that holds the legacy
blob, the counters file and a fresh 1 458 B temp file at once.

### Live GPS made the check fail on a healthy node

`DK5EN-93`'s only movers were `node_lat`, `node_lon` and `node_alt` -- the same
three as section 6. Demonstrated to be drift rather than persistence by
exporting **twice with no flash between the exports**, 20 s apart:
`node_alt` went `482 -> 484` on its own.

`w3_upgrade_check.py` therefore returned exit 1 on a node that had lost
nothing. It now takes **`--gps-node`**, which moves those three into the
allow-list for that run, and reports intentional removals in their own
`REMOVED BY DESIGN` bucket instead of `LOST`. Both are opt-in or narrowly
scoped on purpose: on a node with a fixed configured position `node_lat` is a
setting like any other, and the gate must keep checking it.

### The message-id counter, and the instrument that was missing

`node_msgid` is neither a schema row nor an export field, so the only way to
observe it was to originate a frame and read the id off the air -- transmitting
purely to run a bench check. **`--msgid` now prints it**
(`[SETST];counters;msgid;<n>`), on both platforms, outside
`INSTRUMENT_ENABLED` because the upgrade it verifies happens on shipped images.

Continuity across the `Credentials` -> `Counters` cutover holds:

| node       | reads                                        | note                                                                                                                                       |
| ---------- | -------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| `DK5EN-93` | 182 -> 282 -> 382                            | exactly `kMsgIdPersistStep` per boot, idle                                                                                                 |
| `DK5EN-90` | 412 -> 511, then 511 -> 512 without a reboot | live mesh: the second read shows the counter also advancing from originated traffic, which is what makes the boot delta 99 rather than 100 |

Neither node restarted near 0, which is what a lost counter looks like.

### What this run does NOT cover

- **12 of the 17 NVS-only keys.** `--persiststat` covers `node_perflash`,
  `node_persd`, `node_immsave` and `node_mute` (`flash;0;sd;0;immediate;0;mute;1`,
  identical before and after), and `--info` covers `node_kllock` (`KEYLOCK on`).
  The rest -- `node_audmsg`, `node_audstart`, `node_bllock`, `node_cflash`,
  `node_kblock`, `node_kblsync`, `node_map`, `node_modus`, `node_wifion` and the
  version fields -- have no read-back command, so "survived" is inferred from
  the boot behaving normally, not measured. Note the count: the schema-versus-
  export diff yields **17**, not the 25 quoted in section 6 and in `W3`.
- **`--dfu` is the only working RAK flash path here.** `pio run -e
wiscore_rak4631 --target upload` printed `[SUCCESS]` over an nrfutil run that
  had failed ("Timed out waiting for acknowledgement", "Touch disabled") and
  left the old image in place; it was caught only by checking the running build
  afterwards. Use `--dfu` (sets `GPREGRET=0x57`), wait ~4 s for
  `/Volumes/RAK4631`, copy the `.uf2`, and verify the build string after.
