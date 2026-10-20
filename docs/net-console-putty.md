# MeshCom Net Console mit PuTTY (Windows)

Die Net Console ist ein Debug-Zugang über das Netzwerk. Sie zeigt dasselbe Log wie das USB-Kabel, nur über WLAN oder LAN auf TCP-Port 2323. Man kann dort auch Befehle wie `--info` eintippen. Ein USB-Kabel braucht man dafür nicht, und der Node startet beim Verbinden nicht neu.

**Voraussetzungen**

- Ein ESP32-Node (Heltec, T-Beam, T-Deck, E22 …). Der RAK4631 hat keine Net Console.
- Der Node ist im WLAN oder LAN, und du kennst seine IP-Adresse (Web-Oberfläche, Router oder `--info`).

## 1. Net Console einschalten (einmalig)

Gib den Befehl über die App, die Web-Oberfläche oder die serielle Konsole ein:

```
--netconsole on
```

Die Einstellung bleibt nach einem Neustart erhalten. Mit `--netconsole` siehst du den Status, mit `--netconsole off` schaltest du die Konsole wieder ab.

## 2. PuTTY einrichten

1. **Session**
   - Host Name: IP des Nodes, z. B. `192.168.1.50`
   - Port: `2323`
   - Connection type: **Other: Raw** (nicht Telnet, nicht SSH)
2. **Terminal**
   - Haken bei **Implicit CR in every LF** (sonst laufen die Zeilen treppenartig nach rechts)
   - Local echo: **Force on**
   - Local line editing: **Force on**
3. Optional: Unter **Window** die „Lines of scrollback“ auf z. B. `20000` erhöhen.
4. Zurück zu **Session**, einen Namen bei „Saved Sessions“ eintragen (z. B. `MeshCom 2323`) und auf **Save** klicken.
5. Auf **Open** klicken.

## 3a. Ohne Passwort

Es erscheint sofort:

```
OK
MeshCom Console
Type --help for commands
```

Danach läuft das Log mit. Befehle tippst du einfach ein und schickst sie mit Enter ab.

## 3b. Mit Passwort (falls am Node `--passwd` gesetzt ist)

Der Node schickt keine Passwortabfrage, sondern eine Zahlenfolge:

```
NONCE: 3f9a0c…
```

Du musst innerhalb von **30 Sekunden** eine daraus berechnete Antwort zurückschicken. Mit PuTTY allein geht das nicht. Es gibt zwei Wege:

**Weg 1 (einfacher): PowerShell statt PuTTY.** Nimm das Skript [`tools/hmac_connect.ps1`](../tools/hmac_connect.ps1) aus dem Firmware-Repo und starte es so:

```
powershell -ExecutionPolicy Bypass -File hmac_connect.ps1 192.168.1.50 MeinPasswort
```

Das Skript erledigt die Anmeldung und zeigt danach das Log an.

**Weg 2: PuTTY plus PowerShell zum Ausrechnen.** Öffne vorher ein PowerShell-Fenster und kopiere diese Zeile hinein. Ersetze `MeinPasswort` durch dein Passwort, setze die NONCE-Zahl aus PuTTY ein und drücke Enter:

```powershell
$n='HIER_NONCE_EINSETZEN'; $k=[Text.Encoding]::UTF8.GetBytes('MeinPasswort'); $b=[byte[]](0..15|%{[Convert]::ToByte($n.Substring($_*2,2),16)}); [BitConverter]::ToString(([Security.Cryptography.HMACSHA256]::new($k)).ComputeHash($b)).Replace('-','').ToLower()
```

Kopiere das Ergebnis (64 Zeichen) ins PuTTY-Fenster und drücke Enter. Mit Rechtsklick fügst du in PuTTY ein. Danach sollte `OK` erscheinen. Kommt `FAIL`, war das Passwort falsch oder die 30 Sekunden waren um.

## Gut zu wissen

- Es kann sich immer nur **ein** Client verbinden. Eine neue Verbindung wirft die alte raus.
- Das Log kann man in PuTTY mitschreiben lassen: unter **Session → Logging** „All session output“ wählen und eine Datei angeben.
- Wenn keine Verbindung zustande kommt: Stimmt die IP? Ist `--netconsole on` gesetzt? Ist der PC im selben Netz wie der Node?
- Linux/macOS: ohne Passwort `nc <ip> 2323`, mit Passwort `python3 tools/hmac_connect.py <ip> MeinPasswort`.
