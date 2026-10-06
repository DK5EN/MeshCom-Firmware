# Remote management extended commands - implementation plan

Status: APPROVED (2026-10-06), in progress; wave status in section 7. Builds on `extended-commands-concept.md` (draft 2)
and `extended-commands-verdict.md`. The operator decisions are in section 6 (`[Dn]`).

## 1. Flash overflow: cause and fix

### 1.1 Measured (2026-10-06, HEAD 91d378cb, trial edits reverted)

| Env           | As configured (`-Ofast`)                | With `-Os`        | Free with `-Os` |
| ------------- | --------------------------------------- | ----------------- | --------------- |
| `t_echo`      | link fails, 9 344 B over 815 104 B      | 594 904 B, 73.0 % | 220.2 kB        |
| `heltec_t114` | 799 780 B, 98.1 %                       | 571 084 B, 70.1 % | 244.0 kB        |
| RAK4631       | (already `-Os` since 2026-09-29) 77.3 % | -                 | about 185 kB    |

Cause: the Adafruit nRF52 framework adds `-Ofast` to every build. The RAK4631 env overrides it
(`build_unflags = -Ofast`, `build_flags = -Os`, `variants/wiscore_rak4631/platformio.ini:25-37`,
upstream since 4.40a). `t_echo` and `heltec_t114` never got that override and were at 96 % and
98 % before the RM page commits; those commits only pushed the T-Echo over the edge.

### 1.2 Options

| Option                                          | Gain (measured or attributed)                                                             | Cost and risk                                                                                                            |
| ----------------------------------------------- | ----------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| A: `-Os` on both envs (recommended)             | 204 kB on T-Echo, 229 kB on T114 (measured)                                               | 3 lines per env, the RAK precedent. No T-Echo or T114 on the bench: display and timing are not hardware-verified by us   |
| B: do not build the web layer on web-less nRF52 | about 140 kB in an `-Os` image (web code about 40 kB, page strings about 98 kB, Ethernet) | New macro, guards in `nrf52_main.cpp` and every caller, a second code shape to keep compiling; also untested on hardware |
| C: A now, B later as a hygiene item             | A, then B on top                                                                          | B is no longer needed for space; it stays a backlog row                                                                  |

Attribution in the T-Echo `-Os` image (linked symbols per object plus string literals):
`web_functions.cpp` 30.1 kB code and about 70 kB strings, `web_rm_page.cpp` 0.9 kB code and 26.4 kB
strings (the scaffold JS), `web_setup.cpp` 6.2 kB, `web_rm_handlers.cpp` 2.0 kB, `nrf_eth.cpp` 5.0 kB.
The web server can never start on these boards: `startWebserver()` is only reached behind
`neth.hasETHHardware` (`src/nrf52/nrf52_main.cpp:1184-1197`, `2571-2594`), a runtime flag.

Recommendation: option C. `-Os` restores the link with one known mechanism, ends `-ffast-math` on
both boards (the NaN checks that `-Ofast` folds away work again) and leaves room for the whole
campaign. Option B becomes a backlog row.

### 1.3 Gate gap

`tools/regression.sh` compiles no board firmware at all (stage 1 walks `[env:native*]` only); the
RM GUI board compiles were manual and skipped these two envs. W-1 adds a board link step with a
flash ceiling so a full or failing image turns the gate red.

## 2. Waves

Model tiers: `scout` (Haiku) for recon, `implementer` (Sonnet) for writer briefs, orchestrator for
hotspot integration and gates, `/fable-review` advisor pass before every behavioural commit.
One `pio` process at a time: writers run only their own native env; board builds belong to the gate.

Orchestrator-owned hotspots (no writer edits them inside a parallel wave): `src/rm_runtime.cpp`,
`src/remote_cmd.cpp`, `src/command_functions.cpp`, `src/web_functions/web_functions.cpp`,
`platformio.ini`, `tools/regression.sh`.

| Wave | Content                                                                                                                                                                    | Writers                          | Gate additions                                                          |
| ---- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------- | ----------------------------------------------------------------------- |
| W-1  | `-Os` on `t_echo` and `heltec_t114`; board link step in `tools/regression.sh` (t_echo, heltec_t114, wiscore_rak4631, flash ceiling); concept 2.5 corrected; backlog row B  | orchestrator                     | both envs link, flash below ceiling `[D2]`                              |
| W0a  | `sync` replay fix: own limiter 60 s, never blocks commands; regression test red before, green after                                                                        | 1 implementer (serial, hotspots) | `native_remote_cmd`, pytest twin                                        |
| W0b  | Buffers and `static_assert`s (concept 2.2), `RM_MAX_RESULT` 108, table allowlist, `rmSanitizeResult()`, `rm=2` token, Python twin and vectors, jsdom suite into the gate   | 1 implementer (serial, hotspots) | `lockActive` byte-compare test, `strlen(wire) <= 140`, jsdom in stage 2 |
| W1   | Four parallel writers on disjoint new files, hotspot edits applied by the orchestrator at the gate (section 3)                                                             | 4 implementers                   | each writer's own native env or jsdom; whole gate after                 |
| W2   | `execute()` dispatch glue (orchestrator), page cards P7, P8, P10-P12 (1 implementer on `web_rm_page.cpp` and the jsdom test)                                               | orchestrator + 1 implementer     | full gate, board link step                                              |
| W3   | Full gate `--stage 1,2`, size build on all RM boards incl. E22_XML-DevKitC, advisor on the campaign diff, flash and bench DK5EN-1 and DK5EN-92, RAK compile, docs, backlog | orchestrator                     | bench list of concept section 7                                         |

Commit per wave on `fork-dev`, explicit paths, after the gate and the advisor pass. No push, no
release, no upstream PR without a separate request.

## 3. W1 ownership (parallel, disjoint)

| Writer | Exclusive files                                                                                                                               | Verification slot              |
| ------ | --------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------ |
| W1a    | new `src/rm_format.h` (pure formatters, `rmFmtCount`, input structs), new `test/test_rm_format/`                                              | new env `native_rm_format`     |
| W1b    | new `src/rm_text.h` (text allowlist, HTML escape helper, pos range check), new `test/test_rm_text/`, `src/msgstore.cpp`, `src/msgstore_api.h` | new env `native_rm_text`       |
| W1c    | `src/rm_sender_policy.h`, new `src/rm_sent_book.h` (pure book extracted for native), `test/test_rm_sender_policy/`                            | `native_rm_sender_policy`      |
| W1d    | `src/web_functions/web_rm_page.cpp`, `tools/webgui_rm_test.js` (P1-P6, P9)                                                                    | `node tools/webgui_rm_test.js` |

Orchestrator at the W1 gate: the two new `[env:native_*]` sections are added to `platformio.ini`
before dispatch (so each writer has its slot); after the writers return, the integration edits in
`rm_runtime.cpp` (gather helpers, book 12, no `{NNN`, per-target forget, receiver unlock),
`command_functions.cpp` (`nodePasswdApply(nullptr)` sets rm off), `web_functions.cpp` (escape at
the three sinks plus source-lint test) and `web_rm_handlers.cpp`.

W1c and the hotspot edits it needs in `rm_runtime.cpp` are the riskiest part (real policy against
the real receiver in virtual time). If the extraction of the sent book does not come out header-only,
W1c is pulled out of the parallel wave and run serially after W1a/b/d.

## 4. Verification rules for every wave

- A fix ships with a test that fails before and passes after; the report shows both outputs.
- Positive existence check in the artifact for guarded code (symbol or string scan of the ELF).
- Size figures only from a clean, sequential build at the gate.
- Bench rules of `CLAUDE.md` apply (group TEST or own DM, own callsigns, at most 2 dBm, identity
  guard, `lsof` before a port).
- The docs (`extended-commands-concept.md` status, this plan's wave table, `BACKLOG`) are updated
  after each wave, before the next dispatch.

## 5. Risks

| Risk                                                                     | Handling                                                                                           |
| ------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------- |
| `-Os` on T-Echo and T114 without hardware on the bench                   | Same SoC, SoftDevice and SX1262 path as the RAK, which runs `-Os` since 09-29; field tester `[D3]` |
| The RM core is not upstream (`upstream/dev` has no `src/remote_cmd.cpp`) | The `sync` fix cannot be a stand-alone upstream PR; it rides with the RM upstream PR `[D4]`        |
| Version skew between old and new RM firmware                             | `rm=2` capability token; existing replies stay at 63 characters or less                            |
| Hotspot conflicts in W1                                                  | Writers own new files only; orchestrator integrates                                                |
| E22_XML DRAM headroom (about 19 kB, estimate)                            | Measured in W0b and W3 with release flags                                                          |

## 6. Decisions (operator, 2026-10-06)

| #   | Decision                                        | Outcome                                                                                  |
| --- | ----------------------------------------------- | ---------------------------------------------------------------------------------------- |
| D1  | Flash fix                                       | `-Os` on both envs now; dropping the web layer on web-less nRF52 boards is a backlog row |
| D2  | Flash ceiling in the gate for nRF52 board links | fail above 95 %                                                                          |
| D3  | T-Echo and T114 hardware verification of `-Os`  | ship, no follow-up                                                                       |
| D4  | `sync` fix                                      | plain commit on `fork-dev`; goes upstream inside the later RM PR                         |
| D5  | New commands on T-Echo and T114                 | yes, same code on all boards                                                             |
| D6  | Radio parameters (SF, CR, BW, frequency)        | read-only                                                                                |
| D7  | `maxhop`                                        | read-only                                                                                |
| D8  | `pos` read on the public server                 | answers only when the node already beacons its position                                  |
| D9  | Sender limit                                    | 2 unanswered while the key is unproven, 10 once proven; spacing 10 s                     |
| D10 | "Try once more anyway" after a re-key           | yes, one extra attempt, without a warning dialog                                         |
| D11 | MHEARD window for `mh`                          | 3 h, like the web page                                                                   |
| D12 | Scope of the go                                 | whole campaign through the bench; stop before release, no push                           |

## 7. Wave status

| Wave | Status                                                                                                                                                    |
| ---- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| W-1  | done 2026-10-06: gate 64 envs / 2115 cases green, selftest 48 green, board links 73.0 / 70.1 / 77.3 %; no advisor pass (build flags and gate script only) |
| W0a  | open                                                                                                                                                      |
| W0b  | open                                                                                                                                                      |
| W1   | open                                                                                                                                                      |
| W2   | open                                                                                                                                                      |
| W3   | open                                                                                                                                                      |
