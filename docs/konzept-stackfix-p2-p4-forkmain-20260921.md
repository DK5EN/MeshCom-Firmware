# Kampagne: Stack-Fix P2–P4 auf fork-main + Bench-Nachweis

Stand 21.09.2026. Plan zur Freigabe, noch nichts umgesetzt. Umsetzung per
`/orchestrate-waves`.

> **Nachtrag 21.09.2026 — abgearbeitet.** Welle 1 (P2 + `stack_budget.py`) in
> `efc9681e`, Welle 2 (RCA, CHANGELOG 229-232, RESUME, BACKLOG) in `1b47d3e4`,
> dazu `36b3a895`. Welle 3 (Bank) in `8979c58e`, BACKLOG §3.8an: alle sieben
> Prüffälle PASS, darunter die Stack-Regression auf fork-main-Hardware. Der
> Text unten bleibt als Planungsstand stehen; offene Punkte stehen im BACKLOG
> (`MESH-01`, `MHD-01`, F4) und in `docs/bug-extudp-stack-20260920.md`.

## Ausgangslage (am Baum geprüft, nicht angenommen)

| Teil                                                    | neo-ram-reclaim                     | fork-main                                                                                |
| ------------------------------------------------------- | ----------------------------------- | ---------------------------------------------------------------------------------------- |
| **F1/P1** `ARDUINO_LOOP_STACK_SIZE=12288`               | 8990e94d                            | **schon da** (c754d01f, `[esp32]` + alle 6 eigenständigen Envs)                          |
| **P2** `c_json`/`c_tjson` → BSS auf ESP32               | ja                                  | **fehlt** — die `#ifdef ESP32`-Stack-Variante steht noch                                 |
| **P3** `tools/stack_budget.py` + `docs/stack-budget.md` | ja                                  | **fehlt**                                                                                |
| **P4** RCA-Doku + Changelog                             | `docs/bug-extudp-stack-20260920.md` | **fehlt**; Changelog endet bei Item 228 — es fehlen dort auch Byte-FIFO und `--mesh off` |

Kein reiner Cherry-Pick möglich: fork-main hat in `sendExtern()`
`EXTERN_MSG_JSON_BUF` statt `500` und zusätzlich den BP-07-Puffer `c_json[400]`
in der Notice-Funktion; die Zeilennummern im neo-Kommentar
(`loop_functions.cpp:4297/4903`) gelten dort nicht.

Zwei Entscheidungen des Betreibers sind eingearbeitet:

- **Kein Upstream-PR in dieser Kampagne.** Wird nach dem Bench separat
  entschieden. (Sachlage dafür: der Stack-Fix betrifft jeden ESP32 mit
  `--extudp on`, ist also upstream-fällig; `via_functions.cpp` dagegen ist
  fork-only und existiert upstream gar nicht.)
- **Die Hardware-Prüfung ist Teil der Kampagne**, als eigene, serialisierte
  Welle am Schluss.

## Arbeitsbaum

Orchestrator legt an:
`git worktree add <pfad>/wt-forkmain-<sha> fork-main`, **auf 45e411d4 gepinnt**
(ein automatisch gewählter Merge-Base war schon einmal 40 Commits alt),
`.claude/` hineinkopieren (in einem frischen Worktree fehlen die gitignorierten
Skill-Dateien). Der Hauptcheckout bleibt auf `neo-ram-reclaim` als Quelle — für
die Agenten **lesend, nie schreibend**.

## Ressourcen-Besitz

- **Eine einzige pio-Instanz global**, der Paket-Store ist geteilt. Den Slot
  bekommt in Welle 1 nur A1 (`pio test -e native_extern`). Alle Board-Builds und
  die volle Suite laufen sequentiell im Gate beim Orchestrator.
- **Hardware und serielle Ports gehören ausschließlich Welle 3**, in keiner
  Schreib-Welle.
- Die vorhandenen neo-ELFs in `.pio/build/*/firmware.elf` (20.09., 22:0x) dienen
  A2 nur als Lesekopie zur Werkzeugprüfung.

## Welle 1 — Code + Werkzeug (2 × `implementer`, Sonnet/high, parallel)

### A1 · P2 — exklusiv `src/extudp_functions.cpp`

- `c_json`/`c_tjson` in `sendExtern()` auch auf ESP32 nach `static` BSS, das
  `#ifdef ESP32` auflösen.
- Reentranz auf **fork-main** neu belegen: alle Aufrufer selbst aufzählen und
  Zeilen zitieren, nicht den neo-Kommentar abschreiben.
- Ausdrücklich zu entscheiden und zu begründen: ob der BP-07-Puffer
  `c_json[400]` in der Notice-Funktion mitgeht oder bewusst auf dem Stack
  bleibt.
- Prüfung: `pio test -e native_extern` (der einzige pio-Aufruf der Welle), plus
  `grep` auf die geänderte Stelle. Keine Board-Builds.

### A2 · P3 — exklusiv `tools/stack_budget.py`, `docs/stack-budget.md`

- Portieren; die Doku auf die fork-main-Wirklichkeit ziehen (Envs,
  Root-Symbole, Budget 12288 − 512 Reserve).
- Prüfung ohne pio: das Werkzeug gegen die vorhandenen ELFs laufen lassen,
  Zyklus-Fall und unauflösbares Root-Symbol je einmal provozieren — beide müssen
  FAIL bzw. harter Fehler sein, nie ein stilles 0.

Beide Briefs enthalten: kein git, keine Formatter über den ganzen Baum, keine
Datei außerhalb des eigenen Satzes anlegen oder löschen, Befund außerhalb des
Satzes melden statt reparieren, knapper strukturierter Bericht.

### Gate 1 (Orchestrator), in dieser Reihenfolge

1. `ls` auf alle erwarteten Dateien, dann `git diff` selbst lesen.
2. `stack_budget.py` gegen **fork-main HEAD vorher** und **nachher** auf
   Heltec V3 → die Differenz ist der Beleg für P2 (neo: −1008 B; die
   fork-main-Zahl wird gemessen, nicht übernommen).
3. Sechs Boards sequentiell sauber bauen: Heltec V3, RAK4631, T-Beam, Wireless
   Paper, T-Deck Pro, Vision Master E213-Preview.
4. Volle Host-Suite über alle `native*`-Envs.
5. **Advisor-Pass** (`/fable-review` auf dem Wellendiff) — P2 ist eine
   Verhaltensänderung (statische Puffer), das ist nicht optional.
6. Ein Commit auf `fork-main`, explizite Pfade.

## Welle 2 — Doku (2 × `implementer`, parallel; erst nach Gate 1, weil die Zahlen von dort kommen)

### B1 · exklusiv `docs/bug-extudp-stack-20260920.md`

RCA auf fork-main-Stand: Messwerte aus Gate 1, Feldbeweis DK5EN-98 übernehmen,
Abschnitt „Offen" um F4 (`queueExtern`) und die beiden nicht berührten Altlasten
führen (`reclaim_eval.py` erkennt Neustarts nur an STAT-Zeilen;
`resetExternUDP()` hält über `startExternUDP()` ein `delay(2000)`).

### B2 · exklusiv `docs/CHANGELOG-stability.md`, `docs/RESUME.md`, `docs/BACKLOG.md`

Der Changelog endet bei Item 228; **drei** Items kommen hinzu, nicht nur eines
(entschieden am 21.09.2026):

- **229 — Stack-Fix.** F1 (`ARDUINO_LOOP_STACK_SIZE=12288`, c754d01f) ist noch
  gar nicht eingetragen und kommt mit P2 in **ein** gemeinsames Item: der
  Befund ist einer, die Aufteilung in F1/F2 ist eine Implementierungsfrage.
  Inhalt: ein Knoten mit `--extudp on` startete deterministisch neu, sobald ein
  `{"type":"msg"}`-Datagramm mit fremdem Ziel ankam.
- **230 — Byte-FIFO-Ausgangsringe** (9530967d, mit e2861458). Telefon-Daten,
  Telefon-Kommandos und UDP-Ausgang liegen als Byte-Ringe statt als
  Schlitzfelder. Anlass war ein harter Blocker: `E22_XML-DevKitC` linkte seit
  731e0ebc nicht mehr (dram0_0_seg 1 008 B über), das Board steht in
  `default_envs`. Zahlen aus der Commit-Nachricht übernehmen
  (E22_XML → 115 108 B, 92,40 %; ttgo_tbeam → 105 972 B). Von außen unsichtbar,
  gehört **nicht** unter „What changes on the air".
- **231 — `--mesh off` galt auf Via-Pfaden nicht** (45e411d4), plus der
  `indexOf`-Präfixtreffer. Das **ist** eine Verhaltensänderung auf der Luft:
  ein Knoten mit `--mesh off` hat bisher auf Via-Pfaden weiter relayed. Also
  zusätzlich ein Absatz unter **„What changes on the air"**, und der offene
  Rest dazu (Gateway-DM-ACK und DM-Store-Custody senden weiter) bleibt als
  Betreiberentscheidung im BACKLOG stehen, nicht im Changelog.

Dazu Statusboxen **und** Wellentabellen in RESUME/BACKLOG — jede Item-ID vorher
quer greppen, damit kein zweiter Ort stehenbleibt.

### Gate 2

`npx --yes prettier@3 --write` auf die drei `.md`, Diff lesen, Commit.
Docs-only → kein Advisor.

## Welle 3 — Bank, ein Agent, alles serialisiert (Orchestrator führt selbst)

Ziel: die beiden auf fork-main-Hardware ungeprüften Stücke — UDP-Ausgangsring
(Byte-FIFO) und der eigene nRF52-Drain in `nrf52_main.cpp` — plus der
Regressionsnachweis für den Stack-Fix.

1. RAK4631 (`/dev/cu.usbmodem2101`) mit dem fork-main-Bild per UF2, **als
   Gateway mit W5100S-Ethernet** — der einzige Prüfstand, auf dem der
   nRF52-Drain überhaupt läuft. IP aus dem Bootlog (`Ethernet.localIP():`), kein
   mDNS auf nRF52.
2. ESP32-Gegenstelle (Heltec DK5EN-93) mit demselben Stand.
3. Drain-Prüfung: Verkehr über den Ausgangsring treiben, `extudp_peer.py` /
   `udp_inject.py` auf 1799, Frames Byte für Byte gegen die Erwartung; besonders
   der Verdrängungsfall während des langsamen Sendens (peek/tail_gen/pop).
4. Stack-Regression: `{"type":"msg"}` mit **fremdem Ziel** über Extern-UDP an
   den ESP32 mit `--extudp on` → kein Reset, Nachricht geht raus.
   Vorher/Nachher-Gegenprobe wie am 20.09.
5. Randbedingungen, die gelten: nur Gruppe 9/9999 oder Direktkontakt, **nie**
   `*`; nie ein fremdes Rufzeichen als Quelle; `--mesh off`/`--gateway` bewusst
   setzen; vor dem Capture `backup_nodes.py --restore`.

Das Ergebnis geht in die Welle-2-Dokumente zurück (Nachtrag-Commit), der Status
wird ausdrücklich „bench-bewiesen" oder „weiterhin offen" — nicht dazwischen.

## Was dieser Plan nicht tut

- Kein Upstream-PR.
- Kein F4 (`sendMessage()` → `queueExtern()`): das nähme rund 4 kB aus der
  tiefsten Kette, bricht aber auf nRF52 die Ein-Erzeuger-Zusicherung der
  Warteschlange, weil `OnRxDone` dort im LORA-Task läuft. Bleibt verschoben und
  dokumentiert.
- Kein Merge von `neo-ram-reclaim` nach `fork-main` — die Zweige sind weit
  auseinander, portiert wird inhaltlich.
- `tools/reclaim_eval.py` bleibt unangetastet; steht als offener Punkt in der
  RCA.
