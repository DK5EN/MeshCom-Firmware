# Ringpuffer-Dimensionierung: Diskussionsdokument

Dieses Dokument baut auf der Analyse in `message-design-analysis.md` auf und diskutiert
konkrete Loesungsansaetze fuer die identifizierten Puffer-Engpaesse.

---

## 1. Muessen wir die Ringpuffer an die Netzwerkgroesse anpassen?

### Problemstellung

Die aktuelle Puffer-Dimensionierung ist statisch (Compile-Time-Konstanten) und geht
implizit von einem kleinen Netzwerk aus. Die kritische Groesse ist nicht die Anzahl
der Nodes im Gesamtnetz, sondern die **Anzahl gleichzeitig hoerbarer Nodes im
LoRa-Einzugsbereich**.

### Warum die Anzahl hoerbarer Nodes entscheidend ist

Bei einem Broadcast von Node A mit N-1 hoerbaren Nodes:

```
Empfangene Pakete pro Node:     1 Original + (N-1) Relays = N Pakete
Dedup-Eintraege pro Broadcast:  N msg_ids (1 Original-ID, aber N verschiedene
                                Eintraege wenn jeder Relay eine neue ID haette
                                -> NEIN: alle tragen dieselbe msg_id!)
```

**Korrektur gegenueber der ersten Analyse:** Da alle Relays dieselbe `msg_id` tragen,
belegt ein Broadcast nur **1 Slot** im Dedup-Puffer (`ringBufferLoraRX`), nicht N.
Der Dedup-Puffer prueft `memcmp(compBuffer, ringBufferLoraRX[ib], 4)` -- gleiche
msg_id = gleicher Eintrag = Duplikat erkannt.

Das eigentliche Problem ist der **ackBuffer** und die **Kanalauslastung**:

```
ACKs pro Broadcast bei N hoerbaren Nodes:
  GW-Nodes:    je 2 ACK-Pakete (jeder mit eigener ack_msg_id)
  Mesh-Nodes:  je 1 ACK-Paket (jeder mit eigener ack_msg_id)

  -> Jeder ACK hat eine EIGENE msg_id -> JEDER belegt einen Dedup-Slot!
  -> Bei 8 Nodes: bis zu 10 ACK-msg_ids im Dedup-Puffer
  -> Bei 8 Nodes: bis zu 10 ACKs im ackBuffer (der nur 8 Slots hat!)
```

### Antwort

Ja, die Puffer muessen an die erwartete Netzwerkgroesse angepasst werden, aber
**nicht alle gleichermassen**:

| Puffer | Skalierung mit N Nodes | Anpassung noetig? |
|--------|------------------------|-------------------|
| `ringBufferLoraRX` (Dedup) | 1 pro Nachricht + 1 pro ACK | Ja, moderat |
| `ackBuffer` | Bis zu N pro Broadcast | **JA, KRITISCH** |
| `ringBuffer` (TX) | 1 Relay pro empfangene Nachricht | Moderat |
| `own_msg_id` | Nur eigene Nachrichten | Nein |
| `BLEtoPhoneBuff` | 1 pro empfangene Nachricht | Nein (BLE ist schnell) |

---

## 2. Konkrete Berechnung fuer 8 gleichzeitige Nodes

### Annahme

- 8 Nodes im gegenseitigen Empfangsbereich
- Davon 2 Gateways mit Mesh, 6 reine Mesh-Nodes
- Jeder Node sendet ca. 1 Text-Broadcast pro Minute
- Positionen alle 5 Minuten, HEY alle 10 Minuten

### Worst-Case pro Broadcast (1 Nachricht)

```
ackBuffer-Bedarf:
  2 GWs * 2 ACKs = 4 GW-ACKs
  5 Mesh-Nodes * 1 ACK = 5 Node-ACKs  (Sender sendet keinen ACK an sich selbst)
  Total = 9 ACKs, die innerhalb weniger Hundert ms generiert werden

  ackBuffer_cancel_msgid() kann Node-ACKs canceln wenn GW-ACK zuerst kommt,
  ABER: alle 7 empfangenden Nodes generieren ihren ACK nahezu gleichzeitig.
  Ergebnis: 9 ACKs muessen gleichzeitig in 8-Slot ackBuffer -> OVERFLOW
```

### ackBuffer-Dimensionierung

```
Empfehlung:  MAX_ACK_RING = 16

Begruendung:
  8 Nodes -> max 9 ACKs pro Broadcast
  Overlap mit vorherigem Broadcast moeglich -> 2x = 18 ACKs
  Mit Cancel-Logik: realistisch 10-12 gleichzeitig
  16 Slots gibt genuegend Headroom

RAM-Kosten:  (16 - 8) * 14 = 112 Byte zusaetzlich (vernachlaessigbar)
```

### ringBufferLoraRX-Dimensionierung (Dedup)

```
Pro Broadcast:  1 msg_id (Nachricht) + bis zu 9 ack_msg_ids = 10 Eintraege
Pro Minute:     8 Broadcasts * 10 = 80 Eintraege

Retransmit-Fenster: max 44 Sekunden
-> In 44 Sekunden: ~6 Broadcasts * 10 = 60 Eintraege

Aktuell: 30 Slots -> REICHT NICHT (60 > 30)

Empfehlung:  MAX_RING erhoehen auf 60 oder 80
  ODER: Nur Nachrichten-IDs speichern, ACK-IDs nicht
        (ACKs sind Fire&Forget und brauchen kein Dedup im klassischen Sinn)
  ODER: Zeitbasiertes Expiry (siehe Abschnitt 4)
```

### ringBuffer-Dimensionierung (TX)

```
Pro empfangener Broadcast (mit Mesh ON):
  1 Relay-Slot (0xFF, wird nach TX sofort frei)
  + 1 ACK-Slot im ackBuffer (nicht ringBuffer)

Pro eigenem Text:
  1 Slot mit Status=0x00, belegt fuer max 132s (3 Retransmits * 44s)

Pro Minute bei 8 Nodes:
  7 Relay-Slots (kurz belegt, je ~1-2 Sekunden bis TX)
  + eigene Nachrichten

30 Slots -> AUSREICHEND fuer TX-Puffer (Slots werden schnell frei)
```

### RAM-Gesamtberechnung: Heltec V3

```
ESP32-S3 nutzbarer RAM:  320 KB = 327,680 Byte
Davon fuer Arduino/IDF:  ~100-120 KB (Stack, Heap-Overhead, WiFi, BLE)
Verfuegbar fuer App:     ~200 KB = 204,800 Byte

Aktuelle Puffer (MAX_RING=30):                    41,822 Byte
Code, Strings, globale Variablen (geschaetzt):    ~80,000 Byte
                                                  --------
Geschaetzte Nutzung:                              ~122 KB
Freier Heap (gemessen, ESP.getFreeHeap()):        ~80-100 KB (typisch)
```

**Szenario A: ackBuffer von 8 auf 16, ringBufferLoraRX von 30 auf 60**

```
Zusaetzlicher RAM:
  ackBuffer:          +8 * 14          =    112 Byte
  ringBufferLoraRX:   +30 * 5          =    150 Byte
                                       --------
  Total zusaetzlich:                       262 Byte

Bewertung: PROBLEMLOS, weniger als 0.3 KB
```

**Szenario B: Alle Puffer fuer 8 Nodes optimiert**

```
MAX_ACK_RING = 16:    +112 Byte
Dedup-Puffer = 80:    +250 Byte (von 30*5 auf 80*5)
ringBuffer = 40:      +10 * 260 = 2,600 Byte
BLE-Puffer = 40:      +10 * 305 * 2 = 6,100 Byte
                      --------
Total zusaetzlich:    9,062 Byte = ~9 KB

Neuer Gesamt-Puffer:  ~50 KB
Geschaetzte Nutzung:  ~131 KB
Freier Heap:          ~70-90 KB

Bewertung: PASST NOCH KOMFORTABEL in den Heltec V3
```

**Szenario C: Aggressiv -- alle Puffer verdoppelt**

```
ringBuffer[60][260]:      +30 * 260 =  7,800 Byte
ackBuffer[16][14]:        +8 * 14   =    112 Byte
ringBufferLoraRX[60][5]:  +30 * 5   =    150 Byte
own_msg_id[60][5]:        +30 * 5   =    150 Byte
BLEtoPhoneBuff[60][305]:  +30 * 305 =  9,150 Byte
BLEComToPhoneBuff[60][305]: +30*305 =  9,150 Byte
                                    --------
Total zusaetzlich:                   26,512 Byte = ~26 KB

Neuer Gesamt-Puffer:  ~68 KB
Geschaetzte Nutzung:  ~148 KB
Freier Heap:          ~52-72 KB

Bewertung: GRENZWERTIG -- WiFi/BLE brauchen dynamischen Heap.
           Moeglich, aber kein grosser Spielraum mehr.
```

### Fazit RAM

```
+------+---------------------+--------+----------+------------------+
| Plan | Aenderung           | Kosten | Heap     | Bewertung        |
+------+---------------------+--------+----------+------------------+
| A    | ackBuf 16, Dedup 60 | 262 B  | ~80-100K | EMPFOHLEN        |
| B    | Alle fuer 8 Nodes   | 9 KB   | ~70-90K  | Machbar          |
| C    | Alles verdoppelt    | 26 KB  | ~52-72K  | Grenzwertig      |
+------+---------------------+--------+----------+------------------+
```

**Plan A ist der klare Gewinner**: minimaler RAM-Aufwand, loest die beiden
kritischsten Probleme (ackBuffer-Overflow und Dedup-Puffer-Ueberlauf).

---

## 3. Das 20/30 Ringbuffer-System

### Herkunft

In `configuration_global.h` gibt es drei Build-Varianten:

```c
#if defined(ENABLE_XML)       // E22_XML-DevKitC Board
  #define MAX_RING 20         // kleinere Puffer
  #define MAX_MHEARD 5
  #define MAX_MHPATH 5
#elif defined(ENABLE_SBUFFER) // TOTER CODE -- nirgends verwendet
  #define MAX_RING 20
#else                         // ALLE ANDEREN BOARDS (Heltec V3, T-Beam, RAK, ...)
  #define MAX_RING 30         // Standard-Groesse
  #define MAX_MHEARD 20
  #define MAX_MHPATH 30
#endif
```

### Warum existiert das?

- **ENABLE_XML** wird nur im `variants/E22_XML-DevKitC` Board gesetzt. Dieses
  Board hat XML-basierte serielle Ausgabe (`tinyxml_functions.cpp`), was
  zusaetzlichen RAM braucht -> Puffer wurden reduziert als Kompensation.

- **ENABLE_SBUFFER** ist ein toter Codepfad -- nirgends in den Variants
  definiert. Vermutlich ein geplantes aber nie umgesetztes Feature.

- Die **Einsparung** von MAX_RING=30 auf 20 betraegt ~11.5 KB, weil MAX_RING
  nicht nur den TX-Puffer steuert, sondern auch BLE-Puffer, Dedup, own_msg_id,
  und MHeard-Path.

### Problem

MAX_RING steuert zu viele unabhaengige Puffer gleichzeitig. Die optimale Groesse
ist fuer jeden Puffer unterschiedlich:

```
ringBuffer (TX):        30 reicht (Slots werden schnell frei)
ringBufferLoraRX:       30 ist ZU KLEIN fuer Dedup bei vielen Nodes
ackBuffer:              8 ist unabhaengig von MAX_RING, ABER ZU KLEIN
BLEtoPhoneBuff:         30 reicht (BLE raeumt schnell ab)
own_msg_id:             30 reicht
```

### Empfehlung

Separate Konstanten pro Puffer einfuehren statt alles an MAX_RING zu haengen:

```c
#define MAX_TX_RING      30   // ringBuffer (TX-Queue)
#define MAX_DEDUP_RING   60   // ringBufferLoraRX (Duplikat-Erkennung)
#define MAX_ACK_RING     16   // ackBuffer (ACK Fast-Path)
#define MAX_OWN_TX       30   // own_msg_id
#define MAX_BLE_RING     30   // BLE Phone Buffers
#define MAX_LOG          20   // RAW RX Log
#define MAX_UDP_RING     20   // UDP Outgoing
```

RAM-Kosten gegenueber Ist-Zustand: nur +262 Byte (Dedup 60 statt 30, ACK 16 statt 8).

---

## 4. Bewusstes Frame-Dropping statt Ueberschreiben

### Ist-Zustand

Aktuell gibt es zwei Overflow-Strategien:

```
addRingPointer():    Read-Pointer wird vorgeschoben -> aeltester Eintrag
                     wird ueberschrieben, OHNE Ruecksicht auf Inhalt.
                     Betrifft ALLE Puffer (TX, BLE, UDP, Dedup, Raw).

ringBuffer (Relay):  Explizites Drop wenn pending >= MAX_RING - 2
                     ("RELAY_DROPPED buffer_full")

ringBuffer (Retry):  Explizites Drop wenn pending >= MAX_RING - 2
                     ("RETRANSMIT_DROPPED buffer_full")
```

**Das Problem beim Ueberschreiben:**
- Im Dedup-Puffer (`ringBufferLoraRX`): wenn eine msg_id ueberschrieben wird,
  wird ein spaeterer Empfang derselben Nachricht als "neu" erkannt und erneut
  verarbeitet -> ACK-Storm-Kaskade
- Im TX-Puffer: eine noch nicht gesendete Nachricht geht verloren

### Vorschlag: Priorisiertes Dropping

Statt blindem Ueberschreiben nach FIFO koennte man **bewusst entscheiden was
gedroppt wird**:

#### Strategie A: Drop by Priority (fuer ringBuffer TX)

```
Wenn ringBuffer voll:
  1. Zuerst: Relay-Nachrichten (Status=0xFF) droppen
  2. Dann:   Retransmit-Kopien droppen (retryCount > 0)
  3. Zuletzt: Eigene neue Nachrichten (retryCount == 0) -> NIE droppen

Implementierung: Beim Einfuegen einer neuen Nachricht in vollen Puffer
den aeltesten Relay-Slot (0xFF) suchen und ueberschreiben.
```

#### Strategie B: Zeitbasiertes Expiry (fuer ringBufferLoraRX Dedup)

```
Pro Dedup-Eintrag: 4 Byte msg_id + 1 Byte server_flag + 4 Byte Zeitstempel
                   = 9 Byte statt 5 Byte (+80% RAM pro Slot)

Beim Einfuegen: aeltesten Eintrag suchen (>60s alt), den ueberschreiben.
Wenn kein alter Eintrag: Ring-FIFO wie bisher.

Vorteil: Nachrichten innerhalb des Retransmit-Fensters (max 44s)
         werden zuverlaessig dedupliziert, auch bei hoher Last.

Alternative: millis()-basierter Timestamp als uint16_t
             (nur obere 16 Bit von millis()/1000 -> reicht fuer ~65s Genauigkeit)
             = nur 2 Byte zusaetzlich -> 7 Byte pro Slot
```

#### Strategie C: Getrennte Dedup-Puffer fuer Nachrichten und ACKs

```
ringBufferLoraRX_msg[40][5]   -- nur fuer Nachrichten-IDs (0x3A, 0x21, 0x40)
ringBufferLoraRX_ack[20][5]   -- nur fuer ACK-IDs (0x41)

Vorteil: ACK-Flut kann nicht die Nachrichten-Dedup verdraengen
RAM:     40*5 + 20*5 = 300 Byte (aktuell 30*5 = 150 Byte, +150 Byte)
```

#### Strategie D: Bloom-Filter statt Array

```
Probabilistischer Ansatz: 128 Byte Bloom-Filter fuer ~100 msg_ids
mit <1% False-Positive-Rate.

Vorteil:  Sehr kompakt, O(1) Lookup
Nachteil: Kein Expiry moeglich (Filter muss periodisch geloescht werden)
          False Positives (seltene Nachricht wird als Duplikat verworfen)
          Komplexitaet in der Implementierung

Eher nicht empfohlen fuer diesen Anwendungsfall.
```

### Empfehlung

**Strategie B + C kombiniert**:
- Getrennte Dedup-Puffer fuer Nachrichten (40 Slots) und ACKs (20 Slots)
- Zeitbasiertes Expiry fuer den Nachrichten-Puffer (7 Byte/Slot mit uint16_t Timestamp)
- ACK-Puffer als reiner FIFO (ACKs sind kurzlebig, 20 Slots reichen)
- RAM-Kosten: 40*7 + 20*5 = 380 Byte (aktuell 150 Byte, +230 Byte)

---

## 5. Mehr Fire-and-Forget

### Ist-Zustand: Was nutzt Retransmit?

```
Retransmit (Status=0x00):
  NUR user-originierte Textnachrichten (Typ 0x3A)
  NICHT: {CET}, {MCP}, {SET} Kontrollnachrichten
  NICHT: Positionen, HEY, ACKs, Relays, UDP-to-LoRa

Alles andere ist bereits Fire-and-Forget (Status=0xFF).
```

### Was waere, wenn wir ALLES Fire-and-Forget machen?

```
Vorteile:
  + ringBuffer-Slots werden sofort nach TX frei
  + Kein Retransmit-Timer, keine retryCount-Verwaltung
  + Kein Retransmit-Storm bei Dedup-Puffer-Ueberlauf
  + Einfacherer Code (updateRetransmissionStatus() entfaellt)
  + Weniger Kanalauslastung (keine Wiederholungen)
  + Weniger ACK-Pakete (ACKs werden nur noch als "Empfangsbestaetigung"
    an die Phone-App verwendet, nicht mehr als Retransmit-Trigger)

Nachteile:
  - Textnachrichten ohne ACK gehen verloren wenn der erste TX nicht ankommt
  - Keine Zustellgarantie fuer DMs
  - Bei schlechter Funkverbindung hoehere Verlustrate
  - Phone-App kann dem User nicht mehr "zugestellt" anzeigen
```

### Differenziertes Fire-and-Forget

Statt "alles oder nichts" koennte man differenzieren:

```
+-------------------+------------------+-------------------------------+
| Nachrichtentyp    | Aktuell          | Vorschlag                     |
+-------------------+------------------+-------------------------------+
| Text an *         | 3x Retry         | Fire-and-Forget               |
|                   |                  | (Broadcast erreicht sowieso   |
|                   |                  | alle in Reichweite)            |
+-------------------+------------------+-------------------------------+
| Text an Gruppe    | 3x Retry         | Fire-and-Forget               |
|                   |                  | (wie Broadcast)               |
+-------------------+------------------+-------------------------------+
| Text an Callsign  | 3x Retry         | 1x Retry BEIBEHALTEN          |
| (DM)              |                  | (Zustellgarantie wichtig)     |
+-------------------+------------------+-------------------------------+
| Position          | Fire-and-Forget  | Keine Aenderung               |
+-------------------+------------------+-------------------------------+
| HEY               | Fire-and-Forget  | Keine Aenderung               |
+-------------------+------------------+-------------------------------+
| ACK               | Fire-and-Forget  | Keine Aenderung               |
+-------------------+------------------+-------------------------------+
```

### Auswirkung auf ACK-Storms

Wenn Broadcasts Fire-and-Forget sind:
- Der Sender braucht KEINEN ACK mehr (er retransmitted sowieso nicht)
- Empfaenger koennten trotzdem ACKs senden (fuer HEARD-Status / Phone-App)
- ABER: wenn wir keine ACKs mehr senden, fallen ACK-Storms komplett weg!

```
Szenario: 8 Nodes, Broadcast, OHNE ACKs:
  Kanalauslastung = 1 Original + 7 Relays = 8 Pakete
  KEIN ACK, KEIN ACK-Relay, KEIN Retransmit

  vs. Ist-Zustand:
  1 Original + 7 Relays + 9 ACKs + ACK-Relays + bis zu 3 Retransmits
  = 8 + 9 + ~63 ACK-Relays + bis zu 3*8 = 24 Retransmit-Pakete
  = ~104 Pakete (!!!)
```

### Kompromiss: ACK-Suppression fuer Broadcasts

```
Regel: Broadcasts und Gruppen-Nachrichten bekommen KEINE ACKs.
       Nur DMs (Callsign-adressiert) bekommen ACKs + Retry.

Begruendung:
  - Ein Broadcast erreicht entweder alle oder niemanden
    (bei Mesh: die Mesh-Kette sorgt fuer Verbreitung)
  - Ein ACK auf einen Broadcast sagt nur "mindestens ein Node hat's gehoert"
  - Das ist nuetzlich fuer die Phone-App, aber teuer auf dem Kanal
  - Alternative: HEARD-Status (wenn eigene Nachricht gerelayed gehoert wird)
    ist kostenlos und liefert dieselbe Information
```

---

## 6. Von 3 Retransmits auf 1 Retransmit

### Ist-Zustand

```
MAX_RETRANSMIT = 3
Intervall:     24-44 Sekunden (Jitter basierend auf msg_id Hash)
Slot-Belegung: Bis zu 132 Sekunden pro Nachricht (3 * 44s worst case)
Zeitlinie:

  T=0s     Erster TX
  T=24-44s Zweiter TX (1. Retry)
  T=48-88s Dritter TX (2. Retry)
  T=72-132s Vierter TX (3. Retry) oder Aufgabe
```

### Vorschlag: MAX_RETRANSMIT = 1

```
Intervall:     24-44 Sekunden (unveraendert)
Slot-Belegung: Bis zu 44 Sekunden pro Nachricht (1 * 44s worst case)
Zeitlinie:

  T=0s     Erster TX
  T=24-44s Zweiter TX (1. und einziger Retry) oder Aufgabe
```

### Vorteile

```
1. Slot-Belegung -67%:
   132s -> 44s max pro Nachricht
   Bei 30 Slots: Durchsatz verdreifacht sich theoretisch

2. Kanalauslastung -50%:
   Statt 4 TX (1+3) nur 2 TX (1+1) pro Nachricht im Worst Case
   Bei 8 Nodes: 8 Retransmits weniger pro Nachricht

3. Retransmit-Storm-Risiko sinkt drastisch:
   Weniger Retransmits = weniger Chance auf Dedup-Puffer-Ueberlauf
   = weniger Kaskaden

4. Einfachere Analyse:
   Nur 2 Zustaende (gesendet, einmal retried) statt 4
```

### Risiken

```
1. Zustellrate sinkt bei schlechter Funkverbindung:
   Bei 50% Paketverlust:
     3 Retransmits: Zustellwahrscheinlichkeit = 1 - 0.5^4 = 93.75%
     1 Retransmit:  Zustellwahrscheinlichkeit = 1 - 0.5^2 = 75%
     0 Retransmits: Zustellwahrscheinlichkeit = 50%

   ABER: in einem Mesh-Netzwerk mit mehreren Pfaden ist die effektive
   Zustellrate hoeher, weil Relays die Nachricht ueber alternative
   Wege zum Empfaenger bringen.

2. DMs koennten verloren gehen:
   Bei Callsign-adressierten Nachrichten ist jeder Verlust
   fuer den User sichtbar und aergerlich.
   -> Empfehlung: DMs bei 1 Retry belassen oder sogar 2 Retry
```

### Differenzierter Vorschlag

```
+-------------------+----------+-----------+-------------------------------+
| Nachrichtentyp    | Aktuell  | Vorschlag | Begruendung                   |
+-------------------+----------+-----------+-------------------------------+
| Text an * / Grp   | 3 Retry  | 0 Retry   | Broadcast = Fire-and-Forget   |
|                   |          |           | Mesh sorgt fuer Verbreitung   |
+-------------------+----------+-----------+-------------------------------+
| Text DM (Callsign)| 3 Retry  | 1 Retry   | Zustellgarantie wichtig,      |
|                   |          |           | aber 3 ist zu viel            |
+-------------------+----------+-----------+-------------------------------+
| Position          | 0        | 0         | Keine Aenderung               |
+-------------------+----------+-----------+-------------------------------+
| HEY               | 0        | 0         | Keine Aenderung               |
+-------------------+----------+-----------+-------------------------------+
```

### Auswirkung auf das 8-Node-Szenario

```
IST-ZUSTAND (3 Retry, ACKs fuer alles):
  1 Broadcast generiert:
    8 LoRa-Pakete (1 + 7 Relays)
    9 ACKs + ~63 ACK-Relays
    bis zu 3 Retransmits * 8 = 24 Pakete
    Total: ~104 Pakete auf dem Kanal

VORSCHLAG (0 Retry fuer Broadcast, keine ACKs fuer Broadcast):
  1 Broadcast generiert:
    8 LoRa-Pakete (1 + 7 Relays)
    0 ACKs
    0 Retransmits
    Total: 8 Pakete auf dem Kanal

  -> REDUKTION UM FAKTOR 13 (!!!)
```

---

## 7. Zusammenfassung der Empfehlungen

### Massnahme 1: Sofort umsetzbar, minimal-invasiv

```
Aenderung:  MAX_ACK_RING = 16  (statt 8)
Datei:      configuration_global.h
RAM-Kosten: +112 Byte
Effekt:     ackBuffer-Overflow bei 8+ Nodes vermieden
```

### Massnahme 2: Sofort umsetzbar, minimal-invasiv

```
Aenderung:  Separater Dedup-Puffer, 60 Slots (statt MAX_RING=30)
Dateien:    configuration_global.h, loop_functions.cpp
RAM-Kosten: +150 Byte
Effekt:     Dedup-Fenster verdoppelt, weniger Storm-Kaskaden
```

### Massnahme 3: Design-Entscheidung erforderlich

```
Aenderung:  Broadcasts/Gruppen -> Fire-and-Forget (kein Retry, kein ACK)
            Nur DMs behalten Retry (1x statt 3x)
Dateien:    lora_functions.cpp, loop_functions.cpp
RAM-Kosten: 0
Effekt:     Kanalauslastung um Faktor 10+ reduziert
            ACK-Storms eliminiert
            Simpler Code
Risiko:     Phone-App verliert "zugestellt"-Feedback fuer Broadcasts
            -> Alternative: HEARD-Status nutzen (kostenlos)
```

### Massnahme 4: Mittelfristig

```
Aenderung:  Separate Puffer-Konstanten statt gemeinsames MAX_RING
            ENABLE_SBUFFER Toter Code entfernen
Dateien:    configuration_global.h, loop_functions.cpp, loop_functions_extern.h
RAM-Kosten: 0 (Umstrukturierung)
Effekt:     Jeder Puffer optimal dimensioniert
            Sauberere Architektur
```

### Prioritaet

```
Empfohlene Reihenfolge:

  1. Massnahme 3 (Fire-and-Forget fuer Broadcasts) -- groesster Effekt, 0 Byte RAM
  2. Massnahme 1 (ackBuffer 16) -- 112 Byte, verhindert akute Overflows
  3. Massnahme 2 (Dedup 60) -- 150 Byte, haertet Dedup ab
  4. Massnahme 4 (Refactoring) -- Aufraumen, keine Eile
```
