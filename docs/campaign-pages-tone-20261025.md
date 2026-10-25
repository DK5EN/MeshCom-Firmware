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

| Welle | Inhalt                                                                                                                                                | Status                        |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------- |
| 0     | Skill `schreibstil`, `konzeptpapier` angepasst, `assets/site.css`, Branch                                                                             | erledigt                      |
| 1     | A `meshcom-protocol.html`, B `mesh-roadmap.html`, C `main-loop-timing.html` + `Nachbarschaftsmatrix.html`, D `rm1-protokoll.html` + `pr-history.html` | erledigt (`d58af699`)         |
| 2     | E `index.html` + `404.html` (Karten fassen die fertigen Seiten zusammen)                                                                              | erledigt (`ef8a8e18`)         |
| 3     | Sichtpruefung Chrome Desktop hell; Handy und dunkel ueber die Playwright-PNGs der Agenten; Linkcheck                                                  | erledigt                      |
| 4     | Advisor 1 (Fakten gegen Code): 3 falsche Fakten, 3 Widersprueche, Stil -> Nacharbeit umgesetzt; Advisor 2 auf der Nacharbeit; PR nach `fork-dev`      | erledigt (Advisor 2 APPROVED) |

## Dateibesitz

| Agent        | Dateien                                                                                  | Welle |
| ------------ | ---------------------------------------------------------------------------------------- | ----- |
| A            | `docs/presentation/meshcom-protocol.html`                                                | 1     |
| B            | `docs/presentation/mesh-roadmap.html`                                                    | 1     |
| C            | `docs/presentation/main-loop-timing.html`, `docs/presentation/Nachbarschaftsmatrix.html` | 1     |
| D            | `docs/presentation/rm1-protokoll.html`, `docs/presentation/pr-history.html`              | 1     |
| E            | `docs/presentation/index.html`, `docs/presentation/404.html`                             | 2     |
| Orchestrator | `docs/presentation/assets/site.css`, dieses Dokument                                     | alle  |

## Befunde ausserhalb der Seiten (Welle 1, nicht angefasst)

- `docs/rm1-commands.md` nannte 25 Befehle; jetzt 26 mit `blockcall` (im PR behoben).
- `release-notes.md`, Abschnitt "Upstream": #1213, #1214, #1224, #1233 stehen inzwischen im
  offiziellen Tag `v4.40a.spserver`, nicht nur in `dev`.
- `docs/commands-store-node.md`: `--storeslots 1..100`, der Code erlaubt hoechstens 50
  (`src/msgstore_api.h:19`).
- `docs/BACKLOG.md` (SNF-GW: STOR-Freigabe erledigt) widerspricht `docs/RESUME.md` und
  `docs/fork-dev-vs-upstream-20261018.md` (Freigabe des Serverbetreibers noetig).

## Offen nach dem PR

- `tools/code_share/render_pages.py` passt nicht mehr zum neuen Markup von `pr-history.html`
  (Zeilen-Regex findet 0 PR-Zeilen: Datum jetzt `TT.MM.JJJJ`, keine `<section>`-Bloecke). Das
  Skript laeuft nur von Hand; vor dem naechsten Lauf an das neue Markup anpassen. Bis dahin ist
  der Zeilenanteil (git blame, 18.10.2026) auf der Seite als Altstand gekennzeichnet.

## Offen ausserhalb des PR

- Verweis auf `schreibstil` in `.claude/commands/release-firmware.md` des Haupt-Checkouts eintragen
  (aus dem Worktree heraus gesperrt).
