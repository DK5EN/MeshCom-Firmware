# W3 settings store — Fable verdict, 2026-09-12

Six independent finders, then verification of every load-bearing claim against the
code by the orchestrator. Scope: the forward fix for the settings loss observed on
`DK5EN-90`. The operator ruled out reverting; nothing here proposes one.

## The headline: the diagnosis that justified the forward plan was wrong

The working theory was "the schema covers only 1 284 of 2 008 struct bytes, so
uncovered fields revert to defaults". **Refuted.** All six fields that actually
reverted on hardware (`node_btcode`, EXTUDP, webserver, netmode, `contrast`,
`max_hop_text`) _do_ have schema rows, and survivors and casualties interleave in
encode order, so neither partial coverage nor truncation can produce the observed
set. The 724 uncovered bytes are markers, the device EUI (rebuilt each boot),
fields the schema deliberately excludes, and the struct's own `// nicht im Flash`
section. **There are essentially no missing fields to add.**

So: the loss on boot 2 is **not diagnosed**. Fixing forward from the old theory
would have changed code that was not the cause and left the real defect in place.

## Finding 1: the store is at 98.5 % of its buffer, and `OPT-07` says 50x

- **File:** `src/nrf52/settings_store_nrf52.cpp` (`kSettingsBufferCap`), claim in
  `docs/BACKLOG.md` `OPT-07`
- **Severity:** critical
- **Measured:** worst case **4 036 B** against a **4 096 B** cap -- **60 bytes**.
  The committed 2 937 B / "50x margin" figure maximised strings but left numerics
  at defaults; a `double` at `%.17g` is ~24 characters.
- **Failure scenario:** a node whose strings and numerics are both long makes
  `encode()` return -1. `settingsStoreSave()` returns false, **234 `save_settings()`
  call sites check the return value zero times**, and `DEBUG_MSG` is a no-op under
  `DO_DEBUG 0`. The setting appears to take (RAM is already updated) and silently
  reverts on the next reboot. This is the exact shape of the bench symptom.

## Finding 2: `flash_reset()` overwrites the downgrade safety net with defaults

- **File:** `src/nrf52/nrf52_flash.cpp` `flash_reset()`, reached from
  `src/nrf52/nrf52_main.cpp:527`
- **Severity:** critical
- It calls `InternalFS.format()` -- destroying the keyed store _and_ the legacy
  blob -- then writes a **defaults** blob that `save_settings()` never updates
  again. The result is a marker-correct, size-correct file full of factory
  defaults. `nrf52_main.cpp:527` runs _after_ `init_flash()` has returned via the
  keyed path, so it is reachable on a boot that just loaded fine.
- This explains the observed downgrade outcome: the blob was not deleted, it was
  overwritten. **The comment in shipped `save_settings()` claiming a downgrade
  recovers pre-migration settings is false and must be withdrawn.**
- Also unlisted anywhere: `InternalFileSystem::begin()` formats on any mount
  failure and returns `true`.

## Finding 3: `node_msgid` / `node_ackid` stopped persisting on nRF52

- **File:** `src/settings_schema.h` (deliberate exclusion), effect at
  `src/loop_functions.cpp:3331`
- **Severity:** high
- `msg_id = (_GW_ID << 10) | (node_msgid & 0x3FF)`. The raw blit persisted these
  counters; the schema excludes them, so they restart at 0 every reboot and the
  node replays msg_ids into every neighbour's dedup ring.
- **The error in my reasoning:** "deliberately not _exported_" (correct, and
  `config_json.h` gives the reason) was collapsed into "deliberately not
  _persisted_". On nRF52 the blob persisted everything; those are different
  questions and I answered the wrong one.

## Finding 4: `max_hop_text` is a second range-eats-sentinel row

- **File:** `src/config_json.h:252`, `src/maxhop.h:17-19`, clamp at
  `src/settings_store.cpp:232`
- **Severity:** medium (latent)
- Range `1..6`, struct default `0` meaning "nothing stored yet", documented
  fallback `4`. `decode()` clamps `0 -> 1` before `maxHopTextSanitize()` sees it,
  which then accepts 1. The node pins itself to one text hop instead of four.
- My earlier claim that `node_power` was the only affected row is **refuted**. I
  swept the radio sentinels rather than the correct general rule. Sweeping every
  row whose struct default lies outside its own declared range finds exactly two:
  `node_power` (fixed) and `max_hop_text` (open).

## Finding 5: the byte-coverage gate I proposed is the wrong instrument

- **Severity:** high (design)
- It invents a padding-classification problem, is per-build-env rather than
  per-platform, and cannot see a field covered by a _wrong-typed_ descriptor. Its
  clean state would be 36 % red, which is a gate nobody reads.
- **My coverage numbers were host-ABI.** `unsigned long` is 8 B on the host and
  4 B on the nRF52 (verified with both compilers): the struct is **2 000 B on
  device, not 2 008**, and 15 of the 18 reported region boundaries do not exist on
  hardware.
- **Correct invariant:** member-level, fail-closed round trip, per build env.
  (a) every compiled struct member is in the schema or in a cited exclusion table,
  and a member in neither **fails the build**; (b) a native round trip writes a
  distinct in-range value into every schema member, encodes and decodes through
  the real `settings_schema::fields()`, and compares **member-wise**; (c) a golden
  `key -> member` list so renames need human approval. (b) subsumes coverage and
  additionally catches wrong `CfgType`, wrong STRING size, duplicate keys and the
  Finding 4 class. Member-wise comparison removes padding as a concept entirely.

## Finding 6: the gates were not fail-closed, and the tests could not fail

- `test/golden/settings_schema_lint.py` iterates a hand-transcribed 147-entry
  table and **never parses the struct**: a new member in neither table is
  invisible and the lint exits 0.
- `test_legacy_migrates_into_keyed_store` asserts the store "decodes back to the
  same values" while comparing **two fields**, both inside the covered region.
- The fake filesystem advertises `force_short_read` / `force_short_write`; **no
  test sets either.** `rename()` and `write()` always succeed; there is no
  ENOSPC case.
- `twin_stub_lint` is one-directional by design (`subset-is-fine`), so a member
  missing from the stub's struct copy passes -- and a field outside the schema has
  no `offsetof` reference to fail the compile either.
- Boot 1's "104 fields preserved" reads `meshcom_settings` **in RAM**. It proves
  the legacy read worked and says nothing about what reached flash. The W3
  acceptance test as run cannot catch a bad migration write.

## Refuted claims (do not re-investigate)

- _Partial schema coverage caused the boot-2 loss_ -- all six reverted fields have
  schema rows; survivors and casualties interleave in encode order.
- _The schema must grow to cover the struct_ -- the uncovered bytes are markers,
  EUI, deliberate exclusions and `// nicht im Flash`. Naive completion balloons the
  worst case to ~6 kB, ~2 kB over cap.
- _LittleFS ran out of space_ -- ~8-9 kB used of 28 672 B (7 x 4096, block 128).
- _`api_functions.cpp:232` is a third `init_flash()` caller that matters_ --
  `api_read_credentials()` has zero callers in `src/`; dead code.
- _`sanitize_loaded_settings()` corrupts on the keyed path_ -- the struct is fully
  populated when it runs; it is a write amplifier, not a loss path.
- _`settings_schema.cpp` costs flash on ESP32_ -- verified discarded: 20 entries in
  "Discarded input sections", 0 symbols in the linked ELF.
- _The `CFG_FIELD_LIST` move changed the export or CRC form_ -- all 109 rows
  byte-identical, same order, one expansion per TU.

## Sharpest open lead

The survivors are exactly the fields `sanitize_loaded_settings()` touches (six
radio params plus the `SANITIZE_STR` list); the casualties are exactly the plain
ints it does not name. Six for six. No code path was found that produces that
split, and none was invented to fit.

**It cannot be settled by reading.** `DO_DEBUG 0` compiles out every diagnostic on
the new path, including the sanity-gate failure line, so a boot that formatted the
filesystem, failed the gate, or failed to write is indistinguishable from a clean
one on the console.

## Recommended order

0. **Instrument first.** Raw `Serial.printf` markers (not `DEBUG_MSG`) on the
   migration write, the sanity gate, `flash_reset()`, and `encode()` overflow; a
   command to dump `/MeshCom-Settings-Store`. Reproduce on RAK-90 across two
   reboots and settle the open lead. **No fix ships before this.**
1. `flash_reset()` removes its own files instead of formatting; `format()` only as
   a fallback. Withdraw the false downgrade comment.
2. Raise `kSettingsBufferCap` with the 4 036 B figure cited, and make an
   `encode()` overflow loud and non-silent.
3. Restore `node_msgid` / `node_ackid` persistence; fix `max_hop_text` via
   `CFG_ESC`.
4. Replace the byte gate with the member-level fail-closed invariant; make
   `settings_schema_lint.py` parse the struct.
5. Re-bench across at least two reboots, comparing against
   `docs/bench/w3-baseline/`.
