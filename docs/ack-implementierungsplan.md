# ACK mit Absender: Implementierungsplan Firmware

Stand 2026-09-05, Codebasis fork-main (`d62e1a69`). Setzt `docs/ack-wer-hat-quittiert.md` um
(Stand mit den drei normativen Entscheidungen vom 2026-09-05). Noch kein Code.

## 1. Umfang

Entschieden 2026-09-05: Stufe 1 und 3 aus Abschnitt 4.3 des Analysedokuments, Fork-only, kein
Upstream-PR in dieser Runde.

| Stufe | Inhalt                                                                                  | In dieser Runde |
| ----- | --------------------------------------------------------------------------------------- | --------------- |
| 1     | Toleranter Parser fuer den 3-Byte-Hash-Anhang auf dem Draht, Relay reicht volle Laenge  | ja              |
| 2     | App-Parser (McApp)                                                                      | erledigt, McApp |
| 3     | Node ACK, Peer ACK und eigener-GW-Status mit Rufzeichen zur App (Laengenbyte an Byte 6) | ja              |
| 3b    | `--ackinfo on` als fluechtiges Session-Flag, hebt die "nur das erste ACK"-Sperren       | ja, siehe 1.1   |
| 4     | Gateways senden den Hash-Anhang, Nodes loesen ihn ueber MHeard auf                      | nein            |

### 1.1 Warum 3b schon jetzt

Das Analysedokument haengt das Flag an Stufe 4. McApp sendet `--ackinfo on` aber bereits als
erstes Kommando im Post-Connect-Burst, und ohne gelockerte Sperre gibt es pro Nachricht genau ein
Heard mit einem Rufzeichen, also keine Liste. Das Flag kostet eine globale Variable, ein
Kommando und zwei Reset-Stellen; es aendert am Draht nichts. Wer es streichen will, streicht
Abschnitt 3.5 und die Gate-Aenderungen in 3.3.

### 1.2 Was hier ausdruecklich nicht passiert

- Kein Node sendet einen Anhang auf dem Draht. Die Frame-Laenge bleibt 12 Byte.
- Keine Hash-Aufloesung ueber MHeard. Der Gateway-ACK geht wie heute ohne Rufzeichen zur App
  (`n = 0`).
- Keine Aenderung an `isPlausibleAckFrame()`, an Byte 5, an der Dedup-Logik, an
  `FLASH_STRUCT_VERSION` oder an den Statuswerten 0x00/0x01/0x02.
- Kein extUDP-Status-Datagramm (Abschnitt 6.3 des Analysedokuments). Der Emit-Punkt wird so
  gebaut, dass es spaeter an derselben Stelle angehaengt werden kann.

## 2. Frame-Layouts, die die Firmware erzeugt und liest

### 2.1 Zur App (BLE), neu erzeugt

Uebergabe an `addBLEOutBuffer()`, die wie heute vier Byte Unix-Zeit anhaengt und die
Gesamtlaenge in Byte 0 des Ringslots schreibt:

```
Byte 0      0x41
Byte 1..4   msg_id (LE)
Byte 5      Status 0x00 Node ACK (heard) / 0x01 Gateway bzw. Server / 0x02 Peer ACK
Byte 6      n = Laenge des Anhangs, 0 = altes Format
Byte 7..    Rufzeichen, n Byte, [A-Z0-9-], n <= 10, kein NUL
danach      Unix-Zeit, 4 Byte (haengt addBLEOutBuffer an)
```

Laenge an `addBLEOutBuffer()`: `7 + n`. Bei `n = 0` ist der Frame byteidentisch mit heute.

### 2.2 Auf dem Draht, nur gelesen

```
Byte 0..10   wie bisher
Byte 11      n, akzeptiert werden 0 und 3, alles andere gilt als "kein Anhang"
Byte 12..14  Node-Hash, 22 Bit LE, nur wenn n = 3 und size >= 15
```

Der Anhang wird in dieser Runde nur erkannt, im `[LOG]` sichtbar gemacht und beim Relay
mitgefuehrt. Er wird nicht aufgeloest.

## 3. Aenderungen im Einzelnen

### 3.1 Neuer Header `src/ack_attribution.h` (reine Funktionen, nativ testbar)

Gleiches Muster wie `ack_functions.h` und `extern_notice_json.h`: `static inline`, keine
Globals, keine `String`, kein Heap. Muss aus `OnRxDone`-Kontext aufrufbar sein (nRF52 LORA-Task,
siehe Memory "nRF52 OnRxDone in LORA task").

```c
// Draht: liefert 3, wenn Byte 11 == 3 und size >= 15, sonst 0. Nie einen Frame verwerfen.
static inline uint8_t ackWireAppendixLen(const uint8_t *payload, uint16_t size);

// Draht: 22-Bit-Hash aus Byte 12..14, nur gueltig wenn ackWireAppendixLen() == 3.
static inline uint32_t ackWireHash(const uint8_t *payload);

// BLE: baut den 7+n-Byte-Frame aus 2.1 in out (mind. 17 Byte). Rufzeichen wird gegen
// [A-Z0-9-] und n <= 10 geprueft; bei Verstoss oder NULL/leer wird n = 0 gesetzt und der
// Frame ist der heutige 7-Byte-Frame. Rueckgabe: Laenge fuer addBLEOutBuffer().
static inline uint16_t buildAckPhoneFrame(uint8_t *out, uint32_t msgId, uint8_t status, const char *call);
```

Absichtlich kein Uppercase-Umbau im Builder: Rufzeichen liegen in der Firmware bereits
gross vor. Ein Kleinbuchstabe ist ein Datenfehler und fuehrt zu `n = 0`, nie zu einem
halben Rufzeichen.

### 3.2 `src/lora_functions.cpp`, `handleACK()` (Zeile 310 ff.)

- `memcpy(print_buff, payload, 12)` wird zu `memcpy(print_buff, payload, 12 + n)` mit
  `n = ackWireAppendixLen(payload, size)`. `print_buff[30]` reicht (max. 15).
- Relay (Zeile 391): `addTxRingEntry(print_buff, 12 + n, ...)` statt der Konstante 12.
  Dedup, Byte-5-Dekrement, `checkServerRx()` unveraendert.
- Statusmeldung zur App (Zeile 366 ff.): Der heutige Trick `print_buff[5] = 0x41;
addBLEOutBuffer(print_buff + 5, 7)` legt Draht-Byte 11 als BLE-Byte 6 zur App. Mit dem
  Laengenbyte-Parser in McApp ist das ein Fehlerpfad (siehe 5, R1). Ersetzt durch
  `buildAckPhoneFrame(buf, msg_id, 0x01, "")` und `addBLEOutBuffer(buf, len)`. Rufzeichen
  bleibt leer, bis Stufe 4 den Hash aufloest; der Hash wird bei `bLORADEBUG` als
  `ACK_APPENDIX hash=%06X` geloggt, damit ein 15-Byte-Frame am Bench sichtbar ist.
- Gate: `if(own_msg_id[itxcheck][4] < 2)` wird zu `if(bAckInfo || own_msg_id[itxcheck][4] < 2)`.
  Das Setzen von `own_msg_id[..][4] = 0x02` bleibt, damit ohne Flag alles beim Alten ist.

### 3.3 Node ACK (Heard), `src/lora_functions.cpp` Zeile 838 ff.

- Rufzeichen: `aprsmsg.msg_source_last` (der letzte Hop im Pfad, dasselbe Feld, das
  `mheardLine.mh_callsign` befuellt, Zeile 696). Nicht `msg_source_path`, das ist der ganze
  Pfad.
- Frame: `buildAckPhoneFrame(buf, msg_id, 0x00, aprsmsg.msg_source_last.c_str())`.
  `c_str()` auf eine bestehende `String` allokiert nicht.
- Gate: `own_msg_id[icheck][4] == 0x00` wird zu `bAckInfo || own_msg_id[icheck][4] == 0x00`.
  Mit Flag kommt jeder Relay-Hop als eigenes Heard; McApp faltet Wiederholungen derselben
  Station zusammen. Die Firmware fuehrt keine Liste.
- Das bestehende `HEARD from <%s>`-Log bleibt, zeigt zusaetzlich `n`.

### 3.4 Peer ACK und eigener GW-Status

- LoRa-Text-ACK, `src/lora_functions.cpp` Zeile 992 ff.: Status 0x02, Rufzeichen
  `aprsmsg.msg_source_call`. Kein Gate vorhanden, keines noetig.
- UDP-Text-ACK, `src/udp_functions.cpp` Zeile 406 ff.: Rufzeichen `aprsmsg.msg_source_call`.
  Statuswert bleibt wie im Code: 0x01 als Vorgabe, 0x02 sobald `checkOwnTx()` die eigene
  Nachricht findet, also fuer die App immer 0x02. Das Analysedokument stimmt, R4 ist erledigt.
- Eigener GW, `src/loop_functions.cpp` Zeile 4131: Status 0x01, Rufzeichen
  `meshcom_settings.node_call`. `print_buff[8]` dort wird zu `uint8_t buf[24]`.
- Zweiter eigener-GW-Punkt, `src/lora_functions.cpp` ~Zeile 1187: das Gateway hoert seine
  eigene Meldung ueber einen Relay zurueck und meldet 0x01 mit eigenem Rufzeichen. Bei der
  Umsetzung gefunden, gleich behandelt.

### 3.5 `--ackinfo on|off`, fluechtig

- `bool bAckInfo = false;` in `src/loop_functions.cpp` neben `isPhoneReady`, `extern` in
  `src/loop_functions_extern.h`. Nie in `meshcom_settings`, nie ins Flash.
- Kommando in `src/command_functions.cpp` nach dem `commandCheck()`-Muster (Zeile 286 ff.):
  `on` setzt, `off` loescht, Antwort auf dem Command-Back-Kanal wie bei den anderen
  Schaltern. Zeile in `--info` bzw. der `bDisplayInfo`-Zusammenfassung (Zeile 5820) optional.
- Reset auf `false` an genau den Stellen, die `isPhoneReady = 0` setzen:
  `src/esp32/esp32_main.cpp` Zeile 3002 und `src/nrf52/nrf52_ble.cpp` Zeilen 213, 231, 248.
- Alte McApp-Versionen senden das Kommando nicht; die offizielle App auch nicht. Beide sehen
  weiter genau ein Heard und ein ACK pro Nachricht.

## 4. Nachweis

### 4.1 Nativ (`pio test -e native`)

- `test/test_ack_validate` erweitern: Draht-Anhang mit `n = 0`, `n = 3` bei 15 Byte,
  `n = 3` bei 14 Byte (Anhang faellt, ACK bleibt), `n = 6`, `n = 10`, `n = 11`, Byte 11
  ungleich 0 ohne Anhang. Jeder Fall: `isPlausibleAckFrame()` true, `ackWireAppendixLen()`
  liefert 3 nur im einen gueltigen Fall.
- Neue Suite `test/test_ack_phone_frame`: leeres Rufzeichen liefert die heutigen 7 Byte
  byteidentisch; `DK5EN-98` liefert `n = 8` und Laenge 15; Kleinbuchstabe, Leerzeichen,
  11 Zeichen, NULL liefern `n = 0`; `H3A5F21` (Hash-Token fuer Stufe 4) wird akzeptiert.
  Eintrag in `platformio.ini` `[env:native]` `test_filter`.
- `test/test_ack_replay` unveraendert; der Korpus enthaelt keine 15-Byte-Frames, ein
  synthetischer Vektor gehoert in `test_ack_validate`.

### 4.2 Build-Gate

Clean-Build sequentiell (Memory "PlatformIO build cache race") fuer `wiscore_rak4631`,
Heltec V3 und T-Beam, plus `-Werror`-Lauf wie im Release-Gate. `resource_baseline.json`
nur gegen einen Same-Base-Build vergleichen.

### 4.3 Bench

- Heltec-93 und RAK-90 mit neuer Firmware, McApp auf `mcapp.local` an 93. Text von 93,
  90 wiederholt: das Heard-Frame traegt `DK5EN-90`, McApp zeigt es im Tooltip der Blase.
- DM 93 -> 90 und zurueck: Peer ACK traegt das Rufzeichen des Partners, Status 0x02.
- Ohne `--ackinfo on` (offizielle App oder McApp mit Kommando ausgeschaltet): pro Nachricht
  genau ein Heard-Frame, wie heute. Mit Flag und einem zweiten Relay (T-Beam-92): zwei
  Heard-Frames mit zwei Rufzeichen.
- BLE-Disconnect und Reconnect ohne Kommando: Sperre greift wieder.
- Gemischter Draht-Test (alter Node empfaengt 15 Byte) ist erst mit Stufe 4 moeglich, weil
  in dieser Runde niemand 15 Byte sendet. Bis dahin decken die nativen Tests den Parser ab.

## 5. Risiken und Entscheidungen

- **R1, Byte-11-Durchreiche.** Heute geht Draht-Byte 11 unveraendert als BLE-Byte 6 zur App.
  Die Feldmessung zeigt 0,4 Prozent gueltiger ACKs mit Byte 11 ungleich 0. McApp liest das
  seit 2026-09-05 als Laengenbyte und muss den "Anhang" ueber den Zeichensatz verwerfen. Der
  explizite Builder in 3.2 beendet das: BLE-Byte 6 ist immer das, was die Firmware meint.
- **R2, Kontext.** Alle Emit-Punkte liegen in `OnRxDone`-Reichweite. Der Builder ist
  heapfrei; die einzige `String` ist die schon existierende `aprsmsg`. Keine neue
  Allokation pro ACK (Memory "printf malloc starves NimBLE").
- **R3, Puffer.** BLE-Frame max. 7 + 10 + 4 = 21 Byte gegen `UDP_TX_BUF_SIZE - 4` und
  `MAX_MSG_LEN_PHONE` 300. Draht max. 15 Byte gegen `print_buff[30]`. Nichts knapp.
- **R4, Statuswert des UDP-Peer-ACK.** Erledigt: der Code sendet 0x02 fuer eigene Nachrichten
  (bedingt ueber `checkOwnTx()`), das Analysedokument war korrekt. Unveraendert uebernommen.
- **R5, Mehrfach-Heard ohne Obergrenze.** Mit Flag und vielen Relays kommen viele
  Heard-Frames in den BLE-Ring (`MAX_RING` 10 bis 20). Bei Bedarf Obergrenze pro msg_id
  (z. B. 8) ueber ein Zaehlfeld; in dieser Runde nur messen, nicht bauen.

## 6. Reihenfolge und Commits

1. `ack_attribution.h` plus beide native Suiten. Suiten rot vor, gruen nach dem Header.
2. `handleACK()`: Parser, Relay-Laenge, expliziter Phone-Frame, Debug-Log.
3. Emit-Punkte Heard, Peer ACK LoRa/UDP, eigener GW; `--ackinfo`; Disconnect-Resets.
4. Docs: Analysedokument (Statuswert R4, Stufenstatus in 4.3), `release-notes.md`,
   `CHANGELOG-stability.md`; Bench-Protokoll als Anhang hier.

Ein Wave, ein Autor, keine Subagenten noetig: alle Aenderungen liegen in sechs Dateien und
haengen sequentiell voneinander ab. Groessenordnung 150 Zeilen Firmware, 200 Zeilen Tests.
