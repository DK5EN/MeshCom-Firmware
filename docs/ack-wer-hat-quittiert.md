# ACK mit Absender: Wer hat quittiert?

Stand 2026-09-05, Codebasis fork-main (`ffe31ca5`). Analyse, kein Code.
Vokabular 2026-09-05 an McApp angeglichen (siehe Abschnitt 2). Entscheidungen vom
2026-09-05, alle drei normativ fuer die Firmware-Umsetzung:

- **Draht (4.1):** 22-Bit-Node-Hash, 3 Byte, kein Rufzeichen. Der Node loest den Hash selbst
  auf (Abschnitt 4.1).
- **BLE (4.2):** Laengenbyte statt Trenner, Rufzeichen zur App. McApp parst genau dieses
  Format seit 2026-09-05 (`ble_protocol.parse_ack_appendix`); ein Trenner-Format ohne Laenge
  wuerde dort als ungueltiger Anhang verworfen. Das ACK kaeme an, das Rufzeichen nicht.
- **App-Gate (5.5):** fluechtiges Session-Flag `--ackinfo on`, Reset bei BLE-Disconnect.
  McApp setzt es bei jedem Connect selbst (`ble_adapter.ACK_ATTRIBUTION_COMMAND`).

McApp-Seite: `MCProxy/doc/2026-09-05_1545-ack-attribution-plan.md`.

Stephans Wunsch: In der App soll bei der Wolke bzw. der Wolke mit Hakerl sichtbar sein, **wer**
quittiert hat. Dafuer muss man wissen, was der Node heute ueber den Quittierenden weiss, was er
davon an die App weitergibt, und was auf dem Draht ueberhaupt mitkommt.

## 1. Kurzfassung

- Der Frame zur App ist leicht erweiterbar. Der Node kennt den Absender aber nur in zwei von
  drei Faellen.
- **Node ACK** (heard: ein Nachbar hat meinen Frame wiederholt): Rufzeichen liegt vor, nur
  Firmware noetig.
- **Peer ACK** (DM-ACK, Wolke mit Hakerl): Absender ist der DM-Partner, den die App sowieso
  kennt. Neu waere nur: kam es ueber LoRa oder ueber den Server.
- **Gateway ACK** (Wolke): Der 12-Byte-Binaerframe auf dem Draht traegt **kein** Rufzeichen und
  auch keinen Node-Hash. Das ist der interessante Fall, und er braucht eine Aenderung am
  Funkprotokoll, die auf allen Nodes ankommen muss.

## 2. Was die App heute bekommt

Jeder Statuswechsel geht als ein Frame ueber BLE, gebaut in `addBLEOutBuffer()`
(`src/loop_functions.cpp:571`). Der Node haengt hinter jedes Nicht-JSON-Frame vier Byte
Unix-Zeit an und schreibt die Gesamtlaenge in Byte 0 des Ringpuffers.

```
0x41 | msg_id (4 Byte, LE) | Status | 0x00        + Unix-Zeit (4 Byte, BE)

Status 0x00  Node ACK     "heard": Nachbar hat meinen Frame wiederholt    (lora_functions.cpp:838)
Status 0x01  Gateway ACK  "server reached", oder Node ist selbst GW       (lora_functions.cpp:364, loop_functions.cpp:4131)
Status 0x02  Peer ACK     Empfaenger hat quittiert, nur DM                (lora_functions.cpp:992, udp_functions.cpp:406)
```

Vokabular: Die drei Namen sind die von McApp (`ble_protocol.py`, `ack_kind` auf dem SSE-Event
`msg:status`: `node` / `gateway` / `peer`) und mc-chat. "heard" und "server reached" bleiben
als Erklaerung in Klammern, nicht als Bezeichner. Der Peer ACK ist das, was die App als Wolke
mit Hakerl zeigt; Node und Gateway ACK sind Transportbestaetigungen, nie eine Zustellung.

Byte 6 ist ein fixer Terminator. Ein Rufzeichen steht nirgends. Die App findet den Zeitstempel
vermutlich ueber einen festen Offset, das ist die "Auswertung in der App", die Stephan meint.

## 3. Was der Node ueber den Quittierenden weiss

| Status            | Absender bekannt? | Woher                                                                  | Bewertung                                  |
| ----------------- | ----------------- | ---------------------------------------------------------------------- | ------------------------------------------ |
| 0x00 Node ACK     | ja                | Pfad des wiederholten Frames, Firmware loggt schon `HEARD from <call>` | Firmware-only, wenige Zeilen               |
| 0x02 Peer ACK     | ja, aber trivial  | `msg_source_call` der Textmeldung `DEST:ack123`                        | Neu waere nur "via LoRa" vs. "via Server"  |
| 0x01 Gateway ACK  | **nein**          | Binaerframe ohne Rufzeichen, eigene msg_id ist `millis()`              | Braucht Protokollerweiterung auf dem Draht |
| 0x01 GW = eigener | entfaellt         | Node selbst ist GW mit IP, sendet sich den Status sofort               | Rufzeichen = eigenes                       |

Der Gateway-ACK-Frame auf dem Draht (`src/lora_functions.cpp:1210`):

```
Byte 0      0x41               MSG_TYPE_ACK
Byte 1..4   ack msg_id (LE)    = millis(), reine Dedup-Kennung
Byte 5      0x80 | max_hop     Bit 7 Server-Flag, Bit 0..6 Resthops
Byte 6..9   msg_id (LE)        die quittierte Nachricht
Byte 10     0x01               GW/Node, in der Praxis immer 0x01
Byte 11     0x00               Terminator
```

Anders als beim Text-ACK ist die eigene msg_id hier **nicht** nach dem Schema
`(GW_ID << 10) | counter` gebaut. Nicht einmal der 22-Bit-Hash des Gateways steckt drin.

Zwei Sperren begrenzen ausserdem, was die App sieht:

- Nur das **erste** Gateway ACK geht zur App (`own_msg_id[..][4] < 2`,
  `src/lora_functions.cpp:364`). Weitere ACKs werden nur noch zum Retransmit-Stopp genutzt.
- Nur der **erste** Node ACK (heard) geht zur App (`== 0x00`, `src/lora_functions.cpp:838`).

Fuer eine Liste "wer hat quittiert" muessen beide Sperren fallen.

## 4. Vorschlag

### 4.1 Auf dem Draht (Gateway ACK)

**Entschieden 2026-09-05: 22-Bit-Node-Hash, nicht Rufzeichen.** Airtime ist die knappe
Ressource (Abschnitt 5.4: rund 90 ACK-Frames pro Node und Stunde, ein Rufzeichen verdoppelt
den Nutzteil). Der Hash ist derselbe, den der Node fuer seine eigene msg_id bildet
(`(GW_ID << 10) | counter`), also kann jeder Node ihn ueber seine MHeard-Liste aufloesen:
Hash aller bekannten Rufzeichen bilden, vergleichen. Bleibt er unaufgeloest, geht er als
Hex-Token gross geschrieben zur App (`H3A5F21`), das passt in den Zeichensatz des Anhangs.

```
Byte 0..10   wie bisher
Byte 11      n = Laenge des Anhangs, 0 = altes Format, hier 3
Byte 12..14  Node-Hash des Gateways, 22 Bit in 3 Byte (LE, Bit 22..23 = 0)
```

Neue Laenge 15 Byte statt fix 12. Das Laengenbyte bleibt, damit ein spaeterer Anhang (z. B.
Rufzeichen, falls die Messung Luft laesst) ohne neue Frame-Revision moeglich ist; der Parser
akzeptiert n = 0 und n = 3, alles andere verwirft er als Anhang und behaelt das ACK (5.2).

### 4.2 Zur App (BLE)

Gleiches Muster wie 4.1, **mit Laengenbyte**: Byte 6 wird von "immer 0x00" zur Laenge des
Anhangs, danach das Rufzeichen, danach wie heute die vier Byte Unix-Zeit.

```
Byte 0      0x41
Byte 1..4   msg_id (LE)
Byte 5      Status 0x00 / 0x01 / 0x02
Byte 6      n = Laenge des Anhangs, 0 = altes Format
Byte 7..    Rufzeichen, n Byte, [A-Z0-9-], n <= 10
danach      Unix-Zeit, 4 Byte
```

Warum ein Laengenbyte und nicht "0x00 als Trenner, Zeit ueber `len - 4`": Der Node haengt heute
hinter die Zeit ein Pad-Byte (13 statt 12 Byte auf dem Draht, in McApp als reale Aufzeichnung
gepinnt), und die Zeit-Bytes selbst koennen im Rufzeichen-Zeichensatz liegen. Ohne Laenge
kann ein Parser `DK5EN-98` nicht von `DK5EN-98A` + verschobener Zeit unterscheiden. Mit Laenge
ist die Regel auf BLE und Draht dieselbe, und alte Firmware (Byte 6 == 0x00) ist automatisch
das alte Format. McApp liest die Zeit ohnehin nicht (stempelt Ankunft), parst den Anhang seit
2026-09-05 nach genau dieser Regel und verwirft ihn bei jedem Verstoss, ohne das ACK zu
verlieren (`ble_protocol.parse_ack_appendix`).

Fuer Node ACK und Peer ACK ist das Rufzeichen sofort da. Fuer den Gateway ACK kommt es erst,
wenn das sendende Gateway und alle Relays auf dem Weg die neue Firmware haben.

### 4.3 Reihenfolge

1. Firmware: Parser fuer den Anhang auf allen Nodes, toleriert altes und neues Format. Relays
   leiten die volle Laenge weiter. Noch kein Node sendet den Anhang.
2. App: Parser fuer den variablen Frame (Laengenbyte, 4.2). McApp: erledigt 2026-09-05,
   vertraegt altes und neues Format.
3. Firmware: Node ACK und Peer ACK mit Rufzeichen zur App. Sichtbares Ergebnis fuer Stephan.
4. Firmware: Gateways senden den Hash-Anhang auf dem Draht (4.1), Nodes loesen ihn zum
   Rufzeichen auf. `--ackinfo on` als fluechtiges Flag (5.5); mit Flag fallen die Sperren
   fuer Mehrfach-ACK.

Das passt zu dem, was `docs/backpressure-protocol.md` fuer die fehlenden Zustaende (refused,
dropped, on air) sowieso vorschlaegt. Eine Frame-Revision, nicht zwei.

## 5. Was auf dem Draht nichts kaputt machen darf

### 5.1 Verhalten alter Firmware bei einem 22-Byte-ACK

Geprueft an fork-main, fuer upstream DEV noch zu verifizieren (siehe 5.7):

- `handleACK()` prueft `size < 12` und `payload[0] == 0x41`, dann `isPlausibleAckFrame()`.
  Die Plausibilitaet haengt nur an Byte 5. Ein laengerer Frame **besteht** alle Pruefungen.
- `memcpy(print_buff, payload, 12)` kopiert genau 12 Byte. Der Anhang wird ignoriert, nicht
  fehlinterpretiert.
- Dedup laeuft ueber Byte 1..4. Unveraendert, alte und neue Nodes sehen denselben Frame als
  denselben.
- Retransmit-Stopp und Statusmeldung zur App laufen ueber Byte 6..9. Unveraendert.
- Relay: `print_buff[5]--` und `addTxRingEntry(print_buff, 12, ...)`. Ein alter Relay
  **kuerzt** das ACK auf 12 Byte. Das Rufzeichen geht auf diesem Hop verloren, das ACK selbst
  kommt an.

Ergebnis: Alte Nodes verhalten sich mit dem neuen Frame exakt wie heute. Das Schlimmste ist ein
fehlendes Rufzeichen, nie ein verlorenes ACK.

### 5.2 Was der neue Parser nie tun darf

- Ein ACK wegen eines kaputten Anhangs verwerfen. Der Anhang ist Zusatzinformation. Ungueltige
  Laenge, Zeichen ausserhalb `[A-Z0-9-]`, `n > 10` oder `12 + n > size`: Anhang verwerfen,
  Frame als 12-Byte-ACK weiterverarbeiten.
- Byte 10/11 als Verwerfungsgrund nutzen. Die Feldmessung in `src/ack_functions.h` zeigt
  0,4 Prozent gueltiger ACKs mit abweichenden Werten dort, vermutlich aeltere Firmware. Ein
  Byte 11 ungleich 0 ohne passenden Anhang muss daher als "altes Format" gelten, nicht als
  Fehler.
- Den Anhang aus einem Puffer lesen, der kuerzer ist als `12 + n`. Die Laengenpruefung kommt
  vor jedem Zugriff.

### 5.3 Byte 5 bleibt der Diskriminator

`isPlausibleAckFrame()` bleibt unveraendert. Sie hat den 'A'-Bruchstueck-Befall im Feld beendet
(506 Fremdframes in 32,7 h). Der Anhang darf diese Pruefung nicht aufweichen und nicht ersetzen.

### 5.4 Airtime

Mit dem Hash (4.1) waechst ein Gateway ACK von 12 auf 15 Byte Payload. Im DG0OPK-Korpus
liefen etwa 90 ACK-Frames pro Node und Stunde. Die Rufzeichen-Variante (bis 22 Byte) wurde
deshalb am 2026-09-05 verworfen. Vor dem Rollout die 3 Byte trotzdem mit den echten
Funkparametern rechnen und gegen die Kanalauslastung der `--setlog`-Messung halten.

### 5.5 Mehrfach-ACK zur App

Faellt die Sperre "nur das erste ACK", bekommt die App pro Nachricht mehrere Frames mit Status
0x01. Eine alte App, die den Status als Zustandsmaschine fuehrt, verkraftet das vermutlich
(gleicher Zustand nochmals gesetzt). McApp: idempotent, ein Wiederholungsframe derselben
Station ist ein No-op in der Tabelle `message_acks` und ein doppeltes SSE-Event.

**Entschieden 2026-09-05:** Der Handshake hat kein Faehigkeitsflag, also ein Kommando als
Ersatz: `--ackinfo on`, ein **fluechtiges** Session-Flag im RAM, Reset bei jedem
BLE-Disconnect, nie ins Flash. Solange es nicht gesetzt ist, gilt die heutige Sperre. Die
offizielle App sendet es nicht und sieht keinen Unterschied, auch nicht am selben Node, nachdem
McApp dort verbunden war. McApp sendet es in seinem Post-Connect-Burst als erstes Kommando,
vor `--io` und `--tel` (`ble_service/src/ble_adapter.py`, `query_extended_registers`), damit
schon die ACKs zu Nachrichten aus dem Burst-Fenster ungegated sind. Alte Firmware antwortet
darauf mit `--wrong command --ackinfo on` auf dem Command-Back-Kanal; das ist harmlos und
wird geloggt.

Optional als Gurt und Hosentraeger: Status 0x00 (Node ACK) ohne Flag freigeben und nur 0x01
und 0x02 gaten. Ein zweiter identischer Heard-Frame ist das Harmloseste, was eine alte App
bekommen kann, und die Heard-Liste ist das sichtbare Ergebnis fuer Stephan.

### 5.6 Server bleibt unberuehrt

ACK-Frames erreichen den Server nicht. `handleACK()` kehrt bei `src/lora_functions.cpp:530`
zurueck, `addNodeData()` (Uplink zum Server) wird erst bei Zeile 1360 gerufen. Der neue Anhang
kann also keinen Server-Parser treffen. Gleiches gilt fuer die UDP-Rueckrichtung, dort kommt
das ACK als Text `:ack123`, nicht binaer.

### 5.7 Vor der Umsetzung pruefen

- **Upstream DEV** `handleACK()`: hat upstream die `size < 12`-Pruefung und den 12-Byte-memcpy
  ebenfalls, oder kopiert dort etwas die volle Laenge in einen kleinen Puffer? Nur dann waere
  ein 22-Byte-ACK auf alten Nodes gefaehrlich. In fork-main ist es sicher.
- **App-Parser**: fester Offset fuer den Zeitstempel oder `len - 4`? Bei festem Offset zeigt
  eine alte App das Rufzeichen als Zeit an. McApp: weder noch, die Zeit wird nicht gelesen
  (`transform_ack` stempelt die Ankunft); ein laengerer Frame dekodiert wie ein 12-Byte-Frame.
- **Puffergroessen** in der Kette `print_buff[30]`, `BLEtoPhoneBuff`, Ring-Slot. 22 Byte plus
  Anhang plus Zeit passt ueberall, trotzdem mit dem Laengenbyte (uint8_t) gegenrechnen.
- **Ringpuffer-Slot 12 vs. size**: Der Relay muss `size` statt der Konstante 12 weiterreichen,
  aber nach oben auf 22 klemmen.

### 5.8 Nachweis

- `test/test_ack_validate` (nativ) um Anhang-Faelle erweitern: n = 0, n = 6, n = 10, n = 11,
  Sonderzeichen, `12 + n > size`, Byte 11 ungleich 0 ohne Anhang. Jeder Fall muss das ACK
  akzeptieren, nur der Anhang darf fallen.
- `test/test_ack_replay` mit einem aufgezeichneten 22-Byte-ACK.
- Bench: ein Node alte Firmware, einer neue. Neue Firmware sendet 22-Byte-ACK, alter Node
  muss Retransmit stoppen und Status 0x01 zur App geben. Umgekehrt: alter Node sendet
  12 Byte, neuer Node darf keinen Anhang halluzinieren. Die `[LOG]`-Zeilen zeigen die
  Frame-Laenge, das reicht als Instrument.

## 6. extUDP

### 6.1 Was heute existiert

Ausgehend (`sendExtern()`, `src/extudp_functions.cpp:445`), jeweils als JSON-Datagramm:

| Typ    | Ausloeser                                 | Kennzeichnung                            |
| ------ | ----------------------------------------- | ---------------------------------------- |
| `pos`  | jedes Positionsframe (LoRa, UDP, eigenes) | `src_type` lora / udp / node, `msg_id`   |
| `msg`  | jedes Textframe (LoRa, UDP, eigenes)      | `src`, `dst`, `msg`, `msg_id`            |
| `tele` | zu jedem Positionsframe zusaetzlich       | qfe, qnh, pressure_alt                   |
| Notice | QRS/QRT/QTA-Rueckmeldung (BP-01, BP-06)   | als `msg` vom eigenen Rufzeichen getarnt |

Eingehend (`getExtern()`, Zeile 239): `msg` (dst, msg) und `tele`. Sonst nichts.

Nuetzlich fuer den Peer: Eine ueber extUDP eingespeiste Nachricht wird als `src_type:"node"`
mit ihrer `msg_id` zurueckgespiegelt (`src/loop_functions.cpp:4181`). Der Peer kann seine
Nachricht also der msg_id zuordnen. DMs bekommen dabei automatisch die ACK-Anforderung
`{nnn}` (`src/loop_functions.cpp:4027`).

### 6.2 Was fehlt

- **Kein Zustellstatus.** Der 0x41-Frame erreicht extUDP nie: `handleACK()` kehrt bei Zeile 530
  zurueck, `queueExtern()` wird erst bei Zeile 890 gerufen, und `sendExtern()` wuerde ein
  Binaerframe ohnehin verwerfen (`decodeAPRS()` liefert 0). Weder Node ACK noch Gateway ACK
  noch Peer ACK kommen als Status beim Peer an.
- **DM-ACK nur als Text.** Das Text-ACK `DEST:ack123` ist ein normales 0x3A-Frame und geht als
  `msg` raus. Der Peer muss `:ack` selbst erkennen und die dreistellige Nummer ueber die
  msg_id-Regel `(GW_ID << 10) | ack_id` selbst auf seine Nachricht zurueckrechnen.
- **Notice als getarnte Nachricht.** Bewusste Entscheidung vom 2026-08-31, weil McApp nur die
  bekannten Formen rendert. Ein Peer kann eine Notice nicht maschinell von einer Meldung
  unterscheiden.
- **Kein Node ACK.** Der Peer erfaehrt nicht, ob und von wem seine Nachricht wiederholt wurde.

### 6.3 Was zu ergaenzen waere

Ein Status-Datagramm, das den BLE-Frame spiegelt und dort abgesetzt wird, wo heute
`addBLEOutBuffer(print_buff, 7)` fuer 0x41 gerufen wird:

```json
{
  "type": "ack",
  "msg_id": "1A2B3C4D",
  "status": 1,
  "from": "OE1XYZ-12",
  "via": "lora"
}
```

- `status` 0 Node ACK, 1 Gateway ACK, 2 Peer ACK, spaeter 3..6 wie im Backpressure-Vorschlag.
- `from` das Rufzeichen, wo bekannt, sonst weglassen. Kommt fuer den Gateway ACK erst mit dem
  Anhang aus Abschnitt 4.1.
- `via` lora oder udp, weil der Node das beim Peer ACK unterscheiden kann.
- McApp nimmt das Datagramm seit 2026-09-05 an (`udp_handler.normalize_extudp_ack`): `msg_id`
  8 Hex-Zeichen, `status` 0..2, sonst verworfen; ungueltiges `from`/`via` verwirft nur das
  Feld. Vorher fiel jedes Datagramm ohne `msg` in einen DEBUG-Log und war weg.
- Testbar wie `extern_notice_json.h`: reine Funktion im Header, nativer Test daneben.

Kein Absetzen aus `OnRxDone` heraus, sondern ueber `queueExtern()` in die Hauptschleife, aus
demselben Grund wie heute bei den Textframes. Die Queue hat zwei Plaetze, mit Status-Frames
wird sie enger. Vor der Umsetzung `MAX_EXTERN_QUEUE` gegen die erwartete Rate halten.

Alle drei Stufen (Node ACK, Peer ACK mit Absender, Gateway ACK) kommen beim Peer dann im selben
Datagramm an, ohne dass extUDP fuer die Gateway-Erweiterung ein zweites Mal angefasst wird.
