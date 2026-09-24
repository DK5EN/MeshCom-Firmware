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

| Welle | Inhalt                                                                                                                                                                                                                                                                                                                                                                        | Status                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1     | P13 + P14 auf `feature-neighbour-matrix` (Orchestrator); `tools/bench/pong_ble_check.py` + Unit-Tests (Implementer); Gate: Host-Tests, vier Builds, BLE-Regression an DK5EN-1, Advisor                                                                                                                                                                                        | erledigt 2026-09-24: `14a669cf` P13, `df0ea8c1` P14, `a5ba97df` Pruefskript. Host-Tests 1112/1112, vier Builds gruen (Warnungen nur in Fremdbibliotheken); Advisor APPROVED (P14 nachgebessert: DONE erst nach dem Einreihen, Prio bleibt CRITICAL; Skript: Hop 0, Server-Bit fest, Exit-Codes, Testluecken). BLE-Regression an DK5EN-1 mit Messbuilds: vorher (`082c2412`) Exit 1, nachher Exit 0, Kontrolle beide Male da, `RLY ... q=self slot=-1`, nichts gesendet |
| 2     | OTA DK5EN-98 und DK5EN-1; Abnahme: McApp-Linkcheck DK5EN-98 -> DK5EN-1 bei Extern-UDP aus, Konsole 98 mit `--loradebug on`: fuer die Ping-ID `RING_TX_READ ... status=FF` bzw. `einfügen ... status:FF` und kein `RETRANSMIT` (NICHT an `RING_WRITE` messen: das druckt den Einreihe-Parameter, fuer ein Ping jetzt absichtlich `00`)                                         | OTA erledigt 23:24/23:26: DK5EN-1 und DK5EN-98 auf Produktionsbuild `Sep 24 2026 / 23:15:22` (md5 `63be7c09...`). Abnahme offen -- fuehrt die McApp-Sitzung aus (sie besitzt McApp und die DBG-Konsole)                                                                                                                                                                                                                                                                |
| 3     | Portierung nach `fork-main` (String-API) und `fork-neo-test`; P13/P14 in `ping-pong-befunde-20260921.md`, Changelog auf `fork-main`. Auf `fork-main` zusaetzlich: bei `--dmretry` != off wird eine DM ins Outbox eingetragen (`dmOutboxHasRoom()`-Abweisung, `dmOutboxAdd()`), deren Leiter auf ein ACK wartet, das ein Ping nie bekommt -- `{ping}` dort ebenfalls ausnehmen | offen                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |

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
