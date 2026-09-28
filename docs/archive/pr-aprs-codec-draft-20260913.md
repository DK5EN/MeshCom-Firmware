# PR-Entwurf: APRS-Positionsdecoder und -encoder: #name, /R= /U= /I=, NaN-Schutz je Tag

Eingereicht 2026-09-13 als [PR #1142](https://github.com/icssw-org/MeshCom-Firmware/pull/1142) (Branch `pr-aprs-codec-20260913`, ein Commit auf `dev` `893cffd0`).

Ziel: `icssw-org/MeshCom-Firmware`, Branch `dev`. Ein Commit, nur `src/`, fuenf Dateien.

## 1. Decoder liest drei Positionsschluessel nicht, die der Encoder sendet

### Symptom

`PositionToAPRS()` in `src/loop_functions.cpp` schreibt 17 `/X=`-Schluessel in den
Positionsrahmen, `decodeAPRSPOS()` in `src/aprs_functions.cpp` liest 14. `/R=` (Gruppenliste),
`/U=` (INA226-Busspannung) und `/I=` (INA226-Strom) landen weder in `aprsPosition` noch bei
den Verbrauchern (Web-GUI, mHeard, BLE-Weitergabe an die App).

### Aenderung

- `src/aprs_structures.h`: neue Felder `vbus`, `vcurrent`, `grc[6]`, `grccnt` und `pos_name`
  in `struct aprsPosition`; `initAPRSPOS()` setzt sie auf 0 bzw. leer.
- `src/aprs_functions.cpp`, `decodeAPRSPOS()`: drei neue Scan-Bloecke nach dem Muster der
  bestehenden Schluessel. `/R=` bekommt einen eigenen 40-Byte-Puffer (Worst Case
  6 x `99999;` = 36 Zeichen) statt des gemeinsamen `decode_text[25]`, die Eintraege werden an
  `;` getrennt, maximal sechs, jeder auf 1..99999 geprueft. `/U=` und `/I=` werden als float
  gelesen.
- Nebenbefund im selben Block: die `/Y=`-Schleife war die einzige, die `decode_text`/`ipt` vor
  dem Lesen nicht zurueckgesetzt hat und den `/V=`-Inhalt geerbt hat. Sie setzt jetzt wie alle
  anderen zurueck.

## 2. Der Knotenname im Positionskommentar wird nicht getrennt

### Symptom

Seit `--setname` haengt der Encoder den Knotennamen als `#name` an den Freitext (`--atxt`)
im Positionsrahmen. Der Decoder hat die Kommentarregion an einem Leerzeichen oder einem
nackten `/` beendet und auf 25 Byte gekappt; ein Name hinter dem `#` ging verloren oder
landete abgeschnitten im Kommentarfeld.

### Aenderung

- `decodeAPRSPOS()`: die Kommentarregion reicht vom Anfang des Freitexts bis zum ersten
  `/X=`-Token (`/` + Grossbuchstabe + `=`) oder `/N` + Ziffer 1..9 (Nachbarzaehler). Leerzeichen
  und nackte `/` beenden sie nicht mehr. Die Kappung waechst von 25 auf 47 Byte und entspricht
  damit dem Budget des Encoders (atxt 25 + `#` 1 + node_name 19). Getrennt wird am **letzten**
  `#`: davor `pos_atxt`, dahinter `pos_name`. Ohne `#` bleibt `pos_name` leer und die ganze
  Region ist Kommentar; ein `#` im Freitext eines aelteren Encoders ergibt dann eben einen
  Namen aus dem Rest, aber nie einen Absturz oder eine Verschiebung anderer Felder.
- `src/command_functions.cpp`, `--setname`: entfernt `#` aus der Eingabe. Das ist der einzige
  Schreibpfad fuer den Namen (die Web-GUI ruft denselben Handler), damit ist der Split am
  letzten `#` fuer alle Namen eindeutig, die diese Firmware schreibt.

## 3. NaN-Schutz im Encoder prueft immer den Druck-Puffer

### Symptom

`PositionToAPRS()` formatiert jeden optionalen Sensor-Tag (`/P=`, `/H=`, `/T=`, `/O=`, `/F=`,
`/Q=`, `/G=`, `/C=`) per `snprintf("%.1f")` in einen eigenen Puffer und soll bei NaN den
Rahmen verwerfen. Alle acht Guards haben aber `cpress` (den `/P=`-Puffer) mit `"/P=nan"`
verglichen statt den Puffer, den sie gerade beschrieben haben. Ein NaN in Feuchte,
Temperatur, QFE, QNH, Gaswiderstand oder CO2 ging als `/H=nan` usw. auf Sendung, sobald der
Druck selbst gueltig war.

### Aenderung

- Neu `src/pos_tag_nan.h`: `posTagIsNan(const char *tag)` ueberspringt den dreistelligen
  Schluessel `/X=` und prueft den Wert auf `nan` oder `-nan` (letzteres liefern manche
  printf-Implementierungen fuer ein signalisierendes NaN). Header-only, ohne
  Arduino-Abhaengigkeit.
- `src/loop_functions.cpp`: die acht Guards rufen `posTagIsNan()` mit ihrem eigenen Puffer.

## Nachweis

Build auf `dev` `893cffd0` + Commit: `heltec_wifi_lora_32_V3`, `t_deck_plus`, `wiscore_rak4631`
sauber.

Host-Tests im Fork (gleicher Quellstand, die Tests liegen nicht im PR): `native_aprs` 98/98,
`native_parsers` 47/47 (davon `test_pos_tag_nan` 7/7), `native_aprs_fuzz` 5/5, `native` 308/308.
Darin sechs Faelle fuer `/R=`, `/U=`, `/I=` und den `/Y=`-Reset, die vor der Aenderung
fehlschlagen, sowie drei Split-Faelle fuer `#name` in `test_decodeaprspos` (mehrere `#`, das letzte
trennt; nur Name ohne Kommentar; kein `#`, `pos_name` leer).

Noch nicht auf Hardware gegen Live-Frames verifiziert; die Host-Tests decken den Decoder mit
Mitschnitt-Korpora aus dem OE- und DL-Netz ab.
