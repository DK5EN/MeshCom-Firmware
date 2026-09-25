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
    return UNITY_END();
}
