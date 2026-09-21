# RAM-Rückgewinn: Auswertung des Dauerlaufs, 20.09.2026, 20:00

Dieses Dokument ist der Einstieg für eine neue Session. Es enthält alles, was
für die Auswertung gebraucht wird, in der Reihenfolge, in der es zu tun ist.

## Befund: Neustart am Extern-UDP-Eingang (20.09., ab 18:40) — blockierend

**Der Knoten startet neu, sobald er über Extern-UDP ein `{"type":"msg"}`-Datagramm
mit fremdem Ziel bekommt.** Drei von drei Versuchen. Die Nachricht landet noch im
TX-Ring und geht nie auf die Luft. Vorher lief der Build 7 h 24 min ohne einen
einzigen Neustart — normaler Verkehr, BLE, LoRa, Gateway, alles unauffällig.
Ausgelöst wurde es von McApp: Helmut (OE5HWN-12) schickte `!dice DK5EN-98`, die
Antwort des Bots geht über denselben Transport zurück, auf dem das Kommando
hereinkam — und das war Extern-UDP.

### Die Kette, aus dem Mitschnitt (`~/meshlog/dk5en-98/2026-09-20.log`, rpizero)

```
18:40:29.998  [EXT] Out: {...!dice DK5EN-98{717...} Len: 157
18:40:29.998  [EXT];tx;len;157;stack_hwm;368;ms;26681955
18:40:30.098  [EXT] Inc: {"dst": "OE5HWN-12", "msg": "\ud83c\udfb2 OE5HWN-12: [3][4] \u2192 43", ...}
18:40:30.106  NEW-TXT 067 : x1AE1E024 ... DK5EN-98>OE5HWN-12:... [3][4] -> 43{036
18:40:30.114  RING_WRITE slot=12 type=3A status=00 len=67 msg_id=1AE1E024 queued=1/20 src=user_msg
18:40:30.115  RING_PRIO slot=12 prio=1
              <Stille — Neustart>
18:41:07.466  RX_TIMEOUT_FIRE ts=36247      (vorher ts=26677538)
18:41:30.509  RING_STATUS queued=0 pending=0 iW=2 iR=2      (Ring leer, nie gesendet)
```

Kein `[EXT];rx`-Log nach dem `Inc:` — die Zeile wird erst gedruckt, wenn
`getExtern()` zurückkehrt. Sie kehrt nicht zurück.

### Drei Neustarts, alle am selben Reiz

`millis()`-Rücksprünge im Mitschnitt (erkannt jeweils beim Reconnect des Loggers,
der eigentliche Absturz liegt davor):

| erkannt     | ts-Sprung           | auslösendes Datagramm                  |
| ----------- | ------------------- | -------------------------------------- |
| 18:41:07.47 | 26 677 538 → 36 247 | `!dice`-Antwort an OE5HWN-12, 18:40:30 |
| 18:49:33.97 | 504 709 → 38 593    | zweite `!dice`-Antwort, 18:48:55       |
| 18:51:10.09 | 90 836 → 29 513     | `!mheard`-Antwort (1/2), 18:50:28      |

Am 20.09. gab es genau **sieben** `[EXT] Inc:`-Datagramme. Die drei mit fremdem
Ziel haben den Knoten je einmal umgelegt. Die vier mit `dst=DK5EN-98` (meine
Proben, s. u.) hat er überlebt — dieser Pfad bricht vor dem Sendepfad ab
(`[ERROR]...DM to own-all not allowed`) und geht nie nach `sendMessage()`. Das
grenzt den Absturz auf `getExtern() -> sendMessage() -> TX-Ring / BLE-Out` ein.
Die `!mheard`-Antwort 2/2 (18:50:40) taucht gar nicht mehr als `Inc:` auf — sie
fiel in den Neustart.

### Leitende Hypothese: Stack, und die Zahl ist eindeutig

`[EXT];tx;...;stack_hwm` ist die Low-Water-Marke der Task auf dem
EXTUDP-Ausgangspfad. Der Eingangspfad ist der tiefere (`val[163]` +
`JsonDocument` in `getExtern()`, dazu `msg_text_check`/`msg_text_checked` à 200 B
auf dem Stack in `sendMessage()`, dann `sendExtern()`) — genau der, den der
UDP-01/N-22-Kommentar in `src/extudp_functions.cpp` als „die tiefste
EXTUDP-Kette" markiert.

| Build                     | stack_hwm auf `[EXT];tx`                       |
| ------------------------- | ---------------------------------------------- |
| 13./14.09. (`fork-neo-*`) | Minimum **1 576 B**                            |
| 20.09. `neo-ram-reclaim`  | **368 B** in 385 von 431 Messungen (max 2 288) |

**Rund 1 200 Byte Stack-Reserve weniger als vor dem Umbau.** Der Ausgangspfad
kommt damit gerade noch durch, der tiefere Eingangspfad nicht. Bewiesen ist das
noch nicht: der ESP32-Panic samt Backtrace geht auf die USB-Serielle, nicht auf
die TCP-Konsole, und die Konsole stirbt mit dem Absturz — im Mitschnitt steht
deshalb kein `Guru Meditation`, keine Backtrace-Zeile.

### Ausgeschlossen

- **JSON/Emoji-Escaping und Längenprüfungen.** Dasselbe Datagramm Byte für Byte
  (`\ud83c\udfb2`-Surrogatpaar inklusive) mit eigenem Rufzeichen als `dst`
  durchläuft `deserializeJson()` sauber und kommt bis zum Own-Call-Check.
- **Extern-UDP-Empfang / Socket.** Empfängt und parst, auch nach den Neustarts.
- **Backpressure.** Kein `QRT`/`QTA`; ein Nack ginge über Extern-UDP zurück und
  stünde in der McApp-DB.
- **McApp.** Kommando wurde angenommen, ausgeführt, Antwort erzeugt und an
  `sendto()` übergeben; keine Suppression, kein `UDP_SEND blocked`, kein
  `send_failed`. `udp_target` = 192.168.68.68, `identified`.

### Reproduzierer

```bash
# Absturz — sendet echt auf HF, nur bewusst einsetzen
python3 -c 'import socket,json; socket.socket(socket.AF_INET,socket.SOCK_DGRAM).sendto(
 json.dumps({"dst":"<fremdes-call>","msg":"probe","type":"msg","src":"DK5EN-98"}).encode(),
 ("192.168.68.68",1799))'

# harmlos, kein HF, kein Absturz: dst = eigenes Rufzeichen
#   -> [EXT] Inc: ... / [ERROR]...DM to own-all not allowed / [EXT];rx;...;stack_hwm;1456
```

### Nächste Schritte

1. USB-Serielle am 98er mitschneiden und den Reproduzierer auslösen — Panic und
   Backtrace sind die fehlende Hälfte des Beweises.
2. Stack-Verbrauch der umgebauten Pfade prüfen; Loop-/EXTUDP-Task-Stack erhöhen
   oder die großen Puffer in `getExtern()`/`sendMessage()` nach BSS ziehen (auf
   nRF52 ist genau das schon gemacht, auf ESP32 stehen sie auf dem Stack).
3. Gegenprobe: `fork-neo-test` flashen, dasselbe Datagramm schicken,
   `[EXT];tx;stack_hwm` vergleichen. Erwartung nach der Tabelle oben: ~1 576 B
   und kein Neustart.

### Nebenbefund McApp

Der Verlust ist unsichtbar. Die Extern-UDP-Antwort ist fire-and-forget: nicht
gespeichert, nicht quittiert, kein Retry, kein `send_failed`-Event. Weder Webapp
noch DB kennen sie, einzige Spur ist eine INFO-Zeile im Journal — und journald
hält auf mcapp.local bei dieser Lograte nur rund fünf Minuten
(`RuntimeMaxUse=8M`). Naheliegender Workaround, solange die Firmware steht:
Kommando-Antworten über BLE schicken, solange BLE verbunden ist, unabhängig vom
Eingangstransport (`commands/response.py::_transmit_chunks`).

### Eingriffe von mir am 20.09. (für die Auswertung wichtig)

Zwischen 18:48 und 18:54 habe ich entgegen der Regel unten selbst auf Port 2323
verbunden (zwei Sitzungen) und vier Datagramme `{"dst":"DK5EN-98","msg":"rx
probe"...}` geschickt. Die `[EXT] Inc:`-Zeilen um 18:51:29, 18:52:14, 18:52:59
und 18:53:44 sind meine, ebenso zwei der drei `[LOGGER] reconnect`. Die drei
Neustarts sind **nicht** meine — sie hängen alle an Datagrammen von McApp.

## Wo wir stehen

- Repository: `/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main`
- **Branch: `neo-ram-reclaim`**, abgezweigt von `fork-neo-test` (5ed69ee2).
  Zwei Commits: 2616ef59 (Code) und 51fb5dc4 (Doku). Arbeitsbaum sauber.
- `fork-neo-test` ist unangetastet. **Nicht mergen**, bevor diese Auswertung
  durch ist.
- Beschreibung des Umbaus, Messwerte und was schon geprüft ist:
  `docs/ram-reclaim-20260920.md` im Repository.

Kurzfassung des Umbaus: die drei Ausgangsringe (Telefon-Daten,
Telefon-Kommandos, UDP-Ausgang) sind Byte-Ringe (`src/byte_fifo.h`) statt
20 Schlitze zu 260 Byte. Dazu der Web-Header-Puffer im String und der
Display-Cache halbiert. DRAM gegen `fork-neo-test`: T-Beam −11 952 B,
TLORA −11 936, E22_XML −12 440, Heltec V3 −9 888. IRAM unverändert.

## Was läuft

- **DK5EN-98** (Heltec V3, `dk5en-98.local`) läuft seit 11:14 den Build
  `Sep 20 2026 / 11:14:21` vom Branch, per OTA geflasht.
- **Raspberry Pi `rpizero.local`** schneidet die Konsole (TCP 2323) des 98ers
  mit: `~/meshlog/dk5en-98/2026-09-20.log`, gestartet 11:16 mit
  `meshlogger.py --hours 9`, Ende von selbst gegen **20:15**. Der Logger läuft
  unter `nohup`, keine lokale Shell hängt daran.
- **mcapp.local** (McApp, `../MCProxy`) ist per BLE mit dem 98er verbunden,
  seit 11:16:05 ohne Abbruch (Stand 11:30).

Die Konsole ist Einzelplatz: **nicht selbst auf Port 2323 verbinden**,
solange der Logger läuft. Alles, was der Knoten gedruckt hat, steht im
Mitschnitt auf dem Pi.

## Ausgangslage (Vorabend, Build 19.09., `fork-neo-test`)

Aus `tools/reclaim_eval.py` über acht Stunden:

| Größe                  | Wert                           |
| ---------------------- | ------------------------------ |
| Heap (STAT `heap=`)    | 132 352 → 132 208, min 132 060 |
| Steigung               | −1,8 B je 5-Minuten-Fenster    |
| Neustarts              | 0                              |
| RING_OVERFLOW          | 0                              |
| TX-Ring ringmax / drop | 4 von 20 / 0/0/0/0/0           |

Erwartung für den neuen Stand: Heap rund 10 kB höher (die Byte-Ringe geben
etwa 9,4 kB statisches RAM frei, der Web-Header nimmt 1 kB davon zur Laufzeit
wieder), gleiche flache Steigung, kein Neustart, `RING_OVERFLOW` nur mit
`lost=0` oder gar nicht.

## Die Schritte

### 1. Mitschnitt holen und auswerten

```bash
cd /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main
git branch --show-current            # muss neo-ram-reclaim sein
mkdir -p ~/Downloads/dk5en-98
scp rpizero.local:~/meshlog/dk5en-98/2026-09-20.log ~/Downloads/dk5en-98/2026-09-20-reclaim.log
uv run tools/reclaim_eval.py --since "2026-09-20 11:20" ~/Downloads/dk5en-98/2026-09-20-reclaim.log
```

Das Skript druckt Heap-Trend, Neustarts, Uptime-Sprünge, `RING_OVERFLOW`
je Ring mit verlorenen Frames, Telefon-Ring-Verluste und TX-Ring-Spitze.
`--since 11:20` schneidet die zwei OTA-Neustarts vom Vormittag ab.

Bestanden heißt:

- Heap flach, kein fallender Trend über die neun Stunden
- 0 Neustarts, 0 Uptime-Sprünge — **erwarte hier drei**, alle ab 18:40 und alle
  am Extern-UDP-Eingang; Ursache und Abgrenzung stehen im Befund oben. Für den
  Heap-Trend den Abschnitt bis 18:40 getrennt auswerten, danach ist die Uptime
  dreimal zurückgesetzt.
- `RING_OVERFLOW` mit `lost=` Summe 0 (Verdrängung gelesener Frames ist
  Verlauf, kein Verlust)
- TX-Ring wie am Vorabend (der ist nicht umgebaut)

Falls `[LOGGER] reconnect` oft vorkommt oder Lücken im Zeitraum sind: das ist
der Logger, nicht der Knoten. Uptime `up=` in den STAT-Zeilen ist die
Wahrheit über Neustarts.

### 2. Logger-Status auf dem Pi

```bash
ssh rpizero.local 'cat ~/meshlog/dk5en-98/status.txt; pgrep -f "meshlogger.py dk5en-98" || echo beendet'
```

Läuft er wider Erwarten noch, einfach warten oder mit `pkill -f meshlogger.py`
beenden; die Datei ist bis dahin vollständig.

### 3. BLE-Verbindung über den Tag

```bash
ssh mcapp.local 'journalctl -u mcapp-ble.service --since "2026-09-20 11:20" --no-pager | grep -i "disconnect\|reconnect\|Connected to" | tail -20'
```

Bestanden: keine Abbrüche nach 11:16:05. Jeder `Unexpected disconnect` nach
diesem Zeitpunkt ist ein Befund gegen den Byte-Ring (Telefon-Daten oder
Kommandos), es sei denn, der Knoten hat neu gestartet (Schritt 1).

Tatsächlich stehen dort drei Abbrüche — 18:40:31, 18:48:56 und 18:50:30 —, jeder
ein bis zwei Sekunden nach einem Extern-UDP-Datagramm von McApp. Das sind die
drei Neustarts aus dem Befund oben, kein eigenständiger BLE-Befund. Bis 18:40
hielt die Verbindung durchgehend.

Optional die Register des Knotens, mit dem API-Key aus
`/etc/mcapp/config.json` auf dem Pi (Feld `BLE_API_KEY`, Header `X-API-Key`,
Port 8081):

```bash
ssh mcapp.local 'KEY=$(python3 -c "import json;print(json.load(open(\"/etc/mcapp/config.json\"))[\"BLE_API_KEY\"])"); curl -s -H "X-API-Key: $KEY" http://127.0.0.1:8081/api/ble/status'
```

### 4. Web-Oberfläche

```bash
curl -s --http0.9 "http://dk5en-98.local/?getmessages" | grep -o 'font-normal">[^<]*' | tail -5
```

Bestanden: die Nachrichten des Tages erscheinen, darunter noch die eigene
`reclaim webtest 11:20` an DK5EN-92, sofern der Verlauf sie nicht schon
verdrängt hat (2 048 Byte, rund 25 Frames).

### 5. Entscheiden

Alles bestanden: `neo-ram-reclaim` ist reif, in `fork-neo-test` zu wandern
(Merge oder Cherry-Pick der zwei Commits, dann `tools/neo/derive.sh` für
`fork-neo`). Das ist eine Entscheidung, keine Automatik.

Etwas nicht bestanden: Befund in `docs/ram-reclaim-20260920.md` unter einem
neuen Abschnitt „Dauerlauf" festhalten, Knoten notfalls zurückflashen:

```bash
git checkout fork-neo-test && pio run -e heltec_wifi_lora_32_V3
python3 tools/webflash.py dk5en-98.local --env heltec_wifi_lora_32_V3 --expect-hw HELTEC_V3
git checkout neo-ram-reclaim
```

## Nicht vergessen

- Der Logger hat `--loradebug on` und `--txcapture on` gesetzt und stellt
  beim Ende den vorherigen Zustand wieder her. `--setlog on` war schon vorher
  an und bleibt.
- Bench-Verkehr nur an Gruppe 9/9999/90 oder direkt, nie `*`.
- Nichts nach upstream, nichts nach `origin` pushen, bis das entschieden ist.
