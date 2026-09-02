# PR-Entwurf: GPS-01..04 — Hoehe und QNH-Referenz

**Nicht via `/submit-pr` erzeugt** (kein `gh`-Aufruf, kein PR-Branch geschnitten). Dieser
Text ist die Vorlage fuer den PR-Cut aus
[`gps-nmea-impl-plan-20260902.md`](gps-nmea-impl-plan-20260902.md) §8; Format nach
`submit-pr` (siehe `.claude/commands/submit-pr.md`).

---

## Titel

`fix(gps): NMEA-Drain, Plausibilitaetspruefung, Hoehenfilter und QNH-Relatch`

## Dateien dieses PR-Cuts

Nur `src/`, kein Test-, Tool- oder Doku-Delta:

```
src/gps_filter.h              (neu)
src/gps_filter.cpp            (neu)
src/gps_functions.h
src/gps_functions.cpp
src/esp32/esp32_main.cpp
src/nrf52/nrf52_main.cpp
src/command_functions.cpp
src/bmx280.cpp
src/bme680.cpp
src/loop_functions_extern.h
```

`platformio.ini` (native `test_filter`-Env) wird beim Cut auf `upstream/dev` verworfen —
das Env existiert nur im Fork.

## Was wurde geaendert

### `src/gps_filter.h` / `src/gps_filter.cpp` (neu)

Arduino-freie Datei (baut auch nativ, `env:native`), kein `Serial`, kein `millis()`, kein
`meshcom_settings`. Enthaelt:

- `struct AltFilter { float x; float P; uint8_t rejects; bool init; }` — Zustand eines
  skalaren Kalman-Filters auf der Hoehe.
- `altFilterReset()`, `altFilterSeed()`, `altFilterUpdate(AltFilter*, float meas, uint32_t
dt_ms)`, `altFilterConverged()`. Konstanten `ALT_KF_Q`, `ALT_KF_R`, `ALT_KF_P0`,
  `ALT_KF_GATE_M`, `ALT_KF_RESEED_N`, `ALT_KF_P_CONV`, `ALT_KF_DT_REF_MS`,
  `ALT_KF_DT_MAX_MS` als `#define`.
- `gpsSamplePlausible(double lat, double lon, double alt, int year, int month, int day)` —
  verwirft Nullinsel (`lat`/`lon` exakt 0.0), Winkel ausserhalb +-90/+-180, Hoehe
  ausserhalb `GPS_ALT_MIN_M`/`GPS_ALT_MAX_M` (-500..10000 m) und ein Kalenderdatum, das der
  Parser nicht produziert haben kann.
- `gpsDatePlausible(int, int, int)` / `gpsTimePlausible(int, int, int)` — dieselbe Pruefung
  fuer den Zeitstempel, getrennt von der Positionspruefung, weil beide Pfade unabhaengig
  voneinander in `gps_functions.cpp` gebraucht werden.

### `src/gps_functions.h` / `src/gps_functions.cpp`

- Neue `void WZ_GPS_Feed(void)`: enthaelt nur die UART-Leseschleife
  (`while (GPSSerial.available())`) und das NMEA-Echo (`iGPSDEBUG > 2`, jetzt zeilenweise
  gesammelt statt bytesweise ausgegeben). `WZ_GPS_Loop()` behaelt nur noch die Auswertung
  ab `if (updateGPSdata)` und laeuft weiter auf ihrem eigenen Takt
  (`gps_refresh_intervall`).
- Der Fix-Gate `hdop < 6.0 && sat > 5` wird um `gpsSamplePlausible(...)` erweitert; ein
  verworfener Sample zaehlt einen `static uint16_t`-Zaehler hoch und loggt unter
  `iGPSDEBUG > 0` eine `[GPS ]...reject:`-Zeile.
- Beide Aufrufstellen von `MyClock.setCurrentTime()` (Fix- und Kein-Fix-Zweig) pruefen
  zusaetzlich `gps.time.isValid() && gpsDatePlausible(...) && gpsTimePlausible(...)`, bevor
  sie die Systemzeit setzen.
- `gps.altitude.isUpdated()` wird vor `.meters()` gelesen (der Zugriff loescht das Flag)
  und der Filter nur bei einer wirklich neuen Hoehe gefuettert.
- `node_alt = (int)gpsData.altitude` wird durch `altFilterUpdate(&s_alt, ..., dt_ms)`
  ersetzt (`s_alt` statisch in dieser Datei); `dt_ms` ist die Wanduhrzeit seit dem letzten
  Update. In TRACK (`bDisplayTrack`) wird der Filter zurueckgesetzt und `node_alt` bleibt
  die rohe GPS-Hoehe wie bisher — ein Filter mit Zeitkonstante im Minutenbereich waere auf
  einem bewegten Knoten ein Fehler.
- Neue `WZ_GPS_AltSeed(float)` (fuer `--setalt`) und `WZ_GPS_AltConverged()` (fuer den
  QNH-Relatch).
- Neue, plattformunabhaengige `baroBaseRelatch(float alt)` und `baroBaseLatchAllowed(void)`
  (bewusst ohne `WZ_GPS_`-Praefix — beide sind auf jedem Board uebersetzt, auch auf einem
  ohne GPS). `baroBaseRelatch()` schreibt jede vorhandene Basishoehe
  (`fBaseAltidude`/`fBaseAltidude680`); `baroBaseLatchAllowed()` liefert `true`, solange
  kein Fix vorliegt oder kein GPS aktiv ist, und sonst erst, wenn der Hoehenfilter
  konvergiert ist (`WZ_GPS_AltConverged()`).

### `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`

`WZ_GPS_Feed()` wird auf beiden Plattformen jeden `loop()`-Durchlauf aufgerufen (ESP32
unmittelbar vor dem `gps_refresh_intervall`-Block, nRF52 vor dessen 1s-Zeitfenster), jeweils
innerhalb des bestehenden `ENABLE_GPS`/`bGPSON`/`gpsDetected`-Guards. Der bestehende Aufruf
von `WZ_GPS_Loop()` bleibt unveraendert an seiner Stelle.

### `src/command_functions.cpp`

- `--setalt`: Werte ausserhalb 0..40000 m werden jetzt mit einer Meldung verworfen statt
  auf 0 geklemmt (fruehere Version setzte bei ungueltiger Eingabe `node_alt = 0` und liess
  das so in die Konfiguration schreiben). Bei einem gueltigen Wert wird zusaetzlich
  `WZ_GPS_AltSeed()` (mit GPS) bzw. `baroBaseRelatch()` (ohne GPS) aufgerufen — der Wert ist
  ein Startpunkt fuer den Filter, keine Festlegung; GPS verfeinert danach weiter.
- `--setpress`: schreibt die Basishoehe jetzt ueber `baroBaseRelatch()` statt direkt auf
  `fBaseAltidude` — vorher blieb die Basishoehe eines BME680 dabei unveraendert.
- Hilfetext bei `--setalt` ergaenzt: "seeds the altitude filter, GPS keeps refining".

### `src/bmx280.cpp`, `src/bme680.cpp`

Der Latch-Bedingung in `getPressASL()` / `getPressASL680()` (bisher: `if (fBaseAltidude ==
0) fBaseAltidude = current_alt;`) wird `&& baroBaseLatchAllowed()` hinzugefuegt. Ohne GPS
oder vor einem Fix verhaelt sich das exakt wie bisher (erster Wert gewinnt); mit GPS und
Fix wartet der Latch auf die Filterkonvergenz.

### `src/loop_functions_extern.h`

`fBaseAltidude680` (bisher file-static in `bme680.cpp`) wird `extern`-sichtbar gemacht,
damit `baroBaseRelatch()` in `gps_functions.cpp` beide Sensoren bedienen kann.

## Warum

Feldbericht `OE5HWN-14` (T-Beam Supreme, 4.35p, 2026-09-01): die WX-Dashboard-Hoehe
schwankte zwischen 179 und 308 m auf einem Knoten, der sich nicht bewegt hat. Zwei
`--gpsdebug`-Mitschnitte (Analyse in `bug-GPS-uart-overflow-20260901.md`) zeigen den
eigentlichen Defekt:

- **GPS-01.** `WZ_GPS_Loop()` liest die GPS-UART bisher nur einmal pro
  `gps_refresh_intervall` (3 s auf ESP32). Das L76K-Modul sendet GGA+RMC mit ~140 B/s; der
  Arduino-Standardring fasst 256 B. 3 s x 140 B/s = 420 B in einen 256-B-Ring, also gehen
  pro Zyklus ~165 B verloren — permanent, auf jedem stehenden ESP32-Knoten. Der Schnitt
  liegt mitten im Satz; der Parser synct beim naechsten `$` neu und splict den Rest an einen
  spaeteren Satz.
- **GPS-02.** Die 8-Bit-NMEA-Pruefsumme faengt fast jeden gesplicten Satz ab, aber im
  Schnitt **1 von 256** passiert die Pruefsumme zufaellig und wird als echter Fix
  uebernommen. Beobachtet im 22-Minuten-Log genau einmal (Vorhersage ~1,7): eine Position
  mit `lon:0.000000`, Datum `2015.14.00`, bei `fix:yes sat:7 hdop:2.7` — der Sat/HDOP-Gate
  sieht davon nichts, weil beide Werte aus einem anderen, unbeschaedigten Satz stammen.
- **GPS-03.** Die Hoehe war ein einzelner ungefilterter Rohwert. Beide Feldlogs zeigen eine
  Streuung von mehreren Metern (Sigma 4,3 m bzw. 4,1 m) selbst bei guter Geometrie (HDOP
  1,4, 8 Satelliten). Gemessen mit einem skalaren Kalman-Filter (konstantes `R`, `q`=0,01):
  RMS **4,36 m -> 1,52 m**, schlechtester Einzelwert 25,4 m -> 2,6 m. Ein HDOP-gewichteter
  Filter wurde waehrend des Reviews angefragt und anhand beider Logs gemessen verworfen
  (Korrelation HDOP/Fehler nur +0,20 bis +0,51, identische Werte mit konstantem `R` bis zur
  dritten Nachkommastelle) — Details in `bug-GPS-uart-overflow-20260901.md` §7.6.
- **GPS-04.** Die barometrische QNH-Referenz (`fBaseAltidude`) latcht bisher auf den ersten
  Fix nach dem Boot und wird nie korrigiert. Ein Ausreisser beim Booten kostet die ganze
  Sitzung ~+-7 hPa — und der Knoten des Berichts hat innerhalb von 7 Minuten zweimal neu
  gestartet.

`git diff upstream/dev` ueber den gesamten GPS/Hoehen/QNH-Pfad ist leer: keiner dieser
Konstrukte stammt vom Fork, alles blamiert auf Kurt, 2023-03-05 bis 2026-04-23
(`bug-GPS-uart-overflow-20260901.md` §9).

## Kosten

- **RAM:** ~16 B zusaetzliches statisches RAM, ausschliesslich auf Boards mit
  `ENABLE_GPS` (der `AltFilter`-Zustand, der Reject-Zaehler, das Konvergenz-Flag und der
  letzte Update-Zeitstempel). 0 B auf Boards ohne GPS. Kein `setRxBufferSize`, keine
  Line-Buffer — bewusst verworfen (Analyse-Doc §7.1): der Ring bleibt bei 256 B, das Problem
  wird durch Lesehaeufigkeit statt Puffergroesse geloest.
- **Flash:** kein separates `ram_snapshot`-Ergebnis fuer diesen Cut angehaengt; die
  Aenderung ist eine reine Funktionsverschiebung plus zwei kleine reine Funktionen ohne
  Bibliotheksabhaengigkeit, der Flash-Zuwachs ist im Rahmen der uebrigen Aenderungen dieser
  Woche.
- **Laufzeit:** ein zusaetzlicher `available()`-Aufruf pro `loop()`-Durchlauf auf einem
  leeren Ring im Regelfall — dieselbe Groessenordnung wie das bestehende
  `Serial.available()` fuer die Kommandokonsole.

## Verhalten ohne GPS unveraendert

Jeder neue Aufruf haengt am bestehenden Guard (`#ifdef ENABLE_GPS`, `bGPSON`,
`gpsDetected`). Boards ohne `ENABLE_GPS`, mit `--gps off` oder ohne erkanntes Modul
kompilieren und verhalten sich exakt wie zuvor. `baroBaseRelatch()` /
`baroBaseLatchAllowed()` sind zwar auf jedem Board uebersetzt (auch ohne GPS), verhalten
sich dort aber wie der bisherige Latch: der erste Druckwert nach dem Boot gewinnt.

## Offene Punkte

- **Bench-Nachweis aussteht.** `DK5EN-14` (T-Deck Plus, dasselbe GNSS-Modul wie der
  Feldknoten) war beim Implementieren nicht angeschlossen. Das Protokoll (Arme A/B/C, je
  2 h, `tools/bench/gpsdebug_scan.py`) steht in `gps-nmea-impl-plan-20260902.md` §6 und ist
  noch nicht gelaufen.
- **TRACK-Verhalten.** Im TRACK-Modus (1 s Takt, bewegter Knoten) wird der Hoehenfilter bei
  jeder Auswertung zurueckgesetzt und `node_alt` bleibt der rohe GPS-Wert — eine
  Zeitkonstante von Minuten waere auf einem bewegten Knoten falsch. Das ist eine bewusste
  Entscheidung (Analyse-Doc §7.4), aber ohne Feldmessung im TRACK-Betrieb unter diesem PR
  noch nicht bestaetigt.
- **QNH-Latch-Regel.** Der Latch wird zurueckgehalten, solange ein GPS-Fix vorliegt und der
  Filter noch nicht konvergiert ist; ohne Fix (z. B. ein abgeschatteter WX-Knoten, der nie
  fixt) latcht weiterhin der erste verfuegbare Wert wie bisher. Das ist eine
  Review-Korrektur (Verdikt-Doc, Finding 3) gegenueber dem urspruenglichen Plan, der hier
  einen dauerhaft ungelatchten Zustand (QFE statt QNH) erzeugt haette.

## Tests

Native Testsuite `test/test_gps_filter/test_main.cpp`, 16 Faelle,
`pio test -e native -f test_gps_filter`, alle gruen:

- Plausibilitaet: echter Fix akzeptiert; Nullinsel/Kalender/Winkel/Hoehe ausserhalb des
  Bereichs verworfen (inkl. des Feld-Splice-Samples aus dem Analyse-Dokument); Datums- und
  Zeitpruefung separat abgedeckt.
- Filter: eingeschwungener Zustand haelt synthetisches Rauschen klein; Kaltstart seedet
  exakt; die Doc-Sequenz (278.6..257.7 m) wird mit der exakten Gate-Entscheidung pro
  Stichprobe geprueft; ein einzelner +25-m-Ausreisser wird verworfen; zehn aufeinanderfolgende
  Ausreisser seeden neu; die Konvergenzflagge kippt innerhalb von 100 akzeptierten Samples.
- dt-Skalierung (`dt_ms`): drei 1-s-Updates entsprechen einem 3-s-Update ueber dieselbe
  Wanduhrzeit; das Prozessrauschen skaliert mit `dt_ms`; ein sehr langer Ausfall wird
  geklemmt.
- Zwei vollstaendige Feldserien (`gpsdebug.txt`, `gpsdebug1.txt`) als Fixtures: rohe RMS
  bleibt bei den gemessenen Basiswerten (4,36 m / 4,08 m), gefilterte RMS in der
  konvergierten Phase bei 1,50 m / 0,71 m.

GPS-01 (das Trennen von Lesen und Auswerten) hat keinen nativen Pruefpunkt
(`HardwareSerial` laesst sich nicht mocken) — der Nachweis ist Bench-Arm C, siehe "Offene
Punkte".

Alle sieben Standard-Boards wurden waehrend der Implementierungswellen sequenziell
clean-gebaut (`heltec_wifi_lora_32_V3`, `E22-DevKitC`, `ttgo_tbeam`,
`ttgo_tbeam_supreme`, `t_deck`, `t_deck_plus`, `wiscore_rak4631`), `-Werror` aktiv.

## Hinweise fuer den Reviewer

- Review-Verdikt mit allen zehn Findings und ihren Fixes:
  [`review-verdict-gps-20260902.md`](review-verdict-gps-20260902.md).
- Vollstaendige Root-Cause-Analyse mit Log-Belegen:
  [`bug-GPS-uart-overflow-20260901.md`](bug-GPS-uart-overflow-20260901.md).
- Zwei verwandte Befunde aus denselben Logs sind **nicht** Teil dieses PRs: TM-51 (Boot
  protokolliert den Reset-Grund) ist bereits separat gefixt; TM-52
  (Display-Sektion ~570 ms auf dem T-Beam Supreme) wurde fuer diese Kampagne fallen
  gelassen, Verdachtsmoment dokumentiert.
