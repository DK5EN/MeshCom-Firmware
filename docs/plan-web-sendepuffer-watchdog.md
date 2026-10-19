# Implementierungsplan: Web-Sendepuffer und Watchdog-Schutz für Path, MHeard und Kreuzmatrix

Stand 2026-10-19, Basis `fork-dev` `2ba37b10` (Release `v4.40a.10.19`). Bezug: `cc48f720` (Watchdog-Feed je
Zeile der Kreuzmatrix), Feld-Neustarts `DB0TAW-14` (TASK_WDT beim Seitenaufbau).

## BLUF

- Ein **Sendepuffer im gemeinsamen Web-Client** (`CommonWebClient`) sammelt die vielen kleinen
  `print()`-Aufrufe zu Blöcken von ca. 1400 B und schickt sie mit einer **eigenen, begrenzten Sendeschleife**
  ab. Die Schleife füttert den Watchdog, solange Daten abfließen, und bricht die Antwort ab, wenn der Browser
  2 s lang nichts annimmt oder die Seite insgesamt 15 s überschreitet. Damit ist **jede** Webseite geschützt,
  nicht nur die drei genannten.
- **Path** und **MHeard** bekommen zusätzlich einen Watchdog-Feed je Datensatz bzw. Karte (wie die Kreuzmatrix
  seit `cc48f720`), ebenso die übrigen Schleifen der Kreuzmatrix-Seite. Das schützt die Rechenteile, die nichts
  senden.
- Der Watchdog-Timeout bleibt bei 5 s. Eine Anhebung ist eine getrennte Entscheidung (Abschnitt 8).

## 1. Ausgangslage (aus dem Code)

| Fakt                                                                                                                                                                                                                                                                                                    | Fundstelle                                                                                     |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| Der Webserver läuft synchron in der Hauptschleife; die Seite wird mit ca. 1100 einzelnen `web_client.print/println/printf` geschrieben, jeder Aufruf ein TCP-`send`.                                                                                                                                    | `src/web_functions/web_functions.cpp` (1113 Treffer `web_client.`)                             |
| Der Task-Watchdog bewacht nur die Hauptschleife, Timeout 5 s aus dem vorkompilierten sdkconfig (`CONFIG_ESP_TASK_WDT_TIMEOUT_S=5`, `PANIC=y`).                                                                                                                                                          | `src/esp32/esp32_main.cpp:2389` (`esp_task_wdt_add(NULL)`), Framework-sdkconfig                |
| Ein einzelnes `NetworkClient::write()` wartet bei vollem TCP-Fenster bis zu 10 × `select()` à 1 s; bei Teilfortschritt beginnt die Zählung neu. Ein langsamer Browser kann **einen** Aufruf über 5 s halten.                                                                                            | Framework `libraries/Network/src/NetworkClient.cpp:386-440` (`WIFI_CLIENT_MAX_WRITE_RETRY 10`) |
| Kreuzmatrix: Feed je Zeile (`cc48f720`); die Kopfzeile (bis 64 Spalten) und die Schleifen danach sind ohne Feed.                                                                                                                                                                                        | `web_functions.cpp:2891`; Schleifen bei `:3003`, `:3038`, `:3093`                              |
| Path: kein Feed. Rechenteil (Wege sammeln, Einfügesortierung) und Ausgabeschleife je Datensatz.                                                                                                                                                                                                         | `sub_page_path()` `:2339`, Ausgabeschleife `:2429`                                             |
| MHeard: kein Feed, eine Karte je Station (bis `NBR_MAX_ROWS`).                                                                                                                                                                                                                                          | `sub_page_mheard()` `:2137`, Schleife `:2176`                                                  |
| `CommonWebClient` ist eine dünne Ableitung von `WiFiClient` (ESP32) bzw. `EthernetClient` (nRF52). `CommonWebServer::available()` erzeugt ihn per `static_cast` aus einem `WiFiClient`. **Die Klasse darf deshalb keine eigenen Datenelemente bekommen** (das Objekt ist in Wahrheit ein `WiFiClient`). | `src/web_functions/web_commonServer.h:14-24`, `:31-42`                                         |
| `web_client` wird teils als Kopie übergeben (`web_client_html(CommonWebClient web_client)`).                                                                                                                                                                                                            | `web_functions.cpp:97`                                                                         |
| Jede Antwort ist `Connection: close` ohne `Content-Length` (außer dem Config-Export). Ein Abbruch mitten in der Seite ist für das HTTP-Protokoll zulässig.                                                                                                                                              | `send_http_header()` `:4457`, Export `:775`                                                    |

Gemessen (cc48f720): DK5EN-98, 27 Zeilen Kreuzmatrix ≈ 0,75 s bei ≈ 29 kB/s; volle Tabelle 128 Zeilen ≈ 5,2 s.

## 2. Ziel und Nicht-Ziel

**Ziel:** Kein TASK_WDT-Neustart durch den Aufbau einer Webseite, egal wie groß die Tabellen sind und wie
langsam der Browser liest. Die Hauptschleife steht durch eine Webseite höchstens eine begrenzte, protokollierte
Zeit.

**Nicht-Ziel:** Umbau auf einen asynchronen Webserver; Änderung des Watchdog-Timeouts (Abschnitt 8);
Änderungen an Inhalt oder Aussehen der Seiten.

## 3. Teil 1: Sendepuffer

### 3.1 Aufbau

Neue Dateien:

- `src/web_functions/web_txbuf.h`: **reine** Logik ohne Arduino (wie `gw_reg_state.h`, `prod_lock.h`), damit sie
  auf dem Host testbar ist:
  - `struct WebTxBuf { uint8_t *buf; uint16_t cap; uint16_t len; bool aborted; uint32_t startMs; uint32_t lastProgressMs; uint32_t sentBytes; uint8_t abortReason; }`
  - `size_t webTxAppend(WebTxBuf&, const uint8_t*, size_t, WebTxIo&)`: kopiert in den Puffer; ist er voll,
    `webTxFlush()`; nach einem Abbruch werden alle weiteren Bytes verworfen (Rückgabe = Länge, damit `Print`
    nicht in eigene Fehlerpfade läuft).
  - `bool webTxFlush(WebTxBuf&, WebTxIo&)`: Sendeschleife (3.2).
  - `struct WebTxIo`: Funktionszeiger `send(ctx, p, n) -> int` (≥ 0 gesendet, −1 EAGAIN, −2 Fehler),
    `nowMs()`, `feed()`, `idle()`. Auf dem Gerät verdrahtet, im Test simuliert.
- `src/web_functions/web_txbuf.cpp`: Gerätebindung, **ein** dateistatischer `WebTxBuf` (es gibt immer nur
  einen Web-Client gleichzeitig; Kopien von `web_client` teilen ihn damit automatisch).

Änderung `src/web_functions/web_commonServer.h`, **ohne neue Datenelemente**:

```cpp
class CommonWebClient : public WiFiClient {
    using WiFiClient::WiFiClient;
public:
    size_t write(uint8_t c) override;                    // -> webTxAppend
    size_t write(const uint8_t *buf, size_t size) override; // -> webTxAppend
    void stop() override;                                // webTxFlush, dann WiFiClient::stop()
    void txBegin();                                      // Puffer holen, Zeiten setzen
    bool txEnd();                                        // webTxFlush, Puffer freigeben, Log bei Abbruch
};
```

Gleiches für den nRF52-Zweig (`EthernetClient`). Achtung: `flush()` heißt im Arduino-Client „Empfang
verwerfen“; deshalb eigene Namen `txBegin/txEnd`, `flush()` bleibt unangetastet.

### 3.2 Sendeschleife (ESP32)

`webTxFlush()` schickt nicht über `WiFiClient::write()` (10-s-Retry-Schleife), sondern direkt auf den Socket
(`fd()`, öffentlich in `NetworkClient`) mit `send(fd, p, n, MSG_DONTWAIT)`:

1. Gesendet > 0: Zeiger vor, `lastProgressMs = now`, `feed()`.
2. EAGAIN: `feed()` nur, wenn der letzte Fortschritt jünger als `WEB_TX_STALL_MS` ist; sonst Abbruch
   `stall`. Dazwischen `idle()` = `vTaskDelay(1)` (gibt CPU0/WiFi Luft, keine Busy-Schleife).
3. Fehler (nicht EAGAIN): Abbruch `error`, Socket schließen.
4. Unabhängig davon Abbruch `budget`, wenn `now - startMs > WEB_TX_PAGE_MAX_MS`.

Startwerte (in `web_txbuf.h` als `#define`, per Host-Test fixiert):

| Konstante            | Wert     | Begründung                                                                                            |
| -------------------- | -------- | ----------------------------------------------------------------------------------------------------- |
| `WEB_TX_BUF_SIZE`    | 1400 B   | knapp unter einer TCP-MSS bei MTU 1500; weniger Segmente, ca. 20-50 `send` statt ca. 1000 je Seite    |
| `WEB_TX_STALL_MS`    | 2000 ms  | wie `WEB_TIMEOUT_TIME` beim Lesen; deutlich unter 5 s Watchdog                                        |
| `WEB_TX_PAGE_MAX_MS` | 15000 ms | Obergrenze für den Stillstand der Hauptschleife durch eine Seite; volle Matrix braucht ca. 5 s im LAN |

Abbruch: Socket schließen, ab dann alle Bytes verwerfen, einmal loggen:
`[WEB];tx;abort;<stall|budget|error>;sent;<bytes>;ms;<dauer>`. Bei normalem Ende mit `--setlog`/`bDEBUG`:
`[WEB];tx;done;sent;<bytes>;ms;<dauer>;flushes;<n>` (für die Bench-Messung).

### 3.3 Speicher

- Puffer **pro Anfrage vom Heap** (`malloc` in `txBegin`, `free` in `txEnd`/`stop`), nicht statisch: der
  klassische ESP32 hat keinen Spielraum im statischen RAM (T-Beam Idle-Heap 67304 B, Basis 69332 B).
- Schlägt `malloc` fehl: ungepufferter Rückfall auf `WiFiClient::write()` (heutiges Verhalten), Log
  `[WEB];tx;nobuf`.
- Die Sendeschleife 3.2 gilt auch im Rückfall (Puffer der Länge 0 = jeder Aufruf wird sofort geflusht), damit der
  Watchdog-Schutz nicht vom Heap abhängt.

### 3.4 Einbindung in den Request-Ablauf

- `txBegin()` direkt nach `send_http_header()`, also einmal je Antwort (`web_functions.cpp:4457`; alle
  Seiten-Zweige ab `:1060` rufen `send_http_header` zuerst). Einfacher: `send_http_header()` selbst ruft
  `txBegin()` als erste Zeile.
- `txEnd()` an der Stelle, an der der Request endet (Ende der Leseschleife ab `:906`), und in `stop()`.
- **Audit-Punkt:** jede Stelle, die nach dem Schreiben noch vom Client liest, muss vorher `txEnd()` bzw. einen
  Flush auslösen. Grep auf `web_client.read`/`available` nach `send_http_header` in `web_functions.cpp`,
  `web_rm_handlers.cpp`, `web_rm_page.cpp`.
- Der Config-Export (`:775`, mit `Content-Length`) läuft über denselben Puffer; ein Abbruch liefert dort eine
  unvollständige Datei, die der Browser an der Länge erkennt. Bleibt so.

### 3.5 nRF52 (RAK4631, Ethernet W5100S)

- Gleiche Pufferung (spart SPI-Transaktionen), `feed()` ist dort leer (`meshcom_wdt_feed()` No-op).
- `EthernetClient::write()` hat eine eigene Warteschleife auf freien Socket-Puffer. Offener Punkt: prüfen, ob sie
  begrenzt ist (`RAK13800_W5100S`, `EthernetClient::write`); falls nicht, Sendeschleife 3.2 mit
  `availableForWrite()` statt `MSG_DONTWAIT`. Ohne Befund: nRF52 nur puffern, Abbruchlogik ESP32-only.

## 4. Teil 2: Watchdog-Feed auf der Path-Seite

- `sub_page_path()` (`:2339`): `#include "watchdog_feed.h"` ist schon in der Datei (`:36`).
- Feed je Datensatz in der Ausgabeschleife `for (size_t k = 0; k < nrec; k++)` (`:2429`), am Schleifenkopf, wie
  in der Matrix.
- Feed nach dem Sammeln der Wege (`nbrRouteCount`/Wege-Schleife) und nach der Einfügesortierung (bei
  `maxrec` Datensätzen quadratisch). Kein Feed in den innersten Masken-Schleifen (gebunden durch die
  Matrixgröße, Feed dort wäre nur Rauschen).
- Kommentar wie bei `cc48f720`: „one feed per record; nrec <= maxrec keeps the loop bounded“.

## 5. Teil 3: Watchdog-Feed auf der MHeard-Seite

- `sub_page_mheard()` (`:2137`): Feed je Karte am Kopf der Schleife `for (int k = 0; k < shown; k++)`
  (`:2176`); `shown <= NBR_MAX_ROWS`.
- Dazu, im selben Zug, die restlichen Schleifen der Kreuzmatrix-Seite `sub_page_neighbours()`: Kopfzeile
  (`:2881`, bis 64 Spalten), Abdeckungs-/Rollenteil (`:3003`, `:3038`, `:3093`) je ein Feed am Schleifenkopf der
  äußeren Schleife.

## 6. Tests

### 6.1 Host (neues Env `native_web_txbuf`, `test/test_web_txbuf/`)

Gegen `web_txbuf.h` mit simuliertem `WebTxIo` (Uhr, Sendefunktion mit Skript, Zähler für `feed()`):

1. Viele kleine Appends ergeben wenige Sends in Puffergröße; Bytes und Reihenfolge exakt.
2. Flush am Ende schickt den Rest; leerer Puffer schickt nichts.
3. EAGAIN mit Fortschritt (tröpfelnder Client): kein Abbruch, `feed()` je Fortschritt.
4. EAGAIN ohne Fortschritt > `WEB_TX_STALL_MS`: Abbruch `stall`, danach werden Appends verworfen und
   **kein** `send` mehr aufgerufen.
5. Gesamtdauer > `WEB_TX_PAGE_MAX_MS` trotz Fortschritt: Abbruch `budget`.
6. Sendefehler: Abbruch `error`.
7. Puffer der Größe 0 (Rückfall ohne Heap): jeder Append wird sofort gesendet, Abbruchlogik greift genauso.
8. Kein `feed()` nach Abbruch (der Watchdog soll einen echten Hänger danach weiter erkennen).

Eintrag in `docs/test-suite-map.md`; `tools/regression.sh` findet das Env über `[env:native*]`.

### 6.2 Regressionstest Watchdog (Bench, muss vorher fehlschlagen und nachher bestehen)

Neues Werkzeug `tools/bench/web_stall_client.py`: verbindet zum Knoten, fordert eine Seite an
(`/?page=neighbours`, `/?page=path`, `/?page=mheard`) und liest dann **nicht** bzw. mit 200 B/s (Modus
`--stall` / `--trickle`). Prüft über die Netzkonsole 2323 (Uptime vor/nach, `RESET_REASON`, `[WEB];tx`-Zeilen).

| Lauf | Knoten (Bench, 2 dBm, nie DK5EN-98 ohne OK) | Firmware     | Erwartung                                                                                     |
| ---- | ------------------------------------------- | ------------ | --------------------------------------------------------------------------------------------- |
| A    | DK5EN-1 (Heltec V3)                         | v4.40a.10.19 | `--stall` auf neighbours bzw. path: TASK_WDT-Neustart (Beleg)                                 |
| B    | DK5EN-1                                     | Fix          | kein Neustart, `[WEB];tx;abort;stall`, Uptime läuft weiter                                    |
| C    | DK5EN-1                                     | Fix          | `--trickle`: Seite kommt vollständig oder `budget`-Abbruch, kein Neustart                     |
| D    | DK5EN-92 (T-Beam, klassisch)                | Fix          | wie B/C; Idle-Heap-Schritt gegen `tools/heap_baseline.json`                                   |
| E    | DK5EN-90 (RAK4631)                          | Fix          | Seiten vollständig, keine Hänger (nur Pufferung)                                              |
| F    | DK5EN-1                                     | Fix          | normaler Browser (Chrome): alle Seiten unverändert, `[WEB];tx;done` mit Zeiten vorher/nachher |

Hinweis: Lauf A ist nur ein Beleg, wenn die Matrix genug Zeilen hat. Auf der Bench sind es wenige; `--stall`
erzwingt den Neustart trotzdem, weil ein einzelnes `write()` an einem vollen Fenster hängt. Falls nicht
reproduzierbar: INSTRUMENT-Befehl zum Auffüllen der Matrix mit Testzeilen (eigener kleiner Schritt, nur
INSTRUMENT).

### 6.3 Gate

`tools/regression.sh --stage 1,2`; Board-Builds `heltec_wifi_lora_32_V3`, `ttgo_tbeam`, `wiscore_rak4631`,
`t_deck_plus`, `E22_XML-DevKitC`; jsdom-Suiten (`webgui_*`) unverändert grün.

## 7. Ablauf (Wellen)

| Welle | Inhalt                                                               | Dateien                                                                                       |
| ----- | -------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| W1    | `web_txbuf.h` + Host-Test (6.1), noch nicht verdrahtet               | `src/web_functions/web_txbuf.h`, `test/test_web_txbuf/`, `platformio.ini` (Env)               |
| W2    | Verdrahtung: `CommonWebClient`-Overrides, `txBegin/txEnd`, Audit 3.4 | `web_commonServer.h`, `web_txbuf.cpp`, `web_functions.cpp` (`send_http_header`, Request-Ende) |
| W3    | Feeds Path, MHeard, Kreuzmatrix-Rest (Teile 2 und 3)                 | `web_functions.cpp` (`sub_page_path`, `sub_page_mheard`, `sub_page_neighbours`)               |
| W4    | Bench 6.2 (A vor dem Flash des Fixes), Gate, Doku                    | `tools/bench/web_stall_client.py`, `docs/`                                                    |

W2 und W3 berühren beide `web_functions.cpp` und laufen deshalb nacheinander, nicht parallel.

Doku: Defekt-ID im Katalog (`docs/architecture/08-defect-catalogue.md`, z. B. `WEB-WDT-01`), `docs/BACKLOG.md`,
`docs/RESUME.md`, `docs/test-suite-map.md`, Release-Notes beim nächsten Release.

## 8. Offene Entscheidungen

1. **Watchdog-Timeout:** bleibt 5 s (Empfehlung: erst nach dem Fix neu bewerten). Option: 10-15 s per
   `esp_task_wdt_reconfigure()` vor `esp_task_wdt_add()` als Puffer für Hänger außerhalb des Webservers;
   ein Build-Flag `-D CONFIG_ESP_TASK_WDT_TIMEOUT_S` wirkt nicht (sdkconfig überdeckt es).
2. **`WEB_TX_PAGE_MAX_MS` 15 s:** so lange steht die LoRa-Verarbeitung bei einem sehr langsamen Browser. Kürzer
   (z. B. 8 s) heißt: große Matrix über schlechtes WLAN bricht ab. Alternative später: Matrix-Seite seitenweise
   (`&x=` gibt es schon für Spalten, für Zeilen nicht).
3. **Upstream:** der Fix betrifft auch upstream (gleicher Webserver). Als eigener PR nach dem Bench, oder
   zusammen mit einem größeren PR?
4. **Bench auf DK5EN-98** (dort sind die Feld-Neustarts aufgetreten, volle Matrix): nur mit ausdrücklichem OK.

## 9. Risiken

- **`static_cast`-Konstrukt in `available()`:** jede neue Membervariable in `CommonWebClient` wäre undefiniertes
  Verhalten. Der Plan hält den Zustand deshalb dateistatisch; ein Kommentar in `web_commonServer.h` hält die
  Regel fest.
- **Lesen nach Schreiben** ohne vorherigen Flush würde eine Anfrage blockieren (Browser wartet auf den Rest).
  Audit 3.4.
- **Abgebrochene Seiten** zeigen dem Nutzer eine halbe Tabelle. Bewusst: besser als ein Neustart. Optional am
  Ende ein Hinweis, falls noch Bytes durchgehen (nicht garantiert).
- **RM-Seite und JSON-Antworten** laufen ebenfalls durch den Puffer; JSON-Antworten sind klein (ein Flush). Die
  jsdom-Tests decken nur das HTML ab, nicht das Timing; deshalb Lauf F im Browser.
