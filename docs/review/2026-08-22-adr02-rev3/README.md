# Review-Archiv: ADR 02 Rev. 3 (NC-Importance-Backoff)

> **Status: ABGESCHLOSSEN 2026-08-22.** Alle Befunde dieses Reviews sind in
> `docs/adr-nc-importance-backoff.md` Rev. 3 eingearbeitet (Commit `ab0160a5`),
> Advisor-Re-Verdict: APPROVED, 0 verbleibende Blocking-Defekte. Dieses
> Verzeichnis ist der Beleg- und Reproduzierbarkeits-Pfad, keine offene
> Arbeitsliste.

## Was hier liegt

| Datei                                                                 | Inhalt                                                                                                               |
| --------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| `adr-nc-importance-verdict.md`                                        | Konsolidierter Verdict (V1-V21 verifizierte Befunde, Refuted-Liste) — die Quelle der Rev.-3-Aenderungen              |
| `evidence-pack.md`                                                    | Datenbasis, Methode, Feed-Semantik, Headline-Zahlen (Briefing der Review-Agenten)                                    |
| `heys_analysis.txt`, `deep_analysis.txt`                              | Aggregierte Auswertung der 24-h-Produktionsdaten (Verteilungen, Leaderboards, Importance-Simulation, Cap-Vergleiche) |
| `finder-*.md` (8)                                                     | Unabhaengige Finder: code-claims, math, evidence-fit, data-honesty, storm-starvation, rollout, protocol, validation  |
| `verifier-*.md` (3)                                                   | Adversariale Verifikation: T-Beam-Cap-Widerspruch, HEY-Prio/Sturm-Mechanik, Stufe-2-Logik                            |
| `advisor-rev3.md`                                                     | Advisor-Abnahme: erst REWORK (8 Defekte), nach Einarbeitung APPROVED                                                 |
| `mcp.py`, `harvest.py`                                                | Datenerhebung: JSON-RPC-Client fuer die mcmap-MCP-API, Cursor-Harvest aus `interlink.log`                            |
| `analyze_heys.py`, `analyze_deep.py`, `analyze_links.py`, `verify.py` | Auswertungs- und Nachrechen-Skripte                                                                                  |

## Was hier NICHT liegt (Rohdaten, nicht committet)

- `heys.jsonl` (16 MB, 134.576 Feed-Zeilen / 96.074 eindeutige HEY-Frames,
  Fenster 2026-08-21 14:12 bis 2026-08-22 14:20 UTC)
- `fleet.json` (1.430 Node-DB-Eintraege), `linkload24.json` / `linkload7d.json`

Regenerierung: mcmap-prod-MCP-Zugriff vorausgesetzt (`claude mcp list`),
`python3 harvest.py` fuer den HEY-Harvest (Cursor-basiert ueber
`logs_grep interlink`), `nodes_query`-Praefix-Pagination fuer die Flotte,
`link_load_overview` fuer die Segmentlast. Achtung Log-Retention: 20 Tage —
die exakten Rohdaten dieses Fensters sind danach nicht mehr abrufbar; die
aggregierten Ergebnisse stehen in den beiden `*_analysis.txt`.

## Methode (Kurzfassung)

fable-review: 8 Finder mit je einem Blickwinkel parallel → adversariale
Verifikation der tragenden Kandidaten (3 Verifier + deterministische
Nachrechnungen) → Verdict → Umsetzung als ADR Rev. 3 → unabhaengige
Advisor-Abnahme (REWORK-Runde mit 8 Defekten, dann APPROVED). Widerlegte
Behauptungen sind im Verdict unter "Refuted" dokumentiert, damit sie nicht
erneut untersucht werden.
