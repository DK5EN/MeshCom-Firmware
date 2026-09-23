# Nachbarschaftsmatrix Stufe 2: Kampagnenstand

Konzept: `docs/nbr-wichtigkeit-konzept.md` (v3, 2026-09-22). Branch `feature-neighbour-matrix`.
Betreiberentscheidungen 2026-09-22: POS/HEY duerfen 20 s Nachrang bekommen; Fall A darf vor allen
anderen senden (Re-Arm-Kappung); der Server verteilt jeden Frame an jedes Gateway (Gateway-
Ausnahme ist drin); alles lokal umsetzen, committen, auf DK5EN-98 und den Bench-Heltec flashen,
Test weiterlaufen lassen. Zweites Blatt = Bench-Heltec am Laptop mit 2 dBm.

## Wellen

| Welle | Inhalt                                                                                                                                                                                                                                                      | Dateien                                                                                                                                  | Stand                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 0     | Vertrag: Konstanten, Globals, Matrix-API (`nbrRelayNeed`, `nbrCoverMask`, `nbrRowMeshNeedCount`, `nbrExclusiveDirect`), Echo-Fix (Spalte 0 nur per ME), Direktzeilen-Schutz, 5 Host-Tests                                                                   | `src/configuration_global.h`, `src/loop_functions.cpp`, `src/loop_functions_extern.h`, `src/nbr_matrix.{h,cpp}`, `test/test_nbr_matrix/` | erledigt, 33/33 gruen                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| 1B    | Relay-Entscheidung: Masken je Ring-Slot, NEED/CANCEL/REFUSE, Cancel-Zweig im RX, Backoff nach Fall                                                                                                                                                          | `src/lora_functions.cpp`, `src/txring_functions.{h,cpp}`, `test/test_txring/`                                                            | laeuft                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| 1D    | Web-Seite 2a: vertikale Koepfe, `D/I G M #N #X Role`, Legende, Umbruch, Sortierung, `Covered by`, Block 6.3                                                                                                                                                 | `src/web_functions/web_functions.cpp`                                                                                                    | laeuft                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| 1C    | `--nbrrelay off                                                                                                                                                                                                                                             | count                                                                                                                                    | on`, Boot-Restore, `--info`-Zeile, Toggle-Test; Logformat; Kampagnendoc                                                                                                                                                                                                                                                                                                                                                                                                                                                              | `src/command_functions.cpp`, `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`, `test/test_command_toggles/`, `docs/` | Orchestrator, erledigt |
| Gate  | Host-Tests (native_nbr_matrix, native_aprs/test_txring, test_command_toggles), Builds heltec/rak/tbeam/tdeck sequenziell, String-Scan, Advisor (`/fable-review`), Commit                                                                                    | Orchestrator                                                                                                                             | erledigt: Host-Tests 34/21/120 gruen, vier Builds gruen (nRF52 brauchte `(unsigned)`-Casts), Advisor APPROVED (1 Medium behoben: kein Wissen ist nicht Fall B; 2 Low behoben: Text-Slots, Kommentar; 2 Low offen: Maskenindex bei Verdraengung, count-zu-on-Attribution), committed                                                                                                                                                                                                                                                  |
| 2     | Hardware: OTA DK5EN-98 (PAUSE-Datei, meshlogger mit `nbrdebug,loradebug,txcapture` neu starten, `--nbrrelay count`), Bench-Heltec seriell flashen, `--txpower 2 --gateway off --nbrdebug on --setlog on --nbrrelay count`, serielle Aufzeichnung des Blatts | Orchestrator                                                                                                                             | erledigt 2026-09-22 09:20-09:35: DK5EN-98 per OTA auf Build 09:00:32, `--nbrrelay count`, Pi-Logger neu ab 09:28:34 fuer 48 h (`--since 2026-09-22T09:28` fuer die Auswertung, alter Lauf liegt in derselben Tagesdatei); DK5EN-1 per USB geflasht, `--txpower 2 --mesh on --nbrdebug on --setlog on --nbrrelay count`, Mitschnitt `~/meshlog/dk5en-1/2026-09-22.log` (`tools/bench/serial_capture.py`, nohup); Web-Seite per curl geprueft (Tags balanciert, alle neuen Spalten/Bloecke da), Chrome-Erweiterung war nicht verbunden |

## Welle 3: Symmetrie-Annahme (`--nbrsym`), 2026-09-22

Anlass: nach 11,6 h `count` auf DK5EN-98 926 A / 10 B / 1 `CANCEL?` / 185 `REFUSE`. Die
Allein-Maske war in 96 % der NEED-Zeilen DK5EN-1 (echtes Blatt, 1099 von 1113 Empfaengen ueber
DK5EN-98) und/oder DL2JA-1 (relayt nie, vier Wochen Logs, daher keine beobachtete Hoerkante).
Ohne beide: 36 A / 900 B.

| Schritt | Inhalt                                                                                                                                                                                                    | Stand                                                                                                                                                                                                                                                                                                                                         |
| ------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 3A      | Firmware: Zelle speichert SNR statt RSSI (`NBR_SNR_UNKNOWN` = -128), `--nbrsym on\|off` (node_sset4 0x0080 invertiert, Default an), Fallback in `nbrRelayNeed`/`nbrCoverMask`, `SYM`-Zeilen, `<inferred>` | erledigt                                                                                                                                                                                                                                                                                                                                      |
| 3B      | Logvertrag `docs/nbr-logformat.md`, `tools/nbrlog.py` (SNR-Statistik, Abschnitt 10 Symmetrie-Annahmen), neue Fixture                                                                                      | erledigt                                                                                                                                                                                                                                                                                                                                      |
| Gate    | 1075 Host-Tests, Builds heltec/rak/tbeam/tdeck, String-Scan, Advisor                                                                                                                                      | gruen; Advisor APPROVED, 1 Medium behoben (keine gestapelten Annahmen), 3 Low                                                                                                                                                                                                                                                                 |
| 3C      | OTA DK5EN-98 und DK5EN-1 per `tools/webflash.py`, danach `--gateway on` auf DK5EN-1 ueber Konsole 2323                                                                                                    | erledigt 23:12: beide Knoten Build `Sep 22 2026 / 23:08:13`, Web-Seite zeigt `Symmetry: on (SNR >= -16 dB)`; DK5EN-1 per `/setparam/?gateway=on` (Konsole 2323 dort aus), Server-BEAT ok, an DK5EN-98 `G = Y`, Bit weg aus NEED; USB-Mitschnitt des Blatts lief durch. Auswertung ab `--since 2026-09-22T23:12`, fruehestens nach einer Nacht |

## Welle 4: HN-Nachbarschaftsmeldung (`--nbrreport`), 2026-09-23

Anlass: Nachtlauf 22./23.09. mit `--nbrsym`: 518 B / 14 A, 96 `CANCEL?` (19 % der B-Relays), aber
72 davon ruhen auf Annahmen ueber DL2JA-1, 19 auf einer -15-dB-Strecke. Nur der Knoten selbst kann
sagen, wen er hoert -- per Einzel-ACK (etwa 60 s/h Sendezeit je Endknoten) verworfen, stattdessen
eine seltene Ein-Hop-Meldung.

| Schritt | Inhalt                                                                                                                                                                                                                                | Stand                                                                                                                                                                                                                                                                                |
| ------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 4A      | Matrix: `nbrBuildReport`/`nbrNoteReport`, Veto der Symmetrie durch vollstaendige frische Meldung (45 min), `RPT`/`RPTSUM`/`DROP RPT`/`SYM VETO`, `NbrRow` bleibt 24 B                                                                 | erledigt, 58 + 3 Host-Tests (neue Umgebung `native_nbr_report` fuer die Kappung)                                                                                                                                                                                                     |
| 4B      | Firmware: Decoder nimmt `HN` nur bei `@`, RX-Abfang vor Stufe 2/Dedup/Trickle/Upload, `sendNbrReport` (max_hop 0), fester 15-min-Takt (`NBR_REPORT_INTERVAL_S` = `TRICKLE_IMAX_S`), `--nbrreport off\|auto\|on` (sset4 0x0100/0x0200) | erledigt                                                                                                                                                                                                                                                                             |
| 4C      | Logvertrag, `nbrlog.py` Abschnitt 11, Fixture                                                                                                                                                                                         | erledigt                                                                                                                                                                                                                                                                             |
| Gate    | 1100 Host-Tests, vier Builds, String-Scan, Golden-Makrodatei (separat nachgezogen), Advisor                                                                                                                                           | gruen; Advisor mit 1 Medium (Doku `<heard>`) und 3 Low (VETO-Menge, Ping-Guard, Golden) -- alle behoben                                                                                                                                                                              |
| 4D      | OTA beider Knoten, DK5EN-98 `--nbrreport on` (Bench-Sender) und `--nbrrelay on`                                                                                                                                                       | erledigt 09:20: beide Knoten Build `Sep 23 2026 / 09:16`, DK5EN-98 `NBRRELAY on`, `NBRSYM on`, `NBRREPORT on`; erste Meldung 09:24:48 `R4;N4;DK5EN-1,5;DB0ED-99,-4;DL2JA-2,-9;DL2UD-1,-10;` (78 B, H00), am Blatt 2x ok, 1x self, 1x norow. Auswertung ab `--since 2026-09-23T09:20` |

## Welle 5: Fall-B-Sperre als Frist, kein Kopfblockieren (2026-09-23 abends)

Anlass: erster `on`-Tag (09:20 bis 18:38) -- Relays 72/h auf 22/h, aber 149 Relays und 10 HN-Meldungen
verworfen (`RING_DROP_STALE` nach 180 s, `RING_DROP_NEW` "queue full" bei 4/20), Fall-B-Wartezeit im
Median 137 s (max. 16 min), Fall A hinter Fall B bis 413 s. Ursache: die +20 s von Fall B wurden bei
jedem Re-Arm neu addiert, und `getNextTxSlot()` liess einen gehaltenen Fall-B-Relay alles hinter sich
blockieren.

| Schritt | Inhalt                                                                                                                                                                                                       | Stand                                                                    |
| ------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------ |
| 5A      | `txringCaseBackoffSlot()`: Sperre = einmalige Frist ab Einreihen, danach normaler Fall-B-Backoff, ab `NBR_RELAY_CASE_B_MAX_WAIT_MS` (60 s) Kurzsuche; `getNextTxSlot()` ueberspringt gehaltene Fall-B-Relays | erledigt, 6 Regressionstests in `test_txring` (3 schlagen ohne Fix fehl) |
| Gate    | 1106 Host-Tests, vier Builds, Symbole im Image, Golden (+1 Makro), Advisor                                                                                                                                   | gruen; Advisor APPROVED, 2 Test-Nacharbeiten erledigt                    |
| 5B      | OTA beider Knoten, Soak weiter mit `--nbrrelay on`                                                                                                                                                           | offen                                                                    |

Offen aus dem Advisor (akzeptiert): der Ring meldet "voll" nach Indexabstand bei festgehaltenem
`iRead`; jetzt nur noch bei mindestens 19 Einreihungen innerhalb von 60 s erreichbar. Unter lauter
gehaltenen Kandidaten entscheidet Prio/FIFO statt der fruehesten Frist.

## Welle 6: Text nur noch als ME-Schritt (2026-09-23 abends)

Anlass: die Matrix auf DK5EN-98 zeigte Zeilen, die nie per Funk da waren (OE1XAR-33, DO2QG-1,
DM3KS-12, DL1GFM-7), alle als Kante in die Spalte eines Gateways mit Mesh an (DL2JA-2, DK5EN-1,
DF8RD-1). Ein Gateway setzt Server-Frames mit `<Server-Pfad>,<Gateway>` auf LoRa. Auswertung des
Mitschnitts 22.09. 09:28 bis 23.09. 19:11 (34 h): der Server schickte DK5EN-98 511 Frames, 100 %
Text, kein POS, kein HEY; 79 Kanten entstanden nur aus Text, alle 79 endeten an einem
einspeisenden Gateway (DL2JA-2 57, DK5EN-1 22); kein letzter Hop war nur per Text erreichbar. Das
Server-Bit (Byte 5, 0x80) trennt Einspeisung nicht vom normalen Relay eines IP-Gateways und taugt
nicht als Merkmal.

| Schritt | Inhalt                                                                                                                        | Stand                                                                                                                       |
| ------- | ----------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| 6A      | `nbrNoteFrame()`: `:` liefert nur den ME-Schritt (keine Kanten, keine Zeile ausser fuer den letzten Hop, kein `CUT`), 6 Tests | erledigt                                                                                                                    |
| Gate    | Host-Tests, vier Builds, Advisor                                                                                              | gruen; 1112 Host-Tests, 4 neue Tests schlagen ohne Fix fehl; Advisor REWORK (nur Kommentare/Tests, 7 Punkte), alle erledigt |
| 6B      | OTA beider Knoten, `--nbrreset` nicht noetig (Neustart leert die Matrix)                                                      | offen                                                                                                                       |

Restrisiko: die Regel setzt voraus, dass der Server nur Text an Gateways schickt;
`udp_frame_esp32.cpp` nimmt weiterhin `!`/`@` vom Server an, ohne Merkmal auf der Luft.
Bewusster Verlust: Text traegt auch kein "X hoert mich" (Echo `<ich>,X`) und keine echte
Relay-Kante `A,M` mehr ein -- ein anderes Gateway kann meinen hochgeladenen Text ebenso als
`<ich>,<Gateway>` senden. Diese Kanten kommen jetzt nur noch aus POS, HEY und HN-Bericht.

## Entscheidungen

- pio-Slot: in Welle 1 ausschliesslich Agent B; D kompiliert nicht, das Gate kompiliert.
- Der Gateway-Echo-Fix (5.8 Punkt 0) ist in Welle 0 drin: ohne ihn stuende OE1XAR-33 in der
  Allein-Maske von DK5EN-98 und `count` saehe nie einen Abbruchkandidaten.
- `count` aendert kein Funkverhalten; `on` schaltet Abbruch UND Backoff nach Fall.
- Settings-Bits: `node_sset4` 0x0020 count, 0x0040 on.
- Symmetrie (Betreiberentscheidung 2026-09-22): SNR statt RSSI, Schwelle SNR >= -16 dB
  (Erfahrungswert: DF2SI-12 kommt an DK5EN-98 mit -16 dB Median an und faellt regelmaessig ins
  Rauschen). Keine Rechnung gegen Endstufen (E22, T-Beam 1W, Nachruest-PA), jede Annahme als
  `[NBR]|SYM` geloggt. Nur die Stufe-2-Entscheidung, Stufe-1-Urteile bleiben beobachtet.
  Beobachtete Kante gewinnt immer; ein Knoten, der den Frame nur per Annahme hat, versorgt
  niemanden (hoechstens eine Annahme je Entscheidung, Advisor-Fund).
- Settings-Bit `node_sset4` 0x0080 = `--nbrsym off` (invertiert wie `--mesh`).
- Settings-Bits `node_sset4` 0x0100 = `--nbrreport off`, 0x0200 = `--nbrreport on`, keins = auto
  (Default; sendet nur bei Mesh aus UND Gateway aus).
- Schwelle `LORA_SNR_STABLE_MIN_DB` (-16) steht in `src/configuration_default.h` direkt bei
  SF/BW/CR, weil sie an der Modulation haengt; `NBR_SYM_MIN_SNR` ist nur noch ein Alias.
- Abbruch unter `on` darf auf beobachteter, gemeldeter UND angenommener Deckung ruhen
  (Betreiberentscheidung 2026-09-23).
- Advisor Low, akzeptiert: `REFUSE` kann mit sym auch gegen einen nur per Annahme deckenden
  Relayer fallen (ohne eigene `SYM`-Zeile); `EDGE`-`<snr>` ist der Wert vor dem Frame.
- DK5EN-1 bekommt `--gateway on`: der Server versorgt es, damit faellt es aus der Abhaengigen-Menge
  von DK5EN-98 (Konzept 5.4). Das Blatt taugt damit nicht mehr als R6-Zustellbeweis.
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
- Chrome-Screenshot der Web-Seite am Bench-Knoten (192.168.68.71/?page=neighbours), sobald die Erweiterung wieder verbunden ist.
- Nach einigen Stunden `count`: NEED-Verteilung A/B/U auf DK5EN-98 pruefen, dann `--nbrrelay on` ueber die Konsole (PAUSE-Datei fuer den Logger).
- Entfernungsfilter fuer `#X` bei Gateway-Nachbarn (Konzept 2.1, Vorbehalt).
