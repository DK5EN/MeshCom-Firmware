# DHCP-Hostname aus dem Node-Namen (statt `esp32-XXXXXX`)

**Stand:** 2026-09-13 — Konzept, gegen den Baum verifiziert, noch nicht implementiert
**Repo:** MeshCom-Firmware-DEV-Main, Branch `fork-main`

## BLUF

Der Knoten meldet sich beim DHCP-Server derzeit als `esp32-DBE6E4`. Ein
`WiFi.setHostname()`-Aufruf im WiFi-Hochlauf genügt, damit stattdessen das Rufzeichen
samt SSID (`DK5EN-93`) als DHCP-Option 12 gesendet wird. Der Eingriff umfasst rund 30
Zeilen in drei Dateien und betrifft ausschließlich die ESP32-Boards. Der
nRF52-/Ethernet-Pfad ist damit **nicht** abgedeckt.

## Warum aktuell `esp32-DBE6E4`

Die Firmware setzt nirgends einen WiFi-Hostnamen — `grep -rn "setHostname\|getHostname"
src/` ist leer. Der arduino-esp32-Core bildet ihn dann selbst. Das Repo pinnt **zwei**
Cores; beide verhalten sich identisch:

| Core                                | Fallback-Name           | Puffer                      | Anwendung aufs netif   |
| ----------------------------------- | ----------------------- | --------------------------- | ---------------------- |
| 2.x (`espressif32@^6.13.0`)         | `CONFIG_IDF_TARGET-MAC` | `WiFiGeneric.cpp:283`, 32 B | `WiFiGeneric.cpp:1265` |
| 3.x (tasmota `2026.02.30`, S3-Envs) | dito                    | `NetworkManager.cpp`, 32 B  | `WiFiGeneric.cpp:636`  |

In beiden Fällen schreibt der Core den statischen Puffer auf das STA-netif, sobald der
Modus **nach** `WIFI_MODE_STA` wechselt. Vom netif geht der Name als DHCP-Option 12
(Host Name) im DISCOVER/REQUEST hinaus.

## Der Hebel

Weil der Core den Namen bei jedem Übergang nach STA neu aus dem statischen Puffer zieht,
genügt ein `WiFi.setHostname()` **vor** dem jeweiligen `WiFi.mode()`.

Passender Ort ist der Hochlaufpfad in `startNetwork()`, unmittelbar vor

```c
{ WifiStall st("mode"); WiFi.mode(WIFI_OFF); WiFi.mode(WIFI_STA); }   // src/udp_functions.cpp:1187
```

**Nicht** in `wifiInitOnce()` (`src/udp_functions.cpp:912`). Diese Funktion ist per
`s_wifiInitDone` gegen Mehrfachausführung gesperrt und läuft damit genau einmal pro
Boot. Der Name wäre dann bootgebunden — und damit träger als alles andere, was der
Knoten von sich preisgibt: der mDNS-Responder liest `node_call` bei **jedem**
Webserver-Neustart neu (`MDNS.end()` in `web_functions.cpp:123`, `MDNS.begin()` in
`:132`). Im Hochlaufpfad kostet der Aufruf ein `snprintf` in einen 32-Byte-Puffer pro
Bringup statt pro Boot — vernachlässigbar — und der Name folgt einer Rufzeichenänderung
genauso schnell wie der mDNS-Name.

`wifiInitOnce()` bleibt unangetastet.

## Namensquelle: `meshcom_settings.node_call`

Entschieden am 2026-09-13 gegen den ursprünglichen Vorschlag `cBLEName`.

| Quelle                    | Beispiel           | Für                                                | Wider                                               |
| ------------------------- | ------------------ | -------------------------------------------------- | --------------------------------------------------- |
| `node_call` **(gewählt)** | `DK5EN-93`         | identisch mit beiden mDNS-Namen; kurz; passt immer | bei zwei Knoten mit gleichem Call nicht eindeutig   |
| `cBLEName`                | `MC-e6e4-DK5EN-93` | MAC-Suffix eindeutig                               | zweiter Name neben `DK5EN-93.local`; Länge kritisch |

Ausschlaggebend: beide mDNS-Responder benennen den Knoten bereits nach `node_call` —
die Firmware in `web_functions.cpp:132`, safeboot in `safeboot/main.cpp:338`. Mit
`cBLEName` hätte der Knoten zwei Namen: `MC-e6e4-DK5EN-93` in der Lease-Liste,
`DK5EN-93.local` im mDNS. Das Eindeutigkeitsargument für `cBLEName` trägt nicht: zwei
Knoten mit gleichem Rufzeichen kollidieren heute schon im mDNS, und der DHCP-Server
unterscheidet sie ohnehin über die MAC. `node_call` enthält die SSID (`-93`) und ist
damit im Netz so eindeutig wie das Rufzeichen selbst; erst ein blankes `DK5EN` ohne
SSID würde kollidieren. Nebeneffekt: `node_call` bleibt unbedingt unter der
31-Zeichen-Grenze, die Trunkierungsfrage entfällt.

## Patch (Entwurf)

Gemeinsamer Helfer in `src/configuration_global.h` — der Header wird von der Firmware
**und** von safeboot eingebunden (`safeboot/main.cpp:23`) und beherbergt bereits
`inline`-Helfer (`isNodeUnconfigured()` ab Zeile 18):

```c
// DHCP-Option 12 / RFC-1123-Label aus dem Rufzeichen. false => nicht setzen,
// dann bleibt der Core-Default (esp32-XXXXXX) stehen.
inline bool makeDhcpHostname(char *out, size_t n, const char *call)
{
    if (out == nullptr || n < 2 || isNodeUnconfigured(call))
        return false;
    size_t o = 0;
    for (size_t i = 0; call[i] != 0 && o < n - 1; i++)
    {
        char c = call[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                  (c >= 'a' && c <= 'z') || c == '-';
        out[o++] = ok ? c : '-';
    }
    while (o > 0 && out[o - 1] == '-')   // RFC 1123: Label endet alphanumerisch
        o--;
    out[o] = 0;
    return o > 0;
}
```

Aufruf in `startNetwork()`, `src/udp_functions.cpp`, direkt vor Zeile 1187:

```c
  char host[32];
  if(makeDhcpHostname(host, sizeof(host), meshcom_settings.node_call))
    WiFi.setHostname(host);
```

Ergebnis im DHCP-Server: `DK5EN-93`.

## Was bei einer Rufzeichenänderung passiert

Drei Schichten, die unabhängig voneinander nachziehen.

**Firmware.** `--setcall` (`src/command_functions.cpp:3616-3646`) prüft per
`checkRegexCall()`, schreibt `node_call`, ruft `save_settings()` und setzt dann
`rebootAuto = millis() + 15000` — allerdings unter
`#if !defined(BOARD_T_DECK) && !defined(BOARD_T_DECK_PLUS)`. Auf Heltec, T-Beam, RAK,
E22 usw. bootet der Knoten also von selbst neu und meldet sich mit dem neuen Namen an.

**T-Deck / T-Deck Plus.** Dort gibt es keinen Auto-Reboot. Das ist **kein Versehen im
`setcall`-Zweig**, sondern durchgehende Upstream-Politik: dieselbe Guard-Kombination
steht an 23 Stellen in `command_functions.cpp` — `setssid`, `setownip`, `blelong` und
so fort — eingeführt von Kurt in `b5daf052` ("v4.35p T-DECK SETUP", 2026-06-13). Der
T-Deck wird interaktiv am Gerät konfiguriert; ein Reboot mitten in der Eingabekette
wäre dort schädlich. Nicht anfassen. Der Operator startet mit `--reboot` neu, oder der
Name zieht beim nächsten WiFi-Bringup nach — was mit dem oben gewählten Aufrufort
automatisch geschieht.

**DHCP-Server.** Das ist die eigentliche Trägheit, und sie liegt außerhalb der
Firmware. Option 12 reist nur im DISCOVER/REQUEST. Viele Consumer-Router führen ihre
Geräteliste über die MAC und behalten den zuerst gelernten (oder vom Benutzer
editierten) Namen — eine FRITZ!Box etwa zeigt den alten Eintrag bis zum Ablauf des
Lease, teils darüber hinaus. Nach einer Umbenennung steht `DK5EN-93` also unter
Umständen noch tagelang neben dem neuen `OE1ABC-12` in der Liste. Abhilfe am Server:
Lease löschen.

## Ausbaustufe: Lease vor dem Reboot freigeben

Naheliegende Frage aus der Praxis — ja, das geht, und die APIs sind auf **beiden**
gepinnten Cores vorhanden:

| Funktion                              | 2.x                        | 3.x                        |
| ------------------------------------- | -------------------------- | -------------------------- |
| `esp_netif_get_handle_from_ifkey()`   | `esp_netif.h:814`          | `esp_netif.h:962`          |
| `esp_netif_dhcpc_stop()` / `_start()` | `esp_netif.h:548` / `:533` | `esp_netif.h:633` / dito   |
| `esp_netif_tcpip_exec()`              | `esp_netif.h:928`          | `esp_netif.h:1114`         |
| `esp_netif_get_netif_impl()`          | `esp_netif_net_stack.h:49` | `esp_netif_net_stack.h:38` |
| `dhcp_release_and_stop()`             | `lwip/dhcp.h:133`          | `lwip/dhcp.h:118`          |

Zwei Varianten:

1. **Renew statt Release (empfohlen, falls überhaupt).** `esp_netif_dhcpc_stop()` +
   `esp_netif_dhcpc_start()` auf dem STA-netif erzwingt eine neue DHCP-Transaktion mit
   der neuen Option 12 — **ohne Reboot**. Das löst den T-Deck-Fall elegant mit und
   bleibt vollständig in der esp_netif-API, also thread-sicher ohne eigenes
   `LOCK_TCPIP_CORE()`.
2. **Echtes RELEASE.** `dhcp_release_and_stop()` auf dem `struct netif`, gekapselt in
   `esp_netif_tcpip_exec()` (lwIP-Kernfunktionen dürfen nicht aus dem Arduino-Task
   heraus gerufen werden). Sagt dem Server ausdrücklich, dass die Bindung frei ist.

**Dennoch zurückgestellt.** Der Nutzen ist serverabhängig und nicht messbar
vorhersagbar: ein dnsmasq oder ISC-dhcpd übernimmt den neuen Namen bei frischer
Bindung, eine FRITZ!Box mit persistenter Geräteliste unter Umständen nicht. Dem steht
ein konkreter Nachteil gegenüber — nach einem RELEASE kann der Knoten eine **andere
IP** bekommen, und gerade der RAK4631 ist mangels mDNS-Responder ausschließlich per IP
erreichbar. Für einen Vorgang, der pro Knoten vielleicht einmal im Leben stattfindet,
ist "Lease am Router löschen" der ehrlichere Weg. Wird als BACKLOG-Punkt geführt und
erst nach einer Bench-Messung gegen den tatsächlich eingesetzten Router entschieden.

## Zu beachten

- **Längenbegrenzung.** Der Core-Puffer fasst 32 Byte inkl. NUL, also max. 31 Zeichen.
  Mit `node_call` (10-Byte-Feld) kein Thema, die Schleife bleibt trotzdem begrenzt.
- **Zeichenvorrat.** `checkRegexCall()` filtert schon vorher, der Helfer ist
  Gürtel-und-Hosenträger: alles außerhalb `[A-Za-z0-9-]` wird zu `-`, Trailing-`-`
  fallen weg.
- **Unkonfigurierter Knoten.** `isNodeUnconfigured()` fängt `XX0XXX*`, leer und `none`
  ab; dann wird `setHostname()` gar nicht erst gerufen und der Core-Default bleibt
  stehen. Ein halbgares `XX0XXX-0` geht nie ins Netz.
- **AP-Modus ist nicht betroffen.** Dort ist der Knoten selbst DHCP-Server, und die SSID
  ist bereits das Rufzeichen (`WiFi.softAP(meshcom_settings.node_call)`,
  `src/udp_functions.cpp:1161`).
- **Keine Kollision im Bestand.** `grep -rn "setHostname\|getHostname" src/` ist leer —
  es gibt nichts, was den Namen heute schon liest oder setzt.
- **safeboot.** `src/safeboot/main.cpp` hat einen eigenen WiFi-Hochlauf, berechnet in
  `hostname` (Zeile 44/237) bereits den Namen aus dem Rufzeichen für softAP und mDNS,
  setzt aber den STA-Hostnamen nie. Eine Zeile vor `WiFi.mode(WIFI_STA)` (~Zeile 268),
  und der OTA-Modus erscheint unter demselben Namen.

## Nicht abgedeckt

- **nRF52/RAK13800.** Die vendorierte W5100S-Ethernet-Bibliothek sendet einen fest
  einkompilierten Hostnamen: `HOST_NAME "WIZnet"` in
  `.pio/libdeps/wiscore_rak4631/RAK13800-W5100S/src/Dhcp.h:45`, verwendet in
  `Dhcp.cpp:190-192`. Das wäre ein Patch an einer Fremdbibliothek, kein
  Firmware-Einzeiler. Passt zur bereits akzeptierten Lage, dass der nRF52-Ethernet-Pfad
  keinen mDNS-Responder mitbringt und der Knoten nur per IP erreichbar ist.
- **ESP32-Ethernet (`HAS_ETHERNET`).** Betrifft `variants/T-ETH-ELITE_1262` und
  `variants/LilyGo_T_Connect_Pro`, die über `ETH.begin()` in `src/esp32/esp32_eth.cpp`
  hochfahren. Keiner der beiden Cores setzt dort überhaupt einen netif-Hostnamen. Ein
  `ETH.setHostname()` müsste vor dem DHCP-Start liegen, was die Bringup-Reihenfolge in
  `initethDHCP()` berührt — und **keines der beiden Boards steht auf der Bench**, die
  Änderung wäre also nicht verifizierbar. Zurückgestellt.

## Verifikation

1. Sauberer, sequentieller Build: `heltec_v3` (2.x-Core), ein S3-Env (3.x-Core),
   `esp32-safeboot`, `wiscore_rak4631` (belegt, dass der nRF52-Build unberührt bleibt).
2. Host-Tests und Format-Gate.
3. Flash auf DK5EN-93 (Heltec V3, Bench).
4. Bestehendes Lease im DHCP-Server löschen, Knoten neu verbinden lassen.
5. Prüfen: Hostname in der Lease-Liste lautet `DK5EN-93`.
6. Reconnect-Pfad gegentesten (`WIFI_OFF → STA`): Name muss erhalten bleiben.
7. Umbenennung gegentesten: `--setcall DK5EN-94`, Reconnect abwarten, prüfen dass die
   neue Option 12 hinausgeht — und festhalten, was der Router vor und nach einem
   Lease-Flush anzeigt. Dieses Messergebnis entscheidet über die Ausbaustufe oben.
8. safeboot-Gegenprobe auf demselben Board.

## Offen

- Ausbaustufe "Lease freigeben / DHCP-Renew" — Entscheidung nach Schritt 7.
- ESP32-Ethernet-Pfad — bleibt liegen, solange kein Board auf der Bench steht.
