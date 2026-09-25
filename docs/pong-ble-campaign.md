# Ping/Pong ueber BLE -- Kampagnenstand

**Start:** 2026-09-24, Zweig `feature-neighbour-matrix` (DK5EN-98 und DK5EN-1 laufen diesen Build).
**Befundbasis:** `docs/ping-pong-befunde-20260921.md` auf `fork-main` (P1-P12); hier kommen P13 und
P14 dazu. Gegenstueck in McApp: `MCProxy/doc/2026-08-13_1500-linkcheck-ping-pong-ADR.md` §8.

## Befunde

| #   | Befund                                                                                                                                                                                                                     | Ort                                   |
| --- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------- |
| P13 | Ein `{pong}` an das eigene Rufzeichen geht nur ins Display (und bei `--gateway on` zum Server), nie an den BLE-Client. Ein ueber BLE gesendetes `{ping}` (App, McApp) bekommt deshalb nie eine sichtbare Antwort.          | `lora_functions.cpp`, `{pong}`-Zweig  |
| P14 | `sendMessage()` wiederholt jede DM ausser `{CET}`/`{MCP}`/`{SET}`. Die Gegenstelle antwortet auf `{ping}` mit `{pong}`, nie mit ACK, also stoppt nichts die Wiederholung: ein Ping aus App/McApp geht dreimal in die Luft. | `loop_functions.cpp`, `sendMessage()` |

Beleg (McApp-DBG-Konsole an DK5EN-98, 2026-09-24, Extern-UDP aus): Ping `x1AE1E1EA` 22:42:40 auf
der Luft, Pong `{pong}{451011050}` von DK5EN-1 22:42:46 direkt (-71/6), danach nur `TX-UDP` zum
Server, kein BLE; `RETRANSMIT retry=1` 22:43:24 und `retry=2` 22:44:04 fuer die schon beantwortete
Ping-ID.

P14 setzt den Status erst NACH `addTxRingEntry()` auf DONE, wie `SendAckMessage()`: die erste
Fassung (DONE vorab, wie `{CET}`) haette ein Ping als Relay (`MSG_PRIO_NORMAL`) statt als
persoenliche DM (`MSG_PRIO_CRITICAL`) eingestuft, weil `getMessagePriority()` den Status beim
Einreihen liest -- auf einer ausgelasteten Node haette der Linkcheck hinter ACKs, DMs und
Gruppenverkehr gewartet (vom Advisor als Randnotiz gemeldet, hier als Regression gewertet).
Pongs erscheinen durch P13 auch in der Nachrichtenliste der Web-Oberflaeche (gleicher `phoneRing`).

Nebenbefund fuer das ADR (nicht Teil des Fixes): ein Gateway laedt ein an sich selbst adressiertes
Pong zum Server hoch (`lora_functions.cpp`, `addNodeData()` unter `bGATEWAY && !msg_server`). ADR
§1.4.2 ("nichts erreicht den Server") stimmt damit nicht.

## Entscheidungen (freigegeben 2026-09-24)

- P13 und P14 beide, je ein Commit.
- Zweige: `feature-neighbour-matrix`, `fork-main`, `fork-neo-test`. Kein Push ohne Freigabe.
- OTA DK5EN-98 (und DK5EN-1) in Welle 2. Der 48-h-Pi-Mitschnitt von DK5EN-98 endete 09:28, es
  wird kein laufender Mitschnitt unterbrochen.
- Kein Upstream-PR vorerst. Seiteneffekt, falls spaeter: die offizielle App zeigt `{pong}{<id>}`
  als Chat-Blase (`Meshcom-MobileApp/src/hooks/MessageHandler.ts:327` verwirft nur `{CET}`).

## Wellen

| Welle | Inhalt                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    | Status                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1     | P13 + P14 auf `feature-neighbour-matrix` (Orchestrator); `tools/bench/pong_ble_check.py` + Unit-Tests (Implementer); Gate: Host-Tests, vier Builds, BLE-Regression an DK5EN-1, Advisor                                                                                                                                                                                                                                                                                                    | erledigt 2026-09-24: `14a669cf` P13, `df0ea8c1` P14, `a5ba97df` Pruefskript. Host-Tests 1112/1112, vier Builds gruen (Warnungen nur in Fremdbibliotheken); Advisor APPROVED (P14 nachgebessert: DONE erst nach dem Einreihen, Prio bleibt CRITICAL; Skript: Hop 0, Server-Bit fest, Exit-Codes, Testluecken). BLE-Regression an DK5EN-1 mit Messbuilds: vorher (`082c2412`) Exit 1, nachher Exit 0, Kontrolle beide Male da, `RLY ... q=self slot=-1`, nichts gesendet                                                                                                                                                                                                            |
| 2     | OTA DK5EN-98 und DK5EN-1; Abnahme: McApp-Linkcheck DK5EN-98 -> DK5EN-1 bei Extern-UDP aus, Konsole 98 mit `--loradebug on`: fuer die Ping-ID `RING_TX_READ ... status=FF` bzw. `einfügen ... status:FF` und kein `RETRANSMIT` (NICHT an `RING_WRITE` messen: das druckt den Einreihe-Parameter, fuer ein Ping jetzt absichtlich `00`)                                                                                                                                                     | erledigt. OTA 2026-09-24 23:24/23:26: DK5EN-1 und DK5EN-98 auf Produktionsbuild `Sep 24 2026 / 23:15:22` (md5 `63be7c09...`). Abnahme 2026-09-25 07:56: McApp-Linkcheck DK5EN-98 -> DK5EN-1 (Extern-UDP aus) `completed -- 1 sent, 1 received`, Pong `{pong}{451011116}` (= `0x1AE1E22C`) ueber BLE nach 12 s, direkt. P14 auf der Luft: DK5EN-1 empfing Ping `x1AE1E22C` genau einmal (Mitschnitt lief bis 07:59:41, Wiederholfenster vorbei); vorher, 24.09. 22:42, kam `x1AE1E1EA` viermal (Original + 3 Wiederholungen, 40 s Takt)                                                                                                                                            |
| 3     | Portierung nach `fork-main` (String-API) und `fork-neo-test`; P13/P14 in `ping-pong-befunde-20260921.md`, Changelog auf `fork-main`. Auf `fork-main` zusaetzlich: bei `--dmretry` != off wird eine DM ins Outbox eingetragen (`dmOutboxHasRoom()`-Abweisung, `dmOutboxAdd()`), deren Leiter auf ein ACK wartet, das ein Ping nie bekommt -- `{ping}` dort ebenfalls ausnehmen                                                                                                             | erledigt 2026-09-25: `fork-neo-test` per Cherry-Pick P13/P14/Pruefskript (`3b018f03`, `1a14bef5`, `5e526559`), Gate 1022/1022 Host-Tests, vier Builds, Skript-Tests gruen. `fork-main` nur P13 (`0e271c17`) plus Changelog 234 und P13/P14-Zeilen im Befundbericht (`bad1ec0e`), Gate 879/879, vier Builds gruen. P14 auf `fork-main` ZURUECKGEHALTEN (Advisor): die DM-Klammer-Ersetzung aus Stufe 0 (`7aeb2ac5`) macht aus `{ping}` ein `(ping}`, das ist dort eine normale, per ACK gestoppte DM -- P14 allein naehme ihr nur die Wiederholung. Portierter P14-Stand (inkl. Outbox-Ausnahme) als Patch im Sitzungs-Scratchpad; offene Entscheidung siehe unten. Nichts gepusht |
| 4a    | `fork-main` (Worktree an `bad1ec0e`): gemeinsamer Helfer `addTxRingEntryOnce()` (als frische Nachricht einstufen, als DONE speichern, beides im Lock des Rings) + test_txring; ueber ihn laufen `SendAckMessage()`, `sendPing()`, `SendPong()`, die Outbox-Wiederholungen (`dm_retry`) und in `sendMessage()` Ping und `--dmretry`-DMs; Ausnahme fuer fuehrendes `{ping}`/`{SET}` von der DM-Klammer-Ersetzung (Arduino-freier Header + Host-Test); Ping-Ausnahmen der Outbox (P14-Patch) | laeuft (Implementer, einziger pio-Nutzer)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| 4b    | McApp-ADR (`MCProxy/doc/2026-08-13_1500-linkcheck-ping-pong-ADR.md`) auf den Stand bringen, Commit auf `development`, kein Push                                                                                                                                                                                                                                                                                                                                                           | laeuft (Implementer, kein pio)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| 5     | Neo-Port auf `feature-neighbour-matrix`: Helfer + test_txring, `SendAckMessage()`, `sendPing()`, `SendPong()`, `sendMessage()`-Ping ueber den Helfer statt nachtraeglichem DONE-Schreiben; danach Cherry-Pick auf `fork-neo-test`                                                                                                                                                                                                                                                         | offen                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |

## Entscheidungen 2026-09-25 (Betreiber)

- Prioritaet: `--dmretry` UND Ping/Pong (auf allen drei Zweigen), ueber einen gemeinsamen Helfer, host-getestet.
- Klammer-Ersetzung auf `fork-main`: fuehrendes `{ping}` UND `{SET}` ausnehmen (Upstream-Verhalten fuer beide).
- Nachweis: Host-Tests + Advisor, kein Flashen.
- ADR-Commit auf MCProxy `development`.

Der Helfer schreibt DONE im selben kritischen Abschnitt wie die Einstufung: das Muster "einreihen, danach DONE" (`SendAckMessage()`) hat auf nRF52 ein Fenster, weil `OnRxDone` (und damit `SendPong()`) im LORA-Task laeuft und `doTX()` im Loop-Task den gelesenen Status zuruecksetzen kann.

## Offene Entscheidungen (Betreiber, Stand vor 2026-09-25)

- **`fork-main`: `{ping}` von der DM-Klammer-Ersetzung ausnehmen?** Stufe 0 (`7aeb2ac5`,
  `loop_functions.cpp` ~4166, "advisor m4") ersetzt in jeder DM jedes `{` durch `(`, damit das
  `indexOf("{", 1)` des Empfaengers das ACK-Tag findet. Ein fuehrendes `{ping}` stoert diese Suche
  nie (sie beginnt bei Index 1), und der Empfaenger nimmt den Ping-Zweig ohnehin vorher. Empfehlung:
  `if(bDM && !bPingMsg) strMsg.replace('{', '(');` und P14 im selben Commit; die Annahme der Stufe 3
  ("DM-Text beginnt nie mit `{`") bleibt fuer alles ausser `{ping}` wahr, und den Store-Hook schliesst
  `{`-Praefixe ohnehin aus. Ohne diese Ausnahme funktioniert der McApp-Linkcheck ueber eine
  `fork-main`-Node nicht. Kein Release enthaelt `7aeb2ac5` bisher. Dieselbe Ersetzung trifft auch
  `{SET}`-DMs (Fernkonfiguration) -- ob das gewollt ist, gehoert zur selben Entscheidung.
- **`fork-main`, vorbestehend: `--dmretry` != off stuft jede DM als Relay ein.** Der Status-Zweig
  `bDM && dmRetryMode() != DM_RETRY_OFF` setzt 0xFF VOR `addTxRingEntry()`, `getMessagePriority()`
  liest das als Relay (`MSG_PRIO_NORMAL`) statt persoenliche DM (`MSG_PRIO_CRITICAL`). Gleiche Falle
  wie die erste P14-Fassung; Abhilfe waere dasselbe Muster (0x00 einreihen, danach DONE). Nicht
  Teil dieser Kampagne.

## Bench-Fakten

- Die einzige angeschlossene Bench-Node ist der Heltec an `/dev/cu.usbserial-0001`, seit Feldlauf 2
  als **DK5EN-1** (Blatt, `--txpower 2`, `--gateway on`). Der Port gehoert `serial_capture.py`
  (Mitschnitt `~/meshlog/dk5en-1/2026-09-22.log`) -- nicht oeffnen, das Log ist das serielle
  Instrument; geflasht wird per OTA.
- Die BLE-Probe injiziert mit gesetztem Server-Bit: so ueberspringt ein Gateway den Server-Upload,
  der `{pong}`-Zweig (DM an mich) laeuft trotzdem. Nichts davon geht in die Luft oder ins Netz.
- `--injectraw` gibt es nur im Messbuild (`#if INSTRUMENT_ENABLED`, `command_functions.cpp`) und
  nur auf ESP32 (`test_inject_raw()` lehnt RAK4631 ab). Ein Produktionsimage antwortet
  `...wrong command --injectraw`, das Skript meldet dann VOID (so geschehen 23:00 an DK5EN-1).
  Vorher-/Nachher-Images: `PLATFORMIO_BUILD_FLAGS="-DINSTRUMENT_ENABLED=1"`, Vorher aus einem an
  `082c2412` gepinnten Worktree.
- Die Probe hat Hop-Nibble 0: ein vertipptes `--own-call` macht sie zur fremden DM, und mit Hop > 0
  wuerde die Node sie weiterleiten (Relay-Guard prueft nur das Hop-Nibble).
- Die Probe laeuft durch `nbrNoteFrame()` und kann in der Nachbarschaftsmatrix und im MHeard von
  DK5EN-1 eine Zeile `DK5EN-97` anlegen (Absender der injizierten Frames) -- das ist unsere
  Injektion, kein Funkkontakt.
