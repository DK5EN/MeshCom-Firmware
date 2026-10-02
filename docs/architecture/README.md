# MeshCom Firmware — Architecture Documentation

Living documentation of the firmware's structure, dependency state, and maintainability
profile. Written to be drilled into: each document answers one question and stays
independently readable.

Baseline: branch `v4.35p_prio`, commit `1ba101f4`, analysed 2026-07-30.
Version `SOURCE_VERSION 4.35p`, `FLASH_VERSION 20260712`.

## Documents

| #   | Document                                                         | Answers                                                                                                                                           |
| --- | ---------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| 01  | [System Overview](01-system-overview.md)                         | Is this spaghetti code, or a core with modules? Where is the kernel?                                                                              |
| 02  | [Build System & Variants](02-build-and-variants.md)              | How do 30 board variants map onto one source tree?                                                                                                |
| 03  | [Dependencies](03-dependencies.md)                               | What is outdated, what breaks on upgrade, which hardware is at risk?                                                                              |
| 04  | [Complexity & Duplication](04-complexity-and-duplication.md)     | Where is the code tangled, duplicated, or unmaintainable?                                                                                         |
| 05  | [Rewrite vs. Refactor](05-rewrite-vs-refactor.md)                | Does a 1:1 port make sense? What is the cheaper path to the same goal?                                                                            |
| 06  | [Test Strategy](06-test-strategy.md)                             | How do we get an oracle that makes before/after comparison possible?                                                                              |
| 07  | [Verification Infrastructure](07-verification-infrastructure.md) | What can be observed, driven and asserted today — and what hooks and bench setup should be added?                                                 |
| 08  | [Defect Catalogue & Remediation Plan](08-defect-catalogue.md)    | What is actually broken, in what order do we fix it, and how does each fix become one commit and one PR?                                          |
| 09  | [Concurrency & Core-Ownership Map](09-concurrency-map.md)        | Which core, task or ISR touches which state — so atomics sit exactly where there is real concurrency, and nowhere else                            |
| 10  | [Buffer & Type Inventory](10-buffer-inventory.md)                | Every buffer, its size, its bounds and the input channel that reaches it — and how to make overflow structurally impossible                       |
| 11  | [Wire Format](11-wire-format.md)                                 | Byte-level reference of all four protocol layers (LoRa, server UDP, EXTUDP JSON, BLE) — the basis for mock services and spec-derived test vectors |

Documents 01–05 describe the system as it is. 06 and 07 describe how to make changes to it
verifiable: 06 is the strategy (which layers, why), 07 is the build-out (existing
instrumentation, missing test hooks, physical bench design, scenario catalogue). 08 is the
verified defect backlog and the order of work.

> ### Read 08 before acting on 01–07
>
> Documents 01–07 were written before an adversarial review and **contain errors that would
> send work in the wrong direction**. [08 §1](08-defect-catalogue.md#1-corrections-to-this-concept-docs-0107)
> lists every correction; the load-bearing ones are:
>
> - **C-03** — the "golden-vector corpus you already own" **does not exist** (measured yield:
>   0 usable pairs from 17 logs). 06's Layer 2 cannot be built as specified.
> - **C-02** — the "extract a radio interface, collapse ~3,200 lines" recommendation is
>   ~10× oversized and mis-targeted. **Withdrawn.**
> - **C-01** — `OnRxDone` does **not** run in interrupt context on either platform.
> - **C-04** — four boards run Arduino 2.0.14, not 2.0.17. 03's headline is false for them.
> - **C-05** — 02's finding B-01 proposes a change that would **break two shipping boards**.
>
> Prior art the concept missed entirely: `docs/code-audit-20260712.md` (2026-07-12) holds 39
> already-verified findings, essentially all still open.

## Reproducing the metrics

The numbers in these documents come from two scripts checked in under `tools/`:

```bash
python3 tools/arch_metrics.py src        # function size, decision points, nesting, #ifdef density
python3 tools/arch_duplication.py src 12 # token-normalised clone detection, 12-line window
```

Both are heuristic (regex-based, no libclang). They are good enough to rank hotspots and
bad enough that individual numbers should not be quoted as exact. Where a claim mattered,
it was verified by hand — those places say so.

## Scope note

`lib/` holds vendored third-party libraries (LVGL, TFT_eSPI, GxEPD2, epdiy, XPowersLib, …)
and is excluded from all "our code" counts. So are font tables, image assets and map data
under `src/Fonts/`, `src/GFX_Root/`, `src/*/maps/`, `Font_*`, `img_*`, `firasans*`.

## Size at a glance

| Scope                                                                 | Lines   |
| --------------------------------------------------------------------- | ------- |
| Own firmware code (`src/`, assets excluded)                           | ~71,100 |
| — core services (`src/*.cpp`, `src/*.h`)                              | ~27,200 |
| — display/UI stacks (`t-deck`, `t-deck-pro`, `t5-epaper`, `Displays`) | ~24,100 |
| — MCU layers (`src/esp32/`, `src/nrf52/`)                             | ~12,600 |
| — web UI (`src/web_functions/`)                                       | ~3,250  |
| — safeboot bootloader app (`src/safeboot/`)                           | ~3,220  |
| — board power/platform shims (`src/Platforms/`)                       | ~1,130  |
| Vendored libraries (`lib/`)                                           | ~1.4M   |
| Board variants (`variants/`)                                          | 30 dirs |
| Unit / integration tests                                              | 0       |
