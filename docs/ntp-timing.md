# NTP-Timing: Sync-Kadenz, Retry-Leiter und Feldverhalten

Stand 2026-08-31, Branch `tdeck-partial-refresh-trace`. Anlass: Intake-Punkt 13
(`docs/BACKLOG.md` §3.8p, `NTP-01`). Jede Aussage unten ist gegen den aktuellen Baum verifiziert,
nicht aus dem Intake übernommen. Ergänzt um das `--ntpsync`-Kommando und das Bench-Skript
`tools/bench/experiments/ntpsync.py`, die im selben Zuschnitt entstanden sind.

## Ergebnis in einem Satz

Der Knoten fragt in der Praxis alle **15 Minuten** einen NTP-Server ab — nicht stündlich, wie die
im `NtpAsync`-Objekt selbst gesetzte 1-Stunden-Einstellung suggeriert, denn genau dieser Aufrufer
erzwingt bei jedem Durchlauf `requestNow()` und überschreibt damit die eigene Konfiguration; Retry
läuft danach in zwei festen Stufen (2,5 s Timeout, 5 s / 60 s Backoff), nicht exponentiell.

## 1. Die tatsächliche Kadenz ist 15 Minuten, von außen erzwungen

`NtpAsync` selbst hat einen Default-Refresh von 15 Minuten:

```c
// src/ntp_async.h:62
uint32_t _intervalMs = 15UL * 60UL * 1000UL;
```

Beide Plattform-Aufrufer überschreiben das aber sofort auf eine Stunde, direkt nach `begin()`:

```c
// src/udp_functions.cpp:1240-1241
timeClient.begin();
timeClient.setUpdateInterval(3600000); // Sets refresh interval to 1 hour (in ms)
```

```c
// src/nrf52/nrf_eth.cpp:1180-1181 (und identisch :1307-1308)
timeClient.begin();
timeClient.setUpdateInterval(3600000);   // the 15-min caller drives it; this is the safety net
```

Der nRF52-Kommentar sagt es bereits richtig: das 15-Minuten-Timing kommt nicht von hier. Der
eigentliche Taktgeber sitzt im jeweiligen Main-Loop, außerhalb der Klasse:

```c
// src/esp32/esp32_main.cpp:2632-2637
// every 15 minutes
if((uint32_t)(millis() - updateTimeClient) >= 1000 * 60 * 15 || updateTimeClient == 0)
{
    strTime = udpUpdateTimeClient();
    updateTimeClient = millis();
}
```

```c
// src/nrf52/nrf52_main.cpp:1219-1223 (dieselbe Struktur)
if((uint32_t)(millis() - updateTimeClient) >= (uint32_t)(1000 * 60 * 15) || updateTimeClient == 0)
{
    strTime = neth.udpUpdateTimeClient();
    updateTimeClient = millis();
}
```

`udpUpdateTimeClient()` — ESP32 in `src/udp_functions.cpp:1432-1449`, nRF52 als
`NrfETH::udpUpdateTimeClient()` in `src/nrf52/nrf_eth.cpp:1190-1209` — ruft bedingungslos
`timeClient.requestNow()`:

```c
// src/udp_functions.cpp:1434-1436
timeClient.requestNow();
timeClient.loop();
```

`requestNow()` setzt nur `_nextDueMs = millis()` (`ntp_async.h:42`) — die Fälligkeit springt sofort
auf "jetzt", unabhängig davon, was `setUpdateInterval()` vorher gesetzt hatte. Die 1-Stunden-
Einstellung ist damit **totes Setting**: sie würde nur greifen, wenn der Aufrufer die Klasse in
Ruhe ließe, tut er aber nicht — jeder 15-Minuten-Tick zieht die Fälligkeit sofort vor. Erst nach
einem erfolgreichen Sync trägt `tryConsume()` `_nextDueMs = now + _intervalMs` ein
(`ntp_async.cpp:143`) — dieser Wert wird aber in der Praxis nie erreicht, weil der nächste
15-Minuten-Tick (900.000 ms < 3.600.000 ms) längst vorher wieder `requestNow()` ruft. Der einzige
Fall, in dem die 1-Stunden-Einstellung überhaupt sichtbar würde, wäre ein Knoten ohne den
15-Minuten-Aufrufer — den gibt es im Baum nicht.

Damit steht der Widerspruch fest: das Klassen-Setting sagt "1 Stunde", das beobachtbare Verhalten
ist "15 Minuten", und die Ursache ist nicht ein Bug in `NtpAsync`, sondern ein Aufrufer, der die
Klasse häufiger anstößt, als sie selbst vorsieht, ohne das eigene `setUpdateInterval()` je
zurückzunehmen.

## 2. Retry-Leiter: zwei feste Stufen, kein Exponential-Backoff

```c
// src/ntp_async.cpp:3-5
#define NTP_ASYNC_TIMEOUT_MS 2500
#define NTP_ASYNC_RETRY_FAST_MS 5000
#define NTP_ASYNC_RETRY_SLOW_MS 60000
```

Die State Machine in `loop()` (`ntp_async.cpp:67-104`):

- Eine ausstehende Anfrage (`_pendingSince != 0`) läuft bis **2,5 s** ohne Antwort, dann zählt
  `_fails` hoch und die nächste Anfrage wird geplant.
- Solange `_fails < 3`: nächster Versuch nach **5 s** (`NTP_ASYNC_RETRY_FAST_MS`).
- Ab dem dritten Fehlschlag: nächster Versuch erst nach **60 s** (`NTP_ASYNC_RETRY_SLOW_MS`) —
  fest, nicht wachsend. `_fails` sättigt bei `0xFFFF` und wird nur durch Erfolg, `begin()` oder
  `setPoolServerIP()` zurückgesetzt (`ntp_async.cpp:14-23`).
- Eine Kiss-of-Death-Antwort (Stratum 0, `buf[1] == 0`) erzwingt sofort die 60-s-Stufe und wird
  konsumiert, statt an den MeshCom-Parser weitergereicht zu werden (`ntp_async.cpp:120-127`).

Die Klasse hat keine eigene Link-Erkennung — der Aufrufer gated auf
`meshcom_settings.node_hasIPaddress`; ohne IP-Adresse steht die State Machine schlicht still
(`loop()` kehrt sofort zurück, `ntp_async.cpp:69-70`). Beide Plattformen laufen über dieselbe
Klasse, es gibt keine separate nRF52-Implementierung.

## 3. TM-45: die Antwort landet nur, wenn etwas den Socket liest

`NtpAsync` sendet nur — die Antwort wird vom regulären UDP-Empfangspfad abgegriffen
(`tryConsume()` in `getMeshComUDPpacket()`/`NrfETH::getUDP()`). Dieser Pfad lief historisch aber
**nur, wenn `bGATEWAY` an war**:

```c
// src/ntp_async.h:69-77
// TM-45: on both platforms the reply to sendRequest() is only ever picked up
// by the gateway's receive path (getMeshComUDP() on ESP32, NrfETH::getUDP()
// on nRF52), which itself only runs while bGATEWAY is on. A non-gateway node
// therefore transmits every request fine and then always times out -- the
// reply sits unread in the socket.
```

Ein Nicht-Gateway-Knoten sendete also brav jede Anfrage, aber die Antwort verrottete ungelesen im
Socket — jeder Request lief in den 2,5-s-Timeout, nie in `ok`. Der Fix (Commit `81cfc064`, siehe
unten) fügt für den `bGATEWAY`-off-Zweig einen reinen Harvest-Aufruf ein, der nur den NTP-Socket
abgreift, ohne die restliche Gateway-Buchhaltung (`[GW];rx`, `last_upd_timer`, ...) mitzuziehen:

```c
// src/udp_functions.cpp:189-200 (ESP32, aufgerufen von esp32_main.cpp:3774
// als ntpHarvestUDP(), nur im bGATEWAY-off-Zweig)
void ntpHarvestUDP()
{
  ...
  ntpHarvestReply(Udp, timeClient);
}
```

nRF52 spiegelt das als `NrfETH::harvestNTP()` (`src/nrf52/nrf_eth.cpp:341-347`, deklariert
`nrf_eth.h:55`), aufgerufen von `nrf52_main.cpp:2083` — ebenfalls exklusiv im `bGATEWAY`-off-Zweig
(`nrf52_main.cpp:1970-2083`: "exactly one of the latter two per pass"). Damit bekommt heute **jeder**
Knoten seine NTP-Antwort zugestellt, ob Gateway oder nicht — vorher war das ausschließlich ein
Gateway-Privileg. Vor dem Fix belegten die Bench-Zahlen genau das: 0 NTP-Erfolge und 545-548
Timeouts je Board in 9,1 h (siehe `docs/BACKLOG.md` §3.8p, TM-45); danach: Erfolg mit 89 ms RTT.

## 4. Traffic-Profil eines Feldknotens

Im eingeschwungenen Erfolgsfall: ein UDP-Paket alle 15 Minuten (der Loop-Takt aus Abschnitt 1),
Antwort typischerweise binnen weniger zehn Millisekunden RTT (89 ms im o.g. Beleg — auf einer
schlechteren Strecke entsprechend mehr, aber immer noch weit unter dem 2,5-s-Timeout).

Im **Fehlerfall** (Server nicht erreichbar, aber `node_hasIPaddress` bleibt gesetzt) sieht das
Profil anders aus, wie der TM-36-Nacht-Soak zeigt: der Knoten fällt nach drei schnellen
Fehlversuchen (5-s-Stufe) in die 60-s-Dauerschleife (Abschnitt 2) und bleibt dort, solange der
Server nicht antwortet — ein stetiger **60-Sekunden-Takt fehlschlagender Anfragen**, nicht ein
Verstummen. Das ist Nutzlast auf derselben Leitung wie der Gateway-Verkehr, dauerhaft, nicht nur
alle 15 Minuten. Ein Knoten ohne IP-Adresse dagegen erzeugt gar keinen NTP-Verkehr — die Klasse
ist dann eingefroren (Abschnitt 2, letzter Absatz).

## 5. `--ntpsync`: manueller Sync außerhalb des 15-Minuten-Takts

Neues Kommando (`src/command_functions.cpp`, im `INSTRUMENT_ENABLED`-Block, direkt nach dem
`--srvip`-Hook, nach dessen Registrierungsmuster gebaut): löst `timeClient.requestNow()` auf dem
plattformweiten `timeClient`-Global aus (`extern NtpAsync timeClient;` — exakt eine Definition ist
pro Build gelinkt: `udp_functions.cpp` auf ESP32, `nrf_eth.cpp` auf nRF52, beide selbst
plattformgegated, ein einfacher `extern` löst also auf beiden Plattformen auf).

Die Klasse ist bewusst nicht-blockierend (siehe `src/ntp_async.h`-Header-Kommentar) — das Kommando
selbst blockiert daher **nicht** bis zur Antwort, sondern stößt nur an. Das Ergebnis erscheint
asynchron über dieselben `[NTP];...`-Marker, die `NtpAsync::loop()`/`tryConsume()` ohnehin schon
schreiben (`ok`/`timeout`/`txfail`/`kod`, siehe Abschnitt 2). Zwei Sonderfälle werden vom Kommando
selbst quittiert:

- keine IP-Adresse (`meshcom_settings.node_hasIPaddress == false`): `[NTPSYNC];err;no IP address`
- eine Anfrage läuft bereits: `requestNow()` überschreibt nur `_nextDueMs`, was `loop()` gar nicht
  erst ansieht, solange `_pendingSince != 0` ist — der Aufruf wäre bis zum eigenen ≤2,5-s-Timeout
  der laufenden Anfrage wirkungslos (dokumentiert und durch `test/test_ntp_async/test_main.cpp`
  abgedeckt). Dafür gibt es jetzt `NtpAsync::isPending()` (`ntp_async.h`); das Kommando meldet in
  diesem Fall `[NTPSYNC];busy;request already in flight` statt still nichts zu tun.

## 6. Bench-Regression: `tools/bench/experiments/ntpsync.py`

Treibt ein Board über `--ntpsync` in einer Schleife (`--loops`, Default 10) an, wartet je Versuch
bis zu 6 s auf eine der `[NTP];...`-Zeilen aus Abschnitt 2 (oder eine `[NTPSYNC];busy|err`-Absage),
und reduziert das zu einer Erfolgsquote plus RTT-Verteilung (min/median/p90/max). Im Stil der
bestehenden Skripte in diesem Verzeichnis (`srvprobe.py`, `wifisoak.py`): reiner Line-Reducer ohne
Seiteneffekte für `--parse-only`, Zeichen-für-Zeichen-Pacing beim Senden (dasselbe Timing-Problem,
das die anderen Skripte hier schon lösen). Kein Hardware-Lauf im Rahmen dieser Änderung durchgeführt
(kein Board am Schreibtisch) — das Skript ist geschrieben und `py_compile`-sauber, aber gegen echte
Hardware noch nicht gefahren.

## Randnotizen

- Der frühere Client war die Stock-`NTPClient`-Bibliothek (`platformio.ini:65`, Eintrag steht noch
  in `lib_deps`, wird aber nicht mehr benutzt). Sie blockierte den Aufrufer bis zu 1 s je Refresh,
  und `forceUpdate()` leerte den geteilten Gateway-Socket beim Aufruf — ein Refresh konnte damit
  wartende `GATE`/`CONF`-Datagramme mitreißen. Ersetzt durch Commit `50528168`
  (2026-08-30 13:00:53 +0200, verifiziert per `git show`). `upstream/dev` (Stand `2cb6bb4d`,
  2026-08-28) läuft weiterhin auf dem blockierenden Client.
- `getEffectiveNtpServer()` (`src/web_functions/web_functions.cpp:1098-1110`) bildet die
  Server-Auswahl für die Weboberfläche nach, statt sie zu lesen — die echte Auswahl passiert in
  `src/udp_functions.cpp:1579-1656` (die Codestelle selbst kommentiert das als bewussten,
  read-only Nachbau, mangels eines sauberen Extern-Zugriffs auf das Resultat).
- Wer die Uhrzeit des Knotens besitzt: `MyClock` (`src/clock.cpp:474`, Klasse `Clock`) ist die
  einzige Quelle der Wahrheit, gespeist mit GPS-Vorrang vor RTC; NTP diszipliniert zusätzlich die
  RTC (DS3231/PCF8563) auf ESP32, wenn `bRTCON && bNTPDateTimeValid`
  (`src/esp32/esp32_main.cpp:2680-2695`).
- TM-44 (RAK, ETH-Link oben aber Internet tot, 1,6-s-Loop-Block am `ntp`-Standort durch die
  W5100S-ARP-Retry-Logik) ist ein separater, vom Operator zurückgestellter Befund — nicht Teil
  dieses Berichts, siehe `docs/BACKLOG.md` §3.8p.
