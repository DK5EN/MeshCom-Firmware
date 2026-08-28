# Befehlsaenderungen aus PR #1102 und #1103

**Stand:** 2026-08-28 -- **Upstream:** `icssw-org/MeshCom-Firmware`, Branch `dev`
**PRs:** [#1102](https://github.com/icssw-org/MeshCom-Firmware/pull/1102) "Stabilitaets- und
Speichersicherheits-Fixes aus dem Feldbetrieb (82 Aenderungen)" und
[#1103](https://github.com/icssw-org/MeshCom-Firmware/pull/1103) "FWDATE-Puffer zu klein",
beide am 2026-08-27 gemergt (Merge-Commits `d4dee351` und `fc83554e`).

Dieses Dokument listet ausschliesslich, was sich aus Sicht des **Bedieners** geaendert hat:
Befehle auf der seriellen Konsole, ueber BLE, ueber die Netzkonsole und die Konfig-Message
`{SET}`. Die uebrigen rund 70 Aenderungen der beiden PRs (Speichersicherheit, Nebenlaeufigkeit,
Watchdog, Buildflags) sind hier nicht enthalten -- die stehen in `docs/CHANGELOG-stability.md`.

Zeilennummern beziehen sich auf den Fork-Branch `v4.35p_prio` zum oben genannten Stand.

## Uebersicht

| Befehl                | Art       | Aenderung                                                  |
| --------------------- | --------- | ---------------------------------------------------------- |
| `--dfu`               | neu       | nRF52: Neustart in den UF2-Bootloader                      |
| `--txcapture on/off`  | neu       | Rohframe-Mitschnitt der Sendeseite                         |
| `--bmx off`           | Verhalten | schaltet jetzt auch den BME680 ab                          |
| `--symid` / `--symcd` | Verhalten | Praefix mit Leerzeichen, Symboltabellen-Pruefung repariert |
| `--netconsole on`     | Verhalten | Auth nur noch HMAC-SHA256, kein Klartextvergleich          |
| `{SET}`               | Verhalten | `max_hop` wird gegen `0..7` geprueft                       |
| `--info`              | Ausgabe   | Passwoerter maskiert, FWDATE nicht mehr abgeschnitten      |
| `--help`              | Ausgabe   | `--dfu` und `--txcapture` ergaenzt                         |
| GPS-JSON / Webseite   | Ausgabe   | HDOP aus `fposinfo_hdop` statt aus dem veralteten Integer  |

## Neue Befehle

### `--dfu` (nur nRF52 / RAK4631)

Startet den Knoten in den UF2-Bootloader neu; das Board meldet sich danach als USB-Laufwerk
`RAK4631` und laesst sich per Datei-Kopie flashen.

- **Warum:** Der UF2-Bootloader war bisher nur per Doppeldruck auf Reset oder per
  1200-Baud-Touch auf der USB-CDC erreichbar. Beides faellt aus, wenn die CDC-Verbindung
  haengt -- dann blieb nur physischer Zugriff aufs Geraet. Ueber diesen Befehl geht es auch
  per BLE oder Netzkonsole.
- **Ablauf:** Der Befehl sendet zuerst die Quittung (`--dfu now` ueber BLE), setzt dann nur
  das Flag `bEnterDfu` und `rebootAuto = millis() + 2000`. Der Sprung passiert 2 s spaeter im
  Loop, sonst verschluckt der Reset die Quittung.
- **Umsetzung:** `GPREGRET` wird per SoftDevice-SVC auf `0x57` (`DFU_MAGIC_UF2_RESET`)
  gesetzt, danach `NVIC_SystemReset()`. Bewusst **ohne** `enterUf2Dfu()` /
  `sd_softdevice_disable()` -- genau dieser Pfad hat im Loop-Kontext den Haenger `N-19`
  verursacht (CPU stand, USB blieb auf der App-PID).
- **Code:** `src/command_functions.cpp:626`, `src/nrf52/nrf52_main.cpp:2102`
- **Rueckweg:** Nach dem Sprung bleibt das Board im Bootloader, bis geflasht oder Reset
  gedrueckt wird. Auf macOS unterscheidbar per `ioreg -p IOUSB`: `idProduct 0x8029` und
  Produktname `WisCore RAK4631 Board` heisst App laeuft.

### `--txcapture on/off`

Rohframe-Mitschnitt der **Sendeseite** (Gegenstueck zum bestehenden RX-Mitschnitt).

- **Warum eigener Schalter statt an `--loradebug` gehaengt:** Die Empfangsseite laesst man oft
  dauerhaft mitlaufen, die Sendeseite nur fuer gezielte Interop-Messungen -- sie kostet je
  Frame eine weitere rund 550 Zeichen lange Logzeile.
- **Persistenz:** `meshcom_settings.node_sset4`, Bit `0x0008`; `save_settings()` wird im Befehl
  gerufen, der Zustand ueberlebt also den Neustart.
- **Code:** `src/command_functions.cpp:2588` (on) und `:2609` (off), Flag `bTXCAPTURE` in
  `src/capture_functions.h:47`

## Geaendertes Verhalten

### `--bmx off` erfasst jetzt auch den BME680 (`N-28`)

`--bmx` ist das Sammelkommando, und die Hilfe sagt seit jeher `--bmx BME/BMP/680 off`. Der
BME680 wurde davon aber nie erfasst. Wer der Hilfe folgte und danach `--bme on` gab, bekam
`BME680 and BMx280 can't be used together!` und stand ohne Sensor da.

- Nur das Sammelkommando raeumt mit auf. `--bme off` und `--bmp off` meinen weiterhin genau
  ihren Chip.
- Kollateralschaden gibt es keinen: BME680 und BMx280 teilen sich die I2C-Adressen und koennen
  ohnehin nie gleichzeitig aktiv sein.
- **Code:** `src/command_functions.cpp:1925` (loescht zusaetzlich `node_sset2 & 0x0004`)

### `--symid` und `--symcd`

Zwei Fehler in einem Zweig:

1. **Praefix ohne Leerzeichen.** `commandCheck(..., "symid")` traf auch Eingaben, die nur mit
   `symid` beginnen. Jetzt wird `"symid "` bzw. `"symcd "` geprueft.
2. **Invertierte Tabellenpruefung.** Die Bedingung lautete
   `node_symid == '/' || node_symid != '\''` -- der zweite Teil ist fuer jedes Zeichen ausser
   dem Apostroph wahr, die Pruefung akzeptierte also praktisch alles als gueltige
   Primaertabelle. Korrekt ist `node_symid == '/' || node_symid == '\\'`.

- **Code:** `src/command_functions.cpp:3306` und `:3336`

### `--netconsole on`: Authentifizierung nur noch per HMAC

Der Auth-Pfad der Netzkonsole (TCP 2323) hatte vor dem HMAC-Vergleich einen
`memcmp()`-Zweig gegen das Klartextpasswort; wer das Passwort direkt schickte, war drin,
ohne die Challenge zu rechnen. Der Zweig ist entfernt, gueltig ist jetzt ausschliesslich
`HMAC-SHA256(passwort, nonce)` mit Hex-Dekodierung und konstantzeitigem Vergleich.

- Zusaetzlich stand das Passwort im Klartext im `[CON ]`-Log; die Zeile gibt jetzt nur noch
  die Laenge aus.
- Nebenbei repariert: `stopNetConsole()` legte einen **neuen** Mutex an, statt den
  bestehenden zu nehmen, und das `if` vor `::close(s_listen_fd)` klammerte die
  Folgezuweisung nicht mit ein.
- **Code:** `src/net_console.cpp:171` ff.

### `{SET}`: Bereichspruefung fuer `max_hop`

`{SET}` uebernahm die per `sscanf` gelesenen Hop-Werte ohne jede Pruefung. Ein Tippfehler wie
`{SET}44;2;` landete direkt im Hop-Feld der ausgesendeten Pakete. Byte 5 einer ACK fuehrt
`max_hop` in 7 Bit, der Weiterleitungspfad dekrementiert nur und begrenzt nicht nach oben --
ein solcher Knoten haette das Netz mit Paketen geflutet, die 44 statt 4 Relaissprunge weit
laufen.

- Wie bisher wird jedes Feld einzeln uebernommen, sobald `sscanf` es gelesen hat (`{SET}4;`
  setzt weiterhin nur `max_hop_text`). Neu ist ausschliesslich, dass Werte ausserhalb
  `0..MAX_HOP_LIMIT` den bisherigen Wert stehen lassen, statt ihn zu ueberschreiben.
- **Code:** `src/loop_functions.cpp:2183` ff., Konstante `MAX_HOP_LIMIT 7` in
  `src/configuration_global.h:211` (dieselbe Schranke nutzt die ACK-Plausibilitaetspruefung in
  `src/lora_functions.cpp:251`)

## Geaenderte Ausgaben

### `--info`

- **Passwoerter maskiert.** `node_passwd`, `node_webpwd` und `node_pwd` laufen durch
  `maskSecret()`; vorher standen alle drei im Klartext in der Konsolenausgabe -- und damit in
  jedem Log, das jemand zur Fehlersuche weitergibt.
  **Code:** `src/command_functions.cpp:5031`, `:5105`, `:5128`
- **FWDATE vollstaendig (PR #1103).** Der Puffer war `char cfwdate[20]`, gebraucht werden
  mindestens 21 Byte (`__DATE__` 11 + Leerzeichen + `__TIME__` 8 + NUL). `snprintf` schnitt
  die letzte Sekundenstelle ab. Der PR setzt `char cfwdate[24]`.
  **Hinweis:** Im Fork ist dieser Code inzwischen weitergezogen -- `FWDATE` traegt jetzt
  `FLASH_VERSION` (`src/command_functions.cpp:4978`), weil der Zeitstempel-String das
  BLE-JSON ueber die wirksame 245-Byte-Klemmung gehoben hat und `__DATE__`/`__TIME__` ohnehin
  die Uhrzeit des Compilerlaufs ist, nicht der geflashte Stand.

### `--help`

Zwei neue Zeilen: `--dfu` (nur unter `NRF52_SERIES` eingeblendet,
`src/command_functions.cpp:725`) und `--txcapture on/off` in der Debug-Zeile neben
`--loradebug`.

### HDOP im GPS-JSON und auf der Webseite

Beide Ausgaben lasen noch das veraltete Integer `posinfo_hdop` und zeigten damit einen Wert,
der nicht mehr nachgefuehrt wurde. Jetzt kommt der Wert aus `fposinfo_hdop`, auf `int`
gecastet.

- **Code:** `src/command_functions.cpp:5407` (`sendGpsJson()`, geht an die App u. a. nach
  `--pos` und `--utcoff`) und `src/web_functions/web_functions.cpp:899` (Positionsseite des
  Webservers)

## Nicht geaendert, aber gehaertet

Die App-/BLE-Kommandos selbst haben **keine neue Semantik**. Geaendert wurden nur die
Eingangspruefungen, weil die Frames aus einer nicht vertrauenswuerdigen Quelle stammen:

- Textkommando `0xA0`: `msg_len < 2` bricht ab, sonst lief `msg_len - 2` in einen Unterlauf.
- WiFi-Setting: SSID- und Passwortlaenge werden gegen die deklarierte Framelaenge geprueft,
  bevor das jeweils naechste Laengenbyte gelesen wird; statt VLAs aus Fremddaten stehen jetzt
  feste Puffer in Groesse von `node_ssid` / `node_pwd`, die Kopierlaenge ist geklemmt.
- `sendToPhone()`: Laenge, Statusbyte und Nutzdaten werden unter einem einzigen kritischen
  Abschnitt in einen Schnappschuss kopiert (`CONC-18`), `blelen == 0` bricht ab statt zu
  `255` zu unterlaufen.
- Webserver-Setup: MCP-Portangaben werden auf `A0..A7` / `B0..B7` geprueft, bevor daraus eine
  Bitmaske wird.
- **Code:** `src/phone_commands.cpp:75` ff., `:566`, `:627` ff.,
  `src/web_functions/web_setup.cpp:499` ff.
