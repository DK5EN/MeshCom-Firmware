# Nachbarschaftsmatrix, Stufe 2: Wichtigkeit, Rang und prioritaetsgesteuertes Meshen

**Status:** Konzept v3, **final und implementierbar**, 2026-09-22. Ersetzt v1 (Betreiber-Sitzung)
und v2 (Zwischenstand gegen den Feldlauf). Nichts davon ist Code. Was sich gegenueber v2 geaendert
hat, steht in Abschnitt 2.6; die Zahlen stammen aus dem **Zwischenstand nach 11,9 von 24 h** und sind
nach dem Laufende (22.09. 19:20) mit `tools/nbrsnap.py` und `tools/nbrrelay.py` neu zu ziehen.

**Vorgaenger:** `docs/nachbarschaftsmatrix-campaign.md` (Stufe 1, was gebaut ist),
`docs/nachbarschaftsmatrix-verdict.md` (Advisor-Befunde), `docs/nbr-logformat.md` (Logformat),
`docs/adr-nc-importance-backoff.md` (ADR 02: Wichtigkeit aus dem HEY-`NC`, Slot-Mechanik, die
Guardrails E5 aus dem Feldausfall 60ea7d8), `docs/Relay-Prioritaeten-Verdict.html` (Overhear-Abbruch,
MPR-Kostenrechnung, Stufe "Zaehler zuerst").

---

## 1. BLUF

Das Ziel ist ein Flutnetz, das durch Mesh-Verkehr nicht zulaeuft: **weniger redundante Relays, ohne
eine einzige Zustellung zu verlieren.** Der Feldlauf auf DK5EN-98 liefert dafuer drei Befunde, die
das Konzept gegenueber v2 verschieben:

1. **Die Reihenfolge ist heute invertiert.** Der redundante Nachbar DL2UD-1 wiederholt einen Frame
   im Median 19 s nach der ersten Kopie, DB0ED-99 nach 26 s, der Super-Node DL2JA-2 (29 exklusiv
   versorgte Knoten) **nach 109 s**. Wer am meisten hoert, sendet am spaetesten.
2. **Die Ursache ist messbar der CSMA-Re-Arm:** jeder Empfang setzt den Backoff neu auf. Am eigenen
   Knoten kostet jeder waehrend der Wartezeit gehoerte Frame rund 6 bis 9 s zusaetzlich (Abschnitt
   2.4). Ein Knoten mit 40 Nachbarn erreicht das Ende seines Backoffs fast nie. Die 350-ms-Slot-Ordnung
   aus ADR 02 ist gegen diese Groessenordnung wirkungslos.
3. **Jeder Frame liegt dreifach in der Luft.** DK5EN-98 hat 904 Frames wiederholt; bei 72 bis 79 %
   davon wiederholte danach noch ein anderer Nachbar denselben Frame, bei 11 bis 17 % schon vorher.
   Je Frame hoert der Knoten im Median zwei fremde Wiederholungen.

Daraus die **Entscheidung** (Abschnitt 5):

- **Sendeentscheidung je Frame aus der Matrix**, nicht je Frametyp: _Bedarfsmenge_ = die direkten
  Nachbarn, die den Frame noch nicht haben koennen. Ist sie leer oder anderweitig gedeckt → **Fall B**
  (langer Backoff, Abbruch); enthaelt sie einen Knoten, den sonst niemand mit dem Frame erreicht →
  **Fall A** (kurzer Backoff, nie Abbruch).
- **Den bestehenden Overhear-Abbruch fuer Relay-Eintraege oeffnen**, freigegeben nur, wenn die
  gehoerten fremden Wiederholungen die Bedarfsmenge vollstaendig decken. Kein Zaehler-Cancel, kein
  Modell: nur gegen eine tatsaechlich gehoerte Aussendung.
- **Re-Arm-Kappung fuer Fall A:** ein Relay, an dem jemand exklusiv haengt, darf nicht beliebig oft
  durch fremde Empfaenge zurueckgesetzt werden. Das ist der Hebel fuer den Super-Node.
- **Keine Election, kein `HN`, kein neues Frame, kein neues Feld.** Der Rang ist lokal berechenbar
  (bestaetigt, Abschnitt 2.1) und wird fuer die Sendeentscheidung nicht einmal gebraucht: sie haengt
  nur an der eigenen Bedarfsmenge. Der Rang bleibt Anzeige und Diagnose. Ein `HN`-Ziel wuerde von
  jeder heutigen Firmware verworfen (Abschnitt 5.7).

Die Fehlrichtung ist ueberall dieselbe: fehlt der Matrix Wissen, faellt der Knoten auf heutiges
Fluten zurueck. Nichts unterdrueckt auf Verdacht.

---

## 2. Feldevidenz: DK5EN-98, 21.09. 19:20 bis 22.09. 07:17

Quelle: `martin@rpizero.local:~/meshlog/dk5en-98/`, Mitschnitt mit `--nbrdebug` und `--setlog`.
Ausgewertet mit `tools/nbrlog.py --since 2026-09-21` (Bericht
`~/Desktop/nbr-feldlauf-20260922-zwischenstand.md`) sowie den beiden neuen Skripten
`tools/nbrsnap.py` (Geraetesicht je Schnappschuss) und `tools/nbrrelay.py` (Relay-Baseline).

| Groesse                                         | Wert                                                    |
| ----------------------------------------------- | ------------------------------------------------------- |
| Dauer, Reboots                                  | 11,9 h, 0                                               |
| `[NBR]`-Zeilen, Schnappschuesse, Verdraengungen | 11.212, 46, 56                                          |
| Direkt gehoerte Frames (ME-Zeilen)              | 3.112, mittlerer Abstand 13,8 s                         |
| Direkte Nachbarn, Knoten in 2 Hop               | 6, 39 (alle benannten Nachbarn sind Heltec V3, `hw` 43) |
| Empfangene Frames nach Typ (inkl. Kopien)       | TEXT 659, POS 961, HEY 1.492                            |
| davon Duplikate (`DUP:d`)                       | TEXT 91 %, POS 42 %, HEY 53 %                           |
| Eigene Relays (`TX src=r`)                      | 904: TEXT 6, POS 252, HEY 646                           |
| Relay-Ablehnungen (`RLY q=`)                    | TEXT 56 `gwcap`, POS 259 `gwcap`, HEY 50 `hop0`         |
| Eigene Wartezeit im Ring bis TX (`wait=`)       | Median 5,9 s, p90 20 s, Maximum 102 s                   |

### 2.1 Der Rang ist eindeutig, stabil und auf dem Geraet sichtbar

v2 hatte die Stabilitaet aus Abschnitt 5 des `nbrlog.py`-Berichts gelesen. Diese Spalte ist
`<verdict>` (die Gegenfrage), nicht `<meshneed>`. Die Nachpruefung auf der **richtigen** Spalte
bestaetigt die Aussage trotzdem, und `tools/nbrsnap.py` rekonstruiert zusaetzlich, was das Geraet
in jedem Schnappschuss tatsaechlich in seinen 21 Zeilen hatte:

| Nachbar  | Union 11,9 h: hoert / exklusiv | Geraetesicht je Schnappschuss: hoert / exklusiv (Median, Spanne) | `<meshneed>` ueber 46 Schnappschuesse |
| -------- | ------------------------------ | ---------------------------------------------------------------- | ------------------------------------- |
| DL2JA-2  | 40 / **29**                    | 18 / **10** (5 bis 11)                                           | MESH 46 von 46                        |
| DB0ED-99 | 9 / **1**                      | 8 / **1** (1 bis 3)                                              | MESH 46 von 46                        |
| DK5EN-1  | 4 / 1                          | 3 / 0 bis 1                                                      | RED 12 von 12 (Blatt verdraengt)      |
| DL2UD-1  | 2 / 0                          | 2 / 0                                                            | RED 46 von 46                         |
| DL2JA-1  | 0 / 0                          | 0 / 0                                                            | RED 45 von 45                         |
| DK5EN-92 | 0 / 0                          | 0 / 0                                                            | RED 4 von 4                           |

**Vorbehalt, erst nach v3 erkannt:** DL2JA-2 traegt das GW-Flag (`ROW`-Flags 15). Ein Gateway setzt
auch Serverframes mit `<Absender>,DL2JA-2` auf LoRa; von seinen 29 "exklusiven" Knoten haben nur 12
eine Position im Umkreis (DL2JA-3, DF2KX-12, DG3MNF-4, DG7RJ-8, DF8RD-1, DL5AZZ-99, DL5RAS-2,
DM6CS-12, DO1PIT-10, DO7GH, DB0MMR-12, DD7MH-55), OE3/OE5/OE6/SP-Rufzeichen sind Serverfutter.
Der Rang bleibt (12 zu 1), die Zahl 29 nicht. Die Sendeentscheidung ist davon unberuehrt (sie
rechnet nur auf meinen Direktzeilen); die Anzeige `#X` eines Gateway-Nachbarn ist um dessen
Serverzufuhr ueberhoeht und braucht spaeter einen Entfernungsfilter.

Die Kappung durch `NBR_MAX_ROWS = 21` kostet **zwei Drittel der Zahl, aber nicht die Ordnung**: 10 zu
1 statt 29 zu 1. Ein lokaler Rang ist also **heute schon berechenbar**, `nbrRowMeshNeed()` rechnet
ihn und wirft nur die Zahl weg (`src/nbr_matrix.cpp:669`).

### 2.2 Die rohe Nachbarzahl taugt nicht (unveraendert aus v2)

DB0ED-99 hoert 9 Knoten, 8 davon hoert DL2JA-2 auch; sein exklusiver Beitrag ist ein Knoten
(DB0FHR-12). `NCT` aus dem HEY-Bericht ueberschaetzt ihn um den Faktor 10. `NCT` bleibt Anzeige.

### 2.3 Tabellendruck: real, aber nicht auf der kritischen Menge

43 von 46 Schnappschuesse voll, 56 Verdraengungen. **Aber: nur 2 der 56 Verdraengungen trafen einen
direkten Nachbarn** (DK5EN-92 nach einer einzigen Sichtung, DK5EN-1 nach 4,5 h Stille). Die
Sendeentscheidung aus Abschnitt 5 braucht ausschliesslich die Zeilen der direkten Nachbarn; die
2-Hop-Zeilen dienen nur dem Rang der Nachbarn und der Anzeige. Damit ist der Tabellendruck ein
Anzeigeproblem, kein Entscheidungsproblem, sofern die Direktzeilen bei der Verdraengung geschuetzt
werden (Abschnitt 5.8, Punkt 1).

**Ein Stufe-1-Fehler, den erst diese Nachrechnung zeigt:** die Firmware fuehrte in den
ROW-Zeilen **neun Rufzeichen als direkt gehoert** (`<meshneed>` != `NA`), fuer die es im ganzen
Lauf keine einzige ME-Zeile gibt: OE1XAR-33 in 44 von 46 Schnappschuessen, DG2NPE-10 in 16,
DM3KS-12 in 15, DK4DO-1 in 8, SP3GJ-9 in 7, DO5NE-1 in 6, OE5HWN-6 in 4, DO2QG-1 in 3, IS0QLX-11 in 2. Ursache: DK5EN-98 ist Gateway und setzt Serverframes mit dem Pfad `<Absender>,DK5EN-98` auf LoRa
(174 `TX src=g`, `src/esp32/udp_frame_esp32.cpp:381`); wiederholt ein Nachbar diese Aussendung,
kommt `<Absender>,DK5EN-98,<Nachbar>` zurueck, und `nbrNoteFrame()` trifft aus dem Paar
(`<Absender>`, `DK5EN-98`) die Zelle `cell[Absender][0]` -- "ich habe den Absender gehoert", obwohl
nie ein Funkempfang stattfand. Im Lauf blieben alle neun `RED`, weil DL2JA-2 sie ebenfalls hoert;
ein serverfremder Absender ohne zweiten Hoerer stuende aber als "exklusiv, Mesh needed" auf der
Web-Seite. Fix in 5.8, Punkt 0; `tools/nbrlog.py` und `tools/nbrsnap.py` sind nicht betroffen, sie
lesen Direktheit aus ME-Zeilen.

### 2.4 Reihenfolge und Ursache: der CSMA-Re-Arm

Je fremdem Relayer die Verzoegerung seiner Kopie gegen die erste gehoerte Kopie desselben Frames
(`tools/nbrrelay.py`, 904 von DK5EN-98 wiederholte Frames):

| Relayer  | Kopien | vor eigener TX | nach eigener TX | Verzoegerung Median | p90     | exklusive Knoten |
| -------- | ------ | -------------- | --------------- | ------------------- | ------- | ---------------- |
| DK5EN-98 | 904    | —              | —               | **5,9 s**           | 19,8 s  | 0                |
| DL2UD-1  | 590    | 103            | 487             | **18,8 s**          | 41,3 s  | 0                |
| DB0ED-99 | 301    | 38             | 263             | **25,7 s**          | 104,4 s | 1                |
| DL2JA-2  | 197    | 6              | 191             | **109,0 s**         | 206,3 s | 29               |

Die Ursache ist am eigenen Knoten messbar. `OnRxDone()` setzt bei **jedem** Empfangsende
`iReceiveTimeOutTime` und `csma_timeout` neu (`src/lora_functions.cpp:662` und `:1746`); die
Wartezeit eines Relays bei Ringtiefe 1 gegen die Zahl der waehrend des Wartens gehoerten Frames:

| gehoerte Frames waehrend der Wartezeit | n   | Wartezeit Median | p90    |
| -------------------------------------- | --- | ---------------- | ------ |
| 0                                      | 585 | 5,8 s            | 11,2 s |
| 1                                      | 141 | 12,4 s           | 19,3 s |
| 2                                      | 65  | 18,5 s           | 27,5 s |
| 3                                      | 14  | 28,9 s           | 35,0 s |
| 4                                      | 5   | 37,8 s           | 50,3 s |

Jeder Frame im Ohr kostet einen weiteren vollen Backoff. DK5EN-98 hoert alle 14 s einen Frame und
kommt meist durch. DL2JA-2 hoert 40 Knoten; bei drei Kopien je Frame ist sein Abstand zwischen zwei
Empfaengen kleiner als sein Backoff, und 109 s Median sind genau das, was diese Tabelle fuer einen
solchen Knoten voraussagt. **Der Super-Node ist nicht langsam, er ist TX-verhungert.** Und jede
redundante Kopie, die die kleinen Knoten in die Luft setzen, verlaengert sein Verhungern.

### 2.5 Redundanz je Frame

Auch Frames, die DK5EN-98 selbst nicht wiederholt hat (`gwcap`, `hop0`), kamen mit 1 (305 Frames),
2 (212) oder 3 (129) fremden Wiederholungen an. Link-Asymmetrie war im Lauf nicht zu sehen: alle
vier Nachbarn, die DK5EN-98 wiederholt haben, wurden auch direkt gehoert; DL2JA-1 und DK5EN-92 haben
nie etwas von DK5EN-98 wiederholt (Mesh aus oder zu schwach), sind aber direkt zu hoeren.

### 2.6 Was v2 falsch oder unvollstaendig hatte

| Aussage (v2)                                                        | Befund                                                                                                                                                                                                                           |
| ------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| "Die Rangfolge ist stabil ueber 46 Schnappschuesse"                 | **Richtig, aber aus der falschen Spalte gelesen.** Bestaetigt auf `<meshneed>` (2.1).                                                                                                                                            |
| "Der Engpass ist die quadratische Zellenmatrix"                     | **Nur fuer die Anzeige.** Fuer die Sendeentscheidung reichen die Direktzeilen, die in 11,9 h zweimal verdraengt wurden, beide nach Stunden Stille (2.3).                                                                         |
| "Text ist unter 1 % des Relay-Volumens" (aus ADR 02 uebernommen)    | **Hier nicht.** 21 % der direkt gehoerten Frames sind Text; wiederholt wurden davon nur 6, weil `max_hop_text` fast alles per `gwcap` stoppt. Die Absenkung muss an POS und HEY haengen, das bleibt.                             |
| "Prioritaet und Rang ordnen die Relays" (ADR 02, Slot-Mechanik)     | **Unwirksam in dieser Groessenordnung.** Die Reihenfolge wird von Ringtiefe und Re-Arm bestimmt, in Zehnersekunden, nicht in 350-ms-Slots (2.4).                                                                                 |
| `Wichtigkeit = Exklusiv x f(Reichweite)`                            | **Gestrichen als Steuergroesse.** Reichweite zaehlt doppelt (ein weit entfernter exklusiver Partner ist schon exklusiv), haengt an Zeile 0 (Verdict L3) und braucht einen Kalibrierdatensatz, den es nicht gibt. Bleibt Anzeige. |
| Bitfeld-Naeherung als Voraussetzung fuer den Rang                   | **Herabgestuft auf Anzeigequalitaet.** Die Ordnung ueberlebt die Kappung (2.1); die Sendeentscheidung braucht kein 2-Hop-Wissen (2.3).                                                                                           |
| "Zeile 0 und Spalte 0 sind die Wahrheit ueber Direktheit" (Stufe 1) | **Nicht auf Gateways.** Das Echo eines vom Server eingespeisten Frames schreibt `cell[Absender][0]`; neun falsche Direktzeilen im Lauf (2.3).                                                                                    |
| Election ueber HEY als Rueckfallebene                               | **Gestrichen.** Ein `HN`-Ziel verwirft jede heutige Firmware beim Dekodieren (5.7); der Rang wird fuer die Sendeentscheidung nicht gebraucht.                                                                                    |

---

## 3. Ausgangslage im Code (Branch `feature-neighbour-matrix`, 73132fdf)

| Baustein                      | Ort                                                                       | Aussage                                                                                                                     |
| ----------------------------- | ------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| Kreuzmatrix "Y hat X gehoert" | `src/nbr_matrix.{h,cpp}`, `cells[X][Y]`                                   | 2-Hop-Fenster, 12 h Verfall, N = 21/13/11 je Board, Zeile 0 = ich                                                           |
| Direkt gehoert                | `cells[X][0]` frisch                                                      | Grundlage aller Urteile; wird heute auch aus dem Pfadpaar (X, ich) eines Echos geschrieben, nicht nur beim Empfang (2.3)    |
| Urteil `nbrExclusive()`       | `src/nbr_matrix.cpp:631`, `nbrRowExclusive()` `:618`                      | Zeilen, die im Fenster nur ich hoere; Hoerer koennen auch 2-Hop-Zeilen sein                                                 |
| Urteil `nbrRowMeshNeed()`     | `src/nbr_matrix.cpp:669`                                                  | Muss der Nachbar selbst meshen? Zaehlt die unabgedeckten Knoten intern, gibt nur `NA`/`MESH`/`RED` zurueck                  |
| Verdraengung `nbrPlanRow()`   | `src/nbr_matrix.cpp:130`                                                  | Opfer = aelteste Zeile 1..N-1, ohne Ruecksicht auf Direktheit                                                               |
| Haken in `OnRxDone()`         | `src/lora_functions.cpp:815`                                              | vor der Duplikatpruefung, mit HEY-Ziel `HG` und Signalbericht                                                               |
| Overhear-Abbruch im TX-Ring   | `src/lora_functions.cpp:675-719`                                          | scannt bei jedem Empfang nach `msg_id`, ueberspringt `RING_STATUS_DONE`/`READY`; Relays sind `DONE` (`:1668`, `"rx_relay"`) |
| CSMA-Re-Arm                   | `src/lora_functions.cpp:662`, `:1746`                                     | jedes Empfangsende setzt Anker und Timeout neu                                                                              |
| Prioritaetsklassen            | `src/txring_functions.cpp:93`, `src/configuration_global.h:424-444`       | ACK/DM 1, Gruppe 2, Text-Relay 3, POS 4, HEY 5; Basis 3000/3000/4500/5500/5500 ms, 10 Slots je 35 ms                        |
| Backoff-Rechnung              | `src/lora_functions.cpp:2625` `csma_compute_timeout_prio()`               | Basis je Klasse, Retry-Reduktion, Jitter                                                                                    |
| Ring-Seitenfelder             | `ringPriority[]`, `ringEnqueueTime[]`, `ringSource[]`, `retryCount[]`     | je Slot; `ringSource` unterscheidet `'r'` (alle `rx_*`), `'o'`, `'g'`                                                       |
| Frame-Kopfbyte 5              | `src/aprs_functions.cpp:180-192`, `:1020-1032`                            | Hop-Nibble + 0x80 server, 0x40 track, 0x20 app_offline, 0x10 mesh — **alle acht Bits belegt**                               |
| Zielpruefung beim Dekodieren  | `src/aprs_functions.cpp:357`, `src/regex_functions.cpp:9-31`              | nur `H`, `HG` und Rufzeichen mit Ziffer passieren; alles andere → Frame verworfen                                           |
| HEY-Nutzlast                  | `src/loop_functions.cpp:5046` `sendHey()`, `src/aprs_functions.cpp:1134`  | `R<NCT>;` plus je Relais `NCT,RSSI,SNR;`; `nbr_matrix.cpp` ordnet Gruppe i dem Paar (i-1, i) zu                             |
| POS-Beacon-Tags               | `src/loop_functions.cpp:4527` (`/N<n>`), `src/aprs_functions.cpp:745-800` | `/X=`-Tags werden einzeln gesucht, unbekannte Tags stoeren den Parser nicht                                                 |
| Trickle                       | `src/lora_functions.cpp:1071`, `configuration_global.h:454-456`           | eigener HEY unterdrueckt bei >= 2 gehoerten HEYs je Intervall; dichte Knoten senden selten (`docs/hey-supp.md`)             |
| Instrumente im Mitschnitt     | `src/setlog_lines.{h,cpp}`                                                | `[LOG]`-RX-Zeile mit `DUP:d`/`OWN:e` (nicht `y`), `RLY q=<Grund> prio slot`, `TX prio src wait q cad`                       |
| Web-Seite (die zwei Tabellen) | `src/web_functions/web_functions.cpp:1574` `sub_page_neighbours()`        | Kreuztabelle + Zeilentabelle; Scaffold-CSS nur auf `#content_inner > table`                                                 |

Zwei Punkte, die nicht neu "entdeckt" werden duerfen: **der Overhear-Abbruch existiert** und ist
fuer Relays per Status ausgenommen, nicht vergessen. Und **die Instrumente fuer die Vorher-Messung
existieren**: `RLY`, `TX` und die RX-Zeile mit Pfad reichen fuer alle Zahlen in Abschnitt 2.

---

## 4. Begriffe und Modell

Alle Mengen sind ueber die Matrix definiert. `frisch` heisst innerhalb `NBR_WINDOW_MIN` (12 h) und
gesetzt. `cell[X][Y]` heisst "Y hat X gehoert".

| Begriff           | Definition                                                                                                                                                                                                                                                |
| ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Direkt(X)         | `cell[X][0]` frisch: ich hoere X.                                                                                                                                                                                                                         |
| HoertMich(X)      | `cell[0][X]` frisch: X hat mich gehoert (sichtbar nur, wenn X meine Frames wiederholt).                                                                                                                                                                   |
| Abhaengige **D**  | `{X : Direkt(X) oder HoertMich(X)}`, X != 0. Die Vereinigung beider Richtungen ist das groesste beobachtbare Publikum meiner Aussendung.                                                                                                                  |
| Pfad(F)           | die Rufzeichen in `msg_source_path` des Frames F, Absender zuerst, letzter Hop L zuletzt.                                                                                                                                                                 |
| HatF(X)           | X in Pfad(F), oder es gibt ein P in Pfad(F) mit `cell[P][X]` frisch: X hat P gehoert, also F wahrscheinlich schon von P.                                                                                                                                  |
| **Bedarf(F)**     | `{X in D : nicht HatF(X)}` — wer von meinen Abhaengigen F noch nicht haben kann.                                                                                                                                                                          |
| Deckt(M, X)       | `cell[M][X]` frisch: X hoert M. Nur direkte M zaehlen, denn nur deren Wiederholung kann ich hoeren.                                                                                                                                                       |
| Alternative(X, F) | es gibt ein direktes M != ich mit HatF(M) und Deckt(M, X): jemand anderes, der F hat, erreicht X.                                                                                                                                                         |
| **Allein(F)**     | `{X in Bedarf(F) : keine Alternative(X, F)}` — die Knoten, die F **nur** von mir bekommen koennen.                                                                                                                                                        |
| **Fall A**        | Allein(F) nicht leer. Relay mit Vorrang, nie Abbruch.                                                                                                                                                                                                     |
| **Fall B**        | Allein(F) leer. Relay mit Nachrang; Abbruch, sobald gehoerte Wiederholungen Bedarf(F) decken.                                                                                                                                                             |
| **Kein Wissen**   | Pfad ungueltig oder D leer (leere Matrix nach Boot/Reset): weder A noch B, das Relay laeuft wie heute (`known == false`, Advisor-Fund 2026-09-22).                                                                                                        |
| E_self            | `{X : Direkt(X), kein direktes M != ich mit cell[X][M] frisch}`: direkt gehoerte Knoten, die kein anderer direkter Nachbar hoert (Beobachtungsrichtung wie `nbrExclusive()`, aber nur direkte Hoerer). Anzeige und Diagnose, nicht die Sendeentscheidung. |
| #X(N), Rang       | fuer einen direkten Nachbarn N die Zahl der Knoten, die N hoert und sonst niemand in meiner Hoerweite (heute im Inneren von `nbrRowMeshNeed()`). Super-Node = groesstes #X. Anzeige.                                                                      |

Zwei Annahmen, beide benannt und beide in der sicheren Richtung falsch:

- **Symmetrie.** `Deckt(M, X)` liest "X hat M gehoert" und schliesst auf "X hoert M". Fehlt die
  Kante, weil X nie etwas von M wiederholt hat (Mesh aus, Dedup-Schatten aus
  `Relay-Prioritaeten-Verdict.html` Abb. 3), gilt X als ungedeckt → Fall A → ich relaye. Fluten.
  **Nachtrag 2026-09-22 (Feldlauf 2):** genau das blockierte DK5EN-98 -- DL2JA-1 relayt nie (vier
  Wochen Logs), stand deshalb dauerhaft in der Allein-Maske und zwang zusammen mit dem Blatt
  DK5EN-1 96 % aller Relays in Fall A. Seitdem gibt es `--nbrsym on|off` (Default an, gespeichert):
  fehlt "X hat M gehoert", gilt fuer die Stufe-2-Entscheidung ersatzweise "M hat X gehoert" mit
  SNR >= -16 dB (Betreiberwert). Keine Rechnung gegen Endstufen; jede Annahme wird als
  `[NBR]|SYM` geloggt. Die Stufe-1-Urteile bleiben beobachtungsbasiert. Details:
  `docs/nbr-logformat.md`, Abschnitt `--nbrsym`.
- **Beobachtbarkeit.** Ein Knoten, der mich hoert, den ich nie hoere und der nie etwas wiederholt,
  ist in keiner Matrix. Er wird von keinem lokalen Verfahren geschuetzt; im Lauf gab es keinen (2.5).
  Das ist die eine Luecke, die nur ein HELLO mit Nachbarliste schliessen wuerde, und dessen
  Sendezeit hat der Verdict abgelehnt.

---

## 5. Die Sendeentscheidung

### 5.1 Beim Einreihen des Relays (`lora_functions.cpp:1668`)

```
need  = 0; alone = 0                       // Bitmasken ueber Zeilenindizes, uint32_t
for X in 1..N-1 mit Direkt(X) oder HoertMich(X):
    if HatF(X): continue
    need |= bit(X)
    if not Alternative(X, F): alone |= bit(X)
ringNeed[slot]  = need
ringAlone[slot] = alone
case = alone ? A : B
```

Kosten: hoechstens (N-1) x (|Pfad| + N-1) Zellenzugriffe, bei N = 21 und 8 Pfad-Token rund 600
Vergleiche je Relay, ohne Speicheranforderung. Zwei neue Seitenfelder je Ring-Slot,
`2 x MAX_RING x 4 B = 160 B` BSS. Die Masken werden nur fuer Eintraege aus `"rx_relay"` gesetzt;
`rx_ack_fwd`, `rx_dm_ack_*` und alles andere bekommen `need = alone = 0` **und** ein Kennbit, das sie
vom Abbruch ausschliesst (heute teilen sie sich `ringSource == 'r'`).

**Prioritaet nach Fall, nicht nach Typ.** Die Klassen 3/4/5 bleiben bestehen, bekommen aber je Fall
einen Versatz:

| Fall | Basis                         | Slots         | Re-Arm bei fremdem Empfang                                                      |
| ---- | ----------------------------- | ------------- | ------------------------------------------------------------------------------- |
| A    | wie heute je Typ              | 0..2 (vorn)   | **gekappt:** nach `CSMA_REARM_MAX_A` Neustarts nur noch Schutzabstand, dann CAD |
| B    | wie heute + `CSMA_B_EXTRA_MS` | 7..9 (hinten) | wie heute, voller Neustart                                                      |

`CSMA_B_EXTRA_MS` ist der bewusst lange Nachrang: er muss die natuerliche Flut der Alt-Firmware
abwarten, damit der Abbruch greifen kann. Am gemessenen Standort kommen fremde Kopien nach 19 bis
26 s (Median); ein Wert von 15 bis 20 s faengt die Haelfte, 30 s die meisten. Startwert 20 s, offen
(Abschnitt 9). `CSMA_REARM_MAX_A` = 2 Neustarts als Startwert; danach wartet der Knoten nur noch den
Schutzabstand nach dem Empfangsende (`CSMA_SLOT_SIZE`-Vielfaches) und geht in den CAD, der weiter
vor Kollisionen schuetzt.

Text bleibt ausgenommen vom Nachrang (Menschen warten darauf): Fall-B-Text bekommt heutige Basis
und Slots, aber den Abbruch.

### 5.2 Beim Mithoeren (`lora_functions.cpp:675-719`)

Der bestehende Scan sieht Relay-Eintraege heute nicht. Er bekommt einen zweiten Zweig, der nur
dann feuert, wenn der empfangene Frame eine **fremde Wiederholung** ist:

```
if Pfad(F) hat >= 2 Token und L != ich und Direkt(L):     // L = letzter Hop
    for slot mit gleicher msg_id, Kennbit "rx_relay":
        if ringAlone[slot] != 0: continue                  // Fall A: nie
        ringNeed[slot] &= ~hearers_of(L)                   // alle X mit Deckt(L, X)
        if ringNeed[slot] == 0:
            Slot freigeben, Zaehler + Logzeile
```

`hearers_of(L)` ist die Zeilenmaske aller X mit `cell[L][X]` frisch, ein Spaltendurchlauf. Der
Absender-Retry (Pfad mit einem Token) loest nichts aus; die eigene Echo-Kopie (L == ich) auch nicht.
Ein Slot, der gerade sendet (`RING_STATUS_SENT`, `EXT_PENDING`), wird wie heute uebersprungen.

Gegen die ADR-02-Guardrails E5 geprueft:

| E5-Guardrail          | Hier                                                                                                                                                                                                                                                                                                    |
| --------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| k >= 2 Duplikate      | **nicht noetig**: der Abbruch ist deckungsbasiert, nicht zaehlbasiert. Eine Kopie von L bricht nur ab, wenn L nachweislich jeden aus Bedarf(F) erreicht.                                                                                                                                                |
| Sole-Provider-Veto    | **strukturell**: `ringAlone != 0` verhindert jeden Abbruch. Das Paar-Cancel-Beispiel des ADR (Blatt L hoert nur R1 und R2, Hub H sendet) kann nicht eintreten: bei R1 und R2 ist `cell[H][L]` nicht frisch, H deckt L nicht, kein Abbruch. Wiederholt R1, sieht R2 `cell[R1][L]` und bricht korrekt ab. |
| randomisierter Cancel | **nicht noetig**, weil kein korrelierter Zaehler; bleibt als Tunable `NBR_CANCEL_PCT` (Default 100) fuer den Beta-Cluster.                                                                                                                                                                              |
| Logzeile je Abbruch   | `[NBR]                                                                                                                                                                                                                                                                                                  | CANCEL | <up> | <msg_id> | <typ> | <L> | <need_vorher> | <need_nachher>`und ein Zaehler in`--info`. |

### 5.3 Rang und Super-Node ohne Election

Die Sendeentscheidung braucht den Rang nicht: jeder Knoten rechnet nur seine eigene Bedarfsmenge,
und die Ordnung entsteht daraus von selbst. Wer viele Knoten allein versorgt, ist Fall A und sendet
vorn; wer niemanden allein versorgt, ist Fall B, wartet und bricht ab. Zwei Fall-A-Knoten
nebeneinander loesen sich ueber CAD und den Abbruch (der Verlierer prueft, ob der Gewinner seinen
Bedarf deckt). Gleichstand im Slot: `hash(eigenes Rufzeichen XOR msg_id)` wechselt je Frame, ist also
deterministisch je Frame und fair ueber viele Frames — das erledigt den offenen Punkt "Tiebreak".

Der Rang bleibt trotzdem sichtbar, weil er das wertvollste Diagnoseprodukt der Matrix ist
(Verdict 4.3: eine ueber Wochen exklusive Zeile plus ein chronisch verhungernder Hoerer ist der
dokumentierte Fall fuer einen zweiten Standort):

- `nbrRowMeshNeed()` liefert zusaetzlich die Zahl (2b).
- `E_self` aus `nbrExclusive()` mit der Direktregel aus Abschnitt 4 (heute zaehlen auch 2-Hop-Hoerer
  als Deckung; fuer die Anzeige "exklusiv" ist das zu grosszuegig, weil ich deren Wiederholung nie
  hoeren kann).
- Super-Node = direkter Nachbar mit groesstem #X, sofern #X >= 2 und mindestens doppelt so gross
  wie der Zweite. Sonst "kein Super-Node in Hoerweite".

### 5.4 Gateway-Ausnahme: gebaut

v2 wollte exklusiv versorgte Knoten mit `GW = on` aus der Bedarfsmenge streichen und hielt das fuer
unbelegt. Der Betreiber hat es am 2026-09-22 geklaert: der Server verteilt in erster Naeherung jeden
Frame an jedes Gateway, und ein Gateway meldet in seiner POS die abonnierten Gruppen -- was es nicht
abonniert hat und nicht ueber Mesh bekommt, ist kein Verlust. `nbrRelayNeed()` nimmt deshalb
Zeilen mit `NBR_FLAG_GW` aus der Bedarfsmenge. Das Flag ist beobachtet (HEY mit Ziel `HG` von einem
Absender mit Zeile), nicht verbuergt; ein Fehlurteil geht in die unsichere Richtung und ist mit R6
zu beobachten.

### 5.5 Reichweite: nur Anzeige

Gestrichen als Faktor (2.6). `nbrReach()` bleibt fuer die Zeilentabelle.

### 5.6 Gemischte Flotte

Alt-Firmware flutet wie heute. Ein Knoten mit diesem Konzept bricht auch gegen deren Kopien ab (sie
sind Frames mit Pfad, wie jede andere); er braucht keinen Partner mit derselben Firmware. Der Gewinn
skaliert mit dem Anteil neuer Knoten, ohne Koordination und ohne Stichtag. Klassischer ESP32 mit 13
Zeilen kann die Sendeentscheidung genauso rechnen, solange seine direkten Nachbarn in 12 Zeilen
passen (der Rang ist dort duenn, aber der wird nicht gebraucht).

### 5.7 Was nicht gebaut wird, und warum

| Idee                                   | Grund                                                                                                                                                                                                                                                                                                            |
| -------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| HEY-Ziel `HN` ("ich bin wichtig")      | `decodeAPRS()` prueft das Ziel mit `checkRegexCall()` (`aprs_functions.cpp:357`); die Regex verlangt eine Ziffer, `H` und `HG` sind hart aufgezaehlt (`regex_functions.cpp:27-31`). **Jede heutige Firmware verwirft den ganzen Frame**: kein Relay, kein mheard, kein Upload. Der Super-Node wuerde unsichtbar. |
| Wichtigkeit in der HEY-Nutzlast        | `R<n>;` wird von `updateHeyPath()` nur mit 0 oder 2 Kommas akzeptiert; eine Zusatzgruppe verschiebt in `nbr_matrix.cpp` die Zuordnung Gruppe i → Paar (i-1, i); der Server parst dieselbe Kette. Und Trickle unterdrueckt genau bei dichten Knoten die meisten HEYs (3 in 8,5 h bei OE3XIR-12).                  |
| Flag-Bit im Kopfbyte 5                 | alle acht Bits belegt (`aprs_functions.cpp:180-192`).                                                                                                                                                                                                                                                            |
| POS-Tag `/I=<n>` als Selbstauskunft    | firmware-seitig parser-sicher und alle 30 min frisch, aber ohne Bedarf: die Sendeentscheidung braucht keine Fremdauskunft. Bleibt als Option fuer die Anzeige, **nur** nach Freigabe des Serverparsers.                                                                                                          |
| Election                               | es gibt nichts zu waehlen; der Rang ist lokal und wird nicht gebraucht.                                                                                                                                                                                                                                          |
| MPR / Set-Auswahl                      | Verdict 4.2: entscheidet auf fehlenden Kanten, die die Matrix nicht kennt.                                                                                                                                                                                                                                       |
| `NCT` / `getMheardCount()` als Eingang | Faktor-10-Fehler (2.2).                                                                                                                                                                                                                                                                                          |
| Bitfeld statt Zeilen als Voraussetzung | nicht auf dem kritischen Pfad (2.3); Anzeigeoption 2d.                                                                                                                                                                                                                                                           |

### 5.8 Kleine Vorbedingungen, die vor 2e gebaut sein muessen

0. **Spalte 0 nur beim Empfang schreiben.** In `nbrNoteFrame()` darf die Kantenschleife kein Paar
   (`p[i]`, `p[i+1]`) mit `p[i+1] == ich` in `cell[p[i]][0]` eintragen: mein Rufzeichen steht nur
   in Pfaden, die ich selbst gesendet habe, und ob ich `p[i]` je per Funk gehoert habe, weiss der
   ME-Schritt beim Empfang schon. Auf Gateways schreibt die Schleife sonst Serverabsender als
   direkte Nachbarn (2.3). Host-Test: Pfad `X,ICH,Y` setzt `cell[ICH][Y]`, aber nie `cell[X][0]`.
   Stufe-1-Fehler, fork-lokal, vor jedem weiteren Feldlauf zu beheben.
1. **Direktzeilen bei der Verdraengung schuetzen.** `nbrPlanRow()` waehlt das Opfer unter den
   Zeilen ohne frische `cell[X][0]` zuerst; erst wenn keine solche mehr da ist, unter den Direkten.
   Im Lauf waeren damit 0 statt 2 Direktzeilen gefallen. Wenige Zeilen, ein Host-Test.
2. **Kennbit fuer echte Relays im Ring.** `ringSource` kennt nur `'r'` fuer alle `rx_*`; der
   Abbruch darf `rx_ack_fwd` und die DM-ACKs nie treffen.
3. **Zaehler ohne Wirkung** (Verdict M1): dieselbe Rechnung wie 5.2, aber statt Freigabe nur
   `[NBR]|CANCEL?|...`. Liefert vor dem Scharfschalten, wie oft die Deckungspruefung freigibt und
   gegen wen.
4. **Kill-Switch** `--nbrrelay off|count|on` (Bit in `node_sset4`, `--info`-Zeile), Default `off`
   im Fleet-Build, `count` fuer den Feldlauf, `on` fuer den Beta-Cluster. Das Gate misst
   Datenverfuegbarkeit, nicht Ordnungs-Korrektheit (ADR 02); nur der Schalter kann einen
   misslungenen Rollout beenden.

---

## 6. Anzeige: die zwei Tabellen

**Alle Beschriftungen und Erklaerungen auf Englisch**, wie der Rest der Web-Oberflaeche. `title=`
funktioniert auf dem Telefon nicht; jede Spalte mit Einzelbuchstaben oder Zahl braucht deshalb
**zusaetzlich eine Legendenzeile unter der Tabelle** mit demselben Text. Das Scaffold-CSS greift nur
auf `#content_inner > table`; kein Wrapper-`div`, die Tabelle scrollt selbst als Block.

### 6.1 Kreuztabelle (Tabelle 1)

**Regeln:**

1. **Spaltenkopf mit Rufzeichen inkl. SSID, vertikal** (`writing-mode: vertical-rl; transform:
rotate(180deg)`), Nummer bleibt darueber. Zellen wie heute (`title="T:.. P:.. H:.. rssi:.."`).
2. **Zeilenfarbe nach Rolle**, nicht mehr nach `<verdict>`: Zeile 0 blau; Super-Node gruen;
   Fall-A-relevante Zeilen (in `E_self`) rot wie bisher; Rest ohne Farbe.
3. **Zusaetzliche Spalten links**, je Zeile:

| Spalte | Kopf-Hover / Legende                                                                                              | Zell-Hover                                                       | Quelle                                                           |
| ------ | ----------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------- | ---------------------------------------------------------------- |
| `D/I`  | "Direct: heard by me over the air. Indirect: only via a neighbour."                                               | "Heard directly, RSSI -114 dBm" / "Indirect, via DL2JA-2"        | `cells[X][0]` frisch → `D`, sonst `I`; via = ein Hoerer von X    |
| `G`    | "Gateway: a HEY addressed to HG was seen from this node. 'no' means not observed."                                | "Gateway HEY seen" / "No gateway HEY observed"                   | `NBR_FLAG_GW`                                                    |
| `M`    | "Mesh: relays foreign frames, from its last position frame."                                                      | "Mesh enabled" / "Mesh disabled" / "No position frame in window" | `NBR_FLAG_MESH`, `NBR_FLAG_POS`                                  |
| `#N`   | "Neighbours: nodes this node hears, as far as this table can hold them."                                          | "Hears 18 nodes (table holds 21 rows, 46 seen today)"            | Spaltenzaehlung `cells[*][X]`, nur Direktzeilen                  |
| `#X`   | "Exclusive: nodes that ONLY this neighbour hears. This is the value of a relay."                                  | "10 of 18 heard by nobody else in my range"                      | Zahl aus `nbrRowMeshNeed()` (2b); `-` fuer Zeile 0 und Indirekte |
| `Role` | "Super: largest exclusive share. Needed: has exclusive nodes. Redundant: everything it hears is heard by others." | "Super node: 10 exclusive, next best 1"                          | Abschnitt 5.3                                                    |

**In den Hover-Text gehoeren die Einschraenkungen:** `G = no` heisst "not observed"; `M` kommt nur
aus einem POS-Frame; `#N` und `#X` sind durch `NBR_MAX_ROWS` gekappt, der Hover nennt die Zeilenzahl
und die Zahl der seit Boot verdraengten Rufzeichen, damit niemand eine 18 fuer eine 40 haelt.

### 6.2 Zeilentabelle (Tabelle 2)

**Regeln:**

1. `Hearers` bricht in der Zelle um (`max-width` + `overflow-wrap: anywhere`); die Tabelle waechst in
   die Hoehe, nicht in die Breite.
2. **Sortierung:** Zeile 0 zuerst, dann direkte Nachbarn nach `#X` absteigend, dann Indirekte nach
   Alter. Damit steht der Super-Node oben.
3. Neue Spalte `Covered by`: fuer jede Direktzeile die direkten Nachbarn, die diesen Knoten ebenfalls
   hoeren (das ist `Alternative(X, ·)` ohne Frame) — leer heisst "only I reach this node" und ist
   genau die Menge `E_self`.
4. `Reach` bleibt, mit Partner, als Anzeige.

### 6.3 Neuer Block: "My relay decision"

Unter den Tabellen drei Zeilen Text, keine Tabelle: `Exclusive to me: DB0FHR-12, IS0QLX-11` /
`Super node in range: DL2JA-2 (10 exclusive)` / `Relays since boot: 904, cancelled: 0, could have
cancelled: 517, refused by coverage: 387` (die Zaehler aus 5.8, Punkt 3). Das ist die Seite, die der
Betreiber liest, um `--mesh off` zu entscheiden, und ab 2e die Seite, die zeigt, was der Automat tut.

---

## 7. Ausbaustufen und Umsetzungsstand (2026-09-22)

Betreiberentscheidung vom 2026-09-22: alles lokal umsetzen, committen, auf DK5EN-98 und den
Bench-Heltec flashen, Test weiterlaufen lassen. Umgesetzt auf `feature-neighbour-matrix` in einer
Welle (Kampagnenstand: `docs/nbr-stage2-campaign.md`):

| Stufe | Inhalt                                                                                                                                | Funkverhalten     | Stand                                                                                          |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------- | ----------------- | ---------------------------------------------------------------------------------------------- |
| 2a    | Anzeige: vertikale Koepfe, Spalten `D/I G M #N #X Role`, Legende, Umbruch und Sortierung in Tabelle 2, `Covered by`, Block 6.3        | unveraendert      | gebaut (`sub_page_neighbours()`)                                                               |
| 2b    | `nbrRowMeshNeedCount()`, `nbrExclusiveDirect()`, Direktzeilen-Schutz in `nbrPlanRow()`, Spalte 0 nur per ME (5.8 Punkt 0), `ringKind` | unveraendert      | gebaut, 5 neue Host-Tests in `native_nbr_matrix`, 3 in `test_txring`                           |
| 2c    | `--nbrrelay count`: Bedarfs-/Allein-Maske je Slot, `NEED`/`CANCEL?`/`REFUSE`-Zeilen, fuenf Zaehler in `--info` und auf der Web-Seite  | unveraendert      | gebaut; Feldlauf-Phase 1                                                                       |
| 2d    | Bitfeld fuer `#N`/`#X` jenseits der Zeilen                                                                                            | unveraendert      | nicht gebaut, Anzeigeoption                                                                    |
| 2e    | `--nbrrelay on`: Abbruch nur Fall B, nur deckungsbasiert                                                                              | **greift ein**    | gebaut, per Kommando scharf                                                                    |
| 2f    | Nachrang fuer Fall B (`NBR_RELAY_CASE_B_EXTRA_MS` 20 s, Slots 7..9), Text ausgenommen; Gateway-Ausnahme in der Bedarfsmenge           | greift ein (`on`) | gebaut (Betreiber: 20 s sind fuer POS/HEY vernachlaessigbar; Server verteilt an jedes Gateway) |
| 2g    | Fall A vorn (`3500 ms`, Slots 0..2) und Re-Arm-Kappung: ab 8 s Wartezeit nur noch Kurzsuche `150 ms + Jitter`, dann CAD               | greift ein (`on`) | gebaut (Betreiber: der Super-Node darf vor allen anderen senden)                               |
| --    | Election/HN, POS-Tag `/I=`, MPR                                                                                                       | --                | gestrichen (5.7)                                                                               |

Phasen im Feld: erst `count` (Masken und Zaehler ohne Wirkung, Verdict M1), dann `on`. Beides sind
Kommandos ueber die Konsole, kein Reflash. Kill-Switch ist `--nbrrelay off`.

Zweites Blatt: der Bench-Heltec DK5EN-1 am Laptop mit `--txpower 2` (nur DK5EN-98 hoert ihn),
`--gateway off`, `--mesh on` -- mit Mesh aus saehe DK5EN-98 nie, was das Blatt hoert, und hielte
es fuer allein versorgt; dann gaebe es an DK5EN-98 keinen Fall B mehr --, `--nbrdebug on`,
`--setlog on`, `--nbrrelay count`, Mitschnitt ueber USB mit `tools/bench/serial_capture.py`.

## 8. Messung: falsifizierbar, mit vorhandenen Instrumenten

Der laufende Mitschnitt ist die Vorher-Messung. Alle Groessen kommen aus `--setlog` und `--nbrdebug`,
gerechnet mit `tools/nbrrelay.py` und `tools/nbrsnap.py`; keine neue Firmware fuer die Baseline.

| #   | Groesse                                                                              | Vorher (11,9 h)                 | Erfolg 2e/2f                                 | Abbruch                                          |
| --- | ------------------------------------------------------------------------------------ | ------------------------------- | -------------------------------------------- | ------------------------------------------------ |
| R1  | eigene Relays je Stunde, nach Typ                                                    | 76/h (POS 21, HEY 54, TEXT 0,5) | sinkt                                        | —                                                |
| R2  | Anteil eigener Relays mit fremder Kopie **vor** der eigenen TX                       | POS 17 %, HEY 11 %              | steigt (Nachrang wirkt), Rest wird gecancelt | —                                                |
| R3  | fremde Wiederholungen je Frame im Ohr (Median, Maximum)                              | 2, 3                            | sinkt, sobald Nachbarn nachziehen            | —                                                |
| R4  | Verzoegerung der Super-Node-Kopie gegen die erste Kopie                              | 109 s Median                    | sinkt (weniger Re-Arm), stark erst mit 2g    | steigt                                           |
| R5  | eigene Wartezeit im Ring, p90                                                        | 20 s                            | Fall A sinkt, Fall B darf steigen            | Fall A steigt                                    |
| R6  | Zustellung an exklusive Knoten: Kadenz ihrer POS/HEY im Fenster, ACK-Quote ihrer DMs | aus dem Lauf zu ziehen          | unveraendert                                 | **jeder** Rueckgang → `--nbrrelay off`           |
| R7  | Abbrueche je Stunde, je Relayer, Verweigerungen durch Deckung                        | 0 (Zaehler ab 2c)               | > 0, Verweigerungen dokumentiert             | Abbruch mit `alone != 0` → Fehler, nie zulaessig |

R6 ist die Groesse, deren Fehlen 60ea7d8 das Feld gekostet hat. Sie ist knotenseitig nur als Proxy
messbar; der harte Beweis ist ein zweiter Mitschnitt **auf einem exklusiven Blatt** (Kandidat:
ein eigener Knoten, bewusst so gestellt, dass nur DK5EN-98 ihn hoert).

---

## 9. Offene Punkte und Fragen an den Betreiber

| #   | Frage                                                                                                                                                       | Blockiert   | Vorschlag                                                      |
| --- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------- | -------------------------------------------------------------- |
| 1   | Bestaetigt die 24-h-Endauswertung (ab 22.09. 19:20) die Zahlen aus Abschnitt 2, insbesondere 2.4?                                                           | 2c          | `tools/nbrsnap.py` und `tools/nbrrelay.py` mit `--since`       |
| 2   | Zweiter Mitschnitt auf einem exklusiven Blatt fuer R6 — welcher Knoten, wo?                                                                                 | 2e          | eigener Knoten am Rand der Hoerweite von DK5EN-98              |
| 3   | Ist +20 s Zustellverzoegerung auf redundanten Pfaden fuer POS/HEY akzeptabel (`CSMA_B_EXTRA_MS`)?                                                           | 2f          | ja, Text ausgenommen                                           |
| 4   | Darf 2g die CSMA-Politik anfassen (Re-Arm-Kappung), oder bleibt es bei Prioritaet und Slots?                                                                | 2g          | ja, nach Bench-Nachweis; sonst bleibt der Super-Node bei 109 s |
| 5   | Verteilt der Server jeden Frame an jedes Gateway? (entscheidet 5.4)                                                                                         | GW-Ausnahme | Frage an Kurt, kein Firmware-Thema                             |
| 6   | Soll die Anzeige die Selbstauskunft eines Nachbarn zeigen (`/I=` im POS-Beacon)? Nur mit Freigabe des Serverparsers.                                        | nichts      | nein                                                           |
| 7   | Rollout: fork-lokal auf DK5EN-98 in `count`, dann `on` am eigenen Cluster, dann PR je Stufe?                                                                | 2e          | ja; Kurt reviewt, kein Selbst-Merge                            |
| 8   | Mobile Knoten: Zeile 0 wird einmal aus den Settings gesetzt (Verdict L3). Fuer die Sendeentscheidung egal, fuer `Reach` falsch. Fix mitnehmen?              | Anzeige     | ja, in 2a                                                      |
| 9   | Dedup-Alterung nach Uhrzeit (Memory `dedup-ring-size-settled`) ist unabhaengig, aber jede Verkuerzung der Warteschlangen verschiebt das Fenster. Mitmessen? | —           | M3 aus ADR 02 mitlaufen lassen                                 |

---

## Anhang: Skripte

| Skript              | Liest                                     | Liefert                                                                                                                                                              |
| ------------------- | ----------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `tools/nbrlog.py`   | `[NBR]`-Zeilen                            | den Bericht des Feldlaufs (Union ueber den Lauf, Vergleich mit `<meshneed>`, Tabellendruck)                                                                          |
| `tools/nbrsnap.py`  | `[NBR]`-Zeilen                            | je Schnappschuss die **Geraetesicht**: `#N`/`#X` je Direktnachbar aus den frischen, nicht verdraengten Kanten; `<meshneed>`-Verlauf; Verdraengungen von Direktzeilen |
| `tools/nbrrelay.py` | `[LOG]`-RX/`RLY`/`TX`-Zeilen (`--setlog`) | Relay-Mix, Duplikatanteil, fremde Kopien vor/nach der eigenen TX, Verzoegerung je Relayer, Wartezeit gegen Empfangsdichte, Link-Asymmetrie                           |

Alle drei nehmen `--since YYYY-MM-DD[THH:MM]` und die Logdateien als Argumente; `--own` setzt das
eigene Rufzeichen, wenn keine `SNAP`-Zeile im Log steht. Direktheit lesen alle drei ausschliesslich
aus ME-Zeilen, nie aus EDGE-Kanten mit dem eigenen Rufzeichen als Ziel (2.3).
