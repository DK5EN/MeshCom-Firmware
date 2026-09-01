# Rueckstau und Sendequittung — Protokollreferenz

Stand: 2026-09-01, Firmware `4.35p`, Build `20260831`
Code: `src/backpressure.h`, `src/bp_notice_frame.h`, `src/extern_notice_json.h`,
`src/loop_functions.cpp` (Abschnitt "BP-01 ... back-pressure to the sender")
Entstehung und Review: `docs/archive/` (`bp-l1-l4-impl-plan-20260901.md`,
`bp-advisor-verdict-20260901.md`, `backpressure-flow-control-20260901.md`)

Dieses Dokument beschreibt ausschliesslich den **ausgelieferten Stand**. Es richtet sich an
Nutzende (Kapitel 1-2), an die Entwicklung von Chat-Apps (Kapitel 3-4) und an alle, die
ueberlegen, wie es weitergehen soll (Kapitel 5).

Alle Zahlen in Kapitel 2 stammen aus einem Benchlauf vom 2026-09-01 auf echter Hardware, nicht
aus der Theorie. Der Aufbau steht am Ende von Kapitel 2.

---

## 1. Ausgangslage: warum es das gibt

### Der Sendepuffer

Ein MeshCom-Node haelt seine ausgehenden Frames in einem Ringpuffer, `MAX_RING`, auf allen
aktuellen Boards 20 Plaetze. LoRa ist langsam: im Benchlauf brauchte ein Textframe bei SF11 /
250 kHz **rund 3,9 Sekunden** von der Einreihung bis zur Luft. Wer schneller tippt, als der
Funk abfliesst, fuellt diesen Ring.

Dazu kommt etwas, das man leicht uebersieht: **eine angenommene Nachricht belegt ihren Platz
nicht nur einmal.** MeshCom wiederholt Textnachrichten. Im Benchlauf standen 36
Wiederholungen gegen 15 urspruengliche Nachrichten, und der Ring war 210 Sekunden nach dem
Ende des Sendens immer noch zu elf von zwanzig Plaetzen belegt. Der Ring leert sich also
deutlich langsamer, als die reine Sendezeit vermuten laesst.

### Was frueher passierte

Bis zu dieser Aenderung warf `sendMessage()` den Rueckgabewert der Ring-Einreihung weg. Daraus
folgten vier Probleme, die sich gegenseitig verdeckten:

**Eine abgewiesene Nachricht verschwand lautlos.** Der Node hatte zwar eine Warnung
("QRT — ich nehme nichts mehr an"), aber die kam genau einmal pro Episode und war an den
Zustandswechsel gebunden, nicht an eine konkrete Nachricht. Der Code, der die Abweisung selbst
haette melden sollen, war beweisbar tot: `onRefuse()` fragte den Episoden-Latch, und der stand
zum Zeitpunkt jeder Abweisung immer schon hoch genug. Der Rueckgabewert war konstant "nichts
zu melden". Ein Test bewachte das Verhalten und blieb gruen, weil er eine Meldung zaehlte, die
aus einem ganz anderen Aufruf stammte.

**Die Meldung nannte die betroffene Nachricht nicht.** Sie war eine Zustandsmeldung ueber den
Puffer, keine Quittung ueber eine Nachricht. Wer waehrend eines Staus fuenf Nachrichten
tippte, erfuhr, dass der Puffer voll ist — aber nicht, welche fuenf verloren waren.

**Eine verworfene Nachricht sah aus wie gesendet.** Das Echo an die App ging raus, _bevor_ der
Sendepuffer ueberhaupt gefragt wurde. Verwarf der Ring den Frame anschliessend, stand die
Nachricht trotzdem im Verlauf, als sei alles gut. Auf einem Gateway ging obendrein der Status
"Server erreicht" mit.

**Der getippte Text ging verloren.** Weil `sendMessage()` nichts zurueckgab, konnte keine
Oberflaeche auf den Ausgang reagieren. T-Deck und Web-GUI leerten ihr Eingabefeld unbedingt.
Nach einer Abweisung war der Text weder im Verlauf noch im Eingabefeld — nur noch im Kopf des
Absenders.

### Warum das mehr als Kosmetik ist

Im Notfallverkehr ist eine still verschluckte Nachricht schlimmer als eine sichtbar
abgelehnte. Wer weiss, dass etwas nicht ankam, wiederholt es oder weicht auf einen anderen Weg
aus. Wer es nicht weiss, verlaesst sich auf eine Zustellung, die nie stattgefunden hat.

---

## 2. Wie es jetzt funktioniert

### Die Grundregel

> **Auf jede gesendete Nachricht kommt genau eine Zeile zurueck** — entweder das Echo oder
> eine Quittung, die sagt, warum es keins gibt.

### Die vier Q-Codes

Zustandsmeldungen ueber den Puffer, gelatcht: **eine pro Episode**, nicht eine pro Nachricht.

| Code    | Ausloeser                                      | Nimmt der Node an?             | Text                                                    |
| ------- | ---------------------------------------------- | ------------------------------ | ------------------------------------------------------- |
| **QRS** | Ringtiefe erreicht 5                           | ja                             | `QRS - slow down, TX buffer is filling`                 |
| **QRT** | Ringtiefe erreicht 80 % von `MAX_RING` (16/20) | **nein**, lokal erzeugte Texte | `QRT - stopping to accept new messages, TX buffer full` |
| **QTA** | Der Ring hat einen Frame verworfen             | nein                           | `QTA - message discarded, TX buffer full`               |
| **QRV** | Tiefe 0, oder Tiefe 1 fuer 10 s ununterbrochen | ja                             | `QRV - ready again, TX buffer clear`                    |

QRS liegt bewusst bei festen 5 und nicht bei einem Anteil von `MAX_RING`: eine
5,5-Minuten-Messung auf einem Gateway zeigte im Normalbetrieb eine Grundlast von Tiefe 1-4.
Eine Schwelle darunter meldet staendig Stau, wo keiner ist.

### Die Quittung

Zusaetzlich und **nicht gelatcht**: pro tatsaechlich verlorener Nachricht eine Zeile mit ihrem
Text und dem Grund als Q-Code davor.

```
QRT NOT SENT - Hello World 16      abgewiesen, der Ring stand im QRT-Band
QTA NOT SENT - Hello World 16      angenommen, dann vom Ring verworfen
```

Der Text wird auf **120 Byte** gekuerzt und dann mit `...` markiert, auf jedem Transportweg
gleich. Es wird nie mitten in eine UTF-8-Sequenz geschnitten. `"`, `\` und Steuerzeichen
werden durch Leerzeichen ersetzt.

> **120 Byte, nicht 120 Zeichen.** Bei ASCII dasselbe, bei deutschen Umlauten rund 105
> sichtbare Zeichen, bei Emoji rund 30.

### Nur lokal Getipptes wird abgewiesen

Relaisverkehr, ACKs und Baken laufen nicht durch `sendMessage()` und werden nie abgewiesen.
Ein verstopfter Node bleibt ein funktionierendes Relais, waehrend der flutende Nutzer gebremst
wird.

Die Abweisung passiert **vor jedem Seiteneffekt**: keine Message-ID, kein Flash-Schreibvorgang,
kein Eintrag im Eigen-TX- oder Dedup-Speicher. Eine abgewiesene Nachricht hinterlaesst nichts
ausser der Logzeile und der Quittung.

### Nichts geht halb raus

Verwirft der Ring einen Frame, passiert **gar nichts weiter**: kein Echo, kein UDP-Uplink zum
zentralen Server, kein EXTUDP-Spiegel, keine Dedup-Buchung. Was nicht ueber LoRa gesendet
werden kann, geht auch nicht ins Backbone — eine Nachricht betritt das Netz ganz oder gar
nicht.

Fuer Gateway-Betreiber ist das eine Verhaltensaenderung: eine lokal getippte Nachricht, deren
Frame der TX-Ring verwirft, erscheint nicht mehr im Netz.

### Der getippte Text bleibt stehen

`sendMessage()` liefert ein Ergebnis:

| Wert | Bedeutung                                                                 |
| ---- | ------------------------------------------------------------------------- |
| `0`  | angenommen                                                                |
| `-1` | abgewiesen (QRT)                                                          |
| `-2` | verworfen (QTA)                                                           |
| `-3` | nicht sendbar: Laenge, DM ans eigene Rufzeichen, Rufzeichen nicht gesetzt |

T-Deck, T-Deck Pro und Web-GUI leeren ihr Eingabefeld nur noch bei `0`. Der **Wechsel in die
Nachrichtenansicht passiert dagegen immer** — waere er an den Erfolg gebunden, bliebe der
Operator auf dem Eingabefeld stehen, waehrend die Quittung in die Nachrichtenliste geschrieben
wird, und saehe von der Abweisung nichts.

### Nicht jede Ablehnung ist Rueckstau

Der Sendepuffer lehnt auch aus Gruenden ab, die mit Fuellstand nichts zu tun haben: ein Node
mit werksseitigem Rufzeichen darf ueberhaupt nicht senden, und eine Nachricht mit unmoeglicher
Laenge wird abgewiesen. Diese Faelle erzeugen **keinen** Zustandswechsel und **keine**
Quittung — sonst meldete ein fabrikfrischer Knoten pro Tastendruck "Puffer voll" bei leerem
Ring. Sie hinterlassen nur eine eigene Logzeile.

---

### Der gemessene Ablauf

Aufbau: DK5EN-93 (Heltec V3, `MAX_RING 20`, also QRS bei 5 und QRT bei 16) sendet ueber
EXTUDP 25 Nachrichten im Abstand von 300 ms an die Gruppe `TEST`. Mithoerer DK5EN-14
(T-Deck Plus), DK5EN-92 (T-Beam v1.2), DK5EN-90 (RAK4631) und DK5EN-98 (Heltec V3, grosse
Antenne). **Auf allen fuenf Knoten war Gateway ausgeschaltet**, damit nichts ueber den
zentralen Server laufen kann und jeder Empfang ein echter Funkempfang ist. Alle fuenf auf
Build `20260831`.

Was der Absender zurueckbekam, in echter Reihenfolge:

```
Senden: Hello World 01   ->  Echo: "Hello World 01{803"
Senden: Hello World 02   ->  Echo: "Hello World 02{804"
Senden: Hello World 03   ->  Echo: "Hello World 03{805"
                             Echo: "QRS - slow down, TX buffer is filling"
Senden: Hello World 04   ->  Echo: "Hello World 04{806"
   ...
Senden: Hello World 14   ->  Echo: "Hello World 14{816"
                             Echo: "QRT - stopping to accept new messages, TX buffer full"
Senden: Hello World 15   ->  Echo: "Hello World 15{817"
Senden: Hello World 16   ->  Echo: "QRT NOT SENT - Hello World 16"
Senden: Hello World 17   ->  Echo: "QRT NOT SENT - Hello World 17"
   ...
Senden: Hello World 25   ->  Echo: "QRT NOT SENT - Hello World 25"
```

**15 angenommen, 10 abgewiesen, 25 Rueckmeldungen — keine Nachricht ohne Antwort.**

Das `{803` am Ende der Echos ist die ACK-Anforderung, die der Node bei einer gerichteten
Nachricht anhaengt. Der Echo-Text ist deshalb nicht byteidentisch mit dem Getippten.

Auf der Konsole des Absenders:

```
[BP];notice;QRS;depth;5;max;20;ms;35887
[BP];notice;QRT;depth;16;max;20;ms;39353
[BP];refuse;depth;16;max;20;ms;39509
[BP];nack;QRT;dst;TEST;ms;39509
   ... 10x refuse + 10x nack ...
```

QRS bei Tiefe exakt 5, QRT bei exakt 16 — genau die Schwellen. Eine QRS und eine QRT fuer die
ganze Episode, aber zehn Quittungen fuer zehn verlorene Nachrichten.

**Der Funknachweis.** Von den 25 Nachrichten kamen bei DK5EN-90 und bei DK5EN-98 **genau
01 bis 15** an und **keine einzige von 16 bis 25**. Zwei unabhaengige Empfaenger, Gateway auf
allen Knoten aus, also reiner Funkweg. In einem zweiten, kuerzeren Lauf mit eingeschaltetem
LoRa-Debug bestaetigten auch DK5EN-14 und DK5EN-92 den Empfang. Die 15 Frames brauchten von
43,4 s bis 102,3 s auf die Luft, also rund 3,9 Sekunden pro Frame.

**Eine Beobachtung, die fuer die App-Seite wichtig ist.** In keinem der beiden Laeufe kam
innerhalb des Beobachtungsfensters ein QRV — 131 s im ersten, 251 s im zweiten. Der Ring stand
am Ende des zweiten Laufs immer noch bei elf von zwanzig Plaetzen, weil die angenommenen
Nachrichten weiter wiederholt wurden und ihre Plaetze behielten. **Die Entwarnung kann auf
einem beschaeftigten Knoten Minuten dauern.** Eine App sollte ihre Sendesperre deshalb nicht
allein am QRV aufhaengen.

---

## 3. Drahtformat

### BLE und Web

Meldungen und Quittungen sind normale Textnachrichten-Frames unter dem **eigenen Rufzeichen
des Nodes**. Ein Pseudo-Absender waere kein gueltiges Rufzeichen und landete in McApp in der
Spam-Klasse, wo niemand ihn sieht.

```
Byte  0      0x3A                        payload_type ':' (Textnachricht)
Byte  1..4   msg_id, little endian
Byte  5      Flags | max_hop
               0x80  msg_server
               0x40  msg_track
               0x20  msg_app_offline     << bei Meldung und Quittung immer gesetzt
               0x10  bMESH
               0x0F  max_hop
Byte  6..n   "<node_call>><dst>:"        ASCII, z. B. "DK5EN-93>TEST:"
Byte  n+1..  Text
```

### EXTUDP

JSON auf Port 1799, geformt wie eine ueber LoRa empfangene Nachricht. Ein eigener Typ war in
der ersten Fassung schlicht unsichtbar fuer McApp — die App rendert nur, was sie kennt.

Echo einer angenommenen Nachricht (`src_type` ist `node`):

```json
{
  "src_type": "node",
  "type": "msg",
  "src": "DK5EN-93",
  "dst": "TEST",
  "msg": "Hello World 01{803",
  "msg_id": "EA25A323",
  "firmware": "4.35",
  "fw_sub": "p",
  "rssi": 0,
  "snr": 0
}
```

Meldung und Quittung (`src_type` ist `lora`, `firmware` numerisch):

```json
{
  "src_type": "lora",
  "type": "msg",
  "src": "DK5EN-93",
  "dst": "TEST",
  "msg": "QRT NOT SENT - Hello World 16",
  "msg_id": "00009A55",
  "firmware": 35,
  "fw_sub": "p",
  "rssi": 0,
  "snr": 0
}
```

### Wie eine App eine Quittung erkennt

Es gibt **kein eigenes Feld** dafuer. Die belastbare Regel ist die Kombination:

1. `msg_app_offline` gesetzt (BLE, Bit `0x20` in Byte 5), **und**
2. Absender gleich dem eigenen Node-Rufzeichen, **und**
3. der Text beginnt mit `QRS - `, `QRT - `, `QTA - `, `QRV - `, `QRT NOT SENT - ` oder
   `QTA NOT SENT - `.

Das Textpraefix ist das einzige harte Merkmal. Das ist die groesste offene Schwaeche des
heutigen Standes — Kapitel 5 sagt, was dagegen zu tun waere.

Ein Hinweis, kein Beweis: Meldungen tragen eine `msg_id` aus einem eigenen, streng steigenden
Zaehler (im Benchlauf `00008C30`, `000099BA`, `00009A55` …), echte Nachrichten dagegen
`(GW_ID & 0x3FFFFF) << 10 | Zaehler` (`EA25A323` …). Die Wertebereiche koennen sich
ueberschneiden; der Vergleich taugt nur zur Plausibilitaet.

### Konsolenmarker

| Marker                                  | Bedeutung                                                          |
| --------------------------------------- | ------------------------------------------------------------------ |
| `[BP];notice;<code>;depth;N;max;M;ms;T` | Zustandswechsel, einmal pro Episode und Code                       |
| `[BP];refuse;depth;N;max;M;ms;T`        | Eine Nachricht wurde abgewiesen                                    |
| `[BP];nack;<code>;dst;D;ms;T`           | Quittung zu einer verlorenen Nachricht                             |
| `[BP];nack;...;txt;<Text>`              | dieselbe Zeile mit Text — **nur bei eingeschaltetem `bLORADEBUG`** |
| `[BP];invalid;depth;N;max;M;ms;T`       | Ablehnung, die **kein** Rueckstau ist                              |

Der Text haengt bewusst am Debug-Schalter: die Netzkonsole eines Knotens wird tagelang
mitgeschnitten und ausgewertet, und diese Mitschnitte sollen keine Nachrichteninhalte fuehren.
`txt;` steht als letztes Feld, weil ein Nachrichtentext Semikolons enthalten kann.

### Was garantiert ist

- Eine Meldung oder Quittung geht **nie ueber die Luft**. Sie wird nie in den Sendepuffer
  eingereiht und traegt immer `msg_app_offline`.
- Sie geht **nur** an den Transport, ueber den die ausloesende Nachricht kam.
- Sie geht an **dasselbe Ziel** wie die ausloesende Nachricht. Wer in Gruppe 20 getippt hat,
  sieht sie im Chat 20. Bei einer DM erscheint sie im eigenen DM-Verlauf; der Partner sieht
  sie nie.

---

## 4. Flusssteuerung in einer Chat-App

### Was heute schon geht

**QRS erkannt** — nicht sperren, dezent hinweisen. Wer jetzt weitertippt, kommt noch durch.

**QRT erkannt** — Sendetaste sperren oder deutlich warnen. Ab hier gehen Nachrichten verloren.

**`NOT SENT` erkannt** — die betroffene Nachricht im Verlauf als unzugestellt markieren, mit
einem Wiederholen-Knopf. Der Abgleich laeuft ueber den Text und **muss ein Praefixvergleich
sein**, kein Gleichheitstest: der Text ist auf 120 Byte gekuerzt und traegt dann `...`.
Bei einer DM haengt am Echo zusaetzlich `{NNN}`, an der Quittung nicht.

**QRV erkannt** — Sperre aufheben.

**Zusaetzlich, und wichtiger als es klingt: ein Echo-Timeout.** Bleibt zu einer gesendeten
Nachricht laenger als ein paar Sekunden weder Echo noch Quittung aus, stimmt etwas anderes
nicht — der Node ist weg, die Verbindung steht nicht, oder die Nachricht war ungueltig. Der
`-3`-Fall erzeugt bewusst keine Quittung.

**Nicht automatisch wiederholen.** Ein automatischer Neuversuch waehrend einer laufenden
QRT-Episode fuellt den Ring erneut, sobald er sich lockert. Wiederholen gehoert hinter eine
bewusste Nutzeraktion.

**Nicht allein auf das QRV warten.** Der Benchlauf zeigt: die Entwarnung kann Minuten dauern,
weil Wiederholungen den Ring belegt halten. Eine Sperre, die nur das QRV loest, fuehlt sich
fuer den Nutzer wie ein Haenger an. Besser: nach der Sperre einen manuellen "trotzdem
versuchen"-Weg anbieten — schlimmstenfalls kommt eine weitere Quittung zurueck, und die ist
billig.

### Die Grenze des heutigen Standes

Alles oben haengt am **Textvergleich**. Das ist zerbrechlich:

- Eine Nutzernachricht, die zufaellig mit `QRT NOT SENT - ` beginnt, ist von einer Quittung
  nicht zu unterscheiden.
- Bei mehreren gleichlautenden Nachrichten im selben Chat ist nicht entscheidbar, welche
  gemeint war.
- Die Kuerzung auf 120 Byte macht lange Nachrichten mehrdeutig, wenn sie sich erst spaet
  unterscheiden.
- Jede Aenderung am Wortlaut bricht jede App, die dagegen vergleicht.

---

## 5. Ausblick: Statusmeldungen statt Textvergleich

### Welche Zustaende eine App eigentlich braucht

Eine Nachricht durchlaeuft mehr Stationen, als der heutige Rueckkanal abbildet:

| Zustand                   | Bedeutung                                          | Heute erkennbar?             |
| ------------------------- | -------------------------------------------------- | ---------------------------- |
| angenommen                | Node hat den Text geparst und in den Puffer gelegt | ja, ueber das Echo           |
| auf der Luft              | Frame wurde tatsaechlich gesendet                  | **nein**                     |
| Server erreicht           | ueber ein Gateway ins Backbone gelangt             | ja, `0x41` Statusbyte `0x01` |
| vom Empfaenger bestaetigt | ACK zurueck (nur bei DM)                           | ja, `0x41` Statusbyte `0x02` |
| abgewiesen                | Puffer im QRT-Band, nie eingereiht                 | nur ueber den Text           |
| verworfen                 | eingereiht, dann vom Ring weggeworfen              | nur ueber den Text           |

Zwei der sechs Zustaende haben ein sauberes, maschinenlesbares Signal. Zwei haengen am
Textvergleich. Einer fehlt ganz.

### Der Vorschlag

Die Rahmung existiert schon. MeshCom kennt einen Statusframe, den die App bereits auswertet:

```
Byte 0     0x41                MSG_TYPE_ACK
Byte 1..4  msg_id (LE)         auf welche Nachricht er sich bezieht
Byte 5     Status              0x01 Server erreicht, 0x02 ACK vom Empfaenger
Byte 6     0x00
```

Naheliegend waere, diesen Kanal um die fehlenden Zustaende zu erweitern:

```
0x03  refused   Puffer im QRT-Band, nie eingereiht
0x04  dropped   eingereiht, vom Ring verworfen
0x05  queued    im Sendepuffer  (das, was heute das Echo bedeutet)
0x06  on_air    tatsaechlich gesendet
```

Vorteile: exakte Zuordnung ueber die `msg_id` statt ueber Text, keine Mehrdeutigkeit bei
gleichlautenden Nachrichten, unabhaengig vom Wortlaut, und die App kann den Verlauf mit
echten Zustellzustaenden zeichnen statt mit Vermutungen.

### Die offene Frage, an der es haengt

**Eine abgewiesene Nachricht hat keine `msg_id`.** Das ist kein Versehen, sondern Absicht: der
Node verbraucht bewusst keine Message-ID fuer etwas, das er nie sendet, damit der Zaehler
nicht durch Fehlversuche laeuft. Ein Statusframe braucht aber genau diese Kennung als Bezug.

Drei Wege, mit Vor- und Nachteilen:

1. **Die App liefert eine eigene Kennung mit.** Sie schickt eine Client-ID mit der Nachricht,
   der Node spiegelt sie im Statusframe zurueck. Sauber und ohne verbrauchte Message-ID —
   erfordert aber eine Erweiterung des Sendewegs App -> Node, also Aenderungen auf beiden
   Seiten.
2. **Der Node verbraucht doch eine ID, aber nur im Fehlerfall.** Minimal auf der App-Seite,
   kostet aber genau das, was die heutige Regel vermeidet, und Luecken im Zaehler wandern ins
   Netz.
3. **Statusframes nur fuer die Faelle mit ID** (`queued`, `on_air`, `dropped`), und der
   Abweisungsfall bleibt beim Text. Kleinster Eingriff, loest das Problem aber nur halb —
   ausgerechnet der haeufigste Verlustfall bliebe am Textvergleich haengen.

### Migration

Wichtig fuer jede dieser Varianten: **die Textmeldungen bleiben.** Sie funktionieren heute,
ohne dass irgendeine App etwas aendert, und sie sind auf der Konsole und im Log lesbar.
Statusframes kaemen additiv dazu. Eine alte App ignoriert unbekannte Statusbytes und verhaelt
sich wie bisher; eine neue App bevorzugt den Statusframe und nutzt den Text nur noch als
Rueckfallebene. So laesst sich das einfuehren, ohne einen Stichtag zu brauchen.

### Was zuerst zu klaeren waere

Das ist eine Protokollaenderung und keine reine Firmware-Sache. Bevor jemand Code schreibt,
braucht es eine Verstaendigung mit der App-Seite ueber genau drei Punkte: welcher der drei
Wege fuer die Kennung, welche Statuswerte wirklich gebraucht werden, und ob `on_air`
ueberhaupt mit vertretbarem Aufwand zu melden ist. Ohne diese Klaerung ist jede
Implementierung geraten.
