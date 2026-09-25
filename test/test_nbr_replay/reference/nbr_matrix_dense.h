#pragma once

// Nachbarschaftsmatrix, Wave 1 (Konzept ~/Desktop/Nachbarschaftsmatrix.html,
// Abschnitt 4.1-4.4). Kreuztabelle "wer hat wen gehoert" aus dem Pfad jedes
// empfangenen Frames, mit Signalbericht aus HEY, 12-Stunden-Verfall und den
// Urteilen (exklusiv / redundant / Reichweite), die 4.3 und 4.4 daraus
// ableiten.
//
// Arduino-frei mit voller Absicht: die Regeln (Pfadpaare, Schleifenerkennung,
// Verfall mit Minuten-Ueberlauf, Verdraengung, Urteile) sind reine Logik ohne
// Funkbezug und deshalb auf dem Host pruefbar (test/test_nbr_matrix, env
// native_nbr_matrix). Wer hier ein Feature aus dem Frame- oder Positions-Pfad
// einspeisen will (loop_functions.cpp, lora_functions.cpp), tut das ueber die
// Funktionen unten -- niemals durch ein Arduino-Include in diese Datei.
//
// Erlaubte Includes bleiben absichtlich auf das beschraenkt, was ein
// Host-g++ ohne Framework kennt: <stdint.h> <string.h> <stdio.h> <math.h>.

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// NBR_MAX_ROWS ist board-spezifisch (21 auf ESP32-S3/RAK4631, 13 auf
// klassischem ESP32, 11 in der Entwickler-Variante ENABLE_TBEAM, siehe
// configuration_global.h) und steht dort neben MAX_MHEARD. Ein stiller
// Default hier wuerde eine vergessene Definition verschlucken und der
// Board-Wahl eine Zeilenzahl unterschieben, die zu keiner der drei
// dokumentierten Speicherrechnungen aus Konzept 4.2 passt -- deshalb Abbruch
// beim Bauen statt einer Zahl, die niemand gewaehlt hat.
// Board-Builds: die Zeilenzahl steht in configuration_global.h, das ueber
// configuration.h kommt; configuration.h haengt an seinem Ende ausserdem
// configuration_default.h an (#ifndef-Flottendefaults, u. a.
// LORA_SNR_STABLE_MIN_DB fuer NBR_SYM_MIN_SNR unten). Der Include ist
// UNBEDINGT, nicht nur "falls NBR_MAX_ROWS noch fehlt": lora_functions.cpp
// und command_functions.cpp binden configuration.h schon VOR nbr_matrix.h
// ein, ein bedingter Include haette LORA_SNR_STABLE_MIN_DB dort NICHT
// garantiert sichtbar gemacht, nur NBR_MAX_ROWS. Jede
// variants/<board>/configuration.h traegt "#pragma once" -- ein zweiter
// Include hier ist ein billiges No-Op, kein Doppel-Parse.
//
// Der Host-Test (NATIVE_BUILD) bekommt NBR_MAX_ROWS aus platformio.ini und
// darf kein Arduino-Include sehen (siehe Kopfkommentar); configuration_default.h
// ist mit voller Absicht Arduino-frei (siehe dessen Kopfkommentar) und ist
// darum die einzig zulaessige Quelle fuer LORA_SNR_STABLE_MIN_DB hier.
#ifdef NATIVE_BUILD
#include "configuration_default.h"
#else
#include "configuration.h"
#endif

#ifndef NBR_MAX_ROWS
#error "NBR_MAX_ROWS ist nicht definiert. Board-Builds bekommen ihn aus configuration_global.h, der Host-Test aus platformio.ini (env native_nbr_matrix, -D NBR_MAX_ROWS=5)."
#endif

#ifndef LORA_SNR_STABLE_MIN_DB
#error "LORA_SNR_STABLE_MIN_DB ist nicht definiert -- siehe configuration_default.h (Flottendefault, #ifndef-Wert -16)."
#endif

// Fenster, in dem ein Treffer als "frisch" gilt: 12 h, deckt 24 POS-Perioden
// ab (Konzept 4.2). Der ueberlaufsichere Vergleich (uint16_t)(now-last) <
// NBR_WINDOW_MIN ist nur INNERHALB von 65536 Minuten (45 Tage) nach dem
// letzten Treffer eindeutig -- laeuft der 16-Bit-Minutenzaehler seither
// einmal ganz herum, sieht eine seit Ewigkeiten tote Zelle wieder wie
// gerade eben getroffen aus ("Geist"). Dagegen faehrt nbrNoteFrame()/
// nbrNotePos() periodisch einen Sweep (siehe nbrMaybeSweep() in
// nbr_matrix.cpp), der jede Zelle/Zeile mit einer Alterslluecke >= 32768
// (die Haelfte des 16-Bit-Bereichs) zuruecksetzt, bevor sie als "aeltestes
// Opfer" oder als frisch durchgehen kann.
#define NBR_WINDOW_MIN 720

// Rufzeichen inkl. Nullterminierung, wie mheardCalls[][10] es schon vorgibt.
#define NBR_CALL_LEN 10

// Zeilen-Flags.
#define NBR_FLAG_GW   0x01   // HEY-Ziel "HG": Frame ging an einen Gateway
#define NBR_FLAG_MESH 0x02   // msg_mesh-Flag aus einem POS-Frame dieses Rufzeichens
#define NBR_FLAG_POS  0x04   // lat/lon sind gueltig
#define NBR_FLAG_USED 0x08   // Zeile ist belegt (Zeile 0 ist es immer, auch ohne dieses Bit)
#define NBR_FLAG_RPT  0x10   // letzter HN-Bericht dieser Zeile war VOLLSTAENDIG (kein '+'); rpt_min traegt seine Minute

// Zeile (weiterhin 24 Byte -- siehe rpt_min unten): ein gehoertes Rufzeichen
// mit letzter Position und Typ.
struct NbrRow
{
    char     call[NBR_CALL_LEN];
    // Minute des letzten VOLLSTAENDIGEN HN-Berichts dieser Zeile
    // (NBR_FLAG_RPT), fuer den Symmetrie-Veto in nbrHearsSym()
    // (nbr_matrix.cpp). Absichtlich HIER platziert statt hinter hw: char[10]
    // laesst vor dem folgenden float (4-Byte-Ausrichtung) ohnehin 2 Byte
    // Luecke, die ein uint16_t genau fuellt -- die Zeile bleibt bei 24 Byte,
    // kein zusaetzlicher RAM-Bedarf trotz drittem Report-Feld.
    uint16_t rpt_min;
    float    lat, lon;
    uint16_t last_min;
    uint8_t  flags;
    uint8_t  hw;
};

// Kein gueltiger Signalwert: int8_t deckt -127..127 ab, -128 bleibt
// reserviert und wird nie aus einem echten SNR-Feld geschrieben (siehe
// nbrClampSnr() in nbr_matrix.cpp, das den Wertebereich vorher auf
// [-127,127] begrenzt).
#define NBR_SNR_UNKNOWN (-128)

// Schwelle fuer die Symmetrie-Annahme (--nbrsym, Abschnitt D unten) UND fuer
// den HN-Nachbarschaftsbericht (nbrBuildReport() unten, Abschnitt E):
// derselbe Betreiberwert, ein einziges Mal definiert. Der Wert selbst
// (Herleitung: DF2SI-12 an DK5EN-98, SNR-Median -16 dB, darueber gilt eine
// Strecke als stabil) steht in src/configuration_default.h als
// LORA_SNR_STABLE_MIN_DB -- dort, weil er an der Modulation haengt (SF/BW/CR),
// nicht an der Nachbarschaftsmatrix, und weil ihn auch die Flottendefaults
// kennen muessen. Der Vergleich ist EINSCHLIESSLICH (>= LORA_SNR_STABLE_MIN_DB).
// Wir rechnen NICHT gegen Endstufen (EBYTE E22, T-Beam 1W, Nachruest-PAs):
// der beobachtete SNR wird genommen wie er ist, ueber die Sendeleistung der
// Gegenstation wird keine Aussage getroffen -- die Symmetrie-Annahme gilt
// unveraendert auch fuer solche Hochleistungsknoten. Jede Annahme wird als
// SYM-Zeile geloggt (HASF/ALT/COVER bei Erfolg, VETO bei einem durch einen
// gueltigen HN-Bericht verhinderten Schluss, siehe nbrNoteReport() unten),
// damit sie sich im Nachhinein pruefen laesst.
#ifndef NBR_SYM_MIN_SNR
#define NBR_SYM_MIN_SNR LORA_SNR_STABLE_MIN_DB
#endif

// --- HN-Nachbarschaftsbericht: Konstanten (Abschnitt E, siehe nbrBuildReport()
// und nbrNoteReport() unten) ---------------------------------------------

// Hoechstzahl Eintraege in einem gesendeten Bericht -- mehr passt nicht mehr
// verlustfrei in eine einzelne '@'-Nutzlast neben dem uebrigen HEY-Rahmen.
#ifndef NBR_REPORT_MAX_ENTRIES
#define NBR_REPORT_MAX_ENTRIES 8
#endif

// Ein Direktempfang zaehlt fuer den Bericht nur, wenn er innerhalb dieser
// Minutenzahl liegt -- eigenes, kuerzeres Fenster als NBR_WINDOW_MIN oben: ein
// Bericht ist eine Momentaufnahme ("wen hoere ich GERADE"), kein 12-h-Verlauf.
#ifndef NBR_REPORT_FRESH_MIN
#define NBR_REPORT_FRESH_MIN 60
#endif

// Ein empfangener VOLLSTAENDIGER Bericht (kein '+') bleibt so lange
// massgeblich fuer die Symmetrie-Annahme (nbrHearsSym() in nbr_matrix.cpp):
// 3 Sendeintervalle (3 x NBR_REPORT_INTERVAL_S = 3 x 15 min), danach gilt er als veraltet
// und die Annahme darf wieder greifen.
#ifndef NBR_REPORT_VALID_MIN
#define NBR_REPORT_VALID_MIN 45
#endif

// Zelle (6 Byte): "Spalte hat Zeile gehoert" -- drei sattelnde Zaehler nach
// Frame-Typ, der zuletzt gesehene SNR aus einem HEY-Bericht bzw. aus dem
// eigenen Empfang (dB, NBR_SNR_UNKNOWN = unbekannt) und die Letztzeit dieser
// Zelle.
struct NbrCell
{
    uint8_t  cnt_text, cnt_pos, cnt_hey;
    int8_t   snr;
    uint16_t last_min;
};

// N mal N Kreuztabelle. cell[X][Y] = "Y hat X gehoert". Zeile 0 ist der
// eigene Knoten und wird nie verdraengt.
struct NbrMatrix
{
    NbrRow  rows[NBR_MAX_ROWS];
    NbrCell cells[NBR_MAX_ROWS][NBR_MAX_ROWS];
    uint16_t boot_min;
    uint16_t last_sweep;   // Minute des letzten Geister-Sweeps, siehe NBR_WINDOW_MIN oben
};

// --- Instrumentierung fuer den 24-h-Dauertest (docs/nbr-logformat.md) -----
//
// Diese Datei bleibt Arduino-frei (siehe Kopfkommentar), darum kein direkter
// printfdeb()-Aufruf: der Aufrufer (lora_functions.cpp) haengt einen
// Funktionszeiger ein. NULL = Instrumentierung aus, keine einzige Zeile wird
// formatiert. Das Zeilenformat (Trennzeichen '|', Feldreihenfolge je Typ)
// steht verbindlich in docs/nbr-logformat.md; wer es hier aendert, aendert
// dort mit. Eine Zeile kommt OHNE abschliessendes '\n' -- das haengt der
// Aufrufer an.
typedef void (*NbrLogFn)(const char *line);
extern NbrLogFn nbrLog;

// --- Aufbau und Alterung --------------------------------------------------

// Setzt die Matrix komplett zurueck und traegt die eigene Zeile 0 ein.
void nbrInit(NbrMatrix &m, const char *own_call, uint16_t now_min);

// Ueberlaufsicherer Frischetest: (uint16_t)(now_min - last_min) < NBR_WINDOW_MIN.
// Eine Zeile/Zelle gilt zusaetzlich nur dann als GESETZT, wenn ihr USED-Flag
// bzw. mindestens einer ihrer Zaehler > 0 ist -- eine frische, aber leere
// Zelle (last_min=0, alle Zaehler 0) ist keine Beobachtung.
bool nbrFresh(uint16_t last_min, uint16_t now_min);

// Index einer Zeile per Rufzeichen, oder -1. Zeile 0 wird unabhaengig von
// ihrem USED-Flag gefunden (sie ist immer gueltig).
int nbrFind(const NbrMatrix &m, const char *call);

// Setzt alles zurueck bis auf das Rufzeichen von Zeile 0 (Konzept 4.2:
// "Reset per Kommando nullt alles ausser Zeile 0").
void nbrReset(NbrMatrix &m, uint16_t now_min);

// Alter der letzten Beobachtung dieser Zeile in Minuten (now_min - last_min,
// ueberlaufsicher). Ungueltiger Index liefert 0.
uint16_t nbrRowAgeMin(const NbrMatrix &m, int row, uint16_t now_min);

// --- Hoerbeweis (Konzept 4.1) ----------------------------------------------

// Wertet den Pfad eines empfangenen Frames aus. path ist "A,B,C" wie
// msg_source_path (erster Eintrag = Absender, letzter = letzter Hop, bis zu
// 121 Zeichen, hoechstens 8 Rufzeichen). type ist ':' Text, '!' Position,
// '@' HEY; jeder andere Typ tut nichts und liefert 0 (Log: DROP TYPE).
//
// Ablauf: der Pfad wird in bis zu 8 Rufzeichen zerlegt, jedes 3..9 Zeichen
// aus [A-Z0-9-]; verletzt das ein Token oder gibt es mehr als 8, wird der
// GANZE Frame verworfen (-1, Log: DROP TOK), die Matrix bleibt unangetastet.
// Kommt ein Rufzeichen zweimal vor (Schleife), wird ebenso verworfen
// (-2, Log: DROP LOOP). Diese beiden Pruefungen laufen ueber den GANZEN
// Pfad, unabhaengig vom Fenster unten.
//
// Text (':') faellt NICHT unter das 2-Hop-Fenster und die Pfadpaar-Kanten
// unten: ein Gateway mit Mesh an setzt vom Server eingespeiste Frames mit
// dem Pfad "<Server-Pfad>,<Gateway>" auf LoRa, und das Paar (letztes
// Server-Token, Gateway) ist dabei nie ein Funkempfang. Der on-air
// Server-Bit (Byte 5, 0x80) trennt Einspeisung nicht von normalem Relay
// (lora_functions.cpp:1800 setzt ihn bei jedem IP-Gateway-Relay), darum ist
// der Frame-Typ das einzig nutzbare Merkmal (Feldlog DK5EN-98,
// 22.-23.09.2026, 34h: 511 Server->Gateway-Frames, 100% Text, 0 POS/HEY;
// alle 79 reinen Text-Kanten endeten an einem einspeisenden Gateway). Text
// liefert daher NUR den ME-Schritt unten ("ich habe den letzten Hop
// gehoert"): keine Pfadpaar-Kanten (weder Fenster- noch Regel-3-Gratis-
// Kanten) und keine Zeile fuer irgendein Pfad-Token ausser dem letzten Hop;
// die CUT-Zeile (sie beschreibt das 2-Hop-Fenster) entfaellt fuer Text
// ebenfalls. Restrisiko: das setzt voraus, dass der Server ausschliesslich
// Text nach unten schickt -- udp_frame_esp32.cpp nimmt weiterhin '!'/'@'
// von UDP an. Bewusster Verlust: auch das Echo meines eigenen Textes
// ("<ich>,X") traegt kein "X hoert mich" (cells[0][X]) mehr ein, und ein
// echter Funk-Relay eines Textes ("A,M") keine Kante A->M -- ein anderes
// Gateway kann meinen hochgeladenen Text ebenso als "<ich>,<Gateway>"
// einspeisen. Diese Kanten liefern POS, HEY und HN-Bericht. Alles ab dem
// naechsten Absatz (2-Hop-Fenster, Pfadpaar-Kanten) gilt nur noch fuer '!'
// und '@'; EVICT kann auch Text ausloesen (Zeile des letzten Hops).
//
// 2-Hop-Fenster (Betreiber-Vorgabe: das ist eine HELLO-Matrix, keine
// vollstaendige Nachbarschaftskarte): NEUE Zeilen entstehen nur noch fuer
// die letzten zwei Pfad-Token, start = (ntok > 2) ? ntok - 2 : 0. Fuer jedes
// Token VOR start wird NIE eine Zeile geplant oder committet -- ein Knoten
// in 3+ Hop Entfernung darf die Tabelle nicht mehr fuellen. Ist der Pfad
// laenger als das Fenster, wird das genau einmal pro Frame geloggt (CUT).
//
// Erst werden ALLE Fenster-Rufzeichen lesend aufgeloest (Zeile vorhanden
// oder Zielindex einer Neuanlage/Verdraengung), bevor irgendeine Zelle
// angefasst wird: zwei neue Rufzeichen im selben Frame duerfen nicht
// dieselbe Opferzeile bekommen (sonst faellt der zweite Treffer auf die
// Diagonale). Findet sich fuer ein Fenster-Rufzeichen keine freie und keine
// verdraengbare Zeile mehr, weil alle anderen bereits fuer dieses Frame
// vergeben sind, wird der GANZE Frame verworfen (-3, Log: DROP FULL), die
// Matrix bleibt bis auf einen faelligen Geister-Sweep (siehe NBR_WINDOW_MIN)
// unangetastet. Da das Fenster hoechstens 2 Token breit ist, ist dieser Pfad
// bei NBR_MAX_ROWS >= 3 (jede reale Board-Konfiguration) praktisch nie mehr
// erreichbar -- er bleibt als Sicherung fuer eine sehr kleine Tabelle stehen.
//
// Danach wird geschrieben: fuer jedes Paar (p[i], p[i+1]) mit i >= start ein
// Treffer auf cell[p[i]][p[i+1]] (Log: EDGE) mit dem passenden Typzaehler
// (saettigt bei 255) und last_min. Ein Paar mit i < start (ausserhalb des
// Fensters) bekommt TROTZDEM einen Treffer, aber OHNE je eine neue Zeile
// anzulegen, wenn BEIDE Enden bereits eine bestehende Zeile haben
// (nbrFind() >= 0) -- jeder existierende Zeileninhaber ist per Konstruktion
// hoechstens 2 Hops entfernt, eine Kante zwischen zwei solchen Knoten ist
// gueltige Information und kostet keine Zeile. Danach, sofern der letzte Hop
// nicht das eigene Rufzeichen ist (sonst ist es das eigene Echo), ein
// Treffer auf cell[letzter_hop][0] mit snr = snr_here (Log: ME, das dort
// zusaetzlich rssi_here unveraendert im <rssi>-Feld traegt, ohne es zu
// speichern). Eine
// Zelle, deren last_min beim Treffer bereits verfallen ist, faengt bei ihren
// Zaehlern neu bei 0 an (Konzept 4.2), bevor der Treffer zaehlt. Ein Paar
// (p[i], p[i+1]) mit p[i+1] == eigenes Rufzeichen schreibt NIE cell[p[i]][0]:
// Spalte 0 fuellt ausschliesslich der ME-Schritt beim Empfang (Stufe 2,
// docs/nbr-wichtigkeit-konzept.md 2.3: das Echo eines vom Server
// eingespeisten Frames machte sonst dessen Absender zum direkten Nachbarn).
// Ist die Tabelle voll, weicht beim Anlegen einer Fenster-Zeile die Zeile
// 1..N-1 mit der aeltesten last_min (Log: EVICT) -- zuerst unter den nicht
// frisch direkt gehoerten Zeilen, erst dann unter allen --, ihre Zeilen- und
// Spaltenzellen werden genullt (Zeile 0 ist davon nie betroffen).
//
// Bei '@' setzt dest_gw=true das GW-Flag auf die Zeile des Absenders
// (erster Pfadeintrag, ueber nbrFind() aufgeloest -- Absender ist meist
// ausserhalb des Fensters und bekommt dafuer keine neue Zeile). Bei '@' wird
// zusaetzlich payload als "R<n>;g1;g2;..." gelesen (siehe
// appendHeySignalReport(), src/aprs_functions.cpp:1134): Gruppe i (1-basiert)
// ist "NCT,RSSI,SNR" und gehoert zum Paar (p[i-1], p[i]) -- ihre Zelle
// bekommt snr = das dritte, vorzeichenbehaftete Feld (RSSI aus dem zweiten
// Feld wird nicht mehr gespeichert), aber NUR wenn beide Enden aufgeloest
// sind (im Fenster liegen oder nach der Regel oben als bestehende Zeile
// gelten).
// Eine Gruppe, die nicht aus genau drei Kommafeldern besteht (oder fehlt,
// altes Format), wird uebersprungen; die Pfadtreffer aus dem ersten Schritt
// zaehlen trotzdem.
//
// Rueckgabe: Zahl der gemachten Pfad-Treffer (>= 0, zaehlt nur tatsaechlich
// geschriebene Zellen), -1 bei ungueltigem Rufzeichen/zu vielen Hops, -2 bei
// einer Schleife, -3 wenn im selben Frame mehr neue Fenster-Rufzeichen
// aufzuloesen waren als Opferzeilen frei blieben, 0 bei unbekanntem Typ.
// snr_here ist der SNR des gerade empfangenen Frames (OnRxDone hat ihn als
// int8_t von der Radio-HAL) -- gespeichert wird NUR er (cell[letzter_hop][0].snr),
// rssi_here bleibt reines Logfeld (Log: ME, <rssi>).
int nbrNoteFrame(NbrMatrix &m, const char *path, char type, const char *payload,
                  bool dest_gw, int16_t rssi_here, int8_t snr_here, uint16_t now_min);

// Traegt eine Position NUR in eine BEREITS BESTEHENDE Zeile ein (Konzept
// 4.4). Legt anders als frueher KEINE Zeile mehr an: der Aufrufer in
// lora_functions.cpp reicht das Sender-Rufzeichen eines POS-Frames durch,
// das durch mehrfaches Relayen aus 3+ Hop Entfernung stammen kann -- das
// waere ein stiller Zeilen-Neuanlage-Pfad am 2-Hop-Fenster von
// nbrNoteFrame() vorbei. Ein Knoten, der nur ueber relayte POS-Frames
// sichtbar waere, bekommt damit BEWUSST keine Zeile. Ist call nicht
// gefunden (nbrFind() < 0), kehrt die Funktion folgenlos zurueck. Die
// eigene Zeile 0 existiert immer und wird darueber weiterhin gefuellt.
// Setzt bei einem Treffer lat/lon und NBR_FLAG_POS, setzt oder loescht
// NBR_FLAG_MESH, setzt hw und last_min = now_min (Log: POS).
void nbrNotePos(NbrMatrix &m, const char *call, float lat, float lon, bool mesh,
                uint8_t hw, uint16_t now_min);

// --- HN-Nachbarschaftsbericht (Report), Abschnitt E -------------------------
//
// Baut/liest die Nutzlast eines HN-Frames: Ziel "HN", Typ '@' (wie HEY),
// max_hop 0 -- ein Knoten meldet, wen er GERADE direkt hoert, mit SNR.
// Grammatik (strikt, kein Leerzeichen, jedes Feld mit ';' beendet):
//
//   R<heard>;N<k>[+];<CALL>,<snr>;...;
//
// <heard> ist der aufrufer-seitige Zaehler, identisch zum Feld im normalen
// HEY-Bericht ("R<n>", appendHeySignalReport()/loop_functions.cpp). <k> ist
// die Zahl der folgenden Eintraege, '+' folgt <k> GENAU DANN, wenn mehr
// Kandidaten qualifiziert waren als gelistet wurden (Abschneiden am Limit,
// nicht am Rufzeichen). <k> == 0 ist gueltig ("R3;N0;", eine leere, aber
// VOLLSTAENDIGE Liste). Jeder Eintrag ist "<CALL>,<snr>;" mit <snr>
// vorzeichenbehaftet.

// Baut den Bericht aus den EIGENEN Direktempfaengen: Zeilen X != 0 mit
// gesetzter cells[X][0] ("ich habe X gehoert"), frisch innerhalb
// NBR_REPORT_FRESH_MIN, SNR bekannt und >= NBR_SYM_MIN_SNR (dieselbe Schwelle
// wie --nbrsym -- ein Nachbar, dessen Strecke zu mir als instabil gilt, ist
// keine verlaessliche Aussage ueber SEINE Nachbarschaft). Sortiert nach SNR
// ABSTEIGEND, bei Gleichstand nach Rufzeichen (strncmp, aufsteigend). Listet
// hoechstens NBR_REPORT_MAX_ENTRIES Eintraege, mit '+' wenn mehr qualifiziert
// waren.
//
// Obergrenze fuer out: bei der Default-Schwelle NBR_SYM_MIN_SNR == -16 hat
// jeder gelistete SNR-Wert hoechstens 3 Ziffern inkl. Vorzeichen ("-16" oder
// "127"), ein Eintrag also hoechstens 9 (Rufzeichen) + 1 (',') + 3 (SNR) + 1
// (';') = 14 Byte; 8 Eintraege = 112 Byte, plus ein kurzes Praefix
// "R<heard>;N8+;" (<= 9 Byte fuer ein zwei- bis dreistelliges <heard>) --
// zusammen deutlich unter 128 Byte. Ein Aufrufer, der NBR_SYM_MIN_SNR am Build
// ueberschreibt (theoretisch bis -127) oder ein sehr grosses <heard> erwartet,
// braucht entsprechend mehr; die Funktion selbst erkennt einen zu kleinen
// Puffer immer (Rueckgabe -1, out[0] = 0, nichts wird geschrieben).
//
// Rueckgabe: strlen(out) bei Erfolg, oder -1 wenn outlen nicht reicht (dann
// out[0] = 0, nichts Teilweises steht im Puffer).
int nbrBuildReport(const NbrMatrix &m, uint16_t now_min, int heard_count, char *out, size_t outlen);

// Liest einen empfangenen HN-Bericht. Wird NACH nbrNoteFrame() fuer denselben
// DIREKT empfangenen HN-Frame aufgerufen (die Zeile des Absenders existiert
// dann in der Regel schon -- der letzte Hop eines '@'-Frames ist immer
// Fenster-Token, siehe nbrNoteFrame()). sender ist das Absender-Rufzeichen
// (msg_source_path[0] bzw. der letzte Hop bei einem 1-Hop-Frame), payload die
// rohe HN-Nutzlast (siehe Grammatik oben).
//
// Strikter Parse ZUERST, bevor irgendetwas an der Matrix angefasst wird:
// "R"+Ziffern, ";N"+Ziffern+optional '+'+";", dann GENAU k Eintraege
// "<CALL>,<vorzeichenbehaftete Ziffern>;", <CALL> nichtleer und kuerzer als
// NBR_CALL_LEN, k <= NBR_REPORT_MAX_ENTRIES, kein Rest nach dem letzten
// Eintrag. Jede Abweichung (fehlendes Feld, ueberzaehliger/fehlender
// Eintrag, nicht-numerisches Feld, ueberlanges Rufzeichen, k > Limit) ist
// GANZ ungueltig: Rueckgabe -1, Log [NBR]|DROP|<up>|RPT|<sender>, NICHTS wird
// angewendet.
//
// Ist der Parse gueltig, aber der Absender hat keine Zeile (nbrFind() < 0),
// kehrt die Funktion folgenlos mit 0 zurueck -- ein HN-Bericht von einem noch
// unbekannten Knoten ist (noch) nicht auswertbar, aber kein Protokollfehler.
//
// Sonst je Eintrag <CALL>=m_call:
//   - m_call == eigenes Rufzeichen (Zeile 0): Treffer auf cells[0][s]
//     ("Absender s hat mich gehoert"), Status "self".
//   - m_call hat eine Zeile mrow: Treffer auf cells[mrow][s] ("s hat mrow
//     gehoert") als HEY-Typ-Treffer (nbrHitCell() mit '@'), .snr = der
//     geparste (bereits geklemmte) SNR, Status "ok".
//   - m_call hat keine Zeile: KEINE neue Zeile, Status "norow".
// Je Eintrag eine Log-Zeile [NBR]|RPT|<up>|<x>|<m>|<snr>|<status> (<x> =
// sender, <m> = m_call), danach genau eine Zusammenfassung
// [NBR]|RPTSUM|<up>|<x>|<heard>|<k>|<full 1/0>|<applied>.
//
// Traegt auf der Absender-Zeile NBR_FLAG_RPT (gesetzt bei einem
// VOLLSTAENDIGEN Bericht, geloescht bei einem abgeschnittenen '+') und
// rpt_min = now_min -- ein abgeschnittener Bericht wendet seine Eintraege
// trotzdem an, taugt aber NICHT als Symmetrie-Veto (siehe nbrHearsSym() in
// nbr_matrix.cpp und NBR_REPORT_VALID_MIN oben).
//
// Rueckgabe: Zahl der angewendeten Eintraege (self + ok, nicht norow), 0 wenn
// der Absender keine Zeile hat, -1 bei ungueltiger Grammatik.
int nbrNoteReport(NbrMatrix &m, const char *sender, const char *payload, uint16_t now_min);

// --- Urteile (Konzept 4.3) -------------------------------------------------

// Hoerer(row) = Menge der Spalten Y mit frischer, gesetzter cell[row][Y],
// Y != row (0 zaehlt mit: "ich habe row gehoert" ist ein gueltiger Hoerer).
// Schreibt bis zu max Indizes nach out und liefert die GESAMTZAHL der
// Treffer (auch wenn sie max uebersteigt und out abgeschnitten wurde).
uint8_t nbrHearers(const NbrMatrix &m, int row, uint16_t now_min, uint8_t *out, uint8_t max);

// Zeilen X != 0, die frisch direkt gehoert wurden (cell[X][0]) und deren
// Hoererkreis genau {0} ist, also "hoert nur ich, sonst niemand in meiner
// Hoerweite". Schreibt bis zu max Indizes nach out, liefert ihre Gesamtzahl.
// Liefert -1, wenn ueberhaupt keine Zeile eine frische cell[X][0] hat (noch
// nichts direkt gehoert) -- das ist etwas anderes als 0 exklusive Zeilen bei
// vorhandenem Empfang.
int nbrExclusive(const NbrMatrix &m, uint16_t now_min, uint8_t *out, uint8_t max);

// Betreiberfrage (Advisor-Pass 2026-09-21, docs/nbr-logformat.md): die Sicht
// auf row als HOERER, nicht als Gehoerten -- das Gegenstueck zu
// nbrRowExclusive()/nbrExclusive() oben, das die Sicht auf row als
// Gehoerten liefert. Beide Urteile beantworten entgegengesetzte Fragen an
// denselben Kanten und duerfen nicht verglichen werden.
//
// "NA" fuer Zeile 0 und jede Zeile, die ich nicht frisch direkt gehoert
// habe (cells[row][0] nicht gesetzt/frisch) -- fuer sie ist die Frage nicht
// gestellt. Sonst: H(row) = alle X mit frischer, gesetzter cell[X][row]
// ("row hat X gehoert"), ohne X == row und ohne X == 0 (dass ein Nachbar
// mich hoert, macht ihn nicht unverzichtbar). Jedes X aus H(row) gilt als
// anderweitig abgedeckt, wenn ich X selbst frisch direkt hoere ODER ein
// anderer frisch direkt gehoerter Nachbar M (M != row, M != 0) X ebenfalls
// frisch hoert. Bleibt mindestens ein X ohne Abdeckung, liefert die
// Funktion "MESH" (row muss selbst meshen), sonst "RED" -- eine leere
// Menge H(row) ist ebenfalls "RED" (row hoert niemanden, den ich brauche).
const char *nbrRowMeshNeed(const NbrMatrix &m, int row, uint16_t now_min);

// Dieselbe Rechnung als Zahl (Stufe 2, docs/nbr-wichtigkeit-konzept.md 2.1
// und 6.1, Spalte "#X"): -1 fuer "NA", sonst die Zahl der Knoten, die row
// hoert und die weder ich noch ein anderer direkt gehoerter Nachbar frisch
// hoert. nbrRowMeshNeed() ist nur noch die Wortfassung davon.
int nbrRowMeshNeedCount(const NbrMatrix &m, int row, uint16_t now_min);

// --- Stufe 2: Masken und Relay-Entscheidung (Konzept Abschnitt 4 und 5) ---
//
// Alle Masken sind Bitmasken ueber Zeilenindizes (Bit i = Zeile i); mit
// NBR_MAX_ROWS <= 21 passt das in 32 Bit. Bit 0 (ich selbst) ist in keiner
// dieser Masken gesetzt.

// Anzahl gesetzter Bits.
int nbrMaskCount(uint32_t mask);

// Direkt(X): belegte Zeilen X != 0 mit frischer, gesetzter cell[X][0].
uint32_t nbrDirectMask(const NbrMatrix &m, uint16_t now_min);

// HoertMich(X): belegte Zeilen X != 0 mit frischer, gesetzter cell[0][X]
// ("X hat mich gehoert" -- sichtbar nur, wenn X meine Frames wiederholt).
uint32_t nbrHeardMeMask(const NbrMatrix &m, uint16_t now_min);

// Hoerer von row: Zeilen Y != row, Y != 0 mit frischer, gesetzter
// cell[row][Y] ("Y hat row gehoert"). Dieselbe Menge wie nbrHearers(), ohne
// Zeile 0 und als Maske.
uint32_t nbrHearersMask(const NbrMatrix &m, int row, uint16_t now_min);

// Relay-Entscheidung fuer einen Frame mit Pfad path (msg_source_path, SO WIE
// EMPFANGEN, vor dem Anhaengen des eigenen Rufzeichens):
//   need  = Abhaengige, die den Frame noch nicht haben koennen:
//           (Direkt | HoertMich) ohne Zeile 0, ohne Zeilen mit NBR_FLAG_GW
//           (Gateways bekommen den Frame vom Server), ohne Pfadteilnehmer und
//           ohne jeden X, der einen Pfadteilnehmer P frisch gehoert hat
//           (cell[P][X]).
//   alone = Teilmenge von need ohne Alternative: kein direkter Nachbar M != X,
//           der den Frame hat (im Pfad steht oder einen Pfadteilnehmer
//           gehoert hat) UND den X frisch gehoert hat (cell[M][X]).
// alone != 0 ist Fall A (Relay mit Vorrang, nie Abbruch), alone == 0 Fall B.
//   known = false heisst "kein Wissen": ungueltiger Pfad oder keine einzige
//           abhaengige Zeile (leere Matrix nach Boot/Reset). Dann ist need ==
//           alone == 0, und der Aufrufer darf das NICHT als Fall B lesen,
//           sondern relayt wie heute (Konzept 1: nichts unterdrueckt auf
//           Verdacht) -- Advisor-Fund 2026-09-22. need == 0 bei known == true
//           ("alle Abhaengigen haben den Frame schon") bleibt Fall B.
//   inferred = Teilmenge von "X hat Bit in dieser Maske", deren HatF- oder
//           Allein-Ergebnis NUR durch die Symmetrie-Annahme (sym, --nbrsym)
//           zustande kam, nicht durch eine tatsaechliche Beobachtung. Bleibt
//           0, wenn sym == false. Jede Annahme, die hasf oder alt aendert,
//           erzeugt genau eine SYM-Zeile (nbrLog, Rollen HASF/ALT). Eine durch
//           einen gueltigen, vollstaendigen HN-Bericht des Kandidaten X
//           VERHINDERTE Annahme (NBR_REPORT_VALID_MIN, nbrHearsSym() in
//           nbr_matrix.cpp) aendert weder hasf/alone noch inferred, erzeugt
//           aber eine SYM-Zeile mit Rolle VETO statt HASF/ALT.
struct NbrNeed
{
    uint32_t need;
    uint32_t alone;
    bool     known;
    uint32_t inferred;
};
// sym = --nbrsym (bNBRSYM): erlaubt den Symmetrie-Fallback aus nbrHearsSym()
// fuer hasf UND alone (siehe nbr_matrix.cpp). msg_id geht nur in die
// SYM-Log-Zeilen ein, sonst in keine Rechnung.
NbrNeed nbrRelayNeed(const NbrMatrix &m, const char *path, uint16_t now_min, bool sym, uint32_t msg_id);

// Deckung durch eine gehoerte fremde Wiederholung: die Hoerer des Relayers
// (nbrHearersMask seiner Zeile) plus, wenn sym, jedes X, fuer das der
// Relayer M laut Symmetrie-Fallback X gehoert hat und das noch nicht
// beobachtet war. Liefert 0, wenn der Relayer keine Zeile hat oder ich
// selbst bin. Der Aufrufer rechnet need &= ~nbrCoverMask(...) und bricht bei
// need == 0 ab -- nur wenn alone == 0 war. relevant ist die Bedarfsmaske des
// EINEN Slots, fuer den dieser Aufruf gilt -- eine SYM-COVER-Zeile erscheint
// nur fuer ein per Symmetrie hinzugefuegtes X, dessen Bit auch in relevant
// gesetzt ist (msg_id fuer die Log-Zeile). *inferred (darf NULL sein)
// bekommt die per Symmetrie hinzugefuegten Bits, unabhaengig von relevant --
// der Aufrufer bildet daraus "before & ~after & inferred" fuers Log. Ein X,
// dessen Annahme durch seinen eigenen gueltigen HN-Bericht verhindert wird
// (siehe nbrHearsSym()), bleibt NICHT in mask, erzeugt aber -- ebenfalls nur
// bei gesetztem relevant-Bit -- eine SYM-Zeile mit Rolle VETO statt COVER.
uint32_t nbrCoverMask(const NbrMatrix &m, const char *relayer, uint16_t now_min, bool sym,
                       uint32_t relevant, uint32_t msg_id, uint32_t *inferred);

// E_self (Konzept 4): direkt gehoerte Zeilen, die kein ANDERER direkt
// gehoerter Nachbar frisch hoert. Anders als nbrExclusive() zaehlt ein
// 2-Hop-Hoerer nicht als Deckung -- seine Wiederholung kann ich nie hoeren.
// Schreibt bis zu max Indizes nach out, liefert die Gesamtzahl, -1 wenn
// ueberhaupt keine Zeile frisch direkt gehoert wurde.
int nbrExclusiveDirect(const NbrMatrix &m, uint16_t now_min, uint8_t *out, uint8_t max);

// --- Reichweite (Konzept 4.4) ----------------------------------------------

// Haversine-Distanz in km, Erdradius 6371.0 km.
float nbrDistKm(float lat1, float lon1, float lat2, float lon2);

// Groesste Distanz von row zu einer Zeile y, mit der row im Fenster in
// irgendeiner Richtung eine frische, gesetzte Zelle hat (cell[row][y] oder
// cell[y][row]) und beide Positionen gueltig sind. Liefert -1 und *partner =
// -1, wenn es keinen solchen Partner gibt oder row selbst keine gueltige
// Position hat.
float nbrReach(const NbrMatrix &m, int row, uint16_t now_min, int *partner);

// --- Ausgabe fuer --neighbours (W2 baut Web-Seite und Kommando darauf auf) -

// Formatiert eine Zeile grep-freundlich in EINE Zeile Text, z. B.:
//
//   OE1AAA-1 GW- M+ hears_me:-97 hearers:DK5EN-93,OE1BBB-2 reach:12.4km@OE1BBB-2 age:5m
//
// Feldreihenfolge, durch je ein Leerzeichen getrennt:
//   1. Rufzeichen der Zeile.
//   2. "GW+"/"GW-": NBR_FLAG_GW gesetzt/nicht gesetzt.
//   3. "M+"/"M-": NBR_FLAG_MESH gesetzt/nicht gesetzt.
//   4. "hears_me:<dB>" oder "hears_me:-": SNR aus cell[0][row], also wie
//      DIESE Zeile MICH zuletzt gehoert hat (Konzept 4.3, "Wer hoert mich").
//      "-" wenn nicht frisch oder SNR unbekannt (NBR_SNR_UNKNOWN).
//   5. "hearers:<a>,<b>,...": Rufzeichen aus nbrHearers(row), kommagetrennt,
//      "-" wenn leer.
//   6. "reach:<km>@<partner>" mit einer Nachkommastelle, oder "reach:-".
//   7. "age:<min>m": nbrRowAgeMin(row).
//
// Schreibt hoechstens outlen Byte inklusive Nullterminierung (snprintf-
// Semantik) und liefert die von snprintf gemeldete Laenge, oder 0 bei
// ungueltigem Index oder einer nicht belegten Zeile != 0.
int nbrFormatRow(const NbrMatrix &m, int row, uint16_t now_min, char *out, size_t outlen);

// Periodischer Schnappschuss fuer den 24-h-Dauertest (docs/nbr-logformat.md):
// emittiert SNAP, dann je belegter Zeile (Zeile 0 immer, sonst nur mit
// NBR_FLAG_USED) genau eine ROW-Zeile, dann ENDSNAP. Tut nichts, wenn
// nbrLog == NULL. Entscheidet NICHT selbst ueber das 15-Minuten-Intervall --
// das setzt der Aufrufer im Firmware-Rahmen; diese Funktion emittiert bei
// jedem Aufruf.
//
// <verdict> je ROW ist eines aus EXCL/RED/LEAF/UNK (Konzept 4.3), aus der
// bestehenden Urteilslogik von nbrExclusive() abgeleitet: Zeile 0 ist immer
// UNK (die Frage "exklusiv/redundant" ist auf sich selbst nicht definiert).
// Fuer jede andere Zeile: ohne frische cell[row][0] (kein Direktempfang von
// mir) ist es LEAF, wenn irgendjemand die Zeile trotzdem hoert
// (nbrHearers() > 0, nur ueber Relais sichtbar), sonst UNK (keinerlei
// Beobachtung). Mit frischer cell[row][0] ist es EXCL, wenn kein anderer
// Knoten in meiner Hoerweite die Zeile ebenfalls frisch hoert (dieselbe
// Bedingung wie in nbrExclusive()), sonst RED (redundant gedeckt).
//
// <meshneed> je ROW, LETZTE Spalte, ist eines aus NA/MESH/RED aus
// nbrRowMeshNeed() (Advisor-Pass 2026-09-21) -- die Gegenfrage zu
// <verdict>: nicht "ist MEIN Meshen fuer row noetig", sondern "muss row
// SELBST meshen, weil sie Knoten hoert, die sonst niemand hoert". Siehe
// nbrRowMeshNeed() oben fuer die Herleitung; die beiden Felder duerfen
// nicht miteinander verglichen werden (docs/nbr-logformat.md).
void nbrLogSnapshot(const NbrMatrix &m, uint16_t now_min);

// Eine gemeinsame Instanz fuers Geraet: geschrieben ausschliesslich aus
// OnRxDone (lora_functions.cpp), gelesen von Web-Seite und --neighbours/
// --nbrreset. Der Host-Test legt eigene lokale NbrMatrix-Werte an und
// braucht diese globale Instanz nicht.
#ifndef NATIVE_BUILD
extern NbrMatrix nbrMatrix;
#endif
