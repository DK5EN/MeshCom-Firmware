# Nachbarschaftsmatrix: Kampagnenstand

Konzept: `~/Desktop/Nachbarschaftsmatrix.html` (2026-09-20). Branch `feature-neighbour-matrix`
ab `5ed69ee2` (fork-neo-test). Stufe 2 (Gateway-ACK-Zuordnung) ist zurueckgestellt.

## Wellen

| Welle | Inhalt                                                             | Dateien                                                           | Stand                                                                          |
| ----- | ------------------------------------------------------------------ | ----------------------------------------------------------------- | ------------------------------------------------------------------------------ |
| Vorab | `NBR_MAX_ROWS` je Board, env `native_nbr_matrix`                   | `src/configuration_global.h`, `platformio.ini`                    | erledigt                                                                       |
| W1    | Modul Arduino-frei, Host-Test                                      | `src/nbr_matrix.h`, `src/nbr_matrix.cpp`, `test/test_nbr_matrix/` | Nacharbeit (M1, M2, L2, L4) laeuft, siehe nachbarschaftsmatrix-verdict.md      |
| W2a   | Haken in OnRxDone, POS-Uebergabe, `--neighbours`, `--nbrreset`     | `src/lora_functions.cpp`, `src/command_functions.cpp`             | erledigt, L1 gefixt, Heltec-Image 17:36 auf DK5EN-93                           |
| W2b   | Web-Seite `?page=neighbours`, Menueknopf                           | `src/web_functions/web_functions.cpp`                             | erledigt, Tabellen-CSS-Fix, in Chrome gesehen                                  |
| Gate  | Host-Test, Build aller Board-Envs, Heltec DK5EN-93 flashen, Chrome | Orchestrator                                                      | Builds heltec/rak/tbeam/tdeck gruen, Flash + Chrome ok, Commit nach Nacharbeit |
| Feld  | 12 h auf DK5EN-98                                                  | Betreiber                                                         | offen                                                                          |

## Entscheidungen

- Zelle (X,Y) = "Y hat X gehoert", Zeile 0 = eigener Knoten. Haken VOR der Duplikatpruefung.
- 12 h = 720 min, 16-Bit-Minute seit Boot, Verfall beim Treffer, keine Persistenz.
- N = 21 (S3, RAK4631, XML-Varianten), 13 (klassischer ESP32), 11 (ENABLE_TBEAM).
- Gateway-ACKs bleiben in Stufe 1 anonym.

## Kampagne 2 (2026-09-21): 2-Hop-Schnitt, Instrumentierung, 24-h-Dauertest

Ausloeser: Im Feld tauchten Knoten in 3 und 4 Hop Entfernung in der Matrix auf. Gebraucht wird
nur die HELLO-Aussage "welche Nachbarn haben meine direkt gehoerten Nachbarn" -- daraus faellt
das Urteil, ob ein Nachbar selbst meshen muss (exklusive Nachbarn) oder ob sein Meshen redundant
ist, weil ein anderer meiner Nachbarn dieselbe Menge abdeckt.

| Welle | Inhalt                                                                                              | Dateien                                                                                                                                                                    | Stand                 |
| ----- | --------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------- |
| W0    | Merge `fork-neo-test` (Byte-Ringe, Extern-UDP-Stack, MHeard-Drossel) in den Feature-Zweig           | `src/configuration_global.h` (Konflikt: Byte-Ring-Konstanten plus `NBR_MAX_ROWS`)                                                                                          | erledigt              |
| W1a   | 2-Hop-Schnitt in `nbrNoteFrame()`, `nbrNotePos()` legt keine Zeile mehr an, Log-Emitter, Host-Tests | `src/nbr_matrix.h`, `src/nbr_matrix.cpp`, `test/test_nbr_matrix/`                                                                                                          | laeuft                |
| W1b   | Logparser, `meshlogger.py --flags`                                                                  | `tools/nbrlog.py`, `tools/meshlogger.py`, `tools/testdata/nbr/`                                                                                                            | laeuft                |
| W2    | `--nbrdebug on/off` (node_sset4), Flagdefinition, Boot-Restore, 15-min-Takt fuer `nbrLogSnapshot()` | `src/command_functions.cpp`, `src/loop_functions*.{cpp,h}`, `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`, `src/lora_functions.cpp`, `test/test_command_toggles/` | erledigt (`7065b587`) |
| Gate  | Host-Tests, Board-Builds, String-Scan des Images, Advisor-Pass                                      | Orchestrator                                                                                                                                                               | offen                 |
| Feld  | OTA auf DK5EN-98, 24-h-Mitschnitt auf rpizero, Auswertung mit `tools/nbrlog.py`                     | Orchestrator + Betreiber                                                                                                                                                   | offen                 |

### Entscheidungen dieser Kampagne

- **2-Hop-Fenster**: Zeilen entstehen nur noch aus den letzten zwei Pfad-Token. Eine Kante weiter
  vorn im Pfad wird zusaetzlich eingetragen, wenn BEIDE Endpunkte schon eine Zeile haben -- das
  kostet keine Zeile und ist per Konstruktion eine Kante zwischen Knoten <= 2 Hops.
- **`nbrNotePos()` legt keine Zeile mehr an.** Mehrfach relayte POS-Frames brachten sonst Knoten
  in 3+ Hop Entfernung in die Tabelle.
- **Trennzeichen im Log ist `|`, nicht `;`** -- `printfdeb()` verschluckt Semikolons ausserhalb
  von `--debug csv` (`src/printfdeb_format.h`). Format: `docs/nbr-logformat.md`.
- **Eigenes Flag `--nbrdebug`**, nicht an `bLORADEBUG` gehaengt: der 24-h-Mitschnitt soll nicht im
  RX-Mitschnitt ersaufen. `meshlogger.py` wird darum mit `--flags nbrdebug` gefahren.
- **Mitschnitt**: `martin@rpizero.local`, `tools/meshlogger.py dk5en-98.local --hours 24`,
  Ablage `~/meshlog/dk5en-98/YYYY-MM-DD.log`, rund 1,2 MB/h, 23 GB frei. Die Konsole auf TCP 2323
  ist Ein-Client -- waehrend des Laufs nicht selbst verbinden.
- **Flashen**: WiFi-OTA mit `tools/webflash.py` (Standardziel ist dk5en-98.local / Heltec V3).
  `ota_0` ist der einzige App-Slot, ein Abbruch zerstoert die App.

### Nachtrag W1a (erledigt)

- `nbrTouchRow()` entfernt -- nach dem Umbau von `nbrNotePos()` hatte es keinen Aufrufer mehr.
- `DROP FULL` ist bei `NBR_MAX_ROWS >= 3` praktisch unerreichbar geworden: das Fenster braucht nie
  mehr als zwei neue Zeilen. Der Pfad bleibt als Sicherung stehen.
- Das GW-Flag aus einem HEY-Frame wird nur noch gesetzt, wenn der Absender (erstes Pfad-Token)
  eine aufgeloeste Zeile hat. Bei einem Absender ausserhalb des Fensters verfaellt es still --
  notwendige Folge des Schnitts, der Knoten steht ohnehin nicht mehr in der Matrix.
- `bNBRDEBUG` belegt `node_sset4` Bit `0x0010` (0x0001..0x0008 sind vergeben, alles darueber frei). Seit 2026-09-25 `0x0400`: 0x0010..0x0080 gehoeren upstream KISS/TCP, siehe `docs/nbr-stage2-campaign.md` "Bit layout".

### Feldlauf 2026-09-21/22 -- Stand und Rezept

- **Geflasht** 2026-09-21 19:16 per `python3 tools/webflash.py dk5en-98.local` (WiFi-OTA, Heltec V3).
  Vorher `4.35t build Sep 20 22:04:46`, nachher `4.35t build Sep 21 19:16:08`. Kein
  Settings-Layout geaendert, der Knoten hat seine Konfiguration behalten.
- **Mitschnitt** laeuft auf `rpizero.local` seit 19:20:44, PID aus
  `~/meshlog/dk5en-98/status.txt`:

  ```
  ssh rpizero.local
  cd ~/meshlog && nohup python3 meshlogger.py dk5en-98.local \
      --hours 24 --flags nbrdebug --outdir /home/martin/meshlog/dk5en-98 \
      > /home/martin/meshlog/dk5en-98-nohup.out 2>&1 &
  ```

  Die alte Fassung liegt als `meshlogger.py.pre-20260921` daneben. `--flags nbrdebug` setzt
  bewusst NICHT `loradebug`/`txcapture` -- sonst ersaeuft der Mitschnitt im RX-Capture.
  Der Logger stellt `--nbrdebug` beim Beenden auf den Vorzustand (`off`) zurueck.

- **Die Konsole auf TCP 2323 ist Ein-Client.** Waehrend der 24 Stunden nicht selbst verbinden --
  eine zweite Verbindung wirft den Logger raus, er kommt nach fuenf Sekunden zurueck und wirft
  dafuer den Stoerer raus. Zum Pausieren `touch ~/meshlog/dk5en-98/PAUSE` auf dem Pi.
- **Auswertung** ab 2026-09-22 19:20:

  ```
  python3 tools/nbrlog.py --fetch martin@rpizero.local:~/meshlog/dk5en-98/ \
      --since 2026-09-21 --out ~/Desktop/nbr-feldlauf-20260922.md --json ~/Desktop/nbr-feldlauf-20260922.json
  ```

  `--since` ist Pflicht: ohne sie zieht der Parser alle Altlogs ab dem 2026-08-25 mit herein.

- **Erster Schnappschuss** kommt 15 Minuten nach dem Einschalten des Flags, danach alle 15 min.
  Vorher hat der Bericht keine `<meshneed>`-Spalte zum Vergleichen -- das ist kein Fehler.
