# Command unification: RM1 grammar as the canonical layer over the `--` console

Status 2026-10-12: concept, recon wave done, no code changed. Resume point for the campaign.

## BLUF

- Keep every `--` string working: same effect and the same legacy output. External clients
  (phone app, McApp/MCProxy, KISS, ext-UDP, store clients, web setparam) depend on them.
- Make RM1 the canonical grammar: lowercase noun, `noun on|off`, a bare RW verb reads,
  replies are `ok key=value ...` or `err <token>`.
- One executor per logical command. RM1 and the console call the same setter, which validates once
  and returns an `ok`/`err` result. This replaces the current path, where RM1 renders a console
  literal (`"--gps on"`), runs it through `commandAction()` and reads the state back to see whether it
  worked.
- One registry, grown from `RM_COMMAND_LIST`: every canonical verb, its shape, the surfaces it is
  allowed on, and its frozen legacy spellings. The RM1 allowlist stays a positive, explicit subset
  (an RM bit per row), and a `static_assert` keeps it at today's 23 verbs until a row is added on
  purpose.
- One new console entry, `--cmd <verb> [args]`. It runs the RM1 executor locally without HMAC and
  answers with the RM1 reply body. One grammar then covers USB, 2323, BLE and LoRa, and the bench can
  test every RM1 verb without a radio.

## 1. Findings (recon wave, 2026-10-12)

Inventories: `inventory-rm1.md`, `inventory-console-part1.md` (lines 1-3430 of
`command_functions.cpp`), `inventory-surfaces.md`. Part 2 (3431-EOF, 163 match strings) was
reported inline and must be regenerated before wave 2.

Scale: about 230 `--` first tokens and about 370 match strings, against 23 RM1 verbs. The parser's only
gate is the `--` prefix. Restrictions sit at the surface edges (web password, 2323 HMAC, BLE PIN,
RM1 HMAC plus allowlist).

### 1.1 Drift between RM1 and the console

| ID   | Topic                      | RM1                                                                                          | `--` console                                                                                                                                       | Evidence                                                                        |
| ---- | -------------------------- | -------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| U-01 | txpower range              | `max(0, TX_POWER_MIN)..TX_POWER_MAX`, `err range 0..22`                                      | `TX_POWER_MIN..MAX` = -9..22, reply only via printfdeb; `settings_schema.cpp`/`config_json.cpp` say -20..30                                        | `remote_cmd.cpp:179`, `command_functions.cpp:5032`, `settings_schema.cpp:64-68` |
| U-02 | name                       | `rmTextAllowed` charset, refuses `none`, `err text` if longer than 19                        | strips `#`, silently truncates to 19, no charset check                                                                                             | `rm_exec_write.cpp:44`, `command_functions.cpp:2744`                            |
| U-03 | atxt                       | charset without `,` `/`, refuses `none`, `err text`                                          | `none` clears, truncates to `RM_ATXT_MAX`, no charset                                                                                              | `rm_exec_write.cpp:52`, `command_functions.cpp:3485`                            |
| U-04 | position                   | atomic `pos lat lon alt`, `err range`, `err gps` while GPS is on                             | three commands `--setlat/--setlon/--setalt`, no GPS check, `--setalt` out of range only prints "ignored"                                           | `rm_exec_write.cpp:83-110`, `command_functions.cpp:4087-4143`                   |
| U-05 | LED                        | `led on\|off\|flash` on `REMOTE_LED_PIN`, which is `BOARD_LED` on most boards, not persisted | `--board led on\|off` on the same pad, persisted in `node_sset3`                                                                                   | `loop_functions_extern.h:31-37`, `command_functions.cpp:517`                    |
| U-06 | charge LED                 | `chgled auto\|on\|off`, runtime only                                                         | `--charge led on\|off`, persisted, no `auto`                                                                                                       | `rm_runtime.cpp:293`, `command_functions.cpp:513`                               |
| U-07 | maxhop                     | read only, `ok t=<n> p=<n>`                                                                  | `--maxhop <n>` writes text hops, bare form prints `[MAXHOP];text;N;pos;N`                                                                          | `rm_exec_read.cpp:377`, `command_functions.cpp:4402,4433`                       |
| U-08 | mh                         | `mh <row>` paged, `mh <call>` detail, key=value                                              | `--mh`/`--mheard` dumps the whole list as `[MH]` lines, load-bearing for the phone MH JSON                                                         | `rm_exec_read.cpp:176-330`, `command_functions.cpp:6002`                        |
| U-09 | reads without console twin | `status radio sens txq` have formatters                                                      | none: `--info` (JSON), `--lora` (text), `--wx` (JSON); no txq at all                                                                               | `rm_format.h`                                                                   |
| U-10 | mbox                       | `ok m= u= b= a= ...`                                                                         | `[MBOX]` lines, load-bearing for the store client contract                                                                                         | `rm_exec_read.cpp:137`, `command_functions.cpp:4669`                            |
| U-11 | toggle table               | `kToggles`: gps track display gateway mesh                                                   | `COMMAND_TOGGLES` has `gps on` but not `gps off` (ladder 2053); `track`/`display` are ladder rungs only                                            | `rm_runtime.cpp:114-125`, `command_functions.cpp:521,2006,2053,1534`            |
| U-12 | result channel             | `ok`/`err <token>`, a fixed token set                                                        | nothing, printfdeb, `[TAG];...`, `[ERR];x;...`, `--wrong command`, depending on the rung                                                           | `remote_cmd.cpp:668-745`                                                        |
| U-13 | client validation          | firmware                                                                                     | `tools/remote_cmd.py` accepts the name/atxt charset, lat/lon up to 999 and alt up to 99999 that firmware refuses; the web page has no txpower tile | `tools/remote_cmd.py:59-92`, `web_rm_page.cpp:275`                              |

Structural root cause of U-01..U-04 and U-12: `commandAction()` returns `void`. RM1 can only learn
whether a write worked by reading the field back. Each validator therefore exists twice: a strict
copy in RM1 and a lenient or silent one in the rung.

### 1.2 Console-internal inconsistencies (not drift)

Grammar is mixed: `set` prefix (`setname`, `setcall`, `setlat`) versus bare nouns (`atxt`, `txpower`,
`maxhop`); chip-number nouns (`390`, `680`, `811`); `on|off` versus `0|2|<n>` (`gpsdebug`) versus
`csv|man|en|de` (`debug`); two spellings per polarity (`sethamnet on` = `setinet off`, `proz off`
= `volt on`); a bare form that prints in some families and is missing or destructive in others.
Errors may be silent, may print only via printfdeb, or may be `[ERR];...`.

Scout-reported defects. These go to a separate bug track, each with its own regression test.
Verified ones are marked V; the rest still need a check:

| ID   | Defect                                                                                                       | Where     |
| ---- | ------------------------------------------------------------------------------------------------------------ | --------- |
| C-01 | V `--btcode <non-number>` parses to 0 and silently disables BLE security                                     | 3298      |
| C-02 | V `--pingmax max` sets 100 while `--pingmax N` clamps to `PING_MAX` 5                                        | 3676      |
| C-03 | V `--bme off` / `--bmp off` also clear BMP390 (intent unclear, comment says own chip only)                   | 2328      |
| C-04 | cleartext password echo: `--setcont on` prints `MORE:`/`ONE:` with `--passwd <pw>`; `--webpwd` echoes to BLE | 626, 2704 |
| C-05 | `--button gpio` starts onebutton on the old pin                                                              | 1728      |
| C-06 | `--softser fixtemp` truncates to 4 chars (`msg_text[22]`)                                                    | 3251      |
| C-07 | `--ptime` fallback 1800 lies outside its own 5..120 range                                                    | 6464      |
| C-08 | bare `--setgrc` zeroes all six groups                                                                        | 6281      |
| C-09 | `--setublox foo` / `--setl76k foo` mean off                                                                  | 3097      |
| C-10 | `--extudpip` refusal blanks `node_extern` in RAM                                                             | 2805      |
| C-11 | `--onewire gpio` on RAK keeps a refused value in RAM until the next save                                     | 2407      |
| C-12 | TX power range defined three times with two values (-9..22, -20..30)                                         | see U-01  |

## 2. Target design

### 2.1 Principles

1. **Frozen legacy.** Every `--` match string keeps parsing, with the same effect and the same output.
   Only an approved bug fix (section 1.2) or an approved validation decision (section 4) changes
   console behaviour, and each one gets a changelog line.
2. **RM1 grammar is canonical.** New functionality is added as a registry row and is reachable via
   `--cmd`. A new `--` spelling is added only where it fills a gap and collides with nothing.
3. **One setter, one validator.** Setters return `ok` or `err <token>`. Legacy rungs and RM1 both
   call them. Legacy rungs map the result to their legacy print.
4. **The security boundary is unchanged.** The RM1 allowlist stays positive and explicit. No verb
   becomes remote-reachable through this work unless the operator adds its RM bit.

### 2.2 Layers

```
 USB / 2323 / BLE 0xA0 / web manualcommand        LoRa RM1 DM (HMAC, ctr, allowlist)
            |                                                   |
   commandAction("--...")                                    rmCheck()
     |                 \                                        |
 legacy rung        "--cmd <verb> args"  ------------------> cmdExecute(verb, args, res)   [registry]
 (parse legacy           (no HMAC; local                         |
  args, legacy print)     surfaces only)                  setter / reader per verb
     \_____________________________________________________/     |
                         one validator, one result            ok key=value | err token
```

- **Registry** `src/cmd_registry.h` (grown from `rm_commands.h`):
  `X(name, KIND, SHAPE, SURFACES, LEGACY)`. SURFACES is a bitmask CONSOLE|BLE|WEB|RM. LEGACY names
  the frozen `--` spellings, which the help lint uses. The format contract of `rm_commands.h`
  (regex-parsable, read by `tools/webgui_rm_test.js`) carries over. `tools/remote_cmd.py` also
  starts reading its shapes from this file (closes U-13).
- **Executor** `cmdExecute()`: the body of today's `rm_runtime.cpp` `execute()` plus
  `rm_exec_read.cpp` / `rm_exec_write.cpp`, made transport-free. RM1 framing (ctr, tag, budget,
  reply DM) stays in `remote_cmd.cpp` / `rm_runtime.cpp`.
- **Setters** `src/cmd_set_*.cpp`, grouped by family (toggles, text, position, radio, outputs).
  `runConsole()` and the read-back go away for every migrated verb.
- **Console entry** `--cmd` in `command_functions.cpp`: one rung that prints `ok ...`/`err ...`.
  BLE gets the same line through the existing `addBLECommandBack` path.

### 2.3 Drift resolutions (recommendations)

| ID   | Recommendation                                                                                                                                                                     |
| ---- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| U-01 | One range function `txPowerRange()` in the setter. Delete the -20..30 fallback or derive it. RM floor: see question Q3.                                                            |
| U-02 | Setter validates with `rmTextAllowed`. The legacy `--setname` keeps its normalisation (strip `#`, truncate) before the setter and prints `[ERR];setname;text` on refusal (Q2).     |
| U-03 | as U-02 for `--atxt`/`--aprscomment`. `none` stays the legacy clear word on the console only.                                                                                      |
| U-04 | One setter `posSet(lat?, lon?, alt?)`. RM1 writes all three, the legacy commands write one each. GPS-on rule: Q2.                                                                  |
| U-05 | Define the interaction: RM1 `led` is a runtime override. `--board led` stays the persisted default and wins on boot. Document it; check the "unless RM" branch at 518 on hardware. |
| U-06 | Keep both. Canonical `chgled` = runtime override; `--charge led` = persisted default. Add the `auto` value only to the canonical verb.                                             |
| U-07 | Registry row `maxhop` becomes RW (read, `maxhop <text>` write). The RM bit for the write is Q5.                                                                                    |
| U-08 | `--mh` stays the legacy full dump (phone contract). `--cmd mh <row\|call>` gives the RM1 paging.                                                                                   |
| U-09 | `status radio sens txq` become reachable via `--cmd`. `--info`, `--lora`, `--wx` stay unchanged.                                                                                   |
| U-10 | as U-08: `--mbox` legacy, `--cmd mbox` canonical.                                                                                                                                  |
| U-11 | Move `gps off`, `track`, `display` into `COMMAND_TOGGLES` (or the toggle setter) so that `kToggles` derives from it and the hand-kept copy goes.                                   |
| U-12 | Legacy output stays. `--cmd` is the machine-readable channel. Clients that need results (McApp, bench) move to `--cmd`.                                                            |
| U-13 | Tool and web read shapes and ranges from the registry. The web page gets a txpower tile.                                                                                           |

## 3. Waves

| Wave | Content                                                                                                                                                                                    | Files (exclusive owner)                                                                                                                                                                                                       | Dispatch                                                                              | Est. cost / wall-clock |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ---------------------- |
| W0   | Recon (done)                                                                                                                                                                               | none                                                                                                                                                                                                                          | 4 scouts Haiku medium                                                                 | done                   |
| W1   | Registry + `cmdExecute()` extraction + `--cmd` rung. Zero behaviour change for every `--` string. Native test: every registry verb gives the same body via `--cmd` and via the RM executor | `rm_commands.h` -> `cmd_registry.h`, `rm_runtime.cpp`, `rm_exec_read.cpp`, `rm_exec_write.cpp`, new `cmd_execute.cpp`, new `test/test_cmd_execute/`; the `--cmd` rung in `command_functions.cpp` is an orchestrator carve-out | 1 implementer Sonnet medium + advisor Opus                                            | ~$8 / 40 min           |
| W2   | Setters per family, RM1 off `runConsole()`/read-back; legacy rungs call setters (rung edits = orchestrator carve-out, `command_functions.cpp` is the hotspot)                              | A toggles `cmd_set_toggle.cpp` + `command_toggles.h`; B text `cmd_set_text.cpp`; C position `cmd_set_pos.cpp`; D radio/txpower `cmd_set_radio.cpp` + `configuration_default.h`/`settings_schema.cpp` range                    | 4 implementers Sonnet medium in parallel, native tests per family; one build env each | ~$20 / 45 min + gate   |
| W3   | Drift decisions applied (U-01..U-07 per section 4 answers) with fail-before/pass-after tests; tool + web shape import                                                                      | `tools/remote_cmd.py` + its tests; `web_rm_page.cpp` + `tools/webgui_rm_test.js`; setters from W2                                                                                                                             | 2 implementers Sonnet medium                                                          | ~$12 / 30 min          |
| W4   | `--help` and `docs/commands.md` generated from the registry; `help_parity_lint.py` extended; bench differential: same verb over USB `--cmd` and over LoRa RM1 returns the same body        | `test/golden/help_parity_lint.py`, new `tools/gen_command_doc.py`, `docs/commands.md`; bench serialized at the gate                                                                                                           | 1 implementer + orchestrator bench run                                                | ~$8 + bench 30 min     |
| W5   | Console bug track C-01..C-12, verified first, one regression test each                                                                                                                     | single rungs in `command_functions.cpp` -> serialized or one implementer                                                                                                                                                      | 1 implementer Sonnet medium per batch                                                 | ~$10                   |

Every behavioural wave goes through the advisor gate (Opus medium) and the host gate
`tools/regression.sh --stage 1,2` before the commit. W2 and W4 need bench stage 3 for the RM1 path.

Upstream: RM1 is fork-only (upstream #1189 pending). The registry and setters land on `fork-dev`.
Each `command_functions.cpp` rung edit adds conflict surface at the next upstream sync, which is
why Q4 keeps the rung edits small.

## 4. Open questions (operator)

- **Q1** Name of the canonical console entry: `--cmd <verb>` (recommended; no collision, grep-checked
  2026-10-12), or bare RM1 verbs under `--` where they do not collide. Several do collide:
  `--pos`, `--mh`, `--mbox` and `--maxhop` exist with different output.
- **Q2** Tighten console validation to RM1 rules: refuse with `[ERR];...` for name/atxt charset,
  `--setalt` out of range, and setlat/lon while GPS is on (recommended). The alternative keeps the
  console lenient and accepts that the two paths validate differently.
- **Q3** txpower floor: today RM1 uses 0 and the console -9 (RAK: 2..22). One floor for both, and
  which one?
- **Q4** Depth inside `command_functions.cpp`: only the roughly 15 rungs that back a registry verb
  (recommended, small upstream conflict surface), or all families over time.
- **Q5** Grow the RM1 write set (`maxhop` write, persisted `chgled`)? Default: no. Every added RM
  bit is a security decision.
