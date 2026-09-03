# BLE-Review — Bestandsaufnahme vor dem Abgleich mit dem App-Quellcode

**Stand:** 2026-09-02
**Zweck:** festhalten, was über die BLE-Strecke Knoten ↔ Telefon heute belegt ist, was offen ist,
was der neu verfügbare App-Quellcode davon schließen kann, und wie eine Ende-zu-Ende-Regression
aussehen müsste.
**Abgrenzung:** dieses Dokument ist reine Bestandsaufnahme. Es wurde nichts gebaut, nichts geflasht
und keine Zeile Firmware geändert. Der App-Quellcode ist geklont, aber **noch nicht gelesen** —
alle Aussagen über die App sind als offene Fragen formuliert, nicht als Befunde.

---

## 1. Auslöser

Zwei Fragen des Betreibers:

1. Hilft es, wenn die iOS-App als Quellcode vorliegt?
2. Wie lässt sich die BLE-Thematik Ende zu Ende regressionstesten — ergibt das vom MacBook aus Sinn,
   oder genügt die App, um daraus das Wire-Protokoll mit allen Defekten zu erkennen?

Nachgeschoben: kann der Raspberry Pi Zero 2 W Bluetooth sniffen?

Die Kurzantworten stehen in §5, §6 und §7. Der Rest ist die Belegkette.

---

## 2. Quellenlage

### 2.1 Firmware-Baum

| Dokument                                                                         | Inhalt                                                                                                                                                                                  |
| -------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [`architecture/11-wire-format.md`](architecture/11-wire-format.md) §4            | das BLE-Telefonprotokoll: GATT, Hello/PIN, Config-Burst, Rahmentypen, Größenklemmen, Phone→Node-Kommandos. **Selbst als „reverse-engineered, nicht upstream-normativ" gekennzeichnet.** |
| [`issue-ble-i-register-mtu-20260828.md`](issue-ble-i-register-mtu-20260828.md)   | `I`-Register sprengte die 245-Byte-Klemme, App bekam kaputtes JSON. Enthält den MTU-Nebenbefund (§4 hier).                                                                              |
| [`issue-mh-json-size-budget-20260828.md`](issue-mh-json-size-budget-20260828.md) | MH-Register `PP`/`SRC`/`GW`, Größenbudget                                                                                                                                               |
| [`report-ble-tx-latency.md`](report-ble-tx-latency.md)                           | BLE→LoRa-Latenzkette, 117 Samples über drei Knoten                                                                                                                                      |
| `test/test_ble_json_frame/`                                                      | einziger nativer BLE-Test heute: Puffergrenze des JSON-Rahmenbauers gegen Kanarienvogel-Bytes                                                                                           |

### 2.2 Unabhängige Zweitimplementierung: MCProxy

Wichtig für den Testaufbau, weil die Dekodierschicht bereits existiert und feldgetestet ist:

| Datei                                     | Zeilen | Eigenschaft                                                                               |
| ----------------------------------------- | -----: | ----------------------------------------------------------------------------------------- |
| `MCProxy/src/mcapp/ble_protocol.py`       |    897 | **transportfrei** — nur stdlib plus zwei lokale Module. Dekoder für `@`-Rahmen, FCS, APRS |
| `MCProxy/src/mcapp/ble_protocol_tests.py` |   1182 | Golden-Byte-Frames, keine Hardware nötig                                                  |
| `MCProxy/src/mcapp/ble_client.py`         |      — | **abstrakte Basisklasse** `BLEClientBase` mit `scan`/`connect`/`pair`/`send_message`/…    |
| `MCProxy/ble_service/src/ble_adapter.py`  |   2260 | konkrete Implementierung über `dbus_next` → **BlueZ, damit Linux-only**                   |

Der Zuschnitt ist günstig: die Transportabstraktion steht schon, nur die BlueZ-Implementierung ist
plattformgebunden.

### 2.3 Neu: der App-Quellcode

Geklont nach `/Users/martinwerner/WebDev/Meshcom-MobileApp` (außerhalb dieses Repos).

```
Quelle:       https://github.com/rainerfritz/Meshcom-MobileApp
Stand:        6e7f2e5, 2026-09-01, "show new message count"
Commits:      33, Branch main, 36 MB
Stack:        Ionic React 8.8.5 + Capacitor 8.3.1 + Vite + TypeScript
BLE-Schicht:  @capacitor-community/bluetooth-le 8.3.0
Ziele:        @capacitor/ios 8.3.1 und @capacitor/android 8.3.1
Lizenz:       keine LICENSE-Datei im Repo
```

Zwei Konsequenzen, die sofort feststehen:

- **Eine Codebasis deckt iOS und Android ab.** Der Abgleich beantwortet damit beide App-Plattformen
  auf einmal, nicht nur iOS.
- **Die BLE-Schicht ist ein Capacitor-Plugin, nicht direktes CoreBluetooth.** Was das Plugin an
  MTU-, Reconnect- und Notification-Verhalten durchreicht bzw. verschluckt, gehört mit zum
  Prüfumfang — der Vertrag entsteht aus App-Code **und** Plugin-Verhalten.

Ohne LICENSE-Datei ist die Rechtslage ungeklärt: **read-only-Referenz, nichts davon in dieses Repo
vendorn.** Erkenntnisse gehören als normative Sätze mit Commit-Angabe nach `11-wire-format.md` §4.

---

## 3. Offene BLE-Punkte

| ID                | Sache                                                                                              | Stand                                                                                     |
| ----------------- | -------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------- |
| `WF-01` Sites 1+2 | Pad-Bytes: beide Sender schreiben `blelen + 2`, die Zweige kopieren 1 / 2 / 3 Byte unterschiedlich | **geparkt** — Betreiberentscheidung 2026-08-30: Wire-Format, braucht Test gegen echte App |
| MTU-Blindheit     | Klemmen 245/251 sind geraten, ausgehandelter MTU wird nirgends abgefragt                           | **offen, ohne ID** — Nebenbefund aus dem `I`-Register-Issue, §4 hier                      |
| `N-07`            | BLE-Kommandokanal unauthentifiziert und ungegated                                                  | ACCEPTED / WONTFIX 2026-08-18 — Bonding würde die App-Flotte trennen                      |
| `CQ-01`           | BT-PIN im Klartext auf Serial und auf der unauthentifizierten 2323-Konsole                         | offen, High — der PIN ist das Einzige vor `N-07`                                          |
| `0x91`-Zweig      | MHeard-Binärzweig ohne Produzenten; dazu ein toter Textzweig in `sendComToPhone`                   | löschbar, sobald ein Konsument freigibt                                                   |
| Receipt v2        | DM-Store-and-Forward hängt an App-sichtbaren ACK-Semantiken (`ack_level` 0x00/0x01/0x02)           | Konzept steht, App-Vertrag fehlt                                                          |
| Stille Feldfehler | > 160 Zeichen wird verworfen; Ziel > 9 Zeichen macht aus der DM einen **Broadcast**                | Firmware setzt beides still durch, kein Fehler erreicht das Telefon                       |

Erledigt und hier nur zur Abgrenzung: das akute `I`-Register-Problem ist weg — upstream hat `FWDATE`
zurückgenommen (`grep FWDATE src/` findet nichts mehr), und `JSN-01` hat zusätzlich Fail-Soft mit
Feld-Drop eingezogen (`sendBleJsonRegister()` / `bleJsonFrameFailSoft()`,
`src/command_functions.cpp:122,126`). Die strukturelle Hälfte — §4 — steht unverändert.

---

## 4. Der MTU-Komplex — der strukturelle Kern

### 4.1 Befund

`grep -i mtu src/` findet MTU **ausschließlich in Kommentaren**:

```
src/phone_commands.cpp:66     // MAXIMUM PACKET Length over BLE is 245 (MTU=247 bytes), two get lost, …
src/phone_commands.cpp:164    // dito
src/web_functions/web_functions.cpp:1620  // dito
```

Die Zahlen im Code sind fest verdrahtet und stammen aus vier verschiedenen Quellen:

| Grenze                 | Wert | Herkunft                                                                         |
| ---------------------- | ---: | -------------------------------------------------------------------------------- |
| `0x44`-JSON-Register   |  245 | `BLE_JSON_PAYLOAD_MAX`, Klemme in `loop_functions.cpp` — die **bindende** Grenze |
| Datenpfad-Ringeinträge |  251 | `UDP_TX_BUF_SIZE − 4`                                                            |
| nRF52 ATT-MTU          |  250 | `Bluefruit.configPrphConn(250, …)`, `src/nrf52/nrf52_ble.cpp:90,92`              |
| ESP32 NimBLE ATT-MTU   |  255 | Voreinstellung                                                                   |

Der ausgehandelte Wert wird nie abgefragt. Die Firmware ist bereit, 245 Byte zu schreiben,
unabhängig davon, was das Gegenüber annehmen kann. Sie fragmentiert auch nicht: zu großer Inhalt
wird an der Quelle abgeschnitten, nicht aufgeteilt.

### 4.2 Warum das bisher nicht auffällt

Auf der Verbindung, auf der der `I`-Register-Fall gemessen wurde, stand im Log:

```
[INFO] ble_service.src.ble_adapter: Negotiated ATT MTU for this BLE connection: 255 bytes
```

255 Byte MTU heißt 252 Byte nutzbare Notification-Nutzlast — alles passt, der Defekt ist unsichtbar.
Das war BlueZ auf einem Pi. **Ein Gegenüber mit MTU 185 hätte 182 nutzbare Byte** und bekäme jeden
großen Rahmen abgeschnitten.

Genau hier liegt der blinde Fleck der bisherigen Teststrecke: der Pi ist unser Hauptkonsument, und
der Pi verhandelt groß.

### 4.3 Was daraus folgt

Sauber wäre, die Schreiblänge aus dem ausgehandelten MTU abzuleiten statt sie zu raten. Der erste
Schritt dahin ist kein Fix, sondern ein Messwert — siehe §8.1.

---

## 5. Was der App-Quellcode leisten kann

### 5.1 Er entscheidet Vertragsfragen, die heute geparkt sind

1. **`WF-01` wird entscheidbar.** Liest die App die deklarierte Länge oder scannt sie bis zum
   Rahmenende? Wenn Ersteres, ist das Pad wegwerfbar — eine Einzeile statt eines Flottenrisikos.
   Der einzige Blocker dieses Items ist heute wörtlich „braucht einen Bench-Test gegen eine echte App".
2. **Die Fail-Soft-Priorität bekommt eine Grundlage.** `bleJsonFrameFailSoft()` wirft Felder weg, wenn
   ein Register nicht passt. Welche Felder die App zwingend braucht (`CALL`, `ID`, `HWID`), steht
   nirgends außer in ihrem Parser. Ein Fail-Soft, der ausgerechnet die Knotenidentität opfert, ist
   schlimmer als ein Abschnitt.
3. **MTU-Wahrheit und Verhalten bei Abschnitt.** Was verhandelt das Plugin auf iOS und auf Android —
   und was tut die App mit einem unparsbaren Rahmen? MCProxy verwirft ihn sichtbar (55 Verwerfungen
   im Feldlog). Eine App, die still eine veraltete Identität behält, ist ein eigener Defekt.
4. **Hello- und Reconnect-Semantik.** Schickt die App bei jedem Reconnect ein neues Hello? Davon
   hängt ab, ob der Config-Burst plus `CONFFIN` überhaupt kommt — und daran hängt die Spezifikation
   eines Mock-Telefons wie eines Mock-Knotens.
5. **Vorvalidierung der stillen Grenzen.** Prüft die App die 160 Zeichen und `dst ≤ 9`? Wenn nein,
   ist „intendierte DM wird öffentlicher Broadcast" ein reproduzierbarer Feldfehler mit Klasse.
6. **`0x91` und der tote Textzweig.** Ein Konsument, der sie nachweislich nicht braucht, gibt die
   Löschung frei.
7. **`N-07`-Kostenschätzung.** Wie viel Bonding wirklich bricht, steht im Verbindungscode.
8. **Receipt v2.** Wie die App `ack_level` 0x00/0x01/0x02 anzeigt, entscheidet, ob sich
   DM-Store-and-Forward ohne Bruch der Flotte einführen lässt.

### 5.2 Was er nicht leisten kann

Laufzeit- und Robustheitsdefekte. Heap- und mbuf-Druck beim Verbindungsaufbau,
Notification-Ring-Überlauf während des Config-Bursts, Reconnect-Stürme, WiFi/BLE-Koexistenz,
tatsächliche MTU-Aushandlung im Betrieb.

Der Beleg dafür steht in der eigenen Historie: **`N-18`** — jeder BLE-Verbindungsaufbau war tot, weil
`Print::printf` bei fast jeder Logzeile mallociert und NimBLE (`MSYS1_BLOCK_COUNT=4`) danach keine
mbufs mehr fand. Der Knoten beantwortete `CONNECT_IND` nicht mehr, das Central sah `0x3e`
(`le-connection-abort-by-local`), auf dem Knoten feuerte weder `onConnect` noch `onDisconnect`.
Gefunden per Bisect auf Hardware gegen pristine `upstream/dev`. Aus keinem Quelltext ablesbar,
weder Firmware noch App.

### 5.3 Und eine Einschränkung

Eine App ist **ein** Konsument, nicht die Spezifikation. Für eine Wire-Änderung braucht es die
Vereinigung aus dieser App (iOS + Android), MCProxy, mc-chat-Softnodes, dem Web-UI und dem
zentralen Server. Der Quellcode verkleinert die Unsicherheit erheblich, er beseitigt sie nicht.

---

## 6. Testarchitektur — drei Stufen

### 6.1 Stufe 1 — nativ, ohne Hardware, CI-fähig

`test_ble_json_frame` existiert. Ausbauen zu einem Golden-Vector-Korpus:

- jedes `0x44`-Register (`I`, `SE`/`S1`, `SW`/`S2`, `SN`, `W`, `G`, `SA`, `IO`, `TM`, `AN`, `MH`,
  `CONFFIN`)
- `0x40`-Datenrahmen und der 13-Byte-`0x41`-ACK, inklusive Pad-Zählung und **Big-Endian**-Timestamp
- der Phone→Node-Parser: `0x10 0x20 0x50 0x55 0x70 0x80 0x90 0x95 0xA0 0xF0`, mit Längen-Fuzzing

Der letzte Punkt ist kein Selbstzweck: `SEC-03` (0x55 OOB-Read) und `BUG-07` (0xA0
Längen-Underflow) saßen genau dort. **Hier zahlt sich der App-Quellcode am stärksten aus** — er macht
aus dem Dokument §4 überprüfbare Vektoren.

### 6.2 Stufe 2 — MacBook spielt Telefon, echte Hardware

Ein `tools/blephone.py` auf `bleak`. Der Aufbau ist billiger als er klingt: `ble_protocol.py` ist
transportfrei, `ble_client.py` ist bereits eine ABC, und 1182 Zeilen Protokolltests liegen als Orakel
daneben. Es fehlt eine CoreBluetooth-Implementierung der ABC.

Der eigentliche Gewinn ist nicht die Sprache, sondern die Topologie: **BLE-Central und serielle bzw.
2323-Konsole desselben Knotens laufen auf einem Host, in einem Prozess, mit einer Uhr.** „Was der
Knoten zu schreiben glaubt" gegen „was der Host empfängt" wird ein Diff statt einer Korrelation über
zwei Maschinen. Die Bench-Flotte hängt ohnehin am MacBook.

Testfälle, die damit sofort laufen: Hello → Config-Burst → `CONFFIN` in der richtigen Reihenfolge,
jedes Register als valides JSON, Nachricht senden und ACK-Notification prüfen, `0x55`/`0x70`/`0x80`
schreiben und über die Konsole gegenprüfen, ausgehandelten MTU protokollieren.

### 6.3 Stufe 3 — echtes iPhone, manuell, selten

Nur für OS-Stack-Wahrheit und UI-sichtbare Semantik. Nicht automatisierbar. Als Checkliste vor jedem
Release halten, das die BLE-Wire anfasst.

### 6.4 Warum Mac und nicht Pi

Der Pi bleibt als zweiter Konsument in der Matrix wertvoll — er ist die Flottenrealität. Als
_primäres_ Testgerät ist er irreführend, weil BlueZ groß verhandelt (§4.2). Die Klasse „Firmware
schreibt mehr, als der Peer annehmen kann" ist auf dem Pi strukturell unsichtbar.

Der Mac ist der einzige Host in der Flotte mit CoreBluetooth, also derselben Stack-Familie wie iOS.
Ob er dieselbe Zahl aushandelt wie ein iPhone, ist **zu messen, nicht anzunehmen** — diese Messung
ist der erste Testfall.

---

## 7. Sniffing — was geht, was nicht

### 7.1 Drei Bedeutungen von „sniffen"

| #   | Bedeutung                                                                          | Pi Zero 2 W, Bordmittel |
| --- | ---------------------------------------------------------------------------------- | ----------------------- |
| 1   | eigene Verbindungen auf HCI/ATT-Ebene mitschneiden (`btmon` → btsnoop → Wireshark) | **ja, vollständig**     |
| 2   | fremde Verbindungen aus der Luft mitschneiden (iPhone ↔ Knoten)                    | **nein**                |
| 3   | als Host für einen USB-Sniffer dienen                                              | **ja**, OTG-Port frei   |

Zu (1): liefert kompletten ATT/GATT/SMP-Decode — MTU-Exchange, Connection-Parameter, jede
Notification mit Länge. Genau das, was für Pad-Bytes und Rahmenlängen zählt. Aber es ist die MTU
_des Pi_, nicht die des iPhones.

Zu (2): Um einer BLE-Verbindung zu folgen, braucht man Access Address, CRCInit, Hop-Increment und
Channel-Map aus dem `CONNECT_IND` und muss 37 Datenkanäle mitspringen. Der BCM43430A1 hat keinen
Promiscuous-Modus über HCI und keine Sniffer-Firmware.

_Nebenbei, weil es als Gegenargument kommt:_ InternalBlue unterstützt genau diesen Chip
(BCM43430A1 ist der Pi-3-/Zero-W-Chip) und öffnet den Broadcom-Diagnosemodus. Das gibt Link-Layer-Sicht
auf Verbindungen, an denen der Pi **selbst beteiligt** ist — kein allgemeiner Sniffer.
Forschungswerkzeug, keine Basis für eine Regressionsstrecke.

### 7.2 Befund `rpizero.local`, gelesen 2026-09-02

```
Raspberry Pi Zero 2 W Rev 1.0
hci0: Type: Primary  Bus: UART   B8:27:EB:ED:BD:25   DOWN
      ACL MTU: 1021:8  SCO MTU: 64:1
Firmware:  /lib/firmware/brcm/BCM43430A1.raspberrypi,model-zero-2-w.hcd
Werkzeuge: /usr/bin/btmon, /usr/bin/hcitool, /usr/bin/bluetoothctl, BlueZ 5.66
Dienste:   bluetooth.service inactive, hciuart.service inactive
lsusb:     nur Root-Hub — der OTG-Port ist frei
Kernel:    aarch64
```

Bluetooth ist auf diesem Pi also **aus**. Für Fall (1) müssten `hciuart` und `bluetooth` erst hoch.
Nichts davon wurde geändert.

Für Fall (1) ist ohnehin `mcapp.local` der bessere Host: der hält dauerhaft eine BLE-Verbindung zu
einem Knoten, dort gibt es ATT-Traces ohne neuen Aufbau.

### 7.3 Hardware für Fall (2) — die beiden Angebote

Betreiber-Screenshot vom 2026-09-02 08:27, amazon.de, Suche „nrf52840-dongle", 77 Ergebnisse,
Lieferung an 85354 Freising:

| Angebot                                                                      | Preis                                | Bewertung                                                                                                                                                                                                                               |
| ---------------------------------------------------------------------------- | ------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| „NRF52840-Dongle Nordic USB-Dongle für Eval Bluetooth-Paket-Erfassungs-Tool" | **29,04 €**, Prime, 11 auf Lager     | **Das ist der richtige.** Auf dem Platinenfoto steht `PCA10059` — die Nordic-Dongle-Referenz, die die nRF-Sniffer-Doku selbst nennt. Open Bootloader ab Werk, Flashen über nRF Connect for Desktop bzw. `nrfutil` **ohne Debugger**.    |
| „nRF52840 Bluetooth-USB-Dongle … EBYTE E104-BT5040U, 250 m"                  | 14,38 € (andere Angebote ab 11,07 €) | Halber Preis, aber ein Herstellermodul mit eigener Firmware und **ohne Nordic-Bootloader**. nRF Sniffer aufzuspielen setzt SWD-Zugang voraus (J-Link o. ä.). Vor einem Kauf verifizieren — sonst kostet die Ersparnis eine Debug-Probe. |

**Empfehlung: der PCA10059 für 29,04 €.** Die 15 € Differenz sind gegen das Risiko zu rechnen, ein
Modul zu bekommen, das sich nicht ohne Zusatzhardware umflashen lässt.

Ein Nebeneffekt, der uns hier entgegenkommt: die MeshCom-BLE-Strecke ist unverschlüsselt und
ungebondet (das ist `N-07`). Ein Sniffer sieht den Klartext, ohne die Pairing-Phase mitschneiden zu
müssen. Was sicherheitsseitig ein Defekt ist, macht die Diagnose einfach.

**Aber:** siehe §8 — für die heute offene Frage ist der Sniffer der teure Weg.

---

## 8. Billigere Instrumente zuerst

### 8.1 MTU-Instrument in der Firmware

Beide Plattformen können den ausgehandelten Wert melden. Im Baum verifiziert:

| Plattform | API                                                                 | Fundstelle                                 |
| --------- | ------------------------------------------------------------------- | ------------------------------------------ |
| ESP32     | `NimBLEServer::getPeerMTU(uint16_t connHandle)`                     | `NimBLEServer.h:75` (NimBLE-Arduino 2.2.3) |
| ESP32     | `NimBLEServerCallbacks::onMTUChange(uint16_t MTU, NimBLEConnInfo&)` | `NimBLEServer.h:169`                       |
| ESP32     | `NimBLEDevice::setMTU(uint16_t)`                                    | `NimBLEDevice.h:145`                       |
| nRF52     | `BLEConnection::getMtu()`                                           | `Bluefruit52Lib/src/BLEConnection.h:83`    |

Eine Logzeile im MTU-Callback, ausgegeben auf der 2323-Konsole, und der ausgehandelte MTU **jedes**
Peers ist bekannt — inklusive echter iPhones im Feld, ohne Sniffer, ohne Bench. Fünf Zeilen. Und für
den späteren Fix wird der Wert ohnehin gebraucht, weil die Schreiblänge aus ihm folgen soll statt
geraten zu werden.

Version pinnen: NimBLE-Arduino `2.2.3` laut `platformio.ini:503`.

### 8.2 Apple PacketLogger

Aus den Additional Tools for Xcode, mit dem Bluetooth-Logging-Profil auf dem Telefon: voller
HCI/ATT-Trace der iPhone-Verbindung. Keine Zusatzhardware, direkt die Zahl, die uns fehlt.

### 8.3 `btmon` auf `mcapp.local`

Bestehende Dauerverbindung, ATT-Traces ohne neuen Aufbau. Liefert die BlueZ-Sicht — als Referenz für
„so sieht es aus, wenn alles passt".

---

## 9. Fallstricke

- **macOS liefert keine MAC-Adresse**, nur eine hostspezifische CoreBluetooth-UUID.
  `BLEClientBase.connect(mac: str)` muss dafür aufgeweicht werden; Geräteauswahl über den
  Advertising-Namen `MC-<id>-<CALL>`.
- **macOS cached GATT-Tabellen aggressiv.** Nach einer Änderung an der GATT-Struktur Bluetooth
  togglen, sonst debuggt man einen Cache.
- **Kein Air-Sniffing auf dem Mac.** Für Link-Layer-Wahrheit braucht es den Dongle aus §7.3.
- **App-Quellcode nicht ins Firmware-Repo.** Keine LICENSE-Datei, Rechtslage ungeklärt. Erkenntnisse
  als normative Sätze mit Commit-Angabe nach `11-wire-format.md` §4.
- **Eine App ist kein Standard** (§5.3).
- **Der nRF Sniffer folgt genau einer Verbindung** und muss dafür das `CONNECT_IND` mitbekommen —
  Aufzeichnung vor dem Verbindungsaufbau starten.

---

## 10. Empfohlene Reihenfolge

| #   | Schritt                                                                                      | Aufwand      | Ergebnis                                         |
| --- | -------------------------------------------------------------------------------------------- | ------------ | ------------------------------------------------ |
| 1   | MTU-Instrument in die Firmware (§8.1), beide Plattformen                                     | Fünfzeiler   | wirkt sofort in der ganzen Flotte, auch im Feld  |
| 2   | App-Quellcode gegen `11-wire-format.md` §4 lesen; §5.1 Punkt für Punkt beantworten           | eine Session | `WF-01` entparkt, Fail-Soft-Priorität belegt     |
| 3   | Stufe 1 ausbauen (§6.1) — Golden Vectors aus den Antworten von (2)                           | eine Session | geht ohne Hardware in die CI                     |
| 4   | Stufe 2 bauen (§6.2) — bleak-Transport + MCProxy-Dekoder + 2323-Korrelation                  | eine Session | Ende-zu-Ende-Regression auf der Bench            |
| 5   | PacketLogger gegen ein echtes iPhone (§8.2), wenn die genaue iOS-Zahl zählt                  | Stunde       | iOS-MTU-Wahrheit                                 |
| 6   | nRF52840-Dongle PCA10059 (§7.3) — erst, wenn eine Frage auftaucht, die 1–5 nicht beantworten | 29 €         | z. B. Abbrüche, bei denen keine Seite mehr loggt |

Schritt 2 ist der eigentliche Grund für dieses Dokument: er ist der einzige, der geparkte
Entscheidungen löst, statt neue Messungen zu produzieren.

---

## 11. Belegstellen

Alles am 2026-09-02 gegen den Arbeitsbaum (`v4.35p_prio`) bzw. gegen die genannten Geräte geprüft.

**Firmware**

- `src/phone_commands.cpp:66,164` — MTU nur als Kommentar
- `src/web_functions/web_functions.cpp:1620` — derselbe Kommentar
- `src/nrf52/nrf52_ble.cpp:90,92` — `Bluefruit.configPrphConn(250, BLE_GAP_EVENT_LENGTH_MIN, 16, 16)`
- `src/command_functions.cpp:114,122,126` — `sendBleJsonRegister()` / `bleJsonFrameFailSoft()`, JSN-01
- `grep FWDATE src/` → keine Treffer (upstream-Revert)
- `grep -i mtu src/` → nur die drei Kommentare oben
- `test/test_ble_json_frame/` — einziger nativer BLE-Test

**Bibliotheken**

- `.pio/libdeps/*/NimBLE-Arduino` Version 2.2.3, `platformio.ini:503`
- `NimBLEServer.h:75,169`, `NimBLEDevice.h:145`
- `~/.platformio/packages/framework-arduinoadafruitnrf52/libraries/Bluefruit52Lib/src/BLEConnection.h:83`

**Backlog und Doku**

- `docs/BACKLOG.md:1273` (WF-01-Zeile), `:1403` (WF-01-Abschnitt), `:2727` (CQ-01)
- `docs/architecture/08-defect-catalogue.md:379` (N-07), `:1681` (WONTFIX-Zeile)
- `docs/architecture/11-wire-format.md` §4.1–4.4

**MCProxy**

- `src/mcapp/ble_protocol.py` (897 Z., stdlib-only), `ble_protocol_tests.py` (1182 Z.),
  `ble_client.py` (ABC), `ble_service/src/ble_adapter.py` (2260 Z., `dbus_next`)

**Geräte**

- `rpizero.local` — Ausgabe in §7.2, nur lesende Kommandos
- App-Repo — Metadaten in §2.3, `git log -1` und `package.json`, kein Quellcode gelesen
