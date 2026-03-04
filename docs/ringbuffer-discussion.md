# Ringpuffer-Dimensionierung: Diskussionsdokument

Dieses Dokument baut auf der Analyse in `message-design-analysis.md` auf und diskutiert
konkrete Loesungsansaetze fuer die identifizierten Puffer-Engpaesse.

---

## 1. Muessen wir die Ringpuffer an die Netzwerkgroesse anpassen?

### Problemstellung

Die Puffer-Dimensionierung ist statisch (Compile-Time-Konstanten). Seit der
Entkopplung von MAX_DEDUP_RING und MAX_ACK_RING von MAX_RING sind die kritischsten
Puffer separat konfigurierbar. Die kritische Groesse ist nicht die Anzahl
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
  -> Bei 8 Nodes: bis zu 10 ACKs im ackBuffer (16 Slots, genuegend Headroom)
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
Aktuell:  MAX_ACK_RING = 16 (umgesetzt)

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

Aktuell: 60 Slots (MAX_DEDUP_RING, entkoppelt von MAX_RING) -> AUSREICHEND
  60 / 10 = 6 Broadcasts bevor Ueberschreiben -> Dedup-Fenster ~180s

Moegliche weitere Optimierungen:
  - Nur Nachrichten-IDs speichern, ACK-IDs nicht
    (ACKs sind Fire&Forget und brauchen kein Dedup im klassischen Sinn)
  - Zeitbasiertes Expiry (siehe Abschnitt 4)
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

**Umgesetzt: ackBuffer 16, Dedup 60 (Plan A)**

```
Zusaetzlicher RAM gegenueber Ausgangszustand:
  ackBuffer:          +8 * 14          =    112 Byte
  ringBufferLoraRX:   +30 * 5          =    150 Byte
                                       --------
  Total zusaetzlich:                       262 Byte

Bewertung: PROBLEMLOS, weniger als 0.3 KB.
Loest die beiden kritischsten Probleme (ackBuffer-Overflow und Dedup-Ueberlauf).
```

**Weitere Skalierung (falls zukuenftig noetig)**

```
Fuer >20 Nodes: ringBuffer und BLE-Puffer vergroessern.
ringBuffer = 40:      +10 * 260 = 2,600 Byte
BLE-Puffer = 40:      +10 * 305 * 2 = 6,100 Byte
Total zusaetzlich:    ~9 KB -> Freier Heap ~70-90K (Heltec V3: machbar)
```

---

## 3. Das 20/30 Ringbuffer-System

### Herkunft

In `configuration_global.h` gibt es drei Build-Varianten:

```c
#if defined(ENABLE_XML)       // E22_XML-DevKitC Board
  #define MAX_RING 20         // kleinere TX-Puffer
  #define MAX_DEDUP_RING 40   // Dedup-Puffer (entkoppelt von TX)
  #define MAX_MHEARD 5
  #define MAX_MHPATH 5
#elif defined(ENABLE_SBUFFER) // TOTER CODE -- nirgends verwendet
  #define MAX_RING 20
  #define MAX_DEDUP_RING 40
#else                         // ALLE ANDEREN BOARDS (Heltec V3, T-Beam, RAK, ...)
  #define MAX_RING 30         // Standard-Groesse TX-Puffer
  #define MAX_DEDUP_RING 60   // Dedup-Puffer (entkoppelt)
  #define MAX_MHEARD 20
  #define MAX_MHPATH 30
#endif
// Alle Builds:
#define MAX_ACK_RING 16       // ACK Fast-Path (separat)
#define MAX_RETRANSMIT 3      // Max Retransmit-Versuche pro Nachricht
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

Seit der Entkopplung steuert MAX_RING nur noch TX-Puffer, BLE-Puffer und own_msg_id.
Die kritischsten Puffer haben eigene Konstanten:

```
ringBuffer (TX):        MAX_RING = 30 (Slots werden schnell frei)
ringBufferLoraRX:       MAX_DEDUP_RING = 60 (entkoppelt, genuegend fuer 5+ Broadcasts)
ackBuffer:              MAX_ACK_RING = 16 (genuegend fuer 10-Node-Szenarien)
BLEtoPhoneBuff:         MAX_RING = 30 (BLE raeumt schnell ab)
own_msg_id:             MAX_RING = 30 (reicht)
```

### Offene Punkte

Weitere Entkopplung moeglich (MAX_OWN_TX, MAX_BLE_RING separat), aber
aktuell kein Engpass. Erst bei >20 Nodes relevant.

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
  User-originierte Textnachrichten (Typ 0x3A, BLE/lokal): 3 Retries
  UDP-Text DM (persoenlich, z.B. OE1ABC):                 2 Retries
  UDP-Text Broadcast (*) und Gruppe (z.B. 9999):           1 Retry
  NICHT: {CET}, {MCP}, {SET} Kontrollnachrichten
  NICHT: Positionen, HEY, ACKs, Relays

Alles andere ist Fire-and-Forget (Status=0xFF).
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

### Umgesetztes Retry-Modell (differenziert nach Quelle und Typ)

```
+-------------------+---------------------+-------------------------------+
| Nachrichtentyp    | Retries             | Mechanismus                   |
+-------------------+---------------------+-------------------------------+
| Lokal Text (BLE)  | 3 Retries           | retryCount=0, MAX_RETRANSMIT=3|
+-------------------+---------------------+-------------------------------+
| UDP Text DM       | 2 Retries           | retryCount=1                  |
| (an Callsign)     |                     | (Zustellgarantie wichtig)     |
+-------------------+---------------------+-------------------------------+
| UDP Text Broadcast| 1 Retry             | retryCount=2                  |
| (* oder Gruppe)   |                     | (Kompromiss: 1x reicht meist) |
+-------------------+---------------------+-------------------------------+
| UDP CET/SET       | Fire-and-Forget     | Status=0xFF                   |
+-------------------+---------------------+-------------------------------+
| Position          | Fire-and-Forget     | Status=0xFF                   |
+-------------------+---------------------+-------------------------------+
| HEY               | Fire-and-Forget     | Status=0xFF                   |
+-------------------+---------------------+-------------------------------+
| ACK               | Fire-and-Forget     | Status=0xFF                   |
+-------------------+---------------------+-------------------------------+
```

### Auswirkung auf ACK-Storms

Wenn Broadcasts Fire-and-Forget sind:
- Der Sender braucht KEINEN ACK mehr (er retransmitted sowieso nicht)
- Empfaenger koennten trotzdem ACKs senden (fuer HEARD-Status / Phone-App)
- ABER: wenn wir keine ACKs mehr senden, fallen ACK-Storms komplett weg!

```
Szenario: 8 Nodes, Broadcast, OHNE ACKs (theoretisch):
  Kanalauslastung = 1 Original + 7 Relays = 8 Pakete
  KEIN ACK, KEIN ACK-Relay, KEIN Retransmit

  vs. Aktueller Zustand (1 Retry fuer UDP-Broadcast):
  1 Original + 7 Relays + 9 ACKs + ACK-Relays + max 1 Retransmit
  = 8 + 9 + ~63 ACK-Relays + 8 Retransmit-Pakete
  = ~88 Pakete

  ACK-Suppression fuer Broadcasts wuerde das auf ~8 Pakete reduzieren.
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

## 6. Retransmit-Abstufung (umgesetzt)

### Mechanismus

```
MAX_RETRANSMIT = 3 (in configuration_global.h)
Intervall:     24-44 Sekunden (Jitter basierend auf msg_id Hash, deterministisch)
Timer:         updateRetransmissionStatus() inkrementiert Status-Byte alle 2s

Die Anzahl effektiver Retries wird ueber den Startwert von retryCount gesteuert:
  retryCount startet bei N, wird pro Retry um 1 erhoeht.
  Bei retryCount >= MAX_RETRANSMIT (3) wird aufgegeben.
```

### Retries nach Nachrichtentyp und Quelle

```
+-------------------+-------------+-----------+-------------------------------+
| Nachrichtentyp    | retryCount  | Retries   | Slot-Belegung (worst case)    |
+-------------------+-------------+-----------+-------------------------------+
| Lokal Text (BLE)  | 0           | 3 Retries | ~132s (3 * 44s)               |
+-------------------+-------------+-----------+-------------------------------+
| UDP Text DM       | 1           | 2 Retries | ~88s (2 * 44s)                |
| (an Callsign)     |             |           |                               |
+-------------------+-------------+-----------+-------------------------------+
| UDP Text Broadcast| 2           | 1 Retry   | ~44s (1 * 44s)                |
| (* oder Gruppe)   |             |           |                               |
+-------------------+-------------+-----------+-------------------------------+
| UDP CET/SET       | 0 (0xFF)    | 0         | Sofort frei nach TX           |
+-------------------+-------------+-----------+-------------------------------+
| Position/HEY/ACK  | 0 (0xFF)    | 0         | Sofort frei nach TX           |
+-------------------+-------------+-----------+-------------------------------+
```

### Auswirkung auf das 8-Node-Szenario

```
VORHER (3 Retry fuer alle UDP-Texte, ACKs fuer alles):
  1 UDP-Broadcast generiert:
    8 LoRa-Pakete (1 + 7 Relays)
    9 ACKs + ~63 ACK-Relays
    bis zu 3 Retransmits * 8 = 24 Pakete
    Total: ~104 Pakete auf dem Kanal

JETZT (1 Retry fuer UDP-Broadcast):
  1 UDP-Broadcast generiert:
    8 LoRa-Pakete (1 + 7 Relays)
    9 ACKs + ~63 ACK-Relays (unveraendert)
    max 1 Retransmit * 8 = 8 Pakete
    Total: ~88 Pakete auf dem Kanal

  -> Reduktion der Retransmit-Last um 67%
  -> DMs behalten 2 Retries fuer zuverlaessige Zustellung
```

---

## 7. Umsetzungsstatus

### UMGESETZT: ackBuffer von 8 auf 16

```
Aenderung:  MAX_ACK_RING = 16
Datei:      configuration_global.h
RAM-Kosten: +112 Byte
Effekt:     ackBuffer-Overflow bei 8+ Nodes vermieden
```

### UMGESETZT: Dedup-Puffer entkoppelt und vergroessert

```
Aenderung:  MAX_DEDUP_RING = 60 (40 bei XML/SBUFFER), entkoppelt von MAX_RING
Dateien:    configuration_global.h, loop_functions.cpp, loop_functions_extern.h, lora_functions.cpp
RAM-Kosten: +150 Byte
Effekt:     Dedup-Fenster verdoppelt, weniger Storm-Kaskaden
```

### UMGESETZT: Differenzierte Retries fuer UDP-Textnachrichten

```
Aenderung:  UDP DM: 2 Retries, UDP Broadcast/Gruppe: 1 Retry, CET/SET: Fire&Forget
Dateien:    udp_functions.cpp, configuration_global.h (MAX_RETRANSMIT verschoben)
RAM-Kosten: 0
Effekt:     Retry-Last fuer Broadcasts um 67% reduziert
            DMs behalten zuverlaessige Zustellung
            CET/SET bleiben leichtgewichtig
```

### UMGESETZT: Loop-Detection und Hop-Counter-Safeguard

```
Aenderung:  Eigenes Callsign in Source-Path erkennen -> Relay blockieren
            Path-Count gegen max_hop pruefen (zusaetzlich zum Hop-Counter)
Dateien:    lora_functions.cpp, aprs_functions.cpp
Effekt:     Broadcast-Loops und ueberzaehlige Relays verhindert
```

### OFFEN: Weitere Optimierungen

```
- ACK-Suppression fuer Broadcasts (keine ACKs bei ServerFlag)
- Getrennte Dedup-Puffer fuer Nachrichten und ACKs
- Zeitbasiertes Expiry im Dedup-Puffer
- ENABLE_SBUFFER toten Code entfernen
- Weitere Puffer-Konstanten entkoppeln (MAX_OWN_TX, MAX_BLE_RING)
```
