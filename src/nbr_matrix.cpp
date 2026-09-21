// Nachbarschaftsmatrix, Wave 1 -- siehe src/nbr_matrix.h fuer die Regeln aus
// Konzept ~/Desktop/Nachbarschaftsmatrix.html 4.1-4.4. Diese Datei bleibt
// bewusst Arduino-frei (siehe Header); interne Helfer stehen `static`, weil
// sie kein Teil der oeffentlichen Schnittstelle sind.

#include "nbr_matrix.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// NULL = Instrumentierung aus (siehe nbr_matrix.h). Der Aufrufer im
// Firmware-Rahmen haengt hier printfdeb() (oder aequivalent) ein.
NbrLogFn nbrLog = NULL;

// 'T'/'P'/'H' fuers Logformat (docs/nbr-logformat.md), statt des rohen
// Frame-Typzeichens ':'/'!'/'@'.
static char nbrLogTypeChar(char type)
{
    return (type == ':') ? 'T' : (type == '!') ? 'P' : 'H';
}

// Zaehler des zum Typ passenden Feldes -- fuer <cnt> in EDGE/ME NACH dem
// Treffer (nbrHitCell() wurde vorher schon aufgerufen).
static uint8_t nbrCellCount(const NbrCell &c, char type)
{
    return (type == ':') ? c.cnt_text : (type == '!') ? c.cnt_pos : c.cnt_hey;
}

// DROP-Zeile fuer jede fruehe Ablehnung in nbrNoteFrame(). path kann NULL
// sein (ungueltiger Aufruf) -- dann wird ein leerer Pfad geloggt statt ein
// %s mit NULL an snprintf zu reichen.
static void nbrLogDrop(uint16_t now_min, const char *reason, const char *path)
{
    if (!nbrLog)
        return;
    char buf[160];
    snprintf(buf, sizeof(buf), "[NBR]|DROP|%u|%s|%s", (unsigned)now_min, reason, path ? path : "");
    nbrLog(buf);
}

// CUT-Zeile: der Pfad war laenger als das 2-Hop-Fenster, <kept> Token davon
// wurden fuer die Zeilenvergabe genutzt.
static void nbrLogCut(uint16_t now_min, int ntok, int kept, const char *path)
{
    if (!nbrLog)
        return;
    char buf[160];
    snprintf(buf, sizeof(buf), "[NBR]|CUT|%u|%d|%d|%s", (unsigned)now_min, ntok, kept, path ? path : "");
    nbrLog(buf);
}

// EDGE-Zeile: "<to> hat <from> gehoert" -- <cnt> ist der Typzaehler der
// Zelle NACH dem Treffer (nbrHitCell() ist zu diesem Zeitpunkt schon
// gelaufen).
static void nbrLogEdge(uint16_t now_min, const char *from, const char *to, char type, const NbrCell &c)
{
    if (!nbrLog)
        return;
    char buf[160];
    snprintf(buf, sizeof(buf), "[NBR]|EDGE|%u|%s|%s|%c|%d|%u",
             (unsigned)now_min, from, to, nbrLogTypeChar(type), (int)c.rssi, (unsigned)nbrCellCount(c, type));
    nbrLog(buf);
}

// ME-Zeile: der letzte Hop wurde von mir direkt gehoert (cell[last][0]).
static void nbrLogMe(uint16_t now_min, const char *from, char type, const NbrCell &c)
{
    if (!nbrLog)
        return;
    char buf[160];
    snprintf(buf, sizeof(buf), "[NBR]|ME|%u|%s|%c|%d|%u",
             (unsigned)now_min, from, nbrLogTypeChar(type), (int)c.rssi, (unsigned)nbrCellCount(c, type));
    nbrLog(buf);
}

// Eine Zelle gilt nur dann als Beobachtung, wenn mindestens einer ihrer
// Typzaehler > 0 ist -- last_min allein (z. B. 0 nach memset) waere sonst
// eine Beobachtung "zur Boot-Minute".
static bool nbrCellSet(const NbrCell &c)
{
    return c.cnt_text || c.cnt_pos || c.cnt_hey;
}

// RSSI wird immer auf den Wertebereich begrenzt, den int8_t und die
// Konzept-Zusicherung "-128..0, 0 = unbekannt" vorsehen. Der Parameter ist
// bewusst breiter als int8_t, damit weder rssi_here (int16_t) noch eine
// negierte HEY-RSSI-Ziffernfolge vor dem Vergleich ueberlaeuft.
static int8_t nbrClampRssi(int32_t rssi)
{
    if (rssi > 0)
        return 0;
    if (rssi < -128)
        return -128;
    return (int8_t)rssi;
}

// Legt Zeile idx neu an (Rufzeichen + USED, alles andere auf 0/now_min).
static void nbrRowInit(NbrMatrix &m, int idx, const char *call, uint16_t now_min)
{
    memset(&m.rows[idx], 0, sizeof(NbrRow));
    strncpy(m.rows[idx].call, call, NBR_CALL_LEN - 1);
    m.rows[idx].call[NBR_CALL_LEN - 1] = '\0';
    m.rows[idx].flags = NBR_FLAG_USED;
    m.rows[idx].last_min = now_min;
}

// Nullt bei einer Verdraengung sowohl die Zeile als auch die Spalte idx --
// eine verdraengte Zeile darf keine alte Hoerbeziehung hinterlassen, weder
// als Zeile noch als Spalte (Konzept 4.2).
static void nbrZeroRowAndColumn(NbrMatrix &m, int idx)
{
    for (int y = 0; y < NBR_MAX_ROWS; y++)
        memset(&m.cells[idx][y], 0, sizeof(NbrCell));
    for (int x = 0; x < NBR_MAX_ROWS; x++)
        memset(&m.cells[x][idx], 0, sizeof(NbrCell));
}

// Liest-ONLY, welchen Index ein Touch fuer call waehlen wuerde: die
// vorhandene Zeile, sonst die erste freie, sonst die mit der groessten
// Alterslluecke -- aber niemals einen Index, dessen Bit in protected_mask
// gesetzt ist. protected_mask haelt die Indizes fest, die im selben
// nbrNoteFrame()-Aufruf schon einem ANDEREN Rufzeichen zugesagt wurden
// (M1): ohne diese Sperre wuerden zwei neue Rufzeichen im selben Frame
// beide dieselbe (freie oder aelteste) Zeile planen, der zweite Treffer
// faellt dann auf die Diagonale statt auf ein eigenes Paar. Liefert -1,
// wenn ausser Zeile 0 kein einziger Index mehr frei ist (weder unbenutzt
// noch verdraengbar) -- der Aufrufer muss dann den ganzen Frame verwerfen,
// BEVOR er irgendetwas committet.
static int nbrPlanRow(const NbrMatrix &m, const char *call, uint16_t now_min, uint32_t protected_mask)
{
    if (strncmp(call, m.rows[0].call, NBR_CALL_LEN) == 0)
        return 0;

    for (int i = 1; i < NBR_MAX_ROWS; i++)
        if ((m.rows[i].flags & NBR_FLAG_USED) && strncmp(call, m.rows[i].call, NBR_CALL_LEN) == 0)
            return i;

    for (int i = 1; i < NBR_MAX_ROWS; i++)
        if (!(protected_mask & (1u << (unsigned)i)) && !(m.rows[i].flags & NBR_FLAG_USED))
            return i;

    // Tabelle voll: die nicht gesperrte Zeile mit der groessten
    // Alterslluecke weicht. Der ueberlaufsichere Altersvergleich ist
    // derselbe wie in nbrFresh().
    int oldest = -1;
    uint16_t oldest_age = 0;
    for (int i = 1; i < NBR_MAX_ROWS; i++)
    {
        if (protected_mask & (1u << (unsigned)i))
            continue;
        uint16_t age = (uint16_t)(now_min - m.rows[i].last_min);
        if (oldest < 0 || age > oldest_age)
        {
            oldest = i;
            oldest_age = age;
        }
    }
    return oldest; // -1, wenn in diesem Aufruf kein Opfer mehr frei ist
}

// Setzt einen von nbrPlanRow() gelieferten Index tatsaechlich um: nichts zu
// tun fuer Zeile 0 oder einen bereits passenden Fund, sonst neu anlegen.
// Zeile UND Spalte werden in BEIDEN Faellen (freier Slot wie Verdraengung)
// zuerst genullt -- das nullt bei einer echten Verdraengung die alten
// Hoerbeziehungen weg, und heilt nebenbei eine Zeile, die durch einen
// unvollstaendigen Reset aus einem anderen Task faelschlich als "frei"
// dasteht, aber noch alte Zellwerte traegt.
static void nbrCommitRow(NbrMatrix &m, int target_idx, const char *call, uint16_t now_min)
{
    if (target_idx == 0)
        return;
    if ((m.rows[target_idx].flags & NBR_FLAG_USED) &&
        strncmp(call, m.rows[target_idx].call, NBR_CALL_LEN) == 0)
        return;

    // Eine benutzte Zeile mit einem ANDEREN Rufzeichen wird hier ueberschrieben
    // -- das ist eine echte Verdraengung, nicht nur die Erstbelegung eines
    // freien Slots (der Fall oben, "gleiches Rufzeichen", ist schon
    // abgehandelt; ein leerer Slot hat kein NBR_FLAG_USED).
    if ((m.rows[target_idx].flags & NBR_FLAG_USED) && nbrLog)
    {
        char buf[160];
        snprintf(buf, sizeof(buf), "[NBR]|EVICT|%u|%d|%s|%s",
                 (unsigned)now_min, target_idx, m.rows[target_idx].call, call);
        nbrLog(buf);
    }
    nbrZeroRowAndColumn(m, target_idx);
    nbrRowInit(m, target_idx, call, now_min);
}

// Ein Treffer auf eine verfallene Zelle faengt bei ihren Zaehlern neu bei 0
// an (Konzept 4.2), bevor er zaehlt; der RSSI wird dabei mitgeloescht, weil
// er zu genau diesen Zaehlern gehoert. Eine frische Zelle behaelt ihren
// RSSI, auch wenn der aktuelle Treffer keinen eigenen mitbringt (Text-/POS-
// Frames haben keinen Signalbericht).
static void nbrHitCell(NbrCell &c, char type, uint16_t now_min)
{
    if (nbrCellSet(c) && (uint16_t)(now_min - c.last_min) >= NBR_WINDOW_MIN)
    {
        c.cnt_text = c.cnt_pos = c.cnt_hey = 0;
        c.rssi = 0;
    }
    uint8_t *cnt = (type == ':') ? &c.cnt_text : (type == '!') ? &c.cnt_pos : &c.cnt_hey;
    if (*cnt < 255)
        (*cnt)++;
    c.last_min = now_min;
}

// Geister-Sweep gegen den 16-Bit-Minuten-Ueberlauf (siehe NBR_WINDOW_MIN in
// nbr_matrix.h): eine Zelle/Zeile, die seit > 32768 Minuten (die Haelfte des
// darstellbaren Bereichs) nicht mehr getroffen wurde, koennte durch den
// Ueberlauf wieder faelschlich frisch erscheinen UND als "juengste" Zeile
// eine echte, aktuelle Zeile bei einer Verdraengung ausstechen. Laeuft nur
// alle 1024 Minuten (billig genug fuer jeden nbrNoteFrame()/nbrNotePos()-
// Aufruf), nicht bei jedem Treffer einzeln.
static void nbrMaybeSweep(NbrMatrix &m, uint16_t now_min)
{
    if ((uint16_t)(now_min - m.last_sweep) < 1024)
        return;

    for (int i = 1; i < NBR_MAX_ROWS; i++)
    {
        if ((m.rows[i].flags & NBR_FLAG_USED) && (uint16_t)(now_min - m.rows[i].last_min) >= 32768)
        {
            m.rows[i].flags = 0;
            nbrZeroRowAndColumn(m, i);
        }
    }
    for (int x = 0; x < NBR_MAX_ROWS; x++)
        for (int y = 0; y < NBR_MAX_ROWS; y++)
        {
            NbrCell &c = m.cells[x][y];
            if (nbrCellSet(c) && (uint16_t)(now_min - c.last_min) >= 32768)
            {
                c.cnt_text = c.cnt_pos = c.cnt_hey = 0;
                c.rssi = 0;
            }
        }
    m.last_sweep = now_min;
}

// Ein Rufzeichen-Token: 3..9 Zeichen aus [A-Z0-9-]. Dieselbe Zeichenmenge
// wie das Pfadformat des Frames, nur ohne SSID-Sonderfaelle -- die Matrix
// braucht keine SSID-Bedeutung, nur die Byte-Identitaet des Tokens.
static bool nbrValidToken(const char *tok, size_t len)
{
    if (len < 3 || len > 9)
        return false;
    for (size_t i = 0; i < len; i++)
    {
        char c = tok[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
    }
    return true;
}

// Zerlegt path an ',' in bis zu max_tokens Rufzeichen. Liefert die Anzahl
// oder -1, wenn ein Token ungueltig ist oder es mehr als max_tokens gibt --
// in beiden Faellen wird NICHTS geschrieben, der Aufrufer sieht das am
// Rueckgabewert, bevor irgendeine Zeile angefasst wird.
static int nbrTokenizePath(const char *path, char tokens[][NBR_CALL_LEN], int max_tokens)
{
    if (!path || !*path)
        return -1;

    int n = 0;
    const char *p = path;
    for (;;)
    {
        const char *start = p;
        while (*p && *p != ',')
            p++;
        size_t len = (size_t)(p - start);
        if (n >= max_tokens || !nbrValidToken(start, len))
            return -1;
        memcpy(tokens[n], start, len);
        tokens[n][len] = '\0';
        n++;
        if (*p != ',')
            break;
        p++;
    }
    return n;
}

// Ziffernfolge -> long, ohne stdlib.h (dessen strtol dieser Datei nicht zur
// Verfuegung steht). Auf 6 Ziffern begrenzt: mehr braucht keine reale RSSI-
// Zahl, und das haelt die folgende Negation garantiert ueberlauffrei.
static long nbrParseUint(const char *s, size_t len)
{
    if (len == 0 || len > 6)
        return -1;
    long v = 0;
    for (size_t i = 0; i < len; i++)
    {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

// Eine HEY-Berichtsgruppe "NCT,RSSI,SNR" (appendHeySignalReport(),
// src/aprs_functions.cpp:1134): genau zwei Kommas, sonst ist es kein
// gueltiger Bericht und wird uebersprungen (altes/fremdes Format).
static void nbrApplyGroup(NbrMatrix &m, int row_x, int row_y, const char *g, size_t len)
{
    const char *comma1 = NULL;
    const char *comma2 = NULL;
    for (size_t i = 0; i < len; i++)
    {
        if (g[i] != ',')
            continue;
        if (!comma1)
            comma1 = g + i;
        else if (!comma2)
            comma2 = g + i;
        else
            return; // drittes Komma: nicht "NCT,RSSI,SNR"
    }
    if (!comma1 || !comma2)
        return;

    const char *rssi_start = comma1 + 1;
    size_t rssi_len = (size_t)(comma2 - rssi_start);
    long rssi = nbrParseUint(rssi_start, rssi_len);
    if (rssi < 0)
        return; // nicht numerisch

    m.cells[row_x][row_y].rssi = nbrClampRssi((int32_t)(-rssi));
}

// Liest "R<n>;g1;g2;..." und verteilt Gruppe i (1-basiert) auf das Paar
// (row_idx[i-1], row_idx[i]) -- Konzept 4.1, Abb. 3. Ueberzaehlige Gruppen
// (mehr als ntok-1) werden ignoriert, fehlende ebenso. row_idx traegt -1 fuer
// jedes Token ausserhalb des 2-Hop-Fensters ohne bestehende Zeile (siehe
// nbrNoteFrame()) -- eine Gruppe, deren Paar nicht VOLLSTAENDIG aufgeloest
// ist, wird uebersprungen statt mit einem negativen Index zuzugreifen.
static void nbrApplyHeyGroups(NbrMatrix &m, const int *row_idx, int ntok, const char *payload)
{
    if (!payload)
        return;
    const char *sep = strchr(payload, ';');
    if (!sep)
        return;

    const char *p = sep + 1;
    int gi = 1;
    while (*p && gi <= ntok - 1)
    {
        const char *start = p;
        while (*p && *p != ';')
            p++;
        size_t len = (size_t)(p - start);
        if (len > 0 && row_idx[gi - 1] >= 0 && row_idx[gi] >= 0)
            nbrApplyGroup(m, row_idx[gi - 1], row_idx[gi], start, len);
        if (*p == ';')
            p++;
        gi++;
    }
}

// --- oeffentliche Schnittstelle --------------------------------------------

void nbrInit(NbrMatrix &m, const char *own_call, uint16_t now_min)
{
    memset(&m, 0, sizeof(NbrMatrix));
    strncpy(m.rows[0].call, own_call, NBR_CALL_LEN - 1);
    m.rows[0].call[NBR_CALL_LEN - 1] = '\0';
    m.rows[0].flags = NBR_FLAG_USED;
    m.rows[0].last_min = now_min;
    m.boot_min = now_min;
    m.last_sweep = now_min;
}

bool nbrFresh(uint16_t last_min, uint16_t now_min)
{
    return (uint16_t)(now_min - last_min) < NBR_WINDOW_MIN;
}

int nbrFind(const NbrMatrix &m, const char *call)
{
    if (!call)
        return -1;
    if (strncmp(call, m.rows[0].call, NBR_CALL_LEN) == 0)
        return 0;
    for (int i = 1; i < NBR_MAX_ROWS; i++)
        if ((m.rows[i].flags & NBR_FLAG_USED) && strncmp(call, m.rows[i].call, NBR_CALL_LEN) == 0)
            return i;
    return -1;
}

void nbrReset(NbrMatrix &m, uint16_t now_min)
{
    char own[NBR_CALL_LEN];
    memcpy(own, m.rows[0].call, NBR_CALL_LEN);
    memset(&m, 0, sizeof(NbrMatrix));
    memcpy(m.rows[0].call, own, NBR_CALL_LEN);
    m.boot_min = now_min;
    m.last_sweep = now_min;
}

uint16_t nbrRowAgeMin(const NbrMatrix &m, int row, uint16_t now_min)
{
    if (row < 0 || row >= NBR_MAX_ROWS)
        return 0;
    return (uint16_t)(now_min - m.rows[row].last_min);
}

int nbrNoteFrame(NbrMatrix &m, const char *path, char type, const char *payload,
                  bool dest_gw, int16_t rssi_here, uint16_t now_min)
{
    // Der Sweep ist Wartung unabhaengig von diesem Frame und laeuft darum
    // VOR jeder Pruefung -- auch ein Frame, der gleich danach verworfen
    // wird, darf einen faelligen Sweep nicht aufschieben.
    nbrMaybeSweep(m, now_min);

    if (type != ':' && type != '!' && type != '@')
    {
        nbrLogDrop(now_min, "TYPE", path);
        return 0;
    }

    char tokens[8][NBR_CALL_LEN];
    int ntok = nbrTokenizePath(path, tokens, 8);
    if (ntok < 0)
    {
        nbrLogDrop(now_min, "TOK", path);
        return -1;
    }

    // Eine Schleife (Rufzeichen doppelt im Pfad) ist kein Hoerbeweis --
    // ganz verwerfen, bevor irgendeine Zeile angefasst wird. Laeuft ueber
    // den GANZEN Pfad, unabhaengig vom 2-Hop-Fenster unten.
    for (int i = 0; i < ntok; i++)
        for (int j = i + 1; j < ntok; j++)
            if (strncmp(tokens[i], tokens[j], NBR_CALL_LEN) == 0)
            {
                nbrLogDrop(now_min, "LOOP", path);
                return -2;
            }

    // 2-Hop-Fenster (Betreiber-Vorgabe, siehe nbr_matrix.h): nur die letzten
    // zwei Pfad-Token duerfen noch eine Zeile bekommen. Token davor (Index
    // < start) werden weder geplant noch committet.
    int start = (ntok > 2) ? ntok - 2 : 0;
    if (ntok > 2)
        nbrLogCut(now_min, ntok, ntok - start, path);

    // row_idx[i] = -1 heisst "kein Fenster-Token und (noch) keine bestehende
    // Zeile" -- durchgaengig vorbelegt, damit jede spaetere Stelle (HEY-
    // Gruppen, Kantenschleife) das statt eines undefinierten Werts sieht.
    int row_idx[8];
    for (int i = 0; i < 8; i++)
        row_idx[i] = -1;

    // Erst ALLE Fenster-Zeilen lesend planen (M1), danach erst committen --
    // siehe nbrPlanRow(). Scheitert die Planung fuer ein Fenster-Rufzeichen,
    // ist noch keine einzige Zeile angefasst, der Frame wird ganz verworfen.
    uint32_t protected_mask = 0;
    for (int i = start; i < ntok; i++)
    {
        int idx = nbrPlanRow(m, tokens[i], now_min, protected_mask);
        if (idx < 0)
        {
            nbrLogDrop(now_min, "FULL", path);
            return -3;
        }
        row_idx[i] = idx;
        protected_mask |= (1u << (unsigned)idx);
    }
    for (int i = start; i < ntok; i++)
        nbrCommitRow(m, row_idx[i], tokens[i], now_min);

    // Regel 3 (Gratis-Erweiterung): ein Token VOR dem Fenster bekommt NIE
    // eine neue Zeile, aber wenn es schon eine hat, loesen wir sie hier NUR
    // LESEND auf (nbrFind(), kein nbrPlanRow()/nbrCommitRow()) -- das kann
    // aus einem frueheren Frame stammen oder, fuer i == start-1, aus dem
    // Fenster-Commit von eben (dessen erstes Token).
    for (int i = 0; i < start; i++)
        row_idx[i] = nbrFind(m, tokens[i]);

    int hits = 0;
    for (int i = 0; i + 1 < ntok; i++)
    {
        int x = row_idx[i], y = row_idx[i + 1];
        if (x < 0 || y < 0)
            continue; // ausserhalb des Fensters ohne zwei bestehende Zeilen: keine Kante
        nbrHitCell(m.cells[x][y], type, now_min);
        m.rows[x].last_min = now_min;
        m.rows[y].last_min = now_min;
        nbrLogEdge(now_min, tokens[i], tokens[i + 1], type, m.cells[x][y]);
        hits++;
    }

    // "Ich habe den letzten Hop gehoert" -- entfaellt beim eigenen Echo
    // (letzter Hop = ich selbst, Konzept 4.1). Der letzte Hop ist immer
    // Fenster-Token, row_idx[ntok-1] ist also immer gueltig.
    if (strncmp(tokens[ntok - 1], m.rows[0].call, NBR_CALL_LEN) != 0)
    {
        int last = row_idx[ntok - 1];
        nbrHitCell(m.cells[last][0], type, now_min);
        m.cells[last][0].rssi = nbrClampRssi(rssi_here);
        m.rows[last].last_min = now_min;
        m.rows[0].last_min = now_min;
        nbrLogMe(now_min, tokens[ntok - 1], type, m.cells[last][0]);
        hits++;
    }

    if (type == '@')
    {
        int sender = row_idx[0];
        if (dest_gw && sender >= 0)
            m.rows[sender].flags |= NBR_FLAG_GW;
        nbrApplyHeyGroups(m, row_idx, ntok, payload);
    }

    return hits;
}

void nbrNotePos(NbrMatrix &m, const char *call, float lat, float lon, bool mesh,
                uint8_t hw, uint16_t now_min)
{
    nbrMaybeSweep(m, now_min);
    // Legt bewusst KEINE Zeile mehr an (siehe nbr_matrix.h) -- ein Knoten,
    // der nur ueber relayte POS-Frames sichtbar waere, bleibt ohne Zeile.
    int idx = nbrFind(m, call);
    if (idx < 0)
        return;
    NbrRow &r = m.rows[idx];
    r.lat = lat;
    r.lon = lon;
    r.flags |= NBR_FLAG_POS;
    if (mesh)
        r.flags |= NBR_FLAG_MESH;
    else
        r.flags &= (uint8_t)~NBR_FLAG_MESH;
    r.hw = hw;
    r.last_min = now_min;

    if (nbrLog)
    {
        char buf[160];
        snprintf(buf, sizeof(buf), "[NBR]|POS|%u|%s|%.5f|%.5f|%d|%u",
                 (unsigned)now_min, call, (double)lat, (double)lon, mesh ? 1 : 0, (unsigned)hw);
        nbrLog(buf);
    }
}

uint8_t nbrHearers(const NbrMatrix &m, int row, uint16_t now_min, uint8_t *out, uint8_t max)
{
    if (row < 0 || row >= NBR_MAX_ROWS)
        return 0;
    uint8_t n = 0;
    for (int y = 0; y < NBR_MAX_ROWS; y++)
    {
        if (y == row)
            continue;
        const NbrCell &c = m.cells[row][y];
        if (nbrCellSet(c) && nbrFresh(c.last_min, now_min))
        {
            if (out && n < max)
                out[n] = (uint8_t)y;
            n++;
        }
    }
    return n;
}

// Direkt gehoert: cell[x][0] gesetzt und frisch -- Basis fuer Exklusivitaet
// UND fuer das Verdikt in nbrLogSnapshot() (Konzept 4.3).
static bool nbrHeardDirectly(const NbrMatrix &m, int x, uint16_t now_min)
{
    const NbrCell &c0 = m.cells[x][0];
    return nbrCellSet(c0) && nbrFresh(c0.last_min, now_min);
}

// Zeile x (!=0) ist exklusiv, wenn sonst niemand in meiner Hoerweite (y != 0,
// x) eine frische cell[x][y] hat -- Konzept 4.3, "Hoerer(X) = {ich}".
static bool nbrRowExclusive(const NbrMatrix &m, int x, uint16_t now_min)
{
    for (int y = 1; y < NBR_MAX_ROWS; y++)
    {
        if (y == x)
            continue;
        const NbrCell &c = m.cells[x][y];
        if (nbrCellSet(c) && nbrFresh(c.last_min, now_min))
            return false;
    }
    return true;
}

int nbrExclusive(const NbrMatrix &m, uint16_t now_min, uint8_t *out, uint8_t max)
{
    bool any_heard = false;
    int count = 0;
    for (int x = 1; x < NBR_MAX_ROWS; x++)
    {
        if (!(m.rows[x].flags & NBR_FLAG_USED))
            continue;

        if (!nbrHeardDirectly(m, x, now_min))
            continue;
        any_heard = true;

        if (nbrRowExclusive(m, x, now_min))
        {
            if (out && count < (int)max)
                out[count] = (uint8_t)x;
            count++;
        }
    }
    return any_heard ? count : -1;
}

// Verdikt je Zeile fuer nbrLogSnapshot() (Konzept 4.3) -- siehe nbr_matrix.h
// bei nbrLogSnapshot() fuer die Herleitung je Fall.
static const char *nbrRowVerdict(const NbrMatrix &m, int x, uint16_t now_min)
{
    if (x == 0)
        return "UNK";
    if (!nbrHeardDirectly(m, x, now_min))
        return nbrHearers(m, x, now_min, NULL, 0) > 0 ? "LEAF" : "UNK";
    return nbrRowExclusive(m, x, now_min) ? "EXCL" : "RED";
}

float nbrDistKm(float lat1, float lon1, float lat2, float lon2)
{
    const double R = 6371.0;
    double dlat = (double)(lat2 - lat1) * M_PI / 180.0;
    double dlon = (double)(lon2 - lon1) * M_PI / 180.0;
    double a = sin(dlat / 2.0) * sin(dlat / 2.0) +
               cos((double)lat1 * M_PI / 180.0) * cos((double)lat2 * M_PI / 180.0) *
                   sin(dlon / 2.0) * sin(dlon / 2.0);
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return (float)(R * c);
}

float nbrReach(const NbrMatrix &m, int row, uint16_t now_min, int *partner)
{
    if (partner)
        *partner = -1;
    if (row < 0 || row >= NBR_MAX_ROWS)
        return -1.0f;
    if (!(m.rows[row].flags & NBR_FLAG_POS))
        return -1.0f;

    float best = -1.0f;
    int best_partner = -1;
    for (int y = 0; y < NBR_MAX_ROWS; y++)
    {
        if (y == row)
            continue;
        if (!(m.rows[y].flags & NBR_FLAG_POS))
            continue;
        bool linked = (nbrCellSet(m.cells[row][y]) && nbrFresh(m.cells[row][y].last_min, now_min)) ||
                      (nbrCellSet(m.cells[y][row]) && nbrFresh(m.cells[y][row].last_min, now_min));
        if (!linked)
            continue;
        float d = nbrDistKm(m.rows[row].lat, m.rows[row].lon, m.rows[y].lat, m.rows[y].lon);
        if (d > best)
        {
            best = d;
            best_partner = y;
        }
    }
    if (best_partner < 0)
        return -1.0f;
    if (partner)
        *partner = best_partner;
    return best;
}

int nbrFormatRow(const NbrMatrix &m, int row, uint16_t now_min, char *out, size_t outlen)
{
    if (!out || outlen == 0)
        return 0;
    out[0] = '\0';
    if (row < 0 || row >= NBR_MAX_ROWS)
        return 0;
    if (row != 0 && !(m.rows[row].flags & NBR_FLAG_USED))
        return 0;

    const NbrRow &r = m.rows[row];

    // "hoert mich mit": cell[0][row] ist "row hat 0 (mich) gehoert" --
    // Konzept 4.3, Spalte "hoert mich mit". 0 im Feld heisst "unbekannt",
    // nicht "0 dBm".
    char hears_me[8] = "-";
    const NbrCell &c_hears = m.cells[0][row];
    if (row != 0 && nbrFresh(c_hears.last_min, now_min) && c_hears.rssi != 0)
        snprintf(hears_me, sizeof(hears_me), "%d", (int)c_hears.rssi);

    uint8_t hearer_idx[NBR_MAX_ROWS];
    uint8_t hn = nbrHearers(m, row, now_min, hearer_idx, (uint8_t)NBR_MAX_ROWS);
    char hearers[NBR_MAX_ROWS * NBR_CALL_LEN];
    hearers[0] = '\0';
    uint8_t list_n = hn < NBR_MAX_ROWS ? hn : NBR_MAX_ROWS;
    for (uint8_t i = 0; i < list_n; i++)
    {
        if (i)
            strncat(hearers, ",", sizeof(hearers) - strlen(hearers) - 1);
        strncat(hearers, m.rows[hearer_idx[i]].call, sizeof(hearers) - strlen(hearers) - 1);
    }
    if (list_n == 0)
        snprintf(hearers, sizeof(hearers), "-");

    int partner = -1;
    float reach = nbrReach(m, row, now_min, &partner);
    char reach_buf[32];
    if (reach >= 0.0f && partner >= 0)
        snprintf(reach_buf, sizeof(reach_buf), "%.1fkm@%s", (double)reach, m.rows[partner].call);
    else
        snprintf(reach_buf, sizeof(reach_buf), "-");

    unsigned age = (unsigned)nbrRowAgeMin(m, row, now_min);

    return snprintf(out, outlen, "%s %s %s hears_me:%s hearers:%s reach:%s age:%um",
                     r.call,
                     (r.flags & NBR_FLAG_GW) ? "GW+" : "GW-",
                     (r.flags & NBR_FLAG_MESH) ? "M+" : "M-",
                     hears_me, hearers, reach_buf, age);
}

void nbrLogSnapshot(const NbrMatrix &m, uint16_t now_min)
{
    if (!nbrLog)
        return;

    int rows_used = 1; // Zeile 0 zaehlt immer, auch ohne NBR_FLAG_USED
    for (int i = 1; i < NBR_MAX_ROWS; i++)
        if (m.rows[i].flags & NBR_FLAG_USED)
            rows_used++;

    int cells_set = 0;
    for (int x = 0; x < NBR_MAX_ROWS; x++)
        for (int y = 0; y < NBR_MAX_ROWS; y++)
            if (nbrCellSet(m.cells[x][y]))
                cells_set++;

    char buf[160];
    snprintf(buf, sizeof(buf), "[NBR]|SNAP|%u|%s|%d|%d|%d",
             (unsigned)now_min, m.rows[0].call, rows_used, (int)NBR_MAX_ROWS, cells_set);
    nbrLog(buf);

    for (int i = 0; i < NBR_MAX_ROWS; i++)
    {
        if (i != 0 && !(m.rows[i].flags & NBR_FLAG_USED))
            continue;
        unsigned age = (unsigned)nbrRowAgeMin(m, i, now_min);
        unsigned hearers = (unsigned)nbrHearers(m, i, now_min, NULL, 0);
        snprintf(buf, sizeof(buf), "[NBR]|ROW|%u|%d|%s|%u|%u|%u|%s",
                 (unsigned)now_min, i, m.rows[i].call, (unsigned)m.rows[i].flags,
                 age, hearers, nbrRowVerdict(m, i, now_min));
        nbrLog(buf);
    }

    snprintf(buf, sizeof(buf), "[NBR]|ENDSNAP|%u", (unsigned)now_min);
    nbrLog(buf);
}

// Eine Instanz fuers ganze Geraet, im BSS: geschrieben ausschliesslich aus
// OnRxDone (lora_functions.cpp), gelesen von Web-Seite und --neighbours/
// --nbrreset (W2).
#ifndef NATIVE_BUILD
NbrMatrix nbrMatrix;
#endif
