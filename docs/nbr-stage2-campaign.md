# Nachbarschaftsmatrix Stufe 2: Kampagnenstand

Konzept: `docs/nbr-wichtigkeit-konzept.md` (v3, 2026-09-22). Branch `feature-neighbour-matrix`.
Betreiberentscheidungen 2026-09-22: POS/HEY duerfen 20 s Nachrang bekommen; Fall A darf vor allen
anderen senden (Re-Arm-Kappung); der Server verteilt jeden Frame an jedes Gateway (Gateway-
Ausnahme ist drin); alles lokal umsetzen, committen, auf DK5EN-98 und den Bench-Heltec flashen,
Test weiterlaufen lassen. Zweites Blatt = Bench-Heltec am Laptop mit 2 dBm.

## Wellen

| Welle | Inhalt                                                                                                                                                                                                                                                      | Dateien                                                                                                                                  | Stand                                                                                                                                                                                                                                                                               |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0     | Vertrag: Konstanten, Globals, Matrix-API (`nbrRelayNeed`, `nbrCoverMask`, `nbrRowMeshNeedCount`, `nbrExclusiveDirect`), Echo-Fix (Spalte 0 nur per ME), Direktzeilen-Schutz, 5 Host-Tests                                                                   | `src/configuration_global.h`, `src/loop_functions.cpp`, `src/loop_functions_extern.h`, `src/nbr_matrix.{h,cpp}`, `test/test_nbr_matrix/` | erledigt, 33/33 gruen                                                                                                                                                                                                                                                               |
| 1B    | Relay-Entscheidung: Masken je Ring-Slot, NEED/CANCEL/REFUSE, Cancel-Zweig im RX, Backoff nach Fall                                                                                                                                                          | `src/lora_functions.cpp`, `src/txring_functions.{h,cpp}`, `test/test_txring/`                                                            | laeuft                                                                                                                                                                                                                                                                              |
| 1D    | Web-Seite 2a: vertikale Koepfe, `D/I G M #N #X Role`, Legende, Umbruch, Sortierung, `Covered by`, Block 6.3                                                                                                                                                 | `src/web_functions/web_functions.cpp`                                                                                                    | laeuft                                                                                                                                                                                                                                                                              |
| 1C    | `--nbrrelay off                                                                                                                                                                                                                                             | count                                                                                                                                    | on`, Boot-Restore, `--info`-Zeile, Toggle-Test; Logformat; Kampagnendoc                                                                                                                                                                                                             | `src/command_functions.cpp`, `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`, `test/test_command_toggles/`, `docs/` | Orchestrator, erledigt |
| Gate  | Host-Tests (native_nbr_matrix, native_aprs/test_txring, test_command_toggles), Builds heltec/rak/tbeam/tdeck sequenziell, String-Scan, Advisor (`/fable-review`), Commit                                                                                    | Orchestrator                                                                                                                             | erledigt: Host-Tests 34/21/120 gruen, vier Builds gruen (nRF52 brauchte `(unsigned)`-Casts), Advisor APPROVED (1 Medium behoben: kein Wissen ist nicht Fall B; 2 Low behoben: Text-Slots, Kommentar; 2 Low offen: Maskenindex bei Verdraengung, count-zu-on-Attribution), committed |
| 2     | Hardware: OTA DK5EN-98 (PAUSE-Datei, meshlogger mit `nbrdebug,loradebug,txcapture` neu starten, `--nbrrelay count`), Bench-Heltec seriell flashen, `--txpower 2 --gateway off --nbrdebug on --setlog on --nbrrelay count`, serielle Aufzeichnung des Blatts | Orchestrator                                                                                                                             | offen                                                                                                                                                                                                                                                                               |

## Entscheidungen

- pio-Slot: in Welle 1 ausschliesslich Agent B; D kompiliert nicht, das Gate kompiliert.
- Der Gateway-Echo-Fix (5.8 Punkt 0) ist in Welle 0 drin: ohne ihn stuende OE1XAR-33 in der
  Allein-Maske von DK5EN-98 und `count` saehe nie einen Abbruchkandidaten.
- `count` aendert kein Funkverhalten; `on` schaltet Abbruch UND Backoff nach Fall.
- Settings-Bits: `node_sset4` 0x0020 count, 0x0040 on.
- B musste die Deklaration von `addTxRingEntry()` in `src/loop_functions.h` und
  `src/loop_functions_extern.h` erweitern (drei Default-Parameter) -- ausserhalb seines Dateisatzes,
  gemeldet, vom Orchestrator geprueft: nur die zwei Signaturzeilen.
- nRF52: `uint32_t` ist dort `long unsigned int`; `%08X` braucht `(unsigned)`-Casts (Gate-Fund).
- Leaf-Konfiguration (Welle 2): Bench-Heltec DK5EN-1 mit `--txpower 2`, `--gateway off` (ist),
  `--mesh on` (war off; mit Mesh aus saehe DK5EN-98 nie, was das Blatt hoert, und hielte es fuer
  allein versorgt -- dann gaebe es an DK5EN-98 keinen Fall B mehr), `--nbrdebug on`, `--setlog on`,
  `--nbrrelay count`.

## Offen

- 24-h-Endauswertung des ersten Laufs (ab 22.09. 19:20) mit `tools/nbrsnap.py`/`tools/nbrrelay.py`.
- Chrome-Screenshot der Web-Seite am Bench-Knoten nach dem Flash.
- Entfernungsfilter fuer `#X` bei Gateway-Nachbarn (Konzept 2.1, Vorbehalt).
