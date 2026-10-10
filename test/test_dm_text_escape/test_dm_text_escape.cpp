// Native Testsuite fuer dm_text_escape.h (P15): die {ping}/{SET}-Ausnahme
// vom Klammer-Escape, den sendMessage() bei einer DM auf jedes '{' im Text
// anwendet ("A '{' inside the user text breaks the receiver's NNN parse").
// Arduino-frei, reine char*-Schnittstelle -- kein Link gegen loop_functions.cpp
// oder sonst etwas noetig.
//
//   pio test -e native -f test_dm_text_escape

#include <unity.h>

#include <string.h>

#include <dm_text_escape.h>

void setUp(void) {}
void tearDown(void) {}

// ---- Ausnahme greift: fuehrendes {ping}/{SET}-Tag -------------------------

static void test_ping_tag_wird_ab_index_6_escaped(void)
{
    TEST_ASSERT_EQUAL_UINT(6, dmTextEscapeFrom("{ping}"));
}

static void test_set_tag_mit_argumenten_wird_ab_index_5_escaped(void)
{
    TEST_ASSERT_EQUAL_UINT(5, dmTextEscapeFrom("{SET}4;2;"));
}

// Ein '{' NACH dem {ping}-Tag bricht weiterhin den NNN-Parse des Empfaengers
// und muss ab dem zurueckgegebenen Index erkennbar (= escapebar) bleiben --
// dmTextEscapeFrom() selbst escaped nichts, es liefert nur den Startindex;
// der Aufrufer (sendMessage()) macht die Ersetzung.
static void test_brace_nach_ping_tag_bleibt_ab_index_erkennbar(void)
{
    const char *text = "{ping}x{y";
    size_t from = dmTextEscapeFrom(text);
    TEST_ASSERT_EQUAL_UINT(6, from);

    bool foundBraceAfterFrom = false;
    for(size_t i = from; i < strlen(text); i++)
        if(text[i] == '{')
            foundBraceAfterFrom = true;
    TEST_ASSERT_TRUE(foundBraceAfterFrom);
}

// ---- Ausnahme greift NICHT ------------------------------------------------

static void test_normaler_text_wird_ab_index_0_escaped(void)
{
    TEST_ASSERT_EQUAL_UINT(0, dmTextEscapeFrom("hello{x"));
}

static void test_cet_tag_ist_nicht_ausgenommen(void)
{
    TEST_ASSERT_EQUAL_UINT(0, dmTextEscapeFrom("{CET}"));
}

// Nur ein EXAKTES Tag zaehlt -- ein zusaetzliches Zeichen statt der
// schliessenden Klammer darf nicht treffen.
static void test_pingx_ist_kein_exaktes_tag(void)
{
    TEST_ASSERT_EQUAL_UINT(0, dmTextEscapeFrom("{pingx"));
}

static void test_setx_ist_kein_exaktes_tag(void)
{
    TEST_ASSERT_EQUAL_UINT(0, dmTextEscapeFrom("{SETX"));
}

// Gross-/Kleinschreibung zaehlt -- wie bei den Empfaengern, die ebenfalls
// mit einem case-sensitiven startsWith() pruefen (sendDisplayText() auf
// "{SET}", der {pong}-Antwortpfad auf "{ping}").
static void test_ping_grossgeschrieben_ist_nicht_exakt(void)
{
    TEST_ASSERT_EQUAL_UINT(0, dmTextEscapeFrom("{PING}"));
}

static void test_set_kleingeschrieben_ist_nicht_exakt(void)
{
    TEST_ASSERT_EQUAL_UINT(0, dmTextEscapeFrom("{set}"));
}

// ---- Randfaelle -------------------------------------------------------

static void test_leerer_text_liefert_null(void)
{
    TEST_ASSERT_EQUAL_UINT(0, dmTextEscapeFrom(""));
}

static void test_nullptr_liefert_null(void)
{
    TEST_ASSERT_EQUAL_UINT(0, dmTextEscapeFrom(NULL));
}

// ---- W0c: RM1-Frame = genau einmal, ohne {NNN, ohne Wiederholung ------------

static void test_rm1_frame_is_no_ack_no_retry(void)
{
    // Kommando und Antwort: beide Frames gehen einmalig raus.
    TEST_ASSERT_TRUE(dmTextIsRm1Frame("RM1 17 status 0123456789abcdef"));
    TEST_ASSERT_TRUE(dmTextIsRm1Frame("RM1 17 ok rebooting 0123456789abcdef"));
    TEST_ASSERT_TRUE(dmTextNoRetransmit("RM1 17 status 0123456789abcdef", true));
    TEST_ASSERT_TRUE(dmTextNoRetransmit("RM1 17 ok rebooting 0123456789abcdef", true));
    // Mit dem (aelteren Sendern angehaengten) {NNN-Suffix gilt es ebenso.
    TEST_ASSERT_TRUE(dmTextNoRetransmit("RM1 17 status 0123456789abcdef{042", true));
}

static void test_rm1_prefix_is_exact(void)
{
    const char *no[] = {"RM10 17 status 0123456789abcdef", "rm1 17 status 0123456789abcdef",
                        " RM1 17 status 0123456789abcdef", "xRM1 17 status 0123456789abcdef",
                        "RM1", "RM1x 17", "Hallo RM1 17 status", "{RM1 17", ""};
    for(size_t i = 0; i < sizeof(no) / sizeof(no[0]); i++)
    {
        TEST_ASSERT_FALSE_MESSAGE(dmTextIsRm1Frame(no[i]), no[i]);
        TEST_ASSERT_FALSE_MESSAGE(dmTextNoRetransmit(no[i], true), no[i]);
    }
    TEST_ASSERT_FALSE(dmTextIsRm1Frame(NULL));
    TEST_ASSERT_FALSE(dmTextNoRetransmit(NULL, true));
}

static void test_no_retransmit_class_keeps_cet_mcp_set(void)
{
    TEST_ASSERT_TRUE(dmTextNoRetransmit("{CET}2026-10-06 12:00:00", true));
    TEST_ASSERT_TRUE(dmTextNoRetransmit("{MCP}0123", true));
    TEST_ASSERT_TRUE(dmTextNoRetransmit("{SET}4;2;", true));
    // Suffix-Entscheidung bleibt RM1-only: {CET}/{SET} sind kein RM1-Frame.
    TEST_ASSERT_FALSE(dmTextIsRm1Frame("{CET}2026-10-06 12:00:00"));
    TEST_ASSERT_FALSE(dmTextIsRm1Frame("{SET}4;2;"));
    // Ein normaler Text und {ping} (eigener bUseOnce-Pfad) bleiben ausserhalb.
    TEST_ASSERT_FALSE(dmTextNoRetransmit("Hallo Welt", true));
    TEST_ASSERT_FALSE(dmTextNoRetransmit("{ping}", true));
    TEST_ASSERT_FALSE(dmTextNoRetransmit("{cet}x", true));
    // Gruppentext: {CET}/{MCP}/{SET} bleiben ohne Wiederholung, ein Gruppentext
    // mit fuehrendem "RM1 " wird dagegen wie jeder Gruppentext wiederholt.
    TEST_ASSERT_TRUE(dmTextNoRetransmit("{CET}2026-10-06 12:00:00", false));
    TEST_ASSERT_TRUE(dmTextNoRetransmit("{SET}4;2;", false));
    TEST_ASSERT_FALSE(dmTextNoRetransmit("RM1 test", false));
    TEST_ASSERT_FALSE(dmTextNoRetransmit("RM1 17 status 0123456789abcdef", false));
}

// {mcp} ist die Draht-Form: sendMessage() schreibt ein getipptes {MCP}/{mcp}
// nach {ZIEL} in "{mcp}<id>" um, bevor der Tag-Check laeuft. {mcp} ist die
// einzige kleingeschriebene Ausnahme ({cet}/{set} bleiben unerkannt).
static void test_mcp_lowercase_wire_form_is_no_retransmit(void)
{
    TEST_ASSERT_TRUE(dmTextNoRetransmit("{mcp}0a1b2c", false));
    TEST_ASSERT_TRUE(dmTextNoRetransmit("{mcp}0a1b2c", true));
}

static void test_send_once_covers_ping_tags_and_rm1(void)
{
    const char *tags[] = {"{ping}", "{CET}2026-10-06 12:00:00", "{SET}4;2;", "{MCP}0123", "{mcp}0a1b2c"};
    for(size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(dmTextSendOnce(tags[i], true), tags[i]);
        TEST_ASSERT_TRUE_MESSAGE(dmTextSendOnce(tags[i], false), tags[i]);
    }
    TEST_ASSERT_TRUE(dmTextSendOnce("RM1 17 status 0123456789abcdef", true));
}

static void test_send_once_excludes_retry_texts(void)
{
    // {cet}x: {mcp} ist die einzige kleingeschriebene Ausnahme.
    TEST_ASSERT_FALSE(dmTextSendOnce("Hallo Welt", true));
    TEST_ASSERT_FALSE(dmTextSendOnce("{cet}x", true));
    TEST_ASSERT_FALSE(dmTextSendOnce("{pingx", true));
    // Gruppentext mit fuehrendem "RM1 " wird wiederholt.
    TEST_ASSERT_FALSE(dmTextSendOnce("RM1 test", false));
    TEST_ASSERT_FALSE(dmTextSendOnce(NULL, true));
    TEST_ASSERT_FALSE(dmTextSendOnce(NULL, false));
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_ping_tag_wird_ab_index_6_escaped);
    RUN_TEST(test_set_tag_mit_argumenten_wird_ab_index_5_escaped);
    RUN_TEST(test_brace_nach_ping_tag_bleibt_ab_index_erkennbar);
    RUN_TEST(test_normaler_text_wird_ab_index_0_escaped);
    RUN_TEST(test_cet_tag_ist_nicht_ausgenommen);
    RUN_TEST(test_pingx_ist_kein_exaktes_tag);
    RUN_TEST(test_setx_ist_kein_exaktes_tag);
    RUN_TEST(test_ping_grossgeschrieben_ist_nicht_exakt);
    RUN_TEST(test_set_kleingeschrieben_ist_nicht_exakt);
    RUN_TEST(test_leerer_text_liefert_null);
    RUN_TEST(test_nullptr_liefert_null);
    RUN_TEST(test_rm1_frame_is_no_ack_no_retry);
    RUN_TEST(test_rm1_prefix_is_exact);
    RUN_TEST(test_no_retransmit_class_keeps_cet_mcp_set);
    RUN_TEST(test_mcp_lowercase_wire_form_is_no_retransmit);
    RUN_TEST(test_send_once_covers_ping_tags_and_rm1);
    RUN_TEST(test_send_once_excludes_retry_texts);
    return UNITY_END();
}
