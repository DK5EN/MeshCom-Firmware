# Absturz am Extern-UDP-Eingang: Stackueberlauf des Loop-Tasks

Stand 20.09.2026. Betroffen sind alle ESP32-Boards, unabhaengig vom Branch.
Der Fehler ist eine Altlast, keine Regression des RAM-Umbaus.

## Befund

Ein Knoten mit `--extudp on` startet **deterministisch neu**, sobald ueber
Extern-UDP ein `{"type":"msg"}`-Datagramm mit einem Ziel ankommt, das nicht das
eigene Rufzeichen ist. Vier von vier solchen Datagrammen am 20.09. haben
DK5EN-98 umgelegt, der Reset kam jeweils 0,1 bis 1,1 s nach dem Datagramm.

Die Nachricht landet vorher noch im TX-Ring und geht nie auf die Luft.

## Ursache

Die Kette vom Extern-UDP-Eingang ist die tiefste im Firmwarebild:

```
esp32loop            816 B
getExternUDP          32
getExtern           1312      val[381] + aprsMessage + JsonDocument
sendMessage         1456      msg_text_check/_checked je 200, msg_buffer 300
sendExtern          2160      c_json[500] + c_tjson[500] + zwei Structs
decodeAPRS           912
printfdeb            992
MeshSerial/lwIP     ~960      Netzkonsole auf TCP 2323
                   ------
                    8672 B
```

Der Arduino-Loop-Task hat den Framework-Vorgabewert **8192 B**; im ganzen Baum
stand kein `ARDUINO_LOOP_STACK_SIZE`. Gemessen am kompilierten Artefakt
(`-fstack-usage` sowie die `entry a1,0xNNN`-Prologe der Xtensa-Windowed-ABI),
nicht geschaetzt. Der Suchlauf ueber den ganzen Aufrufgraphen findet vom
selben Wurzelpunkt sogar 10 080 B, ueber `commandAction -> sendPosition`, und
auch dieser Zweig ist vom Extern-Socket aus erreichbar (`getExtern()` reicht
Text mit fuehrendem `-` an `commandAction()` durch).

nRF52 ueberlebt dieselbe Kette, weil `c_json`/`c_tjson` und
`msg_text_check/_checked` dort schon in BSS liegen.

## Warum es vorher nie auftrat

Der Reiz braucht ein Datagramm mit **fremdem** Ziel. Die Probe mit eigenem
Rufzeichen steigt bei `loop_functions.cpp:4057`
(`[ERROR]...DM to own-all not allowed`) aus, also **vor** dem tiefen Schwanz.
Erst der McApp-Bot (Antwort auf `!dice`/`!mheard` ueber denselben Transport,
auf dem das Kommando hereinkam) hat diesen Pfad erstmals im Betrieb erzeugt.

## Beweise

- **Feld:** vier Ruecksprunge von `millis()` im Mitschnitt
  (`~/meshlog/dk5en-98/2026-09-20.log`): 18:40:30, 18:48:55, 18:50:28,
  19:32:35. Das vierte Datagramm war reiner ASCII-Text -- Emoji und
  Surrogatpaare sind damit ausgeschlossen.
- **Gegenprobe:** `fork-neo-test` (Build 21:53) auf denselben Knoten geflasht,
  ein Datagramm an Gruppe 9 geschickt: Mitschnitt bricht auf `[EXT] Inc:` ab,
  Boot zurueckgerechnet auf 120 ms nach dem Datagramm. **Die Basis stuerzt
  genauso ab.**
- **Statisch:** die Frame-Groessen der ganzen Kette sind auf `fork-neo-test`
  und `neo-ram-reclaim` Byte fuer Byte gleich.

Keine `Guru Meditation` im Mitschnitt: die Panik geht auf die USB-Serielle,
nicht auf die TCP-Konsole, und die stirbt mit dem Absturz.

## Was nicht die Ursache ist

- **Die Byte-Ringe** (`src/byte_fifo.h`, Commit 2616ef59). Alle Schreibstellen
  sind auf 255 B geklemmt, alle Lesepuffer sind >= 256 B, und die Gegenprobe
  auf der Basis ohne Byte-Ringe stuerzt identisch ab.
- **`stack_hwm` 368 gegen 1 576.** Das ist das Minimum eines langen Boots
  gegen einen frischen -- keine Aussage ueber diese Kette.
- **JSON, Laengenpruefungen, Socket, Backpressure, McApp.**

## Fix

- **F1** `-D ARDUINO_LOOP_STACK_SIZE=12288` fuer jeden ESP32-Board-Env
  (`[esp32]` plus sechs eigenstaendige Envs in `variants/`).
  **Nicht** `CONFIG_ARDUINO_LOOP_STACK_SIZE`: das definiert die `sdkconfig.h`
  des Frameworks selbst auf 8192 und wird nach der Kommandozeile eingebunden,
  gewinnt also. Der Unterschied ist stumm -- `-Werror` steht nur auf
  `build_src_flags`, die Redefinitionswarnung faellt niemandem auf. Am
  Artefakt nachgewiesen: `getArduinoLoopTaskStackSize()` laedt 0x3000.
- **F2** `c_json`/`c_tjson` in `sendExtern()` auch auf ESP32 nach BSS, wie auf
  nRF52 seit 1951aa7d. Der Frame faellt von 0x870 auf 0x480, also -1 008 B.
  Reentranzpruefung siehe Kommentar an der Stelle.
- **F3** `tools/stack_budget.py` als Gate, siehe `docs/stack-budget.md`.

Ergebnis am neuen Artefakt: `getExternUDP` 9 072 B, `esp32loop` 10 560 B, beide
unter der Schwelle 11 776 (12 288 minus 512 Reserve). F2 allein haette **nicht**
gereicht -- beide Werte liegen weiter ueber 8 192; F1 ist der tragende Teil.

## Offen

- **F4, verschoben:** `sendMessage()` sollte `queueExtern()` statt
  `sendExtern()` inline rufen; die Warteschlange gibt es schon
  (`extudp_functions.cpp:819`, ein Erzeuger, zwei Plaetze, gedraint in
  `esp32_main.cpp:3747`). Das naehme rund 4 kB aus der tiefsten Kette, bricht
  aber auf nRF52 die Ein-Erzeuger-Zusicherung, weil `OnRxDone` dort im
  LORA-Task laeuft.
- `tools/reclaim_eval.py` erkennt Neustarts nur an den 5-Minuten-`STAT`-Zeilen
  und hat zwei der vier verpasst. Auf den `millis()`-Ruecksprung umstellen.
- Pre-existierend, nicht von diesem Fix beruehrt: `resetExternUDP()` haelt
  ueber `startExternUDP()` ein `delay(2000)`; ein fehlgeschlagenes
  `UdpExtern.write()` blockiert den Loop-Task zwei Sekunden.
