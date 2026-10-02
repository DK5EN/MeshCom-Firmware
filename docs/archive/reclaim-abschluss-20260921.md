# RAM-Rückgewinn: Abschlussbericht

Stand 21.09.2026. Dieses Dokument schließt die Kampagne ab, die in
`docs/ram-reclaim-20260920.md` beschrieben und in
`docs/reclaim-auswertung-20260920.md` und `docs/reclaim-auswertung-20260921.md`
ausgewertet wurde. Es ist der letzte Stand; wo es den beiden
Auswertungsdokumenten widerspricht, gilt dieses hier.

## Urteil

**Bestanden.** Der Umbau geht nach `fork-neo-test`, `fork-neo` wird daraus neu
abgeleitet. Der Nachtlauf über 9 h 36 min auf DK5EN-98 erfüllt jedes Kriterium,
und der Absturz, der die erste Auswertung blockiert hatte, ist als Fremdursache
belegt und behoben.

## Was gemessen wurde

Mitschnitt `~/Downloads/dk5en-98/nachtlauf.log`, 194 259 Zeilen, Fenster
2026-09-20 22:20:19 bis 2026-09-21 07:56:23, Logger-Reconnects 0.

| Kriterium             | Sollwert         | Messwert                            | Urteil |
| --------------------- | ---------------- | ----------------------------------- | ------ |
| Heap-Trend            | flach            | −1,8 B je 5-Minuten-Fenster (n=116) | PASS   |
| Heap absolut          | —                | 147 508 → 147 364, min 143 928      | PASS   |
| Neustarts (`[INIT]`)  | 0                | 0                                   | PASS   |
| Neustarts (`millis`)  | 0                | 0                                   | PASS   |
| `RING_OVERFLOW`       | Summe `lost=` 0  | 0 Ringe, 0 verlorene Frames         | PASS   |
| Telefon-Ring `lost>0` | nie              | 0                                   | PASS   |
| TX-Ring               | Spitze ~6 von 20 | Spitze 4 von 20, `0/0/0/0/0`        | PASS   |
| `[EXT];tx` stack_hwm  | > 3 000          | konstant 4 876 (604 Messungen)      | PASS   |
| `[EXT];rx` stack_hwm  | > 3 000          | 4 668 (Live-Probe, s. u.)           | PASS   |
| BLE über Nacht        | keine Abbrüche   | keine Abbrüche                      | PASS   |

Die Neustarts wurden mit **beiden** Verfahren gezählt: `[INIT]`-Zeilen über
`tools/reclaim_eval.py` und unabhängig davon der Rücksprung von `millis()` über
alle `ms;`/`ts=`-Felder. Das zweite Verfahren liefert genau einen Treffer, und
der ist der bekannte Falschtreffer auf der Statuszeile am Dateiende
(`reconnects=0` wird als `ts=0` gelesen). Echte Neustarts: keine.

## Die Lücke, die geschlossen wurde

Der Nachtlauf enthält **kein einziges eingehendes Extern-UDP-Datagramm**. Damit
war genau das Kriterium, um dessentwillen der Lauf angesetzt wurde — die
Mindest-Stack-Reserve auf der tiefsten Kette `getExtern() → sendMessage() →
sendExtern()` — nicht belegt, sondern nur nicht widerlegt.

Deshalb wurde am 21.09. um 14:51 der Reproduzierer live ausgelöst, an Gruppe 9:

```
[EXT] Inc: {"type": "msg", "dst": "9", "msg": "stackprobe", "src": "DK5EN-98"}
[EXT];tx;len;146;stack_hwm;4668;ms;60020679
[EXT];rx;len;67;stack_hwm;4668;ms;60020680
```

Die `[EXT];rx`-Zeile ist der Beweis: sie ist die Zeile, die es vor dem Fix bei
**jedem** Fremdziel-Datagramm nicht gab, weil der Knoten vorher starb. Der Frame
ging anschließend auf die Luft und wurde von vier Nachbarn relayed (DK5EN-90,
DL2UD-1, DL2JA-2, DB0ED-99). Kein Neustart, kein `Guru Meditation`.

## Der Absturz vom 20.09. — erledigt

Ursache war **kein** Rückschritt des RAM-Umbaus, sondern ein Stacküberlauf des
Arduino-Loop-Tasks auf der Extern-UDP-Eingangskette, reproduzierbar auch auf
`fork-neo-test`. Herleitung in `docs/bug-extudp-stack-20260920.md`, Fix in
`8990e94d`:

- `-D ARDUINO_LOOP_STACK_SIZE=12288` für jeden ESP32-Board-Env. **Nicht** der
  `CONFIG_`-Name — den definiert die `sdkconfig.h` des Frameworks selbst auf
  8192 und gewinnt, still.
- `c_json`/`c_tjson` in `sendExtern()` auch auf ESP32 nach BSS (−1 008 B).
- `tools/stack_budget.py` als Gate.

Die Zahl, die es trägt, ist die Verschiebung von `[EXT];tx;stack_hwm`:
**368 im alten Lauf, 4 876 im Nachtlauf**, 604 Messungen ohne Streuung.

Zwei Korrekturen an der Auswertung vom 20.09. bleiben stehen: es waren **vier**
Neustarts, nicht drei, und der Vergleich „368 gegen 1 576, also 1 200 Byte
weniger Reserve" trug nicht — 368 war das Minimum EINES langen Boots, nicht der
Normalwert.

## Gate

Auf dem Stand nach dem Merge, sequentiell gebaut:

| Prüfung                            | Ergebnis                                                            |
| ---------------------------------- | ------------------------------------------------------------------- |
| `pio run` Heltec V3                | SUCCESS, DRAM 165 104 B (47,74 %)                                   |
| `pio run` RAK4631                  | SUCCESS                                                             |
| `pio run` T-Beam                   | SUCCESS                                                             |
| `pio run` E22_XML-DevKitC          | SUCCESS                                                             |
| `stack_budget.py --budget 12288`   | `getExternUDP` 9 072, `esp32loop` 10 560, Schwelle 11 776, **PASS** |
| Host-Suite, alle 35 `native*`-Envs | **998 von 998**                                                     |

## Drei Funde beim Merge nach `fork-neo-test`

Der Merge hatte drei Konflikte und deckte einen vorbestehenden Defekt auf.

**1. `esp32_main.cpp` / `nrf52_main.cpp` — beide Seiten gehören hinein.**
`fork-neo-test` hatte aus PR #1147 den Zweig `mheardToPhonePending()` zwischen
Kommando-Ring und Telefon-Ring eingezogen, der Umbau hatte im selben `else if`
`toPhoneWrite != toPhoneRead` durch `!bf_empty(&phoneRing)` ersetzt. Aufgelöst,
indem der MHeard-Zweig bleibt und die Bedingung darunter auf den Byte-Ring
umgestellt wird.

**2. `loop_functions.cpp` — die Byte-Ring-Seite ersetzt die andere.** PR #1147
hatte in `addBLEComToOutBuffer()` den Lesezeiger beim Überlauf nachgezogen
(`addRingPointer(...)`), weil der Schlitzring sonst nach genau `MAX_RING`
Schreibvorgängen als leer galt. Der Byte-Ring leistet dasselbe von sich aus:
`bf_push()` verdrängt die ältesten Frames und meldet zurück, wie viele davon
ungelesen waren. Der Zeiger-Nachzug hat dort keinen Gegenstand mehr.

**3. `native_parsers` linkte seit PR #1147 überhaupt nicht — vorbestehend.**
`comRingFree()` in `mheard_functions.cpp` las `ComToPhoneRead`/`ComToPhoneWrite`,
die in dieser Env nicht gelinkt werden und auch nicht gestubbt waren. Am
unveränderten `4d162cdc` nachgestellt: dieselben fünf Tests sind dort ebenso rot.
Das ist **keine** Folge des Umbaus.

Behoben und dabei drei Tests repariert, die nie gelaufen waren:

- `comRingFree()` → `comRingWouldEvictUnread()`, gegen `bf_unread()` statt gegen
  freie Schlitze. Schranke ist der größte Frame, der hier überhaupt geschrieben
  wird: `addBLEComToOutBuffer()` klemmt auf 245, dazu das Längenbyte. Bewusst
  nicht `MAX_MSG_LEN_PHONE` (300) — so lang wird hier nie geschrieben, und die
  kleinste Ringklasse (1 536 B) hätte sonst unnötig weniger Durchsatz. Gelesene
  Frames dürfen weichen, sie sind nur Verlauf; deshalb `bf_unread()` und nicht
  `bf_used()`.
- Stub-Definition von `phoneComRing` in `parser_link_stubs.h` und in den beiden
  Suiten mit eigenem Stub-Satz (`test_mheard_render`, `test_mheard_aging`).
- Drei `sendMheard()`-Fälle in `test_mheard_render` riefen `sendMheard()` ohne
  vorheriges `startMheardToPhone()`. Seit der Cursor-Aufteilung kehrt die
  Funktion dann sofort zurück, die Fälle prüften also nichts. Die Produktion
  ruft beides in dieser Reihenfolge (`esp32_main.cpp:3159`,
  `nrf52_main.cpp:1854`); die Tests jetzt auch. `native_parsers` steht damit bei
  67 von 67.

## Was in `fork-main` schon war — und was nicht

`fork-main` hat den Umbau **nicht** durch Merge oder Cherry-Pick bekommen,
sondern über eigene Commits mit derselben Absicht. Am Baum geprüft:

| Teil                                                      | `fork-main`                           |
| --------------------------------------------------------- | ------------------------------------- |
| Byte-Ringe (`src/byte_fifo.{h,cpp}` + Anschluss)          | **da** — `e2861458`, `9530967d`       |
| `ARDUINO_LOOP_STACK_SIZE=12288`                           | **da** — `c754d01f`, alle sechs Envs  |
| `c_json`/`c_tjson` → BSS auf ESP32                        | **fehlt** — `#ifdef ESP32` steht noch |
| Option 2: Web-Header im `String`                          | **fehlt** — `static char[1024]`       |
| Option 3: Display-Cache (`int16_t`, `HAS_LONG_PAGE_TEXT`) | **fehlt**                             |
| `tools/stack_budget.py`, `docs/stack-budget.md`           | **fehlt**                             |

`src/byte_fifo.h` ist auf beiden Zweigen byte-identisch, `byte_fifo.cpp`
unterscheidet sich um acht Zeilen. Ein reiner Cherry-Pick von `8990e94d` geht
nicht: `fork-main` hat in `sendExtern()` `EXTERN_MSG_JSON_BUF` statt `500` und
zusätzlich den BP-07-Puffer. Portiert wird inhaltlich, nicht mechanisch.

Der Wellenplan dafür lag als `~/Desktop/konzept-stackfix-p2-p4-forkmain-20260921.md`
und ist am 21.09.2026 abgearbeitet. Auf `fork-main` sind jetzt:

| Commit     | Inhalt                                                                   |
| ---------- | ------------------------------------------------------------------------ |
| `efc9681e` | P2 (`c_json`/`c_tjson` nach BSS), `tools/stack_budget.py`, die Gate-Doku |
| `36b3a895` | Option 2 (Web-Header im `String`) und Option 3 (Display-Cache)           |

Gemessen an diesem Baum, Schwelle 11 776:

| Kette          | vorher   | nachher | Delta  |
| -------------- | -------- | ------- | ------ |
| `getExternUDP` | 8 464 B  | 7 264 B | −1 200 |
| `esp32loop`    | 10 160 B | 8 960 B | −1 200 |

−1 200 B ist exakt `EXTERN_MSG_JSON_BUF` (700) + 500 — die beiden Puffer, sonst
nichts. Gate: 14 Board-Envs über alle Display-Familien SUCCESS, 873 von 873
Host-Testfällen, unverändert gegen die Baseline vor der Welle. Ein Advisor-Pass
(Fable-Modus) hat die drei tragenden Behauptungen — Reentranz von
`sendExtern()`, Schrankengleichheit des Web-Headers, Vollständigkeit der
Guard-Vereinigung — angegriffen und keine davon widerlegt; am Artefakt
nachgewiesen, dass `pageLastTextLong1/2` auf OLED-Boards fehlen und auf
TFT/E-Paper-Boards vorhanden sind.

`t5_epaper` und `esp32-external-radio` bauen weiterhin nicht. Beide sind schon
am unveränderten `45e411d4` rot und aus Gründen ohne Bezug zu dieser Änderung.

## Offen

- **F4, bewusst verschoben:** `sendMessage()` sollte `queueExtern()` statt
  `sendExtern()` inline rufen. Das nähme rund 4 kB aus der tiefsten Kette,
  bricht aber auf nRF52 die Ein-Erzeuger-Zusicherung, weil `OnRxDone` dort im
  LORA-Task läuft.
- `tools/reclaim_eval.py` erkennt Neustarts nur an den 5-Minuten-`STAT`-Zeilen
  und hat am 20.09. zwei von vier verpasst. Auf den `millis()`-Rücksprung
  umstellen.
- Vorbestehend, von dieser Kampagne nicht berührt: `resetExternUDP()` hält über
  `startExternUDP()` ein `delay(2000)`; ein fehlgeschlagenes
  `UdpExtern.write()` blockiert den Loop-Task zwei Sekunden.
- Der Verlust einer Extern-UDP-Antwort ist bei McApp unsichtbar
  (fire-and-forget, kein Retry, kein `send_failed`). Workaround wäre,
  Kommando-Antworten über BLE zu schicken, solange BLE verbunden ist
  (`commands/response.py::_transmit_chunks`).
- Kein Upstream-PR in dieser Kampagne. Der Stack-Fix betrifft jeden ESP32 mit
  `--extudp on`, ist also upstream-fällig — die Entscheidung fällt nach dem
  Bench, getrennt.
