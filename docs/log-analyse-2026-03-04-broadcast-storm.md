# Log-Analyse DJ8MEH-43: Broadcast-Storm im 9-Knoten-Mesh

**Datum:** 2026-03-04
**Log:** PuTTY-Mitschnitt von DJ8MEH-43 (Heltec V2), 13:31 - 14:13 (41 Min)
**Aufbau:** 9 DJ8MEH-Knoten, alle Mesh aktiv, DJ8MEH-8 = einziges Gateway

---

## 1. Zusammenfassung

Das Mesh leidet unter einem **klassischen Broadcast-Storm**. Jede einzelne Nachricht
wird im Schnitt **10x empfangen** (statt idealerweise 2-3x). Die Ursache ist eine
Kombination aus:

1. **Fehlende Loop-Detection** -- Knoten relayen Pakete, in deren Pfad sie bereits stehen
2. **Dedup-Puffer-Ueberlauf** -- 30 Slots reichen nicht, bereits verarbeitete Nachrichten
   werden nach Eviction als "neu" erkannt und erneut gerelayed
3. **Hop-Counter-Unterlauf** -- Nachrichten mit H0F (15 dezimal) im Log belegen, dass der
   Hop-Counter unter 0 laeuft (uint8_t Wrap: 0 -> 255 -> encodeAPRS -> 0x0F)

**Ergebnis:** Kanalauslastung 65% Durchschnitt / 100% Spitzen, 45 Relay-Drops,
33 Ringpuffer-Ueberlaeufe, Buffer voll auf 28/30 Slots. Keine Abstuerze.

---

## 2. Report (analog serial_monitor.py)

```
============================================================
LOG REPORT  13:31 - 14:13 (41 min)
============================================================
RX:  487 Pakete (MH-LoRa), 17 CRC-Errors
TX:   59 Pakete, 0 Timeouts

Unique Message-IDs:     48
Amplifikationsfaktor:   10.14x (487 / 48)

Drops:  RELAY_DROPPED=45  RING_OVERFLOW(phone)=33
CAD false pos: 0 | Retransmit fails: 0

Channel:  min=22%  avg=65%  max=100%  (27x bei 100%)
Ring:     queued max=28 (Pufferlimit!)

ACK Dedup:  TX-ACK-Fast=90  NODE_ACK_QUEUED=25
            Kein ACK-Storm (max 2 ACKs/msg_id)

Errors:   CRC=17  Crashes=0  Watchdog=0  Panic=0  Reset=0
WiFi:     n/a (kein WiFi auf Heltec V2)
NTP:      n/a
============================================================
  TOTALS: RX=487 TX=59 Errors=0 Drops=45
  RadioSilent=0 (keine Luecken >60s)
============================================================
```

---

## 3. Gesehene Knoten

| Callsign   | Als Originator | Relay-Erwaehnungen | Typ             |
|------------|---------------:|--------------------|-----------------|
| DJ8MEH-46  |            233 | 358                | Heltec V2 (Sender via UDP) |
| DJ8MEH-44  |             59 | 150                | Mesh-Knoten     |
| DJ8MEH-1   |             54 | 148                | Mesh-Knoten     |
| DJ8MEH-8   |             52 | 282                | Gateway (Heltec V3) |
| DJ8MEH-81  |             50 | 250                | Mesh-Knoten     |
| DJ8MEH-43  |             48 | 132                | Aufzeichnender Knoten |
| DJ8MEH-41  |             37 | 206                | Mesh-Knoten     |
| DJ8MEH-82  |             35 | 156                | Mesh-Knoten     |
| DJ8MEH-45  |             17 | 395                | E22, haeufigster Relay-Knoten |
| OE1XAR-45  |              - | 61                 | Extern (CET Zeitserver) |
| OE1EZA-12  |              - | wenige             | Extern via GW   |
| OE5PYB-1   |              - | wenige             | Extern via GW   |

**9 lokale + 3 externe Knoten.**  DJ8MEH-45 taucht am haeufigsten als Relay auf (395x).

---

## 4. Nachrichtentypen, Laengen, Flags

### Nachrichtentypen

| Typ            | Symbol | Gesamt | TX | RX |
|----------------|--------|-------:|---:|---:|
| Status/Beacon  | `@`    |    352 | 28 | 324 |
| Text Message   | `:`    |    293 | 26 | 267 |
| Position       | `!`    |     46 |  5 |  41 |
| **Summe**      |        | **691**| 59 | 632 |

### Nachrichtenlaengen

| Min | Max | Durchschnitt | Haeufigste |
|----:|----:|-------------:|-----------|
| 27 B | 148 B | 69 B | 57 B (62x), 43 B (61x) |

Laenge waechst mit jedem Relay-Hop (~10 Byte pro Callsign im Pfad).

### Flags

| Flag | Wert | Anzahl | Bedeutung |
|------|------|-------:|-----------|
| S    | S0   |    475 | Nicht durch Gateway gelaufen |
| S    | S1   |    216 | ServerFlag gesetzt (via DJ8MEH-8) |
| T    | T0   |    691 | Alle identisch |
| M    | M01  |    691 | Mesh aktiv auf allen Knoten |

---

## 5. Hop Count Verteilung

| Hop | Anzahl | Bewertung |
|-----|-------:|-----------|
| H00 |     94 | Normal (Hop-Budget aufgebraucht) |
| H01 |    171 | Normal |
| H02 |    176 | Normal |
| H03 |    171 | Normal |
| H04 |     55 | Normal (frisch, direkt vom Sender) |
| **H0F** | **18** | **BUG -- Hop-Counter-Unterlauf!** |

### H0F-Anomalie im Detail

H0F = 15 dezimal = `0x0F`. Der Hop-Counter ist ein 4-Bit-Feld (Bits [3:0] von
Byte [5]). `aprsmsg.max_hop` ist `uint8_t`. Der Standardwert fuer Text ist 4.

**Beobachtung:** Alle H0F-Pakete haben **6 Callsigns** im Pfad (= 5 Relays).
Ab Hop 4 braucht es nur 4 Relays um auf H00 zu kommen. Der 5. Relay duerfte
nicht stattfinden (`if(aprsmsg.max_hop > 0)` in `lora_functions.cpp:1032`).

**Erklaerung:** Der 5. Relay findet statt, weil:
1. Die msg_id aus dem Dedup-Puffer evictiert wird (Puffer voll)
2. Ein Relay-Kopie mit H01 ankommt und als "neu" durchgeht
3. Der Knoten decrementiert auf H00 und sendet
4. Dann kommt die H00-Kopie bei einem anderen Knoten an, dessen Dedup ebenfalls evictiert wurde
5. `max_hop` ist 0, wird decrementiert auf 255 (uint8_t Wrap)
6. `encodeAPRS` schreibt 255 in Byte [5], Empfaenger liest `255 & 0x0F = 0x0F`

**Aber:** Die Guard-Condition `if(max_hop > 0)` sollte bei max_hop=0 greifen und
den Relay blockieren. Dass H0F trotzdem auftritt, deutet auf einen Bug in der
Guard-Logik hin oder eine Race-Condition. **Muss untersucht werden.**

Beispiele aus dem Log:

```
13:56:21 H0F DJ8MEH-46,DJ8MEH-81,DJ8MEH-45,DJ8MEH-1,DJ8MEH-45,DJ8MEH-1   <- -45 und -1 jeweils 2x!
13:56:36 H0F DJ8MEH-46,DJ8MEH-81,DJ8MEH-8,DJ8MEH-45,DJ8MEH-8,DJ8MEH-44   <- -8 2x!
14:00:44 H0F DJ8MEH-46,DJ8MEH-81,DJ8MEH-45,DJ8MEH-1,DJ8MEH-45,DJ8MEH-81  <- -45 2x, -81 2x!
14:03:45 H0F DJ8MEH-46,DJ8MEH-81,DJ8MEH-45,DJ8MEH-8,DJ8MEH-45,DJ8MEH-1   <- -45 2x!
```

---

## 6. Analyse: Schaukelt sich was auf?

**Ja, massiv.** Hier die Top-10 der am haeufigsten empfangenen Message-IDs:

| Message-ID  | RX-Empfaenge | Typ | Originator | Inhalt |
|-------------|-------------:|-----|------------|--------|
| xF83120CA   |           24 | `@` | DJ8MEH-82  | Beacon |
| xAF25A098   |           22 | `@` | DJ8MEH-43  | Eigener Beacon (!) |
| xAF59C116   |           22 | `:` | DJ8MEH-46  | "Test3 ueber HF die 7" |
| xAF59C117   |           21 | `:` | DJ8MEH-46  | "Test3 ueber HF die 8" |
| xAF59C118   |           20 | `:` | DJ8MEH-46  | "Test3 ueber HF die 9" |
| xAF59C113   |           20 | `:` | DJ8MEH-46  | "Test3 ueber HF die 4" |
| xAF59C11A   |           19 | `:` | DJ8MEH-46  | "Test3 ueber HF die 11" |
| xE9ED0096   |           19 | `@` | DJ8MEH-44  | Beacon |
| x781111D5   |           17 | `@` | DJ8MEH-1   | Beacon |
| xA5DAC060   |           16 | `@` | DJ8MEH-41  | Beacon |

### Relay-Pfade fuer xF83120CA (Beacon DJ8MEH-82, 24x empfangen)

```
Direkt:    DJ8MEH-82                                    H04
1 Relay:   DJ8MEH-82,DJ8MEH-41                          H03
           DJ8MEH-82,DJ8MEH-81                          H03
           DJ8MEH-82,DJ8MEH-8                           H03 S1
           DJ8MEH-82,DJ8MEH-1                           H03
           DJ8MEH-82,DJ8MEH-44                          H03
           DJ8MEH-82,DJ8MEH-45                          H03
2 Relays:  DJ8MEH-82,DJ8MEH-41,DJ8MEH-82   <- LOOP!    H02
           DJ8MEH-82,DJ8MEH-45,DJ8MEH-82   <- LOOP!    H02
           DJ8MEH-82,DJ8MEH-45,DJ8MEH-1                H02
           DJ8MEH-82,DJ8MEH-45,DJ8MEH-44               H02
           DJ8MEH-82,DJ8MEH-45,DJ8MEH-46               H02
           DJ8MEH-82,DJ8MEH-45,DJ8MEH-81               H02
           DJ8MEH-82,DJ8MEH-45,DJ8MEH-8                H02 S1
           DJ8MEH-82,DJ8MEH-45,DJ8MEH-41               H02
3 Relays:  DJ8MEH-82,DJ8MEH-45,DJ8MEH-41,DJ8MEH-8     H01 S1
           DJ8MEH-82,DJ8MEH-45,DJ8MEH-41,DJ8MEH-45 LOOP! H01
4 Relays:  DJ8MEH-82,...,DJ8MEH-41,DJ8MEH-45,DJ8MEH-44 H00
           DJ8MEH-82,...,DJ8MEH-41,DJ8MEH-45,DJ8MEH-82 LOOP! H00
           DJ8MEH-82,...,DJ8MEH-41,DJ8MEH-8,DJ8MEH-44  H00
           DJ8MEH-82,...,DJ8MEH-41,DJ8MEH-8,DJ8MEH-41  LOOP! H00
```

**Ein einzelner Beacon erzeugt 24 Empfaenge** auf dem aufzeichnenden Knoten.
Mindestens 6 davon enthalten **Loops** (Knoten im Pfad doppelt vorhanden).

---

## 7. Pufferueberlauf

### Relay-Buffer (ringBuffer)

| Zeitpunkt | Ereignis |
|-----------|----------|
| 13:31 - 13:58 | Normal, queued 0-5 |
| **13:58:21** | **Erster RELAY_DROPPED** |
| 14:00 - 14:10 | queued steigt auf **28** (= MAX_RING - 2) |
| 14:00 - 14:13 | **45 RELAY_DROPPED**, Buffer dauerhaft voll |

### Phone-Buffer (BLE)

| Zeitpunkt | Ereignis |
|-----------|----------|
| ab ~14:00 | **33x RING_OVERFLOW buf=phone** |

### Ursache

Die Nachrichtenflut durch den Broadcast-Storm fuellt den Relay-Buffer schneller
als der Kanal die Pakete abarbeiten kann (100% Kanalauslastung). Neue Relays
muessen verworfen werden.

---

## 8. ACK-Analyse

| Metrik | Wert | Bewertung |
|--------|-----:|-----------|
| TX-ACK-Fast gesendet | 90 | Normal |
| NODE_ACK_QUEUED | 25 | Normal |
| Max ACKs pro msg_id | 2 | Kein ACK-Storm |
| ACK_FWD_DROPPED | 0 | ackBuffer nicht uebergelaufen |

**Kein ACK-Storm.** Das ACK-System arbeitet sauber. Die unsere Dedup-Verbesserungen
(ackBuffer_cancel_msgid, ACK_FWD_DEDUP) greifen. Das Problem liegt rein bei den
Nachrichten-Relays, nicht bei den ACKs.

---

## 9. CET Zeitmeldung

CET-Zeitnachrichten von OE1XAR-45 kommen regelmaessig alle ~5 Min via Gateway:

```
{CET}2026-03-04 12:36:51  (Log-Timestamp: 13:37)
{CET}2026-03-04 12:41:54  (Log-Timestamp: 13:42)
{CET}2026-03-04 12:46:57  (Log-Timestamp: 13:47)
{CET}2026-03-04 12:51:59  (Log-Timestamp: 13:52)
{CET}2026-03-04 12:57:02  (Log-Timestamp: 13:57)
{CET}2026-03-04 13:02:04  (Log-Timestamp: 14:02)
```

**Auffaelligkeit:** ~55 Minuten Differenz zwischen CET-Zeit und Log-Timestamp.
Moegliche Ursachen:
- Systemuhr des aufzeichnenden Knotens (DJ8MEH-43) laeuft 55 Min vor
- Oder: PuTTY-Timestamp ist die Systemzeit des verbundenen PCs

**Funktional:** Die CET-Synchronisation funktioniert korrekt. Nachrichten kommen
zuverlaessig durch das Gateway. Die Verzoegerung ist konstant (kein Drift).

---

## 10. Fehlermeldungen und Abstuerze

| Typ | Anzahl | Bewertung |
|-----|-------:|-----------|
| CRC_ERROR (size=255) | 17 | Normal bei hoher Kanalauslastung (Kollisionen) |
| Crash / Panic | 0 | Stabil |
| Watchdog Reset | 0 | Stabil |
| TX Timeout | 0 | Radio funktioniert |
| RX Error (OnRxError) | 0 | Radio funktioniert |
| Stack Overflow | 0 | Stabil |

**Der Knoten selbst laeuft stabil.** Die Probleme sind rein auf Protokollebene.

---

## 11. Root-Cause-Analyse: Warum kreisen Nachrichten?

### Ursache 1: Keine Loop-Detection im Code

**Datei:** `src/lora_functions.cpp:1029-1115`

Der Relay-Entscheidungscode prueft:
- `destination_call != eigenes Call` (Zeile 1029)
- `checkMesh()` -- Mesh aktiv (Zeile 1029)
- `aprsmsg.max_hop > 0` -- Hops verbleibend (Zeile 1032)
- `pending < MAX_RING - 2` -- Buffer hat Platz (Zeile 1077)

Was **NICHT** geprueft wird:
- **Ob das eigene Callsign bereits im Relay-Pfad steht**
- **Ob das Paket schon einmal von diesem Knoten gerelayed wurde**

Der einzige Schutz gegen Loops ist `is_new_packet()` (Zeile 220), das die msg_id
im Dedup-Buffer (`ringBufferLoraRX[30][5]`) sucht. Sobald diese msg_id durch
Puffer-Ueberlauf evictiert wird, wird dasselbe Paket erneut als "neu" behandelt.

### Ursache 2: Dedup-Puffer zu klein

**Datei:** `src/loop_functions.cpp:264`

```cpp
uint8_t ringBufferLoraRX[MAX_RING][5] = {0};  // MAX_RING = 30
```

Bei 9 Knoten mit Mesh werden pro Broadcast ca. 10-12 Dedup-Eintraege verbraucht
(1 Nachrichten-ID + ~9 ACK-IDs mit je eigener millis()-basierter ID). Nach 3
gleichzeitigen Broadcasts ist der Puffer voll. Aelteste Eintraege werden per
FIFO ueberschrieben -- ohne Ruecksicht auf das Retransmit-Fenster.

### Ursache 3: Kein zeitliches Expiry

**Datei:** `src/loop_functions.cpp:455-474`

`addLoraRxBuffer()` ist ein reiner Ring-FIFO. Es gibt keinen Zeitstempel pro
Eintrag. Eintraege werden rein positionsbasiert ueberschrieben, nicht nach Alter.
Eine msg_id, die vor 5 Sekunden gespeichert wurde, wird genauso schnell evictiert
wie eine, die vor 60 Sekunden gespeichert wurde.

### Ursache 4: Gateway-only Path-Count-Check

**Datei:** `src/lora_functions.cpp:1009-1014`

```cpp
if(bGATEWAY)
{
    if(aprsmsg.payload_type == ':' && aprsmsg.msg_last_path_cnt >= max_hop_text+1)
        bMeshDestination = false;
}
```

Es gibt einen Path-Count-Check, der Relays ab einer bestimmten Pfadlaenge
blockiert -- aber **nur fuer Gateways**! Regulaere Mesh-Knoten haben diesen
Check nicht und verlassen sich ausschliesslich auf den Hop-Counter und den
Dedup-Puffer.

---

## 12. Empfohlene Massnahmen

### Massnahme 1: Loop-Detection (KRITISCH, groesster Effekt)

**Problem:** Knoten relayen Pakete, in deren Pfad sie bereits stehen.

**Fix:** Vor dem Relay pruefen, ob das eigene Callsign bereits im
`msg_source_path` enthalten ist.

```
Datei:    src/lora_functions.cpp, vor Zeile 1032
Einfuegen:

    // Loop-Detection: nicht relayen wenn eigenes Call im Pfad
    if(aprsmsg.msg_source_path.indexOf(meshcom_settings.node_call) >= 0)
    {
        if(bLORADEBUG)
            Serial.printf("[MC-DBG] RELAY_LOOP_BLOCKED own_call_in_path\n");
        // skip relay
    }
    else if(aprsmsg.max_hop > 0)
    {
        // ... bestehender Relay-Code ...
    }

RAM:      0 Byte zusaetzlich
Effekt:   Eliminiert alle Loops. Ein Paket wird pro Knoten maximal 1x gerelayed.
Risiko:   Gering -- falsche Substring-Matches bei aehnlichen Callsigns
          (z.B. "DJ8MEH-4" matched auch "DJ8MEH-43")
          -> Loesung: Match mit Komma-Separator oder exakter Vergleich
```

**Hinweis zur Substring-Problematik:** `indexOf("DJ8MEH-4")` wuerde auch in
`"DJ8MEH-41,DJ8MEH-43"` matchen. Loesung: Pruefen mit Komma-Grenzen, z.B.
`",CALL,"` im String `","+ path + ","` suchen.

### Massnahme 2: Path-Count-Check auch fuer Mesh-Knoten

**Problem:** Nur Gateways pruefen die Pfadlaenge.

**Fix:** Den bestehenden Check (Zeile 1009-1014) auch fuer nicht-Gateway-Knoten
aktivieren, oder: generell auf `msg_last_path_cnt >= max_hop + 1` pruefen.

```
Datei:    src/lora_functions.cpp, Zeile 1009
Aendern:  if(bGATEWAY) entfernen, Check fuer alle Knoten gelten lassen

RAM:      0 Byte
Effekt:   Verhindert Relays bei ueberlangen Pfaden (Redundanz zu Massnahme 1)
```

### Massnahme 3: Dedup-Puffer vergroessern

**Problem:** 30 Slots reichen nicht fuer 9 Knoten.

```
Datei:    src/configuration_global.h
Aendern:  Separater Dedup-Puffer mit 60 Slots (statt MAX_RING)

    #define MAX_DEDUP_RING 60

RAM:      +150 Byte (vernachlaessigbar)
Effekt:   Dedup-Fenster verdoppelt, weniger Evictions
```

Siehe `docs/ringbuffer-discussion.md` Massnahme 2 fuer Details.

### Massnahme 4: Hop-Counter-Unterlauf absichern

**Problem:** H0F im Log zeigt, dass `aprsmsg.max_hop--` bei 0 zu uint8_t-Wrap fuehrt.

**Fix:** Guard-Condition nochmal pruefen und ggf. doppelt absichern:

```
Datei:    src/lora_functions.cpp, Zeile 1038
Vor dem Decrement:

    if(aprsmsg.max_hop == 0)
    {
        if(bLORADEBUG)
            Serial.println("[MC-DBG] RELAY_HOP_ZERO skip");
        break;  // oder goto skip_relay
    }
    aprsmsg.max_hop--;
```

Zusaetzlich in `encodeAPRS()` (aprs_functions.cpp, Zeile 926):

    msg_buffer[5] = aprsmsg.max_hop & 0x0F;  // nur untere 4 Bits

RAM:      0 Byte
Effekt:   Verhindert H0F-Anomalie definitiv

### Massnahme 5: Zeitbasiertes Expiry fuer Dedup (mittelfristig)

Statt reinem FIFO einen Zeitstempel pro Dedup-Eintrag speichern und Eintraege
erst nach >60s ueberschreiben.

```
RAM:      +120 Byte (2 Byte Timestamp * 60 Slots)
Effekt:   Innerhalb des Retransmit-Fensters (max 44s) werden Duplikate
          zuverlaessig erkannt, auch bei hoher Last
```

Siehe `docs/ringbuffer-discussion.md` Strategie B fuer Details.

---

## 13. Prioritaet der Massnahmen

```
+------+----------------------------+--------+-----------------------------+
| Prio | Massnahme                  | Aufwand| Effekt                      |
+------+----------------------------+--------+-----------------------------+
| 1    | Loop-Detection (Callsign   | Klein  | Eliminiert ALLE Loops,      |
|      | im Pfad pruefen)           |        | groesster Einzeleffekt       |
+------+----------------------------+--------+-----------------------------+
| 2    | Hop-Counter-Unterlauf      | Minimal| Behebt H0F-Bug              |
|      | absichern                  |        |                             |
+------+----------------------------+--------+-----------------------------+
| 3    | Path-Count-Check fuer      | Minimal| Redundanz zu Loop-Detection |
|      | alle Knoten                |        |                             |
+------+----------------------------+--------+-----------------------------+
| 4    | Dedup-Puffer 60 Slots      | Klein  | Haertet gegen Hochlast ab   |
+------+----------------------------+--------+-----------------------------+
| 5    | Zeitbasiertes Dedup-Expiry | Mittel | Langfristige Loesung fuer   |
|      |                            |        | grosse Netze                |
+------+----------------------------+--------+-----------------------------+
```

**Massnahme 1 + 2 zusammen loesen das akute Problem.** Die Loop-Detection verhindert,
dass Nachrichten kreisen. Der Hop-Counter-Fix verhindert den uint8_t-Unterlauf.
Beide zusammen sind wenige Zeilen Code, 0 Byte zusaetzlicher RAM, und koennen
als minimaler PR upstream eingereicht werden.

---

## 14. Fazit

**Nicht "works as designed".** Das Mesh hat einen systematischen Bug: fehlende
Loop-Detection bei Nachrichten-Relays. Der Dedup-Puffer war nie als einziger
Schutz gegen Loops konzipiert (er schuetzt gegen kurzfristige Duplikate), aber
bei 9 Knoten mit vollem Mesh reicht er nicht.

Die gute Nachricht: Das ACK-System funktioniert sauber (kein ACK-Storm), der
Knoten selbst laeuft stabil (keine Abstuerze), und der Fix ist minimal-invasiv
(Loop-Detection + Hop-Counter-Guard = ~10 Zeilen Code).
