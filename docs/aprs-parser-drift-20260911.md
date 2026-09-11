# APRS-Parser-Drift: Firmware, MCProxy, MobileApp, Contract

Stand 2026-09-11. Firmware `fork-main` @ `c6ac16bd` (Basis upstream v4.35t), MCProxy @ `7dacfa5`,
Meshcom-MobileApp @ `6e7f2e5` (7 Commits hinter `origin/main`, alle sieben sind
Dependency-Bumps ohne Parser-Code). Reine Analyse, keine Codeänderung. Jede Aussage ist gegen
den aktuellen Baum verifiziert; Zeilenangaben sind Stand heute.

## Ergebnis in drei Sätzen

Die Firmware ist die einzige Quelle der Wahrheit für das Positions-Kommentarformat, und sie
emittiert heute 17 `/X=`-Schlüssel, von denen ihr eigener Decoder nur 14 kennt. MCProxy
parst 10 davon typisiert, wirft 6 in einen untypisierten `extras`-Sack und verwirft einen; die
App parst 13, davon einen falsch und einen ins Leere, und ignoriert Symbol, MOD, FCS, FW, LH
und alle Fork-Erweiterungen. Kein
Dokument beschreibt den `/X=`-Schlüsselraum vollständig; das beste Dokument
(`docs/architecture/11-wire-format.md`) enthält ihn gar nicht.

## 1. Wo die Parser liegen

| Rolle                        | Repo     | Datei                                                       | Format                                            |
| ---------------------------- | -------- | ----------------------------------------------------------- | ------------------------------------------------- |
| Encoder Position             | Firmware | `src/loop_functions.cpp:4200-4495` `PositionToAPRS()`       | `!ddmm.mmN<sym>dddmm.mmE<sym><atxt>#<name>/X=...` |
| Encoder Envelope             | Firmware | `src/aprs_functions.cpp:1089-1211` `encodeAPRS()`           | Binärrahmen, Trailer HW MOD FCS FW LH             |
| Decoder Envelope             | Firmware | `src/aprs_functions.cpp:123-523` `decodeAPRS()`             |                                                   |
| Decoder Position             | Firmware | `src/aprs_functions.cpp:556-1087` `decodeAPRSPOS()`         | 14 Schlüssel                                      |
| Parser BLE-Binär             | MCProxy  | `src/mcapp/ble_protocol.py:341` `decode_binary_message()`   | struct `<BIB` + Footer `<BBBHBBBB`                |
| Parser Position              | MCProxy  | `src/mcapp/ble_protocol.py:402-606` `parse_aprs_position()` | Regex, ordnungsunabhängig                         |
| Parser EXTUDP                | MCProxy  | `src/mcapp/udp_handler.py:448-560`                          | JSON (kein APRS-Text)                             |
| Parser BLE (einziger Parser) | App      | `src/hooks/MessageHandler.ts:71` `parseMsg()`               | feste Offsets + `split("/")`                      |

Wichtig für die Erwartungshaltung: MCProxy sieht auf dem EXTUDP-Pfad (Port 1799) **kein**
APRS-Textformat, sondern das von der Firmware bereits geparste JSON aus `sendExtern()`. Nur der
BLE-Pfad von MCProxy und die App parsen den rohen Rahmen selbst.

## 2. Drift-Matrix `/X=`-Schlüssel

Emission: `src/loop_functions.cpp:4304-4478`. Firmware-Decoder: `src/aprs_functions.cpp`,
Suchreihenfolge B A P H T O F Q G N C V Y D. MCProxy: `ble_protocol.py:497-604`. App:
`MessageHandler.ts:793-885`.

| Schlüssel | Firmware emittiert                                            | Firmware dekodiert                | MCProxy BLE                               | App                                           | Drift                                                                                     |
| --------- | ------------------------------------------------------------- | --------------------------------- | ----------------------------------------- | --------------------------------------------- | ----------------------------------------------------------------------------------------- |
| `/A=`     | `%06i` Fuß (`%05i` Meter nur bei `bFuss=false`, unerreichbar) | `%d`, max 7 Zeichen               | `\d{6}` Fuß, x0.3048                      | beliebig viele Ziffern, x0.3048               | MCProxy würde die 5-stellige Meterform verlieren; README sagt "Meter, 4 Stellen" (falsch) |
| `/B=`     | `%03d`, auch bei 0 %                                          | `%d`                              | `\d{1,3}`, 0..100, letzter Treffer        | String, führende Nullen weg, `"0"` → `"N.A."` | konsistent                                                                                |
| `/P=`     | `%.1f` QFE                                                    | `%f`                              | `qfe`                                     | `pressure`                                    | konsistent                                                                                |
| `/H=`     | `%.1f`                                                        | `%f`                              | `hum`                                     | `humidity`                                    | konsistent                                                                                |
| `/T=`     | `%.1f`                                                        | `%f`                              | `temp1`                                   | `temperature`                                 | konsistent                                                                                |
| `/O=`     | `%.1f` zweiter Sensor                                         | `%f`                              | `temp2`                                   | `temp_2`                                      | konsistent                                                                                |
| `/F=`     | `%i` **Druckhöhe in Metern**, kein Druck                      | `%d` in `qfe`                     | bewusst nur `extras["F"]`                 | `alt_press`, mit Kommentar "sollte QFE sein"  | Semantik nur in `extern_tele_json.h:12-17` dokumentiert; App-Kommentar ist falsch         |
| `/Q=`     | `%.1f`, nicht bei MCU811/BME680                               | `%f`                              | `qnh`, geparst, **nie gespeichert**       | `qnh`                                         | MCProxy verliert den Wert (nur für abgeleitetes QFE genutzt)                              |
| `/G=`     | `%.1f` BME680                                                 | `%f`                              | `gas`                                     | `gas_res`                                     | konsistent                                                                                |
| `/C=`     | `%.0f`                                                        | `%f`                              | `co2`                                     | `co2`                                         | konsistent                                                                                |
| `/V=`     | 2 (MCU811), 3 (BME680), 5 (INA226)                            | `%i`                              | `extras["V"]` untypisiert                 | geparst in `data_vers_`, dann **verworfen**   | Bedeutung der Werte nirgends dokumentiert                                                 |
| `/N<n>`   | `"/N%i"`, **ohne `=`**, bis 99                                | `/N` + Ziffer 1..9, bis 3 Stellen | **verworfen** (Regex verlangt `=`, RX-05) | nur **eine** Ziffer (`slice(0,2)`), `N12` → 1 | MCProxy und App verlieren das Feld, nur die Firmware liest es korrekt                     |
| `/R=`     | `"grp;"` wiederholt, bis 6 Gruppen                            | **nicht dekodiert**               | `group_0..5`, **kein Test**               | `groups`, `;` → `,`                           | Firmware-eigener Decoder kennt den Schlüssel nicht                                        |
| `/Y=1`    | bei Telemetrie-Beacon                                         | `%i`, Puffer nicht zurückgesetzt  | `extras["Y"]`                             | ignoriert                                     | Decoder-Bug: erbt den `/V=`-Puffer (`aprs_functions.cpp:999-1022`)                        |
| `/D=`     | 8 Bits, **fork-only** (`b179fdff`)                            | exakt 8 Zeichen                   | `extras["D"]` als **float** (RX-12)       | **fehlt**                                     | Bitstring `00000101` wird zu 101.0                                                        |
| `/U=`     | `%.2f` INA226 Bus-Spannung                                    | **nicht dekodiert**               | `extras["U"]`                             | **fehlt**                                     |                                                                                           |
| `/I=`     | `%.1f` INA226 Strom                                           | **nicht dekodiert**               | `extras["I"]`                             | **fehlt**                                     |                                                                                           |

Zusatzbefunde am Encoder, beide verifiziert:

- Alle acht NaN-Guards (`loop_functions.cpp:4355-4408`) prüfen `cpress` statt des eigenen
  Puffers. Ein NaN in `/H= /T= /O= /F= /Q= /G= /C=` geht ungefiltert auf die Luft.
- Vier Puffer `csfpegel`, `csfpegel2`, `csftemp`, `csfbatt` (`:4259-4262`) werden
  konkateniert, aber nie beschrieben.

## 3. Drift-Matrix Rahmen und Trailer

| Feld                      | Firmware                                                       | MCProxy BLE                                                          | App                                                                                                    |
| ------------------------- | -------------------------------------------------------------- | -------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------ |
| `msg_id`                  | little-endian (`aprs_functions.cpp:1097`)                      | `<I` little-endian                                                   | **`getUint32(2, false)` big-endian** (`MessageHandler.ts:109`)                                         |
| Byte 5 Flags              | 0x0F hop, 0x10 mesh, 0x20 app_offline, 0x40 track, 0x80 server | alle fünf extrahiert                                                 | nur 0x20 (Notify-Logik invertiert wirkend, `:302-314`)                                                 |
| Pfad `A,B>`               | `src_call` = erstes Element                                    | `split_path`: eigenes Call entfernt, `src` = erstes                  | `>` gesucht, letztes Komma-Element = Ziel (`:236-291`)                                                 |
| Symbol-Tabelle / -Zeichen | `node_symid` zwischen lat/lon, `node_symcd` nach lon           | Regex `[/\\0-9A-Z]` + `[ -~]?`, Overlay erlaubt                      | Tabelle nur als `split()`-Trenner, **Symbolzeichen nie extrahiert**                                    |
| Kommentar `atxt`          | 25 Byte, CHR-02-gefiltert, vor `#name` und Tags                | kein Kommentarfeld, Regex überspringt                                | `slice(19, erstes /)`; **Telemetrie ab Offset 20 fest**, Kommentar wird als Schlüssel geparst (`:657`) |
| HW                        | Byte nach `0x00`                                               | Footer `<BBBHBBBB`, positional                                       | Byte nach `0x00`, `hwtable[]` mit Lücken 13..38 → `undefined`                                          |
| MOD                       | `(mod & 0x0F) \| (country << 4)`; MHeard setzt `\| 0xF0`       | `& 0x0F`, Country bewusst nicht dekodiert                            | **nicht gelesen**; `modtable` importiert, ungenutzt                                                    |
| FCS                       | Bytesumme big-endian                                           | `<H` + Swap, Mismatch nur WARNING                                    | **nicht geprüft**                                                                                      |
| FW / LH / Sub / 0x7E      | optional, `inext < rsize`                                      | positional; nRF52-Rahmen 252..254 Byte lesen Footer aus Text (RX-06) | **nicht gelesen**                                                                                      |
| BLE-Zeitstempel (4 B BE)  | immer angehängt außer bei `D{`                                 | immer gelesen, Plausibilitätsfenster                                 | nur wenn Via-Pfad vorhanden (`:206-218`); Epoch-Floor 1. Feb 2024 hart                                 |
| ACK 0x41                  | `[6]` Status, `[7]` n, Callsign-Appendix (fork)                | Appendix geparst, Regex validiert                                    | `[6]` Status, Appendix **nicht gelesen**; DB-Match nur auf `msgNr`                                     |
| `{nnn`-Suffix (DM-Ack)    | ohne schließende Klammer                                       | `\{[0-9]+$`                                                          | nur Echo-Strip bei eigener DM (`:557`)                                                                 |
| `T#`-Telemetrie           | als `:`-Text an Gruppe 100001                                  | **unerreichbar** (RX-02), landet als Chat                            | landet als Chat                                                                                        |
| `#TAG`-Hashtag            | nicht implementiert, `{#TAG}` wird DM an Fantasie-Call         | sendet `{#TAG}text` heute                                            | keinerlei Hashtag-Logik                                                                                |
| HEY `R<n>;n,rssi,snr;`    | RSSI positiv                                                   | negiert, 0/2 Kommas                                                  | nicht behandelt (`0x40`-Subtyp fällt durch)                                                            |

## 4. Drift der Contract-Dokumente

Das vollständigste Dokument ist `docs/architecture/11-wire-format.md` (2026-08-21, gegen
4.35p). Es deckt Envelope, Flags, FCS, Trailer, UDP-1990, EXTUDP-JSON, BLE-Register und
BLE-Binärrahmen ab, mit Korpus-Absicherung. Es fehlt:

1. Der komplette `/X=`-Schlüsselraum. Null Vorkommen von `/A=` bis `/Y=`.
2. Die Positionsgrammatik als Regel, nur ein Hex-Beispiel in §1.6.
3. Symbol-Overlays und das doppelte `\`-Escaping auf EXTUDP (nur in
   `MCProxy/doc/aprs-escape-bug.md`).
4. MOD-Nibble-Semantik mit Country-Tabelle und der `0xF0`/`PL`-Kollision (nur in
   `MCProxy/doc/2026-08-28_1700-firmware-mod-nibble-handover.md`).
5. Die HW-ID-Tabelle (nur in `README.md`).
6. Alles seit 4.35p: `/D=`, ACK-Appendix, `MH.SRC/GW/PP` hin und wieder zurück, `I.FWDATE`
   hin und zurück, `--maxhop`, Backpressure-Notices.

Widersprüche zwischen Dokumenten, die ein Contract auflösen muss:

| Thema                 | Dokument A                                                 | Dokument B                                                      | Wahr ist                                                         |
| --------------------- | ---------------------------------------------------------- | --------------------------------------------------------------- | ---------------------------------------------------------------- |
| `/A=` Einheit, Breite | `README.md:94` "Meter, 0-9999, `HHHH`"                     | MCProxy ADR 2026-02-14 "Fuß"; Parity-Audit "6-stellig Fuß"      | 6-stellig Fuß (`loop_functions.cpp:4347`)                        |
| Hop-Maske             | `README.md` 0x07                                           | `MeshCom-ACK.md` 0x7F; `11-wire-format.md` 0x0F                 | 0x0F                                                             |
| Byte-5 Bit 0x40       | `README.md` "Pfad einfügen"                                | `11-wire-format.md` `msg_track`                                 | `msg_track`                                                      |
| BLE-ACK-Länge         | `MeshCom-ACK.md` 12 Byte                                   | `11-wire-format.md`, `ack-wer-hat-quittiert.md` 13 Byte         | 13                                                               |
| ACK-Status-Werte      | `MeshCom-ACK.md` 0x00/0x01                                 | `11-wire-format.md`, `ack_attribution.h` 0x00/0x01/0x02         | drei Werte                                                       |
| Marker-Liste CHR-02   | `BACKLOG.md` §CHR-02 ohne `/D= /I= /U= /R=`                | `mcp23017-digital-field.md` Buchstabentabelle inkl. dieser vier | 17 Schlüssel, siehe §2                                           |
| Tele-JSON-Schlüssel   | `ext_udp_telemetry.md` eingehend `temp`, `gasres`, `press` | `MCProxy/doc/telemetry.md` ausgehend `temp1`, `gas`, `qfe`      | beides richtig, zwei Richtungen, nirgends als Asymmetrie benannt |
| Symbol-Tabellen-IDs   | `11-wire-format.md` §4.4 "nur `/` und `\`"                 | `aprs-escape-bug.md` "Empfang: `/ \ 0-9 A-Z`"                   | Schreiben 0x95 nur `/ \`; Empfang mit Overlay                    |

Die App bringt kein einziges Dokument mit. Ihr Anteil am Contract ist ausschließlich aus
`MessageHandler.ts` lesbar, und die dort seit Monaten stehenden Beispielrahmen in Kommentaren
sind der einzige Fixture-Bestand: null Unit-Tests für `parseMsg`, die Cypress-Suite prüft noch
das Ionic-Starter-Template.

## 5. Was daraus folgt

Reihenfolge nach Hebel, nicht nach Aufwand:

1. **Contract zuerst.** `11-wire-format.md` um einen §1.8 "Positions-Kommentar" erweitern:
   Grammatik, die 17-Schlüssel-Tabelle aus §2 mit Format, Einheit, Bedingung, Reihenfolge,
   100-Byte-Budget und Drop-Reihenfolge, `/N` ohne `=`, `/V=`-Werte, `/F=`-Semantik. Dazu
   README-Fehler korrigieren und die CHR-02-Liste in `BACKLOG.md` angleichen. Ohne diesen
   Schritt reparieren die beiden Consumer gegen Vermutungen.
2. **Firmware-Decoder nachziehen** (`/R= /U= /I=` fehlen, `/Y=`-Pufferreset, NaN-Guards).
   Klein, upstream-fähig, jeweils mit Regressionstest im `native_aprs`-Korpus.
3. **MCProxy**: `/N<n>` (RX-05), `/D=` als Bitstring (RX-12), `/Q=` speichern, `T#`
   erreichbar machen (RX-02), Fixtures für `/R= /V= /D= /U= /I= /N`. Das Parity-Audit vom
   2026-09-10 hat die Liste bereits.
4. **App-PR** ist der größte Block: `msg_id` little-endian, Symbolzeichen extrahieren und auf
   der Karte rendern, Telemetrie-Parsing nach dem Symbolzeichen statt ab Offset 20, `/N`
   mehrstellig, `/D= /U= /I=` aufnehmen, Zeitstempel auch ohne Via-Pfad, MOD/FW/LH lesen,
   `hwtable`-Fallback, `default`-Zweige mit Logging, und ein Vitest-Fixture-Satz aus den
   Korpus-Rahmen der Firmware. Die App gehört OE1KFR; der PR braucht eine englische
   Beschreibung und sollte das Parser-Modul aus `MessageHandler.ts` herauslösen, damit es
   testbar wird.

Nicht Teil dieser Analyse: der Stack-Overflow-Kandidat `cConcat1[255]` bei `rsize` bis 340
(`aprs_functions.cpp:365-379`) ist als offener Lead bereits bekannt und gehört in die
Defekt-Liste, nicht in den Parser-Contract.
