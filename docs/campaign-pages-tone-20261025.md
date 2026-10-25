# Kampagne: GitHub Pages im Ton der Release Notes (25.10.2026)

**Alle sieben Seiten unter `docs/presentation/` werden neu geschnitten: BLUF zuerst, ein Fakt pro
Bullet, Leser in Schichten (OMs oben, Entwickler eingeklappt), Fakten gegen `fork-dev` geprueft,
ein gemeinsames Aussehen ueber `assets/site.css`.** Ergebnis ist ein PR `docs/pages-tone` nach
`fork-dev`. `pages-sync --push` ist nicht Teil der Kampagne.

## Vorgaben des Betreibers

- Alles deutsch, auch die PR-Beschreibung. Datum im Text `25.10.2026`.
- Kompletter Neuschnitt, alle Seiten, alles in einem Zug (kein Pilot).
- Leser: OMs und Entwickler, geschichtet.
- Fakten gegen den Code pruefen; veraltete Aussagen korrigieren und im PR nennen.
- Diagramme duerfen frei neu gestaltet werden; Sichtpruefung im Browser.
- Aussehen vereinheitlichen.
- Stilregeln: privater Skill `~/.claude/skills/schreibstil/` (nicht im Repo).

## Wellen

| Welle | Inhalt                                                                                                                                                | Status   |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| 0     | Skill `schreibstil`, `konzeptpapier` angepasst, `assets/site.css`, Branch                                                                             | erledigt |
| 1     | A `meshcom-protocol.html`, B `mesh-roadmap.html`, C `main-loop-timing.html` + `Nachbarschaftsmatrix.html`, D `rm1-protokoll.html` + `pr-history.html` | offen    |
| 2     | E `index.html` + `404.html` (Karten fassen die fertigen Seiten zusammen)                                                                              | offen    |
| 3     | Sichtpruefung Chrome (hell/dunkel, Desktop/Handy), Deduktion, Linkcheck                                                                               | offen    |
| 4     | Advisor (Fakten gegen Code), Nacharbeit, Commit, PR nach `fork-dev`                                                                                   | offen    |

## Dateibesitz

| Agent        | Dateien                                                                                  | Welle |
| ------------ | ---------------------------------------------------------------------------------------- | ----- |
| A            | `docs/presentation/meshcom-protocol.html`                                                | 1     |
| B            | `docs/presentation/mesh-roadmap.html`                                                    | 1     |
| C            | `docs/presentation/main-loop-timing.html`, `docs/presentation/Nachbarschaftsmatrix.html` | 1     |
| D            | `docs/presentation/rm1-protokoll.html`, `docs/presentation/pr-history.html`              | 1     |
| E            | `docs/presentation/index.html`, `docs/presentation/404.html`                             | 2     |
| Orchestrator | `docs/presentation/assets/site.css`, dieses Dokument                                     | alle  |

## Offen ausserhalb des PR

- Verweis auf `schreibstil` in `.claude/commands/release-firmware.md` des Haupt-Checkouts eintragen
  (aus dem Worktree heraus gesperrt).
