# Settings-Register-Referenz (`config.json`)

Stand 2026-08-31, Branch `tdeck-partial-refresh-trace`. Anlass: Intake-Punkt 12
(`docs/BACKLOG.md` §3.8p, `DOC-04`) — Referenz für alle Register, die über `GET /config.json`
exportiert und über `POST /config` wieder importiert werden können.

Backbone dieses Dokuments ist die X-Macro-Tabelle `CFG_FIELD_LIST` in `src/config_json.cpp:88-190`
(gemeinsamer Teil) plus `CFG_FIELD_LIST_PLATFORM` (`:201-211`, plattformspezifisch) — sie treibt
`configExportJson()` und `configImportJson()` gleichzeitig, es gibt keine zweite Liste. Jede Zeile
unten stammt direkt aus dieser Tabelle bzw. aus einem Grep/Read gegen den aktuellen Baum
(Reproduktionsbefehle am Dateiende).

## Kurzfassung

- **107 Register auf ESP32, 103 auf nRF52** (101 gemeinsam, 6 ESP32-only, 2 nRF52-only) —
  Auszählung der Intake bestätigt, keine Abweichung gefunden: `CFG_FIELD_LIST` hat exakt 101
  Zeilen (`config_json.cpp:89-189`), der ESP32-Zweig von `CFG_FIELD_LIST_PLATFORM` 6
  (`:202-207`), der nRF52-Zweig 2 (`:210-211`).
- **Ein totes Register:** `node_gpsbaud` hat außerhalb von Export/Import/Flash keinen Leser
  irgendwo im Baum (§3).
- **Vier Key/Member-Mismatches** — historisch gewachsen, absichtlich nicht bereinigt, weil das
  jeden bestehenden NVS-Eintrag brechen würde (§4).
- **Fünf Register tragen ein Geheimnis im Klartext**, von denen nur drei im Code-Kommentar
  dokumentiert sind (§5).
- **Sechs Register haben einen strengeren Boot-Zeit-Clamp als ihr Import-Bereich zulässt** — der
  Import akzeptiert mehr, als beim nächsten Boot stehen bleibt (§6).

## 1. Register nach Bereich

Spalten: **Key** = NVS-Key-String (ESP32) / Feldname im Export-JSON (identisch auf beiden
Plattformen, siehe `docs/wire-compat.md` §4 zum Unterschied dahinter); **Member** = C-Member in
`s_meshcom_settings`; **Typ (Größe)** = `CfgType` aus der Tabelle, bei String-/Char-Feldern die
deklarierte Puffergröße; **Import-Bereich** = `lo..hi` aus der Tabelle, `—` = `CFG_NORANGE`
(unvalidiert — betrifft laut Intake durchgängig Bitmasken, den MCP17-IO-Block und die
Gruppennummern); **Hinweis** = Flags aus §3–§6 dieses Dokuments.

### A. Identität & APRS-Symbol

| Key          | Member       | Typ (Größe) | Import-Bereich | Hinweis            |
| ------------ | ------------ | ----------- | -------------- | ------------------ |
| `node_call`  | `node_call`  | string (10) | —              | Rufzeichen         |
| `node_short` | `node_short` | string (6)  | —              | Shortname          |
| `node_symid` | `node_symid` | char (1)    | —              | APRS-Symboltabelle |
| `node_symcd` | `node_symcd` | char (1)    | —              | APRS-Symbolcode    |

### B. Position

| Key          | Member       | Typ (Größe) | Import-Bereich | Hinweis        |
| ------------ | ------------ | ----------- | -------------- | -------------- |
| `node_lat`   | `node_lat`   | double      | -90..90        |                |
| `node_lon`   | `node_lon`   | double      | -180..180      |                |
| `node_alt`   | `node_alt`   | int         | -1000..20000   |                |
| `node_lat_c` | `node_lat_c` | char (1)    | —              | Hemisphäre N/S |
| `node_lon_c` | `node_lon_c` | char (1)    | —              | Hemisphäre E/W |

### C. Staging-WLAN & allgemeine Bitmasken

| Key          | Member             | Typ (Größe) | Import-Bereich | Hinweis                                                   |
| ------------ | ------------------ | ----------- | -------------- | --------------------------------------------------------- |
| `node_ssid`  | `node_ossid`       | string (40) | —              | **Mismatch** (§4): Server-/MeshCom-WLAN-SSID              |
| `node_pwd`   | `node_opwd`        | string (40) | —              | **Mismatch** (§4), **Secret** (§5): Staging-WLAN-Passwort |
| `node_honly` | `node_hamnet_only` | int         | 0..1           |                                                           |
| `node_sset`  | `node_sset`        | int         | 0..65535       | Bitmaske                                                  |
| `node_maxv`  | `node_maxv`        | float       | 0..20          |                                                           |

### D. Externe Anbindung & Hop-Limit

| Key            | Member         | Typ (Größe) | Import-Bereich         | Hinweis                                                                                                                |
| -------------- | -------------- | ----------- | ---------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| `node_extern`  | `node_extern`  | string (40) | —                      |                                                                                                                        |
| `max_hop_text` | `max_hop_text` | int         | `MAXHOP_TEXT_MIN..MAX` | **Stale-Kommentar** (§7): liegt strukturell unterhalb der „nicht im Flash"-Grenze, hat aber seit `CS-01` einen NVS-Key |

### E. Funkparameter

| Key          | Member       | Typ (Größe) | Import-Bereich                    | Hinweis             |
| ------------ | ------------ | ----------- | --------------------------------- | ------------------- |
| `node_power` | `node_power` | int         | `TX_POWER_MIN..MAX`, Escape `-20` | **Boot-Clamp** (§6) |
| `node_freq`  | `node_freq`  | float       | 0..1e9                            | **Boot-Clamp** (§6) |
| `node_bw`    | `node_bw`    | float       | 0..500                            | **Boot-Clamp** (§6) |
| `node_sf`    | `node_sf`    | int         | 0..12                             | **Boot-Clamp** (§6) |
| `node_cr`    | `node_cr`    | int         | 0..8                              | **Boot-Clamp** (§6) |

### F. APRS-Freitext & Bitmaske 2

| Key          | Member       | Typ (Größe) | Import-Bereich | Hinweis       |
| ------------ | ------------ | ----------- | -------------- | ------------- |
| `node_atxt`  | `node_atxt`  | string (40) | —              | APRS-Freitext |
| `node_sset2` | `node_sset2` | int         | 0..65535       | Bitmaske      |

### G. Diverses I/O & Zeitzone

| Key           | Member        | Typ (Größe) | Import-Bereich | Hinweis |
| ------------- | ------------- | ----------- | -------------- | ------- |
| `node_owgpio` | `node_owgpio` | int         | 0..99          |         |
| `node_utcof`  | `node_utcoff` | float       | -12..14        |         |

### H. MCP17-I/O-Erweiterung

| Key                            | Member                    | Typ (Größe)                         | Import-Bereich | Hinweis                                  |
| ------------------------------ | ------------------------- | ----------------------------------- | -------------- | ---------------------------------------- |
| `node_mcp17`                   | `node_mcp17io`            | int                                 | —              | unvalidiert (Bitmaske/IO-Konfig)         |
| `node_mcp17o`                  | `node_mcp17out`           | int                                 | —              | unvalidiert                              |
| `node_mcp17i`                  | `node_mcp17in`            | int                                 | —              | unvalidiert                              |
| `node_mcp170` … `node_mcp1715` | `node_mcp17t[0]` … `[15]` | string (16 je Element, 16 Elemente) | —              | 16 Register, ein Text-Label je MCP17-Pin |

### I. Gruppen-Codes

| Key                       | Member                | Typ (Größe) | Import-Bereich | Hinweis                      |
| ------------------------- | --------------------- | ----------- | -------------- | ---------------------------- |
| `node_gcb0` … `node_gcb5` | `node_gcb[0]` … `[5]` | int × 6     | —              | unvalidiert (Gruppennummern) |

### J. Land, Tracking, Präambel

| Key          | Member              | Typ (Größe) | Import-Bereich | Hinweis             |
| ------------ | ------------------- | ----------- | -------------- | ------------------- |
| `node_ctry`  | `node_country`      | int         | 0..20          | **Boot-Clamp** (§6) |
| `node_track` | `node_track_freq`   | float       | 0..1e9         |                     |
| `node_pream` | `node_preamplebits` | int         | 0..1024        |                     |

### K. Softserial

| Key          | Member           | Typ (Größe) | Import-Bereich | Hinweis |
| ------------ | ---------------- | ----------- | -------------- | ------- |
| `node_ss_rx` | `node_ss_rx_pin` | int         | 0..99          |         |
| `node_ss_tx` | `node_ss_tx_pin` | int         | 0..99          |         |
| `node_ss_bd` | `node_ss_baud`   | int         | 0..1000000     |         |

### L. Positions-Intervall, Netzkonsolen-Passwort, BT, Taster

| Key            | Member            | Typ (Größe) | Import-Bereich | Hinweis                                                            |
| -------------- | ----------------- | ----------- | -------------- | ------------------------------------------------------------------ |
| `node_postime` | `node_postime`    | int         | 0..1440        |                                                                    |
| `node_passwd`  | `node_passwd`     | string (15) | —              | **Secret, undokumentiert** (§5): Netzkonsolen-Passwort (Port 2323) |
| `node_sset3`   | `node_sset3`      | int         | 0..65535       | Bitmaske                                                           |
| `bt_code`      | `bt_code`         | int         | 0..999999      | **Secret, dokumentiert** (§5): BLE-Pairing-Code                    |
| `node_bpin`    | `node_button_pin` | int         | 0..99          |                                                                    |

### M. Eigenes Netzwerk (statische IP)

| Key           | Member        | Typ (Größe) | Import-Bereich | Hinweis                                         |
| ------------- | ------------- | ----------- | -------------- | ----------------------------------------------- |
| `node_ownip`  | `node_ownip`  | string (20) | —              |                                                 |
| `node_owngw`  | `node_owngw`  | string (20) | —              |                                                 |
| `node_ownms`  | `node_ownms`  | string (20) | —              | Netzmaske                                       |
| `node_name`   | `node_name`   | string (20) | —              | Hostname                                        |
| `node_webpwd` | `node_webpwd` | string (20) | —              | **Secret, dokumentiert** (§5): Web-GUI-Passwort |

### N. Lokales WLAN

| Key          | Member      | Typ (Größe) | Import-Bereich | Hinweis                                                                 |
| ------------ | ----------- | ----------- | -------------- | ----------------------------------------------------------------------- |
| `node_lssid` | `node_ssid` | string (33) | —              | **Mismatch** (§4)                                                       |
| `node_lpwd`  | `node_pwd`  | string (64) | —              | **Mismatch** (§4), **Secret, dokumentiert** (§5): lokales WLAN-Passwort |

### O. Analogsensor (generisch)

| Key           | Member               | Typ (Größe) | Import-Bereich | Hinweis |
| ------------- | -------------------- | ----------- | -------------- | ------- |
| `node_apin`   | `node_analog_pin`    | int         | 0..99          |         |
| `node_afakt`  | `node_analog_faktor` | float       | —              |         |
| `node_parm`   | `node_parm`          | string (50) | —              |         |
| `node_unit`   | `node_unit`          | string (50) | —              |         |
| `node_format` | `node_format`        | string (50) | —              |         |
| `node_eqns`   | `node_eqns`          | string (50) | —              |         |
| `node_values` | `node_values`        | string (50) | —              |         |
| `node_ptime`  | `node_parm_time`     | int         | 0..1440        |         |

### P. WiFi-Sendeleistung & Relay-Rufzeichen

| Key          | Member            | Typ (Größe) | Import-Bereich | Hinweis |
| ------------ | ----------------- | ----------- | -------------- | ------- |
| `node_wifip` | `node_wifi_power` | int         | 0..100         |         |
| `node_ucall` | `node_lora_call`  | string (10) | —              |         |

### Q. Analogsensor-Kalibrierung

| Key          | Member               | Typ (Größe) | Import-Bereich | Hinweis |
| ------------ | -------------------- | ----------- | -------------- | ------- |
| `node_aak`   | `node_analog_alpha`  | float       | —              |         |
| `node_aslo`  | `node_analog_slope`  | float       | —              |         |
| `node_aoff`  | `node_analog_offset` | float       | —              |         |
| `node_atten` | `node_analog_atten`  | float       | —              |         |

### R. Gateway-Server

| Key          | Member       | Typ (Größe) | Import-Bereich | Hinweis                                   |
| ------------ | ------------ | ----------- | -------------- | ----------------------------------------- |
| `node_gwsrv` | `node_gwsrv` | string (3)  | —              | Länderkürzel, siehe `docs/wire-compat.md` |

### S. Temperatur-Offsets

| Key           | Member           | Typ (Größe) | Import-Bereich | Hinweis |
| ------------- | ---------------- | ----------- | -------------- | ------- |
| `node_tmpiof` | `node_tempi_off` | float       | -50..50        |         |
| `node_tmpoof` | `node_tempo_off` | float       | -50..50        |         |

### T. Strommessung

| Key          | Member       | Typ (Größe) | Import-Bereich | Hinweis |
| ------------ | ------------ | ----------- | -------------- | ------- |
| `node_shunt` | `node_shunt` | float       | 0..10          |         |
| `node_imax`  | `node_imax`  | float       | 0..1000        |         |
| `node_isamp` | `node_isamp` | int         | 0..15          |         |

### U. Eigene Nameserver / Display

| Key             | Member          | Typ (Größe) | Import-Bereich | Hinweis                                                                   |
| --------------- | --------------- | ----------- | -------------- | ------------------------------------------------------------------------- |
| `node_owndns`   | `node_owndns`   | string (20) | —              |                                                                           |
| `node_contrast` | `node_contrast` | int         | 0..255         | Display-Kontrast, kein Netzwerkfeld — Tabellenposition der Quelle folgend |
| `node_ownntp`   | `node_ownntp`   | string (40) | —              |                                                                           |

### V. GPS & Netzmodus

| Key             | Member          | Typ (Größe) | Import-Bereich | Hinweis                                                  |
| --------------- | --------------- | ----------- | -------------- | -------------------------------------------------------- |
| `node_gpsbaud`  | `node_gpsbaud`  | uint32      | 1200..921600   | **Tot** (§3) — kein Reader außerhalb Export/Import/Flash |
| `node_netmode`  | `node_netmode`  | int         | 0..1           |                                                          |
| `node_gpsdebug` | `node_gpsdebug` | int         | 0..2           |                                                          |

### W. Relay & Via

| Key          | Member       | Typ (Größe) | Import-Bereich | Hinweis                                                       |
| ------------ | ------------ | ----------- | -------------- | ------------------------------------------------------------- |
| `node_relay` | `node_relay` | int         | 0..65535       | Bitmaske                                                      |
| `node_via`   | `node_via`   | string (40) | —              |                                                               |
| `node_sset4` | `node_sset4` | int         | 0..65535       | Bitmaske — u. a. `NoPMOther` (`PM-01`, Bit `0x8000`, geplant) |

### X. APRS-Multicast & Ping

| Key             | Member          | Typ (Größe) | Import-Bereich | Hinweis |
| --------------- | --------------- | ----------- | -------------- | ------- |
| `node_aprsmc`   | `node_aprsmc`   | string (10) | —              |         |
| `node_pingtime` | `node_pingtime` | int         | 0..86400       |         |
| `node_pingcall` | `node_pingcall` | string (10) | —              |         |
| `node_pingmax`  | `node_pingmax`  | int         | 0..100         |         |

### Y. Nur ESP32 (6 Register)

| Key            | Member                    | Typ (Größe) | Import-Bereich | Hinweis          |
| -------------- | ------------------------- | ----------- | -------------- | ---------------- |
| `node_disrot`  | `node_disp_rot`           | int         | 0..270         | Display-Rotation |
| `node_spstart` | `node_specstart`          | float       | 0..1e9         | Spektrum-Scan    |
| `node_spend`   | `node_specend`            | float       | 0..1e9         | Spektrum-Scan    |
| `node_spstep`  | `node_specstep`           | float       | 0..1000        | Spektrum-Scan    |
| `node_spsamp`  | `node_specsamples`        | int         | 0..65535       | Spektrum-Scan    |
| `node_bfakt`   | `node_analog_batt_faktor` | float       | —              | Batteriefaktor   |

Auf nRF52 existieren dieselben sechs Member (`node_disp_rot` etc.), liegen dort aber unterhalb der
„nicht im Flash"-Grenze des Structs — kein NVS-Äquivalent (es gibt keinen), also auch nicht Teil
des `config.json`-Exports (Quelle: Kommentar `config_json.cpp:192-199`).

### Z. Nur nRF52 (2 Register)

| Key                | Member             | Typ (Größe) | Import-Bereich | Hinweis                                       |
| ------------------ | ------------------ | ----------- | -------------- | --------------------------------------------- |
| `send_repeat_time` | `send_repeat_time` | uint32      | —              | kein NVS-Pendant auf ESP32; Key = Member-Name |
| `auto_join`        | `auto_join`        | bool        | —              | kein NVS-Pendant auf ESP32; Key = Member-Name |

## 2. Was **nicht** exportiert wird, obwohl persistiert

Aus dem Kommentarblock `src/config_json.h:127-137` sowie der Gate-Entscheidung
`src/config_json.cpp:214-219`, beide gegen den Baum geprüft:

- `node_fversion` / `node_mversion` / `node_fwversion` — Flash-/Firmware-Versionsbuchhaltung; ein
  fremder `node_fversion` im Import würde den nächsten Boot glauben lassen, das gespeicherte
  Layout sei inkompatibel, und den Flash löschen.
- `node_cflash` — einmaliger „beim nächsten Boot löschen"-Trigger, kein Setting; eine importierte
  `1` würde die gerade wiederhergestellten Daten sofort wieder löschen.
- Der T-Deck-Block (`node_map` … `node_wifion`) — Geräte-lokale Anzeige-/Tastatur-/Audio-Präferenz,
  keine portable Konfiguration.
- `node_date_*` / `node_age` / `node_device_eui` / `valid_mark_*` — Uhr- und Flash-Marker, ohne
  NVS-Key auf ESP32.
- Alles unterhalb der „nicht im Flash"-Grenze des jeweiligen Structs (`esp32_flash.h:191`,
  `WisBlock-API.h:352`/`596`).
- **Bewusst per Gate ausgeschlossen** (`config_json.cpp:214-219`): `node_msgid`/`node_ackid` (die
  laufenden Message-ID-Zähler — ein Restore würde sie zurückdrehen und neue Nachrichten mit dem
  Dedup-Ring jedes Nachbarn kollidieren lassen) sowie die letzten Sensormesswerte
  (`node_temp`/`hum`/`press`/`temp2`/`gas`/`co2` — Zustand, keine Konfiguration; zwei Exports
  desselben unveränderten Knotens dürften sich sonst unterscheiden).

## 3. Totes Register: `node_gpsbaud`

```
$ grep -rn "node_gpsbaud" src/
src/config_json.cpp:180   (Export/Import-Tabelle)
src/config_json.cpp:243   (static_assert auf die Feldgröße)
src/esp32/esp32_flash.cpp:290, :567   (NVS load/save)
src/nrf52/WisBlock-API.h:331, :567    (Struct-Deklaration, zwei Kopien)
src/esp32/esp32_flash.h:170           (Struct-Deklaration)
src/nrf52/nrf52_flash.cpp:287         (Struct-Migration alt->neu)
```

Keine Zeile außerhalb von Export/Import/Flash liest `node_gpsbaud`. Die tatsächliche GPS-Baudrate
wird per Auto-Scan über ein festes Array ermittelt:

```
src/gps_functions.cpp:80  static const unsigned long GPS_BAUDS[] =
    {38400, 9600, 115200, 57600, 19200, 4800, 2400, 1200};
```

verwendet an den Aufrufstellen `gps_functions.cpp:218, 221, 542, 640, 842, 844, 1109` — keine davon
liest `meshcom_settings.node_gpsbaud`. Das Register wird korrekt geladen, korrekt gespeichert,
korrekt exportiert und korrekt importiert — und danach nirgends konsultiert. **Löschkandidat, nicht
gelöscht** (außerhalb des Skopes dieses Dokuments; siehe Auftrag).

## 4. Vier Key/Member-Mismatches

Historisch gewachsen, aus `src/config_json.h:33-38` und direkt in der Tabelle sichtbar
(`config_json.cpp:98-99`, `:155-156`):

| JSON-Key       | tatsächlicher C-Member | Bedeutung                   |
| -------------- | ---------------------- | --------------------------- |
| `"node_ssid"`  | `node_ossid`           | die MeshCom-Server-SSID     |
| `"node_pwd"`   | `node_opwd`            | das (Staging-)Passwort dazu |
| `"node_lssid"` | `node_ssid`            | das lokale WLAN             |
| `"node_lpwd"`  | `node_pwd`             | das lokale WLAN-Passwort    |

Der Code-Kommentar begründet das ausdrücklich: die Keys ändern würde jeden bestehenden NVS-Eintrag
brechen, deshalb bleibt der Name im JSON/NVS an den ursprünglichen (vermutlich historisch
vertauschten) Member gebunden. Wer neuen Code gegen diese Register schreibt, muss den JSON-Key vom
C-Member unterscheiden — sie sind bei genau diesen vier Feldern **nicht** dieselbe Zeichenkette.

## 5. Fünf Register mit Geheimnis im Klartext

`src/config_json.h:40-44` dokumentiert im Kommentar **drei**:

> `"node_lpwd"` ist das WLAN-Passwort im Klartext, `"node_webpwd"` das Web-GUI-Passwort, `"bt_code"`
> der BLE-Pairing-Code.

Zwei weitere Register tragen ebenfalls ein Geheimnis im Klartext, sind aber **nicht** in diesem
Kommentar aufgeführt:

- **`node_pwd`** (Member `node_opwd`, JSON-Key wegen §4 `"node_pwd"`) — das Staging-WLAN-Passwort.
  Beim Boot wird es in das aktive `node_pwd` kopiert und danach gelöscht
  (`esp32_main.cpp:982,986`; `nrf52_main.cpp:664,668`) — ein Einmal-Provisionierungsfeld für neue
  WLAN-Zugangsdaten. Es wird an keiner Stelle mit `maskSecret()` behandelt.
- **`node_passwd`** — das Netzkonsolen-Passwort (TCP 2323, `netConsoleSetPassword()`,
  `command_functions.cpp:3191-3213`). Auf dem seriellen Log wird es maskiert
  (`command_functions.cpp:5616`, `maskSecret()`), im `config.json`-Export aber im Klartext
  ausgeliefert (`config_json.cpp:146`, `CFG_STR`, keine Sonderbehandlung).

**Deckung durch die Geheimhaltungswarnung (`CS-03`):** Die Download-Seite warnt den Anwender laut
Kommentar allgemein für „das Konfigurationsfile", nicht Register für Register — die Warnung deckt
also faktisch alle fünf ab, auch die zwei undokumentierten. Das eigentliche Risiko liegt woanders:
`GET /config.json` läuft durch dieselbe Passwortprüfung wie jede andere Webseite
(`web_functions.cpp:246-307`), und die lässt **jeden** Request unmoderiert durch, sobald
`node_webpwd` leer ist:

```c
// src/web_functions/web_functions.cpp:246-307 (gekürzt)
if (strlen(meshcom_settings.node_webpwd) > 0)
{
    ... // IP/Passwort-Prüfung
}
else
    bPasswordOk = true;   // <- kein Passwort gesetzt = jeder Request kommt durch
```

Ein Knoten ohne gesetztes Web-Passwort liefert damit `config.json` — und darin alle fünf
Geheimnisse im Klartext — an **jeden**, der die IP kennt. Das ist der `CS-03`-Befund.

| Register                        | Dokumentiert in `config_json.h`? | Auf Serial maskiert?                                                        |
| ------------------------------- | -------------------------------- | --------------------------------------------------------------------------- |
| `node_lpwd` (Member `node_pwd`) | ja                               | ja (`maskSecret`, `udp_functions.cpp:1165`, `command_functions.cpp:5717`)   |
| `node_webpwd`                   | ja                               | ja (`maskSecret`, `command_functions.cpp:5694`)                             |
| `bt_code`                       | ja                               | nein (numerisch, kein String-Masking-Pfad)                                  |
| `node_pwd` (Member `node_opwd`) | **nein**                         | nein                                                                        |
| `node_passwd`                   | **nein**                         | ja (`maskSecret`, `command_functions.cpp:5616`) — aber nicht im JSON-Export |

## 6. Sechs Register mit strengerem Boot-Zeit-Clamp als ihr Import-Bereich

`configImportJson()` prüft jeden Wert nur gegen die `lo..hi`-Spalte der Tabelle. Beim nächsten Boot
läuft zusätzlich `sanitize_radio_params()` (`src/settings_sanitize.cpp:43-108`) und korrigiert enger
— ein importierter Wert kann also die Import-Prüfung bestehen und trotzdem beim nächsten Boot
stillschweigend verworfen werden:

| Register     | Import-Bereich                                       | Boot-Zeit-Clamp                                                                        | Divergenz                                                                                                               |
| ------------ | ---------------------------------------------------- | -------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| `node_power` | `TX_POWER_MIN..MAX` (board-generisch, z. B. -20..30) | `lim.power_min..power_max` (board-spezifisch, z. B. RAK4631 2..22 laut Code-Kommentar) | Import erlaubt mehr als jedes einzelne Board zulässt                                                                    |
| `node_freq`  | 0..1e9                                               | `lim.freq_min..freq_max` (plausibler Bandbereich)                                      | Import ist absichtlich weiter gefasst als jedes Band                                                                    |
| `node_bw`    | 0..500                                               | genau `{0,125,250,500}` (ESP32) bzw. `{0,1,2}` (nRF52-Index)                           | jeder Wert dazwischen (z. B. 200) passiert den Import und wird beim Boot auf 0 zurückgesetzt                            |
| `node_sf`    | 0..12                                                | `0` oder `6..12`                                                                       | 1..5 passiert den Import, wird beim Boot auf 0 zurückgesetzt                                                            |
| `node_cr`    | 0..8                                                 | `0` oder, je nach `lim.cr_style`, `5..8` (ESP32) bzw. `1..4` (nRF52-Index)             | plattformabhängig — derselbe importierte Wert kann auf ESP32 verworfen werden und auf nRF52 gültig sein, oder umgekehrt |
| `node_ctry`  | 0..20                                                | `0..lim.country_count-1` (tatsächliche Länge der Ländertabelle)                        | Import erlaubt mehr Indizes als die Ländertabelle hat                                                                   |

Alle sechs Korrekturen laufen still (nur `sanitize_log_fn`, kein Import-Fehler) — ein importierter,
aber inkompatibler Wert erzeugt keine Fehlermeldung beim Hochladen, sondern eine leise Änderung
beim nächsten Boot. `node_bw` war der von der Intake genannte Fall; die übrigen fünf sind dieselbe
Klasse Divergenz, in dieser Kampagne zusätzlich gefunden.

## 7. Stale Struct-Kommentar: `max_hop_text`

`max_hop_text` steht in beiden Structs unterhalb der „nicht im Flash"-Markierung —

```
src/esp32/esp32_flash.h:191   // nicht im Flash
src/esp32/esp32_flash.h:209   int max_hop_text = 0;
src/nrf52/WisBlock-API.h:352  // nicht im Flash
src/nrf52/WisBlock-API.h:375  int max_hop_text = 0;
```

— hat aber seit `CS-01` sowohl einen NVS-Key (`esp32_flash.cpp:169,379`) als auch einen Platz in
`CFG_FIELD_LIST` (`config_json.cpp:104`). Der Kommentar an der Struct-Position ist also falsch für
dieses eine Feld; `nrf52_flash.cpp:62-63` dokumentiert das im Code sogar explizit
(„`max_hop_text` liegt bereits in der Struktur, die ganze Struktur wird ohnehin geschrieben").
Wer künftig ein Feld unterhalb der Markierung liest und „also nicht exportiert" annimmt, muss bei
`max_hop_text` gegenprüfen.

## Reproduzierbarkeit

```
sed -n '88,212p' src/config_json.cpp                          # Feldtabelle (Backbone)
awk '/^    X\(/{c++} END{print c}' <(sed -n '89,189p' src/config_json.cpp)   # 101 gemeinsame Felder
grep -rn "node_gpsbaud" src/
grep -n "GPS_BAUDS\[" src/gps_functions.cpp
sed -n '1,141p' src/config_json.h                              # Format, 3 dokumentierte Secrets, Exklusionsliste
grep -n "node_opwd\|node_passwd" src/**/*.cpp src/*.cpp
grep -n "maskSecret" src/**/*.cpp src/*.cpp
sed -n '235,318p' src/web_functions/web_functions.cpp          # CS-03: bPasswordOk=true bei leerem node_webpwd
sed -n '1,129p' src/settings_sanitize.cpp                      # Boot-Zeit-Clamps
grep -n "max_hop_text" src/esp32/esp32_flash.h src/nrf52/WisBlock-API.h src/esp32/esp32_flash.cpp src/nrf52/nrf52_flash.cpp
```

Native Regressionstests für das Format selbst: `test/test_config_json/` (`pio test -e
native_config`) — deckt Export/Import-Rundreise, CRC und Range-Checks ab, nicht die hier
dokumentierten Cross-Cutting-Befunde (totes Register, Mismatches, Secrets, Clamp-Divergenz).

## Quellen

- `src/config_json.{h,cpp}` — Formatdefinition und Feldtabelle
- `src/settings_sanitize.cpp` — Boot-Zeit-Clamps
- `src/esp32/esp32_flash.{h,cpp}`, `src/nrf52/WisBlock-API.h`, `src/nrf52/nrf52_flash.cpp` —
  Struct-Deklarationen und Persistenzpfade
- `src/gps_functions.cpp` — `GPS_BAUDS[]`-Autoscan
- `src/web_functions/web_functions.cpp:235-318` — `CS-03`-Passwortprüfung
- `src/mask_secret.h` — `maskSecret()`
- `docs/wire-compat.md` — Persistenz-Mechanismus (NVS-Key vs. Blob) im Detail
- `docs/BACKLOG.md` §3.8p, `DOC-04` — Intake
