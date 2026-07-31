# F0 — Orchestrator reconciliation against the existing `fable-verdict.md`

Prior art: `fable-verdict.md` at repo root, dated **2026-07-12**, branch `v4.35p_prio`,
39 findings (SEC-01 … TEST-39), method identical to this review (8 finders → adversarial
verification).

**The new `docs/architecture/` concept (01–07) does not cite this document once.** That is
itself a concept defect: the concept re-derives several of its structural conclusions
(`commandAction` size, battery duplication, `nrf_eth`/`udp_functions` duplication, absence
of tests, CI-on-tags-only) that were already recorded, verified and ID'd three weeks
earlier.

## Fix status of the 39 prior findings

Git history since the verdict was written:

```
1ba101f4 2026-07-30 fix(aprs): restore --symid symbol-table validation
49a48b74 2026-07-12 updated code reviews
7fcd7c7b 2026-07-12 chore: bump FLASH_VERSION to 20260712
b57d2559 2026-07-12 docs: upstream sync 2026-07-12 (dev)
```

No commit references any verdict ID (`git log --all | grep -iE 'SEC-0|BUG-1|CONC-1|DRY-2|SIMP-2|ALT-3|TEST-3'` → empty).
The June 26–27 fix commits (A1, A2, B1, B2, B3, B5, C1, C2, C3, D1, D2, D3, D5) predate the
verdict and address the earlier `docs/code-audit-*` series, not these IDs.

**Working conclusion: essentially all 39 findings are still OPEN.** Spot-verified below.

### Verified still open (orchestrator, structural items only — SEC/BUG/CONC left to finders)

| ID       | Claim                                                        | Current source                                                                                   | Status |
| -------- | ------------------------------------------------------------ | ------------------------------------------------------------------------------------------------ | ------ |
| SIMP-29  | dead files in `src/`                                         | `src/idf_component.yml.orig` present and byte-identical to `src/idf_component.yml`; `src/code_review/code-audit-20260508.md` still in the compiled tree | OPEN |
| SIMP-30  | `strSOFTSERAPP_ID` declared twice                            | `src/loop_functions_extern.h:319` and `:322`                                                     | OPEN |
| SIMP-30  | int/float HDOP twins                                         | `src/loop_functions_extern.h:274` `int posinfo_hdop`, `:275` `float fposinfo_hdop`               | OPEN |
| DRY-21   | nRF52 ETH ACK code diverged (0x01 vs 0x02)                   | `src/udp_functions.cpp:273` `0x01`, `:280` upgrade to `0x02`; `src/nrf52/nrf_eth.cpp:384` still `0x01` | OPEN |
| DRY-22   | `checkSerialCommand()` duplicated                            | `src/esp32/esp32_main.cpp:3872` and `src/nrf52/nrf52_main.cpp:2515`                              | OPEN |
| DRY-25   | I2C bus-reset guard uses `(BOARD_E22_S3)` without `defined()`| 10 sites — see escalation below                                                                   | OPEN, **worse than reported** |
| STATE-28 | `bGATEWAY` forced false without clearing the persisted bit   | set from `node_sset & 0x1000` at `esp32_main.cpp:768`, forced `false` at `:881` and `:888`       | OPEN |
| ALT-35   | `bOneButton` hijacked as display-dirty flag                  | `src/loop_functions.cpp:1875` and `:1956` (verdict cited one site at `:1939`; now **two**)       | OPEN, grown |
| TEST-36  | zero runnable tests                                          | `test/` holds `compress_functions.cpp/.h` + a header; no Unity harness                            | OPEN |
| TEST-37  | no native test environment                                   | `grep 'platform *= *native'` across all `platformio.ini` → empty                                  | OPEN |
| TEST-38  | CI builds on tags only                                       | `.github/workflows/meshcom-ci.yml` → `on: push: tags: '*'`                                        | OPEN |

### DRY-20 is marked RESOLVED in the verdict but is not resolved

The verdict's own body contradicts its verdict column: it states both files are live and
neither is dead code, then labels the row `RESOLVED`. The label appears to mean "the
disagreement between two finders was resolved", not "the defect was fixed".

Current counts (re-derived 2026-07-30, 30 variants):

- `USE_NEW_BATT` **defined**: 13 variants
- **not** defined → `batt_function_old.cpp` compiled: 17 variants
- (verdict counted 12 / 15 across 27 variants — the split has grown, not shrunk)

`docs/architecture/04-complexity-and-duplication.md` reports the same 13/17 split
independently. **Action: the `RESOLVED` label in `fable-verdict.md` must be corrected to
OPEN, or it will be read as done.**

## DRY-25 re-derived: the guard works, but by arithmetic on the board's product name

The verdict called this "a latent preprocessor bug" — missing `defined()`. That framing is
too mild, but the obvious escalation ("it must be a build error") is **wrong**. Verified
end to end below. Corrected conclusion first:

> On the two E22_S3 environments the guard evaluates **true**, which is the intended
> result — but only because the preprocessor performs subtraction on the undefined
> identifiers that make up the board's product name. Change the product name and the
> workaround silently disappears with no diagnostic.

**Build status: `pio run -e E22_1262_S3-DevKitC-1-N16R8` → SUCCESS (93 s).** Both E22_S3
environments compile today. No shipped environment is broken.

### Why the isolated repro was misleading

`platformio.ini` writes the macro with quotes:

```ini
-D BOARD_E22_S3="esp32-s3-devkitc-1-n16r8"
```

but PlatformIO's ini parser **consumes the quotes**. The real compiler invocation
(`pio run -v`, grepped from the `sht21.cpp` command line) is:

```
-DBOARD_E22_S3=esp32-s3-devkitc-1-n16r8
```

i.e. an *identifier sequence*, not a string literal. Passing the quoted form by hand does
produce a hard error, which is what the first repro showed — but that form never reaches
the compiler.

### What actually happens

`#if defined(BOARD_TBEAM_V3) || (BOARD_E22_S3)` expands on an E22_S3 build to:

```c
#if defined(BOARD_TBEAM_V3) || (esp32 - s3 - devkitc - 1 - n16r8)
```

Undefined identifiers evaluate to `0` in `#if`, so the right operand is
`0 - 0 - 0 - 1 - 0 == -1` → non-zero → **true**. Verified with the project toolchain
(`~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc`):

```
$ xtensa-esp32s3-elf-gcc -E -DBOARD_E22_S3=esp32-s3-devkitc-1-n16r8 pp2.c
int guard = 1;                     # guard active — the intended behaviour, by accident

$ xtensa-esp32s3-elf-gcc -E '#if (esp32-s3-devkitc-1-n16r8)' ...
int val_is_nonzero = 1;            # confirms 0-0-0-1-0 = -1

$ xtensa-esp32s3-elf-gcc -E -DBOARD_E22_S3=esp32-s3-devkitc-0-n16r8 pp2.c
int guard = 0;                     # counterfactual: one digit changed -> workaround GONE
```

The counterfactual is the finding. The I²C bus-reset workaround on E22_S3 hardware is
load-bearing on the arithmetic value of a marketing string. A board rename, a variant
copied for a new SKU, or any name whose digits sum to zero silently removes it — no error,
no warning, and the failure mode is an intermittent I²C sensor hang on real hardware.

**`-Wundef` catches it** (4 warnings on the minimal repro) and is not currently enabled
anywhere in `platformio.ini`.

### Corrected site list — 12 sites in 5 files

Re-derived with a preprocessor-aware scan (not grep): every `#if`/`#elif` whose expression
names a `BOARD_*` macro outside a `defined(...)`.

| File                    | Lines          |
| ----------------------- | -------------- |
| `src/aht20.cpp`         | 45, 70         |
| `src/bmp390.cpp`        | 54, 84         |
| `src/bmx280.cpp`        | 169, 213       |
| `src/rtc_functions.cpp` | 28, 71, 83, 99 |
| `src/sht21.cpp`         | 41, 68         |

The verdict listed 9 sites in 4 files. It **missed `src/rtc_functions.cpp` entirely** (4
sites) and `aht20.cpp:70`, and two sites it did list (`bmx280.cpp:131,143`) have since been
changed to plain `#ifdef BOARD_TBEAM_V3` — so E22_S3 now silently lacks the workaround at
those two places, a regression introduced after the verdict.

### Generalisation — this is a whole class, not one bug

**28 `BOARD_*` macros are defined as bare product-name strings** across `variants/*/platformio.ini`
(`BOARD_HELTEC_V3="heltec_v3"`, `BOARD_T_ECHO="T_ECHO"`, `BOARD_TBEAM_V3="tbeam_supreme_l76k"`, …).
Every one of them becomes an identifier sequence at the compiler. Any `#if` that tests one
without `defined()` silently does arithmetic on a product name. Today only `BOARD_E22_S3`
is used that way, but nothing prevents the next one — and `-Wall -Wextra` does not warn.

This is direct, mechanical evidence for the concept's `#ifdef`-instead-of-HAL critique
([01 §2](../../docs/architecture/01-system-overview.md)) and belongs in the
"mechanical enforcement" proposal: `-Wundef` plus a rule that board identity is a flag
(`-D BOARD_E22_S3=1`) and the human-readable name is a separate string macro.

---

## Original escalation attempt (superseded — kept so it is not re-derived)

The following reasoning was wrong and is retained per the "record refuted claims" rule.

`BOARD_E22_S3` is defined as a **string literal**, not a flag:

```ini
variants/E22_1262_S3-DevKitC-1-N16R8/platformio.ini:39
variants/E22_1268_S3-DevKitC-1-N16R8/platformio.ini:39
    -D BOARD_E22_S3="esp32-s3-devkitc-1-n16r8"
```

so `#if defined(BOARD_TBEAM_V3) || (BOARD_E22_S3)` expands to a string literal inside a
preprocessor expression. Reproduced with the project's own toolchain
(`~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc`):

```
$ xtensa-esp32s3-elf-gcc -E -DBOARD_E22_S3='"esp32-s3-devkitc-1-n16r8"' pptest.c ; echo $?
<command-line>: error: token ""esp32-s3-devkitc-1-n16r8"" is not valid in preprocessor expressions
 #if defined(BOARD_TBEAM_V3) || (BOARD_E22_S3)
                                 ^~~~~~~~~~~~
1
```

Exit code **1** — a hard error, not a warning. `||` does not rescue it: the right operand
is still parsed even when the left is true (verified with both macros defined → still
exit 1).

Affected sites (10, four files — the verdict listed `aht20.cpp:45`, `bmp390.cpp:54,84`,
`bmx280.cpp:131,143,169,213`, `sht21.cpp:41,68`; the actual current set differs and
**includes `rtc_functions.cpp`, which the verdict missed**):

| File                      | Lines            |
| ------------------------- | ---------------- |
| `src/aht20.cpp`           | 45               |
| `src/bmp390.cpp`          | 54, 84           |
| `src/bmx280.cpp`          | 169, 213         |
| `src/rtc_functions.cpp`   | 28, 71, 83, 99   |
| `src/sht21.cpp`           | 41, 68           |

(`bmx280.cpp:131,143` now use plain `#ifdef BOARD_TBEAM_V3` — those two were changed since
the verdict, so the E22_S3 board is silently missing the workaround there.)

Both affected environments — `E22_1262_S3-DevKitC-1-N16R8` and
`E22_1268_S3-DevKitC-1-N16R8` — are in `default_envs`, are not excluded by
`build_src_filter` (they inherit `[esp32]`'s `+<*>`), and therefore compile all four files.

**REFUTED — do not re-investigate.** Both predicted outcomes were wrong. The build
succeeds *and* the workaround does apply. The quoted macro form used in this repro is not
what PlatformIO passes to the compiler; see the corrected section above. Refuting evidence:
`pio run -e E22_1262_S3-DevKitC-1-N16R8` → SUCCESS, and `pio run -v` shows
`-DBOARD_E22_S3=esp32-s3-devkitc-1-n16r8` (unquoted).

## Implication for the concept

The concept's framing — "the project needs a test oracle before it can safely change
anything" — is *strengthened*, not weakened, by this reconciliation. There is already a
verified, ID'd backlog of 39 defects that nobody has been able to land, and at least one
of them is a build-guard error in two default environments that no automated check would
catch.

`docs/architecture/` must therefore:

1. cite `fable-verdict.md` as the standing defect backlog and not re-derive it,
2. record fix status per ID rather than re-discovering findings,
3. correct the DRY-20 `RESOLVED` label,
4. treat "land the existing 39" as a first-class workstream alongside "build the harness",
   because the harness exists to make landing them safe.
