# Release Notes -- MeshCom Firmware v4.35n_20260315_fix1 (2026-03-15)

Bugfixes und Verbesserungen auf Basis von `oe1kbc_v4.35p`. Diese Version
laeuft als "U-Boot" (Vorab-Test) vor der offiziellen v4.35p.

---

## Aenderungen seit v4.35n_20260315

### GPS_REFRESH_INTERVAL auf 5s zurueckgesetzt
- Die Aenderung auf 10s war nicht upstream-konform. Zurueck auf 5s um keine unnoetige Differenz zum Upstream zu erzeugen.
- **Betroffene Datei**: `src/configuration_global.h`

### sendMheard MESH/NCNT Feature uebernommen
- Kurts Weiterentwicklung in `sendMheard()` uebernommen: Beim BLE-Reconnect sendet die Firmware jetzt auch MESH- und NCNT-Felder (getValue Felder 9+10) im JSON an die Phone-App. Zuvor wurden diese Felder bei uns als Bug geflagged — nach Analyse ist es ein sinnvolles Feature.
- **Betroffene Datei**: `src/mheard_functions.cpp`

---

## Bugfixes

### 1. MHeard memcpy Bounds-Check und strcmp→memcmp
- **Ursache**: `memcpy` in `updateMheard()` kopierte `mh_callsign.length()` Bytes ohne Bounds-Check in den 10-Byte `mheardCalls`-Slot. Bei Callsigns >= 10 Zeichen wurde ueber die Slot-Grenze geschrieben und der Nachbar-Eintrag korrumpiert. Zusaetzlich verglich `strcmp()` an 3 Stellen ohne Laengenbegrenzung — bei nicht sauber terminierten Buffer-Eintraegen konnte ueber die Slot-Grenze hinausgelesen und der falsche Eintrag gematcht werden.
- **Auswirkung**: Bug-Report Rainer (2026-03-15): Gespeicherte MHeard-Eintraege zeigten falsche HW-Typen (z.B. Heltec V3 als TLORA_V2) und gemixte Callsigns. Frische Eintraege waren korrekt.
- **Fix**: `memcpy` mit `min(length, sizeof-1)` begrenzt. `strcmp` durch `memcmp` mit expliziter max-length (`ivgll`) ersetzt — die Laengenberechnung existierte in `updateHeyPath` bereits, wurde aber nicht genutzt.
- **Betroffene Datei**: `src/mheard_functions.cpp`

### 2. MHeard ncount-Quelle, Comma-Parsing und 12h-Zeitfenster
- **Ursache 1**: `mheardNCount[ipos]` in `updateMheard()` und `updateHeyPath()` nutzte den stale Array-Wert statt des aktuellen Paket-Werts. Bei neuen Eintraegen war der Wert 0 oder stammte von einem frueheren Node.
- **Ursache 2**: Comma-Check in `updateHeyPath()` blockierte gueltige Path-Payloads die Kommas enthielten und verhinderte still den NCount-Update.
- **Ursache 3**: `getMheardCount()` zaehlte nur Nodes der letzten Stunde (60*60). Nodes die alle 30min eine Position senden wurden nach 60min nicht mehr gezaehlt.
- **Fix**: `mheardLine.mh_ncount` (Wert aus aktuellem Paket) statt `mheardNCount[ipos]`. Comma-Check-Block entfernt. Zeitfenster auf 12h erweitert (60*60*12), konsistent mit MHeard-Expiry. MESH/NCNT Felder aus `sendMheard()` JSON entfernt (unzuverlaessige Legacy-Daten).
- **Betroffene Datei**: `src/mheard_functions.cpp`

### 3. ExtUDP Null-Pointer-Schutz und Socket-Reset-Absicherung
- **Ursache**: `inputJson["dst"]` gibt `nullptr` zurueck wenn das JSON-Feld fehlt. Die direkte Zuweisung an `String` ist undefiniertes Verhalten. Nach `resetExternUDP()` in `sendExtern()` fiel die Ausfuehrung durch zu `endPacket()` auf dem geschlossenen Socket.
- **Auswirkung**: Absturz bei fehlerhaftem JSON von externen Quellen. Undefiniertes Verhalten nach Socket-Reset.
- **Fix**: Null-Checks und Laengenvalidierung fuer `dst` (max 9) und `msg` (max 150) vor Verarbeitung. `return` nach `resetExternUDP()` in beiden Write-Bloecken.
- **Betroffene Datei**: `src/extudp_functions.cpp`

### 4. APRS LoRa-APRS IGate-Text entfernen
- **Ursache**: Jedes LoRa-APRS Compressed Position-Paket enthielt den IGate-Text (`node_atxt` bzw. "(MeshCom)"). Der IGate identifiziert sich bereits ueber sein Callsign im APRS-Header.
- **Auswirkung**: Unnoetig grosse Pakete, mehr Airtime pro Position.
- **Fix**: `msgtext`-Block auskommentiert, `snprintf` ohne `%s`/`msgtext.c_str()`.
- **Betroffene Datei**: `src/aprs_functions.cpp`

### 5. POSINFO-Logs mit Zeitstempel fuer Log-Analyse
- **Ursache**: Die `[POSI]`-Log-Ausgaben in `setSMartBeaconing()` enthielten keinen Zeitstempel. Bei der Log-Analyse waren Speed-, Distance- und Rate-Entscheidungen nicht zeitlich einordbar.
- **Fix**: Tag von `[POSI]` auf `[POSINFO]` vereinheitlicht. Alle 7 `printf`-Stellen um `getTimeString()` Prefix erweitert.
- **Betroffene Datei**: `src/loop_functions.cpp`

### 6. sendPosition Magic-Values durch klare Konstanten ersetzen
- **Ursache**: `sendPosition()` nutzte `unsigned long` und Magic-Values `0x9999` (sofort senden) und `0xEEEE` (mit Telemetrie). Diese kollidierten konzeptionell mit echten Intervallwerten und waren auf Plattformen mit 16-Bit `unsigned int` problematisch.
- **Fix**: Signatur auf `unsigned int` geaendert. Klare Semantik: `0` = sofort senden, `1` = mit Telemetrie. Aufrufer in `command_functions.cpp` angepasst.
- **Betroffene Dateien**: `src/loop_functions.h`, `src/loop_functions.cpp`, `src/command_functions.cpp`

### 7. GPS_REFRESH_INTERVAL von 5s auf 10s
- **Ursache**: GPS-Abfrage alle 5 Sekunden erzeugte unnoetige CPU-Last. Smart Beaconing arbeitet mit Distanz- und Richtungsaenderungen, nicht mit der GPS-Polling-Frequenz.
- **Fix**: `GPS_REFRESH_INTERVAL` von 5 auf 10 Sekunden.
- **Betroffene Datei**: `src/configuration_global.h`

### 8. Audio Log-Tag [audi] zu [audio]
- **Ursache**: Alle anderen Module nutzen vollstaendige Tags (`[CSMA]`, `[LOOP]`, `[POSINFO]`). Das abgekuerzte `[audi]` wurde von Log-Filtern nicht gefunden.
- **Fix**: Suchen-Ersetzen `[audi]` → `[audio]` in allen 16 Log-Ausgaben.
- **Betroffene Datei**: `src/esp32/esp32_audio.cpp`

### 9. MHeard/Path-Persistence Size-Check gegen Datenkorruption
- **Ursache**: Nach der MAX_MHEARD-Aenderung (20→120) hatten alte `mheard.dat`-Dateien auf SD eine andere Groesse als die neuen Arrays. `loadMHeardPersistence()` las ohne Groessencheck — die Byte-Grenzen stimmten nicht mehr, die gesamte MHeard-Tabelle wurde korrumpiert. Gleiches Problem bei `mhpath.dat` (MAX_MHPATH 30→150).
- **Auswirkung**: Bug-Report Rainer: Falsche HW-Typen und gemixte Callsigns bei gespeicherten Eintraegen nach Firmware-Update (T-DECK mit SD-Persistence).
- **Fix**: File-Size-Check vor dem Laden. Erwartete Groesse wird aus `sizeof()` der aktuellen Arrays berechnet. Bei Mismatch wird die alte Datei geloescht — die Tabelle wird frisch aus eingehenden Paketen aufgebaut.
- **Betroffene Datei**: `src/mheard_functions.cpp`

### 10. CONFFIN nach Config-Commands im gleichen Buffer senden
- **Ursache**: Beim BLE-Connect wurde CONFFIN (Config-Finish-Signal an die Phone-App) ueber `addBLEOutBuffer()` in den regulaeren toPhone-Buffer geschrieben, waehrend die Config-JSONs ueber `addBLEComToOutBuffer()` in den priorisierten ComToPhone-Buffer gingen. Da ComToPhone immer Vorrang hat, wurde CONFFIN erst nach allen Config-JSONs gesendet oder ging bei vollem Buffer verloren. Auf NRF52 wurde CONFFIN zusaetzlich direkt im BLE connect_callback aufgerufen (BLE-Task-Kontext statt Main-Loop).
- **Auswirkung**: Phone-App erhielt CONFFIN verzoegert oder gar nicht. Config-Sequenz wurde als unvollstaendig interpretiert.
- **Fix**: `sendConfigFinish()` nutzt jetzt `addBLEComToOutBuffer()`. CONFFIN wird nach allen Config-Commands und `sendMheard()` eingereiht (nicht mehr im connect_callback / connect_pending). Reihenfolge garantiert: Config-JSONs → MHeard → CONFFIN, alles im ComToPhone-Buffer.
- **Betroffene Dateien**: `src/nrf52/nrf52_ble.cpp`, `src/nrf52/nrf52_main.cpp`, `src/esp32/esp32_main.cpp`, `src/command_functions.cpp`
