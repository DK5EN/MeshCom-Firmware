# Wire-Kompatibilität: Endianness, CONF-Frame und Settings-Persistenz

Stand 2026-08-31, Branch `tdeck-partial-refresh-trace`. Anlass: Intake-Punkt 11
(`docs/BACKLOG.md` §3.8p, `CONF-01`/`DOC-03`) — Endianness-Frage rund um den nRF52-CONF-Pfad
und die eigentliche Kompatibilitätsfrage dahinter. Jede Aussage unten ist gegen den aktuellen
Baum verifiziert, nicht aus älteren Notizen übernommen.

## Ergebnis in einem Satz

Es gibt kein Big-Endian-Problem und kein `htonl`/`ntohl` im Baum — die CONF-Koordinaten sind
**little-endian**, und beide MCUs sind selbst little-endian, also ist keine Konvertierung nötig.
Der tatsächliche Kompatibilitätshebel liegt woanders: ESP32 persistiert jedes Feld einzeln unter
einem NVS-Key, nRF52 schreibt den kompletten Settings-Struct als rohen Blob — nur auf nRF52 ist
die Feldreihenfolge im C-Struct Teil des Datenformats.

## 1. CONF-Koordinaten sind little-endian auf dem Draht

Der `CONF`-Frame (server-seitig gepushte Callsign/Shortname/Koordinaten, empfangen über
`getUDP()`/`getMeshComUDPpacket()`) kodiert Latitude/Longitude/Altitude als 4-Byte `int32`,
**Byte 0 zuerst = LSB**. Der gemeinsame Parser beider Plattformen:

```c
// src/conf_frame.cpp:53-62
// optional: 0x02 <4 bytes lat> -- byte order matches nrf_eth.cpp's
// existing parse (payload[pos] is the least-significant byte)
if(pos < len && payload[pos] == 0x02)
{
    pos++;
    if(pos + 4 > len)
        return false;
    out.lat = (int32_t)((uint32_t)payload[pos] | ((uint32_t)payload[pos + 1] << 8) |
                         ((uint32_t)payload[pos + 2] << 16) | ((uint32_t)payload[pos + 3] << 24));
    ...
```

`payload[pos]` geht in die untersten 8 Bit, `payload[pos+3]` in die obersten — das ist die
Definition von Little-Endian. Longitude (`0x03`) und Altitude (`0x04`) folgen demselben Muster.
Der Kommentar sagt es explizit: die Byte-Reihenfolge wurde **absichtlich** an den bereits
bestehenden nRF52-Parser angeglichen, nicht umgekehrt.

`docs/architecture/11-wire-format.md:271-273` dokumentiert dasselbe Format bereits korrekt
(`0x02 <int32 LE> latitude` usw.) — dort sind allerdings die Zeilenverweise auf den
nRF52-Parser (`nrf_eth.cpp:497-587`, `:532-581`) veraltet: die CONF-Verarbeitung wurde seither
in `src/conf_frame.{h,cpp}` ausgelagert; der aktuelle Aufrufer ist der `CONF`-Zweig in
`src/nrf52/nrf_eth.cpp:677-763` (Parser-Aufruf `:720`). Wer von dort aus weiterarbeitet, sollte
sich nicht auf die alten Zeilenzahlen in `11-wire-format.md` verlassen.

**Frühere Annahme korrigiert:** Die Intake-Notiz zu diesem Punkt (`BACKLOG.md`, `CONF-01`/`DOC-03`)
hielt fest, dass frühere interne Dokumentation Big-Endian angenommen hatte. Das war falsch — es
gibt keine Byte-Reihenfolge-Umkehr an dieser Stelle, weder im Code noch (siehe oben) in der
bestehenden Wire-Format-Doku.

**Aktueller Implementierungsstand (zum Zeitpunkt dieses Dokuments), zur Einordnung:** Beide
Plattformen parsen lat/lon/alt aus dem CONF-Frame, wenden sie aber **bewusst nicht an** — nur
zur Sichtbarkeit geloggt (`udp_functions.cpp:549-556`, `nrf_eth.cpp:726-733`, beide mit dem
Kommentar „parsed for visibility, not applied"). Callsign und Shortname werden dagegen auf
beiden Plattformen angewendet, mit Quell-IP-Guard, `checkRegexCall()`, `save_settings()` und
Auto-Reboot. Die LE-Kodierung der Koordinaten ist damit heute ein reiner Wire-Format-Fakt ohne
laufende Settings-Auswirkung — relevant wird sie, sobald jemand den Apply-Schritt für lat/lon/alt
nachrüstet.

## 2. Kein `htonl`/`ntohl` im Baum — zwei `htons()`, aber nicht für CONF/Settings

```
$ grep -rn "htonl\|ntohl\|htons\|ntohs" src/
src/net_console.cpp:329:        addr.sin_port        = htons(NET_CONSOLE_PORT);
src/esp32/external_radio_glue.cpp:71:    addr.sin_port   = htons(port);
```

Ergebnis: **null Treffer** für `htonl`/`ntohl` — nirgends im Baum wird ein 32-Bit-Wert in
Netzwerk-Byte-Order konvertiert. Es gibt **zwei** Treffer für `htons()`, beide für
`sockaddr_in.sin_port` beim Aufbau eines BSD-Sockets (Netzwerkkonsole auf Port 2323, externer
Radio-Glue-Socket) — das ist normale POSIX-Socket-API-Nutzung für die Portnummer im
`sockaddr`-Struct, nicht Teil des CONF-/Settings-Wire-Formats und nicht das, was die frühere
Intake-Formulierung „kein `htonl`/`ntohl`/`htons`/`ntohs` irgendwo in `src/` — null Treffer
repo-weit" meinte. Für die Frage dieses Dokuments (CONF-Koordinaten, Settings-Struct) bleibt die
Aussage richtig: nichts davon wird normalisiert, weil beide MCUs (Xtensa/ESP32 und Nordic/nRF52)
selbst little-endian sind und `meshcom_settings` nie als binärer Struct über die Leitung geht —
jedes Feld wird einzeln serialisiert (siehe unten).

## 3. IP-Adressen sind ASCII-Strings, nie binär gepackt

`node_ip` ist ein `char[40]` in beiden Settings-Structs
(`src/esp32/esp32_flash.h:201`, `src/nrf52/WisBlock-API.h:368`) und wird ausschließlich als
Dotted-Quad-ASCII beschrieben:

```c
// src/udp_functions.cpp:1531 (ESP32, WLAN-STA-Fall)
snprintf(meshcom_settings.node_ip, sizeof(meshcom_settings.node_ip), "%i.%i.%i.%i",
         WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);

// src/nrf52/nrf_eth.cpp:1031 (nRF52, Ethernet-DHCP-Fall)
snprintf(meshcom_settings.node_ip, sizeof(meshcom_settings.node_ip), "%i.%i.%i.%i",
         Ethernet.localIP()[0], Ethernet.localIP()[1], Ethernet.localIP()[2], Ethernet.localIP()[3]);
```

(Statische IPs derselben Felder: `udp_functions.cpp:1484` softAP-Fall, `:1491` und
`esp32_eth.cpp:51` übernehmen `node_ownip` direkt als String.) Zielserver werden ebenfalls nie
als gepackte 32-Bit-Adresse gespeichert, sondern per Octet konstruiert
(`IPAddress(145, 239, 75, 155)`, viermal in `nrf_eth.cpp`, Zeilen 1108/1147/1249/1283). Damit
gibt es an dieser Stelle keine Byte-Order-Frage — eine ASCII-Dezimalzahl hat keine Endianness.
`node_gwsrv` (`char[3]`, Länderkürzel wie `"IT"`) ist ebenfalls ASCII. `node_country` und
`node_alt` sind native `int`-Felder, die nie über die Leitung gehen — nur lokal ausgewertet bzw.
(im Fall von `node_alt`) als Dezimalzahl in JSON exportiert (siehe `docs/settings-registers.md`).

## 4. Der eigentliche Kompatibilitätshebel: NVS-Key vs. Blob

Beide Plattformen haben denselben `s_meshcom_settings`-Struct als Quelle der Wahrheit im RAM,
aber sie schreiben ihn völlig unterschiedlich auf den Flash.

**ESP32 — ein NVS-Key pro Feld** (`src/esp32/esp32_flash.cpp`, Preferences-API auf `nvs_flash`):

```c
// src/esp32/esp32_flash.cpp:532-548 (Auszug)
preferences.putInt("node_wifip", meshcom_settings.node_wifi_power);
strVar = meshcom_settings.node_lora_call;
preferences.putString("node_ucall", strVar);
preferences.putFloat("node_aak", meshcom_settings.node_analog_alpha);
...
strVar = meshcom_settings.node_gwsrv;
preferences.putString("node_gwsrv", strVar);
```

Jedes Feld hat einen eigenen String-Key und einen eigenen Lese-/Schreibaufruf. Die Position des
Felds im C-Struct ist irrelevant für das Flash-Format — NVS ist ein Key-Value-Store, keine
Byte-Offset-Struktur.

**nRF52 — der ganze Struct als roher Blob** (`src/nrf52/nrf52_flash.cpp`, LittleFS):

```c
// src/nrf52/nrf52_flash.cpp:379-392 (Auszug)
if (memcmp((void *)&g_flash_content, (void *)&meshcom_settings, sizeof(s_meshcom_settings)) != 0)
{
    ...
    InternalFS.remove(settings_name);
    if (lora_file.open(settings_name, FILE_O_WRITE))
    {
        lora_file.write((uint8_t *)&meshcom_settings, sizeof(s_meshcom_settings));
        lora_file.flush();
    }
    ...
}
```

Hier ist die Struct-Definition selbst das Wire-Format der Flash-Datei — Feldreihenfolge,
Feldgröße und Padding sind alle Teil des gespeicherten Layouts. Laut
`docs/architecture/08-defect-catalogue.md` (`N-12`) sind die beiden Structs bereits
unterschiedlich groß (2008 B ESP32, 1968 B nRF52, Stand der letzten Vermessung) und die
`node_mcp17io`/`node_mcp17t`/`node_mcp17out`/`node_mcp17in`-Gruppe liegt auf den beiden
Plattformen bereits in **unterschiedlicher Reihenfolge** — folgenlos, weil die beiden Blobs
niemals denselben Flash-Inhalt lesen (ein RAK4631 liest nur, was ein RAK4631-Build geschrieben
hat).

### Was das für das Hinzufügen/Umsortieren von Feldern bedeutet

- **ESP32:** Ein neues Feld braucht nur ein neues `putX()`/`getX()`-Paar mit einem eigenen
  Key-String; die Position im C-Struct ist frei wählbar, auch mitten im Struct. Risiko liegt
  woanders — ein Tippfehler im Key-String oder ein umbenannter Key lässt alte Flash-Daten beim
  nächsten Boot spurlos auf den C++-Default zurückfallen (kein Crash, keine Warnung, kein
  Versions-Trigger, der das fangen würde). Genau dieses Muster liefert vier der in
  `docs/settings-registers.md` dokumentierten Key/Member-Mismatches.
- **nRF52:** Eine Struct-Änderung — Feld eingefügt, entfernt, Reihenfolge getauscht, Typ
  geändert — verschiebt jedes nachfolgende Feld um die entsprechende Byte-Zahl im gespeicherten
  Blob. Ohne einen `FLASH_STRUCT_VERSION`-Bump liest das nächste Boot die alten Bytes unter dem
  neuen Layout — stille Fehlinterpretation, nicht nur beim geänderten Feld, sondern bei allen
  Feldern danach. Die einzige Laufzeit-Integritätsprüfung auf nRF52 sind zwei Marker-Bytes
  (`valid_mark_1=0xAA`, `valid_mark_2=0x55`/`0x57`, siehe `N-12`) plus seit `14e826b8` ein
  exakter `sizeof()`-Größenvergleich — eine **sizeof-neutrale** Umsortierung (z. B. zwei
  gleich große Felder vertauscht) ist für beide Prüfungen unsichtbar und wird im
  Catalogue-Eintrag `N-12` ausdrücklich als „blinder Fleck" benannt.

### `FLASH_STRUCT_VERSION` — die einzige Bremse, und sie ist plattformübergreifend, aber nicht gleich riskant

`src/configuration_global.h:61-76` trennt bewusst zwei Zähler:

- `FLASH_VERSION` (aktuell `20260828`) ist die Build-/Release-Kennung, rein informativ
  (`--info`).
- `FLASH_STRUCT_VERSION` (aktuell `20260724`) benennt die Struct-Layout-Generation und wird
  **nur** hochgezogen, wenn sich Feld, Typ oder Reihenfolge tatsächlich ändert. Nur dieser Wert
  entscheidet über `clear_flash()` (`esp32_main.cpp:742`, `nrf52_main.cpp` analog — siehe
  `flashLayoutCompatible()`).

Beide Plattformen teilen sich diesen Mechanismus, aber er trägt unterschiedlich viel Gewicht:
Auf ESP32 ist ein Layout-Wechsel ohnehin risikoarm (NVS ist positionsunabhängig), der
Versions-Bump schützt dort im Wesentlichen vor entfernten/umbenannten Keys, die sonst auf
Default zurückfallen. Auf nRF52 ist `FLASH_STRUCT_VERSION` die **einzige** Instanz, die zwischen
einer harmlosen Struct-Änderung und einer stillen Flottenkorruption unterscheidet — versäumt man
den Bump, greift weder die Größenprüfung (falls `sizeof` zufällig gleich bleibt) noch die
Zwei-Byte-Markierung.

**Für `CONF-01` und jeden künftigen Apply-Schritt der lat/lon/alt-Felder heißt das:** Die
Zielfelder (`node_lat`/`node_long`/`node_alt` bzw. äquivalent) existieren bereits in beiden
Structs — ein reiner Apply-Fix ohne neues Feld braucht deshalb **keinen**
`FLASH_STRUCT_VERSION`-Bump. Würde stattdessen ein neues Feld für die CONF-Koordinaten
eingeführt, müsste es auf nRF52 ans Ende des Structs angehängt werden (oder der Bump wäre
Pflicht) — mitten hineinschreiben würde jedes bestehende RAK4631-Flash-Image ab dieser Stelle
falsch lesen.

## Reproduzierbarkeit

```
grep -rn "htonl\|ntohl\|htons\|ntohs" src/
grep -n "0x02\|byte order" src/conf_frame.cpp
grep -n "node_ip\[" src/esp32/esp32_flash.h src/nrf52/WisBlock-API.h
grep -n "meshcom_settings.node_ip" src/udp_functions.cpp src/esp32/esp32_eth.cpp src/nrf52/nrf_eth.cpp
grep -n "IPAddress(145, 239, 75, 155)" src/nrf52/nrf_eth.cpp
grep -n "putInt\|putString\|putFloat" src/esp32/esp32_flash.cpp | wc -l
sed -n '370,395p' src/nrf52/nrf52_flash.cpp
grep -n "FLASH_STRUCT_VERSION\|FLASH_VERSION" src/configuration_global.h
```

## Quellen

- `src/conf_frame.{h,cpp}` — gemeinsamer CONF-TLV-Parser
- `src/nrf52/nrf_eth.cpp:677-763` — nRF52-CONF-Zweig (Parser-Aufruf `:720`)
- `src/udp_functions.cpp:508-590` — ESP32-CONF-Zweig
- `src/esp32/esp32_flash.cpp`, `src/nrf52/nrf52_flash.cpp` — Persistenzpfade
- `src/configuration_global.h:55-99` — `FLASH_VERSION`/`FLASH_STRUCT_VERSION`
- `docs/architecture/08-defect-catalogue.md` — `N-12` (Struct-Divergenz, Marker-Bytes,
  sizeof-Blindfleck)
- `docs/architecture/11-wire-format.md:264-287` — CONF-TLV-Format (Byte-Order dort bereits
  korrekt, Zeilenverweise auf den nRF52-Parser veraltet)
- `docs/BACKLOG.md` §3.8p, `CONF-01`/`DOC-03` — Intake, ursprüngliche (falsche) Big-Endian-Annahme
