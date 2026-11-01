> Template as used for the 2026-11-01 run (docs/log-reports/2026-11-01-24h). Before reuse, replace the run-specific
> paths (scratchpad, run dir), node names, capture spans and facts; keep the rules and bans.

# Common brief - MeshCom 24-h log analysis 2026-11-01 (wave 1)

Repo (read-only for you): <repo> (branch fork-dev, HEAD 651e0920)
Scratchpad root ($S): <scratch>

## Inputs

- `$S/norm/oe3xhl.log oe3xia.log oe3xir.log oe3xoc.log oe3xwj.log` - five Austrian (OE3) nodes,
  logged in parallel 2026-10-31 ~06:30 -> 2026-11-01 ~07:50 local (OE3XIR stops 2026-10-31 18:16).
  Line prefix = logger wall clock `YYYY-MM-DD HH:MM:SS.mmm  `, then the firmware line. The `[LOG]`
  lines carry the node's own clock `HH:MM:SS` (may differ from the logger clock, e.g. OE3XIR +1 h -
  report such offsets, do not "fix" them silently). ~20-50 MB each: NEVER read whole; grep/stream.
- `$S/norm/db0ais.log` - DB0AIS-12 (RAK4631 + Ethernet, Germany), PuTTY capture 2026-10-30 17:01 ->
  2026-10-31 18:09, CR stripped. No wall-clock prefix; only `[LOG]`/MH-LoRa lines carry `HH:MM:SS`.
  Different mesh and day: never correlate it with the OE3 cluster. Use `LC_ALL=C grep -a`.
- Flags active on all: `--setlog on` (+ RSSI/SNR/DUP/OWN tail) and `--loradebug on`, `[NBR]` lines.
- Firmware 4.40a on all, exact build UNKNOWN (no boot block). Infer from behaviour where it matters
  (e.g. TXP-01 fix 556b997d: own text never released while READY) and mark it "inferred".
- Wave-0 outputs (may still be in progress when you start; do not depend on them):
  `$S/base/analysis_<node>.txt` (tools/loganalyse.sh), `$S/base/berg/berg.json|berg.md` (tools/berglog.py, OE3 only).
- Callsign lists: `$S/master/calls_<node>.txt`; master data will land in `$S/master/master.tsv`.

## Grammar and code references (read by line range, grep first)

- `[LOG]` line formats SL-01..06: `~/.claude/skills/logauswertung/references/setlog-lines.md`
  (authoritative: `src/setlog_lines.h`). Multi-node correlation: `.../references/berglog-captures.md`.
- TX pool (TXP campaign): `docs/campaign-tx-pool.md`, `src/txring_functions.cpp` (RING_WRITE ~, RING_DROP_STALE :639,
  RING_SUPERSEDE :1068, RING_SUPERSEDE_KEEP :1033, RING_DROP_PRIO/NEW :1072/:1076), `src/txring_ttl.h`.
  MAX_RING=20. TTLs (`src/configuration_global.h:465-480`): HEY prio5 180 s any status; DONE-only:
  ACK prio1 90 s, relay prio3 300 s, position prio4 120 s; prio2 and own READY/SENT texts never age.
  {CET} gate: `src/cet_age.h` CET_MAX_AGE_S 150 -> RING_DROP_STALE reason=cet.
- CSMA age floor: `src/lora_functions.cpp:4030-4045`, `src/csma_timing.h:71-110`,
  constants `src/configuration_global.h:405-411` (stage1 20 s, stage2 60 s).
- NBR markers: `src/nbr_matrix.cpp` (EDGE :282, ME :294, SYM :305, CUT :261, DROP :252, ECHO :366, GW :351,
  POS :2378, RPT :2809), `src/lora_functions.cpp` (REFUSE :1380, `[NBR]|%s|...` :1473 = CANCEL/CHECK family,
  NEED :2641). Read the emit site before interpreting any marker.

## Rules (from the logauswertung skill - mandatory)

- Count events, not lines. Exclude `[LOG]` frames with `FCS:0000` (decoder bailout) from message stats.
  `[LOG]` receptions are printed BEFORE dedup/own-TX filtering: not accepted packets.
- A MessageID is unique only for ~4 h: never report "ID reuse" across >4 h gaps; key cross-node
  matching on (msg_id, time window <= 30 min).
- An unsent ring entry is not a loss until matched against `[NBR]|CANCEL` (overhear-cancel) and
  supersede/TTL markers. A dropped relay is only a LOSS if no other node delivered that msg_id.
- RSSI/SNR only means something for the last hop (direct neighbour).
- Heartbeat/HEY (prio 5, type 40) drops are the priority system working, not a fault.
- `[MC-STAT] qmax` is a since-boot latch. Percent tables must sum to 100 % +-0.5.
- OE1XAR-33 / OE1XAR-62 are the server time signal ({CET}), not nodes (OE vs DL server injection).
- Never print a bare HW number (map via `src/configuration_global.h` `//Hardware Types`).

## Hard bans

- Write ONLY inside your own output directory named in your brief. Never create/modify/delete
  anything in the repo or another agent's directory. No git commands at all.
- No pio, no serial ports, no network/MCP writes, no transmissions, no publishing.
- No `sleep` polling; long scripts run in background and you wait for the notification.
- Locate before reading: `grep -n` then a line range; no unbounded reads of files > 400 lines.
- Scripts: python3 stdlib, type hints, kept in `<your dir>/scripts/` so the result is reproducible.
  Run python with `-I`.

## Deliverables (in your directory)

1. `results.json` - machine-readable numbers: `{"nodes": {"<node>": {...}}, "network": {...}}`.
2. `findings.md` - English, terse: one section per sub-question, each finding = claim + number +
   evidence (log name + logger timestamp or line number, or source file:line) + verdict
   (OK / Beobachten / Handlungsbedarf) + positive/negative/neutral where asked. Separate "fact" from
   "inference". List suspected firmware bugs explicitly with the code site you read.
3. Final reply to the orchestrator: at most 15 lines - files written, top 5 findings with numbers,
   open questions, caveats. No narrative.
