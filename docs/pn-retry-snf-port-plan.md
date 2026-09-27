# PN-Wiederholung (XOR, Bit 10-11) nach feature-snf portieren -- Analyse und Plan

Status: **Entscheidungen getroffen 2026-09-27, Plan wartet auf Freigabe.** Quelle: `dk5en-xor` @ `a605e9f5`
(icssw-org). Ziel: `feature-snf` @ `983b0d7b`.

## Wellenstatus

| Welle | Inhalt                                                | Status |
| ----- | ----------------------------------------------------- | ------ |
| W0    | Header, Host-Tests, native-Env, neo-Pfadlisten        | offen  |
| W1    | lora_functions-Port, Outbox auf XOR, Server-ACK, Doku | offen  |
| W2    | Gate, Advisor, Commit, Kampagnen-Doku, Push           | offen  |

## 1. Ausgangslage

- `feature-snf` hängt 32 Upstream-Commits hinter `upstream/dev` und enthält `cf215b5d` (Basis von
  `dk5en-xor`) nicht. Seit `cf215b5d` weichen `lora_functions.cpp` (+1094/-219) und
  `loop_functions.cpp` (+712/-315) stark ab. **Kein Cherry-Pick, sondern Portierung von Hand.**
- Kurts "keine MSB"-Kommentare (5efa2171, 7f6964d3, d2979ccc) sind nicht in `feature-snf`. Die
  `[DK5EN]`-Kommentare aus `89e03f9c` entfallen daher; sie kommen erst mit dem nächsten
  Upstream-Sync.
- Alle Hilfsfunktionen, die der Port braucht, gibt es in `feature-snf` mit gleicher Signatur
  (`checkOwnRx`, `checkOwnTx`, `addLoraRxBuffer`, `extractRingMsgId`, `setlogCountDedup`,
  `queueExtern`, `queueKiss`, `SendAckMessage`, `RING_STATUS_*`, `MAX_RETRANSMIT` 3, 40 s Takt).

## 2. Was feature-snf schon selbst mitbringt

| Fork-Funktion                                                     | Wechselwirkung mit dem Port                                                                                                                                                                                                                                   | Urteil                            |
| ----------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------- |
| `--dmretry off\|3\|9` (Outbox, Stufe 1)                           | Bei `off` (Standard) laufen PNs über die Ring-Wiederholung wie upstream -- **dort greift der Port**. Bei `3\|9` geht die PN mit `addTxRingEntryOnce` (Status DONE) raus, die Outbox wiederholt selbst; Ring-Wiederholung und Echo-Freigabe fassen sie nie an. | verträglich, zwei getrennte Wege  |
| `dm_dedup` (Stufe 2.1, Empfänger)                                 | Eine XOR-Kopie an uns landet dort als DUP: Re-ACK, keine zweite Anzeige. Der Anzeige/ACK-Teil des Ports ist für PNs an uns überflüssig.                                                                                                                       | verträglich, Port wird kleiner    |
| `reack_limiter` (30 s)                                            | Kürzer als der 40-s-Takt, jede Kopie bekommt ihr Re-ACK.                                                                                                                                                                                                      | verträglich                       |
| Store-Knoten (Stufe 3)                                            | `msgstoreStore` setzt bei gleicher Meldung `stored_ms` neu und den Zustand auf HELD. Heute kommen gleich-id-Wiederholungen dort nie an; XOR-Kopien schon -- jede schöbe die Zustellung hinaus.                                                                | **Entscheidung E2**               |
| `:sto`-Hinweis (Stufe 4), Held-Markierung 0x04                    | Die Aufgabe-Stelle (`lora_functions.cpp:2910-2950`) liest die msg_id der letzten Kopie (XOR-id) und findet so weder `own_msg_id` noch die 0x04-Markierung; das 0x03-Telefonframe trüge eine der App unbekannte id.                                            | **Konflikt, wird im Port gelöst** |
| Server-ACK (`udp_frame_esp32.cpp:343`, `udp_frame_nrf52.cpp:306`) | Stoppt heute nur Outbox und `checkOwnTx`, nie den Ring-Slot. Mit dem Port hält die Echo-Freigabe den Slot nicht mehr frei, ein ACK nur über den Server muss ihn stoppen.                                                                                      | **Konflikt, wird im Port gelöst** |
| NBR Overhear-Cancel, CSMA, Relais                                 | Arbeiten nur auf RELAY-Slots mit DONE; XOR-Kopien sind eigene Frames.                                                                                                                                                                                         | verträglich                       |
| Ping/Pong, BP-11, BP-ids                                          | Ping/Pong gehen mit `addTxRingEntryOnce` raus, nie wiederholt.                                                                                                                                                                                                | verträglich                       |
| Backpressure-Tiefe (`txRingDepth`)                                | Wartende PN-Slots zählen mit. Heute gibt das Echo sie nach Sekunden frei, mit dem Port bleiben sie bis ACK oder Aufgabe (bis ~160 s) stehen. Ab 5 offenen PNs kommt QRS.                                                                                      | **Entscheidung E3**               |
| Dedup-Ring 10 Einträge bei `ENABLE_TBEAM`                         | Die Variantenprüfung reicht dort nicht über 120 s; schlimmstenfalls lädt ein Gateway eine Kopie nochmals hoch. PNs an uns deckt `dm_dedup` ab.                                                                                                                | hingenommen                       |
| EXTERNAL_RADIO (`RING_STATUS_EXT_PENDING`)                        | Die Echo-Freigabe ruft vorher `extTxqAckInvalidateIfOwned`; der Port setzt danach SENT wie in `dk5en-xor`.                                                                                                                                                    | verträglich, bleibt so            |
| nRF52: Server-ACK läuft im Loop-Task                              | `findAndStopRingSlot` schreibt den Ring ohne Sperre, OnRxDone läuft im LORA-Task.                                                                                                                                                                             | **wird im Port gesperrt**         |

## 3. Was portiert wird

Aus `dk5en-xor` (Stand `a605e9f5`), angepasst an `feature-snf`:

1. `src/pn_retry.h` unverändert (Bit 10-11, Maske `0xFFFFF3FF`, `pnFrameIsOwnPn` mit
   Quell-Rufzeichen-Prüfung).
2. `test/test_pn_retry/` unverändert, `[env:native_pnretry]` in `platformio.ini`, 32 Tests.
3. `src/lora_functions.cpp`:
   - `findAndStopRingSlot` vergleicht `pnRetryCore`, wird exportiert (`lora_functions.h`) und
     nimmt auf RAK die Ring-Sperre (`taskENTER_CRITICAL`), weil ihn jetzt auch der Loop-Task ruft.
   - Echo-Freigabe: eigene PN (`pnFrameIsOwnPn`) startet die Wartezeit neu statt freizugeben.
   - Wiedererkennung `rx_pn_repeat` über die drei Bitvarianten (`checkOwnRx`); Extern-UDP, KISS
     und Server-Upload (`addNodeData`) werden für Wiederholungskopien übersprungen.
   - HEARD-Buchung einer eigenen Wiederholungskopie unter der Original-id.
   - `updateRetransmissionStatus`: eigene PN bekommt k = retryCount+1 per XOR, FCS neu, Kopie in
     den eigenen Dedup-Ring; `static_assert(MAX_RETRANSMIT <= 3)`.
   - **Neu für den Fork**: Aufgabe-Stelle faltet die id der letzten Kopie auf die Original-id
     zurück, bevor sie `checkOwnTx`, die 0x04-Held-Prüfung und das 0x03-Telefonframe benutzt.
   - **Nicht portiert**: die Anzeige/BLE-Sperre nach `SendAckMessage` -- für PNs an uns erledigt
     `dm_dedup` das schon.
4. Server-ACK (`udp_frame_esp32.cpp`, `udp_frame_nrf52.cpp`): nach erkanntem eigenem `:ackNNN`
   `findAndStopRingSlot(msg_counter)`.
5. Doku: `docs/pn-zustellung-dedup.md`, `docs/pn-retry-{xor-impl-plan,server,app,mcapp}.md` aus
   `dk5en-xor`, im Umsetzungsplan ein Abschnitt "Fork feature-snf" mit den Punkten aus Kapitel 2.
6. `tools/neo/paths`: `src/pn_retry.h` nach `CORE.txt`, `test/test_pn_retry/test_main.cpp` nach
   `K19.txt` -- sonst verliert `derive.sh` die Dateien wortlos.

## 4. Entscheidungen (getroffen 2026-09-27)

- **E1 Outbox (`--dmretry`)**: Das XOR-Format ist das offizielle Format, auch für die Outbox.
  - Versuch 1 = Original-id, Versuch n (2-4) = `first_id ^ ((n-1) << 10)`. Nie mehr dieselbe id,
    nie mehr `millis()`. Höchstens 4 Aussendungen wie der Ring-Weg.
  - Modus 9 entfällt. `--dmretry` kennt nur noch `off|3`. Ein gespeicherter Wert 9 (NVS
    `dm_retry`, nRF52 `/dm.cfg`) wird als 3 gelesen. Web-GUI und Hilfetext ohne 9.
  - Zeitplan: Versuch 2 nach 40 s, 3 nach 80 s, 4 nach 120 s -- der 40-s-Schritt der Outbox,
    fortgesetzt. Die 9er-Tabelle und die Blocklücke (`DM_OUTBOX_BLOCK_GAP_MS`) gehören zum toten
    Modus 9 und entfallen mit ihm. Damit laufen Outbox und Ring-Weg auf der Luft im selben Takt.
  - Das Echo-Tor ("frische ids nur nach gehörtem Echo") entfällt. Das Echo stoppt die Leiter
    nicht, nur `:ackNNN`.
  - `dmOutboxOnEcho` vergleicht über `pnRetryCore`. `glueTransmit` trägt Wiederholungs-ids nur
    noch in den Dedup-Ring ein (`addLoraRxBuffer`), nicht mehr in `own_msg_id`. HEARD, Held-0x04
    und Aufgabe laufen über die Original-id, der Port bucht das Echo einer Kopie dorthin um.
  - Zähler `same_id_retries` bleibt im Format (dann immer 0), damit die OUTBOX-Zeile gleich bleibt.
- **E2 Store-Knoten**: Wiederholungskopien (`rx_pn_repeat`) gehen nicht in `msgstoreStore` --
  das hält das heutige Verhalten, der Store-Zeitplan verschiebt sich nicht.
- **E3 Backpressure-Tiefe**: wartende PN-Slots zählen weiter mit. Hingenommen und dokumentiert;
  fünf gleichzeitig offene PNs sind selten, QRS ist dann die richtige Antwort.
- **F1 Push**: nach dem Gate nach `origin/feature-snf`.

## 5. Wellen

**W0 (Orchestrator, mechanisch)**: `src/pn_retry.h` und `test/test_pn_retry/` wörtlich aus
`a605e9f5`, `[env:native_pnretry]` in `platformio.ini`, neo-Pfadlisten (`CORE.txt`, `K19.txt`).
Gate: `pio test -e native_pnretry` 32/32. Commit.

**W1 (drei Schreiber parallel, Dateien disjunkt)**:

| Schreiber              | Dateien (exklusiv)                                                                                                                                                                                                                       | Prüfung im Auftrag                                                    |
| ---------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------- |
| A `implementer`        | `src/lora_functions.cpp`, `src/lora_functions.h`                                                                                                                                                                                         | keine eigenen Builds (Gate)                                           |
| C `implementer`        | `src/dm_outbox.cpp`, `src/dm_outbox_api.h`, `src/dm_outbox_glue.cpp`, `src/dm_settings.cpp`, `src/dm_settings.h`, `src/command_functions.cpp` (nur dmretry), `src/web_functions/web_functions.cpp` (nur dmretry), `test/test_dm_outbox/` | einziger pio-Nutzer der Welle: `pio test -e native -f test_dm_outbox` |
| B `implementer` (Doku) | die fünf `docs/pn-*.md`                                                                                                                                                                                                                  | keine Builds; prettier nur auf diese Dateien                          |
| Orchestrator (Hotspot) | `src/esp32/udp_frame_esp32.cpp`, `src/nrf52/udp_frame_nrf52.cpp`                                                                                                                                                                         | nach Rückkehr von A                                                   |

Schnittstelle A/C: A ruft `dmOutboxOnEcho` mit der umgebuchten Original-id (`heardMsgId`); C lässt
`dmOutboxOnEcho` zusätzlich über `pnRetryCore` vergleichen. Keiner fasst die Datei des anderen an.

**W2 (Gate, Orchestrator)**:

1. `ls` aller erwarteten Dateien, Diff selbst lesen.
2. Host-Tests: `native_pnretry`, `native` (Outbox, dm_dedup, msgstore, sto, backpressure),
   `native_aprs`, `native_dedup` -- keine Rückschritte.
3. Voller Build-Sweep nacheinander über alle Firmware-Envs; bekannt rot am unveränderten HEAD:
   `t5_epaper`, `esp32-external-radio`.
4. Nachweis im Image: `[RETX] PNRETRY` als String in `firmware.elf` von Heltec V3 und RAK4631.
5. Advisor-Pass (fable) auf den Diff gegen Kapitel 2-4.
6. Commits je Welle, `docs/resume.md` und `docs/BACKLOG.md` nachziehen.
7. Push nach `origin/feature-snf`.

Nicht Teil dieses Plans: Bench-Test auf Hardware (A -> R1 -> R2 -> B mit verlorenem letzten Hop,
Outbox-Leiter mit `--dmretry 3`), Übertrag nach `fork-main` / `fork-neo-test`, Server-Änderung.
