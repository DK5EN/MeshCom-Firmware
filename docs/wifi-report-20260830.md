# WLAN-Bericht: Warum ESP32-Knoten am WLAN scheiterten — und was es behoben hat

Datum 2026-08-30. Branch `tdeck-partial-refresh-trace`, Commits `f34fd2ae` … `66d965e7`.
Vorarbeit: [`wifi-findings-20260829.md`](wifi-findings-20260829.md) (Treiberanalyse, Fragen a–g),
Backlog-Zeilen TM-34, TM-24, TM-11/TD-01, TM-17, HL-01/02 in [`BACKLOG.md`](BACKLOG.md) §3.8f.
Rohdaten: `tools/bench/runs/bootloop_*/` (je Arm `summary.txt` + `summary.csv`).

---

## 1. Kurzfassung

Ein MeshCom-Knoten auf ESP32 (T-Deck, Heltec V3, T-Beam) brauchte an einem modernen Router
(WPA2/WPA3-Mischbetrieb, Mesh mit zwei Access Points) **55 Sekunden und zehn Anmeldeversuche**, bis
er eine IP-Adresse hatte — in **0 von 24** Starts gelang der erste Versuch. Nach der Änderung
gelingt der erste Versuch in **24 von 24** Starts (T-Deck), **12 von 12** (Heltec V3) und **12 von
12** (T-Beam), ohne einen einzigen Verbindungsabbruch; die IP steht nach 10–14 s Betriebszeit.
Der 9,1-h-Langzeit-Soak (§7.1) bestätigt das: 165/165 erzwungene Trennungen selbständig erholt,
kein Absturz, kein Watchdog-Eingriff.

Es war nicht ein Fehler, sondern **drei, die sich gegenseitig verdeckt haben**:

1. **Die Firmware hat den Access Point selbst gewählt und festgenagelt** (Kanal + BSSID aus einem
   eigenen Kurz-Scan). Damit war der Treiber-Reconnect vertraglich daran gehindert, jemals einen
   anderen AP zu nehmen — und der Treiber-eigene Mechanismus "alle Kanäle scannen, nach Signal
   sortieren, bei Fehlschlag den nächsten" war stillgelegt.
2. **WPA3-SAE am Mischbetriebs-AP**: der ESP-IDF-4.4-Treiber versucht SAE, sobald der AP es anbietet.
   Mit dem BSSID-Pin scheiterte diese Anmeldung **jedes Mal** mit `AUTH_EXPIRE` (Grund 2). Ohne Pin
   klappt sie — aber jeder zweite Start braucht einen stillen zweiten Anlauf (+4,5 s). Ohne PMF
   (`esp_wifi_disable_pmf_config()`) handelt die Station WPA2-PSK aus: deterministisch, 0 Abbrüche.
3. **Ein blindes Fenster**: nach dem Aufgeben des Boot-Versuchs hat niemand mehr auf ein `got_ip`
   des Treibers reagiert. Der Treiber verband sich später von allein — die Firmware merkte es nicht,
   und der 5-Minuten-Pfad riss die stehende Verbindung für einen neuen Scan wieder ab.

Dazu kam als größter Loop-Blocker `WiFi.hostByName()` mit bis zu 31 s auf `loopTask` (LoRa-Empfang
steht in dieser Zeit), und beim Nachmessen ein vierter, selbst eingebauter Fallstrick: **jeder
`esp_wifi_*`-Aufruf aus der Hauptschleife blockiert, solange der Treiber scannt** (2,9 s).

Warum das so lange unentdeckt blieb: jedes Symptom für sich sah nach etwas anderem aus — nach
schwachem Signal, nach BLE-Koexistenz, nach Band-Steering des Routers, nach "der Router mag den
ESP32 nicht". Erst ein Bench, der **jeden Start gleich behandelt und die Treiber-Ereignisse mitloggt**,
hat die Mechanismen getrennt.

---

## 2. Das Symptom, wie es im Feld ankommt

- Knoten ist nach dem Einschalten "fünf Minuten weg", kommt dann von allein.
- `[WIFI]...ssid<…> connection error` in Serie, dann ein Funk-Reset, dann wieder Fehler.
- Auf dem T-Deck: Kopfzeilen-Icons blinken, Bedienung reagiert verzögert.
- An einem WPA2-only-WLAN oder an einem einzelnen alten AP: **alles gut**. Deshalb war es beim
  Entwickler oft nicht reproduzierbar, beim Nutzer mit neuem Router (Orbi, Fritz!Box 7590 mit
  WPA2/WPA3, Unifi mit "WPA2/WPA3 mixed") ständig.
- Upstream-Nebenbefunde derselben Familie: `SET > WIFI` zeigt OFF, obwohl verbunden (#690); frisch
  geflashte T-Decks starten mit WLAN aus (HL-02); WLAN am T-Deck nur über die GUI einschaltbar
  (HL-01).

---

## 3. Messaufbau

| Was             | Wert                                                                                                    |
| --------------- | ------------------------------------------------------------------------------------------------------- |
| WLAN            | Netgear Orbi `ORBI63`: Router + Satellit, eine SSID über 2,4/5 GHz, **WPA2/WPA3 Personal**, Kanal 3     |
| Vergleichs-WLAN | `ORBI63_Guest` auf demselben Orbi, **WPA2-only** (A5)                                                   |
| Knoten          | T-Deck Plus `DK5EN-14`, Heltec V3 `DK5EN-93`, T-Beam v1.2 `DK5EN-92`; RSSI am Tisch −47 … −75 dBm       |
| Runner          | `tools/bench/experiments/bootloop.py`: Port öffnen = Reset, 75 s mitschneiden, eine CSV-Zeile pro Start |
| Marker          | `[WIFI];event;connected/got_ip/disconnected;reason;N;ms;N` (rohes `Serial.printf`)                      |
| Kriterium       | `got_ip` innerhalb 25 s Betriebszeit **und** kein `connection error` (erster Versuch)                   |
| Arme pro Nacht  | gleicher Runner, gleiche Stunde — der AP verhält sich tageszeitabhängig                                 |

Die 24-Start-Arme laufen ~31 min; alle Zahlen unten sind aus diesen Läufen, nichts ist geschätzt.

---

## 4. Vorher — die Messungen, die das Problem eingekreist haben

| Arm                                        | Erste Anmeldung | `got_ip` Median / Max | Abbrüche                                  | Was es bewiesen hat                                                                       |
| ------------------------------------------ | --------------- | --------------------- | ----------------------------------------- | ----------------------------------------------------------------------------------------- |
| TM-11 A: Stand vorher, 12 Starts           | 4/12            | —                     | jeder Fehlschlag `2:AUTH_EXPIRE`          | Ausgangslage                                                                              |
| TM-11 B: BLE-Advertising verzögert         | 3/12            | —                     |                                           | **BLE-Koexistenz widerlegt**                                                              |
| TM-11 C: nur SSID, kein BSSID-Pin          | 8/12            | —                     |                                           | der Pin ist ein Faktor                                                                    |
| **A5**: Stand vorher, **WPA2-only-SSID**   | **24/24**       | 9,6 s / 10,1 s        | 0                                         | mit WPA2 ist der Pin harmlos → **Sicherheitsmodus**                                       |
| **A0**: Stand vorher, WPA2/WPA3, 24 Starts | **0/24**        | **55,8 s / 56,5 s**   | **240** (216× AUTH_EXPIRE, 24× AUTH_FAIL) | jeder Start: Pin auf den Router-BSSID, 9 Fehlversuche, der 10. (Treiber-Reconnect) trifft |

Der A0-Verlauf eines Starts, verkürzt:

```
[WIFI]...SSID: ORBI63 CHAN: 3 RSSI: -70 BSSID: 5A:AF:97:2E:2B:8B
[WIFI]...SSID: ORBI63 CHAN: 3 RSSI: -84 BSSID: 46:AF:97:2E:2B:86
[WIFI]...connecting to CHAN: 3 BSSID: 5A:AF:97:2E:2B:8B        <- Pin
[WIFI];event;disconnected;reason;2;ms;13623                    <- AUTH_EXPIRE
[WIFI];event;disconnected;reason;2;ms;18143
[WIFI]...no connection at boot — full radio reset and retrying <- Funk-Reset (Selbstbestrafung)
[WIFI];event;disconnected;reason;202;ms;23236                  <- AUTH_FAIL
[BOOT];ready;ms;23268;ip;0                                     <- Boot "fertig", offline
... 6 weitere AUTH_EXPIRE, 4,5 s Takt ...
[WIFI]...SET but no Wifi connect ...please wait for next try (5 min)  <- Firmware gibt auf
[WIFI];event;connected;ms;55419                                <- Treiber schafft es allein
[WIFI];event;got_ip;ms;55938                                   <- ... und niemand wertet es aus
```

Die letzten zwei Zeilen sind der eigentliche Skandal: **die Verbindung stand ab Sekunde 56**, aber
`startMeshComUDP()` lief erst im 5-Minuten-Pfad — und der begann mit `WiFi.disconnect(true,true)`.

---

## 5. Die Ursachenkette im Detail

### 5.1 Der BSSID-Pin entwertet den Treiber

`wifiBeginFromScan()` rief `WiFi.begin(ssid, pwd, channel, bssid, true)`. Damit setzt arduino-esp32
`bssid_set = 1`; unter dem Standard `WIFI_FAST_SCAN` probiert der Treiber genau diesen einen AP auf
genau diesem Kanal. Antwortet der nicht (oder nicht rechtzeitig), gibt es keinen zweiten Kandidaten.
`failure_retry_cnt` ("nach N Fehlern den nächsten AP") wirkt nur unter `WIFI_ALL_CHANNEL_SCAN` —
also nie. Der Treiber-Reconnect (`_autoReconnect = true` ist Arduino-Standard, die Firmware hat ihn
nie abgeschaltet) versuchte es zwar alle 4,5 s wieder — **mit derselben gepinnten Konfiguration**.
TM-24 hatte "kein Reconnect" vermutet; richtig ist: Reconnect ja, aber ohne Wahlfreiheit.

### 5.2 SAE braucht PMF — und der Pin bringt SAE zu Fall

Der IDF-4.4.7-Treiber hat `CONFIG_ESP32_WIFI_ENABLE_WPA3_SAE=y`; bietet der AP SAE an, wird SAE
versucht. `threshold.authmode = WPA2_PSK` ist ein **Minimum**, keine Obergrenze — deshalb hat
`setMinSecurity()` nichts bewirkt. Die Kombination Pin + SAE lief in **0 von 24** Starts durch
(`AUTH_EXPIRE` = die Station bekam auf ihren SAE-Commit keine Antwort). Ohne Pin (A4p0) läuft SAE
durch, aber **exakt jeder zweite Start braucht einen stillen zweiten SAE-Anlauf** (+4,5 s, kein
Disconnect-Ereignis, `got_ip` 13,6–14,7 s vs. 18,2–19,7 s im Wechsel).

SAE setzt Protected Management Frames voraus. `esp_wifi_disable_pmf_config(WIFI_IF_STA)` (in
`esp_wifi.h`, seit IDF 4.4) nimmt PMF aus der Konfiguration; die Station handelt dann WPA2-PSK aus.
Nachweisbar über zwei interne Symbole aus `libnet80211.a`, die es in keinem öffentlichen Header gibt:
`esp_wifi_sta_pmf_enabled()` und `esp_wifi_sta_prof_is_wpa3_internal()` — geloggt als `pmf;N;wpa3;N`
in jeder `[WIFI];assoc`-Zeile. `wifi_ap_record_t.authmode` (7 = WPA2/WPA3) sagt dagegen nur, was der
AP anbietet, nicht, was ausgehandelt wurde.

### 5.3 Drei Besitzer des Wiederverbindens, und ein blindes Fenster

| Besitzer              | Auslöser                                 | Aktion                                                  |
| --------------------- | ---------------------------------------- | ------------------------------------------------------- |
| arduino-esp32         | jedes "reconnectable" `STA_DISCONNECTED` | `disconnect(); begin();` — gepinnte Konfiguration       |
| `checkWifiPing()`     | 5 Schläge à 5 s ohne `WL_CONNECTED`      | `disconnect(true,true)` + `startNetwork()` (Funk-Reset) |
| 5-Minuten-`web_timer` | `!hasIPaddress && iWlanWait == 0`        | `stopWebserver()` + `startNetwork()`                    |

Nach dem Boot-Aufgeben (`iWlanWait = 0; bAllStarted = true`) lief `doWiFiConnect()` nie mehr — der
einzige Ort, der `startMeshComUDP()` aufrief. Ein späterer Treiber-Erfolg blieb unbeachtet, bis der
5-Minuten-Pfad ihn zerstörte. Live gesehen am Heltec (`gwflood_gateway_20260829-231216.log`):
`got_ip` bei 53 s, keine UDP-Aktivität bis zum 5-Minuten-Neustart.

### 5.4 `hostByName()`: bis zu 31 s Stillstand

`startMeshComUDP()` löste Server- und NTP-Name synchron auf `loopTask` auf: 16 s Warten auf
`WIFI_DNS_IDLE_BIT` plus 15 s auf `WIFI_DNS_DONE_BIT` im schlechten Fall (Captive Portal, HAMNET ohne
Resolver, Router noch am Booten). LoRa-Empfang, Serial, BLE stehen so lange. Unsichtbar, weil kein
Log und nur ein aggregiertes `max_us` im Instrument.

### 5.5 Der vierte Fallstrick, selbst gebaut und selbst gefunden

Der erste Wurf des 60-s-`[WIFI];link`-Heartbeats rief `WiFi.getMode()` in jedem Schleifendurchlauf.
`esp_wifi_get_mode()` wartet auf den WiFi-Task — und der scannt gerade. Ergebnis: **2,9 s Loop-Lücke
während des Aufbau-Scans und 1,1 s während des Connect-Scans, in jedem Start** (`[INSTR-LOOP];gap`).
Regel seitdem: **kein `esp_wifi_*`/`WiFi.getMode()` aus der Hauptschleife, solange der Treiber
scannen könnte**; Zustand kommt aus `STA_START`/`STA_STOP`-Ereignissen. Das ist genau die Klasse
Fehler, die "der Knoten hängt manchmal beim Start" erzeugt — und ohne den Gap-Reporter aus TM-13 wäre
sie geblieben.

---

## 6. Die Änderung (alle ESP32-Boards; `udp_functions.cpp`, `esp32_main.cpp`, `command_functions.cpp`)

| ID  | Änderung                                                                                                                                                                                                                                                                                                                                      |
| --- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| F1  | Einmal vor dem ersten `WiFi.mode()`: `persistent(false)`, `setAutoReconnect(true)`, `setScanMethod(WIFI_ALL_CHANNEL_SCAN)`, `setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL)`                                                                                                                                                                        |
| F2  | `WiFi.begin(ssid, pwd)` ohne Kanal/BSSID; der eigene Scan bleibt als AP-Liste im Log, geht aber nicht mehr in `begin()`; `BENCH_WIFI_NO_BSSID` gelöscht                                                                                                                                                                                       |
| SAE | `WiFi.begin(…, connect=false)` → `esp_wifi_disable_pmf_config(WIFI_IF_STA)` → `esp_wifi_connect()`; `WIFI_SAE_POLICY` 0 = Treiber-Standard, **1 = PMF aus (Standard)**, 2 = SAE H2E+HnP                                                                                                                                                       |
| F3  | `got_ip` setzt ein Flag im Ereignis-Task; `wifiHarvestGotIp()` in der Hauptschleife ruft `startMeshComUDP()` — auch nach dem 20-s-Pollfenster, auch nach jedem späteren Reconnect. Kein Funk-Reset beim Boot. 5-Minuten-Pfad nur bei `wifiTrulyOffline()` (Treiber 60 s ohne Ereignis oder 15 min ohne IP). TM-17 (`bAllStarted`) eingefaltet |
| F4  | `checkWifiPing()`: 180 s Schonfrist (der Treiber verbindet selbst), dann `disconnect(false,false)+begin()` (AP-Neuwahl bei stehendem Funk), nach 360 s Funk-Reset. Absichtlich abgeschalteter Funk bleibt aus                                                                                                                                 |
| F5  | `[WIFI];stall;<site>;ms;N;task;X` ab 50 ms an `mode`, `disconnect`, `scan`, `begin`, `dns`                                                                                                                                                                                                                                                    |
| F6  | DNS über lwIP `dns_gethostbyname()` mit Callback (dieselbe Funktion, die `hostByName()` intern benutzt, nur ohne das Warten): Literal-IP sofort, einmal pro Boot, Wiederholung aus dem Heartbeat; KEEP/UDP-TX erst mit aufgelöster Adresse                                                                                                    |
| F7  | Toter `startNetwork()`-Zweig in `sendMeshComHeartbeat()` entfernt                                                                                                                                                                                                                                                                             |
| Log | `[WIFI];assoc;<was>;ssid;bssid;chan;rssi;auth;phy;pmf;wpa3;reason;ms` bei connected/got_ip/disconnected; `[WIFI];link` alle 60 s; `--wifistat`; `--wifidrop` (Fork-Haken: Trennen + Neuwahl); `--wifi on/off` (HL-01); `node_wifion` lädt mit `true` (HL-02)                                                                                  |

Nicht geändert, bewusst: Scan-Verweildauer, RSSI-Untergrenze, `rm_enabled`/`btm_enabled` (würden
11k/v ankündigen, das die Bibliothek nicht kann), Modem-Sleep, IDF-Version.

---

## 7. Nachher — die Messungen

Gleicher Runner, gleiches WLAN `ORBI63` (WPA2/WPA3), gleiche Tischposition.

| Arm                                           | Board     | Erste Anmeldung                                 | `got_ip` Median / Max | Abbrüche | `[WIFI];stall` |
| --------------------------------------------- | --------- | ----------------------------------------------- | --------------------- | -------- | -------------- |
| A0 vorher                                     | T-Deck    | 0/24                                            | 55,8 s / 56,5 s       | 240      | —              |
| **A4p1** alle Fixes, WPA2-PSK erzwungen       | T-Deck    | **24/24**                                       | **14,2 s / 14,7 s**   | **0**    | 0              |
| A4p1                                          | Heltec V3 | **12/12**                                       | 11,2 s / 11,5 s       | 0        | 0              |
| A4p1                                          | T-Beam    | **12/12**                                       | 10,6 s / 11,2 s       | 0        | 0              |
| A4p0 gleicher Code, SAE beim Treiber belassen | T-Deck    | 24/24 mit IP bis 19,7 s; 12/24 im ersten Anlauf | 16,4 s / 19,7 s       | 0        | 0              |
| **A4p1b** nach dem `getMode()`-Fix (5.5)      | T-Deck    | **24/24**                                       | **14,1 s / 14,6 s**   | **0**    | 0              |

Jede Anmeldung trägt `pmf;0;wpa3;0` (WPA2-PSK ausgehandelt) bzw. in A4p0 `pmf;1;wpa3;1`.
`--wifidrop` (Trennen + Neuwahl durch den Treiber): 4,0 s bis `got_ip`, Socket bleibt (`same_ip`).
DNS asynchron: 22–93 ms statt bis zu 31 s Blockade. Erster Anmeldeversuch am Heltec in einer
Zeile:

```
[WIFI]...try connecting to SSID: ORBI63
[WIFI];policy;pmf_off;rc;0
[WIFI];event;connected;ms;10754
[WIFI];assoc;connected;ssid;ORBI63;bssid;5A:AF:97:2E:2B:8B;chan;3;rssi;-47;auth;7;phy;bgn;pmf;0;wpa3;0;live;1;reason;0;ms;10754
[WIFI];event;got_ip;ms;11352
[WIFI]...now listening at IP 192.168.68.66, UDP port 1990
[WIFI];dns;meshcom.oevsv.at;ip;89.185.97.38;ms;80
[KEEP]...KEEP433A8968DK5EN-93 4.35p9999;
```

Die 14 s beim T-Deck setzen sich zusammen aus ~5 s bis `CLIENT STARTED`, ~5 s Diagnose-Scan (13
Kanäle × 300 ms, nur noch fürs Log) und ~4 s Treiber-Scan + Anmeldung. Der Diagnose-Scan ist der
Hebel, falls die 14 s stören; er wurde bewusst behalten, weil er im Feldbericht zeigt, welche APs
hörbar waren.

Regressionsgates dazu: native Tests 60/60, T-Deck-Harness 15/15, OLED-Harness Heltec 8/8,
RAK-Harness boot/info/instr/mheard, alle vier Boards gebaut.

### 7.1 Langzeit-Soak TM-36 (Nachtlauf 2026-08-30/31) — bestanden

Der abschließende Soak lief 9,1 h auf allen drei Boards gleichzeitig, mit einem erzwungenen
`--wifidrop` alle 600 s (Details und Tabelle: [`wifi-soak-report-20260831.md`](wifi-soak-report-20260831.md)):

- **165 von 165 Trennungen ohne Eingriff wiederverbunden** (55 je Board), Reconnect-Median
  4,0–4,5 s, Maximum 5,3 s — deckungsgleich mit den 4,0 s der Einzelmessung oben.
- 0 unaufgeforderte Abbrüche, 0 Watchdog-Aktionen, 0 `[WIFI];stall`, 0 unerwartete Reboots,
  jede Wiederverbindung behielt ihre IP; jede Anmeldung WPA2-PSK (`pmf;0;wpa3;0`).
- BSSID-Neuwahl zwischen Router und Mesh-Satellit funktioniert nach jedem Drop wie entworfen
  (T-Deck 14, T-Beam 13, Heltec 6 Wechsel — der Heltec steht am nächsten am Router).

Damit sind alle Messlatten aus Backlog-Zeile TM-36 erfüllt; der WLAN-Umbau aus diesem Bericht
gilt als langzeit-verifiziert. **Nebenbefund** (vom WLAN unabhängig): NTP bekam die ganze Nacht
keine einzige Antwort — die Antwort wird seit TM-35 nur im Gateway-Block des Hauptloops
eingesammelt, Nicht-Gateway-Knoten ohne GPS haben damit nie eine gültige Uhr. Als **TM-45** im
Backlog erfasst; Analyse im Soak-Bericht.

---

## 8. Was das für Betreiber bedeutet

- **Router im WPA2/WPA3-Mischbetrieb sind ab jetzt normal**, kein Umstellen auf WPA2-only mehr nötig.
- **Reines WPA3 (SAE-only)** verbindet sich mit dem Standard (`WIFI_SAE_POLICY=1`) **nicht** — das ist
  der bewusste Preis für Determinismus. Wer so ein WLAN hat, baut mit `-D WIFI_SAE_POLICY=0`; damit
  gelingt die Anmeldung, jeder zweite Start braucht 4,5 s länger.
- Mesh-WLANs mit mehreren APs: der Treiber wählt bei **jedem** Verbindungsversuch den stärksten AP neu.
  Kein 802.11k/v/r — das kann die Bibliothek nicht; ein Wechsel im laufenden Betrieb passiert erst,
  wenn der AP uns loslässt.
- Ein `[WIFI]`-Feldbericht enthält jetzt: welche APs hörbar waren, mit welchem BSSID/Kanal/RSSI
  verbunden wurde, welcher Sicherheitsmodus ausgehandelt wurde, jeden Abbruchgrund, jede Blockade über
  50 ms mit Ort und Task. `--wifistat` liefert das auf Zuruf.

---

## 9. Reproduzieren

```
cd tools/bench/runs
python3 ../experiments/bootloop.py --arm <name> --boots 24 --port /dev/cu.usbmodem1101
python3 ../experiments/bootloop.py --parse-only "bootloop_<name>_*/boot_*.log"
```

Arme über Build-Flags: `PLATFORMIO_BUILD_FLAGS="-D WIFI_SAE_POLICY=0" pio run -e t_deck_plus`.
Marker-Regeln: rohes `Serial.printf` (`printfdeb` entfernt `;` außerhalb `--debug csv`); der Heltec
verliert beim Port-Öffnen das ROM-Banner, `CLIENT SETUP` gilt als Reset-Marke; ein 24-Start-Arm
braucht ~31 min, Arme derselben Frage in dieselbe Stunde legen.

Langzeit: `tools/bench/experiments/wifisoak.py` (gehaltene Sitzungen, `--wifidrop` alle 10 min,
Ereignis-CSV, Reconnect-Verteilung) — Backlog TM-36, Auswertung nach den übrigen Backlog-Punkten.

---

## 10. Lehren, die über WLAN hinausgehen

1. **Ein Treiber-Feature nachzubauen ist fast immer schlechter als es einzuschalten** — hier war es
   ein Scan + Sortierung + Pin, der genau das Feature abgeschaltet hat, das den Fall gelöst hätte.
2. **"Reconnect fehlt" war falsch, "Reconnect ohne Wahlfreiheit" war richtig.** Vor dem Fixen den
   Standardwert der Bibliothek lesen, nicht die eigene Annahme.
3. **Mindest-Sicherheit ist kein Höchst-Sicherheit.** `setMinSecurity(WPA2)` hat SAE nie verhindert.
4. **Ereignis statt Zeitfenster.** Jedes "nach N Sekunden aufgeben" erzeugt ein blindes Fenster, in
   dem der Erfolg unbemerkt bleibt.
5. **Jeder synchrone Bibliotheksaufruf in der Hauptschleife ist ein Verdächtiger**, auch ein harmlos
   aussehender Getter — messen, nicht annehmen.
6. **Arme, nicht Anekdoten.** Zwölf Starts pro Hypothese, gleiche Stunde, gleicher Runner, und die
   Treiber-Ereignisse im Log. Ohne A5 (WPA2-only-SSID) wäre SAE nie vom Band-Steering zu trennen
   gewesen; ohne A4p0 hätte man "SAE ist kaputt" statt "Pin + SAE ist kaputt" aufgeschrieben.
