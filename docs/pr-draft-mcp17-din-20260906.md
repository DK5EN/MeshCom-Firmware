# PR-Entwurf: MCP23017 Port-A-Eingaenge als `/D=` in der Positionsbake und im `T#`-Digitalfeld (Issue 1076)

**Nicht via `/submit-pr` erzeugt** (kein `gh`-Aufruf, kein PR-Branch geschnitten). Dieser
Text ist die Vorlage fuer den PR-Cut gegen `upstream/dev`; Format nach `submit-pr`.
Fork-Commit: `b179fdff`, Changelog-Eintrag 207.

---

## Titel

`feat(tele): MCP23017 Port-A-Eingaenge als /D= in der Positionsbake und im T#-Digitalfeld (#1076)`

## Dateien dieses PR-Cuts

Nur `src/`, kein Test-, Tool- oder Doku-Delta:

```
src/mcp17_bits.h              (neu)
src/loop_functions.cpp
src/aprs_structures.h
src/aprs_functions.cpp
src/extern_tele_json.h
src/extudp_functions.cpp
```

`platformio.ini` (native `test_filter`-Env), `test/test_mcp17_bits/`, die Ergaenzungen in
`test/test_decodeaprspos/` und `test/test_extern_tele_json/` sowie `docs/ext_udp_telemetry.md`
existieren nur im Fork und werden beim Cut verworfen.

## Ausgangslage

Issue 1076 (DB1MIC, 2026-08-03): die APRS-Telemetrie sendet das Digitalfeld mit, aber es ist
immer leer -- gewuenscht ist, Eingaenge darauf abzubilden, z. B. Port A des MCP23017. Eine
zweite Anfrage aus dem Feld will dieselben acht Bits zusaetzlich in der Positionsbake, damit sie
auch ohne APRS-Telemetrie-Umweg beim MeshCom-Server und in den Apps ankommen.

Stand vor diesem PR: `loopMCP23017()` liest alle 16 Pins alle 5 s nach
`meshcom_settings.node_mcp17in`, aber niemand ausser `--io`/`--info` und der Web-GUI konsumiert
den Wert. In `sendTelemetry()` steht das Digitalfeld als Literal `00000000` im `T#`-Frame.

## Was wurde geaendert

### `src/mcp17_bits.h` (neu)

Arduino-freier Header, eine `static inline`-Funktion:

```
void mcp17PortABits(uint16_t in, uint16_t io_mask, char out[9])
```

Formt das Low-Byte von `node_mcp17in` zu einem String aus acht `'0'`/`'1'`-Zeichen, NUL-terminiert.
Reihenfolge nach APRS-BITS-Konvention: `out[0]` ist GPA0, `out[7]` ist GPA7. Pins, die in
`node_mcp17io` als OUTPUT markiert sind, liefern immer `'0'` (ein veraltetes Eingangswort nach
`--setio` kann so nicht in den Frame durchschlagen). Port B (Bits 8-15) wird ignoriert.

Beide Sender unten benutzen diese eine Funktion, damit Bake und `T#`-Frame garantiert denselben
String tragen und niemand zwei Bit-Reihenfolgen erklaeren muss.

### `src/loop_functions.cpp`

- `PositionToAPRS()`: neuer Puffer `cdigital[15]`. Wenn `bMCP23017` gesetzt ist (der Chip hat
  beim Boot auf `begin_I2C(0x20)` geantwortet), wird `/D=<8 Bits>` gebildet und in der
  `strconcat`-Kette direkt nach `/I=` (INA226-Strom) und vor `/Y=` angehaengt, mit demselben
  laengenbegrenzten `strncat`-Muster wie alle anderen Felder. Der Block steht ausserhalb der
  `node_parm`/`bINA226ON`-Verzweigung, wird also unabhaengig vom INA-Zustand gesendet.
- `sendTelemetry()`, Zweig `iNextTelemetry >= 4`: der Bit-String wird einmal am Zweiganfang
  gebildet, Vorbelegung `"00000000"`, bei `bMCP23017` ueberschrieben.
  - `T:`-Pfad (Werte aus `node_values` mit `T:`-Praefix): das Literal `,00000000,` wird durch
    `,<bits>,` ersetzt.
  - `node_values`-Listenpfad: dieser Pfad hat bisher gar kein Digitalfeld gesendet. Nur wenn der
    Chip vorhanden ist, werden die Analogslots auf fuenf aufgefuellt (`,0`) und danach `,<bits>`
    angehaengt -- APRS erwartet das Digitalbyte in Slot 6, sonst wuerde ein Knoten mit drei
    konfigurierten Werten seine Bits als Analogwert 4 verschicken.
- Die `BITS.`-Definitionszeile bleibt unveraendert: sie beschreibt laut APRS-Spezifikation den
  Bit-Sinn (welcher Pegel "aktiv" ist), nicht die Werte.

Knoten ohne MCP23017 erzeugen byte-identische Frames wie bisher, in beiden Pfaden.

### `src/aprs_structures.h`, `src/aprs_functions.cpp`

- `struct aprsPosition` bekommt `char din[9]`; `initAPRSPOS()` setzt es auf `""`.
- `decodeAPRSPOS()` bekommt eine weitere Token-Schleife nach dem Muster der bestehenden
  (`/P=`, `/H=`, ...): sucht `/D=`, kopiert bis `/`, Leerzeichen oder Ende, hoechstens acht
  Zeichen. Akzeptiert wird nur ein Token aus exakt acht Zeichen `'0'`/`'1'`; 7 oder 9 Zeichen,
  Buchstaben oder ein leeres Token lassen `din` leer. Ein neuntes Datenbyte setzt ein
  Overflow-Flag statt `decode_text` zu ueberschreiben.
- Aeltere Firmware kennt `/D=` nicht und ueberspringt es, weil jede Token-Schleife nur ihr
  eigenes Feld sucht. Keine Aenderung an `/V=`.

### `src/extern_tele_json.h`, `src/extudp_functions.cpp`

- `externTeleJsonNode()` und `externTeleJsonLora()` bekommen einen letzten Parameter
  `const char *din`. Der Schluessel `"din"` wird nur geschrieben, wenn der String nicht leer ist;
  fuer Sender ohne MCP23017 bleibt das JSON byte-identisch.
- `sendExtern()`: fuer `src_type "node"` kommt `din` aus `mcp17PortABits()` des eigenen Chips,
  fuer `src_type "lora"` aus `aprspos.din` des empfangenen Frames.

## Warum `/D=` und nicht `/I=`

Die Feldanfrage schlug `/I=11111111` vor. `/I=` traegt aber bereits den INA226-Strom in Ampere
(`PositionToAPRS()`, Block `/V=5`); ein Bit-String darin haette jeden Parser gebrochen, der das
Feld heute liest. Belegt sind A, B, C, F, G, H, I, N, O, P, Q, R, T, U, V, Y; frei waren D, E, J,
K, L, M, S, W, X, Z. `D` fuer "digital" ist das einzige mit offensichtlicher Eselsbruecke.

## Kosten

- 11 Byte pro Positionsbake, nur bei Knoten mit erkanntem MCP23017. Alle anderen: 0 Byte.
- `T#`-Frame: 0 Byte im `T:`-Pfad (Literal ersetzt), im Listenpfad bis zu 13 Byte, nur mit Chip.
- RAM: 9 Byte in `struct aprsPosition` (Stack), sonst nur lokale Puffer.
- Kein neues Setting, kein `FLASH_STRUCT_VERSION`-Bump: die Aussendung haengt allein am
  Vorhandensein des Chips.

## Was der Server und die Apps davon haben

Der MeshCom-Server und die offizielle App kennen `/D=` noch nicht; die Bake wird bei ihnen
unveraendert angezeigt, das Feld ignoriert. Damit die Bits auf der Karte erscheinen, muss der
Server das Feld lernen -- das ist der eigentliche Zweck dieses PRs: das Format festzulegen, bevor
Firmware im Feld es sendet. Auf aprs.fi erscheinen die Bits ueber den `T#`-Pfad sofort, ohne
weitere Aenderung.

## Bewusst nicht gemacht

- Keine Sofortbake bei Pegelwechsel. Die Bits laufen im normalen Bakenintervall mit; die
  Sendelast bleibt berechenbar.
- Port B wird nicht gesendet. Falls gewuenscht, spaeter ein zweiter Buchstabe oder ein
  16-stelliger String -- beides abwaertskompatibel.
- Kein Ein/Aus-Schalter: ein neues Setting haette auf nRF52 einen Struct-Bump und damit einen
  Konfigurations-Wipe der Flotte bedeutet.

## Tests

Nur im Fork, nicht Teil des Cuts:

- `test/test_mcp17_bits` (8 Faelle): Reihenfolge GPA0-zuerst, Output-Maske, Port B ignoriert,
  gemischte Muster.
- `test/test_decodeaprspos` (+6 Faelle): `/D=` zwischen `/A=` und `/N`, fehlend, 7 und 9
  Zeichen, Buchstabe, letztes Token ohne Schlussstrich, direkt nach dem Symbol bei leerem
  Kommentar. Bestehende Vektoren unveraendert gruen.
- `test/test_extern_tele_json` (+5 Faelle): Schluessel vorhanden/fehlend fuer beide Shapes,
  `nullptr` sicher.
- Native Envs gesamt: `native` 296/296, `native_parsers` 36/36, `native_extern` 32/32.
- Saubere Builds: `heltec_wifi_lora_32_V3`, `wiscore_rak4631`, `E22_XML-DevKitC`.

## Hardware-Nachweis

- `DK5EN-93` (Heltec V3, Messbuild mit `INSTRUMENT_ENABLED=1`): Korpus-Frame f003 mit
  eingefuegtem `/D=01001100` und neu berechneter FCS per `--injectraw` durch den echten
  RX-Pfad (`OnRxDone` -> `decodeAPRS` -> `decodeAPRSPOS` -> `sendExtern`). Am UDP-Listener
  kam `{"type":"tele",...,"din":"01001100"}` an; ein echter On-Air-Frame ohne `/D=` in derselben
  Minute trug keinen `din`-Schluessel. Mesh und Gateway waren fuer den Versuch aus, damit der
  synthetische Frame den Knoten nicht verlaesst.
- Der Sendepfad (`PositionToAPRS()`, `sendTelemetry()`) ist nur nativ ueber den Formatter
  belegt: auf der Bench liegt kein MCP23017. Wer einen hat: `--setio` einen Pin als Eingang
  lassen, auf Masse ziehen, naechste Bake abwarten, `/D=` muss an dieser Stelle `0` zeigen.

## Hinweise fuer den Reviewer

- Bit-Reihenfolge ist eine Festlegung, keine Notwendigkeit: GPA0 zuerst, damit `T#`-Slot und
  Bake denselben String tragen. Wenn der Server-Parser eine andere Reihenfolge braucht, bitte
  vor dem Merge sagen -- danach ist sie im Feld.
- Der `node_values`-Listenpfad in `sendTelemetry()` hat vorher kein Digitalfeld gesendet; die
  Auffuellung auf fuenf Analogslots passiert nur mit Chip. Ohne Chip ist der Frame bytegleich.
