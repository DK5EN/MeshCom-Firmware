# Nachbarschaftsmatrix: Feldlauf DK5EN-98, Stand und Auswertung

> **ARCHIVED 2026-09-23.** Wiedereinstieg in Feldlauf 1 (24 h ab 2026-09-21 19:20). Der Lauf ist ausgewertet, siehe `nbr-feldlauf-20260922.md` daneben; die Rezepte stehen live in `../nachbarschaftsmatrix-campaign.md`.

Stand 2026-09-21 19:41. Der 24-Stunden-Mitschnitt laeuft. Dieses Dokument ist der Wiedereinstieg:
was wo liegt, wie ausgewertet wird und was beim Lesen des Berichts zu beachten ist.

## BLUF

- Branch **`feature-neighbour-matrix`**, drei neue Commits auf dem Merge von `fork-neo-test`.
- DK5EN-98 traegt seit **21.09. 19:16** die neue Firmware, der Mitschnitt laeuft seit **19:20:44**
  auf `rpizero.local` und endet am **22.09. gegen 19:20**.
- Ausgewertet wird mit **einem Befehl** (Abschnitt 4). `--since` ist dabei Pflicht.
- Waehrend des Laufs **nicht selbst auf TCP 2323 verbinden** -- die Konsole ist Ein-Client.

## 1. Branch und Commits

Repo `~/WebDev/MeshCom-Firmware-DEV-Main`, Branch `feature-neighbour-matrix`:

| Commit     | Inhalt                                                                           |
| ---------- | -------------------------------------------------------------------------------- |
| `0a2f6e9d` | Merge `fork-neo-test` (Byte-Ringe, Extern-UDP-Stack-Fix, MHeard-Drossel)         |
| `7065b587` | 2-Hop-Schnitt in der Matrix, `--nbrdebug`, Log-Emitter, 15-Minuten-Schnappschuss |
| `dc2d6e57` | Advisor-Nacharbeit: zweites Urteil `<meshneed>`, drei Fehlerbehebungen           |
| `a1cfbe2e` | Feldlauf-Rezept in `docs/nachbarschaftsmatrix-campaign.md`                       |

Der Branch ist **nicht** nach `fork-main` gemergt und **nicht** gepusht. Nichts davon ist bisher
Gegenstand eines Upstream-PR.

### Was fachlich geaendert wurde

- **2-Hop-Schnitt.** `nbrNoteFrame()` vergibt Zeilen nur noch aus den letzten zwei Pfad-Token.
  Eine Kante weiter vorn im Pfad wird zusaetzlich gezaehlt, wenn beide Endpunkte schon eine
  Zeile haben -- das kostet keine Zeile und ist per Konstruktion eine Kante zwischen Knoten
  hoechstens zwei Hops entfernt. `nbrNotePos()` legt gar keine Zeile mehr an; ueber mehrfach
  relayte POS-Frames kamen sonst weit entfernte Absender in die Tabelle.
- **Instrumentierung.** Neues, persistiertes `--nbrdebug on|off` (`node_sset4` Bit `0x0010`),
  getrennt von `--loradebug`. Neun Zeilentypen, Format in `docs/nbr-logformat.md`.
- **Zweites Urteil.** Siehe Abschnitt 5 -- der wichtigste Punkt beim Lesen des Berichts.
- **Drei Fehlerbehebungen** aus dem Advisor-Pass, darunter ein vorbestehender Datenfehler:
  `nbrPlanRow()` konnte bei voller Tabelle eine Zeile verlieren, weil ein neues und ein
  bestehendes Rufzeichen denselben Index belegten und der Treffer auf der Diagonale landete.

### Neue und geaenderte Dateien

| Datei                                                  | Rolle                                                         |
| ------------------------------------------------------ | ------------------------------------------------------------- |
| `src/nbr_matrix.{h,cpp}`                               | Matrix, 2-Hop-Fenster, beide Urteile, Log-Emitter             |
| `src/lora_functions.cpp`                               | Haken in `OnRxDone()`, `nbrLogToConsole()`, `nbrDebugApply()` |
| `src/command_functions.cpp`                            | Toggle-Zeilen `--nbrdebug`, `--info`-Statuszeile              |
| `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp` | Boot-Restore und 15-Minuten-Takt                              |
| `tools/nbrlog.py`                                      | Auswerter (neu)                                               |
| `tools/meshlogger.py`                                  | neue Option `--flags`                                         |
| `docs/nbr-logformat.md`                                | Vertrag Firmware <-> Parser (neu)                             |
| `docs/nachbarschaftsmatrix-campaign.md`                | Kampagnenstand                                                |

## 2. Was auf der Hardware laeuft

| Was             | Wert                                                                      |
| --------------- | ------------------------------------------------------------------------- |
| Knoten          | DK5EN-98, Heltec V3, `dk5en-98.local`                                     |
| Firmware vorher | `4.35t build Sep 20 2026 / 22:04:46`                                      |
| Firmware jetzt  | `4.35t build Sep 21 2026 / 19:16:08`                                      |
| Geflasht mit    | `python3 tools/webflash.py dk5en-98.local` (WiFi-OTA, 68 s)               |
| Settings        | unveraendert -- kein `FLASH_STRUCT_VERSION`-Wechsel, nichts ging verloren |

Firmware-Stand jederzeit pruefbar:

```
python3 tools/bench/netconsole_log.py dk5en-98.local /tmp/info.log 12 "--info"
grep -i "MeshCom 4\|NBRDEBUG" /tmp/info.log
```

Das geht aber **nur nach Ende des Mitschnitts** oder mit gesetzter PAUSE-Datei (Abschnitt 3).

## 3. Der Mitschnitt

Laeuft auf `rpizero.local` (Benutzer `martin`):

```
ssh rpizero.local
cd ~/meshlog && nohup python3 meshlogger.py dk5en-98.local \
    --hours 24 --flags nbrdebug --outdir /home/martin/meshlog/dk5en-98 \
    > /home/martin/meshlog/dk5en-98-nohup.out 2>&1 &
```

| Was                | Wert                                                       |
| ------------------ | ---------------------------------------------------------- |
| Start              | 2026-09-21 19:20:44                                        |
| Ende               | 2026-09-22 gegen 19:20                                     |
| Ablage             | `~/meshlog/dk5en-98/YYYY-MM-DD.log` (taegliche Rotation)   |
| Statusdatei        | `~/meshlog/dk5en-98/status.txt`, alle 60 s neu geschrieben |
| Erwartetes Volumen | rund 1 MB/h, 23 GB frei -- kein Platzproblem               |
| Alte Fassung       | `~/meshlog/meshlogger.py.pre-20260921`                     |

`--flags nbrdebug` setzt bewusst **nicht** `loradebug` und `txcapture` -- sonst ersaeuft der
Mitschnitt im RX-Capture. Der Logger stellt `--nbrdebug` beim Beenden auf `off` zurueck.

Laufkontrolle:

```
ssh rpizero.local 'cat ~/meshlog/dk5en-98/status.txt'
ssh rpizero.local 'grep -c "\[NBR\]|" ~/meshlog/dk5en-98/2026-09-22.log'
```

**Die Konsole auf TCP 2323 ist Ein-Client.** Eine zweite Verbindung wirft den Logger raus; er
kommt nach fuenf Sekunden zurueck und wirft dafuer den Stoerer raus. Wer waehrend des Laufs an
den Knoten muss:

```
ssh rpizero.local 'touch ~/meshlog/dk5en-98/PAUSE'     # Logger gibt die Konsole frei
ssh rpizero.local 'rm ~/meshlog/dk5en-98/PAUSE'        # und nimmt sie wieder
```

## 4. Auswertung -- ab 2026-09-22 19:20

Ein Befehl, holt die Logs selbst vom Pi:

```
cd ~/WebDev/MeshCom-Firmware-DEV-Main
python3 tools/nbrlog.py \
    --fetch martin@rpizero.local:~/meshlog/dk5en-98/ \
    --since 2026-09-21 \
    --out ~/Desktop/nbr-feldlauf-20260922.md \
    --json ~/Desktop/nbr-feldlauf-20260922.json
```

- **`--since 2026-09-21` ist Pflicht.** Ohne sie zieht der Parser alle Altlogs ab dem 25.08.
  mit herein und der Bericht wird unbrauchbar.
- `--fetch ... --dry-run` zeigt nur den `rsync`-Befehl, ohne etwas zu holen.
- Ohne `--fetch` frisst er lokale Dateien: `tools/nbrlog.py datei1.log datei2.log ...`,
  auch `.gz`.
- `--json` schreibt dieselben Auswertungen maschinenlesbar.
- `--self-test` prueft den Parser gegen die Fixtures unter `tools/testdata/nbr/`, ohne Netz.

Der Bericht wird automatisch durch `prettier` geschickt, damit die Tabellen ausgerichtet sind.

### Was im Bericht steht

| Abschnitt | Inhalt                                                                    |
| --------- | ------------------------------------------------------------------------- |
| BLUF      | Die Antwort zuerst, plus Warnungen (Tabellenueberlauf, `DROP\|FULL`)      |
| 1         | Rahmen: Zeitraum, Zeilen, verworfene, Reboots, Luecken im Mitschnitt      |
| 2         | Meine direkten Nachbarn, Typverteilung, RSSI, erste/letzte Sichtung       |
| 3         | Kreuzmatrix: wer hoert wen                                                |
| 4         | **Das Urteil** je Nachbar, mit Gegenprobe gegen die Firmware              |
| 4b        | Deckungsmenge und wechselseitig redundante Gruppen -- siehe Abschnitt 6   |
| 5         | Stabilitaet des Urteils ueber die Zeit: ab wann traegt die Aussage?       |
| 6         | Tabellendruck: belegte Zeilen, Verdraengungen, wer wie oft weichen musste |
| 7         | Wirkung des 2-Hop-Schnitts aus den `CUT`-Zeilen                           |
| 8         | Verworfene Frames nach Grund                                              |
| 9         | Positionen und Luftlinie                                                  |

## 5. Beim Lesen beachten: es gibt ZWEI Urteile

Das ist der wichtigste Punkt. Die Matrix fuehrt je Zeile zwei Urteile, die **entgegengesetzte
Fragen an derselben Kante** beantworten. Sie duerfen nicht miteinander verglichen werden.

| Feld         | Frage                                                 | Bedeutung                                   |
| ------------ | ----------------------------------------------------- | ------------------------------------------- |
| `<verdict>`  | Hoert ausser mir jemand diesen Knoten?                | Ob **ich** meshen muss (Konzept 4.3)        |
| `<meshneed>` | Hoert dieser Nachbar Knoten, die sonst niemand hoert? | Ob **er** meshen muss -- die Betreiberfrage |

Der Bericht vergleicht sein eigenes Urteil nur gegen `<meshneed>`. `<verdict>` steht als reine
Info-Spalte daneben. Dass die beiden auseinandergehen, ist normal und kein Fehler -- im ersten
Schnappschuss aus dem Feld ist DL2JA-2 `RED`/`MESH` und DL2UD-1 `EXCL`/`RED`.

Eine `<meshneed>`-Angabe kann fehlen (`(nicht in Tabelle)`). Das ist **kein Widerspruch**,
sondern heisst, dass es nichts zu vergleichen gab -- der Bericht fuehrt diese Faelle in einer
eigenen Tabelle "Nicht vergleichbar". Steht ein Nachbar dort, weil er zum Snapshot-Zeitpunkt
gar nicht in der Matrix war, ist das Tabellendruck und gehoert zu Abschnitt 6.

## 6. Und: paarweise Redundanz ist keine Abschaltempfehlung

Abschnitt 4 beantwortet "ist **dieser eine** Nachbar verzichtbar, wenn alle anderen bleiben?".
Hoeren zwei Nachbarn exakt dieselbe, sonst von niemandem gehoerte Menge, gilt in dieser Rechnung
jeder fuer sich als redundant -- schaltet man **beide** ab, fehlt die Menge.

Abschnitt 4b macht das explizit: eine Greedy-Deckungsmenge (obere Schranke, nicht beweisbar
minimal -- Set Cover ist NP-schwer) und eine Tabelle der wechselseitig redundanten Gruppen mit
dem Knoten, der die Abhaengigkeit erzeugt. Diese Tabelle ist eine Warnung, kein Freibrief.

Ausserdem: ein Tabellenueberlauf (`rows == maxrows`, `DROP|FULL`) macht das Urteil aus
Abschnitt 4 fuer diese Zeitpunkte **nicht haltbar**, weil die Matrix dann nicht mehr alle
2-Hop-Nachbarn hielt. Der Bericht sagt das im BLUF.

## 7. Wenn etwas schiefgeht

| Symptom                               | Ursache und Abhilfe                                                                |
| ------------------------------------- | ---------------------------------------------------------------------------------- |
| `status.txt` altert, `status=running` | Prozess haengt: `ssh rpizero.local 'ps -p <pid>'`, sonst neu starten (Abschnitt 3) |
| `reconnects` zaehlt hoch              | Knoten oder WLAN wackelt; der Logger faengt das ab, die Luecken stehen im Bericht  |
| Keine `[NBR]`-Zeilen im Log           | `--nbrdebug` steht auf `off`: `--info` pruefen (nur mit PAUSE, Abschnitt 3)        |
| Bericht enthaelt Daten ab August      | `--since` vergessen                                                                |
| Alle Nachbarn "nicht vergleichbar"    | Kein Snapshot im Zeitraum -- der erste kommt 15 min nach dem Einschalten des Flags |

## 8. Offen

- Der Feldlauf selbst: Auswertung steht noch aus.
- Ob `NBR_MAX_ROWS` (21 auf dem S3) nach dem 2-Hop-Schnitt reicht, beantwortet erst Abschnitt 6
  des Berichts. Falls die Tabelle weiter ueberlaeuft, ist das der naechste Hebel.
- Nichts davon ist upstream vorgeschlagen. Ein PR gegen `icssw-org` DEV waere ein eigener
  Schritt mit eigener, deutscher PR-Beschreibung.
- `t5_epaper` und `esp32-external-radio` wurden nicht gebaut -- die sind schon am
  unveraenderten HEAD rot.
