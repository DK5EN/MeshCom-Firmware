# Finder D — Memory Layout, Bit Packing, RAM Arithmetic

Scope: sections 1, 2 (Mengengerüst), 4.1, 4.2, 4.3 bit table, 4.7, 4.9, Abb. 3/5.

## Method

Re-measured every "heute" byte figure directly from `nm -S --size-sort` on the four
built ELFs (all dated 2026-09-24, matching the paper's stated build), and
independently recompiled the disputed struct layouts with the real target
compilers (`xtensa-esp32-elf-gcc`, `arm-none-eabi-gcc`) using `_Static_assert`
to pin `sizeof` exactly, rather than trusting host/x86_64 layout.

## Confirmed correct (no finding)

- **MHeard/Pfad/nbrMatrix/Ring "heute" totals, all 4 families** — summed the actual
  data symbols from nm and they match the paper's `MEAS` dict byte-for-byte:
  - classic (`heltec_wifi_lora_32_V2`): mh=1656, path=2841, nbr=1332, rings=160 — all exact.
  - XML (`E22_XML-DevKitC`): mh=2756, path=3551, nbr=3156 — all exact.
  - S3 (`heltec_wifi_lora_32_V3`): mh=4406, path=7101, nbr=3156 — all exact.
  - nRF52 (`wiscore_rak4631`): mh=4406, path=7101, nbr=3156, stat=1752 (3×584 `mheardLine`) — all exact.
    Commands: `xtensa-esp32-elf-nm -S --size-sort .pio/build/<env>/firmware.elf | grep -iE 'mheard|nbr|ring'`
    (E22_XML/heltec_wifi_lora_32_V3 needed the s3 nm binary or the generic xtensa one — both read
    it fine, no arch mismatch). No missing MHeard/path/nbr-adjacent static array was found beyond
    what the script counts; `mheardPathBuffer1[MAX_MHPATH][52]` (the 52 B/entry path-text buffer) is
    legitimately part of the persistent per-entry path storage, not a scratch buffer — confirmed by
    reading `src/mheard_functions.cpp:106,649-651`. The 55 B/entry MHeard formula and 71 B/entry
    Pfad formula both resolve exactly against the summed nm sizes divided by `N_mh`/`N_path`.
- **40 B (not 36 B) AoS row, §4.1** — compiled the exact field layout from
  `~/Desktop/Nachbarschaftsmatrix-Kantenpool.html` line 643 (`call[10], lat16, lon16, last_min,
rpt_min, flags, hw, hears(uint64_t), heardby(uint64_t)`) with `_Static_assert(sizeof(...)==40)`
  on both `xtensa-esp32-elf-gcc` and `arm-none-eabi-gcc` — compiles clean, confirming `hears`
  lands at offset 24 (4 B padding after the 20 B of scalar fields) and `heardby` at 32, total 40.
  The paper's claim is correct and directly checkable; a naive back-of-envelope 36 or 32 is wrong.
- **12 B `NbrRowCore` and 6 B `NbrEdge`** — compiled: `sizeof(NbrRowCore)==12` (lat16, lon16,
  last_min, rpt_min uint16 ×4 + flags, hw, ncnt, ext uint8 ×4, already 2-byte aligned, no padding)
  and `sizeof(NbrEdge)==6` (x,y,cnt,snr uint8 + last_min uint16, no padding) both confirmed on host
  and via the cross compilers.
- **96-bit / 12 B Direct-Erweiterung bit table** — `Sekunde 6 + PLT 2 + MOD 8 + RSSI 8 + LAT 21 +
LON 22 + ALT 16 + PL 4 + MESH 1 + f 4 + s 4 = 96` sums correctly (script's own assert also holds).
  LAT 21 bit / LON 22 bit at 1e-4° with an all-ones sentinel is dimensionally sound: unsigned
  offset-encoded range needs 1,800,001 values for lat (fits in 21 bits, max 2,097,151, sentinel
  unused) and 3,600,001 for lon (fits in 22 bits, max 4,194,303, sentinel unused) — no collision
  between the sentinel and any real encoded value.
- **PLT 2-bit classification** — checked against the _actual_ `getPayloadType()` in
  `src/mheard_functions.cpp:1030-1041`: today's code already only ever returns "TXT"/"POS"/"HEY"/"???"
  (4 buckets) for the frame types that reach MHeard, so the 2-bit Text/Position/HEY/Other split
  loses no information that today's field distinguishes.
- **PL 4-bit path length** — `MAX_HOP_LIMIT` is 7 (`src/configuration_global.h:363`), well inside
  4 bits (max 15); no truncation risk.
- **6-bit / 37-symbol callsign alphabet** — matches `nbrValidToken()` in
  `src/nbr_matrix.cpp:304-313` exactly: `[A-Z0-9-]`, length 3-9 (26+10+1=37 symbols). 9×6=54 bits,
  leaving 10 free bits in the 64-bit word as shown in Abb. 3. Note (not a new bug, paper already
  discloses it at line 120): `MAX_CALL_LEN` elsewhere in the firmware (own `MY_CALL` etc.) is 20
  chars (`configuration_global.h:259`), so any stored callsign over 9 chars is silently unrepresented
  in the topology today already — pre-existing limitation, not introduced by this paper.
- **"drei- bis sechsmal so viele Zeilen"** — 64/13≈4.9× (classic), 64/21≈3.05× (XML), 128/21≈6.1×
  (S3, nRF52) — spans exactly 3×-6× across all four families as claimed.
- **MH-JSON frame sizes 162/225/268 B** — independently recomputed the exact same `json.dumps`
  call with the script's literal field values; reproduces 162/225/268 exactly. `J_NEW(225) ≤ 245 <
J_POS(268)` holds, matching the BLE 245 B frame-limit claim.
- **T-Deck SD file ≈13.3 kB, §4.9** — `NEW['S3']['total']` (13,284 B, correctly _excluding_
  `ringNeed`/`ringAlone`, which is defensible: those are ephemeral relay-decision scheduling state,
  not something a boot-time topology snapshot needs to persist) + 32 B assumed header = 13,316 B ≈
  13.3 kB, reproduces the paper's figure exactly. The 32 B header size itself is unverifiable (no
  such struct exists yet in code) but is a plausible order of magnitude for "Kennung, Formatnummer,
  4 counts, Epoche, Bootminute."
- **"freed 11.5 kB / 4.5 kB", §4.2** — recomputed: S3 and nRF52 both use the same `MAX_MHEARD`/
  `MAX_MHPATH` (80/100), so MHeard+Pfad alone = 4406+7101 = 11,507 B ≈ 11.5 kB on _both_ families
  (the extra nRF52-only 1,752 B `mheardLine` statics are correctly excluded from this figure and
  accounted for separately in §4.7 — not double-counted, not omitted). Classic: 1656+2841 = 4,497 B
  ≈ 4.5 kB. Both match exactly.
- **XML family classing** — confirmed via `#if defined(ENABLE_XML) || defined(ENABLE_SBUFFER)` in
  `configuration_global.h:298-309`: `E22_XML` is a classic-ESP32-class board (own MAX_MHEARD=50,
  MAX_MHPATH=50, NBR_MAX_ROWS=21), correctly kept as its own size class distinct from both plain
  classic ESP32 (13 rows) and the S3/nRF52 branch — the Mengengerüst and Ziel columns treat it
  consistently with this.

## Findings

**1. [MEDIUM] Horizont-Meta 3 B claim (§4.1 Abb.3, §4.7 Horizont row) is not proven padding-safe
the way the row-level 36-vs-40 B case explicitly is.**
The paper devotes a whole premise (§4.1) to proving, by compiling, that a naive struct with mixed
field widths pads (36→40 B), and prescribes "getrennte Felder" specifically to avoid this. But
`hzMeta[H]` — "letzte Minute, kleinste Hop-Zahl, G-Bit", budgeted at exactly 3 B (`HZ_META=3` in
the build script) — is listed in the components table (line 96) as a single array, with no stated
technique for hitting 3 B without padding. I compiled the most natural literal reading of that
field list as a struct:

```c
struct HzMeta { uint16_t last_min; uint8_t hop_g; };
_Static_assert(sizeof(struct HzMeta) == 3, "...");
```

This fails to compile on both `xtensa-esp32-elf-gcc` and `arm-none-eabi-gcc` — the real size is 4 B
(2-byte alignment pads the struct to a multiple of 2). Unless `hzMeta` is implemented the same way
as the Direct-Erweiterung — raw bytes via `nbrBitsGet()`/`nbrBitsPut()`, or split into fully
separate homogeneous SoA arrays (`hzLastMin[H]`, `hzHopG[H]`) rather than one combined array/struct
— the same alignment trap the paper caught elsewhere recurs here, silently. That would make a
Horizont entry 20/28 B instead of 19/27 B, understating the Horizont line item in §4.7 by
`H` bytes: +40 B classic/XML, +96 B S3/nRF52 (out of totals of 5,612/13,924 B — under 1%, doesn't
flip any net-negative conclusion, but it's exactly the class of error §4.1 exists to rule out, and
here it isn't ruled out).

**2. [LOW] "128 Zeilen überall … rund 6,2 kB mehr als heute" (§4.2 option table) does not
reproduce cleanly from the paper's own `newsize()` formula under any single stated field-scaling
rule.**
Recomputing with the script's own formula: bumping only rows/masks to S3 size (R=128, W=16) while
leaving edges/ext/horizon at classic counts (256/48/40) gives a delta of **+4.5 kB** vs today, not
6.2 kB. Bumping everything to full S3 sizing (256→512 edges, 48→64 ext, 40→96 horizon) gives
**+7.8 kB**. A delta near 6.2 kB (+6.1 to +6.3 kB) only appears under an intermediate, unstated
reading — edges scaled to 512 but ext/horizon left at classic counts. The paper doesn't say which
sub-fields scale with row count for this _rejected_ option, so 6.2 kB, while plausibly reachable,
isn't independently reproducible with confidence from what's written. Severity low: it's a rejected
alternative in a bewertung column, not a number that feeds any decision or the Mengengerüst/RAM
totals.

**3. [LOW/INFO] Two small persistent globals are outside every "heute" total.**
`lastsaveMHEARDPersistence` and `lastsavePATHPersistence` (`unsigned long`, `src/loop_functions.cpp:235-236`)
are 30-second write-throttle timers for the two tables being replaced, 4 B each = 8 B total per
platform — not present in `src/mheard_functions.cpp`, so they don't show up in a same-file nm grep
and aren't in `MEAS`. Immaterial to any of the byte totals (well under 0.2% everywhere) and, being
a control timer rather than table storage, arguably out of scope for the RAM accounting — but the
task explicitly asked to check for exactly this kind of thing, so noting it for completeness rather
than as a real defect.

## Not checked / out of my angle

Did not chase §4.5 NCNT semantics, §4.8 Auto-Via algorithm/log analysis, or the mcmap fleet
percentile numbers in the Mengengerüst's "Bemerkung" column (those are other finders' angles);
only the byte-size and row-count arithmetic in that table was in scope and checked out.
