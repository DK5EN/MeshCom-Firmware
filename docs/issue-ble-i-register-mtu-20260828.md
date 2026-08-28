# BLE TYPE `I`: `FWDATE` als `__DATE__ " " __TIME__` sprengt die Rahmengrenze — das Register kommt seit `82db3d41` nicht mehr an

**Adressat:** OE1KBC (Kurt Baumann), icssw-org/MeshCom-Firmware
**Betrifft:** `82db3d41` ("v4.35p BLE TYPE 'I' new FWDATE"), enthalten in `dev` seit `fc83554e`
**Klasse:** Regression, aktiv im Feld
**Schwere:** hoch — das Register ist für Apps unbrauchbar, nicht nur unvollständig
**Gemeldet:** 2026-08-28, DK5EN
**Verifiziert gegen:** `43a33ccd` (= upstream `dev` `fc83554e` + Fork-Doku)

> Dieses Issue behandelt das `I`-Register und die gemeinsame Rahmengrößen-Infrastruktur
> (`addBLEComToOutBuffer`, `sendComToPhone`, die `serializeJson`-Aufrufform). Das zweite,
> davon getrennte Thema — das MH-Register mit `PP`/`SRC`/`GW` — steht in
> [`issue-mh-json-size-budget-20260828.md`](issue-mh-json-size-budget-20260828.md).

---

## 1. Zusammenfassung

`82db3d41` hat den Wert des Schlüssels `FWDATE` von der Ganzzahl `FLASH_VERSION` auf den
String `__DATE__ " " __TIME__` umgestellt. Das sind **14 Zeichen mehr**. Damit überschreitet
das `I`-Dokument die Grenze, die die Firmware sich selbst in
`loop_functions.cpp:606-611` setzt (245 Byte inkl. Header, also **244 Zeichen JSON**).
`addBLEComToOutBuffer()` schneidet danach hart auf 244 Zeichen ab — mitten im Wert. Das
Ergebnis ist kein verkürztes, sondern ein **syntaktisch kaputtes** JSON-Objekt.

Für die App heißt das: `I` fällt nicht teilweise aus, sondern vollständig. Sie kann das
Objekt nicht parsen und hat damit weder `CALL` noch `ID` noch `HWID` — also keine
Knotenidentität.

Die Ursache ist nicht das Feld `FWDATE` an sich. In seiner ursprünglichen Form
(`ae15bb71`, DL9SAU, `idoc["FWDATE"] = FLASH_VERSION`) passte es. Erst die Umstellung auf
den Zeitstempel-String hat den Rahmen gesprengt.

---

## 2. Beobachtung im Feld

Auf einem Gateway-Host (`mcapp.local`, BLE-Anbindung an einen Knoten mit der neuen
Firmware) seit **2026-08-27 22:29:36** — dem Zeitpunkt des Firmware-Starts — kein einziges
gültiges `I`-Register mehr. Bis zur Meldung 55 Verwerfungen, im 10-Minuten-Takt des
Registerabgleichs:

```
Dropped BLE register update (TYP='I'): D{ frame (247 bytes, 244 chars decoded)
looks truncated (unbalanced or unclosed JSON)
json.decoder.JSONDecodeError: Expecting value: line 1 column 242
```

Die **244 decodierten Zeichen** sind der direkte Fingerabdruck der Klemmung in
`loop_functions.cpp:608`: 245 Byte minus 1 Byte Typkennung (`0x44`) = 244 Zeichen JSON.

### 2.1 Der Knoten meldet es selbst

Das muss man nicht aus dem App-Log erschließen. `addBLEComToOutBuffer()` protokolliert den
Fall unkonditioniert über `printfdeb()` — also auf der seriellen Konsole **und** auf der
Netz-Debugkonsole (TCP 2323):

```c
// src/loop_functions.cpp:606-611
if (len > 245)
{
    printfdeb("[ERR]...BLE out-buffer to long <%i> <%-245.245s>\n", len, buffer);
    len = 245; // clamp - length byte and destination buffer both size to this
}
```

Wer den Fall nachstellen will: Knoten mit `dev` ab `82db3d41` flashen, App/Proxy per BLE
verbinden, `--info` auslösen und die Konsole beobachten. Die Zeile erscheint bei jeder
`I`-Ausgabe.

---

## 3. Mechanismus: drei Grenzen in drei Dateien, keine davon benannt

Der Pfad vom Dokument zum Telefon durchläuft drei voneinander unabhängige Längenbegrenzungen:

| #   | Ort                          | Wert                              | Wirkung                                    |
| --- | ---------------------------- | --------------------------------- | ------------------------------------------ |
| 1   | `command_functions.cpp:4996` | `MAX_MSG_LEN_PHONE - 2` = **298** | greift nie — liegt weit über allem anderen |
| 2   | `loop_functions.cpp:608`     | **245**                           | die **tatsächlich wirksame** Grenze        |
| 3   | `phone_commands.cpp:200`     | `blelen + 2`                      | schreibt 2 Byte mehr, als der Puffer trägt |

```c
// src/command_functions.cpp:4993-5004  (Grenze 1)
serializeJson(idoc, print_buff, measureJson(idoc));

json_len = strlen(print_buff);
if (json_len > MAX_MSG_LEN_PHONE - 2) {      // 298 — wirkungslos
    json_len = MAX_MSG_LEN_PHONE - 2;
}

memset(msg_buffer, 0, sizeof(msg_buffer));
msg_buffer[0] = 0x44;
memcpy(msg_buffer + 1, print_buff, json_len);

addBLEComToOutBuffer(msg_buffer, json_len + 1);   // -> Grenze 2 klemmt auf 245
```

Der Builder prüft also gegen 298 und meldet "passt", während die Stufe darunter auf 245
klemmt. Genau diese Diskrepanz ist der Grund, warum die Änderung durch Review und Build
gekommen ist: `MAX_MSG_LEN_PHONE - 2` **sieht** wie die zuständige Grenze aus, ist es aber
nicht. Derselbe Vergleich gegen 298 steht in allen Registerbauern in
`command_functions.cpp` (`TM`, `W`, `IO`, `I`, `SE`, `S1`, `SW`, `SN`, `APRS`, `CONF` …).

**Wichtige Einordnung zur Rahmenlänge:** die 247 Byte aus dem Log sind `blelen + 2` mit
`blelen = 245` und damit **konstant**, sobald das Dokument ≥ 244 Zeichen ist. Sie beweisen
die Überschreitung, sagen aber nichts über deren Höhe aus. Der ATT-MTU ist hier nicht die
bindende Grenze — 247 Byte kommen empirisch vollständig an. Bindend ist die firmware-eigene
245er-Klemmung.

---

## 4. Die Rechnung

Länge des `I`-Dokuments, ausgezählt über den Builder in `command_functions.cpp:4961-4988`.
Angenommen: 8-stelliges Rufzeichen, `ID` 10-stellig (`unsigned int`), `CTRY` = `"EU"`,
`BPIN` 6-stellig. Die Spalten unterscheiden sich nur in den sechs Gruppenruf-Slots, für die
`command_functions.cpp:4533` Werte bis **99999** zulässt.

| Variante                                      | keine Gruppenrufe | alle 6 Slots belegt |
| --------------------------------------------- | ----------------- | ------------------- |
| **`dev` heute** (`__DATE__ " " __TIME__`)     | **247 (−3)**      | **271 (−27)**       |
| `FWDATE` = `FLASH_VERSION` (Stand `ae15bb71`) | 233 (+11)         | **257 (−13)**       |
| `GCB0..5` → `"GCB":[…]`                       | 223 (+21)         | 237 (+7)            |
| beides zusammen                               | 199 (+45)         | 223 (+21)           |

(Klammerwert = Abstand zur Grenze von 244 Zeichen; negativ = Überschreitung.)

Zwei Punkte, die aus der Tabelle folgen und für die Wahl der Lösung entscheidend sind:

1. **Der aktuelle Stand ist für jeden Knoten kaputt**, nicht nur für Randfälle. Schon ohne
   einen einzigen Gruppenruf fehlen 3 Zeichen.
2. **Die Rückkehr zu `FLASH_VERSION` allein genügt nicht.** Ein Knoten mit allen sechs
   Gruppenruf-Slots liegt weiterhin 13 Zeichen darüber. Dieser Fall war schon vor
   `82db3d41` defekt, nur unauffällig, weil er seltener ist.

Der jeweils teuerste Block ist nicht `FWDATE`, sondern `GCB0..GCB5`: sechs einzelne
Schlüssel kosten bis zu 78 Zeichen, dieselben Werte als Array 43.

---

## 5. Lösungsvorschläge

Die Vorschläge sind so geschnitten, dass 5.1–5.4 ohne jede Absprache mit den Apps sofort
gehen und 5.5 den App-Kontrakt berührt und deshalb getrennt bleibt.

### 5.1 Sofortmaßnahme: `FWDATE` zurück auf `FLASH_VERSION`

```c
// src/command_functions.cpp:4969-4971
-            char cfwdate[24];
-            snprintf(cfwdate, sizeof(cfwdate), "%s %s", __DATE__, __TIME__);
-            idoc["FWDATE"] = cfwdate;
+            idoc["FWDATE"] = FLASH_VERSION;      // configuration_global.h:50, aktuell 20260827
```

Spart 14 Zeichen und stellt den Stand von `ae15bb71` wieder her. Der Schlüsselname bleibt
unverändert, es ändert sich nur der Typ von String auf Zahl.

Drei Argumente dafür, über die reine Länge hinaus:

- `FLASH_VERSION` ist der Wert, den die Firmware ohnehin als Release-Kennung führt
  (`configuration_global.h:33-50`) und der pro Release gepflegt wird. `__DATE__`/`__TIME__`
  ist dagegen die Uhrzeit des jeweiligen **Compilerlaufs**: zwei Builds desselben
  Quellstands liefern verschiedene Werte. Für "welchen Stand hat der Nutzer geflasht?" ist
  das eher hinderlich.
- Als Zahl ist der Wert direkt vergleichbar (`FWDATE < 20260827` → "zu alt"). Als
  `"Aug 27 2026 21:10:58"` muss die App erst einen englischen Monatsnamen parsen.
- Das Feld ist erst einen Tag alt. Der Typwechsel trifft praktisch noch keine
  ausgelieferte App — jetzt ist der billigste Moment dafür.

Falls die Textform gewünscht bleibt, wäre `snprintf(cfwdate, sizeof(cfwdate), "%s", __DATE__)`
(11 statt 20 Zeichen, spart 9) die kleinere Variante — sie reicht aber allein nicht aus,
siehe Tabelle.

### 5.2 Eine benannte Konstante statt drei verstreuter Zahlen

```c
// src/configuration_global.h
// Nutzbare JSON-Nutzlast eines BLE-Rahmens zum Telefon: die Klemmung in
// addBLEComToOutBuffer() laesst 245 Byte zu, davon geht 1 Byte Typkennung ab.
#define BLE_JSON_PAYLOAD_MAX 244
```

und dann konsequent verwenden — in `loop_functions.cpp:608` statt der nackten `245`, und in
allen Registerbauern statt `MAX_MSG_LEN_PHONE - 2`. Damit prüft der Builder gegen die
Grenze, die tatsächlich gilt. Das ist die eigentliche Vorbeugung: solange die beiden Zahlen
auseinanderlaufen, wird dieser Fehler wiederkommen, sobald jemand ein Feld ergänzt.

### 5.3 Nicht abschneiden, sondern weglassen

Ein auf Byteebene abgeschnittenes JSON ist wertlos — die App verliert **alle** Felder, nicht
nur das letzte. Ein Dokument ohne ein optionales Feld ist dagegen voll benutzbar. Vorschlag
für die Bauer, deren Länge variabel ist:

```c
if (measureJson(idoc) > BLE_JSON_PAYLOAD_MAX)
{
    // zuerst das entbehrlichste Feld opfern, dann neu messen
    idoc.remove("BOOST");
    ...
}
```

Zusätzlich sollte die Meldung in `addBLEComToOutBuffer()` den Registertyp nennen, damit im
Log sofort sichtbar ist, _welches_ Register zu groß war.

### 5.4 `blelen` verrechnen, ohne zu überlaufen

```c
// src/phone_commands.cpp:124-129 (identisch in :198-203)
#if defined(ESP8266) || defined(ESP32)
    blelen=blelen + 2;              // blelen ist uint8_t -> 254 wird 0, 255 wird 1
    esp32_write_ble(toPhoneBuff, blelen);
#else
    g_ble_uart.write(toPhoneBuff, blelen + 2);   // int-Promotion, kein Ueberlauf
#endif
```

Auf ESP32/ESP8266 wird das Ergebnis in dasselbe `uint8_t` zurückgeschrieben: bei
`blelen = 254` entsteht 0, bei 255 entsteht 1 — der Rahmen geht dann **vollständig
verloren**, ohne Meldung. Auf nRF52 passiert das nicht, weil `blelen + 2` dort direkt als
`int` an `write()` geht.

Für das `I`-Register ist das folgenlos (die 245er-Klemmung liegt darunter). Erreichbar ist
es über `addBLEOutBuffer()`, das im `'D'`-Zweig bis 255 zulässt — Details im
[MH-Issue](issue-mh-json-size-budget-20260828.md). Die Behebung liegt aber hier, und sie ist
klein:

```c
    uint16_t writelen = (uint16_t)blelen + 2;
    esp32_write_ble(toPhoneBuff, writelen);
```

### 5.5 Mittelfristig: `GCB0..GCB5` als Array — getrennt abstimmen

```c
JsonArray gcb = idoc["GCB"].to<JsonArray>();
for (int i = 0; i < 6; i++)
    gcb.add(meshcom_settings.node_gcb[i]);
```

Spart 34–41 Zeichen und ist damit die einzige Maßnahme, die auch den Fall "alle sechs
Gruppenrufe belegt" dauerhaft löst. Das ist allerdings eine für die Apps **sichtbare
Kontraktänderung** (heute wird `GCB0`…`GCB5` einzeln gelesen) und gehört deshalb nicht in
denselben PR wie die Sofortmaßnahme. Vorschlag: als eigenes Issue mit den App-Autoren
abstimmen, mit einer Übergangszeit, in der beide Formen gesendet werden — sofern der Platz
das hergibt, sonst per Firmware-Version umgeschaltet.

Alternative mit demselben Effekt: `I` aufteilen, wie es bei `SE`/`S1` schon gemacht wird.
Sauberer, aber der größere Eingriff.

### 5.6 Nebenbefund: `serializeJson()` bekommt die gemessene Länge statt der Puffergröße

An **14 Stellen** in `command_functions.cpp` (Zeilen 4734, 4784, 4894, 4993, 5236, 5264,
5306, 5334, 5411, 5495, 5532, 5569, 5596 …) steht:

```c
serializeJson(idoc, print_buff, measureJson(idoc));
```

`measureJson()` liefert die Länge **ohne** Nullterminator. Als Puffergröße übergeben bleibt
damit kein Platz für die abschließende `\0`, und das direkt danach aufgerufene `strlen()`
liest über das serialisierte Ende hinaus. Aktuell fällt das nicht auf, weil `print_buff`
mit `char print_buff[350]` (`command_functions.cpp:89`) reichlich bemessen und vorher
genullt ist — die Maskierung hält aber nur, solange kein Dokument in die Nähe von 350
Zeichen kommt.

Richtig ist die Puffergröße:

```c
serializeJson(idoc, print_buff, sizeof(print_buff));
```

Genau so steht es bereits in `mheard_functions.cpp:361` — die Form ist im Baum also schon
vorhanden, nur nicht überall.

---

## 6. Vorschlag für den Zuschnitt

**PR A — sofort, ohne App-Absprache:** 5.1, 5.2, 5.3, 5.4, 5.6.
Reine Firmware, keine sichtbare Kontraktänderung außer dem Typ von `FWDATE`.

**PR B — getrennt, mit App-Absprache:** 5.5.

Gern übernehmen wir PR A als Fork-Beitrag gegen `DEV`, wenn das recht ist — sagt einfach
Bescheid, ob ihr das lieber selbst macht.

---

## 7. Vorschlag für einen Regressionstest

Der Fehler wäre von einem Test erwischt worden, der die Dokumentlänge gegen die _wirksame_
Grenze prüft. Das Repo hat mit `env:native` bereits die passende Infrastruktur
(`test/test_hey_report`, `test/test_aprs_spec` …). Vorschlag: `test/test_ble_frame_size`,
das die Register mit den ungünstigsten zulässigen Feldwerten baut —

- Rufzeichen in maximaler Länge
- `ID` 10-stellig
- alle sechs `GCB`-Slots auf 99999
- `CTRY` = `"none"` (längster Eintrag in `strCountry[]`, `lora_setchip.cpp:61`)
- `BPIN` 6-stellig

— und `measureJson(doc) <= BLE_JSON_PAYLOAD_MAX` behauptet. Das kostet wenig und deckt jede
künftige Felderweiterung ab, bevor sie im Feld auffällt.
