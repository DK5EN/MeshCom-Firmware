# RM1-Befehle: vollständige Referenz / RM1 commands: complete reference

Deutsch zuerst, English below (ab Abschnitt "English"). Stand: Firmware-Quelltext `fork-dev`,
26 Befehle / 26 commands.

Die Signatur (HMAC-SHA-256, Schlüsselableitung, Antwort-Tag) steht in [rm1-hmac.md](rm1-hmac.md) und wird hier
nicht wiederholt. Dieses Dokument beschreibt, was in der Zeile steht, was der Knoten antwortet und wann er
ablehnt, damit jede Anwendung alle Befehle selbst implementieren kann.

---

# Deutsch

## 1. Rahmen und Antwort-Grammatik

Ein RM1-Befehl ist der Text einer Direktnachricht an den verwalteten Knoten:

```
RM1 <ctr> <befehl>[ <argumente>] <tag>
```

- `<ctr>`: Dezimalzahl 1..4294967295, ohne führende Nullen. Muss streng größer sein als der höchste bisher
  vom Knoten angenommene Zähler. Ausnahme: `sync` verwendet immer `0` (und `0` ist nur bei `sync` erlaubt).
- `<befehl>`: kleingeschrieben, höchstens 15 Zeichen.
- `<argumente>`: höchstens 39 Zeichen, druckbares ASCII, einfache Leerzeichen, kein Leerzeichen am Anfang oder
  Ende. Großbuchstaben nur dort, wo die Befehlsform es erlaubt (Name, APRS-Text, Rufzeichen, Update-Tag).
- `<tag>`: 16 Kleinbuchstaben-Hexziffern (siehe [rm1-hmac.md](rm1-hmac.md)).
- Einfache Leerzeichen als Trenner; die ganze Zeile muss in eine Direktnachricht passen.

Antwort des Knotens (ebenfalls eine Direktnachricht, signiert):

```
RM1 <ctr> ok <ergebnis> <rtag>
RM1 <ctr> err <ergebnis> <rtag>
```

- `<ctr>` der Antwort ist der Zähler des Befehls (bei `sync`: 0).
- `<ergebnis>` ist höchstens 108 Zeichen lang (`ok ...` oder `err ...` inklusive); die ganze Antwortzeile
  höchstens 140 Zeichen. Nutzdaten der Lesebefehle sind auf 105 Zeichen hinter `ok ` begrenzt.
- `<rtag>`: 16 Hexziffern, Antwort-Tag (siehe [rm1-hmac.md](rm1-hmac.md)). Prüfen Sie ihn, bevor Sie der Antwort
  trauen.
- Nutzbare Zeichen im Ergebnis: Leerzeichen, A-Z a-z 0-9 und `- . / = + _ @ ? ( ) , * #`; alles andere wird vom
  Knoten zu `?`.
- Schreibweise der Werte: `-` bedeutet "nicht vorhanden/nicht bestückt". Zähler werden gekürzt auf höchstens 5
  Zeichen: `0`..`99999`, dann `NNNNk` (Tausender), dann `NNNNM` (Millionen).

### Wann antwortet der Knoten?

| Fall                                                                             | Antwort                             | Zähler verbraucht | Strafpunkt |
| -------------------------------------------------------------------------------- | ----------------------------------- | ----------------- | ---------- |
| Gültiger Tag, Befehl erlaubt                                                     | `ok ...` oder `err ...` des Befehls | ja                | nein       |
| Gültiger Tag, aber Befehl nicht erlaubt / Argument ungültig / verbotenes Zeichen | `err blocked` oder `err range a..b` | ja                | nein       |
| Gleicher Zähler und gleicher Tag wie der letzte angenommene Befehl (10 min)      | wiederholt die letzte Antwort       | nein, nichts neu  | nein       |
| `sync` (Zähler 0)                                                                | `ok ctr=<hwm> v=<version>`          | nein              | nein       |
| Falscher Tag, Formatfehler, Replay (Zähler zu klein), leeres Passwort            | keine Antwort (stumm)               | nein              | ja (siehe) |
| Zu schnell (nur mit strenger Sicherheit), Sperre                                 | keine Antwort (stumm)               | nein              | nein       |

"Gültiger Tag, aber abgelehnt" (`refused`) ist eine **Antwort**, keine stumme Ablehnung: der Absender ist
authentifiziert, also kein Angriffssignal und kein Strafpunkt; der Zähler wird verbraucht, eine Wiederholung
derselben Zeile liefert die zwischengespeicherte Antwort. Wenn der Knoten den Zähler nicht dauerhaft speichern kann,
antwortet er `err storage` und führt nichts aus.

### Ratenbegrenzung und strenge Sicherheit

- Schalter `--rmstrictsecurity on|off` (Einstellung `node_rmstrict`), **Standard: aus**. `status` meldet ihn als
  `rs=0|1`.
- Aus: nur Korrektheitsprüfungen (Format, Erlaubnisliste, Tag, Zähler); keine Sperre, keine Mindestabstände,
  Strafpunkte werden nicht gezählt.
- Ein: mindestens 10 s zwischen zwei angenommenen Befehlen, mindestens 60 s zwischen zwei `sync`; drei
  Ablehnungen eines Absenders (falscher Tag, Formatfehler, `sync` mit Argumenten) innerhalb von 90 s sperren RM1
  für diesen Absender 5 min. Dieselbe Zeile (Zähler + Tag + Befehl) wiederholt zählt nur einmal. Gesperrt oder zu
  schnell: keine Antwort.
- Unabhängig vom Schalter beantwortet der Knoten höchstens ein `sync` je 10 s (Schutz gegen Verstärkung).
- Der Absender sollte selbst höchstens alle 10 s einen Befehl senden.

### Nie erreichbar

Die Erlaubnisliste ist geschlossen und im Knoten fest eingebaut: genau die 26 Befehle unten. Alles andere
antwortet (bei gültigem Tag) `err blocked`, auch diese Konsolenbefehle: `cleanflash`, `ota-update`, `dfu`,
`deepsleep`, `setcall`, `passwd`, `webpwd`, `btcode`, `setssid`, `setpwd`, `wifiset`, `updrepo`, `updchan`,
`autoupdate`, `rm`, `stor`. Jedes Argument (und jeder Befehlsname), das `;`, `{`, `%` oder `--` enthält, wird
ebenfalls mit `err blocked` abgelehnt, egal bei welchem Befehl.

## 2. Fehler-Token

| Token                                                                                                       | Bedeutung                                                                                                                                             |
| ----------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| `err blocked`                                                                                               | Befehl nicht in der Erlaubnisliste, Argumentform falsch, verbotenes Zeichen oder Argument zu lang                                                     |
| `err range <min>..<max>`                                                                                    | nur `txpower`: Wert außerhalb des Bereichs dieser Platine (z.B. `err range 0..22`)                                                                    |
| `err range`                                                                                                 | `pos`: Koordinate/Höhe außerhalb des Wertebereichs                                                                                                    |
| `err text`                                                                                                  | `name`/`atxt`: Text enthält ein verbotenes Zeichen, ist leer oder ein reserviertes Wort; `blockcall`: ungültiges Rufzeichen oder mehr als 16 Einträge |
| `err gps`                                                                                                   | `pos` schreiben, solange GPS eingeschaltet ist                                                                                                        |
| `err failed`                                                                                                | Befehl ausgeführt, aber der Zustand hat sich nicht wie gewünscht geändert / Antwort passt nicht hinein                                                |
| `err unsupported`                                                                                           | die Platine/der Build hat die Funktion nicht (LED, Ladeanzeige, Sensor, Mailbox, Auto-Update)                                                         |
| `err not output`                                                                                            | `setout`: der Pin ist nicht als Ausgang konfiguriert                                                                                                  |
| `err hidden`                                                                                                | `pos` lesen: der Knoten hat keine Position (0/0) und gibt keine preis                                                                                 |
| `err end`                                                                                                   | `mh <zeile>`: Zeilenindex hinter dem Ende der Liste                                                                                                   |
| `err unknown`                                                                                               | `mh <rufzeichen>`: Rufzeichen weder direkt gehört noch als Route bekannt                                                                              |
| `err storage`                                                                                               | der Zähler konnte nicht dauerhaft gespeichert werden; nichts wurde ausgeführt                                                                         |
| `err aubusy`, `err nowifi`, `err stale <tag>`, `err current`, `err noasset`, `err safeboot`, `err notready` | `upd`, siehe dort                                                                                                                                     |

## 3. Befehle

Spalte "Gespeichert": ändert der Befehl dauerhaft gespeicherte Einstellungen (überlebt Neustart)?

| Befehl                                          | Art               | Gespeichert                         |
| ----------------------------------------------- | ----------------- | ----------------------------------- |
| `reboot`                                        | Aktion            | nein                                |
| `status`                                        | Aktion (lesend)   | nein                                |
| `sendpos`, `sendtrack`                          | Aktion            | nein                                |
| `sync`                                          | Aktion (lesend)   | nein                                |
| `gps`,`track`,`display`,`gateway`,`mesh`        | Schalter          | ja                                  |
| `led`                                           | Schalter          | nein (aus nach Neustart)            |
| `txpower`, `setout`                             | Parameter         | ja                                  |
| `chgled`                                        | Parameter         | nein (nächster Start stellt zurück) |
| `radio`,`sens`,`txq`,`mbox`,`maxhop`,`net`,`mh` | Lesen             | nein                                |
| `name`,`atxt`,`pos`,`blockcall`                 | Lesen / Schreiben | Schreiben: ja                       |
| `upd`                                           | Lesen / Aktion    | nein (Einstellungen), siehe unten   |

In den Beispielen steht `<tag>` für den 16-stelligen Tag der Anfrage und `<rtag>` für den der Antwort. Echte
Werte samt Schlüssel stehen in `tools/tests/remote_cmd_vectors.json`.

### 3.1 Aktionen

**`reboot`** - startet den Knoten neu (nach dem Senden der Antwort).
Anfrage: `RM1 7 reboot <tag>`. Argumente: keine (sonst `err blocked`).
Antwort: `RM1 7 ok rebooting <rtag>`. Fehler: keine eigenen.

**`status`** - Kurzzustand des Knotens.
Anfrage: `RM1 2 status <tag>`. Argumente: keine.
Antwort: `ok v=<version> up=<min> bat=<proz> heap=<kB> s=<buchstaben> p=<aktuell>/<min>/<max> rs=<0|1>`

- `v` Firmware-Version inklusive Zusatzbuchstabe (z.B. `4.40a`).
- `up` Laufzeit seit Start in Minuten.
- `bat` Akkuladung in Prozent (0 auch, wenn kein Akku gemessen wird).
- `heap` freier Heap in kB.
- `s` Schalterzustand, feste Reihenfolge `G T D M W L` = gps, track, display, mesh, gateway, led. Großbuchstabe =
  an, Kleinbuchstabe = aus. Der Buchstabe `L` fehlt auf Platinen ohne LED (dann 5 statt 6 Zeichen).
- `p` TX-Leistung in dBm: aktuell/Untergrenze/Obergrenze. Die Grenzen sind die Grenzen, die `txpower` aus der
  Ferne akzeptiert (Untergrenze mindestens 0).
- `rs` 1, wenn die strenge Sicherheit eingeschaltet ist, sonst 0 (Standard). Ältere Firmware liefert `rs` nicht;
  ein Parser sollte das Feld als optional behandeln.

Beispiel: `RM1 2 ok v=4.40a up=417 bat=87 heap=115 s=GtdmW p=14/2/22 rs=0 <rtag>` (GPS an, Gateway an, Rest aus,
keine LED).

**`sendpos`** - sendet sofort eine Positionsbake. Argumente: keine. Antwort: `ok sent`. Der Knoten sendet nur,
wenn er eine Position hat (0/0 wird nie gesendet).
**`sendtrack`** - sendet sofort den Track-Datensatz. Argumente: keine. Antwort: `ok sent`.

**`sync`** - Zähler abgleichen. Zähler **0**, keine Argumente (mit Argumenten: stumm abgelehnt, zählt als
Strafpunkt). Ändert nichts am Knoten und verbraucht den Zähler nicht.
Anfrage: `RM1 0 sync <tag>`. Antwort: `RM1 0 ok ctr=<hwm> v=<version> <rtag>`; `hwm` ist der höchste angenommene
Zähler. Wählen Sie für den nächsten Befehl `hwm+1` oder höher. Ratenbegrenzung siehe oben.

### 3.2 Schalter

Form `<name> on|off`, genau diese beiden Wörter in Kleinbuchstaben, sonst `err blocked`. Die Schalter führen
dieselbe Konsolenfunktion aus wie lokal und speichern die Einstellung (außer `led`).

| Befehl    | Wirkung                             |
| --------- | ----------------------------------- |
| `gps`     | GPS-Empfänger ein/aus               |
| `track`   | Track-Anzeige/-Aufzeichnung ein/aus |
| `display` | Display ein/aus                     |
| `gateway` | Gateway-Funktion ein/aus            |
| `mesh`    | Mesh-Weiterleitung ein/aus          |

Antwort: `ok <name>=on|off`, z.B. `RM1 45 ok gps=on <rtag>`. Fehler: `err failed`, wenn der Zustand nach der
Ausführung nicht dem gewünschten entspricht.

**`led on|off|flash`** - Fern-LED. Gilt bis zum nächsten `led off` oder Neustart (kein Timer, nicht gespeichert);
`flash` blinkt kurz (30 ms) alle 2 s. Antwort: `ok led=on|off|flash`. Auf Platinen ohne nutzbare LED:
`err unsupported`. Hält OneWire den LED-Pin (T-Beam: GPIO4), antwortet der Knoten `err pinbusy` (auch bei `off`). Auch die
nRF52-Platinen RAK4631, T114 und T-Echo unterstützen `led`.

### 3.3 Parameter

**`txpower <n>`** - Sendeleistung in dBm. `n`: ganze Zahl, höchstens 3 Ziffern, keine führende Null, Bereich
`max(0, Platinen-Minimum)` bis Platinen-Maximum (ESP32/nRF52 je Funkchip, z.B. 0..22; den Bereich liefert
`status` im Feld `p=` oder `radio`). Außerhalb oder keine Zahl: `err range <min>..<max>` (z.B. `err range 0..22`).
Wird gespeichert, der Funkchip neu initialisiert, kein Neustart. Antwort: `ok txpower=<n>`; `err failed`, wenn der
Wert nicht übernommen wurde.
Beispiel: `RM1 44 txpower 2 <tag>` -> `RM1 44 ok txpower=2 <rtag>`.

**`setout <pin> <on|off>`** - schaltet einen Pin des I/O-Erweiterers (nur Platinen mit MCP23017). `pin`:
`a0`..`a7` oder `b0`..`b7`. Der Pin muss lokal als Ausgang konfiguriert sein, sonst `err not output`. Wird
gespeichert. Antwort: `ok <pin>=on|off`, z.B. `ok a2=on`. Falsche Form: `err blocked`; Pin schaltet nicht:
`err failed`.

**`chgled auto|on|off`** - Ladeleuchte des Power-Management-Chips (AXP192/AXP2101, z.B. T-Beam) übersteuern:
`auto` = Chip steuert, `on`/`off` erzwingen. Nicht gespeichert; der nächste Start nimmt wieder die lokale
Einstellung. Antwort: `ok chgled=auto|on|off`. Ohne Argument (`chgled`) liest der Befehl den wirksamen Modus:
`ok chgled=<modus>`, mit angehängtem ` rm`, solange eine Fernwartungs-Übersteuerung aktiv ist (z.B. `ok chgled=on rm`).
Platine ohne diesen Chip (oder Chip nicht initialisiert): `err unsupported`.

### 3.4 Lesebefehle

Alle ohne Argumente (`mh` mit genau einem). Jedes Argument bei den argumentlosen: `err blocked`.

**`radio`** - Funkparameter. Antwort: `ok f=<MHz> sf=<n> cr=<n> bw=<kHz> p=<aktuell>/<min>/<max>`.

- `f` Frequenz in MHz mit 3 Nachkommastellen (z.B. `433.175`, `869.525`).
- `sf` Spreizfaktor. `cr` Coderate als Nenner (5 = 4/5, ... 8 = 4/8). `bw` Bandbreite in kHz ohne unnötige
  Nullen (`250`, `62.5`, `31.25`). Auf nRF52 speichert die Firmware Indizes/Hertz; die Antwort enthält immer
  die umgerechneten Einheiten.
- `p` Sendeleistung dBm aktuell/Untergrenze/Obergrenze (wie bei `status`).

Beispiel: `ok f=433.175 sf=11 cr=5 bw=250 p=10/2/22`.

**`sens`** - Sensorwerte. Antwort: `ok t=<°C> h=<%> p=<hPa> t2=<°C>`; Temperatur 1 Nachkommastelle, Feuchte ganze
Prozent, Druck 1 Nachkommastelle, `t2` zweite Temperatur (1-Wire/SHT21). `-` = dieser Wert ist nicht bestückt.
Ist gar kein Sensor aktiv: `err unsupported`. Welcher Wert vorhanden ist, richtet sich nach den eingeschalteten
Sensortreibern: `t` bei BMP280/BME280/BME680/AHT20, `h` bei BME280/BME680/AHT20/SHT21, `p` bei
BMP280/BME280/BME680/BMP390, `t2` bei SHT21/1-Wire. Die Anwesenheit der Werte folgt den
tatsächlich erkannten Sensoren: ein BME280 wird an beiden I2C-Adressen erkannt (`h=` vorhanden), bei einem BMP280
bleibt `h=-`.
Beispiel: `ok t=21.4 h=45 p=1013.2 t2=-`.

**`txq`** - Sendewarteschlange. Antwort: `ok q=<belegt>/<kapazität> bp=<quiet|qrs|qrt> tx=<z> rt=<z> dr=<z> u=<%>`.

- `q` Frames in der Warteschlange/Kapazität. `bp` Gegendruck-Zustand (`quiet` normal, `qrs`/`qrt` gebremst, `?`
  unbekannt).
- `tx` gesendete Frames und `dr` verworfene Frames: Zähler des laufenden Statistikintervalls, keine Summe seit
  Start. `rt` Wiederholungen und `u` Kanalauslastung: derzeit stets `0` (der Knoten führt keinen Wert).

Beispiel: `ok q=3/20 bp=quiet tx=17 rt=0 dr=0 u=0`.

**`mbox`** - Nachrichten-Postfach (Store-and-Forward). Antwort:
`ok m=<off|own|list|heard> u=<belegt>/<plätze> b=<bytes> a=<aktionen>/<max> st=<z> dl=<z> ak=<z> dr=<z> bl=<z> nt=<z>`.
`m` Betriebsart; `u` Plätze belegt/gesamt; `b` gespeicherte Bytes (max. 999999); `a` Postfach-Aktionen in der
letzten Stunde/Stundenobergrenze; `st` gespeichert; `dl` zugestellt; `ak` nach Quittung gelöscht; `dr` verworfen
(Speicherzeit + Kapazität + Plätze); `bl` durch Gegendruck blockiert; `nt` verschickte Hinweise. Build ohne
Postfach: `err unsupported`.
Beispiel: `ok m=heard u=12/50 b=1834 a=3/20 st=40 dl=31 ak=9 dr=0 bl=0 nt=2`.

**`maxhop`** - Hop-Grenzen. Antwort: `ok t=<n> p=<n>`; `t` maximale Hops für Textnachrichten, `p` für
Positionsbaken. Beispiel: `ok t=4 p=2`.

**`net`** - Netzanbindung. Antwort: `ok mtu=<1280..1500> srv=<CC> net=<auto|inet|hamnet> eff=<inet|hamnet|->`.
`mtu` Ethernet-MTU (auf 1280..1500 begrenzt); `srv` Ländercode (2 Buchstaben) des gewählten Gateway-Servers
(Voreinstellung `OE`); `net` eingestellter Modus; `eff` tatsächlich gewählter Weg, `-` ohne IP-Uplink. Auf
nRF52 mit fester IP wird die statische Adresse zur Moduswahl herangezogen.
Beispiel: `ok mtu=1280 srv=OE net=auto eff=hamnet`.

**`mh <zeile | rufzeichen>`** - "Zuletzt gehört" (MHEARD). Der Zeitraum beträgt 3 Stunden; Einträge sind nach
Aktualität sortiert (neueste zuerst). Argument zwingend (ohne: `err blocked`).

1. `mh <zeile>`, Zeilenindex 0..999 (1 bis 3 Ziffern, nur Ziffern). Antwort: Seite der Liste:
   `ok <gesamt> <weiter> RUF min RUF min ...`. `gesamt` Einträge der Liste; `weiter` Index der ersten Zeile, die
   **nicht** mehr in dieser Antwort steht (für die nächste Anfrage), `-` wenn die Liste zu Ende ist; danach Paare
   aus Rufzeichen (Großbuchstaben) und Minuten seit dem letzten Direktempfang. So viele Zeilen, wie in die
   Antwort passen, höchstens 8. Leere Liste, Zeile 0: `ok 0 -`. Zeile hinter dem Ende: `err end`.
   Beispiel: `RM1 71 mh 0 <tag>` -> `RM1 71 ok 12 8 DK5EN-98 3 DL2JA-2 7 ... <rtag>`.
2. `mh <rufzeichen>`, 3 bis 9 Zeichen, Groß- oder Kleinschreibung, optional `-SSID` (1-2 Ziffern). Der Knoten
   sucht zuerst unter direkt gehörten, dann unter bekannten Routen; unbekannt: `err unknown`.
   Direkt gehört: `ok d g=<0|1> m=<0|1> r=<dBm> s=<dB> la=<breite> lo=<länge> di=<km> a=<m> n=<z> x=<z> h=<z> t=<min>`
   - `g` Gateway, `m` Mesh; `r` RSSI dBm, `s` SNR dB; `la`/`lo` Position, 4 Nachkommastellen; `di` Entfernung
     in km, 1 Nachkommastelle (nur mit eigener und fremder Position); `a` Höhe in m; `n` Zahl seiner Nachbarn;
     `x` Nachbarn, die nur er hört; `h` Nachbarn, die er hört; `t` Minuten seit zuletzt gehört. Unbekannt: `-`.
   - Beispiel: `ok d g=1 m=1 r=-95 s=8 la=48.4231 lo=11.7871 di=4.0 a=499 n=15 x=14 h=18 t=0`.

   Nur über Route bekannt: `ok r h=<hops> k=<routen> g=<0|1> m=<0|1|-> rc=<snr>@<ruf> t=<min> v=<via,via,...>`
   - `h` Hops der kürzesten Route; `k` Zahl bekannter Routen; `g` Gateway; `m` Mesh (`-` unbekannt); `rc`
     Relais-SNR dB @ Relais-Rufzeichen (`-` fehlt); `t` Minuten seit zuletzt gehört; `v` Via-Kette (nächster
     zuerst, durch Komma getrennt; `-` keine; eine Kette wird nie in der Mitte eines Rufzeichens gekürzt).
   - Beispiel: `ok r h=3 k=2 g=0 m=- rc=17.8@DL2JA-2 t=3 v=DK5EN-98,DL2JA-2`.

### 3.5 Lesen und Schreiben

Ohne Argument liest der Befehl, mit Argument schreibt er (und gibt danach den gespeicherten Wert zurück).
Schreiben wird gespeichert.

**`name`** - Knotenname. Lesen: `ok n=<name>` (`n=-` wenn leer). Schreiben: `name <text>`.
Text 1..19 Zeichen aus `A-Z a-z 0-9`, Leerzeichen, `- . + _ @ ? ( ) *` sowie `,` und `/`. Kein Leerzeichen am Anfang/Ende,
kein doppeltes Leerzeichen (eine solche Zeile ist schon als Rahmen ungültig und wird stumm verworfen), das Wort
`none` in jeder Schreibweise ist reserviert. Andere Zeichen (auch `: { } | = # ; % \ ~ ` < > " ' &`, Steuerzeichen,
Nicht-ASCII): `err text`. Mehr als 19 Zeichen: `err blocked`. Antwort: `ok n=<gespeicherter name>`;
`err failed`, wenn der gespeicherte Wert vom gesendeten abweicht.
Beispiel: `RM1 66 name Martin <tag>`->`RM1 66 ok n=Martin <rtag>`.

**`atxt`** - APRS-Kommentar (steht in der Positionsbake). Lesen: `ok a=<text>` (`a=-` leer). Schreiben:
`atxt <text>`, 1..39 Zeichen, gleiche Regeln wie `name`, aber ohne `,` und `/`; der Text darf nicht genau `none`
sein (Kleinbuchstaben). Fehler wie bei `name` (`err text`, `err blocked`, `err failed`).
Beispiel: `RM1 68 atxt MeshCom Garten <tag>` -> `RM1 68 ok a=MeshCom Garten <rtag>`.

**`pos`** - Knotenposition. Lesen: `ok <breite> <länge> <höhe> <quelle>`; Breite/Länge vorzeichenbehaftet in Grad,
5 Nachkommastellen, Höhe ganze Meter; Quelle `gps` (GPS mit Fix), `nofix` (GPS ein, kein Fix), `set` (manuell
gesetzt). Ohne Position (0/0): `err hidden`.
Schreiben: `pos <breite> <länge> <höhe>`, drei durch Leerzeichen getrennte Zahlen. Breite/Länge: optionales `-`,
1-3 Vorkommastellen, optional `.` und 1-6 Nachkommastellen, kein `+`, keine Exponentialschreibweise;
Bereich -90..90 und -180..180. Höhe: 1-5 Ziffern, ganze Meter, 0..40000 (keine negativen Höhen). Formfehler:
`err blocked`; Wertebereich verletzt: `err range`; solange GPS eingeschaltet ist: `err gps` (erst `gps off`);
gespeicherter Wert weicht ab: `err failed`. Antwort wie beim Lesen, mit Quelle `set`.
Beispiel: `RM1 70 pos 48.40812 11.73812 492 <tag>` -> `RM1 70 ok 48.40812 11.73812 492 set <rtag>`.

**`upd`** - Firmware-Auto-Update (nur ESP32-Builds mit Auto-Update; sonst überall `err unsupported`).

- `upd` (lesen): `ok m=<notify|auto> c=<prod|dev> n=<0|1> a=<tag|-> st=<tag|-> b=<0|1> g=<0|1> ck=<min|-> e=<text|->`
  - `m` Modus (aus gilt als `notify`); `c` Kanal; `n` 1 = neuere Version bekannt; `a` verfügbarer Tag;
    `st` bereits geladener (bereitgestellter) Tag; `b` 1 = Prüfung/Download läuft; `g` 1 = ein `upd go` wartet;
    `ck` Minuten seit der letzten erfolgreichen Prüfung (`-` seit Start keine); `e` letzter Fehlertext
    (Leerzeichen und `=` als `_`, bei Platzmangel gekürzt).
  - Beispiel: `ok m=notify c=prod n=1 a=v4.41a st=- b=0 g=0 ck=37 e=-`.
- `upd check`: startet eine Versionsprüfung. Antwort `ok checking`. Fehler: `err aubusy` (läuft schon),
  `err nowifi` (keine WLAN-Station), `err unsupported`, `err failed`.
- `upd go <tag>`: lädt und installiert genau diese Version. `tag`: 1..23 Zeichen aus `A-Za-z0-9._-`. Antworten:
  `ok installing <tag>` (Download beginnt), `ok applying <tag>` (schon bereitgestellt, Übergabe/Neustart folgt
  nach der Antwort); Fehler: `err stale <verfügbarer-tag|->` (Tag ist weder der verfügbare noch der bereitgestellte),
  `err current` (nichts Neueres bekannt), `err noasset` (Release ohne Update-Datei), `err safeboot` (Safeboot zu
  alt), `err notready` (keine gültige Uhrzeit, Telefon verbunden oder Akku zu schwach), `err aubusy`, `err nowifi`,
  `err unsupported`.
- Alles andere (`upd foo`, `upd go` ohne Tag, Tag mit `/` o.ä., Tag länger als 23): `err blocked`.
- Ablauf: `upd check` -> mit `upd` den Tag aus `a=` lesen -> `upd go <tag>` -> Fortschritt über `upd` (`b`, `g`,
  `st`, `e`) beobachten.

**`blockcall`** - Rufzeichen-Sperrliste (Frames eines gesperrten Rufzeichens, oder von einem solchen weitergeleitete,
werden verworfen und nicht weitergeleitet; die Liste ist ab Werk leer).

- `blockcall` (lesen): `ok b=<liste>` (`b=-` wenn die Liste leer ist). Die Liste ist durch Komma getrennt und
  normalisiert (Großbuchstaben, `-0` entfällt, führende Null der SSID entfällt). Ist die Liste für die Antwort zu
  lang, wird sie an einer Eintragsgrenze gekürzt und mit `,+N` geschlossen (N = ausgelassene Einträge).
- `blockcall <liste>` (schreiben): ersetzt die GANZE Liste, fügt also nichts hinzu. `none` leert sie. `liste`:
  Rufzeichen durch `,` getrennt, nur die Zeichen `A-Za-z0-9,-`. Ein Rufzeichen ohne SSID (`DK5EN`) sperrt alle
  seine SSIDs, eines mit SSID (`DK5EN-7`) nur diese. Höchstens 16 Einträge, je bis 9 Zeichen einschließlich SSID.
  Doppelte Einträge werden zusammengelegt. Das Argument ist auf 39 Zeichen begrenzt, eine aus der Ferne gesetzte
  Liste fasst daher etwa 3-4 Rufzeichen; längere Listen setzt man an der Konsole (`--blockcall`). Schreiben wird
  gespeichert. Antwort wie beim Lesen, mit der gespeicherten Liste.
- Fehler: `err text` (ungültiges Rufzeichen oder mehr als 16 Einträge, die Liste bleibt unverändert), `err failed`
  (nicht speicherbar), `err blocked` (Zeichen außerhalb von `A-Za-z0-9,-` oder länger als 39 Zeichen).
- Beispiel: `RM1 72 blockcall DK5EN-7,DK5EN-12 <tag>` -> `RM1 72 ok b=DK5EN-7,DK5EN-12 <rtag>`.

---

# English

## 1. Frame and reply grammar

An RM1 command is the text of a direct message to the managed node:

```
RM1 <ctr> <command>[ <args>] <tag>
```

- `<ctr>`: decimal 1..4294967295, no leading zeros. Must be strictly above the highest counter the node has
  accepted so far. Exception: `sync` always uses `0` (and `0` is valid only for `sync`).
- `<command>`: lower case, at most 15 characters.
- `<args>`: at most 39 characters, printable ASCII, single spaces, no leading or trailing space. Capitals only
  where the command's argument shape allows them (name, APRS text, callsign, update tag).
- `<tag>`: 16 lower-case hex digits (see [rm1-hmac.md](rm1-hmac.md)).
- Single spaces as separators; the whole line must fit into one direct message.

The node answers with a signed direct message:

```
RM1 <ctr> ok <result> <rtag>
RM1 <ctr> err <result> <rtag>
```

- The reply `<ctr>` is the command's counter (for `sync`: 0).
- `<result>` is at most 108 characters (including the leading `ok`/`err`), the whole reply line at most 140.
  The payload of read commands is limited to 105 characters after `ok `.
- `<rtag>`: 16 hex digits, the reply tag (see [rm1-hmac.md](rm1-hmac.md)). Verify it before you trust a reply.
- Characters that can appear in a result: space, A-Z a-z 0-9 and `- . / = + _ @ ? ( ) , * #`; the node turns
  anything else into `?`.
- Value conventions: `-` means "absent / not fitted". Counters are shortened to at most 5 characters:
  `0`..`99999`, then `NNNNk` (thousands), then `NNNNM` (millions).

### When does the node answer?

| Case                                                                    | Reply                                | Counter consumed | Strike |
| ----------------------------------------------------------------------- | ------------------------------------ | ---------------- | ------ |
| Valid tag, command allowed                                              | the command's `ok ...` / `err ...`   | yes              | no     |
| Valid tag, but command not allowed / bad argument / forbidden character | `err blocked` or `err range a..b`    | yes              | no     |
| Same counter and same tag as the last accepted command (within 10 min)  | repeats the last reply, no execution | no (already)     | no     |
| `sync` (counter 0)                                                      | `ok ctr=<hwm> v=<version>`           | no               | no     |
| Wrong tag, malformed frame, replay (counter too low), empty password    | no reply (silent)                    | no               | yes    |
| Too fast (strict security only), locked out                             | no reply (silent)                    | no               | no     |

"Valid tag, but refused" is an **answer**, not a silent drop: the sender is authenticated, so it is no attack
signal and earns no strike; the counter is consumed, and a repeat of the same line gets the cached reply. If the
node cannot persist the counter it answers `err storage` and executes nothing.

### Rate limits and strict security

- Switch `--rmstrictsecurity on|off` (setting `node_rmstrict`), **default: off**. `status` reports it as
  `rs=0|1`.
- Off: only the correctness checks (format, allowlist, tag, counter) apply; no lockout, no minimum spacing,
  no strikes are counted.
- On: at least 10 s between two accepted commands, at least 60 s between two `sync`; three rejects from one
  sender (wrong tag, malformed frame, `sync` with arguments) within 90 s lock RM1 for that sender for 5 min.
  Repeating the identical line (counter + tag + command) counts once. Locked or too fast: no reply.
- Independent of the switch, the node answers at most one `sync` per 10 s (amplification guard).
- A sender should not send more than one command per 10 s anyway.

### Never reachable

The allowlist is closed and compiled in: exactly the 26 commands below. Everything else (with a valid tag)
answers `err blocked`, including these console commands: `cleanflash`, `ota-update`, `dfu`, `deepsleep`,
`setcall`, `passwd`, `webpwd`, `btcode`, `setssid`, `setpwd`, `wifiset`, `updrepo`, `updchan`, `autoupdate`,
`rm`, `stor`. Any argument (or command name) containing `;`, `{`, `%` or `--` is refused with `err blocked`
as well, whatever the command.

## 2. Error tokens

| Token                                                                                                       | Meaning                                                                                                                          |
| ----------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| `err blocked`                                                                                               | command not on the allowlist, wrong argument shape, forbidden character or argument too long                                     |
| `err range <min>..<max>`                                                                                    | `txpower` only: value outside this board's range (e.g. `err range 0..22`)                                                        |
| `err range`                                                                                                 | `pos`: coordinate or altitude outside its range                                                                                  |
| `err text`                                                                                                  | `name`/`atxt`: text has a forbidden character, is empty or is a reserved word; `blockcall`: invalid call or more than 16 entries |
| `err gps`                                                                                                   | writing `pos` while GPS is switched on                                                                                           |
| `err failed`                                                                                                | command ran but the state did not change as requested / the reply did not fit                                                    |
| `err unsupported`                                                                                           | the board/build lacks the feature (LED, charge LED, sensor, mailbox, auto-update)                                                |
| `err not output`                                                                                            | `setout`: the pin is not configured as an output                                                                                 |
| `err hidden`                                                                                                | reading `pos`: the node has no position (0/0) and does not reveal one                                                            |
| `err end`                                                                                                   | `mh <row>`: row index past the end of the list                                                                                   |
| `err unknown`                                                                                               | `mh <call>`: callsign neither heard directly nor known as a route                                                                |
| `err storage`                                                                                               | the counter could not be stored; nothing was executed                                                                            |
| `err aubusy`, `err nowifi`, `err stale <tag>`, `err current`, `err noasset`, `err safeboot`, `err notready` | `upd`, see there                                                                                                                 |

## 3. Commands

"Persisted" column: does the command change stored settings (survives a reboot)?

| Command                                         | Kind          | Persisted                |
| ----------------------------------------------- | ------------- | ------------------------ |
| `reboot`                                        | action        | no                       |
| `status`                                        | action (read) | no                       |
| `sendpos`, `sendtrack`                          | action        | no                       |
| `sync`                                          | action (read) | no                       |
| `gps`,`track`,`display`,`gateway`,`mesh`        | toggle        | yes                      |
| `led`                                           | toggle        | no (off after boot)      |
| `txpower`, `setout`                             | parameter     | yes                      |
| `chgled`                                        | parameter     | no (next boot restores)  |
| `radio`,`sens`,`txq`,`mbox`,`maxhop`,`net`,`mh` | read          | no                       |
| `name`,`atxt`,`pos`,`blockcall`                 | read / write  | write: yes               |
| `upd`                                           | read / action | no (settings), see below |

In the examples `<tag>` stands for the 16-digit request tag and `<rtag>` for the reply tag. Real values with
their keys are in `tools/tests/remote_cmd_vectors.json`.

### 3.1 Actions

**`reboot`** - restarts the node (after the reply has been sent).
Request: `RM1 7 reboot <tag>`. Arguments: none (otherwise `err blocked`).
Reply: `RM1 7 ok rebooting <rtag>`. Errors: none of its own.

**`status`** - compact node state.
Request: `RM1 2 status <tag>`. Arguments: none.
Reply: `ok v=<version> up=<min> bat=<pct> heap=<kB> s=<letters> p=<current>/<min>/<max> rs=<0|1>`

- `v` firmware version including the sub-letter (e.g. `4.40a`).
- `up` uptime since boot in minutes.
- `bat` battery charge in percent (also 0 when no battery is measured).
- `heap` free heap in kB.
- `s` switch state in fixed order `G T D M W L` = gps, track, display, mesh, gateway, led. Upper case = on,
  lower case = off. The `L` letter is missing on boards without an LED (5 instead of 6 characters).
- `p` TX power in dBm: current/lower limit/upper limit. The limits are what `txpower` accepts remotely (lower
  limit at least 0).
- `rs` 1 if strict security is on, else 0 (default). Older firmware does not send `rs`; a parser should treat
  the field as optional.

Example: `RM1 2 ok v=4.40a up=417 bat=87 heap=115 s=GtdmW p=14/2/22 rs=0 <rtag>` (GPS on, gateway on, rest off, no
LED).

**`sendpos`** - sends a position beacon now. Arguments: none. Reply: `ok sent`. The node only transmits when it
has a position (0/0 is never sent).
**`sendtrack`** - sends the track record now. Arguments: none. Reply: `ok sent`.

**`sync`** - resynchronise the counter. Counter **0**, no arguments (with arguments: silently rejected, counts as
a strike). Changes nothing on the node and does not consume the counter.
Request: `RM1 0 sync <tag>`. Reply: `RM1 0 ok ctr=<hwm> v=<version> <rtag>`; `hwm` is the highest accepted
counter. Use `hwm+1` or higher for the next command. Rate limits see above.

### 3.2 Toggles

Form `<name> on|off`, exactly these two lower-case words, otherwise `err blocked`. The toggles run the same
console function as local use and store the setting (except `led`).

| Command   | Effect                         |
| --------- | ------------------------------ |
| `gps`     | GPS receiver on/off            |
| `track`   | track display/recording on/off |
| `display` | display on/off                 |
| `gateway` | gateway function on/off        |
| `mesh`    | mesh forwarding on/off         |

Reply: `ok <name>=on|off`, e.g. `RM1 45 ok gps=on <rtag>`. Error: `err failed` when the state after execution is
not the requested one.

**`led on|off|flash`** - remote LED. Held until the next `led off` or reboot (no timer, not stored); `flash` blinks
briefly (30 ms) every 2 s. Reply: `ok led=on|off|flash`. On boards without a usable LED: `err unsupported`. While
OneWire owns the LED pad (T-Beam: GPIO4) the node answers `err pinbusy` (`off` included). The nRF52 boards RAK4631,
T114 and T-Echo support `led` as well.

### 3.3 Parameters

**`txpower <n>`** - transmit power in dBm. `n`: integer, at most 3 digits, no leading zero, range
`max(0, board minimum)` to board maximum (depends on the radio chip, e.g. 0..22; `status` field `p=` and `radio`
report it). Outside or not a number: `err range <min>..<max>` (e.g. `err range 0..22`). Stored; the radio is
re-initialised, no reboot. Reply: `ok txpower=<n>`; `err failed` if the value was not applied.
Example: `RM1 44 txpower 2 <tag>` -> `RM1 44 ok txpower=2 <rtag>`.

**`setout <pin> <on|off>`** - switches a pin of the I/O expander (boards with an MCP23017 only). `pin`: `a0`..`a7`
or `b0`..`b7`. The pin must be configured locally as an output, otherwise `err not output`. Stored. Reply:
`ok <pin>=on|off`, e.g. `ok a2=on`. Wrong shape: `err blocked`; pin did not switch: `err failed`.

**`chgled auto|on|off`** - override the charge LED of the power-management chip (AXP192/AXP2101, e.g. T-Beam):
`auto` = chip decides, `on`/`off` force it. Not stored; the next boot returns to the local setting. Reply:
`ok chgled=auto|on|off`. Without an argument (`chgled`) the command reads the effective mode: `ok chgled=<mode>`,
with ` rm` appended while a remote-management override is active (e.g. `ok chgled=on rm`). Board without that chip
(or chip not initialised): `err unsupported`.

### 3.4 Read commands

All without arguments (`mh` takes exactly one). Any argument on an argument-less command: `err blocked`.

**`radio`** - radio parameters. Reply: `ok f=<MHz> sf=<n> cr=<n> bw=<kHz> p=<current>/<min>/<max>`.

- `f` frequency in MHz with 3 decimals (e.g. `433.175`, `869.525`).
- `sf` spreading factor. `cr` coding rate as denominator (5 = 4/5, ... 8 = 4/8). `bw` bandwidth in kHz without
  trailing zeros (`250`, `62.5`, `31.25`). nRF52 stores indices/hertz internally; the reply always carries the
  converted units.
- `p` TX power dBm current/lower/upper limit (as in `status`).

Example: `ok f=433.175 sf=11 cr=5 bw=250 p=10/2/22`.

**`sens`** - sensor values. Reply: `ok t=<degC> h=<pct> p=<hPa> t2=<degC>`; temperature 1 decimal, humidity whole
percent, pressure 1 decimal, `t2` second temperature (1-Wire/SHT21). `-` = this value is not fitted. If no sensor
is active at all: `err unsupported`. Which value exists follows the enabled sensor drivers: `t` with
BMP280/BME280/BME680/AHT20, `h` with BME280/BME680/AHT20/SHT21, `p` with BMP280/BME280/BME680/BMP390, `t2` with
SHT21/1-Wire. Presence of the values follows the sensors actually detected: a BME280 is recognised at either I2C
address (`h=` present), a BMP280 keeps `h=-`.
Example: `ok t=21.4 h=45 p=1013.2 t2=-`.

**`txq`** - transmit queue. Reply: `ok q=<used>/<capacity> bp=<quiet|qrs|qrt> tx=<c> rt=<c> dr=<c> u=<pct>`.

- `q` frames queued/capacity. `bp` back-pressure state (`quiet` normal, `qrs`/`qrt` throttled, `?` unknown).
- `tx` frames sent and `dr` frames dropped: counters of the running statistics interval, not totals since boot.
  `rt` retransmissions and `u` channel utilisation: currently always `0` (the node keeps no such value).

Example: `ok q=3/20 bp=quiet tx=17 rt=0 dr=0 u=0`.

**`mbox`** - message mailbox (store and forward). Reply:
`ok m=<off|own|list|heard> u=<used>/<slots> b=<bytes> a=<actions>/<max> st=<c> dl=<c> ak=<c> dr=<c> bl=<c> nt=<c>`.
`m` mode; `u` slots used/total; `b` stored bytes (max 999999); `a` mailbox actions in the last hour/hourly
ceiling; `st` stored; `dl` delivered; `ak` purged on acknowledgement; `dr` dropped (storage time + capacity +
slots); `bl` blocked by back-pressure; `nt` notices sent. Build without mailbox: `err unsupported`.
Example: `ok m=heard u=12/50 b=1834 a=3/20 st=40 dl=31 ak=9 dr=0 bl=0 nt=2`.

**`maxhop`** - hop limits. Reply: `ok t=<n> p=<n>`; `t` maximum hops of text messages, `p` of position beacons.
Example: `ok t=4 p=2`.

**`net`** - network attachment. Reply: `ok mtu=<1280..1500> srv=<CC> net=<auto|inet|hamnet> eff=<inet|hamnet|->`.
`mtu` Ethernet MTU (clamped to 1280..1500); `srv` two-letter country code of the selected gateway server
(default `OE`); `net` configured mode; `eff` path actually chosen, `-` without an IP uplink. On nRF52 with a static
IP the static address decides the mode.
Example: `ok mtu=1280 srv=OE net=auto eff=hamnet`.

**`mh <row | call>`** - "recently heard" (MHEARD). The window is 3 hours; entries are sorted by recency
(newest first). The argument is mandatory (without: `err blocked`).

1. `mh <row>`, row index 0..999 (1 to 3 digits, digits only). Reply: one page of the list:
   `ok <total> <next> CALL min CALL min ...`. `total` entries in the list; `next` index of the first row that is
   **not** in this reply (use it for the next request), `-` when the list is exhausted; then pairs of callsign
   (upper case) and minutes since last direct reception. As many rows as fit, at most 8. Empty list, row 0:
   `ok 0 -`. Row past the end: `err end`.
   Example: `RM1 71 mh 0 <tag>` -> `RM1 71 ok 12 8 DK5EN-98 3 DL2JA-2 7 ... <rtag>`.
2. `mh <call>`, 3 to 9 characters, either case, optional `-SSID` (1-2 digits). The node looks first among directly
   heard nodes, then among known routes; unknown: `err unknown`.
   Heard directly: `ok d g=<0|1> m=<0|1> r=<dBm> s=<dB> la=<lat> lo=<lon> di=<km> a=<m> n=<c> x=<c> h=<c> t=<min>`
   - `g` gateway, `m` mesh; `r` RSSI dBm, `s` SNR dB; `la`/`lo` position, 4 decimals; `di` distance in km,
     1 decimal (needs own and remote position); `a` altitude m; `n` its neighbour count; `x` neighbours only it
     hears; `h` neighbours it hears; `t` minutes since last heard. Unknown: `-`.
   - Example: `ok d g=1 m=1 r=-95 s=8 la=48.4231 lo=11.7871 di=4.0 a=499 n=15 x=14 h=18 t=0`.

   Known by route only: `ok r h=<hops> k=<routes> g=<0|1> m=<0|1|-> rc=<snr>@<call> t=<min> v=<via,via,...>`
   - `h` hops of the shortest route; `k` number of known routes; `g` gateway; `m` mesh (`-` unknown); `rc` relay
     SNR dB @ relay callsign (`-` absent); `t` minutes since last heard; `v` via chain (nearest first, comma
     separated; `-` none; a chain is never cut in the middle of a callsign).
   - Example: `ok r h=3 k=2 g=0 m=- rc=17.8@DL2JA-2 t=3 v=DK5EN-98,DL2JA-2`.

### 3.5 Read and write

Without an argument the command reads; with an argument it writes (and returns the stored value afterwards).
Writes are stored.

**`name`** - node name. Read: `ok n=<name>` (`n=-` if empty). Write: `name <text>`.
Text 1..19 characters from `A-Z a-z 0-9`, space, `- . + _ @ ? ( ) *` and `,` `/`. No leading/trailing space, no double
space (such a line is already an invalid frame and silently dropped), the word `none` in any case is reserved.
Other characters (including `: { } | = # ; % \ ~ ` < > " ' &`, control characters, non-ASCII): `err text`. More
than 19 characters: `err blocked`. Reply: `ok n=<stored name>`; `err failed`if the stored value differs from the one
sent.
Example:`RM1 66 name Martin <tag>`->`RM1 66 ok n=Martin <rtag>`.

**`atxt`** - APRS comment (carried in the position beacon). Read: `ok a=<text>` (`a=-` empty). Write:
`atxt <text>`, 1..39 characters, same rules as `name` but without `,` and `/`; the text must not be exactly
`none` (lower case). Errors as for `name` (`err text`, `err blocked`, `err failed`).
Example: `RM1 68 atxt MeshCom Garten <tag>` -> `RM1 68 ok a=MeshCom Garten <rtag>`.

**`pos`** - node position. Read: `ok <lat> <lon> <alt> <source>`; lat/lon signed degrees, 5 decimals, altitude whole
metres; source `gps` (GPS with fix), `nofix` (GPS on, no fix), `set` (set manually). Without a position (0/0):
`err hidden`.
Write: `pos <lat> <lon> <alt>`, three numbers separated by spaces. Lat/lon: optional `-`, 1-3 integer digits,
optional `.` and 1-6 decimals, no `+`, no exponent; range -90..90 and -180..180. Altitude: 1-5 digits, whole
metres, 0..40000 (no negative altitudes). Malformed: `err blocked`; value out of range: `err range`; while GPS
is on: `err gps` (send `gps off` first); stored value differs: `err failed`. Reply as for read, with source `set`.
Example: `RM1 70 pos 48.40812 11.73812 492 <tag>` -> `RM1 70 ok 48.40812 11.73812 492 set <rtag>`.

**`upd`** - firmware auto-update (ESP32 builds with auto-update only; elsewhere always `err unsupported`).

- `upd` (read): `ok m=<notify|auto> c=<prod|dev> n=<0|1> a=<tag|-> st=<tag|-> b=<0|1> g=<0|1> ck=<min|-> e=<text|->`
  - `m` mode (off reads as `notify`); `c` channel; `n` 1 = a newer release is known; `a` available tag;
    `st` already downloaded (staged) tag; `b` 1 = a check or download is running; `g` 1 = a `upd go` is
    pending; `ck` minutes since the last successful check (`-` none since boot); `e` last error text (space
    and `=` shown as `_`, cut when space runs out).
  - Example: `ok m=notify c=prod n=1 a=v4.41a st=- b=0 g=0 ck=37 e=-`.
- `upd check`: starts a version check. Reply `ok checking`. Errors: `err aubusy` (already running), `err nowifi`
  (no WiFi station), `err unsupported`, `err failed`.
- `upd go <tag>`: downloads and installs exactly that release. `tag`: 1..23 characters from `A-Za-z0-9._-`.
  Replies: `ok installing <tag>` (download starts), `ok applying <tag>` (already staged, handover/reboot follows
  after the reply); errors: `err stale <available-tag|->` (tag is neither the available nor the staged one),
  `err current` (nothing newer known), `err noasset` (release has no update file), `err safeboot` (safeboot too
  old), `err notready` (no valid clock, a phone is connected or battery too low), `err aubusy`, `err nowifi`,
  `err unsupported`.
- Everything else (`upd foo`, `upd go` without tag, tag containing `/` etc., tag longer than 23): `err blocked`.
- Flow: `upd check` -> read the tag from `a=` with `upd` -> `upd go <tag>` -> watch progress with `upd` (`b`, `g`,
  `st`, `e`).

**`blockcall`** - call blocklist (frames from a blocked call, or relayed by one, are dropped and not relayed; the
list is empty by default).

- `blockcall` (read): `ok b=<list>` (`b=-` if the list is empty). The list is comma-separated and normalised
  (upper case, `-0` dropped, leading zero of the SSID dropped). A list too long for the reply is cut at an entry
  boundary and closed with `,+N` (N = entries left out).
- `blockcall <list>` (write): replaces the WHOLE list, it does not add. `none` clears it. `list`: calls separated by
  `,`, characters `A-Za-z0-9,-` only. A bare call (`DK5EN`) blocks all its SSIDs, a call with SSID (`DK5EN-7`)
  only that one. At most 16 entries, each up to 9 characters including the SSID. Duplicates are merged.
  The argument is capped at 39 characters, so a list set remotely holds about 3-4 calls; longer lists need the
  console (`--blockcall`). Writes are stored. Reply as for read, with the stored list.
- Errors: `err text` (invalid call or more than 16 entries, the list stays unchanged), `err failed` (could not be
  stored), `err blocked` (a character outside `A-Za-z0-9,-`, or longer than 39 characters).
- Example: `RM1 72 blockcall DK5EN-7,DK5EN-12 <tag>` -> `RM1 72 ok b=DK5EN-7,DK5EN-12 <rtag>`.
