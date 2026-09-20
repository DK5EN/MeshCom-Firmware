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
