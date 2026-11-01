> Template as used for the 2026-11-01 run (docs/log-reports/2026-11-01-24h). Before reuse, replace the run-specific
> paths (scratchpad, run dir), node names, capture spans and facts; keep the rules and bans.

# Adversarial verification brief (wave 3a) - Opus verifier

Goal: the operator requires "everything grounded in measurements, claims adversarially checked". You are the
adversary. For EVERY factual claim in your findings file, try to REFUTE it against the raw logs and the source
code. Default stance: the claim is wrong until your own independent measurement reproduces it.

Read first: `$S/BRIEF-COMMON.md` (inputs, log grammar, rules, hard bans), `$S/CORE-FINDINGS.md`.
S = <scratch>
Repo = <repo> (read-only except your two output paths).

Method per claim:

1. Restate the claim (number, scope, node, time).
2. Measure it independently: your own grep/awk/python over `$S/norm/<node>.log` - do NOT reuse the analysis
   agent's scripts or intermediates (you may read them to find where it went wrong). Count events, not lines.
3. For claims about firmware behaviour or code sites: read the cited lines in the repo; check the line numbers
   are right and that the code does what is claimed (HEAD 651e0920; build in the field unknown - say if a claim
   depends on build).
4. Look for the alternative explanation (logging artefact, double-printed lines, serial interleave, clock
   offsets, ID reuse > 4 h, ACK vs frame lines, config difference instead of firmware bug, flag not enabled).
5. Verdict: CONFIRMED (your number, delta to claim) / CORRECTED (claim wrong in number or wording - give the
   correct statement) / REFUTED (give evidence) / UNVERIFIABLE (why, what would be needed). Inferences must be
   labelled as inference; if a claim presents an inference as fact, that is CORRECTED.

Output (the only files you write):

- `<repo>/docs/log-reports/2026-11-01-24h/verify/<topic>.md` - English, a table: # | claim | verdict |
  independent measurement (number + command summary) | correction. Then a short list "Top corrections that
  change a verdict or a recommendation". Keep it factual; no narrative.
- your check scripts in `<repo>/docs/log-reports/2026-11-01-24h/verify/<topic>_checks/` (stdlib python3 with
  type hints or shell; paths via argv, no absolute scratchpad paths inside the scripts; one line in a
  README.txt there saying how to run them: `python3 -I <script> <norm-dir>`).
  No git, no pio, no transmissions, no edits anywhere else, no sleep polling, no unbounded reads of the logs.
  If the Write tool refuses a file, use a Bash heredoc.
  Prioritise: claims that drive a Handlungsbedarf verdict or a firmware-change recommendation first, then
  headline numbers, then the rest. If time runs short, say which claims you did not check.
  Final reply at most 15 lines: counts per verdict, the corrections that change a verdict/recommendation.
