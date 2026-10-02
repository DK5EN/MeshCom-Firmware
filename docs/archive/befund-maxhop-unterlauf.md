# Befund: 4-Bit-Unterlauf des MeshCom-Hop-Zaehlers (`max_hop`)

| Feld                | Wert                                                                                       |
| ------------------- | ------------------------------------------------------------------------------------------ |
| Status              | Verifiziert                                                                                |
| Kind                | findings                                                                                   |
| Verified            | 2026-09-20                                                                                 |
| Datenquelle         | `mcapp.local:/var/lib/mcapp/messages.db` (Node DK5EN), Knotenstammdaten aus mcmap-prod MCP |
| Beobachtungsfenster | 2026-08-21 bis 2026-09-20, 10448 LoRa-Saetze                                               |
| Firmware-Referenz   | `MeshCom-Firmware-DEV-Main`, Stand 2026-09-20                                              |

## BLUF

Nachrichten laufen im Netz deutlich weiter als die konfigurierten 4 Hops, weil ein Node
unter einer klar eingegrenzten Bedingung den Hop-Zaehler **ueberlaufen** laesst: `max_hop` ist
ein 4-Bit-Feld, ein Dekrement von 0 ergibt gesendet `0x0F` = **15**. Ab diesem Punkt hat der
Frame ein frisches 15-Hop-Budget.

Ursache ist **kein fehlender Guard und keine alte Firmware**, sondern eine Flag-Verschmutzung in
`src/lora_functions.cpp:1234`: im Zweig "Broadcast- oder Gruppennachricht bei nicht verbundener
App" wird Bit `0x20` direkt in `aprsmsg.max_hop` geodert. Der Guard `if(aprsmsg.max_hop > 0)`
sieht danach `0x20` und laesst das Relay passieren, obwohl das Hop-Nibble bereits 0 ist. Das
Dekrement borgt aus dem Flag-Bit: `0x20 - 1 = 0x1F`, beim Encoden `& 0x0F` = 15.

**Der Fehler trifft ausschliesslich Broadcast- und Gruppennachrichten. Direktnachrichten sind
strukturell nicht betroffen** -- und genau das zeigen die Messdaten.

## 1. Mechanismus

### 1.1 Die verschmutzende Zeile

```c
// src/lora_functions.cpp:1234, in OnRxDone()
if((strcmp(destination_call, "*") == 0 && !bNoMSGtoALL) || CheckOwnGroup(destination_call))
{
    queueDisplayText(aprsmsg, rssi, snr);
    if(isPhoneReady == 0)                      // keine App/BLE verbunden
    {
        aprsmsg.max_hop = aprsmsg.max_hop | 0x20;   // msg_app_offline = true
        ...
    }
}
```

### 1.2 Warum das durchschlaegt

`OnRxDone()` arbeitet durchgehend auf **derselben** `aprsmsg`-Instanz. Zwischen Zeile 1234 und
dem Relay-Block ab Zeile 1496 wird nicht neu dekodiert (nur `decodeAPRSPOS()` auf dem Payload).
Der Relay-Block lautet:

```c
// src/lora_functions.cpp:1496
if(aprsmsg.max_hop > 0)
{
    aprsmsg.max_hop--;
    ...
}
```

Rechnung fuer ein erschoepftes Paket (`max_hop`-Nibble = 0) auf einem Node ohne App:

| Schritt                                     | Wert von `aprsmsg.max_hop`             |
| ------------------------------------------- | -------------------------------------- |
| Nach `decodeAPRS()` (`RcvBuffer[5] & 0x0F`) | `0x00`                                 |
| Nach Zeile 1234 (`\| 0x20`)                 | `0x20`                                 |
| Guard `max_hop > 0`                         | **wahr** -- haette falsch sein muessen |
| Nach `max_hop--`                            | `0x1F`                                 |
| Gesendet (`msg_buffer[5] = max_hop & 0x0F`) | **`15`**                               |

Solange das Nibble > 0 ist, ist die Verschmutzung folgenlos (`0x23 - 1 = 0x22`, gesendet 2).
Erst bei Nibble 0 borgt das Dekrement in das Flag-Bit hinein. Das ist der komplette Fehler.

### 1.3 Nebenbefund: die Zeile erreicht ihr eigenes Ziel nicht

`encodeAPRS()` setzt Byte 5 als `aprsmsg.max_hop & 0x0F` und ODERT `0x20` danach aus dem
**Bool** `aprsmsg.msg_app_offline` dazu (`src/aprs_functions.cpp:1020-1028`). Das in `max_hop`
geoderte `0x20` wird also wieder ausmaskiert -- der BLE-Frame bekommt das App-Offline-Flag gar
nicht gesetzt. Die Zeile richtet nur Schaden an, ohne zu wirken.

### 1.4 Vorgeschlagene Korrektur

```c
-        aprsmsg.max_hop = aprsmsg.max_hop | 0x20;   // msg_app_offline = true
+        aprsmsg.msg_app_offline = true;
```

Ein Einzeiler, der exakt das tut, was der bestehende Kommentar behauptet. `encodeAPRS()`
serialisiert das Flag bereits aus diesem Bool, der Hop-Zaehler bleibt unberuehrt.

## 2. Beweislage

### 2.1 Entscheidender Test: nur Broadcast und Gruppe

Der verschmutzende Zweig ist ausschliesslich fuer `dst == "*"` oder eine beigetretene Gruppe
erreichbar. Wenn die Hypothese stimmt, darf es unter Direktnachrichten **keinen einzigen**
Wrap geben. Alle LoRa-Saetze mit belegtem `max_hop`:

| Zielart            | Saetze | davon `max_hop >= 7` (Wrap) | Anteil |
| ------------------ | -----: | --------------------------: | -----: |
| Direkt / DM        |   8878 |                       **0** | 0.00 % |
| Gruppe (numerisch) |    329 |                          16 | 4.86 % |
| Broadcast `*`      |    100 |                           1 | 1.00 % |

Null Treffer unter 8878 Direktnachrichten bei 4.9 % Trefferquote in der Gruppe -- die Trennung
folgt exakt der Code-Verzweigung.

### 2.2 Unabhaengige Gegenprobe ueber volle 30 Tage: Pfadlaengen

Die `max_hop`-Spalte wird fuer `src_type=lora` erst seit **2026-09-10 20:55** befuellt
(9307 Saetze). Die Pfadlaenge ist aber ueber die vollen 30 Tage verfuegbar und
zeigt dasselbe Bild ohne den Hop-Wert zu brauchen.

Legale Obergrenze: eine Gateway-Einspeisung (dekrementiert bewusst nicht, siehe Abschnitt 4)
plus `MAXHOP_TEXT_MAX` = 6 Relays ergibt hoechstens 7 Pfadeintraege, realistisch 5 beim
Standardwert 4.

| Pfadlaenge | Direkt | Gruppe | Broadcast |
| ---------: | -----: | -----: | --------: |
|          1 |   2486 |    142 |        83 |
|          2 |   4893 |     66 |        16 |
|          3 |    146 |    257 |        19 |
|          4 |    116 |    191 |        23 |
|          5 |     20 |     70 |         4 |
|      **6** |      0 |  **4** |     **0** |
|      **7** |      0 |  **9** |     **4** |
|      **8** |      0 |  **1** |     **0** |
|      **9** |      0 |  **1** |     **0** |

**Direktnachrichten erreichen in 30 Tagen nie mehr als 5 Pfadeintraege.** Jeder einzelne der 19
Faelle mit 6 und mehr Eintraegen ist eine Gruppen- oder Broadcast-Nachricht. Dieselbe Trennlinie
wie in 2.1, aus einer voellig anderen Spalte gewonnen.

### 2.3 Bimodale Verteilung der Hop-Werte

Beobachtete `max_hop`-Werte bei Empfang: 0, 1, 2, 3, 4, 5 -- dann eine Luecke von 6 bis 11 --
dann 12, 13, 14, 15. Konfigurierbar sind laut `src/maxhop.h:18` nur `MAXHOP_TEXT_MIN=1` bis
`MAXHOP_TEXT_MAX=6`; Positionsframes stehen fest auf `MAX_HOP_POS_DEFAULT=2`. **Jeder Wert ab 7
ist damit per Konfiguration unerreichbar** und kann nur aus dem Wrap stammen.
(`tools/berglog.py:1029` setzt die Schwelle konservativer auf 13.)

## 3. Auswertung der Ereignisse

Alle 29 Saetze der letzten 30 Tage mit Pfadlaenge >= 6 **oder** `max_hop >= 7`.
Der Wrap-Knoten ist rueckgerechnet: der letzte Pfadeintrag sendete mit dem empfangenen Wert,
jedes vorhergehende Relay mit +1; der Knoten, der 15 gesendet hat, ist der Verursacher.
Bei `max_hop = NULL` ist keine Rueckrechnung moeglich.

| Zeit (Europe/Vienna) | Ziel         | Absender  | Laenge | `max_hop` | abgeleiteter Wrap-Knoten |
| -------------------- | ------------ | --------- | -----: | --------: | ------------------------ |
| 04.09.2026, 22:21    | Broadcast    | OE5HWN-12 |      7 |        -- | nicht bestimmbar         |
| 04.09.2026, 22:42    | Broadcast    | OE5HWN-14 |      7 |        -- | nicht bestimmbar         |
| 05.09.2026, 00:18    | Broadcast    | OE5HWN-12 |      7 |        -- | nicht bestimmbar         |
| 11.09.2026, 13:18    | Gruppe 9     | DJ8MEH-8  |      5 |        15 | **DB0ED-99**             |
| 12.09.2026, 08:39    | Gruppe 9     | DK5DM-0   |      5 |        15 | **DB0ED-99**             |
| 12.09.2026, 20:44    | Gruppe 2628  | DK5DM-0   |      6 |        -- | nicht bestimmbar         |
| 12.09.2026, 20:58    | Gruppe 222   | IU5CZN-2  |      7 |        -- | nicht bestimmbar         |
| 12.09.2026, 23:18    | Gruppe 232   | OE5HWN-16 |      6 |        14 | **DB0HOB-12**            |
| 14.09.2026, 09:02    | Gruppe 9     | DK1TCP-77 |      6 |        14 | **DB0HOB-12**            |
| 14.09.2026, 09:08    | Gruppe 9     | DK5DM-0   |      5 |        15 | **DB0ED-99**             |
| 15.09.2026, 08:24    | Gruppe 9     | DK1TCP-77 |      5 |        15 | **DB0ED-99**             |
| 15.09.2026, 18:44    | Gruppe 70011 | DA6GD-01  |      7 |        -- | nicht bestimmbar         |
| 15.09.2026, 19:13    | Broadcast    | OE5HWN-14 |      7 |        13 | **DL1MSE-12**            |
| 16.09.2026, 08:34    | Gruppe 26299 | DO7FJK-99 |      7 |        -- | nicht bestimmbar         |
| 16.09.2026, 12:08    | Gruppe 26299 | DO5DMF-99 |      7 |        -- | nicht bestimmbar         |
| 16.09.2026, 16:19    | Gruppe 9     | DK5DM-0   |      5 |        15 | **DB0ED-99**             |
| 16.09.2026, 17:38    | Gruppe 20    | DL2XL-5   |      9 |        12 | **DL1RHS-25**            |
| 16.09.2026, 22:36    | Gruppe 222   | IK1WNQ-11 |      7 |        -- | nicht bestimmbar         |
| 17.09.2026, 08:37    | Gruppe 9     | DK5DM-0   |      5 |        15 | **DB0ED-99**             |
| 17.09.2026, 11:43    | Gruppe 9     | DK5DM-0   |      5 |        15 | **DB0ED-99**             |
| 17.09.2026, 23:21    | Gruppe 222   | IS0HXK-7  |      8 |        -- | nicht bestimmbar         |
| 18.09.2026, 13:49    | Gruppe 222   | IS0LBH-18 |      7 |        -- | nicht bestimmbar         |
| 18.09.2026, 17:18    | Gruppe 9     | DJ8MEH-8  |      5 |        15 | **DB0ED-99**             |
| 19.09.2026, 04:04    | Gruppe 22299 | IK5ZXH-12 |      7 |        14 | **DB0HOB-12**            |
| 19.09.2026, 09:30    | Gruppe 9     | DD7MH-1   |      6 |        14 | **DB0HOB-12**            |
| 19.09.2026, 12:54    | Gruppe 222   | IS0HXK-7  |      7 |        14 | **DB0HOB-12**            |
| 19.09.2026, 15:17    | Gruppe 9     | DK1TCP-77 |      4 |        13 | **DL4GLE-10**            |
| 19.09.2026, 19:56    | Gruppe 9     | DJ8MEH-8  |      5 |        15 | **DB0ED-99**             |
| 20.09.2026, 10:53    | Gruppe 222   | IS0HXK-7  |      7 |        -- | nicht bestimmbar         |

### 3.1 Wrap-Knoten

| Knoten    | Ereignisse | Firmware | Hardware     | Gateway |
| --------- | ---------: | -------- | ------------ | ------- |
| DB0ED-99  |          9 | 4.35p    | HELTEC V3    | nein    |
| DB0HOB-12 |          5 | 4.35p    | TBEAM V1.2   | nein    |
| DL1MSE-12 |          1 | 4.35c    | TBEAM V1.2   | nein    |
| DL1RHS-25 |          1 | 4.35p    | TLORA V2.1.6 | nein    |
| DL4GLE-10 |          1 | 4.35s    | TLORA V2.1.6 | nein    |

Alle fuenf sind unbemannte Relaisstationen ohne dauerhaft verbundene App -- exakt die
Betriebsart, die `isPhoneReady == 0` erfuellt. DB0HOB-12 (Hochries, 1543 m) und DB0ED-99
(Obermailling, 526 m) sind zusaetzlich die beiden letzten Hops vor dem Messpunkt und daher
ueberrepraesentiert; das ist ein Standortartefakt der Messung, kein Eigenschaftsunterschied.

Die beiden stuetzen sich gegenseitig: empfaengt DB0HOB-12 mit Nibble 1, sendet es legal 0,
DB0ED-99 wrappt auf 15 -- gemessen wird 15. Empfaengt DB0HOB-12 mit 0, wrappt es selbst auf 15,
DB0ED-99 dekrementiert regulaer -- gemessen wird 14. Beide Muster treten auf, in genau den
erwarteten Pfadlaengen.

### 3.2 Firmware der Wrap-Knoten

| Firmware | Ereignisse | Knoten                         |
| -------- | ---------: | ------------------------------ |
| 4.35p    |         15 | DB0ED-99, DB0HOB-12, DL1RHS-25 |
| 4.35c    |          1 | DL1MSE-12                      |
| 4.35s    |          1 | DL4GLE-10                      |

### 3.3 Hardware der Wrap-Knoten

| Hardware     | Ereignisse |
| ------------ | ---------: |
| HELTEC V3    |          9 |
| TBEAM V1.2   |          6 |
| TLORA V2.1.6 |          2 |

### 3.4 Alle Knoten auf betroffenen Pfaden

| Knoten    | Firmware | Hardware     | Gateway | Als Wrap-Knoten |
| --------- | -------- | ------------ | ------- | --------------- |
| DB0AU-12  | 4.35p    | HELTEC E290  | ja      | --              |
| DB0ED-99  | 4.35p    | HELTEC V3    | nein    | 9x              |
| DB0HOB-12 | 4.35p    | TBEAM V1.2   | nein    | 5x              |
| DB0MMR-12 | 4.35t    | EBYTE E22    | nein    | --              |
| DD7MH-55  | 4.35t    | EBYTE E22    | nein    | --              |
| DK5DM-0   | 4.35t    | T-BEAM 1W    | nein    | --              |
| DL1MSE-12 | 4.35c    | TBEAM V1.2   | nein    | 1x              |
| DL1RHS-14 | 4.35p    | TBEAM V1.2   | nein    | --              |
| DL1RHS-24 | 4.35p    | TLORA V2.1.6 | ja      | --              |
| DL1RHS-25 | 4.35p    | TLORA V2.1.6 | nein    | 1x              |
| DL2RN-13  | 4.35p    | TLORA V2.1.6 | nein    | --              |
| DL4GLE-10 | 4.35s    | TLORA V2.1.6 | nein    | 1x              |
| DL4MFH-55 | ?        | TLORA V2.1.6 | nein    | --              |
| DL8HW-12  | 4.35d    | TLORA V2.1.6 | ja      | --              |
| DM0BGH-12 | 4.35p    | TLORA V2.1.6 | ja      | --              |
| DM0WKB-12 | 4.35p    | TLORA V2.1.6 | nein    | --              |
| DM5HR-12  | 4.35p    | TLORA V2.1.6 | nein    | --              |
| DO1MH-12  | 4.35p    | HELTEC V3    | ja      | --              |
| DO7DH-23  | 4.35t    | TLORA V2.1.6 | ja      | --              |
| DO7GH-0   | 4.35t    | T-BEAM 1W    | nein    | --              |
| OE2XZR-12 | 4.35p    | RAK4631      | ja      | --              |
| OE5HWN-12 | 4.35t    | HELTEC V3    | ja      | --              |
| OE5MAO-13 | 4.35p    | TLORA V2.1.6 | nein    | --              |
| OE5MHX-12 | 4.35t    | EBYTE E22    | nein    | --              |
| OE5XFK-12 | 4.35p    | TLORA V2.1.6 | ja      | --              |
| OE5XLM-12 | 4.35t    | HELTEC V3    | ja      | --              |

`?` bei DL4MFH-55: weder mcapp noch mcmap-prod fuehren fuer diesen Knoten eine Firmware; er ist
in mcmap positionslos und seit 2026-08-20 nicht mehr gesehen. Die Hardware stammt aus dem
eigenen Empfang von mcapp.

### 3.5 Firmware-Verteilung im Vergleich

| Firmware | Wrap-Knoten (5) | alle Pfadteilnehmer | Netz gesamt (mcmap, 1496 meldende) |
| -------- | --------------: | ------------------: | ---------------------------------: |
| 4.35c    |               1 |                   1 |                                 11 |
| 4.35d    |               0 |                   1 |                                 25 |
| 4.35p    |               3 |                  14 |                                743 |
| 4.35s    |               1 |                   1 |                                 96 |
| 4.35t    |               0 |                   8 |                                390 |
| ?        |               0 |                   1 |                                 -- |

Kein Build sticht heraus: 4.35c, 4.35p und 4.35s wrappen alle. Das passt zum Codebefund --
die verschmutzende Zeile ist seit Langem unveraendert und nicht versionsabhaengig. Unter den
abgeleiteten Wrap-Knoten ist zwar kein 4.35t, aber 4.35t-Knoten treten in den Pfaden nur an
Positionen mit Restbudget auf und wurden dort nie getestet. **Das ist kein Freispruch fuer
4.35t** -- der Codepfad existiert dort unveraendert.

## 4. Abgrenzung: Gateways dekrementieren beim Einspeisen bewusst nicht

Speist ein Gateway einen Frame vom Server auf LoRa ein, haengt es sein Rufzeichen an
`msg_source_path` an und laesst `max_hop` unveraendert (`src/esp32/udp_frame_esp32.cpp`,
gespiegelt in `src/nrf52/udp_frame_nrf52.cpp`). Jede Funkinsel hinter einem Gateway startet
damit mit vollem Budget.

**Das ist gewolltes Verhalten und kein Fehler.** Es ist der Grund, warum ein Pfad viele
Eintraege haben kann, ohne dass eine einzelne HF-Wolke mehr als 4 Hops nutzt, und es ist in den
Daten sichtbar als 89 Saetze mit Pfadlaenge 1 und vollem `max_hop = 4`. Hier nur als
Interpretationshilfe fuer Abschnitt 3 festgehalten -- die Rueckrechnung des Wrap-Knotens nimmt
ein Dekrement pro Pfadeintrag an und verschiebt sich um eine Position, wenn mitten im Pfad eine
zweite Gateway-Einspeisung stattgefunden hat.

## 5. Auswirkung

- **Funkzeit.** Ein gewrappter Frame laeuft mit 15 statt 0 Resthops weiter und wird von jedem
  Knoten in Reichweite wiederholt, bis Dedup oder Loop-Erkennung greifen. Die Last ist gering
  -- 17 nachgewiesene Wraps im 10-taegigen Beweisfenster, 19 ueberlange Pfade in 30 Tagen, je
  gemessen an diesem einen Empfaenger. Die Reichweitenwirkung ist betraechtlich: der Anlassfall
  (`CB15E2B1`, 20.09. 10:53, Gruppe 222, Absender IS0HXK-7 auf Sardinien) lief nach der
  Server-Einspeisung in Linz ueber 287 km Funkstrecke in 7 Aussendungen bis Obermailling.
- **Verzerrte Netzkarten.** Die ueberlangen Pfade erzeugen Reception-Edges zwischen Knoten, die
  sich nie direkt gehoert haben.
- **Kein Datenverlust, keine Fehlzustellung.** Die Nachricht selbst bleibt korrekt.

## 6. Grenzen dieser Auswertung

- Einziger Messpunkt ist der Node DK5EN. Wraps, deren Frames diesen Node nie erreichen, fehlen
  vollstaendig. Die absolute Haeufigkeit ist damit eine Untergrenze.
- Der Hop-Wert liegt fuer `src_type=lora` erst ab 2026-09-10 20:55 vor. Die 30-Tage-Aussage in
  2.2 stuetzt sich allein auf Pfadlaengen.
- Der Wrap-Knoten ist rueckgerechnet, nicht beobachtet (siehe Abschnitt 4). Fuer eine
  belastbare Zuordnung einzelner Rufzeichen braeuchte es Per-Hop-Mitschnitte.
- `isPhoneReady` und die Gruppenmitgliedschaft der verdaechtigten Knoten sind aus der Ferne
  nicht pruefbar. Sie sind aus dem Datenmuster erschlossen, nicht bestaetigt.

## 7. Empfohlene naechste Schritte

1. Den Einzeiler aus 1.4 anwenden und einen Regressionstest ergaenzen, der `OnRxDone()` mit
   einer Gruppennachricht bei `max_hop`-Nibble 0 und `isPhoneReady == 0` fuettert und prueft,
   dass **nicht** relayed wird.
2. Zusaetzlich den Guard haerten: `if((aprsmsg.max_hop & 0x0F) > 0)` -- dann ist der Relay-Pfad
   auch gegen kuenftige Flag-Verschmutzung immun.
3. Den Befund an den Firmware-Maintainer melden; die Korrektur ist unabhaengig von der
   Hop-Konfiguration und betrifft jeden unbemannt betriebenen Relais-Node.
