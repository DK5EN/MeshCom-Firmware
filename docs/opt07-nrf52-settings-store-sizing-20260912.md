# nRF52 keyed settings store: sizing before `W3` builds it (`OPT-07`)

Date: 2026-09-12. Branch `dry-unification`, measured in the working tree (no source changes).
Answers the open question in `docs/BACKLOG.md` `D1-04` ("Nobody has measured the nRF52 cost") and
closes `OPT-07`.

## 1. Outcome

**Both encodings fit, by a wide margin, on all three nRF52 envs.** The premise `OPT-03` leaned
on ("flash is cheap") holds for this specific feature -- but not because headroom is generically
safe on this platform (`MEM-04` says the opposite lesson on classic ESP32); it holds because the
feature itself is small relative to the headroom that happens to exist today.

| Quantity                                                             |                                               Value | Source                                  |
| -------------------------------------------------------------------- | --------------------------------------------------: | --------------------------------------- |
| Current `sizeof(s_meshcom_settings)` / current on-disk file size     |                                         **2 000 B** | linked symbol, `wiscore_rak4631`        |
| Persist-set worst case, **string-keyed** (`key=value\n`)             |                                         **2 937 B** | computed from `config_json.cpp`'s table |
| Persist-set worst case, **tagged-record** (2 B id + 1 B len + value) |                                         **1 531 B** | computed from the same table            |
| `config_json.cpp`'s own (non-shared) flash cost, already paid        |                                      **≈ 15 592 B** | `nm -S` on `config_json.cpp.o`          |
| Peak extra RAM to load, string-keyed (one line buffer)               |                                         **≤ 128 B** | worst-case field, see §4                |
| Peak extra RAM to load, tagged-record (one value buffer)             |                                          **≤ 96 B** | worst-case field, see §4                |
| Flash headroom, `wiscore_rak4631` (tightest of the three)            |                    **148 656 B free** (81.8 % used) | `pio run`                               |
| Flash headroom, `heltec_t114`                                        |                    **218 184 B free** (73.2 % used) | `pio run`                               |
| Flash headroom, `t_echo`                                             |                    **192 156 B free** (76.4 % used) | `pio run`                               |
| RAM headroom, all three envs                                         | **148 236 -- 159 940 B free** (35.7 -- 37.3 % used) | `pio run`                               |

**Verdict: take the string-keyed encoding, not the tagged-record fallback.** Both cost nothing
worth arguing about against 140+ KiB of free flash and RAM on every env that matters -- the
6x-to-10x headroom margin over the persist set's own worst-case size makes the flash-byte
argument moot. Once bytes stop being the deciding factor, the fallback's only advantage
(lower byte count) stops mattering and its disadvantage (opaque binary, no `strings`/`cat`
inspection, a second value-formatting path parallel to the one `config_json.cpp` already has
for JSON export) does not. The string-keyed loader is also closer to free: it reuses the exact
canonical-text formatting (`cfg_value_from_member` / `fmt_int` / `fmt_real`) that
`configExportJson` already exercises against every field in the same table, so a keyed
persistence layer is a _reader_ for a format the code already _writes_ correctly today.

Negative-result framing, as the ticket allows: **this is not the platform where the premise
breaks.** `MEM-04`'s "there is room" trap is real, but it bit classic ESP32 IRAM/DRAM under
PSRAM/tinyxml2 bloat that has nothing to do with settings persistence; it is not evidence about
nRF52 flash headroom for a ~2.9 KB text table, which this measurement checks directly rather
than inferring by analogy.

### 1a. Re-verified after waves 1 and 2 (same day, later)

The headroom figures below were taken before `W1`/`W2` landed, and those waves
moved both flash and RAM. Re-measured on the post-`W2` tree rather than assumed
to still hold:

| env               | flash free, at measurement | flash free, now | RAM free, now |
| ----------------- | -------------------------: | --------------: | ------------: |
| `wiscore_rak4631` |                    148 656 |     **149 304** |       163 516 |
| `heltec_t114`     |                    218 184 |     **218 896** |       151 812 |
| `t_echo`          |                    192 156 |     **192 908** |       159 692 |

Every env gained a little -- `W2`'s `String`-table conversions returned flash as
well as RAM (-648 B RAM and -688 B flash on `wiscore_rak4631` alone). **The
verdict is unchanged and now sits on slightly more margin**: the persist set's
worst case is 2 937 B string-keyed against 149 kB free on the tightest env, a
50x margin. Nothing about `W3` should be decided on flash bytes.

The one number worth restating, because it is the one that could still bite:
the 15 592 B `config_json.cpp` already costs is **already paid** -- it is in the
image today. The keyed store reuses that table and its formatting rather than
adding a second one, so it is not a second 15 kB.

## 2. Baseline: current struct and file size

**Method: read the real linked symbol, not a synthetic probe.** `s_meshcom_settings` is a real
global (`meshcom_settings`, `nrf52_flash.cpp:19`) written to flash verbatim
(`nrf52_flash.cpp:389`: `lora_file.write((uint8_t*)&meshcom_settings, sizeof(s_meshcom_settings))`),
so its linked size _is_ both the in-memory struct size and the on-disk file size -- no separate
probe compile needed, and no risk of a probe's include set or alignment flags drifting from the
real build's.

```
$ pio run -e wiscore_rak4631
...
RAM:   [====      ]  35.7% (used 88892 bytes from 248832 bytes)
Flash: [========  ]  81.8% (used 666448 bytes from 815104 bytes)
========================= [SUCCESS] Took 3.85 seconds =========================

$ NM=~/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm
$ $NM -S --size-sort .pio/build/wiscore_rak4631/firmware.elf | grep -i meshcom_settings
0007e5e4 00000010 t _GLOBAL__sub_I_meshcom_settings
200069d8 000007d0 D g_flash_content
200071a8 000007d0 D meshcom_settings
```

`0x7d0 == 2000`. Two independent globals of the same type (`meshcom_settings` and
`g_flash_content`, both `s_meshcom_settings`) agree, which is a second cross-check for free.
**`sizeof(s_meshcom_settings) == 2000` bytes**, and since the write path blits the whole struct,
the on-disk `MeshCom-RAK` file is exactly 2000 bytes today, always, regardless of how full any
string field actually is.

## 3. Persist-set worst case, both encodings

**Source of truth**: `config_json.cpp`'s `CFG_FIELD_LIST` macro (nRF52 branch: the shared list
plus the nRF52-only `CFG_FIELD_LIST_PLATFORM`). This is the table `D1-04` designates as the
schema to extend -- it is not a hypothetical field list, it is the exact set that would become
persisted keys.

**Row count, by type**, extracted straight from the macro body (script below):

| Type       |    Rows | Meaning                                                 |
| ---------- | ------: | ------------------------------------------------------- |
| `CFG_STR`  |      42 | char-array fields (26 scalar + 16 `node_mcp17t[0..15]`) |
| `CFG_INT`  |      38 | `int`                                                   |
| `CFG_FLT`  |      14 | `float`                                                 |
| `CFG_CHR`  |       4 | single `char`                                           |
| `CFG_DBL`  |       2 | `double` (`node_lat`, `node_lon`)                       |
| `CFG_U32`  |       2 | `uint32_t` (`node_gpsbaud`, `send_repeat_time`)         |
| `CFG_BOOL` |       1 | `bool` (`auto_join`)                                    |
| **Total**  | **103** | on-disk keys for the nRF52 build                        |

(This is 103 _keys_, i.e. table rows -- the unit that matters for a key-addressed store, since
each `node_mcp17t[i]` is already a separate row/key today. It is not the same count as the
BACKLOG's "89 of 147 struct fields", which counts distinct _struct members_ -- collapsing the
16-element `node_mcp17t` array and the 6-element `node_gcb` array to one member each. Both counts
are internally consistent; 103 is the correct unit for sizing an on-disk key count.)

**`CFG_STR` real bounds**, summed from the struct declaration in `src/nrf52/WisBlock-API.h`
(e.g. `node_call[10]`, `node_pwd[64]`, `node_mcp17t[16][16]` -> 16 x `char[16]`): declared buffer
size sums to **1 027 bytes** across the 42 fields, i.e. **985 bytes** of maximum string _content_
(each buffer minus its NUL). `node_lpwd` (`node_pwd[64]`) is the single largest field at 63
content bytes.

**Per-type worst-case value length**, used identically in both encodings' native/text forms:

| Type       | String-keyed worst text                                                                               | Tagged-record native bytes |
| ---------- | ----------------------------------------------------------------------------------------------------- | -------------------------: |
| `CFG_STR`  | declared size - 1 (real bound, per field)                                                             |          declared size - 1 |
| `CFG_CHR`  | 1 char                                                                                                |                          1 |
| `CFG_INT`  | 11 (`"-2147483648"`, 32-bit signed decimal + sign)                                                    |                          4 |
| `CFG_U32`  | 10 (`"4294967295"`, 32-bit unsigned decimal)                                                          |                          4 |
| `CFG_FLT`  | 15 (`fmt_real`'s own ceiling: sign + 9 sig. digits in `%.9g` scientific form, e.g. `-1.23456789e+08`) |                          4 |
| `CFG_DBL`  | 24 (sign + 17 sig. digits in `%.17g` scientific form, e.g. `-1.2345678901234567e+123`)                |                          8 |
| `CFG_BOOL` | 1 (`"0"`/`"1"`)                                                                                       |                          1 |

The `CFG_FLT`/`CFG_DBL` bounds come directly from `config_json.cpp`'s own `fmt_real()`
(`lo`/`hi` precision loop: 6..9 for float, 15..17 for double) -- these are not guesses, they are
the same textual-encoding function the firmware already runs for every export today; the bound
used here is simply "what if the loop needs its maximum precision `hi`, in scientific form."
Actual field ranges (e.g. `node_lat`/`node_lon` are range-checked to ±90/±180 and never need
scientific form) are tighter in practice; the brief asked for per-type bounds, not per-field
ones, so these are deliberately conservative.

**(a) String-keyed**: `key=value\n`, one line per field, value as its canonical text form (no
extra quoting, no per-field NUL -- the `\n` is the delimiter). Overhead per row: `len(key) + 1
("=") + 1 ("\n")`.

```
sum(len(key)) over all 103 keys           = 1 045 B
+ 103 x 2   ("=" and "\n" per row)          =   206 B
+ value bytes:
    CFG_STR   985 B  (real per-field bound)
    CFG_INT   38 x 11 = 418 B
    CFG_FLT   14 x 15 = 210 B
    CFG_DBL    2 x 24 =  48 B
    CFG_CHR    4 x  1 =   4 B
    CFG_U32    2 x 10 =  20 B
    CFG_BOOL   1 x  1 =   1 B
  value subtotal                           = 1 686 B
--------------------------------------------------
TOTAL                                       = 2 937 B
```

**(b) Tagged record**: 2-byte key id (needed once there are >255 keys worth of headroom to grow
into; 103 today fits in 1 byte, but a format that can only ever address 255 keys is a bad
trade for 1 byte) + 1-byte length (every field's native value is <= 63 bytes, so 1 byte covers
it with headroom to 255) + native value bytes.

```
overhead: 103 x (2 + 1)                     =   309 B
value bytes:
    CFG_STR   985 B (still the real per-field bound -- native storage
                      doesn't need the text encoding, but still needs
                      the actual content, up to declared size - 1)
    CFG_INT   38 x 4 = 152 B
    CFG_FLT   14 x 4 =  56 B
    CFG_DBL    2 x 8 =  16 B
    CFG_CHR    4 x 1 =   4 B
    CFG_U32    2 x 4 =   8 B
    CFG_BOOL   1 x 1 =   1 B
  value subtotal                            = 1 222 B
--------------------------------------------------
TOTAL                                        = 1 531 B
```

Notable: **the tagged-record worst case (1 531 B) is smaller than today's fixed 2 000 B blob**,
even though it is a _worst case_ (every field maximally filled) against today's _actual_
content. That is because the current struct always spends the full declared buffer per string
field regardless of content, while a length-prefixed record only ever spends what the value
uses (bounded above by the same declared buffer). The string-keyed worst case (2 937 B) is
~47 % larger than today's blob -- the human-readable-text tax.

**Extraction script** (reproduces the row/type counts and both totals above):

```python
import re
cpp = open('src/config_json.cpp').read()
hdr = open('src/nrf52/WisBlock-API.h').read()
start = cpp.index('#define CFG_FIELD_LIST(X)')
end = cpp.index('CFG_FIELD_LIST_PLATFORM(X)\n', start)
shared = cpp[start:end]
nrf52_platform = cpp[cpp.index('#else\n    #define CFG_FIELD_LIST_PLATFORM(X)'):
                      cpp.index('#endif', cpp.index('#else\n    #define CFG_FIELD_LIST_PLATFORM(X)'))]
rows = re.findall(r'X\("([^"]+)",\s*(CFG_\w+),\s*([^\s,]+),', shared)
rows += re.findall(r'X\("([^"]+)",\s*(CFG_\w+),\s*([^\s,]+),', nrf52_platform)
# rows now has 103 entries: (key, type, struct-member-expr)
# CFG_STR size: re.search(r'\bchar\s+<base>\[(\d+)\](\[(\d+)\])?', hdr) -- inner dim
#   if array-of-arrays (node_mcp17t), else outer dim.
```

(Full script with the sizing arithmetic was run from the repo root; see §6 for the exact
commands used to derive every number in this section.)

## 4. Flash cost of the serializer/parser code (already paid)

**Method**: `config_json.cpp` already compiles today (it drives JSON export/import, `CS-03`) and
already walks the identical field table row by row, applying the identical per-type
formatting/parsing. A keyed persistence loader is the same kind of code -- a schema walker over
the same table, in the same style -- so what `config_json.cpp` costs in flash _today_ is real,
already-measured evidence for what a keyed store's serializer/parser would cost, not an estimate.

```
$ pio run -e wiscore_rak4631
$ NM=~/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm
$ $NM -S --size-sort .pio/build/wiscore_rak4631/src/config_json.cpp.o
```

Relevant symbols from the object file (before link-time garbage collection, so this includes
everything the TU pulls in):

| Symbol                                                                                                                                      |  Bytes | Kind                                                  |
| ------------------------------------------------------------------------------------------------------------------------------------------- | -----: | ----------------------------------------------------- |
| `cfg_fields` (the schema table)                                                                                                             |  4 944 | `static const` data -- **unique to this TU**          |
| `configImportJson`                                                                                                                          |  7 184 | global function -- **unique to this TU**              |
| `configExportJson`                                                                                                                          |  1 508 | global function -- **unique to this TU**              |
| `cfg_num_from_json`                                                                                                                         |  1 268 | local (static) function -- **unique to this TU**      |
| `canon_kv`                                                                                                                                  |    400 | local (static) function -- **unique to this TU**      |
| `out_add_jsonstr`                                                                                                                           |    244 | local (static) function -- **unique to this TU**      |
| `cfg_err`                                                                                                                                   |     44 | local (static) function -- **unique to this TU**      |
| ArduinoJson template instantiations (`parseObject`, `parseVariant`, `VariantData::clear`, `asIntegral`, `StringBuilder::append`, allocator) | 12 112 | **weak (`W`/`V`) symbols, deduplicated at link time** |

**Own, non-shared cost: 4 944 + 7 184 + 1 508 + 1 268 + 400 + 244 + 44 = 15 592 bytes
(≈ 15.2 KiB).** The 12 112 bytes of ArduinoJson template code are _not_ attributed here: they are
weak symbols, and the linker keeps exactly one copy across the whole firmware regardless of how
many translation units instantiate them. `grep -rl "ArduinoJson.h\|JsonDocument\|deserializeJson"
src/` finds six other consumers (`extudp_functions.cpp`, `udp_functions.cpp`,
`command_functions.cpp`, `mheard_functions.cpp`, `web_functions.cpp`,
`src/nrf52/WisBlock-API.h`) that already require the same JSON machinery, so config_json.cpp's
marginal cost for that part is close to zero.

A new keyed-store reader (string-keyed) is materially _less_ code than `configImportJson`
(7 184 B) -- it needs no JSON parsing, no CRC canon form, no unknown-key/type/range validation
against a hostile upload; it is "split on `=`, look the key up in the same `cfg_fields` table,
call the same `cfg_value_from_json`-shaped setter". A conservative estimate is a few hundred
bytes to ~1-2 KB of new code, on top of the 4 944-byte table that already exists and needs no
duplication. This is bounded by evidence (the table is real and paid for; the full-JSON parser
that is far more complex is 7.2 KB), not asserted.

## 5. Peak RAM during load

**Assumption stated explicitly**: a loader for either format needs one bounded stack/static
buffer sized to the single largest field it must ever hold at once -- it does not need to hold
all 103 values simultaneously (each value is applied to `meshcom_settings` directly, matching
how `configImportJson` already applies fields one at a time today).

- **String-keyed**: one line buffer, sized to the longest possible `key=value\n` line. Computed
  from the same table: the longest is `node_lpwd` (`node_pwd`, 64-byte buffer, key `"node_lpwd"`,
  9 chars) at `9 + 1 + 63 + 1 = 74` bytes, `+1` NUL = 75. Rounding to the existing
  `CFG_VALBUF`-style convention (`config_json.cpp` already uses a 96-byte value buffer for the
  same reason), a **128-byte line buffer** covers every field with margin. No second buffer is
  needed: the key substring can be read in place (index up to the `=`) without copying it out.

- **Tagged record**: a 3-byte header (2-byte id + 1-byte length) plus a value buffer sized to the
  largest native value, again `node_pwd` at 63 bytes. **A 96-byte value buffer** (round number,
  same margin logic) covers every field.

Both are **negligible** against 235-249 KB of total RAM on these boards (0.03-0.05 % of total
RAM, ~0.08-0.09 % of the _free_ RAM measured in §1) -- RAM during load is not a concern for
either encoding on this hardware.

## 6. Headroom, all three envs

Per the brief for this measurement: read the `RAM:`/`Flash:` summary lines from `pio run`
directly. (`docs/BACKLOG.md`'s `OPT-07` row asks for `tools/resource_watch.py regions` against a
map instead; that mode exists to separate ESP32's IRAM/DRAM regions, which is exactly the
distinction `MEM-04` needed on classic ESP32's PSRAM-flag envs. nRF52 has a single flash/RAM
address space with no such split, so the region tool has nothing extra to say here that the
build's own summary doesn't -- and running `snapshot` was explicitly out of scope, since it
overwrites the tracked baseline. This is a deliberate deviation from the BACKLOG row's stated
method, per this task's own instruction, not an oversight.)

```
$ pio run -e wiscore_rak4631
RAM:   [====      ]  35.7% (used 88892 bytes from 248832 bytes)
Flash: [========  ]  81.8% (used 666448 bytes from 815104 bytes)
========================= [SUCCESS] Took 3.85 seconds =========================

$ pio run -e heltec_t114
RAM:   [====      ]  37.1% (used 87284 bytes from 235520 bytes)
Flash: [=======   ]  73.2% (used 596920 bytes from 815104 bytes)
========================= [SUCCESS] Took 3.14 seconds =========================

$ pio run -e t_echo
RAM:   [====      ]  37.3% (used 92716 bytes from 248832 bytes)
Flash: [========  ]  76.4% (used 622948 bytes from 815104 bytes)
========================= [SUCCESS] Took 3.23 seconds =========================
```

| Env               |   RAM used / total |  RAM free |  Flash used / total | Flash free |
| ----------------- | -----------------: | --------: | ------------------: | ---------: |
| `wiscore_rak4631` | 88 892 / 248 832 B | 159 940 B | 666 448 / 815 104 B |  148 656 B |
| `heltec_t114`     | 87 284 / 235 520 B | 148 236 B | 596 920 / 815 104 B |  218 184 B |
| `t_echo`          | 92 716 / 248 832 B | 156 116 B | 622 948 / 815 104 B |  192 156 B |

Against these, the entire feature's cost -- the persist set (up to 2 937 B), the walker code
(a few hundred bytes to ~2 KB beyond the 15.6 KB already spent on `config_json.cpp`), and the
load-time buffer (128 B) -- is 1-2 orders of magnitude below the free headroom on the _tightest_
of the three envs (`wiscore_rak4631`, 145 KiB flash free). None of the three envs is anywhere
near `MEM-04`'s "within 4 kB" territory; that finding was specific to classic-ESP32 PSRAM/tinyxml2
bloat and does not generalize to these nRF52 envs today.

## 7. What is NOT known

- **The migration/write-path code itself was not built.** This is a paper estimate of the
  serializer/parser and the on-disk format, per the ticket's own allowance ("a prototype or a
  paper estimate both count"). The actual writer (`InternalFS` record layout, wear/atomicity
  handling if any) was not designed or measured here -- only its input size (§3), the walker
  code's likely order of magnitude by analogy to `configImportJson` (§4), and its buffer needs
  (§5).
- **`node_mcp17t`'s real content.** All 16 slots are sized `char[16]` in the struct but the
  actual data (MCP23017 pin text labels) is far shorter in practice; §3's 985 B of `CFG_STR`
  content is the declared-buffer worst case, not a measurement of real fleet data.
  `tools/webflash.py`/mcmap were not queried for real string lengths in the field -- out of
  scope for a bytes-fit-or-not question when the worst case already fits by 50x+.
  the `%.9g`/`%.17g` bounds in §3 assume the existing `fmt_real()` precision ladder is kept;
  a different real-number formatter would need re-deriving those two rows.
- **Growth headroom beyond today's fields.** §6 headroom is measured against _today's_ firmware,
  not against whatever else lands before `W3` actually ships (this branch, `dry-unification`,
  has other waves in flight concurrently touching `esp32_main.cpp`/`nrf52_main.cpp`). Re-check
  headroom at `W3` time rather than trusting this snapshot indefinitely.

## 8. Method notes / reproducibility

- Toolchain: `~/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm` (and `-S
--size-sort`) against `.pio/build/<env>/firmware.elf` (linked, §2) and
  `.pio/build/wiscore_rak4631/src/config_json.cpp.o` (single translation unit, before link-time
  GC, §4 -- deliberately _before_ link so the ArduinoJson template symbols are still visible and
  their weak/local/global linkage (`nm`'s `t`/`T`/`W`/`V` column) can be read off directly, which
  is what separates "unique to this TU" from "shared, already paid for elsewhere").
- Builds: `pio run -e wiscore_rak4631`, `pio run -e heltec_t114`, `pio run -e t_echo` from the
  repo root, clean working tree, all three `[SUCCESS]`.
- Field-table extraction: Python, regex over `src/config_json.cpp`'s `CFG_FIELD_LIST` /
  `CFG_FIELD_LIST_PLATFORM(X)` macro bodies (nRF52's `#else` branch) plus
  `src/nrf52/WisBlock-API.h`'s `struct s_meshcom_settings` for `CFG_STR` buffer sizes (script
  skeleton in §3; full script was run interactively, not committed anywhere -- rerun it from the
  skeleton against the current tree to reproduce).
- No `tools/resource_watch.py snapshot` was run (would overwrite the tracked baseline, explicitly
  out of scope for this task).
- No source file was modified; `git status --short` at the end of this measurement showed only
  `src/esp32/esp32_main.cpp` modified, which is another agent's concurrent work in this wave, not
  a change made here.
