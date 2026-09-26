// MeshCom 5, Welle 3 (docs/meshcom5-campaign.md): die Abfrageschicht
// src/nbr_views.* und die Stufe-2-Einspeisungen in src/nbr_matrix.cpp
// (Direkt-Slot, Horizont, Echo-Tabelle, NCNT, Uhr, ME-Schritt ohne
// Pfadpruefung). env native_nbr_views: 128 Zeilen, 512 Kanten, 64 Slots,
// 112 Horizont-Eintraege (S3/nRF52). Die Tests rechnen mit NBR_EXT_SLOTS /
// NBR_HZ_ENTRIES / NBR_MAX_ROWS, damit dieselbe Datei auch mit den
// klassischen Groessen (64/256/48/48) laeuft.
//
// test_build_src=no: beide Quellen kommen per #include (Muster wie
// test/test_nbr_matrix), die Tests duerfen darum die internen Helfer und
// Felder lesen, wo ein Zustand direkt aufgebaut oder geprueft wird.

#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "../../src/nbr_matrix.cpp"
#include "../../src/nbr_views.cpp"

static const char *OWN = "DK5EN-98";

void setUp(void) {}
void tearDown(void)
{
    nbrLog = NULL;
}

// --- Helfer -----------------------------------------------------------------------

static char g_log[32768];

static void logcap(const char *line)
{
    size_t used = strlen(g_log);
    if (used + strlen(line) + 2 >= sizeof(g_log))
        return;
    strcat(g_log, line);
    strcat(g_log, "\n");
}

static void logstart(void)
{
    g_log[0] = '\0';
    nbrLog = logcap;
}

static int count_substr(const char *hay, const char *needle)
{
    int n = 0;
    for (const char *p = strstr(hay, needle); p; p = strstr(p + 1, needle))
        n++;
    return n;
}

// Matrix im BSS-Zustand (wie das Geraet) und initialisiert.
static NbrMatrix *mk(uint16_t t = 0)
{
    NbrMatrix *m = (NbrMatrix *)calloc(1, sizeof(NbrMatrix));
    nbrInit(*m, OWN, t);
    return m;
}

static const char *nm(int i)
{
    static char buf[8][12];
    static int k = 0;
    char *b = buf[k++ & 7];
    snprintf(b, 12, "N%03d", i);
    return b;
}

// Direktempfang eines Textes: nur der ME-Schritt.
static void heard(NbrMatrix &m, const char *call, uint16_t t, int8_t snr = 0)
{
    nbrNoteFrame(m, call, ':', NULL, false, -90, snr, t);
}

static NbrDirectInfo info(char plt, int16_t rssi, uint8_t sec, bool own)
{
    NbrDirectInfo d;
    memset(&d, 0, sizeof(d));
    d.plt = plt;
    d.hw = 3;
    d.mod = 0x23;
    d.rssi = rssi;
    d.sec = sec;
    d.pl = 1;
    d.mesh = true;
    d.own_frame = own;
    d.fw = 0;
    d.has_pos = false;
    d.lat = d.lon = NBR_POS_NONE;
    d.alt_m = NBR_ALT_UNKNOWN;
    return d;
}

static int slot_of(const NbrMatrix &m, const char *call)
{
    int r = nbrFind(m, call);
    return r > 0 && m.row[r].ext != 0xFF ? m.row[r].ext : -1;
}

static void set_edge_snr(NbrMatrix &m, int x, int y, int8_t snr)
{
    int e = nbrIEdgeFind(m, x, y);
    TEST_ASSERT_TRUE(e >= 0);
    m.edge[e].snr = snr;
}

static bool masks_consistent(const NbrMatrix &m)
{
    for (int i = 0; i < NBR_MAX_ROWS; i++)
        for (int j = 0; j < NBR_MAX_ROWS; j++)
        {
            bool live = false;
            for (int e = 0; e < NBR_MAX_EDGES; e++)
                if (m.edge[e].x == i && m.edge[e].y == j)
                    live = true;
            if (nbrMaskTest(m.heardBy[i], j) != live || nbrMaskTest(m.hears[j], i) != live)
                return false;
        }
    return true;
}

static int hz_index(const NbrMatrix &m, const char *call)
{
    uint64_t w = nbrCallEncode(call);
    for (int h = 0; h < NBR_HZ_ENTRIES; h++)
        if (m.hz_call[h] == w)
            return h;
    return -1;
}

// Weg-Eintrag per Rufzeichen (-1 wenn keiner).
static int route_find(const NbrMatrix &m, const char *call, uint16_t now, NbrRouteView *out)
{
    int n = nbrRouteCount(m, now);
    for (int i = 0; i < n; i++)
    {
        NbrRouteView v;
        if (nbrRouteGet(m, i, now, &v) && strcmp(v.call, call) == 0)
        {
            if (out)
                *out = v;
            return i;
        }
    }
    return -1;
}

// --- 1: Bitfelder des Direkt-Slots -----------------------------------------------------

void test_bits_every_field_min_max_and_neighbours_untouched(void)
{
    struct F
    {
        unsigned off, w;
    } f[] = {{NBR_XO_SEC, NBR_XW_SEC}, {NBR_XO_PLT, NBR_XW_PLT}, {NBR_XO_MOD, NBR_XW_MOD},
             {NBR_XO_RSSI, NBR_XW_RSSI}, {NBR_XO_LAT, NBR_XW_LAT}, {NBR_XO_LON, NBR_XW_LON},
             {NBR_XO_ALT, NBR_XW_ALT}, {NBR_XO_PL, NBR_XW_PL}, {NBR_XO_MESH, NBR_XW_MESH},
             {NBR_XO_F, NBR_XW_F}, {NBR_XO_S, NBR_XW_S}, {NBR_XO_FW, NBR_XW_FW},
             {NBR_XO_W, NBR_XW_W}, {NBR_XO_SYM, NBR_XW_SYM}};
    const int nf = (int)(sizeof(f) / sizeof(f[0]));
    // Luekenlos und in der Reihenfolge von build.py EXT_BITS.
    unsigned pos = 0;
    for (int i = 0; i < nf; i++)
    {
        TEST_ASSERT_EQUAL_UINT(pos, f[i].off);
        pos += f[i].w;
    }
    TEST_ASSERT_EQUAL_UINT(103, pos);

    for (int i = 0; i < nf; i++)
    {
        uint32_t maxv = (f[i].w == 32) ? 0xFFFFFFFFu : ((1u << f[i].w) - 1u);
        for (int fill = 0; fill < 2; fill++)
        {
            uint8_t slot[NBR_EXT_BYTES];
            memset(slot, fill ? 0xFF : 0x00, sizeof(slot));
            uint32_t vals[3] = {0u, maxv, maxv / 2u};
            for (int v = 0; v < 3; v++)
            {
                nbrBitsPut(slot, f[i].off, f[i].w, vals[v]);
                TEST_ASSERT_EQUAL_UINT32(vals[v], nbrBitsGet(slot, f[i].off, f[i].w));
                for (int j = 0; j < nf; j++)
                {
                    if (j == i)
                        continue;
                    uint32_t mj = (1u << f[j].w) - 1u;
                    TEST_ASSERT_EQUAL_UINT32(fill ? mj : 0u, nbrBitsGet(slot, f[j].off, f[j].w));
                }
            }
            // Bits jenseits von 103 bleiben unberuehrt.
            TEST_ASSERT_EQUAL_UINT8(fill ? 0x80 : 0x00, slot[12] & 0x80);
        }
    }
}

// Kodierung ueber nbrNoteDirect()/nbrMhGet() an den Raendern.
void test_direct_slot_encodings_at_min_max_unknown(void)
{
    NbrMatrix *mp = mk();
    NbrMatrix &m = *mp;
    heard(m, "DL1AAA-1", 10);
    NbrMhView v;

    // frischer Slot ohne Position: alles unbekannt ausser den Rahmenfeldern
    NbrDirectInfo d = info('X', -200, 60, true);
    d.pl = 20;
    d.fw = '?';
    nbrNoteDirect(m, "DL1AAA-1", d, 10);
    TEST_ASSERT_TRUE(nbrMhGet(m, nbrFind(m, "DL1AAA-1"), 10, &v));
    TEST_ASSERT_EQUAL_UINT8(0xFF, v.sec);
    TEST_ASSERT_EQUAL_INT8(0, v.plt);
    TEST_ASSERT_EQUAL_INT16(-160, v.rssi); // geklemmt
    TEST_ASSERT_EQUAL_UINT8(15, v.pl);     // geklemmt
    TEST_ASSERT_FALSE(nbrPosKnown(v.lat, v.lon));
    TEST_ASSERT_EQUAL_INT16(NBR_MH_ALT_UNKNOWN, v.alt);
    TEST_ASSERT_EQUAL_INT8(0, v.fw);
    TEST_ASSERT_EQUAL_UINT8(1, v.mesh);
    TEST_ASSERT_EQUAL_UINT8(0x23, v.mod);

    // Maximum
    d = info('@', 120, 59, true);
    d.fw = 'z';
    d.has_pos = true;
    d.lat = 90.0f;
    d.lon = 180.0f;
    d.alt_m = 40000;
    d.mesh = false;
    d.pl = 15;
    d.mod = 0xFF;
    nbrNoteDirect(m, "DL1AAA-1", d, 10);
    nbrMhGet(m, nbrFind(m, "DL1AAA-1"), 10, &v);
    TEST_ASSERT_EQUAL_UINT8(59, v.sec);
    TEST_ASSERT_EQUAL_INT8('@', v.plt);
    TEST_ASSERT_EQUAL_INT16(95, v.rssi);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 90.0f, v.lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 180.0f, v.lon);
    TEST_ASSERT_EQUAL_INT16(32767, v.alt);
    TEST_ASSERT_EQUAL_INT8('z', v.fw);
    TEST_ASSERT_EQUAL_UINT8(0, v.mesh);
    TEST_ASSERT_EQUAL_UINT8(0xFF, v.mod);

    // Minimum
    d = info(':', -160, 0, true);
    d.fw = 'a';
    d.has_pos = true;
    d.lat = -90.0f;
    d.lon = -180.0f;
    d.alt_m = -5000;
    d.pl = 0;
    d.mod = 0;
    nbrNoteDirect(m, "DL1AAA-1", d, 10);
    nbrMhGet(m, nbrFind(m, "DL1AAA-1"), 10, &v);
    TEST_ASSERT_EQUAL_UINT8(0, v.sec);
    TEST_ASSERT_EQUAL_INT8(':', v.plt);
    TEST_ASSERT_EQUAL_INT16(-160, v.rssi);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -90.0f, v.lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -180.0f, v.lon);
    TEST_ASSERT_EQUAL_INT16(-1000, v.alt);
    TEST_ASSERT_EQUAL_INT8('a', v.fw);
    TEST_ASSERT_EQUAL_UINT8(0, v.pl);

    // 0,0001-Grad-Aufloesung (Konzept 4.6) und unbekannte Hoehe bei Position
    d = info('!', -95, 30, true);
    d.has_pos = true;
    d.lat = 48.40783f;
    d.lon = 11.73800f;
    d.alt_m = NBR_ALT_UNKNOWN;
    nbrNoteDirect(m, "DL1AAA-1", d, 10);
    nbrMhGet(m, nbrFind(m, "DL1AAA-1"), 10, &v);
    TEST_ASSERT_FLOAT_WITHIN(0.6e-4f, 48.40783f, v.lat);
    TEST_ASSERT_FLOAT_WITHIN(0.6e-4f, 11.73800f, v.lon);
    TEST_ASSERT_EQUAL_INT16(NBR_MH_ALT_UNKNOWN, v.alt);
    TEST_ASSERT_EQUAL_INT8('a', v.fw); // fw 0 ueberschreibt nicht
    free(mp);
}

// --- 2: Slot-Vergabe, EVICT-X, Freigabe ----------------------------------------------------

void test_slot_allocation_evict_x_and_sweep_release(void)
{
    NbrMatrix *mp = mk();
    NbrMatrix &m = *mp;
    TEST_ASSERT_TRUE(NBR_EXT_SLOTS + 2 < NBR_MAX_ROWS);
    // Ohne Zeile: folgenlos. Eigenes Rufzeichen: folgenlos.
    nbrNoteDirect(m, "DL9ZZZ-1", info('!', -90, 1, true), 1);
    nbrNoteDirect(m, OWN, info('!', -90, 1, true), 1);
    for (int s = 0; s < NBR_MAX_ROWS; s++)
        TEST_ASSERT_EQUAL_UINT8(0xFF, m.row[s].ext);

    for (int i = 0; i < NBR_EXT_SLOTS; i++)
    {
        heard(m, nm(i), (uint16_t)(10 + i));
        nbrNoteDirect(m, nm(i), info('!', -90, 1, true), (uint16_t)(10 + i));
        TEST_ASSERT_EQUAL_INT(i, slot_of(m, nm(i)));
    }
    // Wieder gehoert: derselbe Slot, keine Verdraengung.
    logstart();
    heard(m, nm(3), 200);
    nbrNoteDirect(m, nm(3), info(':', -80, 2, true), 200);
    TEST_ASSERT_EQUAL_INT(3, slot_of(m, nm(3)));
    TEST_ASSERT_NULL(strstr(g_log, "EVICT-X"));

    // Voll: der am laengsten nicht gehoerte (N000, Minute 10) weicht, seine Zeile bleibt.
    const char *nw = nm(NBR_EXT_SLOTS);
    heard(m, nw, 201);
    nbrNoteDirect(m, nw, info('!', -90, 1, true), 201);
    char want[64];
    snprintf(want, sizeof(want), "[NBR]|EVICT-X|201|N000|%s\n", nw);
    TEST_ASSERT_NOT_NULL(strstr(g_log, want));
    TEST_ASSERT_EQUAL_INT(0, slot_of(m, nw));
    TEST_ASSERT_EQUAL_INT(-1, slot_of(m, "N000"));
    TEST_ASSERT_TRUE(nbrFind(m, "N000") > 0);
    NbrMhView v;
    TEST_ASSERT_TRUE(nbrMhGet(m, nbrFind(m, "N000"), 201, &v)); // bleibt MHeard, ohne Slotfelder
    TEST_ASSERT_EQUAL_INT16(NBR_MH_RSSI_UNKNOWN, v.rssi);
    TEST_ASSERT_EQUAL_UINT8(0xFF, v.sec);

    // Sweep: N001 (Minute 11) verlaesst bei 731 das Fenster, sein Slot wird frei.
    nbrSweep(m, 731);
    TEST_ASSERT_EQUAL_INT(-1, slot_of(m, "N001"));
    TEST_ASSERT_EQUAL_INT(3, slot_of(m, nm(3)));
    // Ein verfallener, noch nicht gesweepter Slot (N002, Minute 12 -> bei 733
    // verfallen) wird stumm wiederverwendet -- sweep-unabhaengig.
    g_log[0] = '\0';
    heard(m, "DL7NEW-1", 733);
    nbrNoteDirect(m, "DL7NEW-1", info('!', -90, 1, true), 733);
    TEST_ASSERT_NULL(strstr(g_log, "EVICT-X"));
    TEST_ASSERT_EQUAL_INT(1, slot_of(m, "DL7NEW-1")); // vom Sweep frei
    heard(m, "DL8NEW-1", 733);
    nbrNoteDirect(m, "DL8NEW-1", info('!', -90, 1, true), 733);
    TEST_ASSERT_EQUAL_INT(2, slot_of(m, "DL8NEW-1")); // verfallen, ohne Sweep
    TEST_ASSERT_EQUAL_INT(-1, slot_of(m, "N002"));
    TEST_ASSERT_NULL(strstr(g_log, "EVICT-X"));
    free(mp);
}

// --- 3: MH-Sicht aus einer Rahmenfolge ------------------------------------------------------

void test_mh_view_fields_from_frame_sequence(void)
{
    NbrMatrix *mp = mk();
    NbrMatrix &m = *mp;
    // A sendet selbst eine Position (direkt).
    nbrNoteFrame(m, "DL1AAA-1", '!', NULL, false, -95, -7, 10);
    NbrDirectInfo d = info('!', -95, 17, true);
    d.hw = 43 | 0x80;
    d.fw = 't';
    d.has_pos = true;
    d.lat = 48.1f;
    d.lon = 11.5f;
    d.alt_m = 512;
    nbrNoteDirect(m, "DL1AAA-1", d, 10);
    // A wiederholt einen HEY von OE1XXX-1 an "HG": Slot bekommt '@', fw/pos bleiben.
    nbrNoteFrame(m, "OE1XXX-1,DL1AAA-1", '@', "R4;", true, -97, -9, 12);
    d = info('@', -97, 41, false);
    d.hw = 43;
    d.fw = 'p';     // fremder Stand, darf nicht uebernommen werden
    d.has_pos = true; // ebenso fremde Position
    d.lat = 1.0f;
    d.lon = 1.0f;
    d.pl = 2;
    d.mod = 0xF3;
    nbrNoteDirect(m, "DL1AAA-1", d, 12);
    // A hat mich gehoert (HN-Bericht nennt mich mit -9), meldet 7 Nachbarn.
    TEST_ASSERT_EQUAL_INT(1, nbrNoteReport(m, "DL1AAA-1", "R7;N1;DK5EN-98,-11;", 12));
    nbrNoteNcnt(m, "DL1AAA-1", 7, 12);
    // A hoert B und C, die ich nicht direkt hoere; D ist direkt, hoert nichts.
    nbrNoteFrame(m, "DB0BBB-1,DL1AAA-1", '!', NULL, false, -95, -7, 13);
    nbrNoteFrame(m, "DB0CCC-1,DL1AAA-1", '!', NULL, false, -95, -7, 13);
    heard(m, "DL4DDD-1", 13, 3);
    // Gateway-Flag fuer A: ein HEY von A selbst an "HG".
    nbrNoteFrame(m, "DL1AAA-1", '@', "R4;", true, -95, -7, 14);

    int ra = nbrFind(m, "DL1AAA-1");
    NbrMhView v;
    TEST_ASSERT_TRUE(nbrMhGet(m, ra, 20, &v));
    TEST_ASSERT_EQUAL_STRING("DL1AAA-1", v.call);
    TEST_ASSERT_EQUAL_UINT16(6, v.age_min);
    TEST_ASSERT_EQUAL_UINT8(41, v.sec);
    TEST_ASSERT_EQUAL_INT8('@', v.plt);
    TEST_ASSERT_EQUAL_UINT8(43, v.hw);
    TEST_ASSERT_EQUAL_UINT8(0xF3, v.mod);
    TEST_ASSERT_EQUAL_INT16(-97, v.rssi);
    TEST_ASSERT_TRUE(v.snr <= -7 && v.snr >= -9); // SNR-Mittel der Kante (A, ich)
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 48.1f, v.lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 11.5f, v.lon);
    TEST_ASSERT_EQUAL_INT16(512, v.alt);
    TEST_ASSERT_EQUAL_UINT8(2, v.pl);
    TEST_ASSERT_EQUAL_UINT8(1, v.mesh);
    TEST_ASSERT_EQUAL_UINT8(7, v.ncnt);
    TEST_ASSERT_EQUAL_INT8(-11, v.hm_snr);
    TEST_ASSERT_EQUAL_INT8('S', v.role); // #X 3 (OE1XXX-1, B, C), zweitgroesstes 0
    TEST_ASSERT_EQUAL_UINT8(3, v.ex);
    TEST_ASSERT_EQUAL_UINT8(4, v.nb); // OE1XXX-1, B, C und ich
    TEST_ASSERT_EQUAL_UINT8(1, v.gw);
    TEST_ASSERT_EQUAL_UINT8(0, v.via);
    TEST_ASSERT_EQUAL_INT8('t', v.fw);

    // D: direkt ohne Slot und ohne Beitrag.
    TEST_ASSERT_TRUE(nbrMhGet(m, nbrFind(m, "DL4DDD-1"), 20, &v));
    TEST_ASSERT_EQUAL_INT8('R', v.role);
    TEST_ASSERT_EQUAL_UINT8(0, v.ex);
    TEST_ASSERT_EQUAL_INT8(NBR_SNR_UNKNOWN, v.hm_snr);
    TEST_ASSERT_EQUAL_INT16(NBR_MH_RSSI_UNKNOWN, v.rssi);
    TEST_ASSERT_FALSE(nbrPosKnown(v.lat, v.lon));
    // B: keine Kante (B, ich) -> kein MHeard; Zeile 0 und ungueltige Indizes ebenso.
    TEST_ASSERT_FALSE(nbrMhGet(m, nbrFind(m, "DB0BBB-1"), 20, &v));
    TEST_ASSERT_FALSE(nbrMhGet(m, 0, 20, &v));
    TEST_ASSERT_FALSE(nbrMhGet(m, NBR_MAX_ROWS, 20, &v));
    TEST_ASSERT_FALSE(nbrMhGet(m, ra, 20, NULL));
    // Nach 12 h ohne Direktempfang: kein MHeard mehr.
    TEST_ASSERT_FALSE(nbrMhGet(m, ra, 14 + NBR_WINDOW_MIN, &v));

    // A bleibt 'S', wenn D #X 1 bekommt (3 >= 2*1), D wird 'N'.
    nbrNoteFrame(m, "DB0EEE-1,DL4DDD-1", '!', NULL, false, -95, -7, 15);
    nbrMhGet(m, ra, 20, &v);
    TEST_ASSERT_EQUAL_INT8('S', v.role);
    nbrMhGet(m, nbrFind(m, "DL4DDD-1"), 20, &v);
    TEST_ASSERT_EQUAL_INT8('N', v.role);
    TEST_ASSERT_EQUAL_UINT8(1, v.ex);
    // Mit #X 3 gegen 2 gibt es keinen Super-Node mehr (3 < 2*2).
    nbrNoteFrame(m, "DB0FFF-1,DL4DDD-1", '!', NULL, false, -95, -7, 15);
    nbrMhGet(m, ra, 20, &v);
    TEST_ASSERT_EQUAL_INT8('N', v.role);
    free(mp);
}

// --- 4: Reihenfolge und Zaehlung ------------------------------------------------------------

void test_mh_rows_order_newest_minute_then_second_then_index(void)
{
    NbrMatrix *mp = mk();
    NbrMatrix &m = *mp;
    heard(m, "X1AAA", 100);
    nbrNoteDirect(m, "X1AAA", info(':', -90, 30, true), 100);
    heard(m, "X2AAA", 105);
    nbrNoteDirect(m, "X2AAA", info(':', -90, 40, true), 105);
    heard(m, "X3AAA", 105);
    nbrNoteDirect(m, "X3AAA", info(':', -90, 10, true), 105);
    heard(m, "X4AAA", 105); // kein Slot: Sekunde unbekannt, zuletzt in seiner Minute
    heard(m, "X5AAA", 90);
    nbrNoteDirect(m, "X5AAA", info(':', -90, 59, true), 90);
    heard(m, "X6AAA", 105);
    nbrNoteDirect(m, "X6AAA", info(':', -90, 40, true), 105); // Gleichstand mit X2: Zeilenindex
    nbrNoteFrame(m, "X7AAA,X1AAA", '!', NULL, false, -90, 0, 106); // X7 nur 2 Hops: kein MHeard

    // X1 wurde bei 106 als letzter Hop erneut gehoert -> neueste Minute.
    const char *want2[] = {"X1AAA", "X2AAA", "X6AAA", "X3AAA", "X4AAA", "X5AAA"};
    uint8_t out[NBR_MAX_ROWS];
    int n = nbrMhRows(m, 110, 720, out, NBR_MAX_ROWS);
    TEST_ASSERT_EQUAL_INT(6, n);
    for (int i = 0; i < n; i++)
    {
        NbrRowView rv;
        nbrRowGet(m, out[i], &rv);
        TEST_ASSERT_EQUAL_STRING(want2[i], rv.call);
    }
    TEST_ASSERT_TRUE(nbrFind(m, "X2AAA") < nbrFind(m, "X6AAA"));
    // Abgeschnitten: Gesamtzahl bleibt, nur max geschrieben.
    uint8_t few[2] = {0xEE, 0xEE};
    TEST_ASSERT_EQUAL_INT(6, nbrMhRows(m, 110, 720, few, 1));
    TEST_ASSERT_EQUAL_UINT8(nbrFind(m, "X1AAA"), few[0]);
    TEST_ASSERT_EQUAL_UINT8(0xEE, few[1]);
    TEST_ASSERT_EQUAL_INT(6, nbrMhRows(m, 110, 720, NULL, 0));
    // Fenster: juenger als 10 min bei 110 -> nur 106 und 105.
    TEST_ASSERT_EQUAL_INT(5, nbrMhRows(m, 110, 10, out, NBR_MAX_ROWS));
    TEST_ASSERT_EQUAL_INT(5, nbrMhCount(m, 110, 10));
    TEST_ASSERT_EQUAL_INT(1, nbrMhCount(m, 110, 5)); // nur X1 (Alter 4)
    free(mp);
}

// Schatten-Analogon zu getMheardCount(): MHeard-Schluessel = letzter Hop,
// eigenes Echo nicht, gezaehlt wird, was juenger als 60 min ist. Die Folge
// enthaelt Text, POS, HEY, Relays, einen HN-Bericht und einen wegen LOOP
// verworfenen Rahmen (MHeard zaehlt ihn, seit Stufe 2 auch die Topologie).
void test_mh_count_matches_one_sided_mheard_count(void)
{
    struct Fr
    {
        const char *path;
        char type;
        uint16_t t;
    } fr[] = {
        {"DL1AAA-1", ':', 5},          {"OE1XXX-1,DL2BBB-2", '!', 20}, {"DL3CCC-3", '@', 30},
        {"DK5EN-98,DL4DDD-4", '!', 31}, {"OE2YYY-1,OE3ZZZ-1,DL5EEE-5", '@', 40},
        {"DL6FFF-6,DK5EN-98", ':', 41}, {"DL7GGG-7,DL8HHH-8,DL7GGG-7", '!', 50},
        {"DL1AAA-1", '!', 70},          {"DL9III-9", ':', 95},
    };
    const int nfr = (int)(sizeof(fr) / sizeof(fr[0]));
    NbrMatrix *mp = mk();
    NbrMatrix &m = *mp;
    for (int i = 0; i < nfr; i++)
        nbrNoteFrame(m, fr[i].path, fr[i].type, "R2;", false, -90, 0, fr[i].t);
    // HN-Bericht eines direkt gehoerten Knotens: aendert keine Kante (x, 0).
    TEST_ASSERT_TRUE(nbrNoteReport(m, "DL3CCC-3", "R2;N1;DL1AAA-1,-5;", 95) >= 0);

    for (uint16_t now = 30; now <= 160; now += 7)
    {
        // Handmodell: letzter Hop -> juengste Minute.
        const char *calls[16];
        uint16_t last[16];
        int nc = 0;
        for (int i = 0; i < nfr && fr[i].t <= now; i++)
        {
            const char *p = strrchr(fr[i].path, ',');
            const char *lh = p ? p + 1 : fr[i].path;
            if (strcmp(lh, OWN) == 0)
                continue;
            int k = 0;
            while (k < nc && strcmp(calls[k], lh) != 0)
                k++;
            if (k == nc)
                calls[nc++] = lh;
            last[k] = fr[i].t;
        }
        int want = 0;
        for (int k = 0; k < nc; k++)
            if ((uint16_t)(now - last[k]) < 60)
                want++;
        if (now >= 95)
            TEST_ASSERT_EQUAL_INT(want, nbrMhCount(m, now, 60));
    }
    free(mp);
}

// --- 5: NCNT -------------------------------------------------------------------------------------

void test_ncnt_d60_hm_sym_veto_and_cap(void)
{
    NbrMatrix *mp = mk();
    NbrMatrix &m = *mp;
    heard(m, "X1AAA", 100, 5);   // SYM (>= -16), ohne Beweis
    heard(m, "X2AAA", 100, -20); // weder SYM noch HM
    heard(m, "X3AAA", 100, -20); // HM:
    nbrNoteFrame(m, "DK5EN-98,X3AAA", '!', NULL, false, -90, -20, 100);
    heard(m, "X4AAA", 30, 5);    // ausserhalb D60 bei 100
    nbrNoteFrame(m, "DK5EN-98,X4AAA", '!', NULL, false, -90, 5, 30);
    TEST_ASSERT_EQUAL_INT(2, nbrNcnt(m, 100)); // X1, X3
    TEST_ASSERT_EQUAL_INT(2, nbrNcntAir(m, 100));
    TEST_ASSERT_EQUAL_INT(3, nbrMhCount(m, 100, 60)); // einseitig: X1..X3

    // VETO: vollstaendiger Bericht von X1, der mich nicht nennt.
    TEST_ASSERT_EQUAL_INT(0, nbrNoteReport(m, "X1AAA", "R3;N1;ZZ9ZZ,-5;", 100));
    TEST_ASSERT_EQUAL_INT(1, nbrNcnt(m, 100));
    TEST_ASSERT_EQUAL_INT(1, nbrNcnt(m, 144)); // 44 min: Bericht gilt noch
    TEST_ASSERT_EQUAL_INT(2, nbrNcnt(m, 145)); // 45 min: veraltet
    // Ein abgeschnittener Bericht ('+') vetoet nicht.
    TEST_ASSERT_EQUAL_INT(0, nbrNoteReport(m, "X1AAA", "R3;N1+;ZZ9ZZ,-5;", 101));
    TEST_ASSERT_EQUAL_INT(2, nbrNcnt(m, 101));
    // Ein vollstaendiger Bericht, der mich nennt, vetoet nicht.
    nbrNoteReport(m, "X1AAA", "R3;N1;DK5EN-98,-5;", 102);
    TEST_ASSERT_EQUAL_INT(2, nbrNcnt(m, 102));
    // X3: Beweis vor dem Bericht, Bericht ohne mich -> Veto; ein spaeterer
    // Beweis (X3 wiederholt mich) hebt es auf.
    nbrNoteReport(m, "X3AAA", "R3;N0;", 103);
    TEST_ASSERT_EQUAL_INT(1, nbrNcnt(m, 103));
    nbrNoteFrame(m, "DK5EN-98,X3AAA", '!', NULL, false, -90, -20, 104);
    TEST_ASSERT_EQUAL_INT(2, nbrNcnt(m, 104));
    // D60: X1 und X3 zuletzt bei 100/104 direkt gehoert; bei 164 ist X3 noch drin.
    TEST_ASSERT_EQUAL_INT(1, nbrNcnt(m, 163));
    TEST_ASSERT_EQUAL_INT(0, nbrNcnt(m, 170));
    free(mp);
}

void test_ncnt_sym_hysteresis_enter_minus16_leave_below_minus18(void)
{
    NbrMatrix *mp = mk();
    NbrMatrix &m = *mp;
    heard(m, "X1AAA", 100, -17);
    nbrNoteDirect(m, "X1AAA", info(':', -110, 1, true), 100);
    int x = nbrFind(m, "X1AAA");
    TEST_ASSERT_EQUAL_INT(0, nbrNcnt(m, 100)); // -17: nicht an
    struct
    {
        int8_t snr;
        int want;
    } steps[] = {{-16, 1}, {-17, 1}, {-18, 1}, {-19, 0}, {-17, 0}, {-18, 0}, {-16, 1}, {-30, 0}, {10, 1}};
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++)
    {
        set_edge_snr(m, x, 0, steps[i].snr);
        nbrNoteDirect(m, "X1AAA", info(':', -110, 1, true), 100); // fuehrt die Hysterese nach
        TEST_ASSERT_EQUAL_INT_MESSAGE(steps[i].want, nbrNcnt(m, 100), "SYM-Hysterese");
    }
    // Der ME-Schritt fuehrt sie ebenso nach (SNR-Mittel faellt unter -18).
    for (int k = 0; k < 20; k++)
        heard(m, "X1AAA", 101, -40);
    TEST_ASSERT_EQUAL_INT(0, nbrNcnt(m, 101));
    // Ohne Slot (verdraengt) gilt die Schwelle ohne Hysterese.
    m.row[x].ext = 0xFF;
    set_edge_snr(m, x, 0, -16);
    TEST_ASSERT_EQUAL_INT(1, nbrNcnt(m, 101));
    set_edge_snr(m, x, 0, -17);
    TEST_ASSERT_EQUAL_INT(0, nbrNcnt(m, 101));
    free(mp);
}

void test_ncnt_uncapped_and_air_cap(void)
{
    NbrMatrix *mp = mk();
    NbrMatrix &m = *mp;
    for (int i = 1; i < NBR_MAX_ROWS; i++)
        heard(m, nm(i), 50, 0);
    TEST_ASSERT_EQUAL_INT(NBR_MAX_ROWS - 1, nbrNcnt(m, 50));
    int want_air = (NBR_MAX_ROWS - 1 > NBR_NCNT_AIR_MAX) ? NBR_NCNT_AIR_MAX : NBR_MAX_ROWS - 1;
    TEST_ASSERT_EQUAL_INT(want_air, nbrNcntAir(m, 50));
#if NBR_MAX_ROWS == 128
    TEST_ASSERT_EQUAL_INT(127, nbrNcnt(m, 50));
    TEST_ASSERT_EQUAL_INT(99, nbrNcntAir(m, 50));
#endif
    free(mp);
}

void test_note_ncnt_writes_row_core_direct_or_indirect(void)
{
    NbrMatrix *mp = mk();
    NbrMatrix &m = *mp;
    nbrNoteFrame(m, "OE1AAA-1,DL1BBB-1", '!', NULL, false, -90, 0, 5); // OE1AAA-1: Zeile, nicht direkt
    nbrNoteNcnt(m, "OE1AAA-1", 12, 5);
    nbrNoteNcnt(m, "DL1BBB-1", 300, 5);
    nbrNoteNcnt(m, "DL1BBB-1", -1, 5); // ungueltig: bleibt
    nbrNoteNcnt(m, "DL9ZZZ-9", 4, 5);  // ohne Zeile: folgenlos
    NbrRowView v;
    nbrRowGet(m, nbrFind(m, "OE1AAA-1"), &v);
    TEST_ASSERT_EQUAL_UINT8(12, v.ncnt);
    nbrRowGet(m, nbrFind(m, "DL1BBB-1"), &v);
    TEST_ASSERT_EQUAL_UINT8(255, v.ncnt);
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "DL9ZZZ-9"));
    free(mp);
}

// --- 6: Horizont (Konzept 4.7) --------------------------------------------------------------------

void test_horizon_rules_positive_and_negative(void)
{
    NbrMatrix *mp = mk();
    NbrMatrix &m = *mp;
    NbrRouteView rv;

    // + ab 3 Token, Absender ohne Zeile: Eintrag ueber die Eintrittszeile A.
    nbrNoteFrame(m, "S1AAA,A1AAA,B1AAA", '!', NULL, false, -90, 0, 10);
    int h = hz_index(m, "S1AAA");
    TEST_ASSERT_TRUE(h >= 0);
    TEST_ASSERT_TRUE(nbrMaskTest(m.hz_entry[h], nbrFind(m, "A1AAA")));
    TEST_ASSERT_EQUAL_UINT8(1, nbrPrivHzHops(m.hz_meta[h], 10));
    TEST_ASSERT_TRUE(route_find(m, "S1AAA", 10, &rv) >= 0);
    TEST_ASSERT_EQUAL_UINT8(0, rv.is_row);
    TEST_ASSERT_EQUAL_UINT8(3, rv.hops);
    TEST_ASSERT_EQUAL_UINT8(0, rv.gw);
    // - 2 Token: der Absender liegt im Fenster und bekommt eine Zeile.
    nbrNoteFrame(m, "S2AAA,B1AAA", '!', NULL, false, -90, 0, 10);
    TEST_ASSERT_EQUAL_INT(-1, hz_index(m, "S2AAA"));
    // - Eintrittstoken bin ich (Echo eines eigenen Relays).
    nbrNoteFrame(m, "S3AAA,DK5EN-98,B1AAA", '!', NULL, false, -90, 0, 10);
    TEST_ASSERT_EQUAL_INT(-1, hz_index(m, "S3AAA"));
    // - Absender mit frischer Zeile (+ mit verfallener Zeile: am Ende).
    heard(m, "S4AAA", 10);
    nbrNoteFrame(m, "S4AAA,A1AAA,B1AAA", '@', NULL, true, -90, 0, 11);
    TEST_ASSERT_EQUAL_INT(-1, hz_index(m, "S4AAA"));
    // - Text: kein Eintrag; + dieselbe Kette als HEY.
    nbrNoteFrame(m, "S5AAA,A1AAA,B1AAA", ':', NULL, false, -90, 0, 12);
    TEST_ASSERT_EQUAL_INT(-1, hz_index(m, "S5AAA"));
    nbrNoteFrame(m, "S5AAA,A1AAA,B1AAA", '@', NULL, false, -90, 0, 12);
    TEST_ASSERT_TRUE(hz_index(m, "S5AAA") >= 0);
    // - verworfener Frame (LOOP): kein Eintrag.
    nbrNoteFrame(m, "S6AAA,A1AAA,S6AAA,B1AAA", '!', NULL, false, -90, 0, 12);
    TEST_ASSERT_EQUAL_INT(-1, hz_index(m, "S6AAA"));
    // Bekommt der Absender spaeter eine Zeile, wird sein Eintrag frei.
    nbrNoteFrame(m, "S5AAA,B1AAA", '!', NULL, false, -90, 0, 13);
    TEST_ASSERT_EQUAL_INT(-1, hz_index(m, "S5AAA"));
    heard(m, "S1AAA", 14); // ME-Schritt (Text) ebenso
    TEST_ASSERT_EQUAL_INT(-1, hz_index(m, "S1AAA"));
    // Mehrere Eintrittszeilen sammeln sich in der Maske.
    nbrNoteFrame(m, "S7AAA,A1AAA,B1AAA", '!', NULL, false, -90, 0, 15);
    nbrNoteFrame(m, "S7AAA,Q1QQQ,A2AAA,B1AAA", '!', NULL, false, -90, 0, 16);
    h = hz_index(m, "S7AAA");
    TEST_ASSERT_EQUAL_INT(2, nbrMaskCount(m.hz_entry[h]));
    TEST_ASSERT_EQUAL_UINT8(1, nbrPrivHzHops(m.hz_meta[h], 16)); // Minimum 1 und 2
    // Sweep: nach NBR_WINDOW_MIN ohne Rahmen frei.
    nbrSweep(m, 16 + NBR_WINDOW_MIN);
    TEST_ASSERT_EQUAL_INT(-1, hz_index(m, "S7AAA"));
    // + Absender mit verfallener Zeile (S4AAA zuletzt bei 10 gehoert).
    nbrNoteFrame(m, "S4AAA,A1AAA,B1AAA", '@', NULL, true, -90, 0, 20 + NBR_WINDOW_MIN);
    h = hz_index(m, "S4AAA");
    TEST_ASSERT_TRUE(h >= 0);
    TEST_ASSERT_EQUAL_UINT8(1, m.hz_meta[h][1] & 1); // G aus '@' an "HG"
    TEST_ASSERT_TRUE(route_find(m, "S4AAA", 20 + NBR_WINDOW_MIN, NULL) >= 0);
    free(mp);
}

void test_horizon_eviction_longest_unseen(void)
{
    NbrMatrix *mp = mk();
    NbrMatrix &m = *mp;
    char path[64];
    for (int i = 0; i < NBR_HZ_ENTRIES; i++)
    {
        snprintf(path, sizeof(path), "H%03d,A1AAA,B1AAA", i);
        nbrNoteFrame(m, path, '!', NULL, false, -90, 0, (uint16_t)(100 + i));
    }
    // H000 wieder gesehen: jetzt ist H001 der am laengsten nicht gesehene.
    nbrNoteFrame(m, "H000,A1AAA,B1AAA", '!', NULL, false, -90, 0, 300);
    logstart();
    nbrNoteFrame(m, "HNEWX,A1AAA,B1AAA", '!', NULL, false, -90, 0, 301);
    TEST_ASSERT_NOT_NULL(strstr(g_log, "[NBR]|EVICT-H|301|H001|HNEWX\n"));
    TEST_ASSERT_EQUAL_INT(-1, hz_index(m, "H001"));
    TEST_ASSERT_TRUE(hz_index(m, "HNEWX") >= 0);
    TEST_ASSERT_TRUE(hz_index(m, "H000") >= 0);
    // Erneuter Rahmen eines bestehenden Eintrags verdraengt nichts.
    g_log[0] = '\0';
    nbrNoteFrame(m, "H005,A1AAA,B1AAA", '!', NULL, false, -90, 0, 302);
    TEST_ASSERT_NULL(strstr(g_log, "EVICT-H"));
    free(mp);
}

void test_horizon_row_eviction_clears_entry_masks(void)
{
    NbrMatrix *mp = mk();
    NbrMatrix &m = *mp;
    nbrNoteFrame(m, "S1AAA,A1AAA,B1AAA", '!', NULL, false, -90, 0, 1);
    nbrNoteFrame(m, "S2AAA,A1AAA,B1AAA", '!', NULL, false, -90, 0, 1);
    nbrNoteFrame(m, "S2AAA,A2AAA,B1AAA", '!', NULL, false, -90, 0, 1);
    // Tabelle vollmachen, bis A1AAA (aelteste, nicht direkt) weicht.
    for (int i = 0; nbrFind(m, "A1AAA") >= 0 && i < 4 * NBR_MAX_ROWS; i++)
        heard(m, nm(i), (uint16_t)(10 + i));
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "A1AAA"));
    TEST_ASSERT_EQUAL_INT(-1, hz_index(m, "S1AAA")); // einzige Eintrittszeile weg -> frei
    int h = hz_index(m, "S2AAA");
    if (h >= 0) // A2AAA kann ebenfalls gewichen sein; dann ist S2 ebenso frei
    {
        TEST_ASSERT_EQUAL_INT(1, nbrMaskCount(m.hz_entry[h]));
        TEST_ASSERT_TRUE(nbrMaskTest(m.hz_entry[h], nbrFind(m, "A2AAA")));
    }
    for (int k = 0; k < NBR_HZ_ENTRIES; k++)
        if (m.hz_call[k])
            for (int i = nbrMaskNext(m.hz_entry[k], -1); i >= 0; i = nbrMaskNext(m.hz_entry[k], i))
                TEST_ASSERT_TRUE(nbrIUsed(m, i));
    free(mp);
}

void test_horizon_hop_minimum_rises_after_12h(void)
{
    NbrMatrix *mp = mk();
    NbrMatrix &m = *mp;
    NbrRouteView rv;
    // Minute 10 (Epoche 0): 1 Hop bis zur Eintrittszeile.
    nbrNoteFrame(m, "S1AAA,A1AAA,B1AAA", '!', NULL, false, -90, 0, 10);
    // Minute 370 (Epoche 1): 3 Hops -> Minimum ueber beide Epochen bleibt 1.
    nbrNoteFrame(m, "S1AAA,Q1QQQ,Q2QQQ,A1AAA,B1AAA", '!', NULL, false, -90, 0, 370);
    route_find(m, "S1AAA", 370, &rv);
    TEST_ASSERT_EQUAL_UINT8(1 + 2, rv.hops);
    // Minute 730 (Epoche 2): die 1 aus Epoche 0 ist verfallen -> 3.
    nbrNoteFrame(m, "S1AAA,Q1QQQ,Q2QQQ,A1AAA,B1AAA", '!', NULL, false, -90, 0, 730);
    route_find(m, "S1AAA", 730, &rv);
    TEST_ASSERT_EQUAL_UINT8(3 + 2, rv.hops);
    // In derselben Epoche sinkt es sofort wieder.
    nbrNoteFrame(m, "S1AAA,A1AAA,B1AAA", '!', NULL, false, -90, 0, 731);
    route_find(m, "S1AAA", 731, &rv);
    TEST_ASSERT_EQUAL_UINT8(1 + 2, rv.hops);
    TEST_ASSERT_EQUAL_UINT16(4, route_find(m, "S1AAA", 735, &rv) >= 0 ? rv.age_min : 0xFFFF);
    free(mp);
}

// --- 7: Wege ----------------------------------------------------------------------------------------

void test_routes_two_hop_rows_then_horizon(void)
{
    NbrMatrix *mp = mk();
    NbrMatrix &m = *mp;
    heard(m, "B2AAA", 50);
    nbrNoteFrame(m, "X1AAA,B1AAA", '!', NULL, false, -90, 0, 90); // (X1,B1) 90
    nbrNoteFrame(m, "X1AAA,B2AAA", '!', NULL, false, -90, 0, 95); // (X1,B2) 95
    heard(m, "B1AAA", 100);
    nbrNoteFrame(m, "X2AAA,B1AAA", '@', NULL, true, -90, 0, 100);  // X2 an "HG"; (B1,0) 100
    nbrNoteFrame(m, "S1AAA,A1AAA,B2AAA", '@', NULL, true, -90, 0, 101); // (B2,0) 101
    // A1AAA ist selbst eine 2-Hop-Zeile (Fenster-Token, von B2 gehoert).
    int n = nbrRouteCount(m, 110);
    TEST_ASSERT_EQUAL_INT(4, n); // X1, X2, A1 als Zeilen, S1 als Horizont
    NbrRouteView v;
    // Reihenfolge: Zeilen (nach Index), dann Horizont.
    TEST_ASSERT_TRUE(nbrRouteGet(m, n - 1, 110, &v));
    TEST_ASSERT_EQUAL_STRING("S1AAA", v.call);
    TEST_ASSERT_EQUAL_UINT8(0, v.is_row);
    TEST_ASSERT_EQUAL_UINT8(1, v.gw);
    TEST_ASSERT_EQUAL_UINT16(9, v.age_min);
    TEST_ASSERT_TRUE(nbrMaskTest(v.entry, nbrFind(m, "A1AAA")));
    TEST_ASSERT_FALSE(nbrRouteGet(m, n, 110, &v));
    TEST_ASSERT_FALSE(nbrRouteGet(m, -1, 110, &v));

    TEST_ASSERT_TRUE(route_find(m, "X1AAA", 110, &v) >= 0);
    TEST_ASSERT_EQUAL_UINT8(1, v.is_row);
    TEST_ASSERT_EQUAL_UINT8(2, v.hops);
    TEST_ASSERT_EQUAL_UINT8(0, v.gw);
    TEST_ASSERT_EQUAL_INT(2, nbrMaskCount(v.entry));
    // Wege: ueber B1 max(20, 10) = 20, ueber B2 max(15, 9) = 15 -> 15.
    TEST_ASSERT_EQUAL_UINT16(15, v.age_min);
    TEST_ASSERT_TRUE(route_find(m, "X2AAA", 110, &v) >= 0);
    TEST_ASSERT_EQUAL_UINT8(1, v.gw);
    // Direkte Zeilen erscheinen nicht.
    TEST_ASSERT_EQUAL_INT(-1, route_find(m, "B1AAA", 110, NULL));
    // X1 wird direkt: faellt aus den Wegen.
    heard(m, "X1AAA", 111);
    TEST_ASSERT_EQUAL_INT(-1, route_find(m, "X1AAA", 111, NULL));
    TEST_ASSERT_EQUAL_INT(3, nbrRouteCount(m, 111));
    free(mp);
}

// Advisor Welle 3, R1 (scratchpad/advisor-w3/repro_twice.cpp): ein Absender,
// dessen Zeilenminute verfallen ist, dessen Kante (X, B) aber frisch ist (HN-
// Bericht von B), bekam zusaetzlich einen Horizont-Eintrag und stand in den
// Wegen zweimal -- als 2-Hop-Zeile UND als Horizont. Der Sweep half nicht.
static int route_count_of(const NbrMatrix &m, const char *call, uint16_t now)
{
    int n = nbrRouteCount(m, now), hits = 0;
    for (int i = 0; i < n; i++)
    {
        NbrRouteView v;
        if (nbrRouteGet(m, i, now, &v) && strcmp(v.call, call) == 0)
            hits++;
    }
    return hits;
}

void test_route_sender_never_listed_as_row_and_horizon(void)
{
    NbrMatrix *mp = mk();
    NbrMatrix &m = *mp;
    NbrRouteView v;
    nbrNoteFrame(m, "X1AAA,B1AAA", '!', NULL, false, -90, 0, 0); // X 2-Hop ueber B
    nbrNoteFrame(m, "B1AAA", ':', NULL, false, -90, 0, 100);
    nbrNoteFrame(m, "B1AAA", ':', NULL, false, -90, 0, 800);
    // B meldet X: eine Beobachtung ueber X -> Zeile X ist wieder frisch.
    TEST_ASSERT_EQUAL_INT(1, nbrNoteReport(m, "B1AAA", "R1;N1;X1AAA,-5;", 800));
    TEST_ASSERT_EQUAL_UINT16(800, m.row[nbrFind(m, "X1AAA")].last_min);
    // X kommt jetzt auch ueber 3 Hops: kein Horizont-Eintrag, X ist 2-Hop-Zeile.
    nbrNoteFrame(m, "X1AAA,A1AAA,B1AAA", '!', NULL, false, -90, 0, 801);
    TEST_ASSERT_EQUAL_INT(-1, hz_index(m, "X1AAA"));
    TEST_ASSERT_EQUAL_INT(1, route_count_of(m, "X1AAA", 801));
    TEST_ASSERT_TRUE(route_find(m, "X1AAA", 801, &v) >= 0);
    TEST_ASSERT_EQUAL_UINT8(1, v.is_row);
    TEST_ASSERT_EQUAL_UINT8(2, v.hops);
    nbrSweep(m, 802);
    TEST_ASSERT_EQUAL_INT(1, route_count_of(m, "X1AAA", 802));

    // Dieselbe Lage ohne Bericht: Zeilenminute verfallen, nur eine Regel-3-
    // Kante (X, B) aus einem Rahmen vor dem Fenster haelt X als 2-Hop-Zeile.
    NbrMatrix *np = mk();
    NbrMatrix &n = *np;
    nbrNoteFrame(n, "X1AAA,B1AAA", '!', NULL, false, -90, 0, 0);
    nbrNoteFrame(n, "B1AAA", ':', NULL, false, -90, 0, 800);
    nbrNoteFrame(n, "X1AAA,B1AAA,C1AAA,D1AAA", '!', NULL, false, -90, 0, 800); // (X,B) vor dem Fenster
    TEST_ASSERT_TRUE(nbrFresh(nbrIEdgeFind(n, nbrFind(n, "X1AAA"), nbrFind(n, "B1AAA")) >= 0
                                  ? n.edge[nbrIEdgeFind(n, nbrFind(n, "X1AAA"), nbrFind(n, "B1AAA"))].last_min
                                  : 0,
                              800));
    TEST_ASSERT_FALSE(nbrFresh(n.row[nbrFind(n, "X1AAA")].last_min, 801)); // Zeilenminute alt
    nbrNoteFrame(n, "X1AAA,A1AAA,B1AAA", '!', NULL, false, -90, 0, 801);
    TEST_ASSERT_EQUAL_INT(-1, hz_index(n, "X1AAA"));
    TEST_ASSERT_EQUAL_INT(1, route_count_of(n, "X1AAA", 801));
    // Letzte Sicherung in der Sicht: ein Horizont-Eintrag fuer einen Absender,
    // der gerade 2-Hop-Zeile ist, erscheint nicht (Eintrag von Hand gesetzt).
    uint16_t now = 801;
    nbrIHzTouch(n, nbrCallEncode("X1AAA"), "X1AAA", nbrFind(n, "A1AAA"), 1, false, false, now, NULL);
    TEST_ASSERT_TRUE(hz_index(n, "X1AAA") >= 0);
    TEST_ASSERT_EQUAL_INT(1, route_count_of(n, "X1AAA", now));
    route_find(n, "X1AAA", now, &v);
    TEST_ASSERT_EQUAL_UINT8(1, v.is_row);

    // Gegenprobe: eine alte Zeile ohne frische Kante (nur per nbrNotePos
    // verjuengt) zaehlt nicht als Zeile -- der Absender bekommt den Eintrag.
    nbrNotePos(n, "Y1AAA", 48.0f, 11.0f, false, 3, 801); // ohne Zeile: folgenlos
    nbrNoteFrame(n, "Y1AAA,B1AAA", '!', NULL, false, -90, 0, 1);
    nbrNotePos(n, "Y1AAA", 48.0f, 11.0f, false, 3, 801); // Zeilenminute frisch, Kanten alt
    nbrNoteFrame(n, "Y1AAA,A2AAA,B1AAA", '!', NULL, false, -90, 0, 802);
    TEST_ASSERT_TRUE(hz_index(n, "Y1AAA") >= 0);
    TEST_ASSERT_EQUAL_INT(1, route_count_of(n, "Y1AAA", 802));
    free(np);
    free(mp);
}

// --- 8: Namen ----------------------------------------------------------------------------------------

// Kopie der alten Tabelle aus src/mheard_functions.cpp (Nicht-T-Deck-Zweig)
// und der Alt-ID-Uebersetzung aus getHardwareLong(), unabhaengig nachgebaut.
static const char *old_hw_table[36] = {"no info", "TLORA_V2", "TLORA_V1", "TLORA_V2_1_1p6", "TBEAM", "TBEAM_1268", "TBEAM_0p7", "T_ECHO", "TDECK", "RAK4631", "HELTEC_V2_1", "HELTEC_V1", "TBEAM_AXP2101", "EBYTE_E22", "HELTEC_V3", "HELTEC_E290", "TBEAM_1262", "TDECK_PLUS", "TBEAM_SUPREME", "ESP_S3_E22", "TRACK_V3", "STICK_V3", "T5_EPAPER", "TPAGER", "TDECKpro", "TBEAM_1W", "HELTEC_V4", "T_ETH_ELITE", "HELTEC_T114", "T3_S3_V13", "T_CON_PRO", "WIRELESS_PAPER", "HELTEC_E213", "ESP32_LORAPRS_E22", "ESP32_LORAPRS_RA01", "T_WATCH_S3"};

static const char *old_hw_long(uint8_t hwid)
{
    int ihw = hwid;
    if(ihw == 39) ihw=13;
    if(ihw == 40) ihw=22;
    if(ihw == 41) ihw=20;
    if(ihw == 42) ihw=21;
    if(ihw == 43) ihw=14;
    if(ihw == 44) ihw=15;
    if(ihw == 45) ihw=16;
    if(ihw == 46) ihw=17;
    if(ihw == 47) ihw=18;
    if(ihw == 48) ihw=19;
    if(ihw == 49) ihw=23;
    if(ihw == 50) ihw=24;
    if(ihw == 51) ihw=25;
    if(ihw == 52) ihw=26;
    if(ihw == 53) ihw=27;
    if(ihw == 54) ihw=28;
    if(ihw == 55) ihw=29;
    if(ihw == 56) ihw=30;
    if(ihw == 57) ihw=31;
    if(ihw == 58) ihw=32;
    if(ihw == 59) ihw=33;
    if(ihw == 60) ihw=34;
    if(ihw == 61) ihw=35;
    if(ihw < 0 || ihw >= 36) ihw=0;
    return old_hw_table[ihw];
}

void test_name_helpers_match_old_tables(void)
{
    for (int hw = 0; hw < 256; hw++)
    {
        char msg[32];
        snprintf(msg, sizeof(msg), "hw %d", hw);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(old_hw_long((uint8_t)hw), nbrHardwareName((uint8_t)hw), msg);
    }
    TEST_ASSERT_EQUAL_STRING("TXT", nbrPayloadTypeName(':'));
    TEST_ASSERT_EQUAL_STRING("POS", nbrPayloadTypeName('!'));
    TEST_ASSERT_EQUAL_STRING("HEY", nbrPayloadTypeName('@'));
    TEST_ASSERT_EQUAL_STRING("???", nbrPayloadTypeName('X'));
    TEST_ASSERT_EQUAL_STRING("???", nbrPayloadTypeName(0));
}

// --- 9: Echo-Tabelle -------------------------------------------------------------------------------

static uint32_t slot_field(const NbrMatrix &m, const char *call, unsigned off, unsigned w)
{
    int s = slot_of(m, call);
    TEST_ASSERT_TRUE(s >= 0);
    return nbrBitsGet(m.ext[s], off, w);
}

void test_echo_table_first_second_hand_and_fold(void)
{
    NbrMatrix *mp = mk();
    NbrMatrix &m = *mp;
    const char *nb[] = {"A1AAA", "B1AAA", "C1AAA"};
    for (int i = 0; i < 3; i++)
    {
        heard(m, nb[i], 90);
        nbrNoteDirect(m, nb[i], info(':', -90, 1, true), 90);
    }
    nbrNoteOwnTx(m, 0xAABBCC01u, '!', 100);
    nbrNoteOwnTx(m, 0xAABBCC02u, ':', 100); // Text: keine Echo-Zeile
    nbrNoteFrame(m, "DK5EN-98,A1AAA", '!', NULL, false, -90, 0, 101);
    nbrNoteFrame(m, "DK5EN-98,B1AAA,C1AAA", '!', NULL, false, -90, 0, 102);
    nbrNoteFrame(m, "DK5EN-98,C1AAA", '@', NULL, false, -90, 0, 102); // anderer Typ: nichts
    nbrNoteFrame(m, "DK5EN-98,A1AAA,B1AAA", '!', NULL, false, -90, 0, 104);
    int k = -1;
    for (int i = 0; i < 4; i++)
        if (m.echo_type[i])
            k = i;
    TEST_ASSERT_TRUE(k >= 0);
    TEST_ASSERT_EQUAL_INT(2, nbrMaskCount(m.echo_first[k])); // A, B
    TEST_ASSERT_EQUAL_INT(2, nbrMaskCount(m.echo_second[k])); // C, B
    // Nach 5 min in die Zaehler gefaltet, ECHO-Zeile.
    logstart();
    nbrNoteFrame(m, "DK5EN-98,C1AAA", '!', NULL, false, -90, 0, 105); // zu spaet: nicht gezaehlt
    nbrSweep(m, 105);
    char want[128];
    NbrMask f = nbrMaskNone(), s2 = nbrMaskNone();
    nbrMaskSet(f, nbrFind(m, "A1AAA"));
    nbrMaskSet(f, nbrFind(m, "B1AAA"));
    nbrMaskSet(s2, nbrFind(m, "C1AAA"));
    nbrMaskSet(s2, nbrFind(m, "B1AAA"));
    char fh[NBR_MASK_HEX_LEN + 1], sh[NBR_MASK_HEX_LEN + 1];
    nbrMaskHex(f, fh, sizeof(fh));
    nbrMaskHex(s2, sh, sizeof(sh));
    snprintf(want, sizeof(want), "[NBR]|ECHO|105|AABBCC01|%s|%s\n", fh, sh);
    TEST_ASSERT_NOT_NULL(strstr(g_log, want));
    TEST_ASSERT_EQUAL_UINT32(1, slot_field(m, "A1AAA", NBR_XO_F, NBR_XW_F));
    TEST_ASSERT_EQUAL_UINT32(0, slot_field(m, "A1AAA", NBR_XO_S, NBR_XW_S));
    TEST_ASSERT_EQUAL_UINT32(1, slot_field(m, "B1AAA", NBR_XO_F, NBR_XW_F)); // erste Hand schlaegt zweite
    TEST_ASSERT_EQUAL_UINT32(0, slot_field(m, "B1AAA", NBR_XO_S, NBR_XW_S));
    TEST_ASSERT_EQUAL_UINT32(0, slot_field(m, "C1AAA", NBR_XO_F, NBR_XW_F));
    TEST_ASSERT_EQUAL_UINT32(1, slot_field(m, "C1AAA", NBR_XO_S, NBR_XW_S));
    for (int i = 0; i < 4; i++)
        TEST_ASSERT_EQUAL_UINT8(0, m.echo_type[i]);

    // Vier Eintraege; ein fuenfter verdraengt den aeltesten (ECHO sofort).
    g_log[0] = '\0';
    for (uint32_t i = 0; i < 5; i++)
        nbrNoteOwnTx(m, 0x100u + i, '@', (uint16_t)(200 + (i < 4 ? 0 : 1)));
    TEST_ASSERT_EQUAL_INT(1, count_substr(g_log, "[NBR]|ECHO|201|00000100|"));
    // Derselbe msg_id noch einmal: kein neuer Eintrag.
    g_log[0] = '\0';
    nbrNoteOwnTx(m, 0x104u, '@', 201);
    TEST_ASSERT_EQUAL_INT(0, count_substr(g_log, "ECHO"));

    // Saettigung: f + s bleibt unter 16 (Halbierung bei 15).
    for (int r = 0; r < 20; r++)
    {
        uint16_t t = (uint16_t)(300 + r * 10);
        nbrNoteOwnTx(m, 0x200u + r, '!', t);
        nbrNoteFrame(m, "DK5EN-98,A1AAA", '!', NULL, false, -90, 0, t);
        nbrNoteDirect(m, "A1AAA", info('!', -90, 1, true), t);
        nbrSweep(m, (uint16_t)(t + 5));
        uint32_t fa = slot_field(m, "A1AAA", NBR_XO_F, NBR_XW_F);
        uint32_t sa = slot_field(m, "A1AAA", NBR_XO_S, NBR_XW_S);
        TEST_ASSERT_TRUE(fa + sa <= 15);
        if (r >= 14)
            TEST_ASSERT_TRUE(fa >= 8); // nach der ersten Halbierung pendelt f zwischen 8 und 15
    }
    free(mp);
}

// --- 10: Uhr ---------------------------------------------------------------------------------------

void test_set_clock_and_boot_epoch_survive_init_and_reset(void)
{
    NbrMatrix *mp = (NbrMatrix *)calloc(1, sizeof(NbrMatrix));
    NbrMatrix &m = *mp;
    TEST_ASSERT_EQUAL_UINT32(0, nbrBootEpoch(m));
    nbrSetClock(m, 1759000000u, 100); // vor dem ersten nbrInit()
    TEST_ASSERT_EQUAL_UINT32(1759000000u - 6000u, nbrBootEpoch(m));
    nbrInit(m, OWN, 100);
    TEST_ASSERT_EQUAL_UINT32(1759000000u - 6000u, nbrBootEpoch(m));
    nbrReset(m, 200);
    TEST_ASSERT_EQUAL_UINT32(1759000000u - 6000u, nbrBootEpoch(m));
    nbrSetClock(m, 0, 100); // keine Uhr
    TEST_ASSERT_EQUAL_UINT32(0, nbrBootEpoch(m));
    nbrSetClock(m, 100, 100); // Epoche kleiner als die Bootminute: keine gueltige Uhr
    TEST_ASSERT_EQUAL_UINT32(0, nbrBootEpoch(m));
    free(mp);
}

// --- 11: Sicherung (Konzept 4.12) ------------------------------------------------------------------

void test_save_load_round_trip_across_reboot(void)
{
    const uint32_t E = 1759000000u;
    NbrMatrix *ap = mk(0);
    NbrMatrix &a = *ap;
    nbrSetClock(a, E, 500);
    // Kanten mit Alter 10 und 700 beim Sichern (Minute 500), Slot, Horizont, Echo.
    heard(a, "DL1AAA-1", 490, -3);
    nbrNoteDirect(a, "DL1AAA-1", info('!', -77, 12, true), 490);
    nbrNoteFrame(a, "OE1OLD-1,DL2OLD-2", '!', NULL, false, -90, 0, 65000); // Alter 1036 (Ueberlauf)
    heard(a, "DL3OLD-3", (uint16_t)(500 - 700));
    heard(a, "DL4MID-4", 200);
    nbrNoteFrame(a, "S1AAA,A1AAA,DL1AAA-1", '@', NULL, true, -90, -3, 495);
    nbrNoteReport(a, "DL1AAA-1", "R2;N1;DK5EN-98,-4;", 480);
    nbrNoteNcnt(a, "DL1AAA-1", 9, 480);
    nbrNoteOwnTx(a, 0x1234u, '!', 499);

    uint8_t *buf = (uint8_t *)malloc(nbrSaveSize());
    TEST_ASSERT_EQUAL_UINT32(0, nbrSave(a, E, 500, buf, nbrSaveSize() - 1)); // zu klein
    TEST_ASSERT_EQUAL_UINT32(nbrSaveSize(), nbrSave(a, E, 500, buf, nbrSaveSize()));
    TEST_ASSERT_EQUAL_UINT32(E, nbrSavedEpoch(buf, nbrSaveSize()));
    TEST_ASSERT_EQUAL_UINT32(0, nbrSavedEpoch(buf, nbrSaveSize() - 1));
    TEST_ASSERT_EQUAL_UINT32(0, nbrSavedEpoch(NULL, 0));

    // Neustart: eine Stunde spaeter, Minute 5, leere Matrix (BSS).
    NbrMatrix *bp = (NbrMatrix *)calloc(1, sizeof(NbrMatrix));
    NbrMatrix &b = *bp;
    TEST_ASSERT_TRUE(nbrLoad(b, buf, nbrSaveSize(), E + 3600u, 5));
    TEST_ASSERT_TRUE(nbrOwnCallIs(b, OWN));
    TEST_ASSERT_EQUAL_UINT32(E + 3600u - 300u, nbrBootEpoch(b));
    TEST_ASSERT_TRUE(masks_consistent(b));

    NbrMhView v;
    int r1 = nbrFind(b, "DL1AAA-1");
    TEST_ASSERT_TRUE(r1 > 0);
    TEST_ASSERT_TRUE(nbrMhGet(b, r1, 5, &v));
    TEST_ASSERT_EQUAL_UINT16(5 + 60, v.age_min); // Alter beim Sichern (Minute 495) + 60 min
    TEST_ASSERT_EQUAL_INT16(-77, v.rssi);
    TEST_ASSERT_EQUAL_UINT8(12, v.sec);
    TEST_ASSERT_EQUAL_INT8(-3, v.snr);
    TEST_ASSERT_EQUAL_INT8(-4, v.hm_snr);
    TEST_ASSERT_EQUAL_UINT8(9, v.ncnt);
    TEST_ASSERT_EQUAL_UINT8(1, nbrRowHasFlag(b, r1, NBR_FLAG_RPT)); // 20 + 60 min alt: Flag bleibt
    // Aelter als 720 nach der Umrechnung: weg (Zeile, Kante).
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(b, "DL3OLD-3"));
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(b, "DL2OLD-2"));
    int r4 = nbrFind(b, "DL4MID-4");
    TEST_ASSERT_TRUE(r4 > 0);
    TEST_ASSERT_TRUE(nbrMhGet(b, r4, 5, &v));
    TEST_ASSERT_EQUAL_UINT16(300 + 60, v.age_min);
    // Horizont: Alter 5 + 60, Eintrittszeile erhalten.
    NbrRouteView rv;
    TEST_ASSERT_TRUE(route_find(b, "S1AAA", 5, &rv) >= 0);
    TEST_ASSERT_EQUAL_UINT16(65, rv.age_min);
    TEST_ASSERT_EQUAL_UINT8(3, rv.hops);
    TEST_ASSERT_EQUAL_UINT8(1, rv.gw);
    TEST_ASSERT_TRUE(nbrMaskTest(rv.entry, nbrFind(b, "A1AAA")));
    // Echo-Eintrag (61 min alt) wird beim naechsten Sweep gefaltet.
    logstart();
    nbrSweep(b, 6);
    TEST_ASSERT_NOT_NULL(strstr(g_log, "[NBR]|ECHO|6|00001234|"));
    // Laeuft mit der geladenen Topologie weiter.
    heard(b, "DL1AAA-1", 7, -3);
    TEST_ASSERT_TRUE(nbrMhGet(b, r1, 7, &v));
    TEST_ASSERT_EQUAL_UINT16(0, v.age_min);

    // Alles, was beim Laden aelter als 720 wuerde, faellt weg: 12 h spaeter.
    NbrMatrix *cp = (NbrMatrix *)calloc(1, sizeof(NbrMatrix));
    TEST_ASSERT_TRUE(nbrLoad(*cp, buf, nbrSaveSize(), E + 720u * 60u, 5));
    TEST_ASSERT_EQUAL_INT(1, nbrRowsUsed(*cp));
    TEST_ASSERT_EQUAL_INT(0, nbrEdgesUsed(*cp));
    TEST_ASSERT_EQUAL_INT(0, nbrRouteCount(*cp, 5));
    // Ein verworfener Echo-Eintrag ist ganz leer, wie nach dem Falten (R3).
    for (int k = 0; k < 4; k++)
    {
        TEST_ASSERT_EQUAL_UINT8(0, cp->echo_type[k]);
        TEST_ASSERT_EQUAL_UINT32(0, cp->echo_id[k]);
        TEST_ASSERT_EQUAL_UINT16(0, cp->echo_min[k]);
        TEST_ASSERT_TRUE(nbrMaskEmpty(cp->echo_first[k]) && nbrMaskEmpty(cp->echo_second[k]));
    }
    free(cp);
    free(ap);
    free(bp);
    free(buf);
}

void test_load_rejects_foreign_header_clockless_and_other_node(void)
{
    const uint32_t E = 1759000000u;
    NbrMatrix *ap = mk(0);
    heard(*ap, "DL1AAA-1", 490);
    size_t n = nbrSaveSize();
    uint8_t *buf = (uint8_t *)malloc(n);
    nbrSave(*ap, E, 500, buf, n);

    NbrMatrix *bp = mk(3);
    heard(*bp, "DL9XXX-9", 3);
    NbrMatrix *before = (NbrMatrix *)malloc(sizeof(NbrMatrix));
    memcpy(before, bp, sizeof(NbrMatrix));

    // Fremder Kopf je Feld: Magic, Version, Zeilen, Kanten, Slots, Horizont, Woerter, Reserve.
    const int offs[] = {0, 4, 6, 8, 10, 12, 14, 22};
    for (size_t i = 0; i < sizeof(offs) / sizeof(offs[0]); i++)
    {
        buf[offs[i]] ^= 0x01;
        TEST_ASSERT_FALSE(nbrLoad(*bp, buf, n, E + 60, 5));
        TEST_ASSERT_EQUAL_UINT32(0, nbrSavedEpoch(buf, n));
        TEST_ASSERT_EQUAL_INT(0, memcmp(before, bp, sizeof(NbrMatrix)));
        buf[offs[i]] ^= 0x01;
    }
    // Zu kurz, ohne Uhr auf einer Seite.
    TEST_ASSERT_FALSE(nbrLoad(*bp, buf, n - 1, E + 60, 5));
    TEST_ASSERT_FALSE(nbrLoad(*bp, buf, n, 0, 5));
    uint8_t *b0 = (uint8_t *)malloc(n);
    nbrSave(*ap, 0, 500, b0, n);
    TEST_ASSERT_FALSE(nbrLoad(*bp, b0, n, E, 5));
    TEST_ASSERT_EQUAL_UINT32(0, nbrSavedEpoch(b0, n)); // gueltiger Kopf, Epoche 0
    TEST_ASSERT_EQUAL_INT(0, memcmp(before, bp, sizeof(NbrMatrix)));
    // Anderes eigenes Rufzeichen: verworfen.
    NbrMatrix *op = mk(3);
    nbrInit(*op, "DL0OTH-1", 3);
    TEST_ASSERT_FALSE(nbrLoad(*op, buf, n, E + 60, 5));
    // Dasselbe Rufzeichen, schon initialisiert: geladen, alter Inhalt ersetzt.
    TEST_ASSERT_TRUE(nbrLoad(*bp, buf, n, E + 60, 5));
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(*bp, "DL9XXX-9"));
    TEST_ASSERT_TRUE(nbrFind(*bp, "DL1AAA-1") > 0);
    free(op);
    free(b0);
    free(before);
    free(ap);
    free(bp);
    free(buf);
}

// --- 12: leere Matrix -------------------------------------------------------------------------------

void test_zero_initialised_matrix_is_safe_for_every_new_function(void)
{
    NbrMatrix *mp = (NbrMatrix *)calloc(1, sizeof(NbrMatrix));
    NbrMatrix &m = *mp;
    NbrMatrix *zero = (NbrMatrix *)calloc(1, sizeof(NbrMatrix));
    NbrMhView v;
    NbrRouteView rv;
    uint8_t out[4];
    nbrNoteDirect(m, "DL1AAA-1", info('!', -90, 1, true), 1);
    nbrNoteNcnt(m, "DL1AAA-1", 3, 1);
    nbrNoteOwnTx(m, 1, '!', 1);
    nbrNoteFrame(m, "AB,DL1AAA-1", '!', NULL, false, -90, 0, 1); // TOK: ME-Schritt nur nach nbrInit()
    nbrNoteFrame(m, "S1AAA,A1AAA,B1AAA", '!', NULL, false, -90, 0, 1);
    nbrSweep(m, 2);
    TEST_ASSERT_EQUAL_INT(0, nbrMhRows(m, 1, 720, out, 4));
    TEST_ASSERT_FALSE(nbrMhGet(m, 1, 1, &v));
    TEST_ASSERT_EQUAL_INT(0, nbrMhCount(m, 1, 60));
    TEST_ASSERT_EQUAL_INT(0, nbrNcnt(m, 1));
    TEST_ASSERT_EQUAL_INT(0, nbrNcntAir(m, 1));
    TEST_ASSERT_EQUAL_INT(0, nbrRouteCount(m, 1));
    TEST_ASSERT_FALSE(nbrRouteGet(m, 0, 1, &rv));
    TEST_ASSERT_EQUAL_UINT32(0, nbrBootEpoch(m));
    uint8_t xc[NBR_MAX_ROWS];
    NbrMask d;
    nbrPrivXCounts(m, 1, xc, &d);
    TEST_ASSERT_TRUE(nbrMaskEmpty(d));
    TEST_ASSERT_EQUAL_INT(0, memcmp(zero, mp, sizeof(NbrMatrix)));
    // Sichern einer leeren Matrix: Laden wird verworfen (kein eigenes Rufzeichen).
    size_t n = nbrSaveSize();
    uint8_t *buf = (uint8_t *)malloc(n);
    TEST_ASSERT_EQUAL_UINT32(n, nbrSave(m, 1759000000u, 1, buf, n));
    TEST_ASSERT_FALSE(nbrLoad(m, buf, n, 1759000060u, 2));
    TEST_ASSERT_EQUAL_INT(0, memcmp(zero, mp, sizeof(NbrMatrix)));
    nbrSetClock(m, 1759000000u, 1);
    TEST_ASSERT_EQUAL_UINT32(1759000000u - 60u, nbrBootEpoch(m));
    free(buf);
    free(zero);
    free(mp);
}

// --- 13: #X in einem Durchlauf = #X je Zeile --------------------------------------------------------

void test_private_xcounts_equal_row_mesh_need_count(void)
{
    srand(12345);
    for (int round = 0; round < 30; round++)
    {
        NbrMatrix *mp = mk();
        NbrMatrix &m = *mp;
        int nrows = 8 + rand() % (NBR_MAX_ROWS - 9);
        for (int i = 0; i < 3 * nrows; i++)
        {
            int a = rand() % nrows, b = rand() % nrows;
            if (a == b)
                continue;
            char path[32];
            uint16_t t = (uint16_t)(rand() % 700);
            if (rand() % 3 == 0)
                heard(m, nm(a), t);
            else
            {
                snprintf(path, sizeof(path), "%s,%s", nm(a), nm(b));
                for (int k = rand() % 4; k >= 0; k--)
                    nbrNoteFrame(m, path, '!', NULL, false, -90, 0, t);
            }
        }
        for (uint16_t now = 600; now <= 1300; now += 350)
        {
            uint8_t xc[NBR_MAX_ROWS];
            NbrMask d;
            nbrPrivXCounts(m, now, xc, &d);
            TEST_ASSERT_TRUE(nbrMaskEqual(d, nbrDirectMask(m, now)));
            for (int r = 0; r < NBR_MAX_ROWS; r++)
            {
                int want = nbrRowMeshNeedCount(m, r, now);
                TEST_ASSERT_EQUAL_INT(want < 0 ? 0xFF : want, xc[r]);
            }
        }
        free(mp);
    }
}

// --- 14: Groessen je Teil (Bericht an build.py) -----------------------------------------------------

void test_size_table_per_part(void)
{
    const int W = NBR_MASK_WORDS;
    struct P
    {
        const char *name;
        size_t ours;
        size_t model;
    } p[] = {
        {"calls", sizeof(NbrMatrix::call), 8u * NBR_MAX_ROWS},
        {"core", sizeof(NbrMatrix::row), 12u * NBR_MAX_ROWS},
        {"masks", sizeof(NbrMatrix::hears) + sizeof(NbrMatrix::heardBy), 2u * 8u * W * NBR_MAX_ROWS},
        {"edges", sizeof(NbrMatrix::edge), 6u * NBR_MAX_EDGES},
        {"ext", sizeof(NbrMatrix::ext), 13u * NBR_EXT_SLOTS},
        {"hz", sizeof(NbrMatrix::hz_call) + sizeof(NbrMatrix::hz_entry) + sizeof(NbrMatrix::hz_meta),
         (8u + 8u * W + 4u) * NBR_HZ_ENTRIES},
        {"hdr", sizeof(NbrMatrix::boot_min) + sizeof(NbrMatrix::last_sweep) + sizeof(NbrMatrix::last_halve) +
                    sizeof(NbrMatrix::boot_epoch),
         2u + 2u + 4u + 8u * W + 8u * W + 4u},
        {"echo", sizeof(NbrMatrix::echo_id) + sizeof(NbrMatrix::echo_first) + sizeof(NbrMatrix::echo_second) +
                     sizeof(NbrMatrix::echo_min) + sizeof(NbrMatrix::echo_type),
         4u * (4u + 8u * W + 8u * W + 2u)},
    };
    size_t sum = 0, msum = 0;
    printf("\n[nbr_views] size table R=%d E=%d X=%d H=%d W=%d (ours / build.py)\n", NBR_MAX_ROWS, NBR_MAX_EDGES,
           NBR_EXT_SLOTS, NBR_HZ_ENTRIES, W);
    for (size_t i = 0; i < sizeof(p) / sizeof(p[0]); i++)
    {
        printf("  %-6s %6zu %6zu\n", p[i].name, p[i].ours, p[i].model);
        sum += p[i].ours;
        msum += p[i].model;
    }
    printf("  %-6s %6zu %6zu  (sizeof(NbrMatrix) %zu, padding %zu)\n", "total", sum, msum, sizeof(NbrMatrix),
           sizeof(NbrMatrix) - sum);
    printf("  save image %zu bytes\n", nbrSaveSize());
    for (size_t i = 0; i < 6; i++)
        TEST_ASSERT_EQUAL_UINT32(p[i].model, p[i].ours);
    TEST_ASSERT_TRUE(sizeof(NbrMatrix) - sum <= 8);
}

// RAK-Test 2026-09-26: der nRF52-Build laeuft mit -Ofast (-ffast-math), isnan()
// fiel dort weg und --mheard zeigte "lat=N nan". Diese Umgebung (native_nbr_views64)
// baut ebenfalls mit -ffast-math; unbekannte Positionen muessen trotzdem als
// unbekannt erkennbar sein.
void test_unknown_position_detected_under_fast_math(void)
{
    TEST_ASSERT_TRUE(nbrPosKnown(48.4f, 11.7f));
    TEST_ASSERT_TRUE(nbrPosKnown(-90.0f, 180.0f));
    TEST_ASSERT_FALSE(nbrPosKnown(NBR_POS_NONE, NBR_POS_NONE));
    TEST_ASSERT_FALSE(nbrPosKnown(48.4f, NBR_POS_NONE));

    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 100);
    nbrNoteFrame(m, "OE1AAA-1", '@', "", false, -70, 5, 100);
    NbrDirectInfo info;
    memset(&info, 0, sizeof(info));
    info.plt = '@';
    info.own_frame = true;
    info.has_pos = false;
    info.lat = NBR_POS_NONE;
    info.lon = NBR_POS_NONE;
    info.alt_m = NBR_ALT_UNKNOWN;
    nbrNoteDirect(m, "OE1AAA-1", info, 100);
    NbrMhView v;
    TEST_ASSERT_TRUE(nbrMhGet(m, nbrFind(m, "OE1AAA-1"), 100, &v));
    TEST_ASSERT_FALSE(nbrPosKnown(v.lat, v.lon));
    NbrRowView r;
    TEST_ASSERT_TRUE(nbrRowGet(m, nbrFind(m, "OE1AAA-1"), &r));
    TEST_ASSERT_FALSE(nbrPosKnown(r.lat, r.lon));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_bits_every_field_min_max_and_neighbours_untouched);
    RUN_TEST(test_direct_slot_encodings_at_min_max_unknown);
    RUN_TEST(test_slot_allocation_evict_x_and_sweep_release);
    RUN_TEST(test_mh_view_fields_from_frame_sequence);
    RUN_TEST(test_mh_rows_order_newest_minute_then_second_then_index);
    RUN_TEST(test_mh_count_matches_one_sided_mheard_count);
    RUN_TEST(test_ncnt_d60_hm_sym_veto_and_cap);
    RUN_TEST(test_ncnt_sym_hysteresis_enter_minus16_leave_below_minus18);
    RUN_TEST(test_ncnt_uncapped_and_air_cap);
    RUN_TEST(test_note_ncnt_writes_row_core_direct_or_indirect);
    RUN_TEST(test_horizon_rules_positive_and_negative);
    RUN_TEST(test_horizon_eviction_longest_unseen);
    RUN_TEST(test_horizon_row_eviction_clears_entry_masks);
    RUN_TEST(test_horizon_hop_minimum_rises_after_12h);
    RUN_TEST(test_routes_two_hop_rows_then_horizon);
    RUN_TEST(test_route_sender_never_listed_as_row_and_horizon);
    RUN_TEST(test_unknown_position_detected_under_fast_math);
    RUN_TEST(test_name_helpers_match_old_tables);
    RUN_TEST(test_echo_table_first_second_hand_and_fold);
    RUN_TEST(test_set_clock_and_boot_epoch_survive_init_and_reset);
    RUN_TEST(test_save_load_round_trip_across_reboot);
    RUN_TEST(test_load_rejects_foreign_header_clockless_and_other_node);
    RUN_TEST(test_zero_initialised_matrix_is_safe_for_every_new_function);
    RUN_TEST(test_private_xcounts_equal_row_mesh_need_count);
    RUN_TEST(test_size_table_per_part);
    return UNITY_END();
}
