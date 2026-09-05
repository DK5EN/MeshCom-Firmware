# Web-GUI Messages: Ring-Scan, Browser-Historie, Gruppen-Tabs

Stand: 2026-09-05, Branch `fork-main`, Worktree `glistening-kindling-candy`

## 1. Befund (RCA, Kurzfassung)

- Die Messages-Seite (`sub_content_messages()` in
  `src/web_functions/web_functions.cpp:1734`) liest den BLE-Ring `BLEtoPhoneBuff`
  ab `toPhoneRead` bis `toPhoneWrite`. Sie hat keinen eigenen Lesezeiger.
- `toPhoneRead` wird nur von `sendToPhone()` vorgerückt, und das nur bei
  verbundenem BLE-Client mit `hello`. Ist ein Client verbunden, wird jeder
  Eintrag innerhalb von ~100 ms abgezogen; die Web-Seite findet einen leeren
  Ring (gemessen: `toPhoneWrite:13 toPhoneRead:13`, `[LOOP] WRITE BLE`, `ble:1`).
- Kein Fork-Regress. Ursprung: `87c6c200` Kurt, 2025-05-17, "v4.34x
  web_functions neu". Ein separater Web-Lesezeiger hat nie existiert.
- Slots werden beim BLE-Senden nur kopiert, nie gelöscht
  (`BLEtoPhoneBuff[MAX_RING][MAX_MSG_LEN_PHONE+5]`, 20 x 305 B, bereits
  belegt). Der Inhalt steht bis zum Überschreiben durch den Schreiber.

Randbedingung: kein zusätzlicher RAM auf dem Node. Ein eigener
Web-Nachrichtenspeicher ist ausgeschlossen.

## 2. Zielbild

| Schicht | Aufgabe                                                                      | RAM-Kosten Node |
| ------- | ---------------------------------------------------------------------------- | --------------- |
| Node    | liefert das aktuelle Ring-Fenster (alle 20 Slots), unabhängig vom BLE-Zeiger | 0 B             |
| Browser | hängt neue Nachrichten an, dedupliziert, hält die Historie                   | 0 B             |
| Browser | Tabs `*`, Gruppen aus `node_gcb[]`, `DM`; Filter auf der Historie            | 0 B             |

## 3. Schritte

### Schritt 1: Ring-Scan statt Handy-Zeiger (Firmware, upstream-fähig)

Datei: `src/web_functions/web_functions.cpp`, `sub_content_messages()`.

- Start bei `toPhoneWrite` (ältester Slot), genau `MAX_RING` Slots rundherum
  durchlaufen, Ende wieder bei `toPhoneWrite`. `toPhoneRead` wird nicht mehr
  gelesen.
- Slot mit Längenbyte 0 überspringen (nur nach Reboot leer).
- Rendered-Zähler; erst nach der Schleife bei 0 `No messages available.`
  ausgeben (heute hängt die Meldung an `toPhoneWrite == 0`, was mit dem Scan
  falsch wäre).
- Pro gerenderter Nachricht zwei Attribute an das äußere `div` hängen:
  `data-id="<msg_id>"` und `data-dst="<destination_call>"` (`*`, Gruppennummer
  oder Rufzeichen bei DM; eigene DMs: Zielrufzeichen, fremde DMs: Quellrufzeichen,
  damit das DM-Tab beide Richtungen zeigt). Werte HTML-escaped wie in WEB-03a.
- Ack-, Positions- und MH-JSON-Slots (0x41, 0x21/0x40, 0x44) werden wie heute
  verworfen. Kein Filter nach `node_gcb[]` auf dem Node, das erledigt der
  Browser.
- Race: Slot kann während des Lesens vom Schreiber (OnRxDone, nRF52
  Timer-Task) überschrieben werden. Heute schon so, Blast-Radius ein
  verstümmelter Eintrag in einer Antwort, nächster Refresh heilt. Kein
  Lock, keine Kopie (Kopie wäre 305 B Stack, tragbar, aber nicht nötig).

Einschränkung, die bleibt: die 20 Slots teilen sich Texte mit Positionen,
MH-JSON und Acks. Auf einem Gateway überlebt ein Text auf dem Node nur wenige
Minuten. Deshalb Schritt 2.

Verifikation:

- Bench: BLE-Client an DK5EN-98 verbinden (iPhone-App), `/?getmessages` per
  curl. Erwartung: Texte sichtbar, obwohl `toPhoneWrite == toPhoneRead`.
- Ohne BLE-Client: gleiche Ausgabe wie heute (Ordnung alt nach neu).
- Nach Reboot mit leerem Ring: `No messages available.`
- Resource-Gate: Flash/RAM-Delta gegen gleichen Base-Build (kein neuer Puffer,
  Delta muss im Rauschen liegen).

Commit: eigener Commit, minimaler Diff, PR-Text deutsch.

### Schritt 2: Browser-seitige Historie (Scaffold-JS, upstream-fähig)

Datei: `src/web_functions/web_functions.cpp`, `deliver_scaffold()`, Funktion
`updateMessages()`.

- Heute: `messages_panel.innerHTML = responseText` alle 10 s
  (`autorefresh()`), Historie geht bei jedem Refresh verloren.
- Neu: Antwort in ein `template`-Element parsen, jede `.message` mit
  `data-id` gegen ein JS-Set `mcSeen` prüfen. Unbekannte anhängen, bekannte
  überspringen. Der Ack-Haken (`&#x2713`, `&#x2611;`) kann sich nachträglich
  ändern: bei bekannter `data-id` das Element ersetzen statt überspringen,
  Position im DOM beibehalten.
- Schlüssel `data-id` allein reicht: `msg_id` ist 32 Bit, Kollision im
  Browser-Fenster vernachlässigbar. Zeitstempel nicht in den Schlüssel, der
  ändert sich nicht.
- Obergrenze im DOM, z. B. 200 Nachrichten, älteste entfernen.
- Beim Wechsel auf eine andere Seite und zurück ist `messages_panel` neu:
  `mcSeen` bleibt im Scaffold, das Panel wird aus einem JS-Array
  `mcHistory` (die gerenderten `outerHTML`-Strings) neu aufgebaut. Ergebnis:
  Historie überlebt Seitenwechsel, solange der Tab offen ist.
- Optional (Entscheidung offen): `mcHistory` in `localStorage`, dann
  überlebt sie einen Reload. Aufwand gering, aber Wachstum begrenzen (gleiche
  Obergrenze). Empfehlung: erst ohne, nachrüsten bei Bedarf.
- Tabellenanzeige `No messages available.` nur, wenn `mcHistory` leer ist.

Verifikation:

- Bench: BLE-Client verbunden, Text senden, 10 min warten (Ring sicher
  überschrieben), Text bleibt auf der Seite.
- Seitenwechsel Info -> Messages: Historie da.
- Ack-Update: eigene Nachricht ohne Haken, nach Gateway-Ack mit Haken.

### Schritt 3: Tabs (Firmware-HTML plus Scaffold-JS, upstream-fähig)

Datei: `src/web_functions/web_functions.cpp`, `sub_page_messages()` und
`deliver_scaffold()`.

- Tab-Leiste oberhalb von `messages_panel`: `*`, ein Tab je Eintrag in
  `meshcom_settings.node_gcb[0..5]` mit Wert `> 0 && < 100000` (Definition
  `src/esp32/esp32_flash.h:91`, nRF52 `src/nrf52/WisBlock-API.h:260`), dann
  `DM`. Tab `All` als erster Eintrag zeigt alles (heutiges Verhalten).
- Markup: `<div id="mctabs"><button class="mctab mctab-on" data-tab="all">All</button>
<button class="mctab" data-tab="*">*</button> ... <button class="mctab"
data-tab="dm">DM</button></div>`. Buttons rufen `mcTab(this)`.
- JS `mcTab()`: aktiven Tab merken (`mcTabSel`), dann über `.message` im
  Panel: `hidden` setzen, wenn `data-dst` nicht passt. Regeln:
  `all` zeigt alles; `*` zeigt `data-dst == "*"`; Gruppe zeigt
  `data-dst == "<nr>"`; `dm` zeigt alles, was weder `*` noch rein numerisch
  ist. Nach jedem `updateMessages()` den Filter erneut anwenden.
- Senden: Gruppen-Tab setzt `sendcall` auf die Gruppennummer (Nachricht geht
  als `:{232}text`), `*`-Tab leert `sendcall`, `DM`-Tab lässt das Feld
  unangetastet. `updateCharsLeft()` danach aufrufen.
- Tab-Wahl in `localStorage` merken (ein String, unkritisch).
- CSS: zwei Klassen im Scaffold (`.mctab`, `.mctab-on`), Farben aus den
  vorhandenen Variablen `--mclightblue` / `--mcgray`.
- Ungelesen-Zähler pro Tab: bewusst nicht in dieser Runde.

Verifikation:

- Gruppen der 98 (`--info`: GC 20, 232, 262, 26244, 9) erscheinen als Tabs.
- Nachricht an 232 senden: erscheint unter `All` und `232`, nicht unter `*`.
- Node ohne Gruppen: nur `All`, `*`, `DM`.

## 4. Reihenfolge und Gates

1. Schritt 1 -> Build `heltec_v3` + `wiscore_rak4631`, Bench 98 mit BLE,
   Commit.
2. Schritt 2 -> Build, Bench, Commit.
3. Schritt 3 -> Build, Bench, Commit.
4. `/fable-review` über die drei Commits, dann PR-Text (deutsch) für
   upstream DEV; alle drei Schritte fassen sich zu einem PR zusammen.

Nicht Teil des Plans: Ungelesen-Zähler, Persistenz auf dem Node, Änderung
am BLE-Pfad oder an `sendToPhone()`.
