> Template as used for the 2026-11-01 run (docs/log-reports/2026-11-01-24h). Before reuse, replace the run-specific
> paths (scratchpad, run dir), node names, capture spans and facts; keep the rules and bans.

# Addendum to BRIEF-REPORT.md (operator requirements, 01.11.2026) - binding

1. **Animated explainer drawing per identified issue.** Every Handlungsbedarf and Beobachten item and every
   firmware bug candidate on your page gets one small drawing that explains the mechanism (e.g. queue with
   prio 4/5 waiting behind ACKs until TTL expires; ping -> N ACKs -> M relays; stale {CET} relay; deaf
   receiver while CAD/TX continue). Use `from reportkit import explain` (new in the kit, see its docstring)
   with the `.d-*` shape/state classes and `.anim-flow / .anim-pulse / .anim-move / .anim-drop` animation
   classes defined in the kit CSS. The resting frame must already tell the story; SVG text >= 12 px, inside
   the viewBox, umlauts written ae/oe/ue inside SVG text; colours only via classes (light/dark aware).
   Keep each drawing simple (5-12 shapes). Not counted against the 6-chart limit.
2. **Generator is committed later.** Your generator script will be moved into the repo
   (`docs/log-reports/2026-11-01-24h/generators/<node>.py`). So: no absolute scratchpad paths in it.
   It takes `--data <dir>` (the dir holding q2/ q34/ q5/ q6/ health/ master/ base/ CORE-FINDINGS.md),
   `--kit <dir>` (dir holding reportkit.py) and `--out <file>`. Run it with
   `--data $S --kit $S/kit --out <your html path>`. Stdlib only, type hints, `python3 -I` clean,
   must pass `uv run --quiet --with ruff==0.17.0 ruff check <script>` (repo ruff.toml applies when run from
   the repo root: `cd <repo> && uv run ... ruff check <abs path>`).
3. **Light/dark:** the kit already follows the system theme (prefers-color-scheme) and data-theme. Do not
   hard-code colours anywhere (no hex in your HTML/SVG); use the kit tokens/classes.
4. **Every claim is grounded in a measurement** with its source in the dev details; claims will be
   adversarially re-checked against the raw logs after you finish, so do not soften or round into a
   different statement; mark inferences as "Vermutung".
5. Do not describe confidential gateway states the operator keeps out of public docs (ask when unsure).
