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
