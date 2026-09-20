# User Story: Hashtag-Gruppen (`#TAG`) statt fixer Gruppennummern

Status: Diskussionsentwurf fuer FW 4.36
Basis: User-Request "Thema zur Diskussion fuer FW 4.36"
Analyse-Stand: FW 4.35p

---

## 1. Ausgangslage

Heute kennt MeshCom drei Adressierungsarten im Zielfeld einer Textnachricht:

| Adressierung | Beispiel                | Verhalten am Empfaenger                                     |
| ------------ | ----------------------- | ----------------------------------------------------------- |
| `*`          | `OE1KBC-24>*:TEXT`      | alle Nodes, ausser `--nomsgall on`                          |
| `<CALL>`     | `OE1KBC-24>OE3XYZ:TEXT` | nur der Node mit diesem Rufzeichen (DM, mit ACK)            |
| `<GRC>`      | `OE1KBC-24>232:TEXT`    | nur Nodes, die diese Gruppennummer lokal konfiguriert haben |

Die Gruppennummer ist rein numerisch, Wertebereich 1..99999 (`CheckGroup()`,
`src/aprs_functions.cpp:27`). Pro Node sind **maximal 6 Gruppen** konfigurierbar
(`node_gcb[6]`, `src/nrf52/WisBlock-API.h:256`, `src/esp32/esp32_flash.h:91`).

## 2. Problem aus Sicht der User

- Der Gruppenraum ist zwar rechnerisch gross (99999), aber die Nummern sind
  bedeutungslos und muessen ausserhalb der Firmware verwaltet und kommuniziert werden.
- Pro Node koennen nur 6 Gruppen abonniert werden. Wer OE1, OE2, SOTA, EMCOM und
  Landesgruppen mitlesen will, ist sofort am Limit.
- Es gibt keine Hierarchie: "alles was mit OE zu tun hat" ist nicht ausdrueckbar.
- Neue Themen (Contest, Fieldday, Notfunkuebung) brauchen eine zentral vergebene
  Nummer, statt spontan entstehen zu koennen.

## 3. Vision

Nachrichten werden mit einem sprechenden Hashtag **getaggt** statt an eine Nummer
adressiert. Jeder Node abonniert beliebig viele Tag-Filter, gespeichert als
kompakter String (`#OE1#OE-SOTA#EMCOM`).

## 4. User Stories

### US-1: Nachricht mit Hashtag senden

> **Als** MeshCom-Nutzer
> **moechte ich** eine Nachricht mit einem frei waehlbaren Hashtag versenden koennen,
> **damit** thematisch Interessierte sie sehen, ohne dass ich vorher eine Gruppennummer beantragen muss.

Akzeptanzkriterien:

- Eingabe `{#OE-SOTA}Text` in App, Web-UI oder Terminal sendet die Nachricht mit
  Zielmarkierung `#OE-SOTA`.
- Das Tag wird automatisch in Grossbuchstaben normalisiert.
- Erlaubter Zeichensatz: `A-Z`, `0-9`, `-`. Maximale Tag-Laenge inkl. `#`: 9 Zeichen.
- Eine Nachricht traegt **genau ein** Tag (eine Markierung am Uebertragungsweg).
- Eine Hashtag-Nachricht wird wie heute eine Gruppennachricht behandelt:
  kein Empfaenger-ACK, aber Server-ACK ueber das Gateway.

### US-2: Beliebig viele Filter abonnieren

> **Als** MeshCom-Nutzer
> **moechte ich** beliebig viele Tag-Filter auf meinem Node setzen,
> **damit** ich nicht mehr an 6 Gruppen gebunden bin.

Akzeptanzkriterien:

- Neues Kommando `--setgrp #OE1#OE-SOTA#EMCOM` setzt die Filterliste am Stueck.
- `--setgrp` ohne Parameter loescht alle Filter, `--info` zeigt die aktive Liste.
- Die Filterliste wird persistent gespeichert und ueberlebt einen Neustart.
- Die Anzahl der Filter ist nur durch die Gesamtlaenge des Filterpuffers begrenzt
  (Vorschlag: 96 Zeichen, das sind ca. 12 bis 20 Filter).

### US-3: Praefix-Filter

> **Als** MeshCom-Nutzer
> **moechte ich** mit `#OE` alle Tags durchlassen, die mit `OE` beginnen,
> **damit** ich nicht `#OE1` bis `#OE9` einzeln eintragen muss.

Akzeptanzkriterien:

- Filter `#OE` laesst `#OE1`, `#OE9`, `#OE-SOTA` durch.
- Filter `#OE-SOTA` laesst ausschliesslich `#OE-SOTA` durch.
- Filter `#` allein laesst jede Hashtag-Nachricht durch ("die Neugierigen").
- Der Praefixvergleich bricht an einer Tag-Grenze ab: `#OE` matcht `#OE1`
  und `#OE-SOTA`, aber **nicht** `#OEM`. Ohne diese Regel waeren
  Namenskollisionen zwischen `#DL` und `#DLARC` unvermeidlich.

### US-4: Broadcast bleibt Broadcast

> **Als** MeshCom-Nutzer
> **moechte ich**, dass `*`-Nachrichten weiterhin unabhaengig von meinen Tag-Filtern
> ankommen, **damit** Notfall- und Allgemeinmeldungen niemand ausfiltert.

Akzeptanzkriterien:

- `OE1KBC-24>*:TEXT` wird angezeigt, sofern `--nomsgall off` gesetzt ist.
- Die Tag-Filterliste hat auf `*`-Nachrichten keinen Einfluss.

### US-5: Kompatibilitaet zu bestehenden Gruppen

> **Als** Betreiber
> **moechte ich**, dass die bestehenden numerischen Gruppen weiterlaufen,
> **damit** kein Umstiegsstichtag noetig ist.

Akzeptanzkriterien:

- `node_gcb[6]` und `--setgrc` bleiben unveraendert funktionsfaehig.
- Tag-Filter und Gruppenfilter wirken parallel (ODER-Verknuepfung).

### US-6: Anzeige und Bedienung

> **Als** MeshCom-Nutzer
> **moechte ich** am Display sehen, mit welchem Tag eine Nachricht kam.

Akzeptanzkriterien:

- Kopfzeile zeigt `#OE-SOTA <OE1KBC-24>` analog zum heutigen `GM232 <...>`.
- Web-UI und T-Deck/T-Deck-Pro-UI bieten ein Textfeld fuer die Filterliste
  statt der sechs reinen Zahlenfelder.

## 5. Vorgeschlagenes Wire-Format

```
OE1KBC-24>#OE-SOTA:TEXT
```

Das Tag steht im bestehenden Zielfeld. Es braucht kein neues Protokollfeld.
Verbotene Zeichen im Tag: `,` (VIA-Trenner), `>` (Pfadtrenner), `:` `!` `@`
(payload_type-Terminatoren) und `0x00` (Payload-Ende).

Speicherung am Node: ein String `#OE1#OE-SOTA#EMCOM`, `#` als Trennzeichen.

---

# Code-Recherche: Machbarkeit und Nebenwirkungen

## 6. Kurzfassung

Umsetzbar, aber es ist **kein lokales Feature** - es ist eine Protokollaenderung
mit Flag-Day-Charakter. Der Aufwand in der Firmware ist ueberschaubar (ca. 10
Dateien, keine Architekturaenderung). Das Risiko liegt fast vollstaendig in der
Rueckwaertskompatibilitaet: **Nodes mit FW < 4.36 verwerfen ein Paket mit
`#`-Ziel vollstaendig und leiten es auch nicht weiter.**

## 7. Betroffene Codestellen

### 7.1 Blocker: Zielfeld-Validierung beim Dekodieren

`decodeAPRS()` prueft das Zielrufzeichen. Ist es keine Gruppennummer, muss es die
Callsign-Regex bestehen:

- `src/aprs_functions.cpp:340` - `if(CheckGroup(...) == 0) { if(!checkRegexCall(...)) ... }`
- `src/regex_functions.cpp:9` - `^[0-9A-Z]?[A-Z]?[0-9]+[A-Z][A-Z]?[A-Z]?[%-]?[0-9]?[0-9]?$`

`#` ist im Zeichensatz nicht enthalten. Ergebnis: `decodeAPRS()` liefert `0x00`.

- `src/lora_functions.cpp:517` - bei `0x00` wird das Paket komplett verworfen:
  kein Display, kein BLE, **kein MHeard, kein Relay**.

Das ist die zentrale Konsequenz: In einem gemischten Netz breiten sich
Hashtag-Nachrichten nur ueber Inseln von 4.36-Nodes aus. Alte Nodes sind nicht
nur "taub", sie sind auch Relay-Loecher.

### 7.2 Filterlogik am Empfaenger

- `src/aprs_functions.cpp:52` `CheckOwnGroup()` - heutige Gruppenpruefung.
  Wichtiges Detail: Hat ein Node **keine** Gruppe konfiguriert, liefert die
  Funktion `true`, d.h. er sieht **alle** Gruppennachrichten. Fuer Tags muss
  bewusst entschieden werden, ob dieses "default open" uebernommen wird. Bei
  freier Tag-Vergabe bedeutet default-open: jeder unkonfigurierte Node sieht
  jede getaggte Nachricht.
- `src/lora_functions.cpp:983` - Anzeige-/BLE-Gate fuer `*` und Gruppen.
- `src/udp_functions.cpp:242` und `src/nrf52/nrf_eth.cpp:353` - dieselbe
  Bedingung ein zweites und drittes Mal, fuer den UDP/Server-Pfad. Diese drei
  Stellen sind bereits heute Duplikate und muessen synchron erweitert werden.

### 7.3 Senden

- `src/loop_functions.cpp:3344` - `{ZIEL}Text` wird geparst, `iCall < 11`
  begrenzt das Ziel auf 10 Zeichen. Mit `#` bleiben 9 Zeichen fuer den Tag.
- `src/loop_functions.cpp:3361` - **Konkreter Fehler ohne Anpassung:** Die
  DM-Erkennung lautet
  `if(CheckGroup(...) == 0 && != "*" && != "WLNK-1" && != "APRS2SOTA") bDM = true;`
  Ein Ziel `#OE1` faellt hier durch und wird als **DM** behandelt: es wird ein
  ACK-Request `{nnn` an den Text angehaengt und der Node erwartet ein ACK.
- `src/loop_functions.cpp:3422` - Server-ACK-Rueckmeldung an die App nur fuer
  `*` und Gruppen.
- `src/extudp_functions.cpp:266` - externe Quelle, `dst` auf 1..9 Zeichen begrenzt.

### 7.4 Gateway-ACK

- `src/lora_functions.cpp:1060` - das Gateway quittiert nur `*`, `WLNK-1`,
  `APRS2SOTA` und numerische Gruppen. Ohne Erweiterung bekommt der Sender einer
  Hashtag-Nachricht keine Server-Bestaetigung. Kombiniert mit 7.3 fuehrt das zu
  `MAX_RETRANSMIT` (3, `src/lora_functions.cpp:1981`) unnoetigen Wiederholungen
  pro Hashtag-Nachricht.

### 7.5 Prioritaetsklassifikation

- `src/lora_functions.cpp:1522` - `getMsgPriority()` parst das Ziel aus dem
  Ringbuffer. `*` und Gruppen bekommen `MSG_PRIO_HIGH`, alles andere
  `MSG_PRIO_CRITICAL`. Hashtag-Nachrichten wuerden ohne Anpassung faelschlich
  als persoenliche DM eingestuft und damit vor echten DMs und ACKs gesendet.

### 7.6 Server-Anmeldung (KEEP)

- `src/udp_functions.cpp:1049-1061` - `sendKEEP()` meldet die abonnierten
  Gruppennummern an den Server:
  `snprintf(keep_buffer, sizeof(keep_buffer), "KEEP%08X%-9.9s%-4.4s%-1.1s%s", ...)`
  mit `char keep_buffer[60]`. Fixanteil sind 22 Zeichen, es bleiben rund
  **37 Zeichen** fuer die Filterliste. Eine Filterliste von 96 Zeichen passt
  nicht hinein. Entweder Puffer vergroessern (Serverseite muss mitziehen) oder
  ein eigenes Registrierungspaket definieren.

  Das ist die Schnittstelle, ueber die der Server heute entscheidet, welchen
  Gruppenverkehr er an welches Gateway ausliefert. Ohne serverseitige
  Tag-Unterstuetzung gibt es fuer Tags keine Verteilfilterung im Backbone.

### 7.7 Konfiguration und Persistenz

- `src/command_functions.cpp:4422` - `--setgrc`, parst `9;9;9;...` in `node_gcb`.
- `src/command_functions.cpp:4877` / `:4940` - JSON- und Info-Ausgabe.
- `src/esp32/esp32_flash.cpp:119` / `:393` - NVS-Keys `node_gcb0..5`.
  Ein neuer String-Key ist unkritisch (NVS-Keyname max. 15 Zeichen).
- `src/nrf52/nrf52_flash.cpp:157` - Struct-Migration auf nRF52. Ein neues Feld
  am Ende der Struktur plus Migrationszeile ist der etablierte Weg.
- `src/configuration_global.h:5` - **`FLASH_VERSION` nicht anfassen**, wenn nicht
  noetig: bei Abweichung wird die gesamte Konfiguration geloescht
  (`src/esp32/esp32_main.cpp:734`, `src/nrf52/nrf52_main.cpp:503`). Ein neues
  NVS-Feld mit Default braucht keinen Versionssprung.
- RAM-Kosten: ein `char[96]` in `s_meshcom_settings` kostet 96 Byte auf jedem
  Board, auch auf den knappen ESP32-Originalen.

### 7.8 UI

- `src/web_functions/web_functions.cpp:1244-1250` - sechs Zahlenfelder,
  `maxlength="5"`.
- `src/t-deck-pro/ui_deckpro.cpp:2593-2707` - sechs Textareas,
  `lv_textarea_set_accepted_chars(..., "0123456789")`, `max_length 5`.
- `src/t-deck/event_functions.cpp:613ff` - `sscanf(cNew, "%i", ...)`.

Alle drei UIs sind hart auf "sechs Zahlen" verdrahtet und muessen auf ein
Freitextfeld umgebaut werden.

### 7.9 Anzeige

- `src/loop_functions.cpp:2307`, `:2341`, `:2398`, `:2450` - vier
  boardspezifische Varianten der Kopfzeile `"GM" + Ziel + " <" + Absender + ">"`.
  `GM#OE-SOTA <OE1KBC-24>` sind 22 Zeichen und wird auf 20-Zeichen-Displays
  abgeschnitten (`msg_text[20]=0x00`).

### 7.10 Ausserhalb dieses Repos

- **Phone-Apps (iOS/Android):** bekommen ueber `addBLEOutBuffer()` den rohen
  APRS-Frame und parsen das Zielfeld selbst. Ohne App-Update zeigen sie
  Hashtag-Nachrichten falsch oder gar nicht an.
- **MeshCom-Server:** KEEP-Auswertung, Verteillogik, Web-Dashboard.
- **APRS-IS-Anbindung:** `#` ist im APRS-Rufzeichenfeld nicht zulaessig.

## 8. Unerwuenschte Nebenwirkungen

| #   | Nebenwirkung                                                                                                             | Schwere            | Bemerkung                                                                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------------------ | ------------------ | ------------------------------------------------------------------------------------------------------------------------- |
| 1   | Alte Nodes verwerfen `#`-Pakete **und relayen sie nicht**                                                                | hoch               | Netz zerfaellt in Inseln, bis alle Nodes >= 4.36 sind                                                                     |
| 2   | Ohne Anpassung von `bDM` wird jede Hashtag-Nachricht als DM behandelt (ACK-Request im Text, 3 Wiederholungen ohne ACK)   | hoch               | `src/loop_functions.cpp:3361`                                                                                             |
| 3   | Kein Gateway-ACK fuer Tags                                                                                               | mittel             | `src/lora_functions.cpp:1060`                                                                                             |
| 4   | Falsche Sendeprioritaet (CRITICAL statt HIGH)                                                                            | mittel             | Tags draengen sich vor echte DMs und ACKs                                                                                 |
| 5   | KEEP-Puffer 60 Byte reicht fuer lange Filterlisten nicht                                                                 | mittel             | Serverprotokoll betroffen                                                                                                 |
| 6   | Tags sind laenger als Gruppennummern - mehr Airtime                                                                      | niedrig bis mittel | SF11/BW250/CR4-6: ca. 9 ms pro Byte, `#OE-SOTA` statt `232` sind +5 Byte = ca. +49 ms je Aussendung, und das je Relay-Hop |
| 7   | Kein Namensraum-Management: `#EMCOM` vs. `#EMCOMM`, Tippfehler, Squatting                                                | mittel             | Bei Nummern verhindert das die zentrale Vergabe                                                                           |
| 8   | Naives Praefix-Matching erzeugt Fehltreffer (`#OE` matcht `#OEM`)                                                        | mittel             | Muss an Tag-Grenzen abbrechen, siehe US-3                                                                                 |
| 9   | `default open` wie bei `CheckOwnGroup()` bedeutet: jeder unkonfigurierte Node sieht jeden Tag                            | mittel             | Bewusste Entscheidung noetig                                                                                              |
| 10  | Kopfzeile laeuft auf 20-Zeichen-Displays ueber                                                                           | niedrig            | Kosmetisch                                                                                                                |
| 11  | Phone-Apps und Server muessen zeitgleich nachziehen                                                                      | hoch               | Ausserhalb Firmware-Kontrolle                                                                                             |
| 12  | **Keine Airtime-Ersparnis:** getaggte Nachrichten werden wie heute von jedem Node weitergeleitet, unabhaengig vom Filter | mittel             | Der Gewinn ist reine Anzeige-Entlastung, nicht HF-Entlastung                                                              |

Zu 12 im Detail: Der Filter wirkt erst im `queueDisplayText()`-Gate
(`src/lora_functions.cpp:983`). Die Relay-Entscheidung faellt vorher und
unabhaengig davon in `checkMesh()` (`src/via_functions.cpp:49`). Mehr Tags und
ein einfacherer Zugang zu "Gruppen"-Nachrichten fuehren daher eher zu **mehr**
HF-Last, nicht zu weniger.

## 9. Alternativen

### A) Hashtag im Zielfeld (der vorgeschlagene Weg)

Sauber, sichtbar, serverseitig filterbar. Preis: Flag Day, siehe Nebenwirkung 1.

**Empfohlene Entschaerfung:** Zweistufiger Rollout.
Stufe 1 (z.B. 4.35q, reines Wartungsrelease): `checkRegexCall()` bzw. die
Zielpruefung akzeptiert `#TAG` und relayt es, ohne jede weitere Funktion.
Stufe 2 (4.36): Filter, UI, Senden. Zwischen den Stufen liegt genug Zeit, dass
die Relay-Loecher verschwunden sind, bevor der erste Tag gesendet wird.

### B) Tag im Payload statt im Zielfeld

`OE1KBC-24>*:#OE-SOTA TEXT`

Alte Nodes sehen eine ganz normale `*`-Nachricht mit sichtbarem Praefix, leiten
sie korrekt weiter und zeigen sie an. Keine Protokollaenderung, kein Flag Day,
kein Relay-Loch. Neue Nodes werten das fuehrende Tag aus und filtern.

Nachteile: alte Nodes sehen den Tag-Text mit; `*`-Last steigt; der Server kann
ohne Payload-Inspektion nicht vorfiltern; `--nomsgall on` blendet auf alten
Nodes alles aus.

### C) Nur die Symptome beheben, ohne Protokolleingriff

Die Praemisse "zu wenig Gruppen" trifft strenggenommen nicht zu: der
Nummernraum ist 1..99999. Knapp sind zwei andere Dinge:

1. **Filterplaetze pro Node** - heute 6. Eine Erhoehung auf z.B. 20 ist eine
   reine Array- und UI-Aenderung, ohne jede Protokollwirkung und ohne
   Kompatibilitaetsrisiko.
2. **Sprechende Namen** - eine lokale Alias-Tabelle `#OE-SOTA = 4711` loest das
   Benennungsproblem am Node, ohne dass ein einziges Byte auf der Luft anders
   aussieht.

Nicht loesbar ist damit die Hierarchie (`#OE` matcht `#OE1`) und die spontane
Tag-Vergabe ohne zentrale Nummernverwaltung.

## 10. Empfehlung

1. **Sofort und risikofrei:** Filterplaetze von 6 auf 16-20 erhoehen und eine
   lokale Alias-Tabelle fuer Gruppennamen einfuehren (Alternative C). Das nimmt
   den groessten Teil des Leidensdrucks, kostet ein Release und bricht nichts.
2. **Parallel:** Entscheidung mit Server- und App-Entwicklung herbeifuehren, ob
   `#TAG` im Zielfeld kommt. Ohne verbindliche Zusage von beiden Seiten sollte
   Stufe 2 nicht starten.
3. **Falls ja:** Stufe 1 (Relay-Toleranz fuer `#TAG`) in ein fruehes
   Wartungsrelease vorziehen, Feature erst danach.
4. **Vor der Umsetzung zu klaeren:**
   - default open oder default closed bei leerer Filterliste?
   - Namensraum: freie Vergabe oder reservierte Praefixe (`#OE`, `#DL`, `#EMCOM`)?
   - Tag-Laenge: 8 Zeichen reichen? (`iCall < 11` und `dst <= 9` sind die
     heutigen Grenzen)
   - Verhaeltnis Tags zu bestehenden Gruppennummern: parallel oder Migration?
