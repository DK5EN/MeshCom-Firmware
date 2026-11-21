# Plan: web getparam `tempoffsetoutdoor` is unreachable (P-0385)

Date: 2026-11-21. Branch `fork-dev` at `067d29cd`. Problem file `P-0385` (RCA at pin `96aaa67c72`). Backlog id
`WEB-TEMPOFF-01`. Nothing implemented in this session.

## 1. Symptom

`GET /getparam/?tempoffsetoutdoor` never returns the outdoor temperature offset: HTTP 422,
`{"returncode":2,"tempoffsetoutdoor":""}` (`WS_RETURNCODE_UNKNOWN`, `src/web_functions/web_setup.h:16`; 422 chosen at
`src/web_functions/web_functions.cpp:5197-5200`). `getparam` for `tempoffsetindoor` works, and both `setparam`
rungs work (they also return the stored value in the answer). Only scripted clients hit this; the browser form renders
the value from the settings (`web_functions.cpp:3687-3688`) and never calls `getparam`.

## 2. Root cause at HEAD

`webSetup_getParam()` (`src/web_functions/web_setup.cpp:882`) has two consecutive branches with the same condition:

- `:1053` `if(setupData->paramName.equals("tempoffsetindoor"))` returns `node_tempi_off` (correct).
- `:1058` `if(setupData->paramName.equals("tempoffsetindoor"))` returns `node_tempo_off`. Copy-paste of `:1053` with only the
  returned variable changed; the condition should be `"tempoffsetoutdoor"`. The branch is dead; a request for
  `tempoffsetoutdoor` falls through to `returnCode = WS_RETURNCODE_UNKNOWN` (`:1064`).
- The name is not in the uniform `wsParams` table either (`wsFindParam()` lookup at `:885`; `grep -n tempoffset
src/web_functions/*` at HEAD finds only `:831` and `:847` (setters), `:1053` and `:1058` (getters) and the page
  builder `web_functions.cpp:3687-3688`).
- Origin: `git blame -L 1058,1059` gives `6cc92828` (Marco Kubon, 2026-01-21, "Implemented the missing INA226 functions
  and corrected webui"); `git show 6cc92828 -- src/web_functions/web_setup.cpp` adds both getter blocks with the
  duplicated condition. `1afefe27` (v4.35a.07.13) only introduced the setter strings. Both are ancestors of HEAD; no
  revert touches these lines (`git log -S'tempoffsetindoor")) {' -- src/web_functions/web_setup.cpp` lists only these two
  commits). The P-0385 RCA said "whether the duplicate came with `1afefe27` was not checked": it did not, it came with
  `6cc92828`.
- Changes since the pin: `git log --oneline 96aaa67c72..HEAD -- src/web_functions/web_setup.cpp` shows only `ce05f38c`
  (APPLINK-01, adds the `applink` row, does not touch these lines). The scout's `:1053`/`:1058` are right; the pin's
  `:1045`/`:1050` moved by +8.
- Scope check: inside `webSetup_getParam()` this is the only literal that occurs twice
  (`awk '/^void webSetup_getParam/{f=1} f' ... | grep -o 'paramName.equals("[^"]*")' | sort | uniq -d` prints only
  `tempoffsetindoor`); the setter function has no duplicate literal.

## 3. Platforms

The code is shared and has no platform guard around these lines (the file is compiled when
`!defined(NRF52_SERIES) || HAS_NRF_ETH`, `web_setup.cpp:4`):

| Platform                                                                                                       | Affected                          |
| -------------------------------------------------------------------------------------------------------------- | --------------------------------- |
| ESP32 (all boards with the web server)                                                                         | Yes.                              |
| nRF52 `wiscore_rak4631` with RAK13800 Ethernet (`HAS_NRF_ETH=1`, `variants/wiscore_rak4631/platformio.ini:38`) | Yes, same file, same lines.       |
| nRF52 T114, T-Echo (`HAS_NRF_ETH=0`)                                                                           | File not compiled, no web server. |

The offset values exist on every platform (`node_tempo_off`, `--tempoff out`, `src/command_functions.cpp:4447`), so the
getter's only dependency is the settings struct.

## 4. Fix design

One token: `src/web_functions/web_setup.cpp:1058`, `"tempoffsetindoor"` to `"tempoffsetoutdoor"`. Nothing else in the file
changes; no new helper (the getter style of its neighbours is kept). Optional doc line in
`src/web_functions/Web-API_documentation.txt` (the "Read-only parameters" list at `:146` shows what is documented, and no
`tempoffset` getter is listed): `/getparam/?tempoffsetindoor` and `/getparam/?tempoffsetoutdoor`, value in degrees
Celsius as `String(float)` (two decimals, for example `-0.50`). Doc line is a suggestion for the same change, not a
requirement.

## 5. Test

Executable host test is not possible: `webSetup_getParam()` needs `commandAction`, the settings struct and Arduino
`String`, and no native env compiles `web_setup.cpp`. The seam used by the neighbouring tests is the source pin
(`read_repo_file()`, `test/test_command_setters/test_command_setters.cpp:224`, as in
`test_web_temp_offset_takes_comma_and_leading_zero`, `:1035`). Add to the same file and register it after `:2563`; env
`native_command_setters` (`platformio.ini:2193`, `test_filter = test_command_setters`).

`test_web_getparam_outdoor_offset_is_reachable`:

1. Cut `set = read_repo_file("src/web_functions/web_setup.cpp")` from `"void webSetup_getParam("` to the end of the file.
2. Assert the cut contains `paramName.equals("tempoffsetoutdoor")` and that between that position and the next
   `return;` the text `node_tempo_off` appears (the branch reads the outdoor offset, not the indoor one).
3. Assert the indoor branch still reads `node_tempi_off`.
4. Assert that no `paramName.equals("...")` literal occurs twice inside the cut (generic guard against the same
   copy-paste class; it passes at HEAD except for the one literal this plan fixes).

Fails today: step 2, because the cut has no `equals("tempoffsetoutdoor")` literal at all (the outdoor branch tests
`tempoffsetindoor`), and step 4, because `tempoffsetindoor` occurs twice. Passes after the one-token change. Run:
`pio test -e native_command_setters -f test_command_setters` (one `pio` at a time; never bare `pio test`).

## 6. Bench

Low value, cheap, needs a node with the web server on (ESP32, or the RAK4631 with RAK13800). Bench rules: own call
sign, TEST group, TX power at most 2 dBm; this step sends nothing on air.

1. `--tempoff out 1.5` on the console (or `/setparam/?tempoffsetoutdoor=1.5`).
2. `curl -i 'http://<node>/getparam/?tempoffsetoutdoor'`. Expected after the fix: HTTP 200 and a JSON body with
   `returncode` 0 and `tempoffsetoutdoor` "1.50". Before the fix: HTTP 422, `returncode` 2, empty value.
3. Same for `tempoffsetindoor` (unchanged, must stay 200).

If the web GUI session or login is enabled on the node, send the request with the session cookie as the other
`getparam` calls need (`web_auth`); the check itself does not depend on it.

## 7. Upstream impact

Yes, affected, identical. `git show upstream/dev:src/web_functions/web_setup.cpp` has the same pair at `:957` and `:962`
(both `tempoffsetindoor`), setters at `:749` and `:765`; `6cc92828` is an ancestor of `upstream/dev`
(`git merge-base --is-ancestor`, rc 0). Upstream also has no `tempoffset` getter in `Web-API_documentation.txt` (count 0
there). One-token PR from `upstream/dev`, German body; trivial port. Nothing is sent from this plan.

## 8. Risks

- Practically none: one condition on a branch that is unreachable today; no behaviour of any other parameter changes.
- A client that probed `tempoffsetoutdoor` and treated the 422 as "unsupported" now gets a value; that is the intent.
- The generic duplicate-literal guard (step 4) could flag a future deliberate duplicate; the message names the literal
  so the test is easy to adjust.
- The pin is text based; reformatting the branch (for example moving it into the `wsParams` table) would need the
  test updated. If the operator prefers the table, the outdoor and indoor offsets need float rows with their own
  setter handling (`--tempoff in|out`), which is a larger change and not recommended for this defect.

## 9. Evidence checklist

- `6cc92828` and `1afefe27` are ancestors of HEAD; `6cc92828` is an ancestor of `upstream/dev`; not reverted.
- Every path:line was read at HEAD (`067d29cd`): `web_setup.cpp:831`, `:847`, `:882`, `:885`, `:1053`, `:1058`,
  `:1064`; `web_functions.cpp:3687-3688`, `:5200`; `web_setup.h:16`.
- Platforms read separately: the compile guard `web_setup.cpp:4`; `HAS_NRF_ETH` for `wiscore_rak4631` (1),
  `heltec_t114` and `t_echo` (0).
- Not verified: no request sent to a node, no test run, no build (nothing may be built in this session).
