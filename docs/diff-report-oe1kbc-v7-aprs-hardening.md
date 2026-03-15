# Diff-Report v7: Vorschlag APRS-Parser Hardening fuer v4.35p

Erstellt: 2026-03-15 20:15

Author: Martin (DK5EN)

Bezug: `upstream/oe1kbc_v4.35p` (Stand f94b675)

---

## BLUF (Bottom Line Up Front)

**Anlass:** OE1KBC-12 (RAK/ESP32, FW 4.35p) ist am 15.03.2026 nach Empfang eines korrupten RF-Pakets abgestuerzt. OE3MAG-12 war ebenfalls betroffen. Log-Analyse zeigt 488ms APRS-Verarbeitungszeit statt normal 1-9ms. Der APRS-Parser hat das Paket zwar korrekt abgelehnt, aber vorher ca. 250 Bytes Muell-Daten per `String::concat()` verarbeitet — Heap-Stress auf dem ESP32.

**4 Verbesserungsvorschlaege:**

| # | Verbesserung | Schwere | Dateien |
|---|-------------|---------|---------|
| 1 | Source-Path-Schleife: Laengenlimit + Zeichenvalidierung | hoch | aprs_functions.cpp |
| 2 | Destination-Path-Schleife: Laengenlimit + Zeichenvalidierung | hoch | aprs_functions.cpp |
| 3 | Buffer-Overread bei FW-Sub-Version (fehlender Bounds-Check) | mittel | aprs_functions.cpp |
| 4 | printAsciiBuffer: Bounds-Check vor Zugriff auf buffer[1..3] | niedrig | loop_functions.cpp |

---

## Hintergrund: Das korrupte Paket

Letztes empfangenes Paket vor dem Absturz:
```
@h....OE1FUC-13,OE3MAG-12,OE3CZC-##.................Ԁ.............
```

Das Paket enthielt gueltige Callsigns (`OE1FUC-13`, `OE3MAG-12`), aber ab `OE3CZC-` waren die Daten korrupt — `##` statt SSID, kein `>` Trennzeichen. Die Source-Path-Schleife in `decodeAPRS()` hat fuer jedes Byte ein `String::concat()` ausgefuehrt, bis `rsize` erreicht war.

```
Normales Paket:   ONRXDONE_TIME = 1-9ms
Korruptes Paket:  ONRXDONE_TIME = 488ms  (einziger Ausreisser in 6h Log)
Danach:           Keine weiteren Log-Eintraege — Node weg
```

---

## TEIL 1: Verbesserungsvorschlaege

---

### 1. Source-Path-Schleife: Laengenlimit + Zeichenvalidierung

**Problem:** Die Schleife in `decodeAPRS()` iteriert von Byte 6 bis `rsize` und sucht das `>` Trennzeichen. Bei korrupten Paketen ohne `>` laeuft sie durch den gesamten Restbuffer (bis 249 Bytes). Fuer jedes Byte wird `String::concat()` aufgerufen — das allokiert auf dem ESP32-Heap und kann bei Muell-Daten zu Fragmentierung und Absturz fuehren.

**Vorschlag:** Maximale Source-Path-Laenge auf 120 Bytes begrenzen (8 Callsigns a 10 Zeichen + Kommas = ~88, grosszuegig aufgerundet) und Non-Printable-Bytes sofort abbrechen. Der bestehende `bSourceEndOk`-Check verwirft das Paket dann automatisch.

#### `src/aprs_functions.cpp`, Zeile 179

```cpp
// AKTUELL bei Kurt:
        for(ib=6; ib < rsize; ib++)
        {
            if(RcvBuffer[ib] == '>')
            {
                inext=ib+1;
                bSourceEndOk=true;
                break;
            }
            else
            {
                aprsmsg.msg_source_path.concat((char)RcvBuffer[ib]);

// VERBESSERUNGSVORSCHLAG:
        for(ib=6; ib < rsize && (ib - 6) < 120; ib++)
        {
            if(RcvBuffer[ib] == '>')
            {
                inext=ib+1;
                bSourceEndOk=true;
                break;
            }
            else
            {
                if(RcvBuffer[ib] < 0x20 || RcvBuffer[ib] > 0x7E)
                    break;

                aprsmsg.msg_source_path.concat((char)RcvBuffer[ib]);
```

**Aenderung:**
- Schleifenbedingung: `&& (ib - 6) < 120` begrenzt auf maximal 120 Bytes
- Neuer Guard im else-Block: Bytes ausserhalb 0x20-0x7E (druckbares ASCII) brechen sofort ab
- Rest des Schleifenkoerpers bleibt unveraendert

**Auswirkung auf normale Pakete:** Keine. Der laengste beobachtete Source-Path im 6h-Log hatte 5 Callsigns (~55 Bytes). 120 Bytes ist weit darueber.

---

### 2. Destination-Path-Schleife: Laengenlimit + Zeichenvalidierung

**Problem:** Identische Struktur wie die Source-Path-Schleife. Sucht `payload_type` statt `>`, iteriert ebenfalls bis `rsize` mit `concat()` pro Byte.

**Vorschlag:** Gleiche Absicherung wie bei Vorschlag 1.

#### `src/aprs_functions.cpp`, Zeile 254

```cpp
// AKTUELL bei Kurt:
        for(ib=inextstart; ib < rsize; ib++)
        {
            if(RcvBuffer[ib] == aprsmsg.payload_type)
            {
                inext=ib+1;
                bDestinationEndOk=true;
                break;
            }
            else
            {
                aprsmsg.msg_destination_path.concat((char)RcvBuffer[ib]);

// VERBESSERUNGSVORSCHLAG:
        for(ib=inextstart; ib < rsize && (ib - inextstart) < 120; ib++)
        {
            if(RcvBuffer[ib] == aprsmsg.payload_type)
            {
                inext=ib+1;
                bDestinationEndOk=true;
                break;
            }
            else
            {
                if(RcvBuffer[ib] < 0x20 || RcvBuffer[ib] > 0x7E)
                    break;

                aprsmsg.msg_destination_path.concat((char)RcvBuffer[ib]);
```

**Aenderung:** Analog zu Vorschlag 1. Der bestehende `bDestinationEndOk`-Check verwirft das Paket bei fruehzeitigem Abbruch.

---

### 3. Buffer-Overread bei FW-Sub-Version (fehlender Bounds-Check)

**Problem:** Nach dem Parsen von FW-Version und Hardware-Byte wird `RcvBuffer[inext]` gelesen, ohne zu pruefen ob `inext` noch innerhalb von `rsize` liegt. Die Zeilen direkt darueber (391-401) haben den Check `if(inext < rsize)`, aber Zeile 403 nicht — vermutlich vergessen.

Bei kurzen oder abgeschnittenen Paketen liest das ein Byte hinter dem gueltigen Buffer.

#### `src/aprs_functions.cpp`, Zeilen 403-416

```cpp
// AKTUELL bei Kurt:
        if(RcvBuffer[inext] == 0x7e)
        {
            aprsmsg.msg_source_fw_sub_version = '#';
            inext++;
        }
        else
        {
            if(RcvBuffer[inext] == 0x00)
                aprsmsg.msg_source_fw_sub_version = '#';
            else
                aprsmsg.msg_source_fw_sub_version = RcvBuffer[inext];
            inext++;

        }

// VERBESSERUNGSVORSCHLAG:
        if(inext < rsize && RcvBuffer[inext] == 0x7e)
        {
            aprsmsg.msg_source_fw_sub_version = '#';
            inext++;
        }
        else if(inext < rsize)
        {
            if(RcvBuffer[inext] == 0x00)
                aprsmsg.msg_source_fw_sub_version = '#';
            else
                aprsmsg.msg_source_fw_sub_version = RcvBuffer[inext];
            inext++;
        }
```

**Aenderung:** `inext < rsize` Guard eingefuegt, analog zu den Guards in Zeilen 391 und 397. Wenn `inext >= rsize`, wird `msg_source_fw_sub_version` einfach nicht gesetzt (bleibt Default).

---

### 4. printAsciiBuffer: Bounds-Check vor Zugriff auf buffer[1..3]

**Problem:** Die Debug-Funktion `printAsciiBuffer()` greift in der ersten Zeile auf `buffer[0]`, `buffer[1]`, `buffer[2]`, `buffer[3]` zu, ohne vorher zu pruefen ob `len >= 4`. Bei sehr kurzen Paketen (die den `rsize < 16`-Check in decodeAPRS nicht erreichen) koennte das zu einem Out-of-Bounds-Read fuehren.

#### `src/loop_functions.cpp`, Zeile 2103

```cpp
// AKTUELL bei Kurt:
void printAsciiBuffer(uint8_t *buffer, int len)
{
    if(buffer[0] != 0x21 && buffer[0] != 0x3A && buffer[0] != 0x40 && buffer[0] != 0x41)
    {
        Serial.printf("LoRa starting with 0x%02X and %02X%02X%02X ... no decode\n",
                       buffer[0], buffer[1], buffer[2], buffer[3]);

// VERBESSERUNGSVORSCHLAG:
void printAsciiBuffer(uint8_t *buffer, int len)
{
    if(len < 4)
        return;

    if(buffer[0] != 0x21 && buffer[0] != 0x3A && buffer[0] != 0x40 && buffer[0] != 0x41)
    {
        Serial.printf("LoRa starting with 0x%02X and %02X%02X%02X ... no decode\n",
                       buffer[0], buffer[1], buffer[2], buffer[3]);
```

**Aenderung:** `if(len < 4) return;` als erstes Statement. Debug-Ausgabe fuer zu kurze Buffers wird uebersprungen.

---

## TEIL 2: Zusammenfassung der Aenderungen

| Datei | Zeilen | Aenderung |
|-------|--------|-----------|
| `src/aprs_functions.cpp` | 179 | Source-Path-Schleife: `(ib - 6) < 120` + Non-Printable-Guard |
| `src/aprs_functions.cpp` | 254 | Dest-Path-Schleife: `(ib - inextstart) < 120` + Non-Printable-Guard |
| `src/aprs_functions.cpp` | 403-416 | `inext < rsize` Guard vor FW-Sub-Version-Zugriff |
| `src/loop_functions.cpp` | 2103 | `len < 4` Guard in printAsciiBuffer |

**Nicht geaendert:**
- Payload-Schleife (Zeile 329): bereits durch `ib < rsize` begrenzt, Payload darf Sonderzeichen enthalten
- FCS-Pruefung: kann nicht vorverlegt werden, Offset haengt von geparsten Feldern ab
- String-Klasse: kein Umbau auf Fixed-Buffer (waere grosses Refactoring)

**Kompiliert erfolgreich** fuer alle Targets (getestet: heltec_wifi_lora_32_V3).

---

## TEIL 3: Erwartete Wirkung

Mit diesen Verbesserungen haette das korrupte Paket vom 15.03.2026 folgendes Verhalten:

1. Source-Path-Schleife beginnt bei Byte 6, liest `OE1FUC-13,OE3MAG-12,OE3CZC-`
2. Bei den Null-Bytes nach `OE3CZC-` greift der Non-Printable-Guard (`< 0x20`) → **sofortiger Abbruch**
3. `bSourceEndOk` bleibt `false` → Paket wird verworfen
4. Geschaetzte Verarbeitungszeit: **<1ms** statt 488ms
5. Kein Heap-Stress, kein Absturz
