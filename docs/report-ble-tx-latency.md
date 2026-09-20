# BLE-to-LoRa TX Latenz-Analyse

**Datenquelle:** Alle Logs in `tools/meshcom_monitor/` (8 Dateien)
**Samples:** 117 BLE-Textnachrichten (`src=user_msg`)
**Nodes:** DK5EN-98 (RAK4631), DK5EN-99 (Heltec V3), DK5EN-12

---

## Der Weg einer BLE-Nachricht

```
BLE UART RX
    |
    v
readPhoneCommand() -- Parst 0xA0 Payload, setzt hasMsgFromPhone=true
    |
    v
Main Loop -- Verarbeitet Flag, baut Paket
    |
    v
RING_WRITE (src=user_msg) -- In TX-Queue geschrieben     <-- t0 (messbar)
    |
    v
Priority Queue -- Wartet auf TX-Slot (Prio 2 fuer Text)
    |
    v
RING_TX_READ (lat=Nms) -- Aus Queue gelesen              <-- t1 (messbar, lat = t1-t0)
    |
    v
TX_GATE_ENTER -- CAD/CSMA Kanalzugriff
    |
    v
RADIO_TX -- LoRa Paket in der Luft                       <-- t2 (messbar)
    |
    v
TX_DONE -- Fertig
```

---

## Ergebnisse

### Gesamtstatistik (117 Nachrichten)

```
QUEUE_WAIT: AVG = 5.267ms  MIN = 101ms  MAX = 48.944ms
END_TO_END: AVG = 5.273ms  MIN = 0ms    MAX = 51.000ms
MEDIAN:     4.000ms
P95:        13.000ms
```

Die BLE-Verarbeitung (BLE RX bis RING_WRITE) liegt bei **<20ms** -- vernachlaessigbar.
Der Radio-Overhead (RING_TX_READ bis TX-LoRa) liegt bei **<1s** -- ebenfalls gering.

**Der Bottleneck ist die Queue-Wartezeit** (RING_WRITE bis RING_TX_READ).

### Verteilung

```
Latenz        Anzahl   Anteil    Kumulativ
< 2s            10      8,5%       8,5%
2-5s            51     43,6%      52,1%     <-- Hauptgruppe
5-10s           32     27,4%      79,5%
10-20s          11      9,4%      88,9%
> 20s            3      2,6%      91,5%
ohne TX-Match   10      8,5%     100,0%
```

```
 0s         5s        10s        15s        20s        50s
 |          |          |          |          |          |
 |##        |          |          |          |          |   < 2s   (10)
 |##########|##########|          |          |          |   2-5s   (51)
 |          |##########|######    |          |          |   5-10s  (32)
 |          |          |####      |          |          |   10-20s (11)
 |          |          |          |#         |          |   > 20s   (3)
```

**Typische Latenz: 2-5 Sekunden** (43,6% aller Nachrichten).
80% der Nachrichten sind innerhalb von 10 Sekunden in der Luft.

### Ausreisser

3 Nachrichten mit >20s:

| MSG_ID | Zeit | Queue | Wartezeit | Text |
|--------|------|-------|-----------|------|
| 91A4356B | 17:54 | 0 | 25.548ms | "Was gibt's fuer eine..." |
| EA25A3DA | 12:09 | 0 | 34.522ms | "Ja, dann testen wir..." |
| EA25A3DC | 12:12 | 3 | 48.944ms | "So ist das eben :)..." |

Die Ausreisser EA25A3D* stammen von DK5EN-12 -- moeglicherweise ein langsamerer Node oder hoher Relay-Traffic.

### Queue-Tiefe vs Wartezeit

```
Queue   Avg Wait   Count   Bemerkung
0        4.923ms     61    Leere Queue, trotzdem ~5s warten
1        4.632ms     20    Aehnlich wie Queue=0
2        5.188ms     10    Kaum Unterschied
3        8.959ms     10    Deutlicher Anstieg
4        4.843ms      7    (Streuung)
5        7.386ms      3
6        4.709ms      2
7        3.742ms      2
10       3.105ms      1
14       5.784ms      1
```

**Ueberraschend:** Die Queue-Tiefe hat wenig Einfluss auf die Wartezeit. Selbst bei leerer Queue (queued=0) betraegt die durchschnittliche Wartezeit ~5 Sekunden.

### Warum 5 Sekunden bei leerer Queue?

Die Erklaerung liegt im CSMA-Mechanismus:

1. **Adaptive Wait:** Der Node wartet nach jedem RX-Timeout einen zufaelligen Zeitraum (4-6s, siehe CSMA_TIMING). Dies ist die Backoff-Zeit bevor ein TX-Versuch gestartet wird.
2. **CAD Check:** Nach der Wartezeit prueft CAD ob der Kanal frei ist (~16ms)
3. **CAD Busy:** Bei 35,9% CAD-Busy-Rate muss oft erneut gewartet werden

Die 5s Grundlatenz ist also **by design** -- es ist die CSMA-Wartezeit, nicht die Queue.

---

## Per-Node Aufschluesselung

### DK5EN-98 (RAK4631) -- 90 Nachrichten

```
QUEUE_WAIT: AVG=4.440ms  MIN=101ms  MAX=25.548ms
Mehrheitlich queued=0 (leere Queue)
Einige sehr schnelle (<300ms) -- wenn gerade ein TX-Slot frei war
```

### DK5EN-99 (Heltec V3) -- 15 Nachrichten

```
QUEUE_WAIT: AVG=6.007ms  MIN=1.865ms  MAX=14.684ms
Hoehere Durchschnittswerte -- hoehere Relay-Last (95,7% Relay)
```

### DK5EN-12 -- 3 Nachrichten

```
QUEUE_WAIT: AVG=29.402ms  MIN=4.739ms  MAX=48.944ms
Deutlich langsamer -- moeglicherweise ueberlastet oder andere Config
```

---

## Analyse: Sind die Zeiten akzeptabel?

### Vergleich mit theoretischem Minimum

| Phase | Theoretisch | Gemessen |
|-------|-------------|----------|
| BLE Processing | <10ms | ~17ms |
| CSMA Backoff | 4-6s (SF11/BW250) | 4,8s avg |
| CAD Check | ~16ms | ~16ms |
| Radio TX (70 Bytes) | ~300ms | <1s |
| **Minimum Total** | **~5s** | **5,3s avg** |

Die gemessene Latenz liegt sehr nahe am theoretischen Minimum. Die Firmware arbeitet effizient.

### Wo geht die Zeit verloren bei Ausreissern?

1. **Queue-Stau:** Bei queued=3+ muessen erst andere Pakete gesendet werden (je ~1s Airtime + 5s Wait)
2. **CAD Busy Chains:** Mehrere aufeinanderfolgende CAD-Busy-Detektionen verzoegern den TX
3. **Relay-Prioritaet:** Relay-Pakete (Prio 3-5) haben hoehere/gleiche Prioritaet und werden bevorzugt

### Empfehlung

- Die **Grundlatenz von ~5s ist nicht reduzierbar** ohne Aenderung der CSMA-Parameter (kuerzere Backoff-Zeit wuerde Kollisionen erhoehen)
- **DK5EN-12** sollte untersucht werden (48s Ausreisser deuten auf Ueberlastung hin)
- Fuer zeitkritische Nachrichten koennte eine **hoehere Prioritaet fuer user_msg** (aktuell Prio 2) helfen, um Queue-Stau zu vermeiden -- allerdings ist Prio 2 bereits hoch
- Die 80% <10s sind fuer ein LoRa-Mesh-Netzwerk mit SF11 **sehr gut**
