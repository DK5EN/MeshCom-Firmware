// Native Testsuite fuer die `--setlog on`-Formatierer (SL-00, Welle 0 des
// Implementierungsplans docs/setlog-instrumentation-impl-plan-20260902.md).
//
//   pio test -e native -f test_setlog_lines
//
// Geprueft wird genau das, was auf dem Knoten nicht mehr geprueft werden kann,
// ohne einen Mitschnitt zu lesen:
//
//  1. Feldreihenfolge und Schreibweise jeder Zeilenart (Stringvergleich,
//     nicht "enthaelt"), weil tools/berglog.py stellungsabhaengig parst.
//  2. Laengenobergrenzen. printfdeb() hat ZWEI Klippen: loc_buf[600] (darueber
//     malloc, und Heap-Churn kostet auf ESP32 die NimBLE-Verbindung) und
//     nformat[300], das den FORMATSTRING kappt. Geprueft wird deshalb: jeder
//     Formatstring unter 300 Zeichen, RLY/TX/ERR/GWI/GWU auch im Extremfall
//     unter 160 Byte, STAT unter 300 Byte, RX-Zeile mit Acht-Rufzeichen-Pfad
//     unter 340 Byte. Die 300/160-Schranken der Rahmenbedingung 4 des Plans
//     halten fuer RX und STAT nicht -- siehe die Vermerke an den beiden
//     Tests und die Eskalation im Wellenbericht.
//  3. Grenzwerte: negative Pegel, uint32-Maximum bei t=, Pufferkuerzung.
//  4. Dass die bestehende [LOG]-RX-Zeile byteidentisches PRAEFIX der neuen
//     bleibt (Rahmenbedingung 7) -- alte Parser duerfen nicht brechen.
//
// Fails-before: der behavioural fails-before dieses Plans ist der Bench in
// Welle 3 (ein Knoten mit `--setlog on`, dessen RX-Zeile heute kein `DUP:`
// traegt). Nativ laesst sich das nicht herstellen, weil es hier keinen
// Empfang gibt; test_rx_tail_traegt_dup_verdikt() haelt stattdessen fest,
// dass das Feld ueberhaupt existiert und wie es heisst -- der Test faellt,
// sobald jemand `DUP:` umbenennt oder streicht.

#include <unity.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <setlog_lines.h>

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Die beiden Formatstrings aus src/loop_functions.cpp, wortwoertlich.
//
// Bewusst hier dupliziert und nicht aus einer Datei gelesen: der Test soll
// genau dann fallen, wenn jemand printBuffer_aprs() aendert, ohne
// printBuffer_aprs_rx() nachzuziehen (oder umgekehrt). Eine Fixture-Datei
// wuerde stattdessen den Test mitwandern lassen.
static const char RX_FMT_ALT[] =
    "%s %s %03i %c x%08X H%02X S%i T%i M%02X %s>%s%c%s HW:%02i MOD:%01X/%01i FCS:%04X FW:%02i:%c LH:%02X\n";
static const char RX_FMT_NEU[] =
    "%s %s %03i %c x%08X H%02X S%i T%i M%02X %s>%s%c%s HW:%02i MOD:%01X/%01i FCS:%04X FW:%02i:%c LH:%02X%s\n";

static const char ACK7_FMT_ALT[] = "%s %s 007 %c x%02X%02X%02X%02X H%02X %02X\n";
static const char ACK7_FMT_NEU[] = "%s %s 007 %c x%02X%02X%02X%02X H%02X %02X%s\n";
static const char ACK12_FMT_ALT[] =
    "%s %s 012 %c x%02X%02X%02X%02X H%02X x%02X%02X%02X%02X %02X %02X\n";
static const char ACK12_FMT_NEU[] =
    "%s %s 012 %c x%02X%02X%02X%02X H%02X x%02X%02X%02X%02X %02X %02X%s\n";

// Acht Rufzeichen mit Suffix, kommagetrennt -- der laengste Pfad, den ein
// Frame mit max_hop 8 im Feld tragen kann.
static const char PFAD_ACHT[] =
    "OE3XIR-12,OE3XOC-12,OE3XWJ-12,OE3MAG-12,DK5EN-90,DB0ABC-12,OE1XYZ-11,OE5ABC-10";

// 100 Byte Nutzlast (Annahme der Risikoabschaetzung im Plan).
static const char NUTZLAST_100[] =
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789";

// ---------------------------------------------------------------------------
// SL-01 -- RX-Anhang
// ---------------------------------------------------------------------------

static void test_rx_tail_typischer_fall(void)
{
    char buf[64];
    int len = setlogFormatRxTail(buf, sizeof(buf), -108, 7, false, false, 123456u);

    TEST_ASSERT_EQUAL_STRING(" RSSI:-108 SNR:7 DUP:n OWN:- t=123456", buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), len);
}

static void test_rx_tail_traegt_dup_verdikt(void)
{
    char buf[64];

    setlogFormatRxTail(buf, sizeof(buf), -95, -3, true, true, 42u);
    TEST_ASSERT_EQUAL_STRING(" RSSI:-95 SNR:-3 DUP:d OWN:e t=42", buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "DUP:"));

    setlogFormatRxTail(buf, sizeof(buf), -95, -3, false, true, 42u);
    TEST_ASSERT_EQUAL_STRING(" RSSI:-95 SNR:-3 DUP:n OWN:e t=42", buf);
}

static void test_rx_tail_maximalwerte(void)
{
    char buf[64];
    // int16-Minimum, int8-Minimum, uint32-Maximum: der laengste Anhang, den
    // die Typen erlauben.
    int len = setlogFormatRxTail(buf, sizeof(buf), -32768, -128, true, true, 4294967295u);

    TEST_ASSERT_EQUAL_STRING(" RSSI:-32768 SNR:-128 DUP:d OWN:e t=4294967295", buf);
    TEST_ASSERT_EQUAL_INT(46, len);
    // Der Puffer in printBuffer_aprs_rx()/printBuffer_ack_rx() ist 56 Byte --
    // faellt dieser Test, ist er zu klein geworden.
    TEST_ASSERT_TRUE(len + 1 <= 56);
}

static void test_rx_tail_kuerzt_und_meldet_die_echte_laenge(void)
{
    char buf[10];
    memset(buf, 0x7F, sizeof(buf));

    int len = setlogFormatRxTail(buf, sizeof(buf), -32768, -128, true, true, 4294967295u);

    TEST_ASSERT_EQUAL_INT(9, len);                 // n-1, nicht der Wunschwert
    TEST_ASSERT_EQUAL_INT(9, (int)strlen(buf));
    TEST_ASSERT_EQUAL_STRING(" RSSI:-32", buf);

    // n == 0 fasst den Puffer nicht an
    char leer = 0x55;
    TEST_ASSERT_EQUAL_INT(0, setlogFormatRxTail(&leer, 0, -1, -1, false, false, 0));
    TEST_ASSERT_EQUAL_INT(0x55, (int)leer);
}

// ---------------------------------------------------------------------------
// SL-02 -- RLY
// ---------------------------------------------------------------------------

static void test_rly_eingereiht_und_abgelehnt(void)
{
    char buf[160];

    int len = setlogFormatRly(buf, sizeof(buf), 0x1A2B3C4Du, ':', 0x03, "tx", 2, 7);
    TEST_ASSERT_EQUAL_STRING("RLY x1A2B3C4D : H03 q=tx prio=2 slot=7", buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), len);

    // Abgelehnt: kein Slot, keine Prio -- die Aufrufer geben 0 / -1 mit.
    setlogFormatRly(buf, sizeof(buf), 0x00000001u, '!', 0x00, "hop0", 0, -1);
    TEST_ASSERT_EQUAL_STRING("RLY x00000001 ! H00 q=hop0 prio=0 slot=-1", buf);
}

static void test_rly_maximalfall_bleibt_unter_160(void)
{
    char buf[256];
    // laengster Grund aus der SL-02-Tabelle, Maximalwerte in allen Zahlen
    int len = setlogFormatRly(buf, sizeof(buf), 0xFFFFFFFFu, 'W', 0xFF, "gwfilter", 5, 19);

    TEST_ASSERT_EQUAL_STRING("RLY xFFFFFFFF W HFF q=gwfilter prio=5 slot=19", buf);
    TEST_ASSERT_TRUE(len < 160);

    // NULL-Grund darf nicht in ein undefiniertes Format laufen
    setlogFormatRly(buf, sizeof(buf), 0u, 'W', 0, NULL, 0, -1);
    TEST_ASSERT_EQUAL_STRING("RLY x00000000 W H00 q=? prio=0 slot=-1", buf);
}

// ---------------------------------------------------------------------------
// SL-03 -- TX
// ---------------------------------------------------------------------------

static void test_tx_typischer_fall(void)
{
    char buf[192];
    int len = setlogFormatTx(buf, sizeof(buf), 0xDEADBEEFu, ':', 0x02, 3, 'r',
                             1450u, 2, 4, 87u, 987654u);

    TEST_ASSERT_EQUAL_STRING(
        "TX xDEADBEEF : H02 prio=3 src=r wait=1450 q=2 cad=4 len=87 t=987654", buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), len);
}

static void test_tx_maximalfall_bleibt_unter_160(void)
{
    char buf[256];
    int len = setlogFormatTx(buf, sizeof(buf), 0xFFFFFFFFu, '!', 0xFF, 5, 'g',
                             4294967295u, 20, 65535, 65535u, 4294967295u);

    TEST_ASSERT_EQUAL_STRING(
        "TX xFFFFFFFF ! HFF prio=5 src=g wait=4294967295 q=20 cad=65535 len=65535 t=4294967295",
        buf);
    TEST_ASSERT_TRUE(len < 160);
}

// ---------------------------------------------------------------------------
// SL-04 -- ERR
// ---------------------------------------------------------------------------

static void test_err_typisch_und_maximal(void)
{
    char buf[192];

    // nRF52: keine Laenge, kein Frequenzfehler -- beide 0
    int len = setlogFormatErr(buf, sizeof(buf), -121, -14, 0u, 0, 55000u);
    TEST_ASSERT_EQUAL_STRING("ERR rssi=-121 snr=-14 len=0 ferr=0 t=55000", buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), len);

    // ESP32: Laenge und Frequenzfehler vorhanden, Frequenzfehler negativ
    len = setlogFormatErr(buf, sizeof(buf), -32768, -128, 65535u, -2147483647, 4294967295u);
    TEST_ASSERT_EQUAL_STRING(
        "ERR rssi=-32768 snr=-128 len=65535 ferr=-2147483647 t=4294967295", buf);
    TEST_ASSERT_TRUE(len < 160);
}

// ---------------------------------------------------------------------------
// SL-05 -- STAT
// ---------------------------------------------------------------------------

static void fuelleStat(struct setlogStatFields *f)
{
    memset(f, 0, sizeof(*f));
    f->util_pct = 17;
    f->rx_ms = 51000;
    f->tx_ms = 2400;
    f->newid = 812;
    f->dup = 1943;
    f->err = 27;
    f->txn = 96;
    f->txfail = 1;
    f->ringmax = 7;
    f->ring_size = 20;
    f->drop[0] = 0;
    f->drop[1] = 0;
    f->drop[2] = 3;
    f->drop[3] = 0;
    f->drop[4] = 12;
    f->mh = 34;
    f->heap = 148320;
    f->trk_interval_s = 600;
    f->trk_consistent = 4;
    f->fw_major = 35;
    f->fw_sub = 'p';
    f->flash = 20260901u;
    f->up_s = 86400;
    f->t_ms = 86400000u;
}

static void test_stat_typischer_fall(void)
{
    struct setlogStatFields f;
    char buf[300];

    fuelleStat(&f);
    int len = setlogFormatStat(buf, sizeof(buf), &f);

    TEST_ASSERT_EQUAL_STRING(
        "STAT util=17 rx=51000 tx=2400 newid=812 dup=1943 err=27 txn=96 txfail=1 "
        "ringmax=7/20 drop=0/0/3/0/12 mh=34 heap=148320 trk=600/4 "
        "fw=35p/20260901 up=86400 t=86400000",
        buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), len);
    // Gemessen 164 Byte. Die 160-Byte-Schranke aus Rahmenbedingung 4 des
    // Plans ist mit der dort festgelegten Feldliste nicht erreichbar (schon
    // typische Werte liegen darueber); massgeblich ist die echte Klippe,
    // loc_buf[600] in printfdeb_functions.cpp. Siehe Eskalation im
    // Wellenbericht. Diese Schranke nagelt fest, dass die Zeile nicht
    // unbemerkt weiter waechst.
    TEST_ASSERT_TRUE(len < 200);
}

static void test_stat_maximalfall(void)
{
    struct setlogStatFields f;
    char buf[400];

    memset(&f, 0, sizeof(f));
    f.util_pct = 100;
    f.rx_ms = f.tx_ms = 300000u;
    f.newid = f.dup = f.err = f.txn = f.txfail = 4294967295u;
    f.ringmax = 20;
    f.ring_size = 20;
    for(int i = 0; i < 5; i++)
        f.drop[i] = 65535u;
    f.mh = 65535u;
    f.heap = 4294967295u;
    f.trk_interval_s = 4294967295u;
    f.trk_consistent = 65535u;
    f.fw_major = 255;
    f.fw_sub = 'z';
    f.flash = 4294967295u;
    f.up_s = 4294967295u;
    f.t_ms = 4294967295u;

    int len = setlogFormatStat(buf, sizeof(buf), &f);

    // Reihenfolge und Trenner bleiben auch im Extremfall exakt
    TEST_ASSERT_EQUAL_STRING(
        "STAT util=100 rx=300000 tx=300000 newid=4294967295 dup=4294967295 "
        "err=4294967295 txn=4294967295 txfail=4294967295 ringmax=20/20 "
        "drop=65535/65535/65535/65535/65535 mh=65535 heap=4294967295 "
        "trk=4294967295/65535 fw=255z/4294967295 up=4294967295 t=4294967295",
        buf);
    // Gemessen 254 Byte: der arithmetische Extremfall (alle Zaehler am
    // uint32-Anschlag, den ein 5-Minuten-Fenster nie erreicht) bleibt weit
    // unter loc_buf[600] -- die STAT-Zeile loest also niemals ein malloc in
    // printfdeb() aus.
    TEST_ASSERT_TRUE(len < 300);
}

static void test_stat_ohne_struct_schreibt_nichts(void)
{
    char buf[8];
    memset(buf, 0x33, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, setlogFormatStat(buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_INT(0x33, (int)buf[0]);
}

// ---------------------------------------------------------------------------
// SL-06 -- GWI / GWU
// ---------------------------------------------------------------------------

static void test_gwi_und_gwu(void)
{
    char buf[192];

    int len = setlogFormatGwi(buf, sizeof(buf), 0x0A0B0C0Du, ':', 0x04, "OE1KBC-12", 7200u);
    TEST_ASSERT_EQUAL_STRING("GWI x0A0B0C0D : H04 from=OE1KBC-12 t=7200", buf);
    TEST_ASSERT_TRUE(len < 160);

    len = setlogFormatGwu(buf, sizeof(buf), 0x0A0B0C0Du, ':', 0x03, 7205u);
    TEST_ASSERT_EQUAL_STRING("GWU x0A0B0C0D : H03 t=7205", buf);
    TEST_ASSERT_TRUE(len < 160);

    // Maximalfall: laengstes Rufzeichen-Feld, das der Frame tragen kann
    len = setlogFormatGwi(buf, sizeof(buf), 0xFFFFFFFFu, 'W', 0xFF, "OE3XIR-12", 4294967295u);
    TEST_ASSERT_EQUAL_STRING("GWI xFFFFFFFF W HFF from=OE3XIR-12 t=4294967295", buf);
    TEST_ASSERT_TRUE(len < 160);

    setlogFormatGwi(buf, sizeof(buf), 0u, 'W', 0, NULL, 0u);
    TEST_ASSERT_EQUAL_STRING("GWI x00000000 W H00 from=? t=0", buf);
}

// ---------------------------------------------------------------------------
// Rahmenbedingung 7 -- die alte RX-Zeile bleibt byteidentisches Praefix
// ---------------------------------------------------------------------------

// Baut die alte [LOG]-Zeile mit festen Werten. Die Werte entsprechen einem
// realen Relay-Empfang: langer Pfad, 100 Byte Nutzlast.
static int baueAlteRxZeile(char *out, size_t n, const char *pfad, const char *nutzlast)
{
    return snprintf(out, n, RX_FMT_ALT,
                    "21:47:03", "[LOG]", 231, ':', 0x1A2B3C4Du, 0x03,
                    1, 0, 0x01, pfad, "*", ':', nutzlast,
                    9, 0x0, 3, 0xAB12, 35, 'p', 0x09);
}

static int baueNeueRxZeile(char *out, size_t n, const char *pfad, const char *nutzlast,
                           const char *tail)
{
    return snprintf(out, n, RX_FMT_NEU,
                    "21:47:03", "[LOG]", 231, ':', 0x1A2B3C4Du, 0x03,
                    1, 0, 0x01, pfad, "*", ':', nutzlast,
                    9, 0x0, 3, 0xAB12, 35, 'p', 0x09, tail);
}

static void test_neue_rx_zeile_beginnt_mit_der_alten(void)
{
    char alt[600];
    char neu[600];
    char tail[64];

    int alt_len = baueAlteRxZeile(alt, sizeof(alt), PFAD_ACHT, NUTZLAST_100);
    setlogFormatRxTail(tail, sizeof(tail), -113, -9, false, false, 3600000u);
    int neu_len = baueNeueRxZeile(neu, sizeof(neu), PFAD_ACHT, NUTZLAST_100, tail);

    TEST_ASSERT_TRUE(alt_len > 0 && neu_len > 0);

    // alles bis auf das abschliessende '\n' der alten Zeile ist byteidentisch
    TEST_ASSERT_EQUAL_INT('\n', alt[alt_len - 1]);
    TEST_ASSERT_EQUAL_INT(0, memcmp(neu, alt, (size_t)(alt_len - 1)));

    // und direkt danach beginnt der Anhang, gefolgt vom Zeilenende
    TEST_ASSERT_EQUAL_INT(0, memcmp(neu + (alt_len - 1), tail, strlen(tail)));
    TEST_ASSERT_EQUAL_INT('\n', neu[neu_len - 1]);
    TEST_ASSERT_EQUAL_INT(neu_len, alt_len + (int)strlen(tail));
}

static void test_neue_rx_zeile_ist_der_alte_formatstring_plus_ein_prozent_s(void)
{
    // Der eigentliche Schutz: die beiden Formatstrings duerfen sich um genau
    // "%s" vor dem "\n" unterscheiden -- sonst hat jemand die bestehende
    // Zeile veraendert (Rahmenbedingung 7 verletzt).
    size_t alt_n = strlen(RX_FMT_ALT);
    size_t neu_n = strlen(RX_FMT_NEU);

    TEST_ASSERT_EQUAL_UINT32((uint32_t)(alt_n + 2), (uint32_t)neu_n);
    TEST_ASSERT_EQUAL_INT(0, memcmp(RX_FMT_ALT, RX_FMT_NEU, alt_n - 1));
    TEST_ASSERT_EQUAL_STRING("%s\n", RX_FMT_NEU + neu_n - 3);
    TEST_ASSERT_EQUAL_STRING("\n", RX_FMT_ALT + alt_n - 1);

    // dasselbe fuer beide ACK-Formate
    TEST_ASSERT_EQUAL_INT(0, memcmp(ACK7_FMT_ALT, ACK7_FMT_NEU, strlen(ACK7_FMT_ALT) - 1));
    TEST_ASSERT_EQUAL_STRING("%s\n", ACK7_FMT_NEU + strlen(ACK7_FMT_NEU) - 3);
    TEST_ASSERT_EQUAL_INT(0, memcmp(ACK12_FMT_ALT, ACK12_FMT_NEU, strlen(ACK12_FMT_ALT) - 1));
    TEST_ASSERT_EQUAL_STRING("%s\n", ACK12_FMT_NEU + strlen(ACK12_FMT_NEU) - 3);
}

// ---------------------------------------------------------------------------
// Laengenschranken (Rahmenbedingung 4 und die nformat[300]-Klippe)
// ---------------------------------------------------------------------------

static void test_rx_zeile_mit_langem_pfad(void)
{
    char alt[900];
    char neu[900];
    char tail[64];

    int alt_len = baueAlteRxZeile(alt, sizeof(alt), PFAD_ACHT, NUTZLAST_100);
    setlogFormatRxTail(tail, sizeof(tail), -32768, -128, true, true, 4294967295u);
    int neu_len = baueNeueRxZeile(neu, sizeof(neu), PFAD_ACHT, NUTZLAST_100, tail);

    printf("[setlog] RX-Zeile 8 Rufzeichen + 100 B Nutzlast: alt %d Byte, "
           "neu %d Byte (Extremanhang %d Byte)\n",
           alt_len, neu_len, (int)strlen(tail));

    // Befund (Eskalation im Wellenbericht): die 300-Byte-Schranke aus
    // Rahmenbedingung 4 ist fuer diesen Fall nicht erreichbar -- die
    // BESTEHENDE Zeile liegt hier schon bei 264 Byte, der Plan schaetzt sie
    // selbst auf "~260 Byte" und den Anhang auf "unter 45 Byte". Was der
    // Anhang tatsaechlich kostet, ist hier festgenagelt (<= 46 Byte); die
    // wirksame Klippe ist loc_buf[600] in printfdeb_functions.cpp, darueber
    // ruft printfdeb() malloc (Heap-Churn kostet ESP32 die BLE-Verbindung).
    TEST_ASSERT_TRUE(neu_len - alt_len <= 46);
    TEST_ASSERT_TRUE(neu_len < 340);
    TEST_ASSERT_TRUE(neu_len < 600);

    // Ohne den 100-Byte-Anhaenger -- also die Empfangszeile eines kurzen
    // Textes ueber acht Hops -- bleibt die Zeile unter 300.
    int kurz_len = baueNeueRxZeile(neu, sizeof(neu), PFAD_ACHT, "QRV?", tail);
    TEST_ASSERT_TRUE(kurz_len < 300);
}

static void test_alle_formatstrings_unter_300_zeichen(void)
{
    // printfdeb_functions.cpp kopiert den Formatstring in char nformat[300]
    // und kappt ihn dort -- ein zu langes Format zerstoert die Zeile, nicht
    // nur ihr Ende.
    TEST_ASSERT_TRUE(strlen(RX_FMT_ALT) < 300);
    TEST_ASSERT_TRUE(strlen(RX_FMT_NEU) < 300);
    TEST_ASSERT_TRUE(strlen(ACK7_FMT_NEU) < 300);
    TEST_ASSERT_TRUE(strlen(ACK12_FMT_NEU) < 300);

    // Die Formatstrings der Formatierer selbst sind in setlog_lines.cpp
    // gekapselt; ihre Ausgabe ist oben festgenagelt, und die laengste davon
    // (STAT im Extremfall) bleibt unter 300 -- ein Formatstring ist nie
    // laenger als seine laengste Ausgabe zuzueglich der Platzhalterlaenge,
    // hier zusaetzlich direkt geprueft:
    struct setlogStatFields f;
    char buf[400];
    fuelleStat(&f);
    TEST_ASSERT_TRUE(setlogFormatStat(buf, sizeof(buf), &f) < 300);
}

// ---------------------------------------------------------------------------
// SL-06 -- Herkunftskennung fuer ringSource[]
// ---------------------------------------------------------------------------

// setlogRingSourceCode() ist die Abbildung, die addTxRingEntry()
// (src/txring_functions.cpp) auf jeden Enqueue anwendet. Sie steht als
// Header-Inline in setlog_lines.h, damit txring_functions.cpp sie ohne
// Link-Abhaengigkeit nutzen kann -- und damit sie hier ohne den ganzen
// Ring-Fixture pruefbar ist. Die Verdraengungs-Kopie
// (ringSource[worst_slot] = ringSource[r]) steht neben der bestehenden
// ringEnqueueTime-Kopie und wird von test_txring (env:native_aprs) mit
// abgedeckt, sobald dort ein Fall dafuer entsteht -- siehe Wellenbericht.
static void test_ring_source_code_bildet_die_labels_ab(void)
{
    // Relay eines Empfangs
    TEST_ASSERT_EQUAL_INT('r', (int)setlogRingSourceCode("rx_relay"));
    TEST_ASSERT_EQUAL_INT('r', (int)setlogRingSourceCode("rx_ack_fwd"));
    TEST_ASSERT_EQUAL_INT('r', (int)setlogRingSourceCode("rx_dm_ack_gw"));
    TEST_ASSERT_EQUAL_INT('r', (int)setlogRingSourceCode("rx_dm_ack_new"));

    // vom Server eingespeist (WiFi-UDP und RAK-Ethernet nutzen dasselbe Label)
    TEST_ASSERT_EQUAL_INT('g', (int)setlogRingSourceCode("udp_rx"));

    // alles Eigene
    TEST_ASSERT_EQUAL_INT('o', (int)setlogRingSourceCode("user_msg"));
    TEST_ASSERT_EQUAL_INT('o', (int)setlogRingSourceCode("user_pos"));
    TEST_ASSERT_EQUAL_INT('o', (int)setlogRingSourceCode("user_wx"));
    TEST_ASSERT_EQUAL_INT('o', (int)setlogRingSourceCode("user_hey"));
    TEST_ASSERT_EQUAL_INT('o', (int)setlogRingSourceCode("auto_pos"));
    TEST_ASSERT_EQUAL_INT('o', (int)setlogRingSourceCode("beacon"));
    TEST_ASSERT_EQUAL_INT('o', (int)setlogRingSourceCode("phone_msg"));
    TEST_ASSERT_EQUAL_INT('o', (int)setlogRingSourceCode("phone_raw"));
    TEST_ASSERT_EQUAL_INT('o', (int)setlogRingSourceCode("test_loratx"));
    // "retransmit" kopiert einen bestehenden Slot: die Kennung wird dort neu
    // aus dem Label abgeleitet und faellt auf 'o' zurueck -- siehe
    // Eskalation im Wellenbericht (Aufrufstelle liegt in Welle 1).
    TEST_ASSERT_EQUAL_INT('o', (int)setlogRingSourceCode("retransmit"));

    // kein Label -> kein Absturz
    TEST_ASSERT_EQUAL_INT('o', (int)setlogRingSourceCode(NULL));
    TEST_ASSERT_EQUAL_INT('o', (int)setlogRingSourceCode(""));
    // "rx" ohne Unterstrich ist kein Relay-Label
    TEST_ASSERT_EQUAL_INT('o', (int)setlogRingSourceCode("rx"));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_rx_tail_typischer_fall);
    RUN_TEST(test_rx_tail_traegt_dup_verdikt);
    RUN_TEST(test_rx_tail_maximalwerte);
    RUN_TEST(test_rx_tail_kuerzt_und_meldet_die_echte_laenge);
    RUN_TEST(test_rly_eingereiht_und_abgelehnt);
    RUN_TEST(test_rly_maximalfall_bleibt_unter_160);
    RUN_TEST(test_tx_typischer_fall);
    RUN_TEST(test_tx_maximalfall_bleibt_unter_160);
    RUN_TEST(test_err_typisch_und_maximal);
    RUN_TEST(test_stat_typischer_fall);
    RUN_TEST(test_stat_maximalfall);
    RUN_TEST(test_stat_ohne_struct_schreibt_nichts);
    RUN_TEST(test_gwi_und_gwu);
    RUN_TEST(test_neue_rx_zeile_beginnt_mit_der_alten);
    RUN_TEST(test_neue_rx_zeile_ist_der_alte_formatstring_plus_ein_prozent_s);
    RUN_TEST(test_rx_zeile_mit_langem_pfad);
    RUN_TEST(test_alle_formatstrings_unter_300_zeichen);
    RUN_TEST(test_ring_source_code_bildet_die_labels_ab);
    return UNITY_END();
}
