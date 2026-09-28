# Finding: Passwortschutz der Web-GUI

Datum: 2026-09-05
Branch: fork-main (MeshCom Firmware)

## Ergebnis

Der Passwortschutz ist in der Firmware bereits eingebaut. Er muss nur per Befehl aktiviert
werden; über die Web-GUI selbst lässt sich das Passwort nicht setzen.

## Aktivieren

Über serielle Konsole, BLE-App oder Netz-Konsole (TCP 2323):

```
--webpwd geheim123
```

Wieder abschalten:

```
--webpwd none
```

## Verhalten

- Der Befehl schreibt das Passwort in die Einstellung `node_webpwd` und speichert sie im Flash.
  Maximal 19 Zeichen, das Feld ist 20 Bytes groß.
- Ist das Feld leer, lässt der Webserver jeden Client ohne Login durch. Ist es gefüllt, liefert er
  statt der Startseite eine Login-Seite aus; alle anderen Pfade bekommen HTTP 401.
- Das Browser-Formular schickt das Passwort per GET als `?nodepassword=...` an den Node. Bei
  Treffer wird die Client-IP in einer Tabelle mit zehn Plätzen freigeschaltet.
- Die Freischaltung läuft nach 4 Stunden Inaktivität ab. Ein Logout-Knopf in der Navigation
  schickt ein leeres Passwort und löscht den Eintrag.
- Im JSON-Setup-Export erscheint das Passwort als Feld `WSPWD`.

## Einschränkungen

- Klartextvergleich, Freischaltung pro IP-Adresse, kein Session-Cookie.
- Nur HTTP ohne TLS: das Passwort geht unverschlüsselt und als URL-Parameter durchs Netz. Es ist
  ein Schutz gegen Zufallszugriffe im LAN, nicht mehr.
- Die IP-Tabelle hat zehn Plätze. Clients hinter NAT mit derselben IP sind automatisch mit
  eingeloggt.

## Code-Stellen

| Was                        | Datei                                 | Stelle       |
| -------------------------- | ------------------------------------- | ------------ |
| Passwortprüfung pro IP     | `src/web_functions/web_functions.cpp` | ab Zeile 243 |
| Login-/Logout-Verarbeitung | `src/web_functions/web_functions.cpp` | ab Zeile 588 |
| Login-Seite                | `src/web_functions/web_functions.cpp` | Zeile 1152   |
| Befehl `--webpwd`          | `src/command_functions.cpp`           | Zeile 2484   |
| Flash-Feld (ESP32)         | `src/esp32/esp32_flash.h`             | Zeile 119    |
| Flash-Feld (nRF52)         | `src/nrf52/WisBlock-API.h`            | Zeile 288    |
