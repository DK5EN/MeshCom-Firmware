# Absturz am Extern-UDP-Eingang: Stackueberlauf des Loop-Tasks

Stand 21.09.2026. Betroffen waren alle ESP32-Boards, unabhaengig vom Branch.

`fork-main` hat den Fix **nicht** durch einen Merge bekommen: `neo-ram-reclaim`
wurde nach `fork-neo-test` gemerged (`9397647e`), nach `fork-main` dagegen
inhaltlich portiert, weil die beiden Zweige in `sendExtern()` auseinanderliegen
(`EXTERN_MSG_JSON_BUF` statt `500`, dazu der BP-07-Puffer). Auf `fork-main`
sind es zwei eigene Commits: `c754d01f` (P1) und `efc9681e` (P2 und P3).
Dieses Dokument haelt Befund, Ursache und Beweislage auf fork-mains eigenen
Zeilen und Messwerten fest.

## Befund

Ein Knoten mit `--extudp on` startete **deterministisch neu**, sobald ueber
Extern-UDP ein `{"type":"msg"}`-Datagramm mit einem Ziel ankam, das nicht das
eigene Rufzeichen war. Beobachtet am 20.09.2026 auf DK5EN-98 (Heltec V3),
vier Resets, alle ausgeloest von McApp-Antworten (`!dice`, `!mheard`), die
ueber denselben Extern-UDP-Transport zurueckkamen, auf dem das Kommando
hereinkam.

Ein Datagramm mit dem **eigenen** Rufzeichen als Ziel ueberlebte, weil dieser
Pfad vorher aussteigt: `loop_functions.cpp:4044`
(`[ERROR]...DM to own-all not allowed`), also **vor** dem tiefen Sendepfad.
Die Nachricht landete nie auf der Luft, sie blieb im TX-Ring stehen.

Kein `Guru Meditation` im Mitschnitt: Die Panik geht auf die USB-Serielle,
nicht auf die TCP-Konsole, und die stirbt mit dem Absturz.

## Ursache

Stackueberlauf des Arduino-Loop-Tasks auf der Extern-UDP-Eingangskette:

```
esp32loop -> getExternUDP -> getExtern -> sendMessage -> sendExtern
          -> decodeAPRS -> printfdeb -> MeshSerial/lwIP
```

Der Loop-Task hatte 8192 B (Framework-Vorgabewert). Die Kette passte nicht
hinein.

## Fix

Drei Teile, alle bereits auf fork-main:

- **P1** `-D ARDUINO_LOOP_STACK_SIZE=12288` fuer jeden ESP32-Board-Env.
  Seit Commit `c754d01f` (`platformio.ini:574`, plus sechs eigenstaendige
  Varianten-Envs, die nicht von `[esp32]` erben). **Nicht** der
  `CONFIG_`-Name: `CONFIG_ARDUINO_LOOP_STACK_SIZE` definiert die `sdkconfig.h`
  des Frameworks selbst auf 8192 und wird nach der Kommandozeile eingebunden,
  gewinnt also -- und das still, denn `-Werror` steht nur auf
  `build_src_flags`.
- **P2** `c_json`/`c_tjson` in `sendExtern()` auch auf ESP32 nach BSS.
  Commit `efc9681e` in diesem Baum.
- **P3** `tools/stack_budget.py` als Gate, dokumentiert in
  `docs/stack-budget.md`.

## Messwerte

Mit `tools/stack_budget.py` gegen das `heltec_wifi_lora_32_V3`-Artefakt,
Schwellwert 11776 (Budget 12288 minus 512 B Reserve):

| Kette          | vor P2  | nach P2 |
| -------------- | ------- | ------- |
| `getExternUDP` | 8464 B  | 7264 B  |
| `esp32loop`    | 10160 B | 8960 B  |

Die Differenz ist auf beiden Ketten exakt -1200 B -- das ist
`EXTERN_MSG_JSON_BUF` (700 B, `src/extern_msg_json.h:21`) plus 500 B
(`c_tjson`), also genau die beiden verschobenen Puffer und nichts sonst.

**P2 allein haette nicht gereicht**: Vor P1 lagen beide Werte (8464 und 10160) weiterhin ueber den damaligen 8192 B des Loop-Tasks. Erst P1 schafft
den Abstand, den `tools/stack_budget.py` gegen die Schwelle prueft.

## Reentranz (der tragende Teil von P2)

`sendExtern()` hat auf diesem Baum fuenf Aufrufer, alle ausschliesslich im
jeweiligen Loop-Task:

- `flushExternQueue()` (`src/extudp_functions.cpp`)
- `udp_functions.cpp:279`
- `loop_functions.cpp`, innerhalb `sendMessage()`
- `loop_functions.cpp`, innerhalb `sendPosition()`
- `nrf52/nrf_eth.cpp:453`

Der einzige radio-getriebene Einstieg, `OnRxDone()`, ruft nur
`queueExtern()` (`src/lora_functions.cpp:997`) -- reines Memcopy in den
Ringpuffer `externQueue[]`, kein `sendExtern()`-Aufruf. Genau dafuer gibt es
diese Warteschlange. Die eine reale Reentranz im Baum
(`sendMessage -> bpRoute -> bpEmitNotice -> bpDeliver -> GUI -> sendMessage`)
ist sequenziell, nie verschachtelt.

Auf manchen ESP32-Boards laeuft `checkRX(true)` in einer eigenen
FreeRTOS-Task (`src/t5-epaper/peri_lora.cpp:220`) -- dieser Pfad erreicht
ebenfalls nur `queueExtern()`, nie `sendExtern()` direkt. Statische Puffer
sind damit auf allen Plattformen sicher.

## Feldbeweis

21.09.2026, 14:51 Uhr, DK5EN-98. Derselbe Reproducer, der den Knoten vorher
zuverlaessig umgelegt hatte, lief durch (Test auf dem `neo`-Zweig, der
denselben Fix traegt). Konsole:

```
[EXT] Inc: {"type": "msg", "dst": "9", "msg": "stackprobe", "src": "DK5EN-98"}
[EXT];rx;len;67;stack_hwm;4668
```

Die `[EXT];rx`-Zeile ist die, die es vor dem Fix nie gab, weil der Knoten
vorher starb. Der Frame ging auf die Luft und wurde von vier Nachbarn
relayed.

Zusaetzlich, ueber einen 9 h 36 min Nachtlauf: die ausgehende
`[EXT];tx;stack_hwm`-Zeile stand konstant bei 4876 ueber 604 Messungen,
gegen 368 auf dem Stand vor dem Fix.

## Offen

- **F4, bewusst zurueckgestellt:** `sendMessage()` sollte `queueExtern()`
  statt `sendExtern()` inline aufrufen. Naehme rund 4 kB aus der tiefsten
  Kette, bricht aber die Ein-Erzeuger-Zusicherung auf nRF52, wo `OnRxDone`
  im LORA-Task laeuft.
- `tools/reclaim_eval.py` existiert auf diesem Zweig nicht. Auf dem
  `neo`-Zweig erkennt es Resets nur an den 5-Minuten-`STAT`-Zeilen und hat
  zwei der vier verpasst. Das verlaessliche Signal ist der
  `millis()`-Ruecksprung.
- Pre-existierend, von diesem Fix nicht beruehrt: `resetExternUDP()` haelt
  ueber `startExternUDP()` ein `delay(2000)`; ein fehlgeschlagenes
  `UdpExtern.write()` blockiert den Loop-Task zwei Sekunden.
- Das Gate liefert eine **untere Schranke**: indirekte Calls sind zur
  Bauzeit nicht aufloesbar. Ein PASS ist notwendig, aber nicht hinreichend.
