# PING/PONG — Befundbericht und Fix-Plan

**Datum:** 2026-09-21
**Basis:** `upstream/dev @ 80b85a5a` (Merge PR #1149), gelesen in einem temporaeren Worktree
**Anlass:** Bugmeldung DM3KS-12 — "warum sendet meine Node keine eigenen HEY?"
**Beweismittel:** `~/Downloads/2026-09-21.log`, 19 599 Zeilen, 08:34–10:58 Lokalzeit, Node DM3KS-12 (T-Beam 1W, MeshCom 4.35t, Build 20.09.2026 17:33:15)

---

## 1. Urteil

Kein Defekt der Node, sondern ein Fehler im Firmware-Guard von `sendHey()`:

```c
// src/loop_functions.cpp:4951
if(meshcom_settings.node_call[0] != 0x00 && meshcom_settings.node_pingtime > 0)
    return;
```

`node_call` ist auf jeder betriebsbereiten Node gesetzt. Die Bedingung lautet damit faktisch
**`pingtime > 0` ⇒ niemals ein eigener HEY**. DM3KS-12 hat ein Ping-Ziel konfiguriert
(`PING CALL DB0SD-22 Time:40 Max:5 Count:0`), die Ping-Serie ist aber laengst beendet
(`Count:0`). Die Node sendet seitdem weder Pings noch HEYs — dauerhaft, ueber alle Reboots
hinweg, weil `pingtime` im Flash steht.

Eingefuehrt wurde der Guard mit Commit `be216750` ("v4.35p ping check").

### Beleg aus dem Log

| Beobachtung                                                       | Zaehlung |
| ----------------------------------------------------------------- | -------- |
| `[MC-TRICKLE] SEND consistent=…<k=2` — Scheduler ruft `sendHey()` | 11       |
| `NEW-HEY` — Ausgabe von `sendHey()` bei `bDisplayInfo on`         | 0        |
| eigene `@`-Frames `DM3KS-12>H…` auf der Luft                      | 0        |
| `TX-LoRa @` gesamt (ausnahmslos Relays fremder HEYs)              | 79       |
| `NEW-POS` / `NEW-TXT` — andere eigene Aussendungen funktionieren  | 4 / 13   |

Die Node arbeitet also normal, nur der HEY-Pfad bricht in Zeile 1 ab.

---

## 2. Die Vorgeschichte der Node

1. **`--pingcall DB0SD-22`** setzt `node_pingcall` und — weil `node_pingtime` noch 0 war —
   automatisch `node_pingtime = PING_INTERVAL (60)` (`command_functions.cpp:3708-3716`).
   Beides wird persistiert.
2. **`--pingtime 40`** — der Wert im Log ist kein Default. Erlaubt sind 15…300.
3. **`--ping start`** setzt `node_pingcount = node_pingmax`; `pingmax` war 0 und wird dabei auf
   `PING_MAX = 5` gehoben. Daher `Max:5` im `--info`.
4. Der Sender (`esp32_main.cpp:4066-4083`) schickt alle 40 s ein `{ping}` an DB0SD-22 und
   dekrementiert `pingcount` bei jedem Schuss: 5 → 0.

**`Count:0` ist heruntergezaehlt, nicht konfiguriert.** Es ist der Rest der Serie. Bei 0 endet der
Sender, `pingcall` und `pingtime` bleiben aber absichtlich stehen, damit man mit `--ping start`
neu starten kann. Es gibt keinen Aufraeumschritt. Auf ESP32 kommt hinzu, dass `node_pingcount`
gar nicht persistiert wird (`esp32_flash.cpp:604-609` speichert nur `pingtime`, `pingcall`,
`pingmax`) — ein Reboot setzt ihn ebenfalls auf 0. Auf nRF52 schreibt `save_settings()` die
gesamte Struktur, dort ueberlebt der Zaehler.

Ergebnis: ein einmaliger Linktest gegen DB0SD-22 hat die Node dauerhaft HEY-stumm gemacht.

### Sofort-Workaround fuer den Betreiber

`--pingcall none` — setzt `pingcall` und `pingtime` auf 0 und gibt den HEY frei.
`--pingtime 0` hilft **nicht**: der Wert wird auf 60 hochgeklemmt (`command_functions.cpp:3727`).

---

## 3. Befunde im gesamten Ping/Pong-Pfad

| #   | Befund                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          | Ort                                                                |
| --- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------ |
| P1  | HEY-Guard prueft nur `pingtime`, nicht "Serie laeuft" → dauerhafte HEY-Stille. `node_call[0]!=0` ist eine tote Bedingung.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       | `loop_functions.cpp:4951`                                          |
| P2  | Der Trickle-Scheduler loggt `SEND`, obwohl `sendHey()` sofort zurueckkehrt — die Ursache ist im Log unsichtbar.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 | `esp32_main.cpp:3487`, `nrf52_main.cpp:2020`                       |
| P3  | Der nRF52-Sender prueft **nur** `pingtime > 29`: kein `pingcall`-Guard in der aeusseren Bedingung, kein `pingcount`, kein Dekrement, kein `PongFail`. Eine RAK-Node mit gesetztem `pingtime` pingt endlos; `pingmax` und `--ping start/stop` sind dort wirkungslos.                                                                                                                                                                                                                                                                                                                                                                                                                             | `nrf52_main.cpp:2589-2600`                                         |
| P4  | `--ping stop` ruft kein `save_settings()`; `bReturn` unterdrueckt nur die "wrong command"-Meldung. Auf nRF52 lebt die Serie nach einem Reboot weiter.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           | `command_functions.cpp:3751-3756`, `:6184`                         |
| P5  | `pingcount` ist auf nRF52 persistent, auf ESP32 nicht → zwei verschiedene Verhalten derselben Funktion.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         | `nrf52_flash.cpp:388` vs. `esp32_flash.cpp:604`                    |
| P6  | `--pingtime` akzeptiert 15…300, der Sender feuert aber erst ab `> 29`. Werte 15…29 werden angenommen und tun nichts.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | `command_functions.cpp:3727` vs. `esp32_main.cpp:4066`             |
| P7  | `bPingSend` bleibt nach dem letzten Ping einer Serie `true` → die naechste Serie meldet als Erstes ein `FAILED` fuer einen nie gesendeten Ping.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 | `loop_functions.cpp:3371`, `esp32_main.cpp:4070`                   |
| P8  | PONG-Anzeige liest `substring(14,17)` aus dem 11 Zeichen langen Payload `{pong}{861}` → immer leer. Richtig waere Index 7…10.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   | `loop_functions.cpp:2234,2236`                                     |
| P9  | PING-Anzeige nimmt `cmsg[7..9]` aus der **dezimal** formatierten 32-Bit-`msg_id` → zufaellige Ziffern statt der 3-stelligen Kennung. Richtig waere `msg_id & 0x3FF` als `%03i`.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 | `loop_functions.cpp:3357-3363`                                     |
| P10 | `node_pingduration` wird mit dem Delta ueberschrieben (`= millis() - node_pingduration`) → ein zweites Pong ohne neues Ping rechnet Unsinn. Auf nRF52 wandert der Wert zusaetzlich in den Flash.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                | `loop_functions.cpp:2239`                                          |
| P11 | `snprintf(node_pingcall, sizeof(node_call), …)` — beide sind zufaellig `char[10]`, die Groessenangabe ist trotzdem falsch.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      | `command_functions.cpp:3708`                                       |
| P12 | Die Bedingung "Ping ist aktiv" existiert in fuenf Kopien und drei Varianten. Genau daraus ist P1 entstanden.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    | fuenf Fundstellen, siehe unten                                     |
| P13 | Ein `{pong}` an das eigene Rufzeichen geht nur ans Display (als Gateway auch zum Server), nie an den BLE-Client: ein ueber BLE gesendetes `{ping}` (App, McApp) bekommt nie eine sichtbare Antwort. **Behoben 2026-09-24/25** (fork-only, Changelog 234; auf `fork-main` fuer App-Pings zusammen mit P14/Changelog 235): `addBLEOutBuffer(RcvBuffer, size)` wie im DM-else-Zweig. Regression `tools/bench/pong_ble_check.py` (auf den neo-Zweigen) an DK5EN-1 vorher Exit 1, nachher Exit 0; McApp-Linkcheck DK5EN-98 -> DK5EN-1 ueber BLE bestanden.                                                                                                                                           | `lora_functions.cpp`, `{pong}`-Zweig                               |
| P14 | `sendMessage()` wiederholt jede DM ausser `{CET}`/`{MCP}`/`{SET}`; auf `{ping}` antwortet die Gegenstelle mit `{pong}`, nie mit ACK, also ging ein Ping aus App/McApp bis zu viermal in die Luft (DK5EN-98, 24.09.). **Behoben** (neo-Zweige 24.09., auf der Luft belegt: ein Empfang statt vier; `fork-main` 25.09., Changelog 235): kein Ring-Retry, keine Outbox-Leiter, nie wegen voller Outbox abgelehnt. Auf `fork-main` dazu die Ausnahme von der DM-Klammer-Ersetzung aus Stufe 0 (`7aeb2ac5`): ein fuehrendes `{ping}`/`{SET}` bleibt stehen (`src/dm_text_escape.h`, `test_dm_text_escape`), vorher verliess ein Ping den Zweig als `(ping}{NNN`.                                     | `loop_functions.cpp`, `sendMessage()`                              |
| P15 | Eigene Nachrichten ohne Wiederholung wurden mit Status DONE eingereiht und damit als Relay (`MSG_PRIO_NORMAL`) eingestuft: `sendPing()`, `SendPong()`, die `--dmretry`-Wiederholungen, bei `--dmretry` jede erste DM -- auf einer ausgelasteten Node wartet das Pong hinter ACKs, DMs und Gruppenverkehr. `SendAckMessage()` und die Store-Node (`msgstore_glue.cpp`, Postfach und Hinweis) umgingen das mit einem Status-Nachtrag ausserhalb des Locks (auf nRF52 ein Fenster gegen `doTX()`). **Behoben** (`fork-main` 25.09., Changelog 236): `addTxRingEntryOnce()` stuft als frische Nachricht ein und speichert DONE, beides im Lock; fuenf neue `test_txring`-Faelle. Neo-Zweige folgen. | `txring_functions.cpp`, `loop_functions.cpp`, `dm_outbox_glue.cpp` |

### Die fuenf Kopien der Aktiv-Bedingung (P12)

| Ort                       | Bedingung                                               |
| ------------------------- | ------------------------------------------------------- |
| `loop_functions.cpp:4951` | `node_call != "" && pingtime > 0`                       |
| `loop_functions.cpp:2194` | `pingcall == "" \|\| pingtime == 0 \|\| pingcount == 0` |
| `esp32_main.cpp:3524`     | `pingcall == "" \|\| pingtime == 0 \|\| pingcount == 0` |
| `esp32_main.cpp:4066`     | `pingtime > 29 && pingcall != "" && pingcount > 0`      |
| `nrf52_main.cpp:2589`     | `pingtime > 29`                                         |

---

## 4. Fix-Plan fuer den PR gegen DEV

1. Neuer, Arduino-freier Header `src/ping_state.h` mit der einen Wahrheit: `isPingActive()`,
   Clamp-Grenzen, ID-Formatierung. Host-getestet unter `test/test_ping_state/`.
2. Alle fuenf Fundstellen aus P12 ziehen diesen Header.
3. HEY-Guard (P1) auf "Serie laeuft" umgestellt; die tote `node_call`-Bedingung entfaellt.
4. `sendHey()` gibt `bool` zurueck, der Trickle-Log sagt die Wahrheit (P2).
5. nRF52-Sender auf ESP32-Paritaet: Guard, Dekrement, `PongFail` (P3).
6. `--ping stop` persistiert (P4); Zaehler-Semantik plattformgleich (P5, siehe offene Frage 2).
7. Clamp-Untergrenze auf die Sender-Schwelle gezogen (P6).
8. Anzeige-Indizes (P8, P9), `bPingSend`-Reset (P7), `pingduration` in eine eigene,
   nicht persistierte Variable (P10), `sizeof` korrigiert (P11).
9. Regressionstest, der P1 reproduziert: Guard muss bei `pingtime > 0 && pingcount == 0`
   den HEY **freigeben**.
10. Anschliessend `/fable-review` als Advisor ueber den fertigen Zweig, erst danach der PR.

---

## 5. Offene Fragen — vor dem ersten Commit zu entscheiden

### Frage 1: Soll ein eigener HEY unterdrueckt werden, solange eine Ping-Serie tatsaechlich laeuft?

| Option                                           | Konsequenz                                                                                                                                                                                                           |
| ------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **a) Nur waehrend laufender Serie** (Empfehlung) | `isPingActive() = pingcall gesetzt && pingtime > 0 && pingcount > 0`. Kurts urspruengliche Absicht — kein Beacon-Verkehr waehrend der Messung — bleibt erhalten, die Dauerstille endet mit der letzten Ping-Antwort. |
| b) HEY nie unterdruecken                         | Guard ersatzlos streichen. HEY und Ping sind orthogonal; ein HEY alle 30 s bis 15 min stoert eine Laufzeitmessung kaum. Einfachste Variante, weicht aber von der urspruenglichen Absicht ab.                         |

### Frage 2: Soll eine laufende Ping-Serie einen Reboot ueberleben?

| Option                                        | Konsequenz                                                                                                                                                                                                              |
| --------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **a) Nein, Serie ist transient** (Empfehlung) | `pingcount` auf beiden Plattformen beim Boot 0; nRF52 wird an ESP32 angeglichen. Eine Node kann nach einem Watchdog-Reset nicht ungewollt weiterpingen. `--ping start` ist dann bewusst eine Aktion, keine Einstellung. |
| b) Ja, Serie persistieren                     | ESP32 speichert `pingcount` zusaetzlich, `--ping stop` speichert ebenfalls. Das nRF52-Verhalten wird zum Standard. Eine begonnene Serie laeuft nach einem Reset zu Ende.                                                |
