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
// configuration.h kommt. Der Host-Test (NATIVE_BUILD) bekommt sie aus
// platformio.ini und darf kein Arduino-Include sehen; deshalb der Zweig.
#if !defined(NBR_MAX_ROWS) && !defined(NATIVE_BUILD)
#include "configuration.h"
#endif

#ifndef NBR_MAX_ROWS
#error "NBR_MAX_ROWS ist nicht definiert. Board-Builds bekommen ihn aus configuration_global.h, der Host-Test aus platformio.ini (env native_nbr_matrix, -D NBR_MAX_ROWS=5)."
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

// Zeile (24 Byte): ein gehoertes Rufzeichen mit letzter Position und Typ.
struct NbrRow
{
    char     call[NBR_CALL_LEN];
    float    lat, lon;
    uint16_t last_min;
    uint8_t  flags;
    uint8_t  hw;
};

// Zelle (6 Byte): "Spalte hat Zeile gehoert" -- drei sattelnde Zaehler nach
// Frame-Typ, der zuletzt gesehene RSSI aus einem HEY-Bericht (negatives dBm,
// 0 = unbekannt) und die Letztzeit dieser Zelle.
struct NbrCell
{
    uint8_t  cnt_text, cnt_pos, cnt_hey;
    int8_t   rssi;
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
// Treffer auf cell[letzter_hop][0] mit rssi = rssi_here (Log: ME). Eine
// Zelle, deren last_min beim Treffer bereits verfallen ist, faengt bei ihren
// Zaehlern neu bei 0 an (Konzept 4.2), bevor der Treffer zaehlt. Ist die
// Tabelle voll, weicht beim Anlegen einer Fenster-Zeile die Zeile 1..N-1 mit
// der aeltesten last_min (Log: EVICT), ihre Zeilen- und Spaltenzellen werden
// genullt (Zeile 0 ist davon nie betroffen).
//
// Bei '@' setzt dest_gw=true das GW-Flag auf die Zeile des Absenders
// (erster Pfadeintrag, ueber nbrFind() aufgeloest -- Absender ist meist
// ausserhalb des Fensters und bekommt dafuer keine neue Zeile). Bei '@' wird
// zusaetzlich payload als "R<n>;g1;g2;..." gelesen (siehe
// appendHeySignalReport(), src/aprs_functions.cpp:1134): Gruppe i (1-basiert)
// ist "NCT,RSSI,SNR" und gehoert zum Paar (p[i-1], p[i]) -- ihre Zelle
// bekommt rssi = -RSSI, aber NUR wenn beide Enden aufgeloest sind (im
// Fenster liegen oder nach der Regel oben als bestehende Zeile gelten).
// Eine Gruppe, die nicht aus genau drei Kommafeldern besteht (oder fehlt,
// altes Format), wird uebersprungen; die Pfadtreffer aus dem ersten Schritt
// zaehlen trotzdem.
//
// Rueckgabe: Zahl der gemachten Pfad-Treffer (>= 0, zaehlt nur tatsaechlich
// geschriebene Zellen), -1 bei ungueltigem Rufzeichen/zu vielen Hops, -2 bei
// einer Schleife, -3 wenn im selben Frame mehr neue Fenster-Rufzeichen
// aufzuloesen waren als Opferzeilen frei blieben, 0 bei unbekanntem Typ.
int nbrNoteFrame(NbrMatrix &m, const char *path, char type, const char *payload,
                  bool dest_gw, int16_t rssi_here, uint16_t now_min);

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
//   4. "hears_me:<dBm>" oder "hears_me:-": RSSI aus cell[0][row], also wie
//      DIESE Zeile MICH zuletzt gehoert hat (Konzept 4.3, "Wer hoert mich").
//      "-" wenn nicht frisch oder RSSI unbekannt (0).
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
void nbrLogSnapshot(const NbrMatrix &m, uint16_t now_min);

// Eine gemeinsame Instanz fuers Geraet: geschrieben ausschliesslich aus
// OnRxDone (lora_functions.cpp), gelesen von Web-Seite und --neighbours/
// --nbrreset. Der Host-Test legt eigene lokale NbrMatrix-Werte an und
// braucht diese globale Instanz nicht.
#ifndef NATIVE_BUILD
extern NbrMatrix nbrMatrix;
#endif
