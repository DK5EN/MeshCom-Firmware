// Native Testsuite fuer die dauerhafte Zustellung-Statustabelle eigener Texte
// (docs/webgui-ack-ticks-verdict-20261004.md, Finding 1 / Plan B1).
//
//   pio test -e native -f test_own_msg_status
//
// Vor B1 gab es fuer den Tick einer gesendeten Nachricht nur checkOwnTx() ueber
// den gemeinsamen 20-Slot-Ring own_msg_id: Positionen, ACKs und Gateway-
// Weiterleitungen verbrauchen dort ebenfalls Slots, ein Text, der noch auf
// sein ACK wartet, war nach ca. 20 eigenen Frames ueberschrieben und der
// Web-GUI-Tick verschwand. Die Tabelle hier haelt NUR eigene Texte.

#include <unity.h>

#include <stdint.h>

#include <own_msg_status.h>

void setUp(void)
{
    ownMsgStatusReset();
}

void tearDown(void) {}

// Eigene Texte verdraengen sich erst nach SLOTS neueren Texten.
static void test_status_ueberlebt_weniger_als_slots_neue_texte(void)
{
    ownMsgStatusRegister(100);
    ownMsgStatusSet(100, 0x02);
    for (uint32_t i = 1; i < OWN_MSG_STATUS_SLOTS; i++)
        ownMsgStatusRegister(1000 + i);
    TEST_ASSERT_EQUAL(0x02, ownMsgStatusGet(100));
}

static void test_monoton_ack_dann_heard(void)
{
    ownMsgStatusRegister(1);
    ownMsgStatusSet(1, 0x02);
    ownMsgStatusSet(1, 0x01);
    TEST_ASSERT_EQUAL(0x02, ownMsgStatusGet(1));
}

static void test_monoton_held_dann_ack(void)
{
    ownMsgStatusRegister(1);
    ownMsgStatusSet(1, 0x04);
    ownMsgStatusSet(1, 0x02);
    TEST_ASSERT_EQUAL(0x02, ownMsgStatusGet(1));
}

static void test_monoton_failed_dann_heard(void)
{
    ownMsgStatusRegister(1);
    ownMsgStatusSet(1, 0x03);
    ownMsgStatusSet(1, 0x01);
    TEST_ASSERT_EQUAL(0x03, ownMsgStatusGet(1));
}

static void test_monoton_held_dann_failed(void)
{
    ownMsgStatusRegister(1);
    ownMsgStatusSet(1, 0x04);
    ownMsgStatusSet(1, 0x03);
    TEST_ASSERT_EQUAL(0x03, ownMsgStatusGet(1));
}

static void test_monoton_heard_dann_held(void)
{
    ownMsgStatusRegister(1);
    ownMsgStatusSet(1, 0x01);
    ownMsgStatusSet(1, 0x04);
    TEST_ASSERT_EQUAL(0x04, ownMsgStatusGet(1));
}

static void test_monoton_ack_ueberschreibt_failed_nicht_umgekehrt(void)
{
    ownMsgStatusRegister(1);
    ownMsgStatusSet(1, 0x03);
    ownMsgStatusSet(1, 0x02);
    TEST_ASSERT_EQUAL(0x02, ownMsgStatusGet(1));
    ownMsgStatusSet(1, 0x03);
    TEST_ASSERT_EQUAL(0x02, ownMsgStatusGet(1));
}

static void test_rang_tabelle(void)
{
    TEST_ASSERT_EQUAL(0, ownMsgStatusRank(0x00));
    TEST_ASSERT_EQUAL(1, ownMsgStatusRank(0x01));
    TEST_ASSERT_EQUAL(2, ownMsgStatusRank(0x04));
    TEST_ASSERT_EQUAL(3, ownMsgStatusRank(0x03));
    TEST_ASSERT_EQUAL(4, ownMsgStatusRank(0x02));
    TEST_ASSERT_EQUAL(0, ownMsgStatusRank(0x7F));
}

static void test_verdraengung_nur_durch_eigene_texte(void)
{
    for (uint32_t i = 1; i <= OWN_MSG_STATUS_SLOTS + 1; i++)
        ownMsgStatusRegister(i);
    TEST_ASSERT_EQUAL(-1, ownMsgStatusGet(1));
    for (uint32_t i = 2; i <= OWN_MSG_STATUS_SLOTS + 1; i++)
        TEST_ASSERT_EQUAL(0x00, ownMsgStatusGet(i));
}

static void test_unbekannte_id(void)
{
    ownMsgStatusSet(77, 0x02);
    TEST_ASSERT_EQUAL(-1, ownMsgStatusGet(77));
    TEST_ASSERT_EQUAL(-1, ownMsgStatusGet(12345));
}

static void test_id_null_wird_nie_registriert(void)
{
    ownMsgStatusRegister(0);
    ownMsgStatusSet(0, 0x02);
    TEST_ASSERT_EQUAL(-1, ownMsgStatusGet(0));
    // und verbraucht keinen Slot
    for (uint32_t i = 1; i <= OWN_MSG_STATUS_SLOTS; i++)
        ownMsgStatusRegister(i);
    TEST_ASSERT_EQUAL(0x00, ownMsgStatusGet(1));
}

static void test_re_register_behaelt_status(void)
{
    ownMsgStatusRegister(5);
    ownMsgStatusSet(5, 0x02);
    ownMsgStatusRegister(5);
    TEST_ASSERT_EQUAL(0x02, ownMsgStatusGet(5));
    // und rueckt den Schreibzeiger nicht vor
    for (uint32_t i = 1; i < OWN_MSG_STATUS_SLOTS; i++)
        ownMsgStatusRegister(500 + i);
    TEST_ASSERT_EQUAL(0x02, ownMsgStatusGet(5));
}

static void test_neu_registriert_startet_bei_null(void)
{
    ownMsgStatusRegister(9);
    TEST_ASSERT_EQUAL(0x00, ownMsgStatusGet(9));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_status_ueberlebt_weniger_als_slots_neue_texte);
    RUN_TEST(test_monoton_ack_dann_heard);
    RUN_TEST(test_monoton_held_dann_ack);
    RUN_TEST(test_monoton_failed_dann_heard);
    RUN_TEST(test_monoton_held_dann_failed);
    RUN_TEST(test_monoton_heard_dann_held);
    RUN_TEST(test_monoton_ack_ueberschreibt_failed_nicht_umgekehrt);
    RUN_TEST(test_rang_tabelle);
    RUN_TEST(test_verdraengung_nur_durch_eigene_texte);
    RUN_TEST(test_unbekannte_id);
    RUN_TEST(test_id_null_wird_nie_registriert);
    RUN_TEST(test_re_register_behaelt_status);
    RUN_TEST(test_neu_registriert_startet_bei_null);
    return UNITY_END();
}
