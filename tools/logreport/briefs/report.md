> Template as used for the 2026-11-01 run (docs/log-reports/2026-11-01-24h). Before reuse, replace the run-specific
> paths (scratchpad, run dir), node names, capture spans and facts; keep the rules and bans.

# Wave 2 brief - per-node HTML report (German)

Read first: `$S/BRIEF-COMMON.md` (rules + hard bans still apply). S = scratchpad root named there.
Output dir (gitignored, outside git tracking): `<repo>/logs/20261101-24h-log/reports/`
You write exactly ONE file there, named in your dispatch (`<node>.html`), plus your generator script
in `$S/rep_<node>/` (your own dir). Nothing else. No git.

## Sources (read the findings files fully; results.json via jq/python, by key)

- `$S/health/findings.md|results.json` - baseline per node (identity, traffic, channel, noise, firmware census, DB0AIS ETH-04)
- `$S/q2/` TX slot pool, `$S/q34/` TTL/CET decay + CSMA age floor, `$S/q5/` time signal, `$S/q6/` manipulations
- `$S/master/master.tsv`, `distances.tsv`, `coverage.txt` - master data (hardware, firmware, location, altitude, distance)
- `$S/base/analysis_<node>.txt` (loganalyse.sh, read by section with grep/sed; ignore always-0 legacy sections),
  `$S/base/berg/berg.md` (OE3 cross-node)
- Raw log `$S/norm/<node>.log` only for spot checks (grep, never whole).
- OE3XIR-12 has no master data in mcmap (not in the database); an earlier analysis (2026-09-02) located it at the
  Hegerberg - write "keine Stammdaten (laut Auswertung 02.09.2026 Hegerberg)".

Every number you print must come from these files or from a check you ran; never invent or round into a
different claim. If sources disagree, say which and why in the methods section.

## Rendering

Use the shared kit read-only: `sys.path.insert(0, "$S/kit"); from reportkit import page, chip, table, bars, hist, timeline, esc`.
Do not copy or edit the kit; do not add external fonts/scripts/images. The page must be self-contained.
Charts: at most 6 per report, each caption `Abb. N: <Aussage>`. Tables >12 rows go in `<details>`.

## Language and tone (skill `~/.claude/skills/schreibstil/SKILL.md` - read it)

German with umlauts, dates `31.10.2026`. BLUF first (1-2 bold sentences), one fact per bullet with a bold
lead-in, plain words before internal IDs ("Relay-Kopie verworfen, weil zu alt (TTL)"), no emoji, no dash as
sentence substitute. Every code/marker explained once in a half-sentence for a ham who does not read C
(CAD, TTL, Supersede, Overhear-Cancel, Dedup-Ring, {CET}). Never a bare HW number. Every callsign named gets
hardware, firmware, location, altitude, distance (from master data or "keine Stammdaten"). RSSI/SNR only for
direct neighbours. Developer depth (code sites `src/file.cpp:line`, marker formats) in `<details>` blocks.

## Structure (the operator's questions are the spine)

1. eyebrow `MeshCom Log-Auswertung · <CALL>`; h1 = the statement for this node (not the topic);
   ul.meta: Log-Datei, Zeitraum (logger clock, note node clock offset), Hardware, Firmware (Build: inferred / unknown),
   Standort/Höhe, Rolle (Gateway / Relay), LoRa-Parameter, Stand 01.11.2026.
2. `.bluf` + `.callout.act` if there is Handlungsbedarf.
3. Antwort-Index: table, one row per question Q2-Q6 (+ Q0 Gesundheit): Kurzantwort + verdict chip (ok/watch/act).
4. Sections in order:
   - Gesundheit und Funkumfeld (baseline: traffic, neighbours, noise floor, channel util, CAD, crashes, heap; pre-CAD share)
   - F2 TX-Puffer mit Slots
   - F3 Was verlieren wir durch den Zeitverfall (TTL/{CET}-Gate; CSMA-Altersboden als Unterabschnitt)
   - F4 Was gewinnen wir durch den Zeitverfall
   - F5 Zeitsignal
   - F6 Manipulationen dieses Knotens: three columns / cards Positiv / Negativ / Neutral, each item with number + effect
   - Verdachtsfälle Firmware-Bugs (only real ones, each: Beobachtung, Beleg, Code-Stelle, Status "Verdacht"/"bestätigt")
   - Nachbarn und Hardware-Inventar (table: Call, Hardware, Firmware, Standort, Höhe, Distanz, RSSI/SNR wenn direkt)
   - Was nicht geprüft ist / Grenzen
   - Offene Fragen an den Betreiber
   - details.dev: Methodik, Marker, Code-Stellen, Skripte
   - .foot: Quellen, "Auswertung der Fork-Firmware DK5EN, nicht die offizielle Firmware"
     For this node, network-wide findings (from cross-node correlation) appear only where they concern
     this node; link the overview as `uebersicht.html` (relative) for the network view.
5. Percent tables must sum to 100 % +-0.5.

## Verification before you report

- `python3 -I` run of your generator succeeds; file exists; `grep -c '<script' <file>` = 0;
  `grep -cE 'https?://' <file>` - only plain links allowed, no external resources (`src=`/`@import`/`url(` = 0).
- `grep -oE 'HW:[0-9]+' <file>` empty outside dev details; no "TODO"/"XXX".
- Spot-check three numbers in your report against the raw log with grep and list them in your reply.

Final reply at most 12 lines: file written, size, verdicts per question, the three spot-checks, open issues.
