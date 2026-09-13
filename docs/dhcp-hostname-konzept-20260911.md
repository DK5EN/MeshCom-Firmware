# DHCP-Hostname aus dem Node-Namen (statt `esp32-XXXXXX`)

**Stand:** 2026-09-11 — Konzept, nicht implementiert
**Repo:** MeshCom-Firmware-DEV-Main, Branch `dry-unification`

## BLUF

Der Knoten meldet sich beim DHCP-Server derzeit als `esp32-DBE6E4`. Ein einzelner
`WiFi.setHostname()`-Aufruf in `wifiInitOnce()` genügt, damit stattdessen die
BLE-Bezeichnung (`MC-e6e4-DB0MW-12`) als DHCP-Option 12 gesendet wird. Der Eingriff
umfasst rund 12 Zeilen in einer Datei und betrifft ausschließlich die ESP32-Boards.
Der nRF52-/Ethernet-Pfad ist damit **nicht** abgedeckt.

## Warum aktuell `esp32-DBE6E4`

Die Firmware setzt nirgends einen WiFi-Hostnamen. Der arduino-esp32-Core bildet ihn
dann selbst:

| Ort                    | Verhalten                                                                    |
| ---------------------- | ---------------------------------------------------------------------------- |
| `WiFiGeneric.cpp:283`  | statischer Puffer `default_hostname[32]`                                     |
| `WiFiGeneric.cpp:288`  | Fallback `CONFIG_IDF_TARGET "-" + letzte 3 MAC-Bytes` → `esp32-DBE6E4`       |
| `WiFiGeneric.cpp:1265` | schreibt den Puffer bei **jedem** Wechsel nach `WIFI_MODE_STA` auf das netif |
| `WiFiGeneric.cpp:901`  | `WiFi.setHostname()` füllt genau diesen Puffer                               |

Vom netif geht der Name als DHCP-Option 12 (Host Name) im DISCOVER/REQUEST hinaus.

## Der Hebel

Weil der Core den Namen bei jedem STA-Moduswechsel neu aus dem statischen Puffer
zieht, genügt **ein** Aufruf vor dem ersten `WiFi.mode()`. Er überlebt auch die
`WIFI_OFF → STA`-Zyklen des Watchdog-/Reconnect-Pfads — kein erneutes Setzen nötig.

Passender Ort: `wifiInitOnce()` in `src/udp_functions.cpp:911`. Diese Funktion läuft
genau einmal und liegt vor jedem `WiFi.mode()`-Aufruf der Bringup-Kette.

Der BLE-Name steht zu diesem Zeitpunkt bereits bereit:

- `cBLEName` wird in `src/esp32/esp32_main.cpp:1709` gesetzt
- `startNetwork()` — und damit `wifiInitOnce()` — läuft erst ab `src/esp32/esp32_main.cpp:1838`

## Patch (Entwurf)

In `wifiInitOnce()`, `src/udp_functions.cpp`:

```c
  // DHCP-Hostname (Option 12): dieselbe Bezeichnung wie im BLE-Advertising.
  char host[32];
  const char *src = (cBLEName[0] != 0) ? cBLEName : meshcom_settings.node_call;
  size_t o = 0;
  for(size_t i = 0; src[i] != 0 && o < sizeof(host) - 1; i++)
  {
    char c = src[i];
    host[o++] = (isalnum((unsigned char)c) || c == '-') ? c : '-';
  }
  host[o] = 0;
  if(o > 0)
    WiFi.setHostname(host);
```

Ergebnis im DHCP-Server: `MC-e6e4-DB0MW-12`.

## Namensquelle — Abwägung

| Quelle                       | Beispiel           | Für                                               | Wider                                             |
| ---------------------------- | ------------------ | ------------------------------------------------- | ------------------------------------------------- |
| `cBLEName` **(empfohlen)**   | `MC-e6e4-DB0MW-12` | identisch mit dem BLE-Namen; MAC-Suffix eindeutig | länger                                            |
| `meshcom_settings.node_call` | `DB0MW-12`         | kurz, direkt lesbar                               | nicht eindeutig bei zwei Knoten mit gleichem Call |

## Zu beachten

- **Längenbegrenzung.** Der Core-Puffer fasst 32 Byte inkl. NUL, also max. 31 Zeichen.
  `cBLEName` ist mit 60 deklariert — deshalb die begrenzte Schleife statt eines nackten
  `strcpy`.
- **Zeichenvorrat.** Rufzeichen können Zeichen enthalten, die als Hostname unzulässig
  sind (`/`, `_`). Die Schleife ersetzt alles außer `[A-Za-z0-9-]` durch `-`.
- **AP-Modus ist nicht betroffen.** Dort ist der Knoten selbst DHCP-Server, und die SSID
  ist bereits das Rufzeichen (`WiFi.softAP(meshcom_settings.node_call)`,
  `src/udp_functions.cpp:1161`).
- **Lease-Verhalten.** Viele Router übernehmen Option 12 nur bei einem neuen Lease. Für
  den Test das bestehende Lease löschen, sonst bleibt der alte Name in der Liste stehen.
- **nRF52/RAK13800 geht so nicht.** Die vendored W5100S-Ethernet-Bibliothek sendet einen
  fest einkompilierten Hostnamen: `HOST_NAME "WIZnet"` in
  `.pio/libdeps/wiscore_rak4631/RAK13800-W5100S/src/Dhcp.h:45`, verwendet in
  `Dhcp.cpp:190-192`. Das wäre ein Patch an einer Fremdbibliothek, kein
  Firmware-Einzeiler — separat zu betrachten. Passt ohnehin zur bereits akzeptierten
  Lage, dass der nRF52-Ethernet-Pfad keinen mDNS-Responder mitbringt und der Knoten nur
  per IP erreichbar ist.

## Verifikation

1. Build `pio run -e heltec_v3` (bzw. betroffene ESP32-Envs), Lint/Typecheck/Format-Gate.
2. Flash auf DK5EN-93 (Heltec V3, Bench).
3. Bestehendes Lease im DHCP-Server löschen, Knoten neu verbinden lassen.
4. Prüfen: Hostname in der Lease-Liste lautet `MC-xxxx-DK5EN-93`.
5. Reconnect-Pfad gegentesten (`WIFI_OFF → STA`): Name muss erhalten bleiben, weil der
   Core ihn bei jedem Moduswechsel neu aus dem Statik-Puffer setzt.

## Offen

- Entscheidung Namensquelle: `cBLEName` oder `node_call`.
- Soll der Name über ein Setting abschaltbar/überschreibbar sein, oder fest aus dem
  BLE-Namen? Empfehlung: fest — ein weiteres Flash-Setting kostet mehr, als es hier nutzt.
