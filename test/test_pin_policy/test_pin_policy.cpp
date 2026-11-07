// Unity tests of src/pin_policy.h. Host only. Motivating defect: the RAK4631 brick (P0.00/P0.01 on the 32 kHz crystal).
#include <unity.h>
#include <string.h>
#include "../../src/pin_policy.h"

void setUp(void) {}
void tearDown(void) {}

static PinContext ctx(PinChip c, bool octal, const PinBoardTable *t, uint64_t res = 0, uint64_t cl = 0)
{
    PinContext x;
    x.chip = c; x.octal = octal; x.reserved = res; x.claimed = cl; x.table = t;
    return x;
}
static uint64_t B(int n) { return 1ULL << n; }

static size_t tableCount() { size_t n = 0; pinBoardAt(0, &n); return n; }

void test_tables_consistent(void)
{
    TEST_ASSERT_TRUE(tableCount() >= 4);
    for (size_t i = 0; i < tableCount(); i++)
    {
        const PinBoardTable *t = pinBoardAt(i, NULL);
        TEST_ASSERT_NOT_NULL(t);
        TEST_ASSERT_TRUE((t->used & t->freeOk) == 0);
        for (int p = 0; p < 64; p++)
        {
            if (!(t->freeOk & B(p))) continue;
            TEST_ASSERT_TRUE(pinChipExists(t->chip, p));
            TEST_ASSERT_FALSE(pinChipForbidden(t->chip, t->octal, p));
            TEST_ASSERT_FALSE(pinChipStrapping(t->chip, p));
            TEST_ASSERT_FALSE(pinChipInputOnly(t->chip, p));
        }
        TEST_ASSERT_EQUAL_PTR(t, pinBoardFor(t->env));
    }
}

void test_exact_sets(void)
{
    const char *tb[] = {"ttgo_tbeam", "ttgo_tbeam_SX1262", "ttgo_tbeam_SX1268"};
    for (int i = 0; i < 3; i++)
    {
        const PinBoardTable *t = pinBoardFor(tb[i]);
        TEST_ASSERT_NOT_NULL(t);
        TEST_ASSERT_TRUE(t->freeOk == (B(13) | B(25)));
    }
    const PinBoardTable *h = pinBoardFor("heltec_wifi_lora_32_V3");
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_TRUE(h->freeOk == (B(2) | B(4) | B(5) | B(6)));
}

void test_unknown_env_no_output(void)
{
    const char *e[] = {"t_deck_plus", "unknown", NULL};
    for (int i = 0; i < 3; i++)
    {
        const PinBoardTable *t = pinBoardFor(e[i]);
        TEST_ASSERT_NULL(t);
        PinChip chips[] = {PIN_CHIP_ESP32, PIN_CHIP_ESP32S3, PIN_CHIP_NRF52840, PIN_CHIP_NONE};
        for (int k = 0; k < 4; k++)
            for (int p = 0; p < 64; p++) TEST_ASSERT_FALSE(pinCanOutput(ctx(chips[k], false, t), p));
    }
}

void test_classify_classic(void)
{
    PinContext c = ctx(PIN_CHIP_ESP32, false, NULL);
    for (int p = 6; p <= 11; p++) TEST_ASSERT_EQUAL(PIN_FORBIDDEN, pinClassify(c, p));
    TEST_ASSERT_EQUAL(PIN_FORBIDDEN, pinClassify(c, 1));
    TEST_ASSERT_EQUAL(PIN_FORBIDDEN, pinClassify(c, 3));
    TEST_ASSERT_EQUAL(PIN_FORBIDDEN, pinClassify(c, 24));
    TEST_ASSERT_EQUAL(PIN_FORBIDDEN, pinClassify(c, 40));
    TEST_ASSERT_EQUAL(PIN_FORBIDDEN, pinClassify(c, -1));
    TEST_ASSERT_EQUAL(PIN_INPUT_ONLY, pinClassify(c, 34));
    TEST_ASSERT_EQUAL(PIN_STRAPPING, pinClassify(c, 12));
    TEST_ASSERT_EQUAL(PIN_UNREVIEWED, pinClassify(c, 13));
}

void test_classify_s3(void)
{
    PinContext q = ctx(PIN_CHIP_ESP32S3, false, NULL), o = ctx(PIN_CHIP_ESP32S3, true, NULL);
    TEST_ASSERT_EQUAL(PIN_FORBIDDEN, pinClassify(q, 19));
    TEST_ASSERT_EQUAL(PIN_FORBIDDEN, pinClassify(q, 20));
    TEST_ASSERT_EQUAL(PIN_FORBIDDEN, pinClassify(q, 43));
    TEST_ASSERT_EQUAL(PIN_FORBIDDEN, pinClassify(q, 23));
    TEST_ASSERT_EQUAL(PIN_UNREVIEWED, pinClassify(q, 33));
    TEST_ASSERT_EQUAL(PIN_FORBIDDEN, pinClassify(o, 33));
    TEST_ASSERT_EQUAL(PIN_STRAPPING, pinClassify(q, 45));
}

void test_classify_nrf52(void)
{
    PinContext c = ctx(PIN_CHIP_NRF52840, false, NULL);
    for (int p = -1; p <= 48; p++)
    {
        PinClass k = pinClassify(c, p);
        if (p < 0 || p > 47 || mcPinReservedNrf52840(p)) TEST_ASSERT_EQUAL(PIN_FORBIDDEN, k);
        else TEST_ASSERT_EQUAL(PIN_UNREVIEWED, k);
        TEST_ASSERT_FALSE(pinCanOutput(c, p));
    }
    TEST_ASSERT_EQUAL(PIN_FORBIDDEN, pinClassify(c, 0));
    TEST_ASSERT_EQUAL(PIN_FORBIDDEN, pinClassify(c, 1));
    TEST_ASSERT_EQUAL(PIN_FORBIDDEN, pinClassify(c, 18));
    TEST_ASSERT_FALSE(pinCanAssign(c, PIN_FN_BUTTON, 1, 0));
    TEST_ASSERT_TRUE(pinCanAssign(c, PIN_FN_BUTTON, 5, 0));
}

void test_free_reserved_claimed(void)
{
    const PinBoardTable *t = pinBoardFor("ttgo_tbeam");
    PinContext c = ctx(PIN_CHIP_ESP32, false, t);
    TEST_ASSERT_EQUAL(PIN_FREE, pinClassify(c, 13));
    TEST_ASSERT_TRUE(pinCanOutput(c, 25));
    TEST_ASSERT_FALSE(pinCanOutput(c, 14));
    TEST_ASSERT_FALSE(pinCanOutput(c, 4));
    PinContext r = ctx(PIN_CHIP_ESP32, false, t, B(13));
    TEST_ASSERT_EQUAL(PIN_FORBIDDEN, pinClassify(r, 13));
    TEST_ASSERT_FALSE(pinCanOutput(r, 13));
    PinContext cl = ctx(PIN_CHIP_ESP32, false, t, 0, B(13));
    TEST_ASSERT_EQUAL(PIN_CLAIMED, pinClassify(cl, 13));
    TEST_ASSERT_FALSE(pinCanOutput(cl, 13));
    TEST_ASSERT_FALSE(pinCanAssign(cl, PIN_FN_BUTTON, 13, 0));
}

void test_assign(void)
{
    PinContext c = ctx(PIN_CHIP_ESP32, false, NULL);
    for (int f = 0; f < PIN_FN_COUNT; f++) TEST_ASSERT_TRUE(pinCanAssign(c, (PinFunc)f, 0, 0));
    TEST_ASSERT_TRUE(pinCanAssign(c, PIN_FN_BUTTON, 0, 0));
    TEST_ASSERT_TRUE(pinCanAssign(c, PIN_FN_BUTTON, 12, 12));  // default on a strapping pin
    TEST_ASSERT_FALSE(pinCanAssign(c, PIN_FN_BUTTON, 12, 38)); // non-default strapping
    TEST_ASSERT_FALSE(pinCanAssign(c, PIN_FN_BUTTON, 12, 0));
    TEST_ASSERT_TRUE(pinCanAssign(c, PIN_FN_BUTTON, 34, 0));
    TEST_ASSERT_TRUE(pinCanAssign(c, PIN_FN_ANALOG, 36, 0));
    TEST_ASSERT_TRUE(pinCanAssign(c, PIN_FN_WIND, 35, 0));
    TEST_ASSERT_FALSE(pinCanAssign(c, PIN_FN_ONEWIRE, 34, 0));
    TEST_ASSERT_FALSE(pinCanAssign(c, PIN_FN_BUTTON, 6, 0));
    TEST_ASSERT_TRUE(pinCanAssign(c, PIN_FN_ONEWIRE, 13, 0));
}

void test_names(void)
{
    for (int k = PIN_FORBIDDEN; k <= PIN_FREE; k++) TEST_ASSERT_TRUE(strlen(pinClassName((PinClass)k)) > 0);
    for (int f = 0; f < PIN_FN_COUNT; f++) TEST_ASSERT_TRUE(strlen(pinFuncName((PinFunc)f)) > 0);
    TEST_ASSERT_EQUAL_STRING("free", pinClassName(PIN_FREE));
    TEST_ASSERT_EQUAL_STRING("wind", pinFuncName(PIN_FN_WIND));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_tables_consistent);
    RUN_TEST(test_exact_sets);
    RUN_TEST(test_unknown_env_no_output);
    RUN_TEST(test_classify_classic);
    RUN_TEST(test_classify_s3);
    RUN_TEST(test_classify_nrf52);
    RUN_TEST(test_free_reserved_claimed);
    RUN_TEST(test_assign);
    RUN_TEST(test_names);
    return UNITY_END();
}
