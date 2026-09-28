# Nachtlauf DK5EN-98: Auswertung am 21.09.2026, ab 08:00

Einstieg fuer eine neue Session. Alles, was fuer die Auswertung gebraucht wird,
in der Reihenfolge, in der es zu tun ist. Ersetzt
`reclaim-auswertung-20260920.md` (dessen Befund ist erledigt, s. u.).

## Was seit gestern Abend passiert ist

Der Absturz vom 20.09. ist gefunden, behoben, geprueft und committet.

**Ursache:** Stackueberlauf des Arduino-Loop-Tasks auf der Extern-UDP-
Eingangskette. `esp32loop -> getExternUDP -> getExtern -> sendMessage ->
sendExtern -> decodeAPRS -> printfdeb -> MeshSerial/lwIP` brauchte mehr als
die 8 192 Byte, die der Task hatte. Keine Regression des RAM-Umbaus: die
Frame-Groessen sind auf `fork-neo-test` Byte fuer Byte gleich, und die
Gegenprobe mit der Basis auf demselben Knoten stuerzte identisch ab.
Vollstaendige Herleitung: `docs/bug-extudp-stack-20260920.md` im Repository.

**Korrekturen an der gestrigen Auswertung**, beide belegt:

- Es waren **vier** Neustarts, nicht drei. Der vierte (19:32:35) fehlte, weil
  `tools/reclaim_eval.py` Neustarts nur an den 5-Minuten-`STAT`-Zeilen erkennt.
- Das vierte Datagramm war **reines ASCII** -- Emoji und Surrogatpaare sind
  als Ursache ausgeschlossen, nicht nur unwahrscheinlich.
- `stack_hwm` 368 war das Minimum EINES langen Boots, nicht der neue
  Normalwert; nach jedem Neustart standen dort 1 184 bis 2 284. Der Vergleich
  368 gegen 1 576 trug die Aussage "1 200 Byte weniger Reserve" nicht.

**Fix (Commit `8990e94d` auf `neo-ram-reclaim`):**

- `-D ARDUINO_LOOP_STACK_SIZE=12288` fuer jeden ESP32-Board-Env. **Nicht** der
  `CONFIG_`-Name -- den definiert die `sdkconfig.h` des Frameworks selbst auf
  8192 und gewinnt; das Flag waere stumm wirkungslos geblieben. Am Artefakt
  geprueft.
- `c_json`/`c_tjson` in `sendExtern()` auch auf ESP32 nach BSS (-1 008 B).
- `tools/stack_budget.py` als Gate, siehe `docs/stack-budget.md`.

**Belegt nach dem Flashen:** derselbe Reproduzierer, der die Basis umlegte,
laeuft jetzt durch. `[EXT];rx;len;73;stack_hwm;4944` und
`[EXT];rx;len;75;stack_hwm;4876` -- die Zeile, die es bei jedem Absturz nie
gab. Spitzenverbrauch 7 412 B von 12 288. Zurueckgerechnet haette der alte
Build dafuer 8 420 B gebraucht, bei 8 192 verfuegbar.

## Was laeuft

- **DK5EN-98** (Heltec V3) traegt den Build `Sep 20 2026 / 22:04:46` vom Branch
  `neo-ram-reclaim`, Stand `8990e94d`, per OTA geflasht.
- **rpizero.local** schneidet die Konsole mit, gestartet **22:20:19** mit
  `meshlogger.py dk5en-98.local --hours 9.6 --outdir ~/meshlog/dk5en-98`,
  endet von selbst gegen **08:00**. Unter `setsid nohup`, Elternprozess ist
  init -- keine Shell und keine SSH-Sitzung haengt daran.
- **mcapp.local** haengt per BLE am 98er.

Zwei Dinge zum Mitschnitt, die sonst Zeit kosten:

- Der Logger legt **pro Datum** eine Datei an. Der Lauf geht ueber Mitternacht,
  die Aufzeichnung liegt also in **`2026-09-20.log` UND `2026-09-21.log`**.
- `2026-09-20.log` enthaelt **zwei** Laeufe: den alten von 11:16 bis 20:16 mit
  den vier Abstuerzen, und ab **22:20** den neuen. Deshalb unten ueberall
  `--since "2026-09-20 22:20"`.
- Unwichtig, nur zur Einordnung: `~/meshlog/dk5en-98-bruchstueck-2217.log` sind
  drei Minuten Fehlstart mit falschem Zielverzeichnis. Kann weg.

Die Konsole ist Einzelplatz: **nicht selbst auf Port 2323 verbinden**, solange
der Logger laeuft.

## Die Schritte

### 1. Mitschnitt holen und zusammensetzen

```bash
cd /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main
git branch --show-current            # muss neo-ram-reclaim sein
git log --oneline -1                 # muss 8990e94d sein oder darauf aufbauen

mkdir -p ~/Downloads/dk5en-98
scp 'rpizero.local:~/meshlog/dk5en-98/2026-09-2[01].log' ~/Downloads/dk5en-98/
cat ~/Downloads/dk5en-98/2026-09-20.log ~/Downloads/dk5en-98/2026-09-21.log \
  > ~/Downloads/dk5en-98/nachtlauf.log

uv run tools/reclaim_eval.py --since "2026-09-20 22:20" ~/Downloads/dk5en-98/nachtlauf.log
```

Vorher pruefen, dass der Lauf wirklich zu Ende ist:

```bash
ssh rpizero.local 'cat ~/meshlog/dk5en-98/status.txt'   # status=finished
```

**Bestanden heisst:**

- Heap flach, kein fallender Trend ueber die knapp zehn Stunden
- **0 Neustarts** -- diesmal wirklich null, das ist der Zweck des Laufs
- `RING_OVERFLOW` mit `lost=`-Summe 0
- TX-Ring wie bisher (Spitze um 6 von 20, Drop-Muster `0/0/0/0/0`)

### 2. Neustarts unabhaengig nachzaehlen

`reclaim_eval.py` verpasst dicht aufeinanderfolgende Neustarts (es sieht nur
die 5-Minuten-`STAT`-Zeilen und hat gestern zwei von vier uebersehen).
Verlaesslich ist der Ruecksprung von `millis()`:

```bash
grep -hoE "^[0-9-]+ [0-9:]+\.[0-9]+.*(ms;[0-9]+|ts=[0-9]+)" ~/Downloads/dk5en-98/nachtlauf.log \
 | sed -E 's/^([0-9-]+ [0-9:]+)\.[0-9]+.*(ms;|ts=)([0-9]+).*/\1 \3/' \
 | awk '$1" "$2 >= "2026-09-20 22:20"' \
 | awk 'NR==1{p=$3;next}{if($3+0 < p+0-1000) print "REBOOT "$1" "$2"  "p" -> "$3; p=$3}'
```

Achtung: die Statuszeile am Dateiende enthaelt `reconnects=N`, also die
Zeichenfolge `ts=` -- der erzeugt einen Falschtreffer, ignorieren.

Erwartung: **keine Ausgabe**.

### 3. Die tiefe Kette hat gehalten

```bash
grep -F '[EXT];rx' ~/Downloads/dk5en-98/nachtlauf.log | tail -20
```

(Einfache Anfuehrungszeichen -- die eckige Klammer sonst als Zeichenklasse
gelesen, das hat gestern Abend eine Viertelstunde gekostet.)

Jede Zeile ist ein Extern-UDP-Datagramm, das den ganzen Weg durch
`sendMessage()` ueberlebt hat. Interessant ist das **Minimum** von
`stack_hwm`:

| Minimum         | Bedeutung                                            |
| --------------- | ---------------------------------------------------- |
| ueber 3 000     | reichlich Luft, 12 288 ist grosszuegig bemessen      |
| 1 000 bis 3 000 | passt, so war es gedacht                             |
| unter etwa 500  | auch 12 288 reicht nicht -- naechster Schritt ist F4 |

Zum Vergleich die beiden Messungen direkt nach dem Flashen: 4 944 und 4 876.
Und am 20.09. vor dem Fix gab es diese Zeile bei jedem Fremdziel-Datagramm
**gar nicht** -- der Knoten starb vorher.

### 4. Stack-Budget-Gate

```bash
pio run -e heltec_wifi_lora_32_V3
uv run tools/stack_budget.py .pio/build/heltec_wifi_lora_32_V3/firmware.elf \
  --root esp32loop --root getExternUDP --budget 12288
echo "exit=$?"     # 0 = bestanden
```

Erwartung: `getExternUDP` 9 072 B, `esp32loop` 10 560 B, beide unter der
Schwelle 11 776.

### 5. BLE-Verbindung ueber die Nacht

```bash
ssh mcapp.local 'journalctl -u mcapp-ble.service --since "2026-09-20 22:20" --no-pager | grep -i "disconnect\|reconnect\|Connected to" | tail -20'
```

Bestanden: keine Abbrueche. Jeder Abbruch ohne zugehoerigen Knoten-Neustart
waere ein Befund gegen die Byte-Ringe (Telefon-Daten oder Kommandos).

### 6. Entscheiden

Alles bestanden: `neo-ram-reclaim` ist reif fuer `fork-neo-test` (Merge oder
Cherry-Pick, dann `tools/neo/derive.sh` fuer `fork-neo`). Das ist eine
Entscheidung, keine Automatik.

Der Stack-Fix `8990e94d` betrifft **alle** ESP32-Boards und haengt nicht am
RAM-Umbau. Er sollte unabhaengig davon nach `fork-main` und als PR nach
upstream -- jeder Knoten mit `--extudp on` ist betroffen, auch die im Feld.

Etwas nicht bestanden: Befund in `docs/ram-reclaim-20260920.md` unter
"Dauerlauf" ergaenzen. Zurueckflashen:

```bash
git checkout fork-neo-test && pio run -e heltec_wifi_lora_32_V3
python3 tools/webflash.py dk5en-98.local --env heltec_wifi_lora_32_V3 --expect-hw HELTEC_V3
git checkout neo-ram-reclaim
```

## Offene Punkte

- **F4, bewusst verschoben:** `sendMessage()` sollte `queueExtern()` statt
  `sendExtern()` inline rufen. Die Warteschlange gibt es schon
  (`extudp_functions.cpp:819`, gedraint in `esp32_main.cpp:3747`). Das naehme
  rund 4 kB aus der tiefsten Kette, bricht aber auf nRF52 die
  Ein-Erzeuger-Zusicherung, weil `OnRxDone` dort im LORA-Task laeuft.
- `tools/reclaim_eval.py` auf die `millis()`-Erkennung aus Schritt 2
  umstellen, statt nur `STAT` zu lesen.
- Pre-existierend: `resetExternUDP()` haelt ueber `startExternUDP()` ein
  `delay(2000)`; ein fehlgeschlagenes `UdpExtern.write()` blockiert den
  Loop-Task zwei Sekunden.
- Der Verlust einer Extern-UDP-Antwort ist bei McApp unsichtbar
  (fire-and-forget, kein Retry, kein `send_failed`). Unveraendert seit
  gestern, Workaround waere, Kommando-Antworten ueber BLE zu schicken,
  solange BLE verbunden ist (`commands/response.py::_transmit_chunks`).

## Reproduzierer

```bash
# Nur an Gruppe 9 -- nie an ein fremdes Rufzeichen, nie an "*".
python3 -c 'import socket,json; socket.socket(socket.AF_INET,socket.SOCK_DGRAM).sendto(
 json.dumps({"type":"msg","dst":"9","msg":"stackprobe","src":"DK5EN-98"}).encode(),
 ("192.168.68.68",1799))'
```

Vor dem Fix: Knoten weg, Mitschnitt bricht auf `[EXT] Inc:` ab.
Nach dem Fix: `[EXT];rx;...;stack_hwm;...` und der Knoten laeuft weiter.

## Nicht vergessen

- Der Logger setzt `--loradebug on` und `--txcapture on` und stellt den
  vorherigen Zustand am Ende wieder her.
- Bench-Verkehr nur an Gruppe 9/9999/90 oder direkt, nie `*`.
- Nichts nach upstream, nichts nach `origin` pushen, bis das entschieden ist.
