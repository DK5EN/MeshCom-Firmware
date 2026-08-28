# MH-JSON: `PP`/`SRC`/`GW` verbrauchen das gesamte Rahmenbudget — ab `PL = 6` geht der Datensatz auf ESP32 spurlos verloren

**Adressat:** DL9SAU (Thomas Osterried)
**Betrifft:** `MH-JSON: Link-Kette der HEY-Bake mitsenden (RSSI/SNR je Hop)` und
`MH-JSON: Ursprungsrufzeichen und Gateway-Kennung mitsenden` (Branch `feat/mh-json-hey-path`),
heute in upstream `dev`
**Klasse:** latenter Größenfehler + Schnittstellen-Inkonsistenz + fehlende Formatdoku
**Schwere:** mittel — mit Standardkonfiguration nicht auslösbar, per `{SET}` aber jederzeit erreichbar
**Gemeldet:** 2026-08-28, DK5EN
**Verifiziert gegen:** `43a33ccd` (= upstream `dev` `fc83554e` + Fork-Doku)

> Dieses Issue behandelt ausschließlich das MH-Register. Die aktive Regression im
> `I`-Register (`FWDATE`) ist ein anderes Thema und steht in
> [`issue-ble-i-register-mtu-20260828.md`](issue-ble-i-register-mtu-20260828.md). Beide
> haben dieselbe strukturelle Wurzel — dazu §4.

---

## 1. Zusammenfassung

Die drei neuen Felder sind inhaltlich genau richtig: `SRC` schließt die Lücke, dass `CALL`
nur den letzten Hop nennt, `GW` ist die Auskunft, die `updateMheardPath()` intern ohnehin
schon auswertet, und `PP` ist der einzige Weg, an die Signalwerte der Zwischenhops zu
kommen. Ohne diese Felder lässt sich eine Relaisstrecke aus dem MH-Register nicht
rekonstruieren.

Der Punkt ist die Länge. `PP` wächst **linear mit der Hop-Zahl** — rund 11 Zeichen je
Relais —, und der MH-Rahmen hatte vorher keinen nennenswerten Puffer eingeplant. Mit
`MAX_HOP_POS_DEFAULT = 2` (`configuration_global.h:210`) liegt der Datensatz bei ~214
Zeichen und ist unauffällig. `{SET}` lässt aber Werte bis `MAX_HOP_LIMIT = 7` zu
(`loop_functions.cpp:2189-2192`), und dort kippt der Pfad — auf ESP32 nicht in eine
Verstümmelung, sondern in einen **stillen Totalverlust des Datensatzes**.

Dazu kommen zwei kleinere Befunde: die beiden MH-Bauer im Baum liefern seit der Erweiterung
**unterschiedliche Schemata** unter demselben `TYP`, und das `PP`-Format hat vier
Eigenheiten, an denen sich jeder Parser ohne Doku verletzen wird.

---

## 2. Der Größenpfad

### 2.1 Wo die Grenzen liegen

Der Live-Bauer schreibt über `addBLEOutBuffer()`, nicht über `addBLEComToOutBuffer()`:

```c
// src/mheard_functions.cpp:358-364
uint8_t bleBuffer[MAX_MSG_LEN_PHONE] = {0};
bleBuffer[0] = 0x44;
size_t json_len = serializeJson(mhdoc, bleBuffer+1, sizeof(bleBuffer)-1);

if(isPhoneReady == 1)
    addBLEOutBuffer(bleBuffer, json_len+1);
```

`addBLEOutBuffer()` klemmt den `'D'`-Zweig bei **255**, nicht bei 245:

```c
// src/loop_functions.cpp:534-539
uint16_t maxlen = (buffer[0] != 'D') ? (UDP_TX_BUF_SIZE - 4) : UDP_TX_BUF_SIZE;
if (len > maxlen)
    len = maxlen;
```

Und die Länge landet in einem `uint8_t`, aus dem `sendToPhone()` die Schreiblänge berechnet:

```c
// src/phone_commands.cpp:67, 124-129
uint8_t blelen;
...
#if defined(ESP8266) || defined(ESP32)
    blelen=blelen + 2;                            // 254 -> 0, 255 -> 1
    esp32_write_ble(toPhoneBuff, blelen);
#else
    g_ble_uart.write(toPhoneBuff, blelen + 2);    // int-Promotion, kein Ueberlauf
#endif
```

Auf ESP32/ESP8266 wird das Ergebnis in dasselbe `uint8_t` zurückgeschrieben. Bei
`blelen = 254` entsteht 0, bei `blelen = 255` entsteht 1 — der Rahmen wird als 0 bzw. 1 Byte
geschrieben und ist damit weg, ohne eine einzige Logzeile. Auf nRF52 tritt das nicht auf,
weil `blelen + 2` dort direkt als `int` an `write()` geht.

Weil `addBLEOutBuffer()` alles ab 255 auf genau 255 klemmt, ist der Effekt **nicht auf einen
schmalen Grat beschränkt**: jedes MH-Dokument ab 253 Zeichen fällt hinein und bleibt drin.

### 2.2 Die Rechnung

Ausgezählt über `mheard_functions.cpp:331-353`. `PP` = `R<ncnt>;` plus je Relais eine
Gruppe `<ncnt>,<rssi>,<snr>;` (ungünstig: `99,128,-20;` = 11 Zeichen). Rufzeichen 9 bzw. 8
Zeichen, `DIST` vierstellig mit Nachkommastelle.

| `PL`          | ohne `PP`/`SRC`/`GW` | mit den drei Feldern | `len` an `addBLEOutBuffer()` | Wirkung                                         |
| ------------- | -------------------- | -------------------- | ---------------------------- | ----------------------------------------------- |
| 0             | 156                  | 192                  | 193                          | ok                                              |
| 2 _(Default)_ | 156                  | 214                  | 215                          | ok                                              |
| 4             | 156                  | 236                  | 237                          | ok                                              |
| 5             | 156                  | 247                  | 248                          | oberhalb der 244, die für `I` nachweislich gilt |
| 6             | 156                  | 258                  | 259                          | **`blelen+2` läuft über → Datensatz verloren**  |
| 7             | 156                  | 269                  | 270                          | **`blelen+2` läuft über → Datensatz verloren**  |

Zur Einordnung von `PL = 5`: dort greift die Klemmung noch nicht, und der Überlauf auch
nicht. Ob der Rahmen ankommt, hängt am ausgehandelten ATT-MTU. Belegt ist aus dem
`I`-Register, dass 247-Byte-Schreibvorgänge vollständig ankommen; für 250 Byte fehlt der
Beleg in beide Richtungen. Sicher falsch ist erst `PL ≥ 6` — dort ist es unabhängig vom MTU
kaputt.

Der Anteil der drei Felder am Dokument: bei `PL = 2` sind es 58 von 214 Zeichen, bei
`PL = 7` bereits 113 von 269. `PP` allein ist bei sieben Hops mit 81 Zeichen das größte
Einzelfeld des Registers.

---

## 3. Zweiter Befund: zwei MH-Bauer, zwei Schemata

Der Baum enthält zwei Stellen, die `TYP: "MH"` erzeugen:

| Ort                        | Pfad                                               | Felder                    | Länge   |
| -------------------------- | -------------------------------------------------- | ------------------------- | ------- |
| `mheard_functions.cpp:331` | Live, bei jedem gehörten Paket                     | **mit** `PP`, `SRC`, `GW` | 192–269 |
| `mheard_functions.cpp:651` | `--mheard`, Tabellenabzug (`addBLEComToOutBuffer`) | **ohne** die drei         | 156     |

Die Erweiterung hat nur den Live-Bauer erfasst. Für einen Konsumenten heißt das: dasselbe
`TYP` liefert je nach Auslöser ein anderes Schema, und ein `--mheard`-Abzug kann eine
Zuordnung über `SRC` nicht bedienen — auch nicht für Knoten, die live sauber gemeldet
wurden. Wer die MHeard-Tabelle nach einem Neustart des Telefons über `--mheard` neu aufbaut,
verliert die Ursprungszuordnung für den gesamten Bestand.

Das ist ohne Weiteres nachvollziehbar — der Tabellenabzug rekonstruiert aus
`mheardBuffer[]`, und `mh_path_payload` / `mh_sourcecallsign` stehen dort nicht drin
(`mheard_functions.cpp:325-327`). Es sollte aber entweder geschlossen oder ausdrücklich
dokumentiert werden, damit die App-Seite nicht davon ausgeht, `SRC` sei immer vorhanden.

---

## 4. Gemeinsame Wurzel mit dem `I`-Regressionsfall: die falsche Konstante

Das gehört hierher, weil es beide Fälle erklärt und weil die Formulierung aus `ae15bb71`
stammt. Die Commit-Nachricht dort sagt:

> Länge: das Info-JSON liegt damit bei grob 260 Zeichen. Die vorhandene Begrenzung greift
> bei `MAX_MSG_LEN_PHONE - 2` = 298, es bleibt also Reserve.

`MAX_MSG_LEN_PHONE - 2` ist die Prüfung, die im Builder sichtbar ist
(`command_functions.cpp:4996`) — aber nicht die, die wirkt. Wirksam ist die Klemmung eine
Ebene tiefer, in `addBLEComToOutBuffer()` bei 245, also **244 Zeichen JSON**. Die
Einschätzung "260 Zeichen, es bleibt Reserve" war damit schon damals eine Überschreitung um
16 Zeichen — sie ist nur nicht aufgefallen, weil die tatsächliche Länge bei einem Knoten
ohne Gruppenrufe bei 233 lag.

Nachgerechnet über `command_functions.cpp:4961-4988`, mit `GCB`-Werten bis 99999
(`command_functions.cpp:4533` lässt das zu):

| `FWDATE`-Variante                           | keine Gruppenrufe | alle 6 Slots belegt |
| ------------------------------------------- | ----------------- | ------------------- |
| `FLASH_VERSION` (dein `ae15bb71`)           | 233 (+11)         | **257 (−13)**       |
| `__DATE__ " " __TIME__` (`82db3d41`, heute) | **247 (−3)**      | **271 (−27)**       |

Das heißt: die Umstellung durch OE1KBC hat den Fall akut gemacht, aber für Knoten mit
belegten Gruppenruf-Slots war das Feld schon in der ursprünglichen Fassung zu groß. Ich
melde das nicht als Schuldzuweisung, sondern weil es die Wahl der Lösung ändert: eine
Rückkehr zu `FLASH_VERSION` allein reicht nicht aus, es braucht zusätzlich Platz an anderer
Stelle. Das steht im [`I`-Issue](issue-ble-i-register-mtu-20260828.md) §5.

Für das MH-Register gilt dasselbe Muster: die Prüfung, gegen die man intuitiv rechnet, ist
nicht die, die greift. Solange die Zahlen 298 / 255 / 245 unbenannt und verstreut sind, wird
jede weitere Felderweiterung dieselbe Falle stellen.

---

## 5. Dritter Befund: das `PP`-Format braucht eine Dokumentation

Das Format ist aus `sendHey()` (`loop_functions.cpp:4242`) und `appendHeySignalReport()`
(`aprs_functions.cpp:1127-1135`) eindeutig ablesbar:

```
R<ncnt>;                  vom Absender selbst, dessen eigene Nachbarzahl
<ncnt>,<rssi>,<snr>;      von jedem Relais angehängt, eine Gruppe je Hop
```

Vier Eigenheiten, an denen sich ein Parser ohne Vorwissen verletzt — alle bewusst so gebaut,
aber nirgends festgehalten:

1. **`RSSI` steht als positiver Betrag in `PP`.**

   ```c
   // src/aprs_functions.cpp:1130
   aprsmsg.msg_payload.concat(String(rssi*-1.0, 0));
   ```

   `-101 dBm` liegt als `101` auf dem Draht. Ein Parser, der dem Vorzeichen traut, dreht
   jede Messung um — und zwar plausibel aussehend, also unbemerkt.

2. **`GW` beschreibt `SRC`, nicht `CALL`.**

   ```c
   // src/mheard_functions.cpp:353
   mhdoc["GW"] = (mheardLine.mh_destinationpath == "HG") ? 1 : 0;
   ```

   `mh_destinationpath` setzt der Absender; Relais fassen es nicht an. Die Kennung auf die
   `CALL`-Zeile zu schreiben ist bei jeder weitergeleiteten Bake falsch — und weitergeleitet
   ist der Normalfall.

3. **`PP` endet vor dem eigenen Hop.** Die Kette beschreibt die Strecke bis
   einschließlich des letzten Relais; das Segment `CALL → wir` steht in den Feldern `RSSI`
   und `SNR` auf oberster Ebene. Kette plus oberste Ebene ergibt erst den vollständigen Pfad.

4. **Die Alt-Format-Erkennung geht über die Kommazahl**, nicht über die Feldzahl:
   ```c
   // src/mheard_functions.cpp:436-451
   // gültig:   R99;   R99,99,99;
   // ungültig: R99,99;
   if(icomma == 0 || icomma == 2)
   ```
   Wer sich eine eigene Regel ausdenkt, weicht davon ab.

Und eine Eigenschaft, die für die Auswertung entscheidend ist: **`PP` enthält keine
Rufzeichen.** Man bekommt eine geordnete Liste von `(ncnt, rssi, snr)`, aber keine
Hop-Identitäten — und `PL` liefert nur eine Anzahl. Aus dem MH-Register allein lässt sich
also nicht sagen, _welche_ Station eine schwache Verbindung hatte, nur _an welcher Position_
in der Kette. Das begrenzt, was man sinnvoll persistieren kann, und sollte in der Doku
stehen, bevor jemand eine Datenbankmigration darauf plant.

---

## 6. Lösungsvorschläge

### 6.1 Fail-soft statt Abschneiden

Der Datensatz ist ohne `PP` weiterhin vollständig brauchbar, ohne `TYP` und `CALL` dagegen
wertlos. Also das teuerste optionale Feld opfern, bevor der Rahmen kippt:

```c
// src/mheard_functions.cpp, vor dem serializeJson
if (measureJson(mhdoc) + 1 > BLE_JSON_PAYLOAD_MAX)
    mhdoc.remove("PP");        // groesstes optionales Feld zuerst
if (measureJson(mhdoc) + 1 > BLE_JSON_PAYLOAD_MAX)
    mhdoc.remove("DIST");      // aus CALL/SRC jederzeit nachrechenbar
```

Das ist die kleinste Änderung mit der größten Wirkung: sie macht den Fehler unerreichbar,
unabhängig davon, welchen Hop-Wert ein Betreiber einstellt. `BLE_JSON_PAYLOAD_MAX` ist die
benannte Konstante, die im `I`-Issue §5.2 vorgeschlagen wird — solange die nicht existiert,
tut es hier `244`.

Eine Meldung über `printfdeb()` beim Weglassen wäre hilfreich, damit im Netz-Log sichtbar
ist, dass gekürzt wurde.

### 6.2 `PP` schon beim Anhängen begrenzen

Ergänzend, an der Quelle:

```c
// src/aprs_functions.cpp:1127
void appendHeySignalReport(struct aprsMessage &aprsmsg, int16_t rssi, int8_t snr, int mheard_count)
{
    if (aprsmsg.msg_payload.length() + 12 > HEY_PATH_PAYLOAD_MAX)
        return;                 // Kette lieber beenden als den Rahmen sprengen
    ...
```

Das begrenzt gleichzeitig die Länge der HEY-Bake auf der Luftschnittstelle, was für sich
genommen wünschenswert ist.

### 6.3 Den Tabellenabzug angleichen oder die Abweichung dokumentieren

Zwei gangbare Wege für `mheard_functions.cpp:651`:

- `mh_sourcecallsign` und `mh_destinationpath` in `mheardBuffer[]` mitführen
  (`mheard_functions.cpp:325-327`, das Format ist ohnehin `|`-getrennt und erweiterbar) und
  `SRC`/`GW` auch im Abzug senden. `PP` würde ich dort weglassen — es ist der teuerste Teil
  und für einen Tabellenabzug am wenigsten wert.
- Oder die Abweichung ausdrücklich festhalten, damit die App-Seite `SRC` als optional
  behandelt statt als garantiert.

Der erste Weg ist mir lieber, weil `SRC` und `GW` zusammen nur ~25 Zeichen kosten und der
Abzug mit 156 Zeichen reichlich Luft hat.

### 6.4 Regressionstest über die Hop-Zahl

Der Fall ist gut testbar, weil er rein von der Feldlänge abhängt. Das Repo hat mit
`env:native` und `test/test_hey_report` schon genau die passende Umgebung. Vorschlag: den
bestehenden Test um eine Schleife über `PL = 0..MAX_HOP_LIMIT` erweitern, die
`measureJson(mhdoc) + 1 <= BLE_JSON_PAYLOAD_MAX` behauptet — mit ungünstigsten Werten
(9-stelliges Rufzeichen, `ncnt = 99`, `rssi = -128`, `snr = -20`, `DIST` vierstellig).
Dieser Test wäre vor dem Merge rot gewesen.

### 6.5 Formatdoku: `docs/mheard-json-wire-format.md`

Für `PP` gibt es bisher keine Beschreibung — `docs/hey-supp.md` behandelt die
Trickle-Unterdrückung, nicht das Nutzlastformat. Vorschlag für ein kurzes Dokument mit
genau dem Inhalt aus §5: die Grammatik, die vier Fallen, die Aussage "keine Rufzeichen in
`PP`", und je eine Quellcode-Referenz dazu. Das ist billig zu schreiben und erspart jedem
Konsumenten — App, Proxy, Auswertung — dieselbe Herleitung.

---

## 7. Was ich ausdrücklich übernehmen würde

Deine Form in `mheard_functions.cpp:361` —

```c
size_t json_len = serializeJson(mhdoc, bleBuffer+1, sizeof(bleBuffer)-1);
```

— ist die richtige und im Baum bisher die einzige. `command_functions.cpp` übergibt an **14
Stellen** stattdessen `measureJson(doc)` als Puffergröße, womit kein Platz für den
Nullterminator bleibt; das nachfolgende `strlen()` liest über das Ende hinaus und wird nur
durch das vorherige `memset(print_buff, 0, 350)` gerettet. Ich schlage im
[`I`-Issue](issue-ble-i-register-mtu-20260828.md) §5.6 vor, alle 14 auf deine Form
umzustellen.
