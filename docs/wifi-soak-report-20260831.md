# WLAN-Soak-Bericht TM-36 — Nachtlauf 2026-08-30/31

Abschlussmessung zum WLAN-Umbau aus Wave W ([`wifi-report-20260830.md`](wifi-report-20260830.md)).
Rohdaten: `tools/bench/runs/wifisoak_night_20260830-224246/` (`summary.txt`, je Board
`*_events.csv`; Reduktion mit `wifisoak.py --parse-only`). Build `7b65233a` auf allen drei Boards.

## Ergebnis in einem Satz

**Der WLAN-Soak ist bestanden** — 9,1 h, 55 erzwungene Trennungen pro Board, alle 165 ohne
Eingriff wiederverbunden, kein Absturz, kein Watchdog, kein Stall, kein unerwarteter Reboot;
**dabei fiel ein neuer, vom WLAN unabhängiger Defekt auf: NTP bekommt auf Nicht-Gateway-Knoten
nie eine Antwort** (als TM-45 im Backlog, Details unten).

## Aufbau

- 2026-08-30 22:42 bis 2026-08-31 07:48 (9,1 h), T-Deck Plus, Heltec V3, T-Beam v1.2, je eine
  offen gehaltene USB-Session (`wifisoak.py`, detached).
- Alle 600 s ein `--wifidrop` (Trennung treiberseitig, AP bleibt an) — der Treiber muss allein
  zurückkommen.
- **GPS auf allen drei Boards aus (persistiert)**, damit NTP die einzige Uhr ist — der erste
  Soak, der NTP wirklich fordert. Gateway aus (Normalzustand der Bench-Knoten).
- WLAN `ORBI63` (WPA2/WPA3-Mischbetrieb, Mesh: Router `5A:…:8B` + Satellit `46:…:86`, beide
  Kanal 3).

## WLAN-Messwerte

| Board     | Drops erholt | Reconnect Median / p90 / Max   | Unaufgeforderte Abbrüche | BSSID-Wechsel | Watchdog | `[WIFI];stall` | RSSI min/med/max |
| --------- | ------------ | ------------------------------ | ------------------------ | ------------- | -------- | -------------- | ---------------- |
| T-Deck    | 55/55        | 4 459 ms / 5 037 ms / 5 329 ms | 0                        | 14            | 0        | 0              | −83/−75/−67      |
| Heltec V3 | 55/55        | 4 016 ms / 4 086 ms / 4 155 ms | 0                        | 6             | 0        | 0              | −78/−56/−49      |
| T-Beam    | 55/55        | 4 032 ms / 5 002 ms / 5 221 ms | 0                        | 13            | 0        | 0              | −85/−58/−54      |

- Die je 1 „reset" bei T-Deck/T-Beam sind der bekannte Reboot beim Port-Öffnen zu Soak-Beginn
  (22:42:46, Zeile 3 des Logs) — kein Reboot während des Laufs. Heltec: 0.
- Jede der 165 Wiederverbindungen behielt ihre IP (`same_ip` 55/55/55); jede Anmeldung
  `auth=7 pmf=0 wpa3=0` = WPA2-PSK, wie seit Wave W beabsichtigt.
- Die BSSID-Wechsel sind Neuwahlen zwischen den beiden Orbi-Funkzellen nach einem Drop
  (Treiber wählt nach Signal — gewollt); sie erklären auch die etwas höheren p90/Max bei
  T-Deck/T-Beam gegenüber dem Heltec, der fast immer beim näheren Router blieb.
- DNS asynchron: je 2 Auflösungen, max. 67/96/114 ms.
- Vergleich Wave-W-Baseline (51-min-Fragment vom 2026-08-30 vormittags, 5 Drops): Median
  3 989 / p90 4 123 / max 4 123 ms — der Nachtlauf liegt gleichauf; der längere Schwanz kommt
  aus den Roams zum schwächeren Satelliten.

**Messlatte aus TM-36:** 0 unerwartete Reboots, jeder Drop erholt, kein Stall > 500 ms,
Watchdog nie Stufe 2, Laufzeit > 6 h — **alle erfüllt. TM-36 damit positiv abgeschlossen.**

## Nebenbefund NTP: 0 Antworten in 9,1 h → TM-45

Erster Soak mit GPS aus, also mit NTP als einziger Uhr — und NTP hat **kein einziges Mal**
funktioniert:

- `[NTP];ok`: **0** auf allen drei Boards; `[NTP];timeout` 545–548 pro Board, im 60-s-Takt
  (Backoff) die ganze Nacht; 2–3 `[NTP];txfail` pro Board, alle exakt in Drop-Fenstern
  (Senden ohne Link — erwartbar).
- Ursache im Code, nicht im Netz: Seit dem TM-35-Umbau wird die NTP-Antwort vom normalen
  UDP-Empfangspfad eingesammelt (`getMeshComUDP()` → `tryConsume()`,
  `udp_functions.cpp:172`) — dieser Pfad läuft aber nur im Gateway-Block
  (`if(bGATEWAY && …)`, `esp32_main.cpp:3708`; nRF52 identisch gegated,
  `nrf52_main.cpp:1963`). Die Anfrage geht raus (Senden ist nicht gegated), die Antwort
  liegt ungelesen im Socket, jeder Versuch läuft in den 2,5-s-Timeout.
- Folge im Feld: ein Knoten mit WLAN, aber **ohne GPS-Fix und ohne Gateway-Flag hat nie eine
  gültige Uhrzeit** (vorher las das blockierende `NTPClient::forceUpdate()` den Socket
  selbst) — und schickt dauerhaft alle 60 s ein NTP-Paket ins Leere.
- Das WLAN-Urteil bleibt davon unberührt: Link, Reconnect und Sockets waren die ganze Nacht
  gesund, nur der Konsument der Antwort fehlt.

## Zustand nach dem Lauf

GPS auf allen drei Boards wieder eingeschaltet und per `--pos` verifiziert (Fix vorhanden:
T-Deck, Heltec sat:7, T-Beam sat:8).

---

## Nachtrag: Nachtlauf 2026-08-31/09-01 — realer Router-Reboot, drei Plattformen

Zweiter Soak in Folge, diesmal mit **realem Router-Neustart** statt erzwungener
Treiber-Drops, und erstmals mit Gateway- und Ethernet-Knoten im Bild. Build
`v4.35p.08.31.4-stability` (BP-02…06) auf allen dreien. Laufzeit 22:00–07:58 (~10 h).
Rohdaten: `~/Downloads/dk5en-9?-soak-20260901/` und `meshcom_monitor/meshcom_2026-08-31_22*.log`.

### Aufbau

| Node                 | Rolle                 | Kanal                           | Werkzeug                      |
| -------------------- | --------------------- | ------------------------------- | ----------------------------- |
| DK5EN-98 (Heltec V3) | Gateway, WLAN         | Netconsole 2323                 | `tools/meshlogger.py` (nohup) |
| DK5EN-90 (RAK4631)   | Node, W5100S-Ethernet | Serial USB (CDC-ACM)            | `tools/serial_monitor.py`     |
| DK5EN-93 (Heltec V3) | Node, WLAN            | Serial USB (CP2102, `--no-dtr`) | `tools/serial_monitor.py`     |

Der Router-Reboot (Orbi, geplant) kam um **05:35:28**.

### Ergebnis in einem Satz

**Bestanden auf allen drei Plattformen** — ein realer AP-Ausfall von 96 s wurde ohne
Eingriff, ohne Reboot, ohne Absturz und ohne Watchdog überstanden; das Gateway blieb
aus Server-Sicht lückenlos verfügbar (mcmap: 100 % Uptime, 0 Events — 96 s liegen
unter der 3-min-Stille-Schwelle des Hubs).

### WLAN-Sicht (DK5EN-93, Serial lückenlos)

- 05:35:28 `reason 200` (Beacon-Timeout), dann 92 s lang `reason 201` (AP weg) im
  3,4-s-Takt — der Treiber pollte selbstständig weiter.
- 05:37:03 Reconnect auf **anderen BSSID und Kanal** (Orbi-Mesh 3→8), `got_ip` eine
  Sekunde später, DNS neu aufgelöst, **NTP-Resync 3 s nach `got_ip`** (TM-45-Fix
  bestätigt sich erneut unter realen Bedingungen).
- Ganze Nacht: exakt ein Disconnect-Ereignis, 17 NTP-Syncs (adaptives Intervall bis
  2,3 h), 0 unaufgeforderte Abbrüche, 0 Reboots.

### Ethernet-Sicht (DK5EN-90, W5100S)

- Stündliche DHCP-Renews (10×) fehlerfrei; beim Router-Boot 3 kurze Link-Downs,
  danach Renew mit **neuer IP** — 0 Stack-Resets, 0 `tx_fail`, Gateway-Heartbeat
  nahtlos wieder da.

### Backpressure-Beobachtung (passiv)

- **0 `[BP]`-Marker auf allen dreien.** Einschränkung ehrlich benannt: über Nacht gab
  es keine lokalen User-Sends, die die Statemaschine füttern — die BP-05-Baseline-
  Validierung bleibt die 3,5-min-Abendmessung. Belastbar ist: die 98 erreichte durch
  Relay-Bursts **Tiefe bis 10/20** ohne jede Meldung.
- **BP-03 im Feld nachgewiesen: 13× `RING_DROP_STALE` auf der 98**, alle bei
  `age_s` 180–181 — ausgehungerte HEY-Relays altern auf die Sekunde genau aus dem
  Ring, statt den Lesezeiger zu pinnen (der DJ8MEH-Mechanismus tritt damit unter
  Live-Last nicht mehr auf).

### Zwei Werkzeug-Befunde

1. **meshlogger hält Zombie-TCP** (als TM-50 im Backlog): nach dem WLAN-Ausfall des
   Zielknotens blieb die 2323-Verbindung halb offen — 2,4 h Lücke (05:35–07:59),
   `reconnects=0`, Broken pipe erst beim Flag-Restore; das `--loradebug` der 98 wurde
   dadurch nicht restauriert (am Morgen manuell nachgeholt). Fix: TCP-Keepalive oder
   Read-Timeout + Reconnect. Die Serial-Redundanz hat den Datenverlust komplett
   aufgefangen — genau dafür war sie aufgesetzt.
2. **CP2102-Korrektur:** Der Port-Open auf der 93 rebootet das Board **doch** (Boot
   bei `ms=2900` im Log belegt; der `rst:`-Banner läuft vor dem Reader-Attach durch
   und entgeht deshalb dem Banner-Grep). `serial_monitor.py --no-dtr` verhindert den
   Reset nicht — die Bench-Regel „CP2102-Open = Reboot einplanen" bleibt gültig.

### Zustand nach dem Lauf

`--loradebug off` auf 90 (Serial), 93 und 98 (jeweils 2323, um den CP2102-Reset zu
vermeiden) wiederhergestellt; alle drei Aufzeichnungsprozesse sauber per `timeout`
beendet.
