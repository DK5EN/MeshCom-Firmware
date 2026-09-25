// Nachbarschaftsmatrix als Kantenpool (MeshCom 5, Stufe 1, Welle 2) -- siehe
// src/nbr_matrix.h fuer die oeffentliche Schnittstelle und
// docs/meshcom5-topologie/ 4.1-4.5 fuer das Konzept. Diese Datei bleibt
// Arduino-frei (siehe Header); einzige Ausnahme ist die Scheduler-Klammer
// unten, die auf nRF52 FreeRTOS braucht.
//
// Aufbau:
//   - Jede oeffentliche Funktion ist eine duenne Huelle: Klammer nehmen,
//     eine interne static-Funktion rufen, Klammer loslassen, DANACH loggen.
//     Intern ruft nie eine oeffentliche Funktion eine andere (keine
//     verschachtelte Klammer, kein Log unter der Klammer), und keine interne
//     Funktion ruft eine oeffentliche -- das haelt die Datei auch in einem
//     Namensraum mehrfach einbindbar (test/test_nbr_replay).
//   - Frische wird IMMER an der Kantenminute geprueft. nbrSweep() raeumt nur
//     auf (Kanten ueber NBR_WINDOW_MIN frei, Geisterzeilen, Halbierung); ob er
//     gelaufen ist, aendert keine Entscheidung (ausser ueber die Halbierung,
//     die nur er ausfuehrt, und ueber einen vollen Pool).

#include "nbr_matrix.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- Scheduler-Klammer (Konzept 4.5) ----------------------------------------
//
// nRF52: LORA-Task (OnRxDone, Schreiber) und Loop-Task (Sweep, --nbrreset,
// Web, Konsole, Bericht) haben dieselbe Prioritaet und wechseln bei jedem
// Aufwachen eines hoeher priorisierten Tasks reihum -- mitten in einer
// Aenderung. vTaskSuspendAll() haelt nur den Scheduler an (kein Interrupt-
// Sperren, der Tick laeuft weiter, Vorbild N-16 in lora_functions.cpp) und
// ist verschachtelbar. NRF52_SERIES und ARDUINO_ARCH_NRF52 setzt das
// Adafruit-Framework fuer jedes nRF52-Board (RAK4631, Heltec T114, T-Echo);
// die Host-Umgebung native_nrf52_settings_paths setzt NRF52_SERIES zusammen
// mit NATIVE_BUILD, darum der NATIVE_BUILD-Ausschluss. ESP32 und Host: leer
// (auf ESP32 laufen Empfang, Web und BLE im selben Loop-Task).
//
// Unter der Klammer: kein nbrLog (Serial kann auf nRF52 blockieren), kein
// printf mit Gleitkomma (newlib-dtoa alloziert).
#if (defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52)) && !defined(NATIVE_BUILD)
#include <FreeRTOS.h>
#include <task.h>
#define NBR_LOCK()   vTaskSuspendAll()
#define NBR_UNLOCK() ((void)xTaskResumeAll())
#ifndef NBR_DEFER_LOG
#define NBR_DEFER_LOG 1
#endif
#else
#define NBR_LOCK()   ((void)0)
#define NBR_UNLOCK() ((void)0)
#endif

// SYM-Zeilen entstehen mitten in der Relay-Rechnung. Wo die Klammer wirkt
// (nRF52), werden sie als Satz gemerkt und erst nach der Klammer
// ausgegeben (statischer Puffer, siehe nbrISymAdd()); ohne Klammer (ESP32,
// Host) laeuft alles in einem Task, und die Zeile geht wie frueher sofort
// hinaus -- kein Puffer, kein Generationszaehler. Ein Codepfad: beide Faelle
// laufen durch nbrISymAdd()/nbrISymEmit(); der Host-Test setzt
// NBR_DEFER_LOG=1, um den nRF52-Pfad ebenfalls zu pruefen.
#ifndef NBR_DEFER_LOG
#define NBR_DEFER_LOG 0
#endif

static_assert(sizeof(NbrRow) == 12, "NbrRow muss 12 Byte ohne Fuellung bleiben (Konzept 4.1)");
static_assert(sizeof(NbrEdge) == 6, "NbrEdge muss 6 Byte ohne Fuellung bleiben (Konzept 4.1)");
static_assert(NBR_MAX_EDGES <= 65535, "Kantenindex muss in uint16_t passen");

// NULL = Instrumentierung aus (siehe nbr_matrix.h).
NbrLogFn nbrLog = NULL;

#define NBR_EDGE_FREE   0xFF     // NbrEdge.x/.y eines freien Eintrags
#define NBR_POS_UNKNOWN 0x7FFF   // NbrRow.lat16/.lon16 unbekannt

// Rufzeichenwort fuer ein eigenes Rufzeichen, das nicht kodierbar ist (nicht
// 3..9 Zeichen aus [A-Z0-9-]). Ein einzelnes Zeichen mit Code 63: kein
// gueltiges Pfadtoken kann dieses Wort ergeben, und es ist != 0, damit die
// Matrix als initialisiert gilt (siehe nbrOwnCallIs()).
#define NBR_CALL_OWN_INVALID ((uint64_t)63)

// Zaehlt jede Aenderung an der Zeilenbelegung (Anlage, Verdraengung,
// Geisterraeumung, Reset). Eine SYM-Zeile, die NACH der Klammer formatiert
// wird, liest ihre Rufzeichen nur dann aus der Matrix, wenn sich seither
// nichts an der Belegung geaendert hat (siehe nbrISymFlush()). Nur mit
// aufgeschobenem Log (nRF52) gebraucht.
#if NBR_DEFER_LOG
static uint32_t s_nbr_gen = 0;
#define NBR_GEN_BUMP() (s_nbr_gen++)
#else
#define NBR_GEN_BUMP() ((void)0)
#endif

// --- kleine Helfer ------------------------------------------------------------

static inline bool nbrIFresh(uint16_t last_min, uint16_t now_min)
{
    return (uint16_t)(now_min - last_min) < NBR_WINDOW_MIN;
}

static inline bool nbrIReady(const NbrMatrix &m)
{
    return m.call[0] != 0;
}

static inline bool nbrIUsed(const NbrMatrix &m, int i)
{
    return (m.row[i].flags & NBR_FLAG_USED) != 0;
}

static inline bool nbrIEdgeLive(const NbrEdge &e)
{
    return e.x != NBR_EDGE_FREE;
}

// SNR wird immer auf [-127,127] begrenzt -- -128 bleibt NBR_SNR_UNKNOWN.
static int8_t nbrClampSnr(int32_t snr)
{
    if (snr > 127)
        return 127;
    if (snr < -127)
        return -127;
    return (int8_t)snr;
}

// --- Rufzeichenwort (Konzept 4.1) ---------------------------------------------

static uint8_t nbrICharCode(char c)
{
    if (c >= '0' && c <= '9')
        return (uint8_t)(1 + (c - '0'));
    if (c >= 'A' && c <= 'Z')
        return (uint8_t)(11 + (c - 'A'));
    if (c == '-')
        return 37;
    return 0;
}

static uint64_t nbrIEncodeN(const char *s, size_t len)
{
    if (!s || len < 3 || len > 9)
        return 0;
    uint64_t w = 0;
    for (size_t i = 0; i < len; i++)
    {
        uint8_t v = nbrICharCode(s[i]);
        if (!v)
            return 0;
        w |= (uint64_t)v << (6 * i);
    }
    return w;
}

static uint64_t nbrIEncode(const char *s)
{
    if (!s)
        return 0;
    size_t n = 0;
    while (n < NBR_CALL_LEN && s[n])
        n++;
    if (n >= NBR_CALL_LEN)
        return 0; // 10 Zeichen oder mehr
    return nbrIEncodeN(s, n);
}

static void nbrIDecode(uint64_t w, char *out)
{
    int n = 0;
    for (; n < NBR_CALL_LEN - 1; n++)
    {
        uint8_t v = (uint8_t)((w >> (6 * n)) & 63u);
        if (!v)
            break;
        out[n] = (v <= 10) ? (char)('0' + v - 1) : (v <= 36) ? (char)('A' + v - 11) : (v == 37) ? '-' : '?';
    }
    out[n] = '\0';
}

// --- Maske als Hex (nbr_mask.h) ---------------------------------------------

static int nbrIMaskHex(const NbrMask &mask, char *out, size_t outlen)
{
    if (!out || outlen == 0)
        return 0;
    char buf[NBR_MASK_HEX_LEN + 1];
    size_t pos = 0;
    for (int i = NBR_MASK_WORDS - 1; i >= 0; i--)
    {
        // %08lX mit unsigned long: auf nRF52 (32 Bit) wie auf dem Host (64 Bit)
        // gueltig; nie %llX (nano-printf gaebe den Buchstaben woertlich aus).
        snprintf(buf + pos, sizeof(buf) - pos, "%08lX%08lX",
                 (unsigned long)(uint32_t)(mask.w[i] >> 32), (unsigned long)(uint32_t)(mask.w[i] & 0xFFFFFFFFu));
        pos += 16;
    }
    buf[NBR_MASK_HEX_LEN] = '\0';
    size_t n = (size_t)NBR_MASK_HEX_LEN < outlen - 1 ? (size_t)NBR_MASK_HEX_LEN : outlen - 1;
    memcpy(out, buf, n);
    out[n] = '\0';
    return (int)n;
}

// --- Position in 0,01 Grad ----------------------------------------------------

static int16_t nbrIDeg16(float v)
{
    if (v != v)
        return (int16_t)NBR_POS_UNKNOWN; // NAN
    float s = v * 100.0f;
    s = (s >= 0.0f) ? floorf(s + 0.5f) : -floorf(-s + 0.5f);
    if (s > 32766.0f)
        s = 32766.0f;
    if (s < -32768.0f)
        s = -32768.0f;
    return (int16_t)s;
}

static bool nbrIPosKnown(const NbrRow &r)
{
    return (r.flags & NBR_FLAG_POS) && r.lat16 != (int16_t)NBR_POS_UNKNOWN && r.lon16 != (int16_t)NBR_POS_UNKNOWN;
}

// --- Log-Helfer (nur AUSSERHALB der Klammer) ----------------------------------

static char nbrLogTypeChar(char type)
{
    return (type == ':') ? 'T' : (type == '!') ? 'P' : 'H';
}

static void nbrLogDrop(uint16_t now_min, const char *reason, const char *path)
{
    if (!nbrLog)
        return;
    char buf[160];
    snprintf(buf, sizeof(buf), "[NBR]|DROP|%u|%s|%s", (unsigned)now_min, reason, path ? path : "");
    nbrLog(buf);
}

static void nbrLogCut(uint16_t now_min, int ntok, int kept, const char *path)
{
    if (!nbrLog)
        return;
    char buf[160];
    snprintf(buf, sizeof(buf), "[NBR]|CUT|%u|%d|%d|%s", (unsigned)now_min, ntok, kept, path ? path : "");
    nbrLog(buf);
}

static void nbrFormatSnrField(char *out, size_t outlen, int8_t snr)
{
    if (snr == NBR_SNR_UNKNOWN)
        snprintf(out, outlen, "NA");
    else
        snprintf(out, outlen, "%d", (int)snr);
}

// EDGE: "<to> hat <from> gehoert", <cnt> ist der EINE Zaehler der Kante nach
// dem Treffer (frueher der Typzaehler), <rssi> immer 0.
static void nbrLogEdge(uint16_t now_min, const char *from, const char *to, char type, uint8_t cnt, int8_t snr)
{
    if (!nbrLog)
        return;
    char snr_buf[8];
    nbrFormatSnrField(snr_buf, sizeof(snr_buf), snr);
    char buf[160];
    snprintf(buf, sizeof(buf), "[NBR]|EDGE|%u|%s|%s|%c|%d|%u|%s",
             (unsigned)now_min, from, to, nbrLogTypeChar(type), 0, (unsigned)cnt, snr_buf);
    nbrLog(buf);
}

static void nbrLogMe(uint16_t now_min, const char *from, char type, int16_t rssi_here, uint8_t cnt, int8_t snr)
{
    if (!nbrLog)
        return;
    char snr_buf[8];
    nbrFormatSnrField(snr_buf, sizeof(snr_buf), snr);
    char buf[160];
    snprintf(buf, sizeof(buf), "[NBR]|ME|%u|%s|%c|%d|%u|%s",
             (unsigned)now_min, from, nbrLogTypeChar(type), (int)rssi_here, (unsigned)cnt, snr_buf);
    nbrLog(buf);
}

static void nbrLogSym(uint16_t now_min, uint32_t msg_id, const char *role,
                      const char *x_call, const char *m_call, int8_t snr)
{
    if (!nbrLog)
        return;
    char buf[160];
    snprintf(buf, sizeof(buf), "[NBR]|SYM|%u|%08X|%s|%s|%s|%d",
             (unsigned)now_min, (unsigned)msg_id, role, x_call, m_call, (int)snr);
    nbrLog(buf);
}

static void nbrLogEvict(uint16_t now_min, int idx, const char *old_call, const char *new_call)
{
    if (!nbrLog)
        return;
    char buf[160];
    snprintf(buf, sizeof(buf), "[NBR]|EVICT|%u|%d|%s|%s", (unsigned)now_min, idx, old_call, new_call);
    nbrLog(buf);
}

// EVICT-E: der Kantenpool war voll, die Kante "<to> hat <from> gehoert" wich.
static void nbrLogEvictEdge(uint16_t now_min, const char *from, const char *to)
{
    if (!nbrLog)
        return;
    char buf[160];
    snprintf(buf, sizeof(buf), "[NBR]|EVICT-E|%u|%s|%s", (unsigned)now_min, from, to);
    nbrLog(buf);
}

// --- Ereignisliste eines Schreibaufrufs ----------------------------------------
//
// nbrNoteFrame()/nbrNoteReport() sammeln unter der Klammer, was sie loggen
// wollen, und geben es danach in derselben Reihenfolge aus. Ein Frame hat
// hoechstens 8 Token: 2 Zeilenverdraengungen, 7 Pfadkanten + ME (je mit
// hoechstens einer Kantenverdraengung), 1 DROP -- 20 Eintraege reichen.
// Liegt auf dem Stack des Aufrufers (LORA-Task, 16 KB auf nRF52).

enum
{
    NBR_EV_EVICT = 1, // a = Zeilenindex, c1 = altes, c2 = neues Rufzeichen
    NBR_EV_EVICTE,    // c1 = from, c2 = to der gewichenen Kante
    NBR_EV_EDGE,      // a/b = Token-Index from/to, cnt, snr
    NBR_EV_ME,        // a = Token-Index, cnt, snr
    NBR_EV_DROPFULL
};

struct NbrEv
{
    uint8_t kind;
    uint8_t a, b;
    uint8_t cnt;
    int8_t  snr;
    char    c1[NBR_CALL_LEN];
    char    c2[NBR_CALL_LEN];
};

#define NBR_EV_MAX 20
struct NbrEvList
{
    int   n;
    NbrEv ev[NBR_EV_MAX];
};

static NbrEv *nbrIEvAdd(NbrEvList *lg, uint8_t kind)
{
    if (!lg || lg->n >= NBR_EV_MAX)
        return NULL;
    NbrEv *e = &lg->ev[lg->n++];
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    return e;
}

// --- Kantenpool (Konzept 4.1) ---------------------------------------------------

static int nbrIEdgeFind(const NbrMatrix &m, int x, int y)
{
    if (x < 0 || y < 0 || x >= NBR_MAX_ROWS || y >= NBR_MAX_ROWS || x == y)
        return -1;
    if (!nbrMaskTest(m.heardBy[x], y))
        return -1; // die Maske ist der schnelle Negativtest: keine Kante (x, y) lebt
    for (int e = 0; e < NBR_MAX_EDGES; e++)
        if (m.edge[e].x == x && m.edge[e].y == y)
            return e;
    return -1;
}

static void nbrIEdgeFree(NbrMatrix &m, int e)
{
    NbrEdge &ed = m.edge[e];
    if (nbrIEdgeLive(ed))
    {
        nbrMaskClear(m.hears[ed.y], ed.x);
        nbrMaskClear(m.heardBy[ed.x], ed.y);
    }
    ed.x = ed.y = NBR_EDGE_FREE;
    ed.cnt = 0;
    ed.snr = NBR_SNR_UNKNOWN;
    ed.last_min = 0;
}

// Neue Kante (x, y) mit cnt 0 und unbekanntem SNR. Ist der Pool voll, weicht
// die aelteste Kante, die weder Spalte 0 noch Zeile 0 beruehrt, erst danach
// die aelteste ueberhaupt (Konzept 4.1); Gleichstand: kleinster Index.
static int nbrIEdgeAlloc(NbrMatrix &m, int x, int y, uint16_t now_min, NbrEvList *lg)
{
    int slot = -1;
    for (int e = 0; e < NBR_MAX_EDGES; e++)
        if (!nbrIEdgeLive(m.edge[e]))
        {
            slot = e;
            break;
        }
    if (slot < 0)
    {
        for (int pass = 0; pass < 2 && slot < 0; pass++)
        {
            uint16_t oldest_age = 0;
            for (int e = 0; e < NBR_MAX_EDGES; e++)
            {
                const NbrEdge &ed = m.edge[e];
                if (pass == 0 && (ed.x == 0 || ed.y == 0))
                    continue;
                uint16_t age = (uint16_t)(now_min - ed.last_min);
                if (slot < 0 || age > oldest_age)
                {
                    slot = e;
                    oldest_age = age;
                }
            }
        }
        NbrEv *ev = nbrIEvAdd(lg, NBR_EV_EVICTE);
        if (ev)
        {
            nbrIDecode(m.call[m.edge[slot].x], ev->c1);
            nbrIDecode(m.call[m.edge[slot].y], ev->c2);
        }
        nbrIEdgeFree(m, slot);
    }
    NbrEdge &ed = m.edge[slot];
    ed.x = (uint8_t)x;
    ed.y = (uint8_t)y;
    ed.cnt = 0;
    ed.snr = NBR_SNR_UNKNOWN;
    ed.last_min = now_min;
    nbrMaskSet(m.hears[y], x);
    nbrMaskSet(m.heardBy[x], y);
    return slot;
}

// Ein Treffer "y hat x gehoert". Eine Kante, deren last_min beim Treffer
// bereits verfallen ist, faengt bei cnt 0 und unbekanntem SNR neu an
// (Konzept 4.2) -- dasselbe Ergebnis, das eine vom Sweep schon freigegebene
// und jetzt neu angelegte Kante haette. Liefert den Kantenindex, -1 fuer die
// Diagonale.
static int nbrIEdgeHit(NbrMatrix &m, int x, int y, uint16_t now_min, NbrEvList *lg)
{
    if (x == y || x < 0 || y < 0)
        return -1;
    int e = nbrIEdgeFind(m, x, y);
    if (e < 0)
        e = nbrIEdgeAlloc(m, x, y, now_min, lg);
    else if (!nbrIFresh(m.edge[e].last_min, now_min))
    {
        m.edge[e].cnt = 0;
        m.edge[e].snr = NBR_SNR_UNKNOWN;
    }
    if (m.edge[e].cnt < 255)
        m.edge[e].cnt++;
    m.edge[e].last_min = now_min;
    return e;
}

static bool nbrIEdgeFresh(const NbrMatrix &m, int x, int y, uint16_t now_min)
{
    int e = nbrIEdgeFind(m, x, y);
    return e >= 0 && nbrIFresh(m.edge[e].last_min, now_min);
}

// ME-Messwert fuer die Kante (x, ich): ganzzahliges gleitendes Mittel ueber
// NBR_SNR_AVG_N Rahmen (Konzept 4.1/4.8). Der erste Wert (oder der erste nach
// einem Neubeginn, SNR unbekannt) ist der Wert selbst; danach zaehlt n =
// min(cnt, NBR_SNR_AVG_N) -- cnt der Kante (x, 0) zaehlt genau die ME-Rahmen,
// eine Halbierung verkuerzt das Gedaechtnis entsprechend. Der Schritt
// (Messwert - Mittel) / n wird gerundet und ist mindestens 1 dB in Richtung
// des Messwerts, sonst bliebe das ganzzahlige Mittel bis zu n/2 dB vor einem
// gleichbleibenden Wert stehen. NBR_SNR_AVG_N == 1: immer der letzte Wert
// (bisheriges Verhalten).
static void nbrISnrSample(NbrEdge &e, int8_t sample)
{
    if (NBR_SNR_AVG_N <= 1 || e.snr == NBR_SNR_UNKNOWN)
    {
        e.snr = sample;
        return;
    }
    int n = (e.cnt < NBR_SNR_AVG_N) ? (int)e.cnt : (int)NBR_SNR_AVG_N;
    if (n <= 1)
    {
        e.snr = sample;
        return;
    }
    int d = (int)sample - (int)e.snr;
    int step = (d >= 0) ? (d + n / 2) / n : -((-d + n / 2) / n);
    if (step == 0 && d != 0)
        step = (d > 0) ? 1 : -1;
    e.snr = nbrClampSnr((int32_t)e.snr + step);
}

// --- Zeilen ----------------------------------------------------------------------

static int nbrIFindWord(const NbrMatrix &m, uint64_t w)
{
    if (w == 0)
        return -1;
    if (w == m.call[0])
        return 0;
    for (int i = 1; i < NBR_MAX_ROWS; i++)
        if (m.call[i] == w && nbrIUsed(m, i))
            return i;
    return -1;
}

static void nbrIRowBlank(NbrMatrix &m, int idx)
{
    memset(&m.row[idx], 0, sizeof(NbrRow));
    m.row[idx].lat16 = (int16_t)NBR_POS_UNKNOWN;
    m.row[idx].lon16 = (int16_t)NBR_POS_UNKNOWN;
    m.row[idx].ext = 0xFF;
}

// Zeile idx vollstaendig leeren: ihre Kanten frei (je zwei Maskenbits), ihr
// Bit in jeder Maske geloescht, Rufzeichen 0 -- ein Durchlauf ueber den Pool
// (Konzept 4.1). Nie fuer Zeile 0.
static void nbrIRowClear(NbrMatrix &m, int idx)
{
    for (int e = 0; e < NBR_MAX_EDGES; e++)
        if (nbrIEdgeLive(m.edge[e]) && (m.edge[e].x == idx || m.edge[e].y == idx))
            nbrIEdgeFree(m, e);
    for (int r = 0; r < NBR_MAX_ROWS; r++)
    {
        nbrMaskClear(m.hears[r], idx);
        nbrMaskClear(m.heardBy[r], idx);
    }
    m.hears[idx] = nbrMaskNone();
    m.heardBy[idx] = nbrMaskNone();
    nbrIRowBlank(m, idx);
    m.call[idx] = 0;
    NBR_GEN_BUMP();
}

// Liest-ONLY, welchen Index ein Touch fuer w waehlen wuerde (frueher
// nbrPlanRow(), unveraendert): vorhandene Zeile, sonst die erste freie, sonst
// die mit der groessten Altersluecke -- zuerst unter den Zeilen, die ich NICHT
// frisch direkt hoere, erst dann unter allen --, nie ein Index aus prot.
static int nbrIPlanRow(const NbrMatrix &m, uint64_t w, uint16_t now_min, const NbrMask &prot)
{
    if (w == m.call[0])
        return 0;
    for (int i = 1; i < NBR_MAX_ROWS; i++)
        if (nbrIUsed(m, i) && m.call[i] == w)
            return i;
    for (int i = 1; i < NBR_MAX_ROWS; i++)
        if (!nbrMaskTest(prot, i) && !nbrIUsed(m, i))
            return i;
    // Frisch direkt gehoert, ein Durchlauf ueber den Pool statt einer
    // Kantensuche je Zeile (die Klammer soll kurz bleiben).
    NbrMask fresh_direct = nbrMaskNone();
    for (int e = 0; e < NBR_MAX_EDGES; e++)
    {
        const NbrEdge &ed = m.edge[e];
        if (nbrIEdgeLive(ed) && ed.y == 0 && nbrIFresh(ed.last_min, now_min))
            nbrMaskSet(fresh_direct, ed.x);
    }
    for (int pass = 0; pass < 2; pass++)
    {
        int oldest = -1;
        uint16_t oldest_age = 0;
        for (int i = 1; i < NBR_MAX_ROWS; i++)
        {
            if (nbrMaskTest(prot, i))
                continue;
            if (pass == 0 && nbrMaskTest(fresh_direct, i))
                continue; // frisch direkt gehoert: erst im zweiten Durchgang verdraengbar
            uint16_t age = (uint16_t)(now_min - m.row[i].last_min);
            if (oldest < 0 || age > oldest_age)
            {
                oldest = i;
                oldest_age = age;
            }
        }
        if (oldest >= 0)
            return oldest;
    }
    return -1;
}

// Setzt einen geplanten Index um: nichts fuer Zeile 0 oder einen passenden
// Fund, sonst leeren (Verdraengung loggt EVICT) und neu anlegen.
static void nbrICommitRow(NbrMatrix &m, int idx, uint64_t w, const char *call, uint16_t now_min, NbrEvList *lg)
{
    if (idx == 0)
        return;
    if (nbrIUsed(m, idx) && m.call[idx] == w)
        return;
    if (nbrIUsed(m, idx))
    {
        NbrEv *ev = nbrIEvAdd(lg, NBR_EV_EVICT);
        if (ev)
        {
            ev->a = (uint8_t)idx;
            nbrIDecode(m.call[idx], ev->c1);
            strncpy(ev->c2, call, NBR_CALL_LEN - 1);
            ev->c2[NBR_CALL_LEN - 1] = '\0';
        }
    }
    nbrIRowClear(m, idx);
    m.call[idx] = w;
    m.row[idx].flags = NBR_FLAG_USED;
    m.row[idx].last_min = now_min;
}

static void nbrIInitAll(NbrMatrix &m, uint64_t own, uint16_t now_min)
{
    memset(&m, 0, sizeof(NbrMatrix));
    for (int i = 0; i < NBR_MAX_ROWS; i++)
        nbrIRowBlank(m, i);
    for (int e = 0; e < NBR_MAX_EDGES; e++)
    {
        m.edge[e].x = m.edge[e].y = NBR_EDGE_FREE;
        m.edge[e].snr = NBR_SNR_UNKNOWN;
    }
    m.call[0] = own;
    m.boot_min = now_min;
    m.last_sweep = now_min;
    m.last_halve = now_min;
    NBR_GEN_BUMP();
}

// --- Pfad- und Berichtsparser (unveraendert) --------------------------------------

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

static bool nbrParseInt(const char *s, size_t len, long *out)
{
    bool neg = (len > 0 && s[0] == '-');
    long digits = nbrParseUint(s + (neg ? 1 : 0), len - (neg ? 1 : 0));
    if (digits < 0)
        return false;
    *out = neg ? -digits : digits;
    return true;
}

// HEY-Gruppe "NCT,RSSI,SNR": das dritte Feld wird der SNR der Kante (x, y),
// aber nur wenn diese Kante lebt. Die Paarkanten dieses Frames sind vorher
// getroffen worden; einzig (x, 0) -- mein Rufzeichen mitten im Pfad -- kann
// ohne Kante sein, und dort gab es auch in der dichten Matrix keine
// Beobachtung (nur einen SNR in einer leeren Zelle, den der naechste
// ME-Treffer ohnehin ueberschrieb).
static void nbrIApplyGroup(NbrMatrix &m, int row_x, int row_y, const char *g, size_t len)
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
            return;
    }
    if (!comma1 || !comma2)
        return;
    const char *snr_start = comma2 + 1;
    size_t snr_len = (size_t)((g + len) - snr_start);
    long snr;
    if (!nbrParseInt(snr_start, snr_len, &snr))
        return;
    // Kante (x, 0) gehoert dem ME-Schritt: die Gruppe ist mein eigener,
    // frueherer Empfang von x, der als Echo zurueckkommt. Mit laufendem
    // SNR-Mittel (NBR_SNR_AVG_N > 1) wuerde ein alter Einzelwert das Mittel
    // ueberschreiben (Advisor Welle 2, Befund A). Mit N == 1 bleibt das
    // Verhalten der dichten Matrix, damit der Differenzialtest exakt bleibt.
    if (row_y == 0 && NBR_SNR_AVG_N > 1)
        return;
    int e = nbrIEdgeFind(m, row_x, row_y);
    if (e >= 0)
        m.edge[e].snr = nbrClampSnr((int32_t)snr);
}

static void nbrIApplyHeyGroups(NbrMatrix &m, const int *row_idx, int ntok, const char *payload)
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
            nbrIApplyGroup(m, row_idx[gi - 1], row_idx[gi], start, len);
        if (*p == ';')
            p++;
        gi++;
    }
}

// --- nbrNoteFrame(), innerer Teil (unter der Klammer) ------------------------------

static int nbrINoteFrame(NbrMatrix &m, char tokens[][NBR_CALL_LEN], const uint64_t *words, int ntok,
                         char type, const char *payload, bool dest_gw, int8_t snr_here, uint16_t now_min,
                         NbrEvList *lg)
{
    // Text: nur der ME-Schritt (siehe nbr_matrix.h).
    if (type == ':')
    {
        int last_idx = nbrIFindWord(m, words[ntok - 1]);
        if (last_idx < 0)
        {
            last_idx = nbrIPlanRow(m, words[ntok - 1], now_min, nbrMaskNone());
            if (last_idx < 0)
            {
                nbrIEvAdd(lg, NBR_EV_DROPFULL);
                return -3;
            }
            nbrICommitRow(m, last_idx, words[ntok - 1], tokens[ntok - 1], now_min, lg);
        }
        if (words[ntok - 1] == m.call[0])
            return 0; // eigenes Echo

        int e = nbrIEdgeHit(m, last_idx, 0, now_min, lg);
        nbrISnrSample(m.edge[e], nbrClampSnr(snr_here));
        m.row[last_idx].last_min = now_min;
        m.row[0].last_min = now_min;
        NbrEv *ev = nbrIEvAdd(lg, NBR_EV_ME);
        if (ev)
        {
            ev->a = (uint8_t)(ntok - 1);
            ev->cnt = m.edge[e].cnt;
            ev->snr = m.edge[e].snr;
        }
        return 1;
    }

    // 2-Hop-Fenster fuer '!'/'@'.
    int start = (ntok > 2) ? ntok - 2 : 0;
    int row_idx[8];
    for (int i = 0; i < 8; i++)
        row_idx[i] = -1;

    // Erst bestehende Fenster-Zeilen aufloesen und schuetzen (M2), dann die
    // unbekannten lesend planen (M1); scheitert eine Planung, ist noch nichts
    // angefasst.
    NbrMask prot = nbrMaskNone();
    for (int i = start; i < ntok; i++)
    {
        int idx = nbrIFindWord(m, words[i]);
        if (idx < 0)
            continue;
        row_idx[i] = idx;
        nbrMaskSet(prot, idx);
    }
    for (int i = start; i < ntok; i++)
    {
        if (row_idx[i] >= 0)
            continue;
        int idx = nbrIPlanRow(m, words[i], now_min, prot);
        if (idx < 0)
        {
            nbrIEvAdd(lg, NBR_EV_DROPFULL);
            return -3;
        }
        row_idx[i] = idx;
        nbrMaskSet(prot, idx);
    }
    for (int i = start; i < ntok; i++)
        nbrICommitRow(m, row_idx[i], words[i], tokens[i], now_min, lg);

    // Regel 3: Token vor dem Fenster nur lesend.
    for (int i = 0; i < start; i++)
        row_idx[i] = nbrIFindWord(m, words[i]);

    int hits = 0;
    for (int i = 0; i + 1 < ntok; i++)
    {
        int x = row_idx[i], y = row_idx[i + 1];
        if (x < 0 || y < 0)
            continue;
        if (y == 0)
            continue; // Spalte 0 schreibt nur der ME-Schritt
        int e = nbrIEdgeHit(m, x, y, now_min, lg);
        if (e < 0)
            continue;
        if (i >= start)
        {
            m.row[x].last_min = now_min;
            m.row[y].last_min = now_min;
        }
        NbrEv *ev = nbrIEvAdd(lg, NBR_EV_EDGE);
        if (ev)
        {
            ev->a = (uint8_t)i;
            ev->b = (uint8_t)(i + 1);
            ev->cnt = m.edge[e].cnt;
            ev->snr = m.edge[e].snr;
        }
        hits++;
    }

    if (words[ntok - 1] != m.call[0])
    {
        int last = row_idx[ntok - 1];
        int e = nbrIEdgeHit(m, last, 0, now_min, lg);
        if (e >= 0)
        {
            nbrISnrSample(m.edge[e], nbrClampSnr(snr_here));
            m.row[last].last_min = now_min;
            m.row[0].last_min = now_min;
            NbrEv *ev = nbrIEvAdd(lg, NBR_EV_ME);
            if (ev)
            {
                ev->a = (uint8_t)(ntok - 1);
                ev->cnt = m.edge[e].cnt;
                ev->snr = m.edge[e].snr;
            }
            hits++;
        }
    }

    if (type == '@')
    {
        int sender = row_idx[0];
        if (dest_gw && sender >= 0)
            m.row[sender].flags |= NBR_FLAG_GW;
        nbrIApplyHeyGroups(m, row_idx, ntok, payload);
    }
    return hits;
}

// --- Kontext der Urteile: Direkt-Maske und staerkster direkter Hoerer --------------
//
// Anteilsregel (nbr_matrix.h, Konzept 4.3): die Kante (a, b) zaehlt als
// Deckung, wenn sie lebt, frisch ist und cnt(a, b) mindestens NBR_SHARE_PCT
// Prozent von maxd[a] erreicht, dem groessten frischen cnt(a, D) ueber alle
// direkten Nachbarn D (ohne mich). NBR_SHARE_PCT == 0: jede frische Kante.
//
// Liegt auf dem Stack des Aufrufers (2 + 8 * NBR_MASK_WORDS + NBR_MAX_ROWS
// Byte, rund 150 Byte bei 128 Zeilen) und lebt nur fuer einen Aufruf.

struct NbrCtx
{
    uint16_t now;
    NbrMask  direct;
#if NBR_SHARE_PCT > 0
    uint8_t  maxd[NBR_MAX_ROWS];
#endif
};

static const NbrCtx &nbrICtx(const NbrMatrix &m, uint16_t now_min, NbrCtx &c)
{
    c.now = now_min;
    c.direct = nbrMaskNone();
    for (int e = 0; e < NBR_MAX_EDGES; e++)
    {
        const NbrEdge &ed = m.edge[e];
        if (nbrIEdgeLive(ed) && ed.y == 0 && ed.x != 0 && nbrIUsed(m, ed.x) && nbrIFresh(ed.last_min, now_min))
            nbrMaskSet(c.direct, ed.x);
    }
#if NBR_SHARE_PCT > 0
    memset(c.maxd, 0, sizeof(c.maxd));
    for (int e = 0; e < NBR_MAX_EDGES; e++)
    {
        const NbrEdge &ed = m.edge[e];
        if (nbrIEdgeLive(ed) && nbrMaskTest(c.direct, ed.y) && nbrIFresh(ed.last_min, now_min) && ed.cnt > c.maxd[ed.x])
            c.maxd[ed.x] = ed.cnt;
    }
#endif
    return c;
}

static inline bool nbrIShareOk(const NbrCtx &c, const NbrEdge &ed)
{
#if NBR_SHARE_PCT > 0
    return (uint32_t)ed.cnt * 100u >= (uint32_t)NBR_SHARE_PCT * (uint32_t)c.maxd[ed.x];
#else
    (void)c;
    (void)ed;
    return true;
#endif
}

// Frische, lebende Kante (a, b) mit Anteil.
static inline bool nbrICovers(const NbrMatrix &m, const NbrCtx &c, const NbrEdge &ed)
{
    (void)m;
    return nbrIEdgeLive(ed) && nbrIFresh(ed.last_min, c.now) && nbrIShareOk(c, ed);
}

// Hoerer von row (ohne Anteil): Y != row, Y != 0, belegt, frische Kante (row, Y).
static NbrMask nbrIHearersMask(const NbrMatrix &m, int row, uint16_t now_min)
{
    NbrMask r = nbrMaskNone();
    if (row < 0 || row >= NBR_MAX_ROWS)
        return r;
    for (int e = 0; e < NBR_MAX_EDGES; e++)
    {
        const NbrEdge &ed = m.edge[e];
        if (nbrIEdgeLive(ed) && ed.x == row && ed.y != 0 && nbrIUsed(m, ed.y) && nbrIFresh(ed.last_min, now_min))
            nbrMaskSet(r, ed.y);
    }
    return r;
}

// Hoerer inklusive Zeile 0 (frueher nbrHearers()): alle Y != row mit frischer
// Kante (row, Y).
static NbrMask nbrIHearersAll(const NbrMatrix &m, int row, uint16_t now_min)
{
    NbrMask r = nbrMaskNone();
    if (row < 0 || row >= NBR_MAX_ROWS)
        return r;
    for (int e = 0; e < NBR_MAX_EDGES; e++)
    {
        const NbrEdge &ed = m.edge[e];
        if (nbrIEdgeLive(ed) && ed.x == row && nbrIFresh(ed.last_min, now_min))
            nbrMaskSet(r, ed.y);
    }
    return r;
}

static NbrMask nbrIHeardMeMask(const NbrMatrix &m, uint16_t now_min)
{
    NbrMask r = nbrMaskNone();
    for (int e = 0; e < NBR_MAX_EDGES; e++)
    {
        const NbrEdge &ed = m.edge[e];
        if (nbrIEdgeLive(ed) && ed.x == 0 && ed.y != 0 && nbrIUsed(m, ed.y) && nbrIFresh(ed.last_min, now_min))
            nbrMaskSet(r, ed.y);
    }
    return r;
}

// Zeilen, die irgendein Y != 0 mit Anteil hoert (frueher: nicht
// nbrRowExclusive()).
static NbrMask nbrICoveredByAnyone(const NbrMatrix &m, const NbrCtx &c)
{
    NbrMask r = nbrMaskNone();
    for (int e = 0; e < NBR_MAX_EDGES; e++)
    {
        const NbrEdge &ed = m.edge[e];
        if (ed.y != 0 && nbrICovers(m, c, ed))
            nbrMaskSet(r, ed.x);
    }
    return r;
}

// E_self: direkte Zeilen, die kein ANDERER direkter Nachbar mit Anteil hoert.
static NbrMask nbrIExclusiveDirect(const NbrMatrix &m, const NbrCtx &c)
{
    NbrMask cov = nbrMaskNone();
    for (int e = 0; e < NBR_MAX_EDGES; e++)
    {
        const NbrEdge &ed = m.edge[e];
        if (nbrIEdgeLive(ed) && nbrMaskTest(c.direct, ed.x) && nbrMaskTest(c.direct, ed.y) && nbrICovers(m, c, ed))
            nbrMaskSet(cov, ed.x);
    }
    return nbrMaskAndNot(c.direct, cov);
}

// #X-Menge von row (Konzept 4.3): X, die row mit Anteil hoert, die ich nicht
// direkt hoere und die kein anderer direkter Nachbar mit Anteil hoert.
// Liefert -1 ("NA") fuer Zeile 0 und nicht direkte Zeilen.
static int nbrIMeshNeedSet(const NbrMatrix &m, const NbrCtx &c, int row, NbrMask *out)
{
    if (out)
        *out = nbrMaskNone();
    if (row <= 0 || row >= NBR_MAX_ROWS)
        return -1;
    if (!nbrMaskTest(c.direct, row))
        return -1;
    NbrMask rowcov = nbrMaskNone(), peercov = nbrMaskNone();
    for (int e = 0; e < NBR_MAX_EDGES; e++)
    {
        const NbrEdge &ed = m.edge[e];
        if (!nbrIEdgeLive(ed) || ed.x == 0 || ed.x == row)
            continue;
        if (!nbrICovers(m, c, ed))
            continue;
        if (ed.y == row)
            nbrMaskSet(rowcov, ed.x);
        else if (nbrMaskTest(c.direct, ed.y))
            nbrMaskSet(peercov, ed.x);
    }
    NbrMask r = nbrMaskAndNot(nbrMaskAndNot(rowcov, c.direct), peercov);
    if (out)
        *out = r;
    return nbrMaskCount(r);
}

static const char *nbrIRowVerdict(const NbrMatrix &m, const NbrCtx &c, int x, const NbrMask &covered_any)
{
    if (x == 0)
        return "UNK";
    if (!nbrMaskTest(c.direct, x))
        return nbrMaskEmpty(nbrIHearersAll(m, x, c.now)) ? "UNK" : "LEAF";
    return nbrMaskTest(covered_any, x) ? "RED" : "EXCL";
}

static const char *nbrIMeshNeedWord(int n)
{
    if (n < 0)
        return "NA";
    return n > 0 ? "MESH" : "RED";
}

// --- Symmetrie-Annahme ------------------------------------------------------------

static bool nbrIHearsSym(const NbrMatrix &m, int x, int mrow, uint16_t now_min, bool sym,
                         int8_t *snr_used, bool *inferred, bool *vetoed)
{
    *inferred = false;
    *vetoed = false;
    if (nbrIEdgeFresh(m, mrow, x, now_min)) // "X hat M gehoert", beobachtet
        return true;
    if (!sym || x == mrow)
        return false;
    int e = nbrIEdgeFind(m, x, mrow); // "M hat X gehoert"
    if (e < 0 || !nbrIFresh(m.edge[e].last_min, now_min))
        return false;
    int8_t snr = m.edge[e].snr;
    if (snr == NBR_SNR_UNKNOWN || snr < NBR_SYM_MIN_SNR)
        return false;
    const NbrRow &rx = m.row[x];
    *snr_used = snr;
    if ((rx.flags & NBR_FLAG_RPT) && (uint16_t)(now_min - rx.rpt_min) < NBR_REPORT_VALID_MIN)
    {
        *vetoed = true;
        return false;
    }
    *inferred = true;
    return true;
}

// SYM-Zeilen. Mit NBR_DEFER_LOG (nRF52) unter der Klammer nur als 4-Byte-Satz
// gemerkt und nach der Klammer formatiert: statisch, 96 Saetze = 384 Byte.
// Belegt ihn ein anderer Task gerade oder laeuft er ueber, gehen die Zeilen
// verloren -- gezaehlt und als "[NBR]|DROP|<up>|SYMBUF|<n>" gemeldet. Ohne
// NBR_DEFER_LOG geht jede Zeile sofort hinaus (die Klammer ist dort leer).
enum
{
    NBR_SYM_HASF = 1,
    NBR_SYM_ALT,
    NBR_SYM_COVER,
    NBR_SYM_VETO
};
#define NBR_SYM_TOKEN 0x80 // im Rollenbyte: m ist ein Pfad-Token-Index, kein Zeilenindex

struct NbrSymSink
{
    bool              on;     // Log an
    const NbrMatrix  *m;
    char            (*tokens)[NBR_CALL_LEN]; // Pfad-Token (nbrRelayNeed) oder NULL
    uint16_t          now;
    uint32_t          msg_id;
#if NBR_DEFER_LOG
    bool              owner;  // dieser Aufruf haelt s_sym
    uint16_t          lost;
    uint32_t          gen;
#endif
};

// Eine SYM-Zeile formatieren und ausgeben. names: Zeilenindizes duerfen aus
// der Matrix gelesen werden (sonst "?<idx>", siehe nbrISymFlush()).
static void nbrISymEmit(const NbrSymSink &s, uint8_t role, int x, int mref, int8_t snr, uint64_t wx, uint64_t wm,
                        bool names)
{
    bool tok = (role & NBR_SYM_TOKEN) != 0;
    int role_id = role & 0x7F;
    char xc[NBR_CALL_LEN], mc[NBR_CALL_LEN];
    if (names)
        nbrIDecode(wx, xc);
    else
        snprintf(xc, sizeof(xc), "?%u", (unsigned)x);
    if (tok)
    {
        strncpy(mc, s.tokens ? s.tokens[mref] : "?", NBR_CALL_LEN - 1);
        mc[NBR_CALL_LEN - 1] = '\0';
    }
    else if (names)
        nbrIDecode(wm, mc);
    else
        snprintf(mc, sizeof(mc), "?%u", (unsigned)mref);
    const char *r = (role_id == NBR_SYM_HASF) ? "HASF" : (role_id == NBR_SYM_ALT) ? "ALT"
                  : (role_id == NBR_SYM_COVER) ? "COVER" : "VETO";
    nbrLogSym(s.now, s.msg_id, r, xc, mc, snr);
}

#if NBR_DEFER_LOG
struct NbrSymRec
{
    uint8_t role;
    uint8_t x;
    uint8_t m;
    int8_t  snr;
};
#define NBR_SYM_REC_MAX 96
static_assert(sizeof(NbrSymRec) * NBR_SYM_REC_MAX <= 384, "SYM-Puffer ueber 384 Byte");
static NbrSymRec s_sym[NBR_SYM_REC_MAX];
static uint8_t   s_sym_n = 0;
static bool      s_sym_busy = false;
#endif

// Unter der Klammer.
static void nbrISymBegin(NbrSymSink &s, const NbrMatrix &m, char (*tokens)[NBR_CALL_LEN], uint16_t now_min,
                         uint32_t msg_id)
{
    s.on = (nbrLog != NULL);
    s.m = &m;
    s.tokens = tokens;
    s.now = now_min;
    s.msg_id = msg_id;
#if NBR_DEFER_LOG
    s.owner = false;
    s.lost = 0;
    s.gen = s_nbr_gen;
    if (s.on && !s_sym_busy)
    {
        s_sym_busy = true;
        s.owner = true;
        s_sym_n = 0;
    }
#endif
}

// Unter der Klammer (die ohne NBR_DEFER_LOG leer ist).
static void nbrISymAdd(NbrSymSink &s, uint8_t role, int x, int mref, int8_t snr)
{
    if (!s.on)
        return;
#if NBR_DEFER_LOG
    if (!s.owner || s_sym_n >= NBR_SYM_REC_MAX)
    {
        s.lost++;
        return;
    }
    NbrSymRec &r = s_sym[s_sym_n++];
    r.role = role; // NBR_SYM_TOKEN bleibt im Rollenbyte, m ist ein voller Zeilenindex (bis 255)
    r.x = (uint8_t)x;
    r.m = (uint8_t)mref;
    r.snr = snr;
#else
    bool tok = (role & NBR_SYM_TOKEN) != 0;
    nbrISymEmit(s, role, x, mref, snr, s.m->call[x], tok ? 0 : s.m->call[mref], true);
#endif
}

// Nach der Klammer: aufgeschobene SYM-Zeilen ausgeben, Puffer freigeben. Die
// Rufzeichen je Zeile unter einer kurzen Klammer; hat sich die
// Zeilenbelegung seit der Rechnung geaendert (s_nbr_gen), steht "?<idx>"
// statt eines womoeglich falschen Namens. Ohne NBR_DEFER_LOG: nichts zu tun.
static void nbrISymFlush(NbrSymSink &s)
{
#if NBR_DEFER_LOG
    if (!s.on)
        return;
    if (s.owner)
    {
        for (int i = 0; i < (int)s_sym_n; i++)
        {
            const NbrSymRec &r = s_sym[i];
            bool tok = (r.role & NBR_SYM_TOKEN) != 0;
            uint64_t wx = 0, wm = 0;
            bool same;
            NBR_LOCK();
            same = (s_nbr_gen == s.gen);
            if (same)
            {
                wx = s.m->call[r.x];
                if (!tok)
                    wm = s.m->call[r.m];
            }
            NBR_UNLOCK();
            nbrISymEmit(s, r.role, r.x, r.m, r.snr, wx, wm, same);
        }
        s_sym_n = 0;
        s_sym_busy = false;
    }
    if (s.lost)
    {
        char n[12];
        snprintf(n, sizeof(n), "%u", (unsigned)s.lost);
        nbrLogDrop(s.now, "SYMBUF", n);
    }
#else
    (void)s;
#endif
}

// --- Relay-Entscheidung, innerer Teil ----------------------------------------------

static NbrNeed nbrIRelayNeed(const NbrMatrix &m, const uint64_t *words, int ntok, uint16_t now_min, bool sym,
                             NbrSymSink &sink)
{
    NbrNeed r;
    r.need = nbrMaskNone();
    r.alone = nbrMaskNone();
    r.known = false;
    r.inferred = nbrMaskNone();
    if (!nbrIReady(m))
        return r;

    int pidx[8];
    NbrMask inpath = nbrMaskNone();
    for (int i = 0; i < ntok; i++)
    {
        pidx[i] = nbrIFindWord(m, words[i]);
        nbrMaskSet(inpath, pidx[i]);
    }
    // HatF = Pfad | Hoerer der Pfadteilnehmer (blosse Kante, keine Anteilsregel).
    NbrMask hasf = inpath;
    for (int e = 0; e < NBR_MAX_EDGES; e++)
    {
        const NbrEdge &ed = m.edge[e];
        if (nbrIEdgeLive(ed) && nbrMaskTest(inpath, ed.x) && ed.y != 0 && nbrIUsed(m, ed.y) &&
            nbrIFresh(ed.last_min, now_min))
            nbrMaskSet(hasf, ed.y);
    }

    NbrCtx cbuf;
    const NbrCtx &c = nbrICtx(m, now_min, cbuf);
    NbrMask dep = nbrMaskOr(c.direct, nbrIHeardMeMask(m, now_min));
    nbrMaskClear(dep, 0);
    for (int x = nbrMaskNext(dep, -1); x >= 0; x = nbrMaskNext(dep, x))
        if (m.row[x].flags & NBR_FLAG_GW)
            nbrMaskClear(dep, x);
    if (nbrMaskEmpty(dep))
        return r;
    r.known = true;

    if (sym)
    {
        for (int x = nbrMaskNext(dep, -1); x >= 0; x = nbrMaskNext(dep, x))
        {
            if (nbrMaskTest(hasf, x))
                continue;
            for (int i = 0; i < ntok; i++)
            {
                if (pidx[i] < 0)
                    continue;
                int8_t snr_used = 0;
                bool inferred = false, vetoed = false;
                if (nbrIHearsSym(m, x, pidx[i], now_min, sym, &snr_used, &inferred, &vetoed) && inferred)
                {
                    nbrMaskSet(hasf, x);
                    nbrMaskSet(r.inferred, x);
                    nbrISymAdd(sink, NBR_SYM_HASF | NBR_SYM_TOKEN, x, i, snr_used);
                    break;
                }
                if (vetoed)
                {
                    nbrISymAdd(sink, NBR_SYM_VETO | NBR_SYM_TOKEN, x, i, snr_used);
                    break;
                }
            }
        }
    }

    r.need = nbrMaskAndNot(dep, hasf);

    // Allein: kein Versorger M (direkt, hat den Frame beobachtet), dessen
    // Kante (M, X) -- "X hat M gehoert" -- mit Anteil lebt.
    NbrMask providers = nbrMaskAndNot(nbrMaskAnd(c.direct, hasf), r.inferred);
    NbrMask altobs = nbrMaskNone();
    for (int e = 0; e < NBR_MAX_EDGES; e++)
    {
        const NbrEdge &ed = m.edge[e];
        if (nbrIEdgeLive(ed) && nbrMaskTest(providers, ed.x) && nbrMaskTest(r.need, ed.y) && nbrICovers(m, c, ed))
            nbrMaskSet(altobs, ed.y);
    }
    for (int x = nbrMaskNext(r.need, -1); x >= 0; x = nbrMaskNext(r.need, x))
    {
        bool alt = nbrMaskTest(altobs, x);
        if (!alt && sym)
        {
            for (int mrow = nbrMaskNext(providers, -1); mrow >= 0 && !alt; mrow = nbrMaskNext(providers, mrow))
            {
                if (mrow == x)
                    continue;
                int8_t snr_used = 0;
                bool inferred = false, vetoed = false;
                if (nbrIHearsSym(m, x, mrow, now_min, sym, &snr_used, &inferred, &vetoed) && inferred)
                {
                    alt = true;
                    nbrMaskSet(r.inferred, x);
                    nbrISymAdd(sink, NBR_SYM_ALT, x, mrow, snr_used);
                }
                else if (vetoed)
                {
                    nbrISymAdd(sink, NBR_SYM_VETO, x, mrow, snr_used);
                    break;
                }
            }
        }
        if (!alt)
            nbrMaskSet(r.alone, x);
    }
    return r;
}

static NbrMask nbrICoverMask(const NbrMatrix &m, uint64_t relayer, uint16_t now_min, bool sym,
                             const NbrMask &relevant, NbrMask *inferred_out, NbrSymSink &sink)
{
    NbrMask mask = nbrMaskNone();
    if (inferred_out)
        *inferred_out = nbrMaskNone();
    if (!nbrIReady(m))
        return mask;
    int idx = nbrIFindWord(m, relayer);
    if (idx <= 0)
        return mask;

    NbrCtx cbuf;
    const NbrCtx &c = nbrICtx(m, now_min, cbuf);
    for (int e = 0; e < NBR_MAX_EDGES; e++)
    {
        const NbrEdge &ed = m.edge[e];
        if (nbrIEdgeLive(ed) && ed.x == idx && ed.y != 0 && nbrIUsed(m, ed.y) && nbrICovers(m, c, ed))
            nbrMaskSet(mask, ed.y);
    }
    if (!sym)
        return mask;

    NbrMask local_inferred = nbrMaskNone();
    for (int x = 1; x < NBR_MAX_ROWS; x++)
    {
        if (x == idx || nbrMaskTest(mask, x) || !nbrIUsed(m, x))
            continue;
        int8_t snr_used = 0;
        bool was_inferred = false, was_vetoed = false;
        if (nbrIHearsSym(m, x, idx, now_min, sym, &snr_used, &was_inferred, &was_vetoed) && was_inferred)
        {
            nbrMaskSet(mask, x);
            nbrMaskSet(local_inferred, x);
            if (nbrMaskTest(relevant, x))
                nbrISymAdd(sink, NBR_SYM_COVER, x, idx, snr_used);
        }
        else if (was_vetoed && nbrMaskTest(relevant, x))
            nbrISymAdd(sink, NBR_SYM_VETO, x, idx, snr_used);
    }
    if (inferred_out)
        *inferred_out = local_inferred;
    return mask;
}

// --- Reichweite ------------------------------------------------------------------------

static float nbrIDistKm(float lat1, float lon1, float lat2, float lon2)
{
    const double R = 6371.0;
    double dlat = (double)(lat2 - lat1) * M_PI / 180.0;
    double dlon = (double)(lon2 - lon1) * M_PI / 180.0;
    double a = sin(dlat / 2.0) * sin(dlat / 2.0) +
               cos((double)lat1 * M_PI / 180.0) * cos((double)lat2 * M_PI / 180.0) *
                   sin(dlon / 2.0) * sin(dlon / 2.0);
    double cc = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return (float)(R * cc);
}

static float nbrIReach(const NbrMatrix &m, int row, uint16_t now_min, int *partner)
{
    *partner = -1;
    if (row < 0 || row >= NBR_MAX_ROWS)
        return -1.0f;
    const NbrRow &r = m.row[row];
    if (!nbrIPosKnown(r))
        return -1.0f;
    // Partner: frische Kante in irgendeiner Richtung.
    NbrMask linked = nbrMaskNone();
    for (int e = 0; e < NBR_MAX_EDGES; e++)
    {
        const NbrEdge &ed = m.edge[e];
        if (!nbrIEdgeLive(ed) || !nbrIFresh(ed.last_min, now_min))
            continue;
        if (ed.x == row)
            nbrMaskSet(linked, ed.y);
        else if (ed.y == row)
            nbrMaskSet(linked, ed.x);
    }
    float best = -1.0f;
    int best_partner = -1;
    for (int y = nbrMaskNext(linked, -1); y >= 0; y = nbrMaskNext(linked, y))
    {
        if (!nbrIPosKnown(m.row[y]))
            continue;
        float d = nbrIDistKm(r.lat16 / 100.0f, r.lon16 / 100.0f, m.row[y].lat16 / 100.0f, m.row[y].lon16 / 100.0f);
        if (d > best)
        {
            best = d;
            best_partner = y;
        }
    }
    if (best_partner < 0)
        return -1.0f;
    *partner = best_partner;
    return best;
}

// ======================================================================================
// Oeffentliche Schnittstelle
// ======================================================================================

void nbrInit(NbrMatrix &m, const char *own_call, uint16_t now_min)
{
    uint64_t own = nbrIEncode(own_call);
    if (own == 0)
        own = NBR_CALL_OWN_INVALID;
    NBR_LOCK();
    nbrIInitAll(m, own, now_min);
    m.row[0].flags = NBR_FLAG_USED;
    m.row[0].last_min = now_min;
    NBR_UNLOCK();
}

bool nbrFresh(uint16_t last_min, uint16_t now_min)
{
    return nbrIFresh(last_min, now_min);
}

int nbrFind(const NbrMatrix &m, const char *call)
{
    uint64_t w = nbrIEncode(call);
    NBR_LOCK();
    int idx = nbrIReady(m) ? nbrIFindWord(m, w) : -1;
    NBR_UNLOCK();
    return idx;
}

void nbrReset(NbrMatrix &m, uint16_t now_min)
{
    NBR_LOCK();
    uint64_t own = m.call[0];
    nbrIInitAll(m, own, now_min);
    NBR_UNLOCK();
}

void nbrSweep(NbrMatrix &m, uint16_t now_min)
{
    NBR_LOCK();
    if (nbrIReady(m) && now_min != m.last_sweep)
    {
        for (int e = 0; e < NBR_MAX_EDGES; e++)
            if (nbrIEdgeLive(m.edge[e]) && !nbrIFresh(m.edge[e].last_min, now_min))
                nbrIEdgeFree(m, e);
        // Geisterzeilen: eine Zeile, deren letzte Beobachtung die Haelfte des
        // 16-Bit-Minutenbereichs zurueckliegt, saehe nach dem Ueberlauf wieder
        // jung aus und staeche bei der Verdraengung eine echte Zeile aus.
        for (int i = 1; i < NBR_MAX_ROWS; i++)
            if (nbrIUsed(m, i) && (uint16_t)(now_min - m.row[i].last_min) >= 32768)
                nbrIRowClear(m, i);
#if NBR_CNT_HALVE_MIN > 0
        if ((uint16_t)(now_min - m.last_halve) >= NBR_CNT_HALVE_MIN)
        {
            for (int e = 0; e < NBR_MAX_EDGES; e++)
                if (nbrIEdgeLive(m.edge[e]))
                    m.edge[e].cnt = (uint8_t)(((unsigned)m.edge[e].cnt + 1u) >> 1);
            m.last_halve = now_min;
        }
#endif
        m.last_sweep = now_min;
    }
    NBR_UNLOCK();
}

bool nbrRowGet(const NbrMatrix &m, int row, NbrRowView *out)
{
    if (!out || row < 0 || row >= NBR_MAX_ROWS)
        return false;
    uint64_t w;
    NbrRow r;
    NBR_LOCK();
    bool ok = nbrIReady(m) && (row == 0 || nbrIUsed(m, row)); // vor nbrInit(): keine Zeile
    w = m.call[row];
    r = m.row[row];
    NBR_UNLOCK();
    if (!ok)
        return false;
    nbrIDecode(w, out->call);
    bool pos = (r.lat16 != (int16_t)NBR_POS_UNKNOWN && r.lon16 != (int16_t)NBR_POS_UNKNOWN);
    out->lat = pos ? r.lat16 / 100.0f : NAN;
    out->lon = pos ? r.lon16 / 100.0f : NAN;
    out->last_min = r.last_min;
    out->rpt_min = r.rpt_min;
    out->flags = r.flags;
    out->hw = r.hw;
    out->ncnt = r.ncnt;
    return true;
}

bool nbrEdgeGet(const NbrMatrix &m, int from, int to, NbrEdgeView *out)
{
    bool ok = false;
    NBR_LOCK();
    int e = nbrIReady(m) ? nbrIEdgeFind(m, from, to) : -1;
    if (e >= 0 && out)
    {
        out->cnt = m.edge[e].cnt;
        out->snr = m.edge[e].snr;
        out->last_min = m.edge[e].last_min;
    }
    ok = (e >= 0);
    NBR_UNLOCK();
    return ok;
}

bool nbrOwnCallIs(const NbrMatrix &m, const char *call)
{
    uint64_t w = nbrIEncode(call);
    NBR_LOCK();
    uint64_t own = m.call[0];
    NBR_UNLOCK();
    if (own == 0)
        return false; // nie initialisiert
    if (w == 0)
        return own == NBR_CALL_OWN_INVALID && call && *call; // nicht kodierbares eigenes Rufzeichen
    return w == own;
}

bool nbrRowHasFlag(const NbrMatrix &m, int row, uint8_t flag)
{
    if (row < 0 || row >= NBR_MAX_ROWS)
        return false;
    NBR_LOCK();
    bool r = (m.row[row].flags & flag) != 0;
    NBR_UNLOCK();
    return r;
}

void nbrRowSetFlag(NbrMatrix &m, int row, uint8_t flag)
{
    if (row < 0 || row >= NBR_MAX_ROWS)
        return;
    NBR_LOCK();
    if (row == 0 || nbrIUsed(m, row))
        m.row[row].flags |= flag;
    NBR_UNLOCK();
}

int nbrRowsUsed(const NbrMatrix &m)
{
    int n = 1; // Zeile 0 zaehlt immer
    NBR_LOCK();
    for (int i = 1; i < NBR_MAX_ROWS; i++)
        if (nbrIUsed(m, i))
            n++;
    NBR_UNLOCK();
    return n;
}

static int nbrIEdgesUsed(const NbrMatrix &m)
{
    if (!nbrIReady(m))
        return 0;
    int n = 0;
    for (int e = 0; e < NBR_MAX_EDGES; e++)
        if (nbrIEdgeLive(m.edge[e]))
            n++;
    return n;
}

int nbrEdgesUsed(const NbrMatrix &m)
{
    NBR_LOCK();
    int n = nbrIEdgesUsed(m);
    NBR_UNLOCK();
    return n;
}

uint64_t nbrCallEncode(const char *call)
{
    return nbrIEncode(call);
}

void nbrCallDecode(uint64_t word, char *out)
{
    if (out)
        nbrIDecode(word, out);
}

int nbrMaskHex(const NbrMask &mask, char *out, size_t outlen)
{
    return nbrIMaskHex(mask, out, outlen);
}

uint16_t nbrRowAgeMin(const NbrMatrix &m, int row, uint16_t now_min)
{
    if (row < 0 || row >= NBR_MAX_ROWS)
        return 0;
    NBR_LOCK();
    uint16_t last = m.row[row].last_min;
    NBR_UNLOCK();
    return (uint16_t)(now_min - last);
}

int nbrNoteFrame(NbrMatrix &m, const char *path, char type, const char *payload,
                 bool dest_gw, int16_t rssi_here, int8_t snr_here, uint16_t now_min)
{
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
    for (int i = 0; i < ntok; i++)
        for (int j = i + 1; j < ntok; j++)
            if (strncmp(tokens[i], tokens[j], NBR_CALL_LEN) == 0)
            {
                nbrLogDrop(now_min, "LOOP", path);
                return -2;
            }
    uint64_t words[8];
    for (int i = 0; i < ntok; i++)
        words[i] = nbrIEncode(tokens[i]);

    NBR_LOCK();
    bool ready = nbrIReady(m);
    NBR_UNLOCK();
    if (!ready)
        return 0; // vor nbrInit(): nichts einzutragen

    if (type != ':' && ntok > 2)
        nbrLogCut(now_min, ntok, 2, path);

    NbrEvList lg;
    lg.n = 0;
    NBR_LOCK();
    int rc = nbrINoteFrame(m, tokens, words, ntok, type, payload, dest_gw, snr_here, now_min, &lg);
    NBR_UNLOCK();

    for (int i = 0; i < lg.n; i++)
    {
        const NbrEv &ev = lg.ev[i];
        switch (ev.kind)
        {
        case NBR_EV_EVICT:
            nbrLogEvict(now_min, ev.a, ev.c1, ev.c2);
            break;
        case NBR_EV_EVICTE:
            nbrLogEvictEdge(now_min, ev.c1, ev.c2);
            break;
        case NBR_EV_EDGE:
            nbrLogEdge(now_min, tokens[ev.a], tokens[ev.b], type, ev.cnt, ev.snr);
            break;
        case NBR_EV_ME:
            nbrLogMe(now_min, tokens[ev.a], type, rssi_here, ev.cnt, ev.snr);
            break;
        case NBR_EV_DROPFULL:
            nbrLogDrop(now_min, "FULL", path);
            break;
        default:
            break;
        }
    }
    return rc;
}

void nbrNotePos(NbrMatrix &m, const char *call, float lat, float lon, bool mesh,
                uint8_t hw, uint16_t now_min)
{
    uint64_t w = nbrIEncode(call);
    NBR_LOCK();
    int idx = nbrIReady(m) ? nbrIFindWord(m, w) : -1;
    if (idx >= 0)
    {
        NbrRow &r = m.row[idx];
        r.lat16 = nbrIDeg16(lat);
        r.lon16 = nbrIDeg16(lon);
        r.flags |= NBR_FLAG_POS;
        if (mesh)
            r.flags |= NBR_FLAG_MESH;
        else
            r.flags &= (uint8_t)~NBR_FLAG_MESH;
        r.hw = hw;
        r.last_min = now_min;
    }
    NBR_UNLOCK();
    if (idx < 0 || !nbrLog)
        return;
    // Log mit den EINGANGSwerten (5 Nachkommastellen wie bisher), nicht mit
    // der auf 0,01 Grad gerundeten Speicherung.
    char buf[160];
    snprintf(buf, sizeof(buf), "[NBR]|POS|%u|%s|%.5f|%.5f|%d|%u",
             (unsigned)now_min, call, (double)lat, (double)lon, mesh ? 1 : 0, (unsigned)hw);
    nbrLog(buf);
}

// --- HN-Nachbarschaftsbericht ----------------------------------------------------------

struct NbrReportTop
{
    int8_t snr;
    char   call[NBR_CALL_LEN];
};

// a vor b: SNR absteigend, bei Gleichstand Rufzeichen aufsteigend.
static bool nbrIReportBefore(int8_t snr_a, const char *call_a, const NbrReportTop &b)
{
    if (snr_a != b.snr)
        return snr_a > b.snr;
    return strncmp(call_a, b.call, NBR_CALL_LEN) < 0;
}

int nbrBuildReport(const NbrMatrix &m, uint16_t now_min, int heard_count, char *out, size_t outlen)
{
    if (!out || outlen == 0)
        return -1;
    out[0] = '\0';

    // Nur die besten NBR_REPORT_MAX_ENTRIES werden gebraucht (plus die
    // Gesamtzahl fuer '+') -- eine sortierte Bestenliste statt einer
    // zeilengrossen Kandidatenliste auf dem Loop-Stack.
    NbrReportTop top[NBR_REPORT_MAX_ENTRIES];
    int ntop = 0, ncand = 0;
    NBR_LOCK();
    if (nbrIReady(m))
    {
        for (int e = 0; e < NBR_MAX_EDGES; e++)
        {
            const NbrEdge &ed = m.edge[e];
            if (!nbrIEdgeLive(ed) || ed.y != 0 || ed.x == 0 || !nbrIUsed(m, ed.x))
                continue;
            if ((uint16_t)(now_min - ed.last_min) >= NBR_REPORT_FRESH_MIN)
                continue;
            if (ed.snr == NBR_SNR_UNKNOWN || ed.snr < NBR_SYM_MIN_SNR)
                continue;
            ncand++;
            char call[NBR_CALL_LEN];
            nbrIDecode(m.call[ed.x], call);
            int pos = ntop;
            while (pos > 0 && nbrIReportBefore(ed.snr, call, top[pos - 1]))
                pos--;
            if (pos >= NBR_REPORT_MAX_ENTRIES)
                continue;
            int last = (ntop < NBR_REPORT_MAX_ENTRIES) ? ntop : NBR_REPORT_MAX_ENTRIES - 1;
            for (int k = last; k > pos; k--)
                top[k] = top[k - 1];
            top[pos].snr = ed.snr;
            memcpy(top[pos].call, call, NBR_CALL_LEN);
            if (ntop < NBR_REPORT_MAX_ENTRIES)
                ntop++;
        }
    }
    NBR_UNLOCK();

    bool truncated = ncand > ntop;
    char line[256];
    int n = snprintf(line, sizeof(line), "R%d;N%d%s;", heard_count, ntop, truncated ? "+" : "");
    if (n < 0)
        return -1;
    for (int i = 0; i < ntop && strlen(line) < sizeof(line) - 1; i++)
    {
        size_t used = strlen(line);
        snprintf(line + used, sizeof(line) - used, "%s,%d;", top[i].call, (int)top[i].snr);
    }
    size_t len = strlen(line);
    if (len + 1 > outlen)
    {
        out[0] = '\0';
        return -1;
    }
    memcpy(out, line, len + 1);
    return (int)len;
}

struct NbrReportEntryIn
{
    char   call[NBR_CALL_LEN];
    int8_t snr;
};

static bool nbrParseReport(const char *payload, long *heard, int *k, bool *truncated,
                           NbrReportEntryIn *entries)
{
    if (!payload || payload[0] != 'R')
        return false;
    const char *p = payload + 1;
    const char *seg = p;
    while (*p && *p != ';')
        p++;
    if (*p != ';')
        return false;
    long h = nbrParseUint(seg, (size_t)(p - seg));
    if (h < 0)
        return false;
    p++;
    if (*p != 'N')
        return false;
    p++;
    seg = p;
    while (*p && *p != ';' && *p != '+')
        p++;
    long kk = nbrParseUint(seg, (size_t)(p - seg));
    if (kk < 0 || kk > NBR_REPORT_MAX_ENTRIES)
        return false;
    bool trunc = false;
    if (*p == '+')
    {
        trunc = true;
        p++;
    }
    if (*p != ';')
        return false;
    p++;
    for (long i = 0; i < kk; i++)
    {
        seg = p;
        while (*p && *p != ',')
            p++;
        if (*p != ',')
            return false;
        size_t call_len = (size_t)(p - seg);
        if (call_len < 1 || call_len >= NBR_CALL_LEN)
            return false;
        p++;
        const char *snr_start = p;
        while (*p && *p != ';')
            p++;
        if (*p != ';')
            return false;
        long snr_v;
        if (!nbrParseInt(snr_start, (size_t)(p - snr_start), &snr_v))
            return false;
        p++;
        memcpy(entries[i].call, seg, call_len);
        entries[i].call[call_len] = '\0';
        entries[i].snr = nbrClampSnr((int32_t)snr_v);
    }
    if (*p != '\0')
        return false;
    *heard = h;
    *k = (int)kk;
    *truncated = trunc;
    return true;
}

int nbrNoteReport(NbrMatrix &m, const char *sender, const char *payload, uint16_t now_min)
{
    long heard = 0;
    int k = 0;
    bool truncated = false;
    NbrReportEntryIn entries[NBR_REPORT_MAX_ENTRIES];
    if (!nbrParseReport(payload, &heard, &k, &truncated, entries))
    {
        nbrLogDrop(now_min, "RPT", sender);
        return -1;
    }

    uint64_t sw = nbrIEncode(sender);
    uint64_t ew[NBR_REPORT_MAX_ENTRIES];
    for (int i = 0; i < k; i++)
        ew[i] = nbrIEncode(entries[i].call);

    // Status je Eintrag: 0 norow, 1 self, 2 ok. Eine Kantenverdraengung je
    // Eintrag hoechstens (je Eintrag eine neue Kante).
    uint8_t status[NBR_REPORT_MAX_ENTRIES];
    NbrEvList lg;
    lg.n = 0;
    int ev_at[NBR_REPORT_MAX_ENTRIES + 1];
    int applied = 0;

    NBR_LOCK();
    int s = nbrIReady(m) ? nbrIFindWord(m, sw) : -1;
    if (s >= 0)
    {
        for (int i = 0; i < k; i++)
        {
            ev_at[i] = lg.n;
            int target;
            if (ew[i] != 0 && ew[i] == m.call[0])
            {
                target = 0; // "Absender s hat mich gehoert" -> Kante (0, s)
                status[i] = 1;
            }
            else
            {
                target = nbrIFindWord(m, ew[i]);
                status[i] = (target < 0) ? 0 : 2;
            }
            if (status[i] == 0)
                continue;
            // Die Diagonale bleibt leer (Konzept 4.1): ein Eintrag, der den
            // Absender selbst nennt, zaehlt, schreibt aber keine Kante.
            int e = nbrIEdgeHit(m, target, s, now_min, &lg);
            if (e >= 0)
                m.edge[e].snr = entries[i].snr;
            applied++;
        }
        ev_at[k] = lg.n;
        if (truncated)
            m.row[s].flags &= (uint8_t)~NBR_FLAG_RPT;
        else
            m.row[s].flags |= NBR_FLAG_RPT;
        m.row[s].rpt_min = now_min;
    }
    NBR_UNLOCK();

    if (s < 0)
        return 0; // Absender (noch) unbekannt

    if (nbrLog)
    {
        for (int i = 0; i < k; i++)
        {
            for (int j = ev_at[i]; j < ev_at[i + 1]; j++)
                if (lg.ev[j].kind == NBR_EV_EVICTE)
                    nbrLogEvictEdge(now_min, lg.ev[j].c1, lg.ev[j].c2);
            const char *st = (status[i] == 1) ? "self" : (status[i] == 2) ? "ok" : "norow";
            char buf[160];
            snprintf(buf, sizeof(buf), "[NBR]|RPT|%u|%s|%s|%d|%s",
                     (unsigned)now_min, sender, entries[i].call, (int)entries[i].snr, st);
            nbrLog(buf);
        }
        char buf[160];
        snprintf(buf, sizeof(buf), "[NBR]|RPTSUM|%u|%s|%ld|%d|%d|%d",
                 (unsigned)now_min, sender, heard, k, truncated ? 0 : 1, applied);
        nbrLog(buf);
    }
    return applied;
}

// --- Urteile ------------------------------------------------------------------------------

uint8_t nbrHearers(const NbrMatrix &m, int row, uint16_t now_min, uint8_t *out, uint8_t max)
{
    if (row < 0 || row >= NBR_MAX_ROWS)
        return 0;
    NBR_LOCK();
    NbrMask h = nbrIReady(m) ? nbrIHearersAll(m, row, now_min) : nbrMaskNone();
    NBR_UNLOCK();
    uint8_t n = 0;
    for (int y = nbrMaskNext(h, -1); y >= 0; y = nbrMaskNext(h, y))
    {
        if (out && n < max)
            out[n] = (uint8_t)y;
        n++;
    }
    return n;
}

int nbrExclusive(const NbrMatrix &m, uint16_t now_min, uint8_t *out, uint8_t max)
{
    NbrMask direct, excl;
    NBR_LOCK();
    if (nbrIReady(m))
    {
        NbrCtx cbuf;
        const NbrCtx &c = nbrICtx(m, now_min, cbuf);
        direct = c.direct;
        excl = nbrMaskAndNot(c.direct, nbrICoveredByAnyone(m, c));
    }
    else
        direct = excl = nbrMaskNone();
    NBR_UNLOCK();
    if (nbrMaskEmpty(direct))
        return -1;
    int count = 0;
    for (int x = nbrMaskNext(excl, -1); x >= 0; x = nbrMaskNext(excl, x))
    {
        if (out && count < (int)max)
            out[count] = (uint8_t)x;
        count++;
    }
    return count;
}

int nbrRowMeshNeedCount(const NbrMatrix &m, int row, uint16_t now_min)
{
    NbrCtx cbuf;
    NBR_LOCK();
    int n = nbrIReady(m) ? nbrIMeshNeedSet(m, nbrICtx(m, now_min, cbuf), row, NULL) : -1;
    NBR_UNLOCK();
    return n;
}

const char *nbrRowMeshNeed(const NbrMatrix &m, int row, uint16_t now_min)
{
    NbrCtx cbuf;
    NBR_LOCK();
    int n = nbrIReady(m) ? nbrIMeshNeedSet(m, nbrICtx(m, now_min, cbuf), row, NULL) : -1;
    NBR_UNLOCK();
    return nbrIMeshNeedWord(n);
}

// Privat (nicht im Header): die Menge hinter #X, fuer Tests und die Web-Sicht
// einer spaeteren Welle. Liefert #X wie nbrRowMeshNeedCount(). unused, damit
// ein Build ohne Aufrufer keine Warnung bekommt.
__attribute__((unused)) static int nbrRowMeshNeedSet(const NbrMatrix &m, int row, uint16_t now_min, NbrMask *out)
{
    NbrCtx cbuf;
    NBR_LOCK();
    int n = nbrIReady(m) ? nbrIMeshNeedSet(m, nbrICtx(m, now_min, cbuf), row, out) : -1;
    NBR_UNLOCK();
    if (n < 0 && out)
        *out = nbrMaskNone();
    return n;
}

NbrMask nbrDirectMask(const NbrMatrix &m, uint16_t now_min)
{
    NbrCtx cbuf;
    NBR_LOCK();
    NbrMask r = nbrIReady(m) ? nbrICtx(m, now_min, cbuf).direct : nbrMaskNone();
    NBR_UNLOCK();
    return r;
}

NbrMask nbrHeardMeMask(const NbrMatrix &m, uint16_t now_min)
{
    NBR_LOCK();
    NbrMask r = nbrIReady(m) ? nbrIHeardMeMask(m, now_min) : nbrMaskNone();
    NBR_UNLOCK();
    return r;
}

NbrMask nbrHearersMask(const NbrMatrix &m, int row, uint16_t now_min)
{
    NBR_LOCK();
    NbrMask r = nbrIReady(m) ? nbrIHearersMask(m, row, now_min) : nbrMaskNone();
    NBR_UNLOCK();
    return r;
}

NbrNeed nbrRelayNeed(const NbrMatrix &m, const char *path, uint16_t now_min, bool sym, uint32_t msg_id)
{
    char tokens[8][NBR_CALL_LEN];
    int ntok = nbrTokenizePath(path, tokens, 8);
    if (ntok < 0)
    {
        NbrNeed r;
        r.need = r.alone = r.inferred = nbrMaskNone();
        r.known = false;
        return r; // ungueltiger Pfad: kein Wissen
    }
    uint64_t words[8];
    for (int i = 0; i < ntok; i++)
        words[i] = nbrIEncode(tokens[i]);

    NbrSymSink sink;
    NBR_LOCK();
    nbrISymBegin(sink, m, tokens, now_min, msg_id);
    NbrNeed r = nbrIRelayNeed(m, words, ntok, now_min, sym, sink);
    NBR_UNLOCK();
    nbrISymFlush(sink);
    return r;
}

NbrMask nbrCoverMask(const NbrMatrix &m, const char *relayer, uint16_t now_min, bool sym,
                     const NbrMask &relevant, uint32_t msg_id, NbrMask *inferred)
{
    uint64_t w = nbrIEncode(relayer);
    NbrSymSink sink;
    NBR_LOCK();
    nbrISymBegin(sink, m, NULL, now_min, msg_id);
    NbrMask r = nbrICoverMask(m, w, now_min, sym, relevant, inferred, sink);
    NBR_UNLOCK();
    nbrISymFlush(sink);
    return r;
}

int nbrExclusiveDirect(const NbrMatrix &m, uint16_t now_min, uint8_t *out, uint8_t max)
{
    NbrMask direct, excl;
    NBR_LOCK();
    if (nbrIReady(m))
    {
        NbrCtx cbuf;
        const NbrCtx &c = nbrICtx(m, now_min, cbuf);
        direct = c.direct;
        excl = nbrIExclusiveDirect(m, c);
    }
    else
        direct = excl = nbrMaskNone();
    NBR_UNLOCK();
    if (nbrMaskEmpty(direct))
        return -1;
    int count = 0;
    for (int x = nbrMaskNext(excl, -1); x >= 0; x = nbrMaskNext(excl, x))
    {
        if (out && count < (int)max)
            out[count] = (uint8_t)x;
        count++;
    }
    return count;
}

float nbrDistKm(float lat1, float lon1, float lat2, float lon2)
{
    return nbrIDistKm(lat1, lon1, lat2, lon2);
}

float nbrReach(const NbrMatrix &m, int row, uint16_t now_min, int *partner)
{
    int p = -1;
    NBR_LOCK();
    float d = nbrIReady(m) ? nbrIReach(m, row, now_min, &p) : -1.0f;
    NBR_UNLOCK();
    if (partner)
        *partner = p;
    return d;
}

// --- Ausgabe ------------------------------------------------------------------------------

// Anhaengen mit snprintf-Semantik: schreibt, solange Platz ist, und zaehlt
// die Laenge weiter, die ein einziges snprintf gemeldet haette.
struct NbrApp
{
    char  *out;
    size_t cap;
    size_t pos;
};

static void nbrAppStr(NbrApp &a, const char *s)
{
    size_t n = strlen(s);
    if (a.pos + 1 < a.cap)
    {
        size_t room = a.cap - a.pos - 1;
        size_t k = n < room ? n : room;
        memcpy(a.out + a.pos, s, k);
        a.out[a.pos + k] = '\0';
    }
    a.pos += n;
}

static void nbrAppInt(NbrApp &a, long v)
{
    char buf[24];
    char *p = buf + sizeof(buf) - 1;
    *p = '\0';
    bool neg = v < 0;
    unsigned long u = neg ? (unsigned long)(-(v + 1)) + 1u : (unsigned long)v;
    do
    {
        *--p = (char)('0' + (u % 10u));
        u /= 10u;
    } while (u);
    if (neg)
        *--p = '-';
    nbrAppStr(a, p);
}

int nbrFormatRow(const NbrMatrix &m, int row, uint16_t now_min, char *out, size_t outlen)
{
    if (!out || outlen == 0)
        return 0;
    out[0] = '\0';
    if (row < 0 || row >= NBR_MAX_ROWS)
        return 0;

    NbrApp a;
    a.out = out;
    a.cap = outlen;
    a.pos = 0;
    int partner = -1;
    float reach = -1.0f;
    char partner_call[NBR_CALL_LEN] = "";
    uint16_t age = 0;
    bool ok;

    // Unter der Klammer nur Zeichenketten und Ganzzahlen, ohne printf.
    NBR_LOCK();
    ok = nbrIReady(m) && (row == 0 || nbrIUsed(m, row));
    if (ok)
    {
        const NbrRow &r = m.row[row];
        char call[NBR_CALL_LEN];
        nbrIDecode(m.call[row], call);
        nbrAppStr(a, call);
        nbrAppStr(a, (r.flags & NBR_FLAG_GW) ? " GW+" : " GW-");
        nbrAppStr(a, (r.flags & NBR_FLAG_MESH) ? " M+" : " M-");
        nbrAppStr(a, " hears_me:");
        int e = (row != 0) ? nbrIEdgeFind(m, 0, row) : -1;
        if (e >= 0 && nbrIFresh(m.edge[e].last_min, now_min) && m.edge[e].snr != NBR_SNR_UNKNOWN)
            nbrAppInt(a, m.edge[e].snr);
        else
            nbrAppStr(a, "-");
        nbrAppStr(a, " hearers:");
        NbrMask h = nbrIHearersAll(m, row, now_min);
        if (nbrMaskEmpty(h))
            nbrAppStr(a, "-");
        bool first = true;
        for (int y = nbrMaskNext(h, -1); y >= 0; y = nbrMaskNext(h, y))
        {
            if (!first)
                nbrAppStr(a, ",");
            first = false;
            char hc[NBR_CALL_LEN];
            nbrIDecode(m.call[y], hc);
            nbrAppStr(a, hc);
        }
        reach = nbrIReach(m, row, now_min, &partner);
        if (partner >= 0)
            nbrIDecode(m.call[partner], partner_call);
        age = (uint16_t)(now_min - r.last_min);
    }
    NBR_UNLOCK();
    if (!ok)
        return 0;

    char tail[64];
    if (reach >= 0.0f && partner >= 0)
        snprintf(tail, sizeof(tail), " reach:%.1fkm@%s age:%um", (double)reach, partner_call, (unsigned)age);
    else
        snprintf(tail, sizeof(tail), " reach:- age:%um", (unsigned)age);
    nbrAppStr(a, tail);
    return (int)a.pos;
}

void nbrLogSnapshot(const NbrMatrix &m, uint16_t now_min)
{
    if (!nbrLog)
        return;

    char own[NBR_CALL_LEN];
    int rows_used = 1, edges = 0;
    NBR_LOCK();
    nbrIDecode(m.call[0], own);
    for (int i = 1; i < NBR_MAX_ROWS; i++)
        if (nbrIUsed(m, i))
            rows_used++;
    edges = nbrIEdgesUsed(m);
    NBR_UNLOCK();

    char buf[160];
    snprintf(buf, sizeof(buf), "[NBR]|SNAP|%u|%s|%d|%d|%d",
             (unsigned)now_min, own, rows_used, (int)NBR_MAX_ROWS, edges);
    nbrLog(buf);

    // Je Zeile eine eigene kurze Klammer: jede ROW-Zeile ist in sich
    // stimmig, zwischen zwei Zeilen darf der Empfang weiterlaufen.
    for (int i = 0; i < NBR_MAX_ROWS; i++)
    {
        bool show;
        char call[NBR_CALL_LEN];
        unsigned flags = 0, age = 0, hearers = 0;
        const char *verdict = "UNK";
        const char *meshneed = "NA";
        NBR_LOCK();
        show = (i == 0 || nbrIUsed(m, i));
        if (show)
        {
            nbrIDecode(m.call[i], call);
            flags = m.row[i].flags;
            age = (uint16_t)(now_min - m.row[i].last_min);
            if (nbrIReady(m))
            {
                hearers = (unsigned)nbrMaskCount(nbrIHearersAll(m, i, now_min));
                NbrCtx cbuf;
                const NbrCtx &c = nbrICtx(m, now_min, cbuf);
                NbrMask cov = nbrICoveredByAnyone(m, c);
                verdict = nbrIRowVerdict(m, c, i, cov);
                meshneed = nbrIMeshNeedWord(nbrIMeshNeedSet(m, c, i, NULL));
            }
        }
        NBR_UNLOCK();
        if (!show)
            continue;
        snprintf(buf, sizeof(buf), "[NBR]|ROW|%u|%d|%s|%u|%u|%u|%s|%s",
                 (unsigned)now_min, i, call, flags, age, hearers, verdict, meshneed);
        nbrLog(buf);
    }

    snprintf(buf, sizeof(buf), "[NBR]|ENDSNAP|%u", (unsigned)now_min);
    nbrLog(buf);
}

// Eine Instanz fuers ganze Geraet, im BSS (siehe nbr_matrix.h). Bis zum
// ersten nbrInit() ist call[0] == 0, und jede Funktion behandelt die Matrix
// als leer.
#ifndef NATIVE_BUILD
NbrMatrix nbrMatrix;
#endif
