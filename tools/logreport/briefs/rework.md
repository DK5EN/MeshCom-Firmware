# Rework brief - move one report into the repo and apply the verified corrections

Used after the adversarial verification wave. One agent per page. Repo root R =
<repo>. Run dir RUN = `$R/docs/log-reports/<run-id>` (this run:
`2026-11-01-24h`). The report pages are self-contained HTML committed to the repo; the operator re-runs this
pipeline on future captures, so nothing may depend on a scratch directory.

## Read first

- `$R/tools/logreport/briefs/report.md` and `report-addendum.md` (structure, tone, animated explainer per issue,
  light/dark via kit tokens only, verification) - still binding.
- `$RUN/data/CORE-FINDINGS.md` - v2 after verification. It overrides v1 wherever they differ; v1 numbers that it
  marks WRONG must disappear from your page.
- `$RUN/verify/*.md` - the verifier tables (CONFIRMED / CORRECTED / REFUTED). Apply every CORRECTED/REFUTED item
  that touches your page; keep inferences labelled "Vermutung".
- `$RUN/verify/rca_own_beacons.md` if it exists (root cause of own-beacon starvation); if it does not exist yet,
  keep the three-part mechanism from CORE v2 and say the root cause of own-vs-relay difference is under analysis.

## Move (your exclusive file set)

- Your generator becomes `$RUN/generators/<page>.py` (top level = one page; the runner renders every top-level
  `*.py` to `$RUN/<page>.html`). Helpers go to `$RUN/generators/lib/<page>_*.py` (the runner does not execute
  lib/). Do not touch other pages' generators or helpers.
- The generator reads ONLY `--data` (= `$RUN/data`) and `--kit` (= `$R/tools/logreport`); no raw logs at render
  time, no fallback constants for numbers that a helper can measure. A helper that needs raw logs takes
  `--norm <dir>` and writes `$RUN/data/extra/<page>_<name>.json`; run it once on the normalised logs
  (`<logs>/.logreport-work/norm/` if present, else the scratch norm dir given in your dispatch) and commit-ready
  output lands in data/extra. Hard-coded literals from findings.md are allowed only for judgement text, never for
  a number that exists in data/; every number shown traces to a data file (name it in the dev details).
- No absolute paths, stdlib only, type hints, `python3 -I` clean.

## Correct and complete

- Apply CORE v2 and the verifier corrections (numbers, denominators, model-vs-measurement labels, code sites).
- New confirmed firmware findings to place where they concern your page (with an animated explainer if it is an
  issue on your page): RAK4631 auto-ping has no count limit (`src/nrf52/nrf52_main.cpp:2717-2737` vs
  `src/esp32/esp32_main.cpp:5264,5285`; also upstream); OE3XIA deaf receiver diagnosis; ping ACKs come from foreign
  gateways; OE3GJC-12 (RAK4631, 4.40a, Gateway Traiskirchen, 48.0135 N 16.2948 E, 213 m, mcmap) radiates stale
  {CET} - Vermutung: build without the gate (before 05a18b36 / #1238) or a trusted clock running minutes slow (an
  untrusted clock fails closed, `src/cet_age.h:129`).
- Readability: nothing may be cut off. SVG text stays inside the viewBox with margin; long labels wrap or shorten;
  tables scroll inside `.table-wrap`; check at 1280 px and 400 px width, light and dark (headless Chrome screenshot
  of your own file is fine: `--force-dark-mode` / default). No text smaller than the kit's `--fs-small`.

## Verify before you report

- `cd $R && python3 -I $RUN/generators/<page>.py --data $RUN/data --kit tools/logreport --out $RUN/<page>.html`
- `cd $R && uv run --quiet --with ruff==0.17.0 ruff check $RUN/generators/<page>.py $RUN/generators/lib/`
  (your files only - name them).
- `grep -c '<script' / 'https\?://' / 'src=\|@import\|url(' / '#[0-9a-fA-F]\{6\}'` on your html (hex only allowed
  inside the kit's `<style>` block), no `HW:[0-9]`, no TODO.
- Spot-check three corrected numbers on your page against data/ or the verifier table and list them.

Bans: no git, no pio, no transmissions, no edits outside your exclusive file set (never the kit, the runner, data/
files other than your own data/extra/<page>_*, verify/, other pages), no sleep polling, no unbounded reads of logs.
If the Write tool refuses a file, use a Bash heredoc. Final reply at most 12 lines: files, corrections applied
(count + the three that matter most), checks, open issues.
