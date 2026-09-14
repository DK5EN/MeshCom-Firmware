// Native Testsuite fuer die Stage-4-Custody-Notice (docs/dm-stage4-plan-
// 20260914.md): das Frameformat (Build/Parse) und die Sender-seitige
// Holder-Tabelle.
//
//   pio test -e native -f test_sto_notice

#include <unity.h>

#include <stdint.h>
#include <string.h>

#include <sto_notice.h>

void setUp(void)
{
    stoHolderReset();
}

void tearDown(void) {}

// --------------------------------------------------------------- build()

static void test_build_kurzes_rufzeichen_wird_gepolstert(void)
{
    char buf[64];
    int  len = stoNoticeBuild(buf, sizeof(buf), "DK5EN", 17, "DK5EN-14");
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("DK5EN    :sto017 DK5EN-14", buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), len);
}

static void test_build_neunstelliges_rufzeichen_ungepolstert(void)
{
    char buf[64];
    // "DK5EN-99A" ist neun Zeichen -- %-9.9s polstert nicht und schneidet nicht.
    int len = stoNoticeBuild(buf, sizeof(buf), "DK5EN-99A", 5, "OE1XYZ");
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("DK5EN-99A:sto005 OE1XYZ", buf);
}

static void test_build_laengeres_rufzeichen_wird_auf_neun_gekappt(void)
{
    char buf[64];
    // %-9.9s: die .9-Praezision kappt ein laengeres Rufzeichen auf neun
    // Zeichen, genau wie beim bestehenden :ack-Layout (SendAckMessage()).
    // "DK5EN-1234" ist zehn Zeichen lang, "DK5EN-123" die ersten neun.
    int len = stoNoticeBuild(buf, sizeof(buf), "DK5EN-1234", 3, "OE1ABC");
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("DK5EN-123:sto003 OE1ABC", buf);
}

static void test_build_nnn_wird_auf_drei_stellen_modulo_1000_geklemmt(void)
{
    char buf[64];
    int len = stoNoticeBuild(buf, sizeof(buf), "DK5EN", 1234, "OE1ABC");
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("DK5EN    :sto234 OE1ABC", buf);
}

static void test_build_null_argumente_und_zu_kleiner_puffer(void)
{
    char buf[64];

    TEST_ASSERT_EQUAL_INT(0, stoNoticeBuild(NULL, sizeof(buf), "DK5EN", 1, "OE1ABC"));
    TEST_ASSERT_EQUAL_INT(0, stoNoticeBuild(buf, 0, "DK5EN", 1, "OE1ABC"));
    TEST_ASSERT_EQUAL_INT(0, stoNoticeBuild(buf, sizeof(buf), NULL, 1, "OE1ABC"));
    TEST_ASSERT_EQUAL_INT(0, stoNoticeBuild(buf, sizeof(buf), "DK5EN", 1, NULL));

    char tiny[5];
    TEST_ASSERT_EQUAL_INT(0, stoNoticeBuild(tiny, sizeof(tiny), "DK5EN", 1, "OE1ABC"));
}

// --------------------------------------------------------------- parse()

static void test_parse_roundtrip(void)
{
    char buf[64];
    stoNoticeBuild(buf, sizeof(buf), "DK5EN-93", 17, "DK5EN-14");

    uint16_t nnn = 0;
    char     dst[STO_NOTICE_CALL_MAX];
    dst[0] = 0x7F;

    TEST_ASSERT_TRUE(stoNoticeParse(buf, &nnn, dst));
    TEST_ASSERT_EQUAL_UINT16(17, nnn);
    TEST_ASSERT_EQUAL_STRING("DK5EN-14", dst);
}

static void test_parse_nnn_und_dst_optional_null(void)
{
    char buf[64];
    stoNoticeBuild(buf, sizeof(buf), "DK5EN-93", 42, "DK5EN-14");

    // nnn only
    uint16_t nnn = 0;
    TEST_ASSERT_TRUE(stoNoticeParse(buf, &nnn, NULL));
    TEST_ASSERT_EQUAL_UINT16(42, nnn);

    // dst only
    char dst[STO_NOTICE_CALL_MAX];
    TEST_ASSERT_TRUE(stoNoticeParse(buf, NULL, dst));
    TEST_ASSERT_EQUAL_STRING("DK5EN-14", dst);

    // neither
    TEST_ASSERT_TRUE(stoNoticeParse(buf, NULL, NULL));
}

static void test_parse_lehnt_geschweifte_klammer_ab(void)
{
    uint16_t nnn = 99;
    char     dst[STO_NOTICE_CALL_MAX];
    strcpy(dst, "X");

    TEST_ASSERT_FALSE(stoNoticeParse("DK5EN    :sto017 DK5EN-14{003", &nnn, dst));
    // On rejection nnn/dst are cleared, not left stale.
    TEST_ASSERT_EQUAL_UINT16(0, nnn);
    TEST_ASSERT_EQUAL_STRING("", dst);
}

static void test_parse_lehnt_ack_und_rej_ab(void)
{
    uint16_t nnn;
    TEST_ASSERT_FALSE(stoNoticeParse("DK5EN    :ack017", &nnn, NULL));
    TEST_ASSERT_FALSE(stoNoticeParse("DK5EN    :rej017", &nnn, NULL));
}

static void test_parse_lehnt_fehlenden_tag_ab(void)
{
    uint16_t nnn;
    TEST_ASSERT_FALSE(stoNoticeParse("Hallo, wie geht's?", &nnn, NULL));
    TEST_ASSERT_FALSE(stoNoticeParse("", &nnn, NULL));
}

static void test_parse_lehnt_nicht_numerische_stellen_ab(void)
{
    uint16_t nnn = 5;
    TEST_ASSERT_FALSE(stoNoticeParse("DK5EN    :stoABC DK5EN-14", &nnn, NULL));
    TEST_ASSERT_FALSE(stoNoticeParse("DK5EN    :sto1x2 DK5EN-14", &nnn, NULL));
    // too short a tail (only two digits) is not a valid NNN either
    TEST_ASSERT_FALSE(stoNoticeParse("DK5EN    :sto12", &nnn, NULL));
}

static void test_parse_null_payload_ist_sicher(void)
{
    uint16_t nnn = 7;
    char     dst[STO_NOTICE_CALL_MAX];
    strcpy(dst, "X");

    TEST_ASSERT_FALSE(stoNoticeParse(NULL, &nnn, dst));
    TEST_ASSERT_EQUAL_UINT16(0, nnn);
    TEST_ASSERT_EQUAL_STRING("", dst);
}

static void test_parse_destination_laenger_als_puffer_wird_gekappt(void)
{
    char dst[STO_NOTICE_CALL_MAX];
    // A destination call longer than STO_NOTICE_CALL_MAX-1 must not overflow
    // the caller's buffer -- the parser truncates, it never writes past it.
    TEST_ASSERT_TRUE(stoNoticeParse("DK5EN    :sto017 OE1ABCDEFGH-99", NULL, dst));
    TEST_ASSERT_EQUAL_INT(STO_NOTICE_CALL_MAX - 1, (int)strlen(dst));
}

// -------------------------------------------------------------- holder table

static void test_holder_note_erster_aufruf_erlaubt_und_gemerkt(void)
{
    TEST_ASSERT_TRUE(stoHolderNote(100, "DK5EN-90", 17, 1000));
    TEST_ASSERT_EQUAL_STRING("DK5EN-90", stoHolder(100));
}

static void test_holder_note_innerhalb_fenster_gesperrt(void)
{
    TEST_ASSERT_TRUE(stoHolderNote(100, "DK5EN-90", 17, 1000));
    TEST_ASSERT_FALSE(stoHolderNote(100, "DK5EN-90", 17, 1000 + (STO_NOTICE_WINDOW_MS - 1)));
    // still remembered, the refused call did not touch the table
    TEST_ASSERT_EQUAL_STRING("DK5EN-90", stoHolder(100));
}

static void test_holder_note_nach_fenster_wieder_erlaubt(void)
{
    TEST_ASSERT_TRUE(stoHolderNote(100, "DK5EN-90", 17, 1000));
    TEST_ASSERT_TRUE(stoHolderNote(100, "DK5EN-90", 17, 1000 + STO_NOTICE_WINDOW_MS));
}

static void test_holder_note_millis_rollover(void)
{
    uint32_t near_wrap = 0xFFFFFFF0UL;
    TEST_ASSERT_TRUE(stoHolderNote(1, "DK5EN-90", 5, near_wrap));

    // 32 ms after the wrap: clearly still inside the window.
    uint32_t wrapped = 0x00000010UL;
    TEST_ASSERT_FALSE(stoHolderNote(1, "DK5EN-90", 5, wrapped));

    uint32_t after_window = (uint32_t)(near_wrap + STO_NOTICE_WINDOW_MS);
    TEST_ASSERT_TRUE(stoHolderNote(1, "DK5EN-90", 5, after_window));
}

static void test_holder_note_anderer_holder_ist_ein_eigener_eintrag(void)
{
    // A second holder for the same msg_id is tracked separately -- not
    // rate-limited against the first holder's note.
    TEST_ASSERT_TRUE(stoHolderNote(100, "DK5EN-90", 17, 1000));
    TEST_ASSERT_TRUE(stoHolderNote(100, "DK5EN-91", 17, 1001));
}

static void test_holder_liefert_den_zuletzt_notierten(void)
{
    TEST_ASSERT_TRUE(stoHolderNote(100, "DK5EN-90", 17, 1000));
    TEST_ASSERT_TRUE(stoHolderNote(100, "DK5EN-91", 17, 2000));
    TEST_ASSERT_EQUAL_STRING("DK5EN-91", stoHolder(100));

    // A fresh (later) note from the first holder overtakes again.
    TEST_ASSERT_TRUE(stoHolderNote(100, "DK5EN-90", 17, 2000 + STO_NOTICE_WINDOW_MS));
    TEST_ASSERT_EQUAL_STRING("DK5EN-90", stoHolder(100));
}

static void test_holder_unbekannte_msg_id_liefert_leerstring(void)
{
    TEST_ASSERT_EQUAL_STRING("", stoHolder(999));
}

static void test_holder_clear_entfernt_alle_eintraege_der_msg_id(void)
{
    TEST_ASSERT_TRUE(stoHolderNote(100, "DK5EN-90", 17, 1000));
    TEST_ASSERT_TRUE(stoHolderNote(100, "DK5EN-91", 17, 1001));
    TEST_ASSERT_TRUE(stoHolderNote(200, "DK5EN-92", 3, 1002));   // different msg_id, untouched

    stoHolderClear(100);

    TEST_ASSERT_EQUAL_STRING("", stoHolder(100));
    TEST_ASSERT_EQUAL_STRING("DK5EN-92", stoHolder(200));

    // Cleared entries free their slots: noting (100, DK5EN-90) again is
    // immediately allowed, not rate-limited against the cleared note.
    TEST_ASSERT_TRUE(stoHolderNote(100, "DK5EN-90", 17, 1001));
}

// STO_HOLDER_SLOTS (16) distinct (msg_id, holder) pairs fill the table; the
// 17th evicts the physically oldest (by noted_ms) rather than being refused.
static void test_holder_table_verdraengt_den_aeltesten_bei_voller_tabelle(void)
{
    for(int i = 0; i < STO_HOLDER_SLOTS; i++)
        TEST_ASSERT_TRUE(stoHolderNote((uint32_t)i, "DK5EN-90", (uint16_t)i, (uint32_t)(1000 + i)));

    // Table full: a 17th distinct pair evicts slot 0 (msg_id 0, noted at 1000
    // -- the smallest timestamp, hence the oldest).
    TEST_ASSERT_TRUE(stoHolderNote(1000, "DK5EN-90", 0, 5000));
    TEST_ASSERT_EQUAL_STRING("", stoHolder(0));           // evicted
    TEST_ASSERT_EQUAL_STRING("DK5EN-90", stoHolder(1000)); // the replacement

    // Every other original entry is untouched.
    for(int i = 1; i < STO_HOLDER_SLOTS; i++)
        TEST_ASSERT_EQUAL_STRING("DK5EN-90", stoHolder((uint32_t)i));
}

static void test_holder_note_leeres_oder_null_rufzeichen_abgelehnt(void)
{
    TEST_ASSERT_FALSE(stoHolderNote(1, NULL, 1, 1000));
    TEST_ASSERT_FALSE(stoHolderNote(1, "", 1, 1000));
    TEST_ASSERT_EQUAL_STRING("", stoHolder(1));
}

static void test_holder_clear_und_stoHolder_auf_leerer_tabelle_sind_sicher(void)
{
    stoHolderClear(12345);   // no crash on an id that was never noted
    TEST_ASSERT_EQUAL_STRING("", stoHolder(12345));
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_build_kurzes_rufzeichen_wird_gepolstert);
    RUN_TEST(test_build_neunstelliges_rufzeichen_ungepolstert);
    RUN_TEST(test_build_laengeres_rufzeichen_wird_auf_neun_gekappt);
    RUN_TEST(test_build_nnn_wird_auf_drei_stellen_modulo_1000_geklemmt);
    RUN_TEST(test_build_null_argumente_und_zu_kleiner_puffer);

    RUN_TEST(test_parse_roundtrip);
    RUN_TEST(test_parse_nnn_und_dst_optional_null);
    RUN_TEST(test_parse_lehnt_geschweifte_klammer_ab);
    RUN_TEST(test_parse_lehnt_ack_und_rej_ab);
    RUN_TEST(test_parse_lehnt_fehlenden_tag_ab);
    RUN_TEST(test_parse_lehnt_nicht_numerische_stellen_ab);
    RUN_TEST(test_parse_null_payload_ist_sicher);
    RUN_TEST(test_parse_destination_laenger_als_puffer_wird_gekappt);

    RUN_TEST(test_holder_note_erster_aufruf_erlaubt_und_gemerkt);
    RUN_TEST(test_holder_note_innerhalb_fenster_gesperrt);
    RUN_TEST(test_holder_note_nach_fenster_wieder_erlaubt);
    RUN_TEST(test_holder_note_millis_rollover);
    RUN_TEST(test_holder_note_anderer_holder_ist_ein_eigener_eintrag);
    RUN_TEST(test_holder_liefert_den_zuletzt_notierten);
    RUN_TEST(test_holder_unbekannte_msg_id_liefert_leerstring);
    RUN_TEST(test_holder_clear_entfernt_alle_eintraege_der_msg_id);
    RUN_TEST(test_holder_table_verdraengt_den_aeltesten_bei_voller_tabelle);
    RUN_TEST(test_holder_note_leeres_oder_null_rufzeichen_abgelehnt);
    RUN_TEST(test_holder_clear_und_stoHolder_auf_leerer_tabelle_sind_sicher);

    return UNITY_END();
}
