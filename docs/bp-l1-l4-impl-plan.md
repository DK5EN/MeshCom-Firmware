# Implementierungsplan BP-07 / BP-08 / BP-09 — Rueckmeldung fuer abgewiesene Nachrichten

Datum: 2026-09-01
Basis: v4.35p.08.31.4-stability, Branch `tdeck-partial-refresh-trace`
Vorlauf: `docs/backpressure-flow-control.md` Kapitel 8 (Luecken L1-L5)

Umfang: **L1, L2, L3, L4** und die Erweiterung `QRT NOT SENT - <Text>`.
**L5 (maschinenlesbares Merkmal statt Textpraefix) ist ausdruecklich nicht Teil dieses Plans**
und bleibt offen — es braucht eine Protokollaenderung und eine Abstimmung mit der App-Seite.

Vier Entscheidungen des Operators vom 2026-09-01 sind eingearbeitet, siehe Abschnitt
"Getroffene Entscheidungen".

---

## Grundentscheidung: der Refuse-Check wandert nach hinten

Alles Weitere haengt daran, deshalb zuerst.

Der Abweisungs-Check steht heute in `sendMessage()` bei Zeile 3552, also **vor** der
Prozentzeichen-Dekodierung und vor dem Abtrennen des `{ZIEL}`-Praefixes. Genau deshalb gibt es
`bpPeekDst()` in `bp_notice_frame.h`: eine Mini-Zweitparsung des Ziels, weil die
authoritative Parsung erst spaeter passiert. Und genau deshalb steht an dieser Stelle kein
brauchbarer Nachrichtentext zur Verfuegung — nur der Rohtext mit `%C3%BC`-Sequenzen und
`{232}`-Praefix.

Zwischen dem Ende des `{ZIEL}`-Blocks (Zeile 3736) und `meshcom_settings.node_msgid++`
(Zeile 3774) liegt ein Fenster, in dem

- `strMsg` fertig dekodiert und vom Zielpraefix befreit ist,
- `strDestinationCall` authoritativ ist (getrimmt, gross geschrieben),
- **noch kein einziger Seiteneffekt** stattgefunden hat: keine Message-ID, kein
  `save_settings()`, kein `insertOwnTx()`, kein Echo.

Der Check wandert dorthin. Das loest L2 vollstaendig und macht `bpPeekDst()` ersatzlos
ueberfluessig — eine Nettoloeschung von rund 60 Zeilen plus zugehoerige Testfaelle.

Zwei Verhaltensaenderungen, die daraus folgen und beide gewollt sind:

1. Eine Nachricht mit unzulaessiger Laenge (`< 1` oder `> 160`) und eine DM an das eigene
   Rufzeichen werden jetzt **vor** dem Rueckstau-Check abgewiesen. Richtig so: fuer eine
   fehlerhafte Nachricht ist "TX-Puffer voll" die falsche Begruendung.
2. Eine abgewiesene Nachricht durchlaeuft jetzt die Dekodierschleife. Reine Rechenzeit, keine
   Seiteneffekte. Auf nRF52 unkritisch, weil die Dekodierpuffer dort ohnehin schon in BSS
   liegen (N-22) und der Stack nicht zusaetzlich belastet wird.

---

## BP-07 — L1, L2 und die Nack-Erweiterung

### Befund L1, zur Erinnerung

`onRefuse()` ruft `latchIfHigher(BP_NOTICE_QRT)`. Der Abweisungszustand wird nur in zwei
Zweigen von `onSend()` betreten, die beide zwingend auf QRT oder QTA latchen. Wenn
`refusing()` wahr ist, steht der Latch also immer schon auf mindestens QRT, und
`latchIfHigher` liefert `NONE`. **Der Rueckgabewert ist konstant `BP_NOTICE_NONE`.**

### Kernidee

Eine Rueckmeldung pro abgewiesener Nachricht ist eine **andere Klasse** als eine
Episodenwarnung. Die Episodenwarnung ist gelatcht (eine pro Zustandswechsel, TM-21). Die
Rueckmeldung ist pro Nachricht und darf nicht gelatcht sein. Also bekommt sie ein eigenes
Vokabular statt eines weiteren Wertes in `BpNotice`, dessen Reihenfolge den Latch bildet.

### `src/backpressure.h`

Neu:

```cpp
/// Per-message outcome for a locally originated user message. Deliberately
/// NOT a BpNotice value: BpNotice is the EPISODE vocabulary and its first
/// four values double as the latch (one notice per state transition, TM-21).
/// A nack is per message and is never latched.
enum BpNack
{
    BP_NACK_NONE = 0,
    BP_NACK_QRT  = 1,   ///< refused before enqueue, ring sits in the QRT band
    BP_NACK_QTA  = 2    ///< enqueue attempted, the ring threw the frame away
};

inline const char *bpNackCode(BpNack n);     // "QRT" / "QTA" / ""
inline const char *bpNackPrefix(BpNack n);   // siehe unten
```

Texte (Entscheidung E1: ein Frame pro verlorener Nachricht, der Q-Code steckt im Text):

```
BP_NACK_QRT -> "QRT NOT SENT - "
BP_NACK_QTA -> "QTA NOT SENT - "
```

Die gelatchte Episodenwarnung bleibt daneben unveraendert bestehen und behaelt ihren
bisherigen Wortlaut (`QRT - stopping to accept new messages, TX buffer full`). Sie ist die
Vorwarnung; die NOT-SENT-Zeile ist die Quittung zur einzelnen Nachricht.

Geaendert:

```cpp
/// A user message was refused because refusing() is true. Always yields
/// BP_NACK_QRT: the refusal is per message, unlike the episode notice.
/// (Before BP-07 this returned latchIfHigher(BP_NOTICE_QRT), which was
/// provably always BP_NOTICE_NONE -- refusing() implies latch_ >= QRT.)
BpNack onRefuse() { return BP_NACK_QRT; }
```

Der Rueckgabewert ist bewusst konstant. Die Methode bleibt, weil die Aufrufstelle so lesbar
bleibt und der Automat die Zustaendigkeit behaelt.

Fuer QTA braucht der Automat **nichts**: `sendMessage()` kennt `w < 0` selbst und setzt die
Rueckmeldung direkt ab. Die gelatchte QTA-Episodenmeldung aus `onSend()` bleibt unveraendert.

### `src/bp_notice_frame.h`

Entfaellt: `bpPeekDst()` samt Kommentarblock (rund 60 Zeilen).

Neu: der Textbauer, hier statt inline in `loop_functions.cpp`, damit die Suite ihn festnagelt.

```cpp
#define BP_NACK_TEXT_MAX 120   // BYTES, nicht Zeichen -- siehe Budget unten

/// prefix + bis zu BP_NACK_TEXT_MAX Bytes aus text, bei Kuerzung "..." angehaengt.
///
/// Zwei Regeln, beide budgetgetrieben:
///  1. Schneidet nie mitten in eine UTF-8-Sequenz. Der Text ist hier bereits
///     dekodiert (nach der %-Schleife), Umlaute und Emoji sind echte
///     Mehrbytefolgen; beim Kuerzen wird rueckwaerts ueber Folgebytes
///     (0x80..0xBF) gelaufen, bis eine Codepoint-Grenze erreicht ist.
///  2. Ersetzt '"', '\\' und jedes Byte < 0x20 durch ein Leerzeichen. Auf dem
///     EXTUDP-Weg wird der Text in JSON eingebettet; ohne diese Regel koennte
///     ein Text voller Anfuehrungszeichen beim Escapen auf das Doppelte
///     wachsen und den Datagramm-Puffer sprengen. So bleibt die Laenge 1:1.
///
/// Rueckgabe: geschriebene Bytes ohne NUL. out_len-sicher.
static inline size_t bpNackCompose(char *out, size_t out_len,
                                   const char *prefix, const char *text);
```

**Laengenbudget (Entscheidung E3: einheitlich 120, gleicher Text auf allen Wegen).**

| Weg          | Grenze                                 | Belegt                                     | Rest    |
| ------------ | -------------------------------------- | ------------------------------------------ | ------- |
| BLE / Web    | 255 (`UDP_TX_BUF_SIZE`)                | 6 Kopf + 18 `"OE1KBC-99>OE1XYZ-12:"` + 138 | **93**  |
| EXTUDP heute | 300 (`c_json` in `sendExternNotice()`) | 141 JSON-Geruest + 138 Nutztext = 279      | **21**  |
| EXTUDP neu   | 400 (siehe unten)                      | 279                                        | **121** |

Nutztext = `"QRT NOT SENT - "` (15) + 120 Text + `"..."` (3) = 138 Byte.
Das Geruest ist mit dem laengstmoeglichen Rufzeichen und Ziel gerechnet.

21 Byte Reserve sind zu wenig fuer eine Konstante, die spaeter jemand anfasst. Deshalb
zusaetzlich:

**`src/extudp_functions.cpp`, `sendExternNotice()`: `char c_json[300]` -> `[400]`**, mit dem
etablierten Muster aus `sendExtern()` direkt darueber — auf ESP32 auf dem Stack, auf nRF52
`static` in BSS (N-22: der Loop-Task-Stack ist dort 4 KB, und `sendExternNotice()` haengt
ueber `bpRoute()` an `sendMessage()`, laeuft also im Loop-Kontext). `sendExtern()` benutzt an
derselben Stelle bereits 500 Byte nach genau diesem Muster.

`BP_NACK_TEXT_MAX` zaehlt **Bytes**. Bei reinem ASCII sind das 120 sichtbare Zeichen, bei
deutschen Umlauten rund 105, bei Emoji rund 30. Das ist gewollt: der Puffer kennt nur Bytes,
und die Kuerzung muss die harte Grenze einhalten, nicht eine optische.

### `src/loop_functions.cpp`

**1. `bpEmitNotice()` aufspalten.** Der Transport-Switch wird von der Meldungsart getrennt,
damit Notice und Nack ihn teilen:

```cpp
static void bpDeliver(const char *text, MsgOrigin origin, const char *dst);   // nur der Switch
static void bpEmitNotice(BpNotice n, MsgOrigin origin, const char *dst);      // Marker + bpDeliver
static void bpEmitNack(BpNack n, MsgOrigin origin, const char *dst, const char *msg_text);
```

`bpDeliver()` ist wortwoertlich der heutige `switch(origin)`-Block, unveraendert
uebernommen — inklusive der Pruefung, ob BLE noch verbunden bzw. der Webserver noch an ist.

**2. Neue Konsolenzeile.** `[BP];notice;` bleibt reserviert fuer Zustandswechsel, damit die
bestehende Bench-Pruefung im Runbook gueltig bleibt. Der Textanteil haengt an `bLORADEBUG`
(E6), der Rest kommt immer:

```
Default:            [BP];nack;QRT;dst;232;ms;102400
Mit bLORADEBUG an:  [BP];nack;QRT;dst;232;ms;102400;txt;Test 09
```

`txt;` steht bewusst als letztes Feld: der Nachrichtentext kann Semikolons enthalten, und ein
Parser, der von links liest, bleibt so heil.

**2b. `bpNextMsgId()` einfuehren** (E5) und in `bpNoticeToPhone()` sowie im EXTUDP-Pfad
anstelle des direkten `millis()` benutzen.

**3. Rohtext-Peek loeschen.** Der Block bei Zeile 3538-3543 (`bpPeekDst`) entfaellt.

**4. `bp_origin_dst` hochziehen.** Die Zuweisung bei Zeile 3768 wandert nach oben, direkt vor
den verschobenen Refuse-Check — sie braucht nur `strDestinationCall`, das dort bereits final
ist.

**5. Refuse-Check verschieben und erweitern**, neue Position hinter Zeile 3736:

```cpp
if(bp_origin != ORIGIN_NONE && bp_state.refusing())
{
    Serial.printf("[BP];refuse;depth;%d;max;%d;ms;%lu\n",
                  txRingDepth(), (int)MAX_RING, (unsigned long)millis());

    bpEmitNack(bp_state.onRefuse(), bp_origin, bp_origin_dst, strMsg.c_str());
    return;   // Welle 3 macht daraus BP_SEND_REFUSED
}
```

Die gelatchte Episodenwarnung bleibt, wo sie ist: sie entsteht in `onSend()` bei der
Schwellenueberschreitung. Der Operator bekommt bei einem Burst also weiterhin genau eine QRS
und eine QRT — plus ab jetzt eine benannte Rueckmeldung pro tatsaechlich verlorener Nachricht.

---

## BP-08 — L3: kein erfolgreich aussehendes Echo fuer eine verworfene Nachricht

### Das Echo ist die Empfangsquittung

Auf BLE, Web und EXTUDP schickt der Node jede angenommene Nachricht als Frame zurueck. Das ist
die einzige Bestaetigung, dass der Text am Node angekommen und akzeptiert worden ist — es gibt
kein zweites Signal dafuer. Genau deshalb darf ein Echo nie fuer eine Nachricht erscheinen, die
der Node gar nicht senden wird.

Heute steht das Echo bei Zeile 3791, der Ringeintrag bei 3854. Die App sieht die Nachricht
also, bevor ueberhaupt versucht wurde, sie einzureihen. Das ist L3.

### Umbau

Der Block 3791-3832 (App-Echo, Gateway-Statusframe, T-Deck-Anzeige, `insertOwnTx()`,
`addLoraRxBuffer()`) wandert hinter `addTxRingEntry()`. Der Verwurffall verlaesst die Funktion
vorher:

```cpp
int w = addTxRingEntry(msg_buffer, (uint16_t)aprsmsg.msg_len, user_msg_status, "user_msg", 0);

if(w < 0)
{
    // Symmetrie (Operatorentscheidung 2026-09-01, E4): was nicht auf HF geht,
    // geht auch nicht ins Backbone. Kein Echo, kein UDP-Uplink, kein
    // EXTUDP-Spiegel, keine Eigen-TX-/Dedup-Buchung -- nur die Rueckmeldung
    // an den Absender. Die Nachricht ist vollstaendig nicht passiert.
    bpRoute(bp_state.onSend(txRingDepth(), true, millis()));   // Episoden-QTA
    bpEmitNack(BP_NACK_QTA, bp_origin, bp_origin_dst, strMsg.c_str());
    return;   // Welle 3 macht daraus BP_SEND_DROPPED
}

... Block 3791-3832 unveraendert hierher ...

bpRoute(bp_state.onSend(txRingDepth(), false, millis()));

... addNodeData() / sendExtern() / bConsoleText-Echo wie bisher ...
return BP_SEND_OK;
```

Reihenfolge innerhalb des Verwurfzweigs: erst die Episodenmeldung, dann die NOT-SENT-Zeile —
so, wie der Operator es im Beispiel aufgeschrieben hat (Q-Code-Zeile oben, Quittung darunter).

### E4: kein UDP-Uplink fuer einen verworfenen Frame

Das ist die groesste Verhaltensaenderung dieses Plans und sie geht ueber L3 hinaus.

Bisher lief `addNodeData()` auf einem Gateway mit IP-Verbindung **unabhaengig** vom Ausgang des
Ringeintrags: der LoRa-Ring konnte den Frame verwerfen, die Nachricht ging trotzdem an den
zentralen Server. Operatorentscheidung: das ist unsymmetrisch. Was nicht ueber LoRa gesendet
werden kann, geht auch nicht ins Backbone. Eine Nachricht betritt das Netz entweder ganz oder
gar nicht.

Praktische Folge: **ein Gateway, dessen TX-Ring einen lokal getippten Text verwirft, leitet ihn
nicht mehr an den Server weiter.** Der Absender bekommt stattdessen `QTA NOT SENT - <Text>` und
weiss, dass er wiederholen muss. Vorher ging die Nachricht ins Backbone, ohne dass die
HF-Nachbarn sie hoerten, und niemand erfuhr von der Asymmetrie.

Das vereinfacht den Code: die zuvor geplante `went_out`-Sonderregel fuer Gateways entfaellt
ersatzlos, es gibt nur noch `w < 0`.

### Nebenlaeufigkeit geprueft

`insertOwnTx()` und `addLoraRxBuffer()` wandern hinter den Ringeintrag. Ein Fenster gaebe es
nur, wenn der Sendevorgang in einem eigenen Task liefe und den Ring vor `insertOwnTx()` leeren
koennte. Tut er nicht: `doTX()` wird auf beiden Plattformen aus dem Hauptloop gerufen
(`esp32_main.cpp:2569`, `nrf52_main.cpp:1464`), und `sendMessage()` laeuft ebenfalls
ausschliesslich im Loop-Kontext (N-22-Kommentar in `sendMessage()` selbst). Die beiden koennen
sich nicht verschraenken.

Gewollte Nebenwirkung: fuer einen verworfenen Frame laufen `insertOwnTx()` und
`addLoraRxBuffer()` nicht mehr. Beide sind Buchhaltung fuer einen Frame, der nie auf die Luft
ging — der Dedup-Platz war bisher verschenkt.

---

## BP-09 — L4: der getippte Text bleibt stehen

`sendMessage()` gibt `int` zurueck statt `void`. Das Ergebnis-Enum wird **erst hier**
angelegt, nicht schon in Welle 1 — solange `sendMessage()` `void` ist, waere es unbenutzt und
ein `return BP_SEND_REFUSED;` wuerde nicht uebersetzen. In `src/backpressure.h`:

```cpp
/// Result of sendMessage(), so a caller can keep the operator's typed text
/// instead of clearing an input field for a message that never went out.
enum BpSendResult
{
    BP_SEND_OK      =  0,
    BP_SEND_REFUSED = -1,   ///< refused, BP_NACK_QRT
    BP_SEND_DROPPED = -2,   ///< dropped by the ring, BP_NACK_QTA
    BP_SEND_INVALID = -3    ///< length out of range, DM to own call
};
```

Betroffene Deklarationen (Scout-Vollerhebung 2026-09-01):

- `src/loop_functions.h:71`
- `src/nrf52/nrf52_ble.cpp:25` (eigenes `extern`)
- `test/test_getextern/test_getextern.cpp:59` (Recording-Stub der nativen Suite)

Keine C-Linkage-Falle: weder `loop_functions.h` noch `loop_functions_extern.h` stehen in einem
`extern "C"`-Block, und keine `.c`-Einheit bindet sie ein.

Alle `return;` in `sendMessage()` werden zu einem Wert:

| Stelle                              | Rueckgabe         |
| ----------------------------------- | ----------------- |
| Kommandopfad (`msg_text[0] == '-'`) | `BP_SEND_OK`      |
| Laenge `< 1` oder `> 160`           | `BP_SEND_INVALID` |
| DM an das eigene Rufzeichen         | `BP_SEND_INVALID` |
| Refuse-Check                        | `BP_SEND_REFUSED` |
| Ring hat verworfen (`!went_out`)    | `BP_SEND_DROPPED` |
| Ende der Funktion                   | `BP_SEND_OK`      |

Aufrufstellen — nur drei werten aus, die uebrigen bekommen ein `(void)`:

| Datei                            | Verhalten                                                      |
| -------------------------------- | -------------------------------------------------------------- |
| `t-deck/event_functions.cpp:717` | Eingabefeld und Tabwechsel nur bei `BP_SEND_OK`                |
| `t-deck-pro/ui_deckpro.cpp:1805` | dito                                                           |
| `web_functions.cpp:2193`         | `sendmessage refused` / `sendmessage dropped` statt `ok`       |
| `esp32_main.cpp:2972`, `:4287`   | `(void)`                                                       |
| `nrf52_main.cpp:1617`, `:2932`   | `(void)`                                                       |
| `extudp_functions.cpp:336`       | `(void)` — die Rueckmeldung geht ohnehin per Datagramm zurueck |

Am T-Deck heisst das konkret: `lv_textarea_set_text(text_input, "")` und
`lv_tabview_set_act(tv, 0, LV_ANIM_ON)` wandern in einen `if(rc == BP_SEND_OK)`-Zweig. Der
Operator sieht "Test 09" weiter im Eingabefeld und kann nach dem QRV einfach nochmal senden.

---

## Getroffene Entscheidungen

Operator, 2026-09-01. Alle vier sind oben eingearbeitet.

| Nr. | Frage                                                     | Entscheidung                                                                                                                   |
| --- | --------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| E1  | Q-Code-Zeile pro Episode oder pro abgewiesener Nachricht? | Episodenwarnung bleibt gelatcht. Pro verlorener Nachricht **ein** Frame, der den Code im Text traegt: `QRT NOT SENT - <Text>`. |
| E2  | Rahmung der NOT-SENT-Zeile?                               | Wie die bisherigen Meldungen: `msg_app_offline = true`, `msg_id = millis()`. Keine Message-ID wird verbraucht.                 |
| E3  | Kuerzung?                                                 | Einheitlich 120 Byte plus `...`, identischer Text auf BLE, Web und EXTUDP.                                                     |
| E4  | Gateway: Ring verwirft, UDP sendet trotzdem?              | Nein. Was nicht auf HF geht, geht auch nicht ins Backbone. `addNodeData()` entfaellt im Verwurffall.                           |

### E5 — `msg_id` muss eindeutig bleiben

Alle BP-Frames setzen heute `msg_id = millis()` (`bpNoticeFillFrame()` ueber
`loop_functions.cpp:3414`, `sendExternNotice()` ueber `extudp_functions.cpp:811`). Der
QTA-Pfad von BP-08 setzt **zwei** Frames aus demselben `sendMessage()`-Aufruf ab — die
Episodenmeldung und die NOT-SENT-Zeile. Beide holen sich `millis()` und bekommen mit hoher
Wahrscheinlichkeit denselben Wert. Der Dedup-Filter der Chat-App wuerde eine der beiden
verwerfen (Operatorhinweis 2026-09-01).

Ein gemeinsamer Helfer in `loop_functions.cpp`, den **jeder** BP-Frame benutzt — auch das QRV
aus dem Drain-Poll, das zufaellig mit einem Send zusammenfallen kann:

```cpp
// E5: msg_id muss ueber alle BP-Frames eindeutig bleiben, sonst schluckt der
// Dedup-Filter der Chat-App die zweite Meldung derselben Millisekunde. Der
// QTA-Pfad setzt zwei Frames aus einem sendMessage()-Aufruf ab.
// Kein Rollover-Problem: die Anhebung ist eine reine uint32-Addition, und ein
// Sprung ueber die millis()-Grenze hinaus normalisiert sich beim naechsten
// Aufruf mit hinreichend fortgeschrittener Uhr wieder von selbst.
static uint32_t bp_last_msg_id = 0;

static uint32_t bpNextMsgId(void)
{
    uint32_t id = millis();
    if(id <= bp_last_msg_id)
        id = bp_last_msg_id + 1;
    bp_last_msg_id = id;
    return id;
}
```

Ausdruecklich **nicht** angefasst: `addBLECommandBack()` (`loop_functions.cpp:666`) benutzt
ebenfalls `millis()` als `msg_id`, gehoert aber zum Kommandoantwort-Pfad und nicht zu BP-01.
Ausserhalb des Umfangs.

### E6 — Nachrichtentext nur im Debug-Log

Die Node-Konsole auf Port 2323 wird mit `tools/meshlogger.py` tagelang mitgeschnitten, und diese
Mitschnitte werden geteilt und ausgewertet. Bisher stehen dort ausschliesslich Metadaten. Der
`txt;`-Anteil des neuen Markers haengt deshalb an `bLORADEBUG`; der Rest der Zeile bleibt
unbedingtes `Serial.printf` wie die uebrigen BP-Marker:

```
Default:            [BP];nack;QRT;dst;20;ms;102400
Mit bLORADEBUG an:  [BP];nack;QRT;dst;20;ms;102400;txt;Hello World 17
```

Zu E2, zur Klarstellung: die NOT-SENT-Zeile ist bewusst **kein** echtes Echo, sondern eine
Meldung in Echo-Form. Sie traegt keine reale `msg_id`, weil im Abweisungsfall gar keine
existiert — BP-01 verbraucht bewusst keine. Damit bleibt Luecke L5 bestehen: die App erkennt
die Zeile nur am Textpraefix. Der Weg zu einer sauberen Zuordnung (`0x41`-Statusframe mit
neuem Statusbyte) ist in `docs/backpressure-flow-control.md` Kapitel 10 beschrieben und
braucht ein App-Release.

---

## Tests

### Fails-before-Nachweis fuer L1

Ein Unit-Test auf `onRefuse()` kann den Vorher-Zustand nicht abbilden, weil sich die Signatur
aendert. Der ehrliche Fails-before sitzt deshalb eine Ebene hoeher in
`test/test_bp_regression`, wo echter Ring und echte Statemaschine so verdrahtet sind wie in
`sendMessage()`:

```
test_flood_13_into_10_yields_five_nacks
  MAX_RING 10, 13 lokale Sends ohne Abfluss
  erwartet: 8 im Ring, 5 Abweisungen, 5 Nacks, 1 QRS, 1 QRT
  vorher:   5 Abweisungen, 0 Nacks   -> rot
```

### `test/test_backpressure`

- `test_refusal_announces_once_per_episode` umschreiben: Episodenmeldung weiterhin genau
  einmal, **und** `onRefuse()` liefert bei jeder der 20 Abweisungen `BP_NACK_QRT`. Der Test
  zementiert heute ein Verhalten, das nie eintritt — der Name bleibt richtig, der Rumpf nicht.
- Neu: der Nack veraendert weder `latch()` noch `state()`.
- Neu: nach `enterQuiet()` liefert `refusing()` false, also kein Nack mehr.

### `test/test_extern_notice_json`

- Neu: das laengstmoegliche Nutzsignal (`"QRT NOT SENT - "` + 120 Byte + `"..."`) mit dem
  laengstmoeglichen Rufzeichen und Ziel passt in den Datagramm-Puffer, und `externNoticeJson()`
  liefert eine Laenge > 0 (also kein abgeschnittenes JSON).

### `test/test_bp_notice_frame`

- `bpPeekDst`-Faelle loeschen (Funktion entfaellt).
- Neu fuer `bpNackCompose()`: kurzer Text; Text exakt auf `BP_NACK_TEXT_MAX` (120 Byte); zu
  langer Text bekommt `...`; **Kuerzung mitten in einer UTF-8-Sequenz schneidet auf der
  Codepoint-Grenze**; `"` und `\\` und Steuerzeichen werden zu Leerzeichen; leerer Text;
  `text == nullptr`; `out_len == 1`; `out_len` kleiner als das Praefix; `out == nullptr`.

### Verifikationsgate

```
pio test -e native      -f test_backpressure -f test_extern_notice_json
pio test -e native_aprs -f test_txring -f test_txring_flood \
                        -f test_bp_notice_frame -f test_bp_regression
pio test -e native
pio test -e native_aprs
```

Danach ein sauberer Neubau der betroffenen Ziele, **sequenziell** — parallele `pio run` auf
dasselbe Env beschaedigen `.pio/build`:

```
pio run -e heltec_v3 -e wiscore_rak4631 -e tbeam -e tdeck -e tdeck_plus
```

T-Deck und T-Deck Pro muessen mit gebaut werden, weil BP-09 ihre GUI-Aufrufstellen anfasst.

### Benchnachweis

Operatorvorgabe 2026-09-01: **auf allen beteiligten Knoten muss Gateway ausgeschaltet sein.**
Sonst kommen die Nachrichten ueber den zentralen Server bei den Nachbarn an und der
LoRa-Empfang ist nicht nachgewiesen.

**Lauf 1 — Flood und Quittungen, DK5EN-93 (Heltec V3, `MAX_RING 20`, QRT bei 16).**
25 Nachrichten per `::{TEST}...` auf die Konsole. Erwartung auf der Konsole:

```
[BP];notice;QRS;depth;5;max;20;...
[BP];notice;QRT;depth;16;max;20;...
[BP];refuse;depth;16;max;20;...     \
[BP];nack;QRT;dst;TEST;ms;...        > 9x
[BP];notice;QRV;depth;0;max;20;...
```

und in der App neun Zeilen `QRT NOT SENT - <Text>` statt Stille.

**Lauf 2 — LoRa-Empfang bei den Nachbarn.** Der Kernnachweis, dass die Abweisung nur die
abgewiesenen Nachrichten kostet und keine angenommene verschluckt: von den 25 gesendeten
muessen genau die **16 angenommenen** bei den Nachbarn ankommen und **keine der 9 abgewiesenen**.

Mithoerer: DK5EN-14 (T-Deck), DK5EN-92 (T-Beam), DK5EN-90 (RAK4631) und **DK5EN-98**, der mit
seiner grossen Antenne am meisten hoert. Auf **jedem** dieser Knoten und auf dem Sender muss
Gateway aus sein — DK5EN-98 spielt sonst selbst Gateway und die Nachrichten kaemen ueber den
Server statt ueber HF.

Auswertung ueber die 2323-Konsolen der Mithoerer (`tools/meshlogger.py`), nicht ueber mcmap:
mcmap sieht nur, was ein Gateway eingespeist hat, und Gateways sind fuer diesen Lauf aus.

**Lauf 3 — T-Deck DK5EN-14, Eingabefeld.** Bei einer abgewiesenen Nachricht muss der getippte
Text im Eingabefeld stehen bleiben (BP-09) und die NOT-SENT-Zeile in der Nachrichtenliste
erscheinen.

**Nicht bench-verifiziert: E4 (Uplink-Unterdrueckung).** Der Nachweis braucht ein aktives
Gateway und einen mcmap-Abgleich, was sich mit der Gateway-aus-Vorgabe der Laeufe 1-3 beisst.
E4 wird deshalb nur durch Code-Review und den nativen Test abgedeckt; ein spaeterer
Gateway-Lauf bleibt offen.

---

## Reihenfolge

Branch: **`tdeck-partial-refresh-trace`** (Operatorentscheidung 2026-09-01, kein neuer Branch).

Alle drei Schritte fassen `loop_functions.cpp` an, davon zwei denselben Funktionsrumpf. Das ist
**keine parallelisierbare Welle**, sondern eine Kette. Ein Commit pro Welle, jeweils erst nach
gruenem Gate.

| Welle | Inhalt                                                                                                                                                                 | Exklusive Dateien                                                                                                                                                                                                                                                                                 |
| ----- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1     | **BP-07** — `bpNextMsgId()`, `BpNack`/`BpSendResult`, `bpNackCompose()`, `bpDeliver()`-Abspaltung, Refuse-Check verschieben, `bpPeekDst()` loeschen, EXTUDP-Puffer 400 | `src/backpressure.h`, `src/bp_notice_frame.h`, `src/loop_functions.cpp`, `src/extudp_functions.cpp`, `test/test_backpressure/`, `test/test_bp_notice_frame/`, `test/test_extern_notice_json/`, `test/test_bp_regression/`                                                                         |
| 2     | **BP-08** — Echo hinter den Ringeintrag, Verwurfzweig, kein UDP-Uplink                                                                                                 | `src/loop_functions.cpp`                                                                                                                                                                                                                                                                          |
| 3     | **BP-09** — `sendMessage()` gibt `int` zurueck, acht Aufrufstellen                                                                                                     | `src/loop_functions.h`, `src/loop_functions.cpp`, `src/nrf52/nrf52_ble.cpp`, `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`, `src/web_functions/web_functions.cpp`, `src/t-deck/event_functions.cpp`, `src/t-deck-pro/ui_deckpro.cpp`, `src/extudp_functions.cpp`, `test/test_getextern/` |

Welle 1 laesst den Baum nicht rot zurueck: die Signaturaenderung an `onRefuse()` und ihre
Aufrufstelle liegen beide in dieser Welle.

Nach Welle 3 ein unabhaengiger Advisor-Durchlauf (`/fable-review`) ueber den gesamten Diff.

---

## Doku-Nachzug

| Datei                                   | Aenderung                                                       |
| --------------------------------------- | --------------------------------------------------------------- |
| `docs/backpressure-flow-control.md`     | L1-L4 nach "behoben", Kap. 8 auf Ist umstellen, L5 bleibt offen |
| `docs/BACKLOG.md`                       | BP-07/08/09 anlegen, BP-01-Zeile um den Nack-Pfad ergaenzen     |
| `docs/CHANGELOG-stability.md`           | neuer nummerierter Eintrag (zuletzt 161)                        |
| `docs/RESUME.md`                        | Stand fortschreiben                                             |
| `docs/automation-runner-runbook.md:160` | BP-01-Bench-Assert um `[BP];nack;` erweitern                    |

`tools/loganalyse.sh` wertet `[BP];`-Zeilen heute nicht aus und braucht keine Aenderung.

---

## Risiken

| Risiko                                                                               | Bewertung                                                                                                                                                                          |
| ------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **E4: Gateway leitet einen verworfenen Text nicht mehr an den Server**               | Bewusste Verhaltensaenderung, kein Nebeneffekt. Vorher ging die Nachricht ins Backbone, ohne dass die HF-Nachbarn sie hoerten und ohne dass es jemand erfuhr. Muss in den PR-Text. |
| Verschobener Refuse-Check laesst abgewiesene Nachrichten die Dekodierung durchlaufen | Reine Rechenzeit, keine Seiteneffekte. nRF52-Stack unbetroffen (Puffer in BSS).                                                                                                    |
| Echo kommt jetzt nach dem Ringeintrag                                                | Mikrosekunden. Kein Nebenlaeufigkeitsfenster, beide Pfade im Loop.                                                                                                                 |
| `insertOwnTx`/`addLoraRxBuffer` entfallen fuer verworfene Frames                     | Gewollt. Kein bestehender Test nagelt das fuer den Drop-Fall fest.                                                                                                                 |
| EXTUDP-Puffer 300 -> 400                                                             | Auf nRF52 nach dem N-22-Muster `static` in BSS, also kein Stackzuwachs. ESP32-Loopstack ist 8 KB, `sendExtern()` legt dort schon 2x500 ab.                                         |
| Ein Frame pro Sendeversuch statt einem pro Episode                                   | Rein lokal, nie ueber die Luft, immer Antwort auf eine Nutzeraktion. Durch die Tipprate begrenzt.                                                                                  |
| Neuer Marker `[BP];nack;`                                                            | Kein bestehender Parser liest `[BP];`-Zeilen.                                                                                                                                      |
| Signaturaenderung `sendMessage()`                                                    | 8 Aufrufstellen, alle im Repo bekannt. Compiler faengt jede uebersehene ab.                                                                                                        |
| L5 bleibt offen                                                                      | Die App muss die NOT-SENT-Zeile weiterhin am Textpraefix erkennen. Ein Nutzertext, der mit `QRT NOT SENT - ` beginnt, ist nicht unterscheidbar.                                    |
| Konfliktflaeche zum Upstream                                                         | `loop_functions.cpp` ist eine haeufig geaenderte Datei; drei Commits hintereinander in derselben Region erhoehen den Rebase-Aufwand.                                               |
