// Unity tests of src/gpio_control.h (GPIO-01 Part B state machine). Host only.
#include <unity.h>
#include "../../src/gpio_control.h"

void setUp(void) {}
void tearDown(void) {}

static uint64_t B(int n) { return 1ULL << n; }

static PinContext tbeam(uint64_t claimed = 0)
{
    PinContext c;
    c.chip = PIN_CHIP_ESP32;
    c.table = pinBoardFor("ttgo_tbeam");
    c.claimed = claimed;
    return c;
}

static GpioCtl on()
{
    GpioCtl g;
    gpioSetMaster(g, true);
    return g;
}

void test_master_off_refuses_everything(void)
{
    GpioCtl g;
    TEST_ASSERT_FALSE(g.master);
    PinClass why = PIN_FREE;
    const GpioOp ops[] = {GPIO_OP_ON, GPIO_OP_OFF, GPIO_OP_IN};
    for (size_t i = 0; i < 3; i++)
        TEST_ASSERT_EQUAL(GPIO_REFUSED_MASTER, gpioDo(g, tbeam(), 13, ops[i], &why));
    TEST_ASSERT_EQUAL_UINT64(0, g.held);
    TEST_ASSERT_EQUAL_UINT64(0, g.out);
    TEST_ASSERT_EQUAL_UINT64(0, g.level);
}

void test_non_free_pins_refused_with_reason(void)
{
    GpioCtl g = on();
    PinClass why = PIN_FREE;
    TEST_ASSERT_EQUAL(GPIO_REFUSED_CLASS, gpioDo(g, tbeam(), 21, GPIO_OP_ON, &why)); // I2C pin, in `used`
    TEST_ASSERT_EQUAL(PIN_UNREVIEWED, why);
    TEST_ASSERT_EQUAL(GPIO_REFUSED_CLASS, gpioDo(g, tbeam(), 2, GPIO_OP_ON, &why));
    TEST_ASSERT_EQUAL(PIN_STRAPPING, why);
    TEST_ASSERT_EQUAL(GPIO_REFUSED_CLASS, gpioDo(g, tbeam(), 6, GPIO_OP_OFF, &why));
    TEST_ASSERT_EQUAL(PIN_FORBIDDEN, why);
    TEST_ASSERT_EQUAL(GPIO_REFUSED_CLASS, gpioDo(g, tbeam(), 34, GPIO_OP_IN, &why));
    TEST_ASSERT_EQUAL(PIN_INPUT_ONLY, why);
    TEST_ASSERT_EQUAL(GPIO_REFUSED_CLASS, gpioDo(g, tbeam(), 99, GPIO_OP_ON, &why));
    TEST_ASSERT_EQUAL(PIN_FORBIDDEN, why);
    TEST_ASSERT_EQUAL(GPIO_REFUSED_CLASS, gpioDo(g, tbeam(), 14, GPIO_OP_ON, &why)); // not in the table
    TEST_ASSERT_EQUAL(PIN_UNREVIEWED, why);
    TEST_ASSERT_EQUAL_UINT64(0, g.held);
}

void test_claimed_pin_refused(void)
{
    GpioCtl g = on();
    PinClass why = PIN_FREE;
    TEST_ASSERT_EQUAL(GPIO_REFUSED_CLASS, gpioDo(g, tbeam(B(13)), 13, GPIO_OP_ON, &why));
    TEST_ASSERT_EQUAL(PIN_CLAIMED, why);
    TEST_ASSERT_EQUAL(GPIO_OK, gpioDo(g, tbeam(B(13)), 25, GPIO_OP_ON, &why));
}

void test_transitions_and_masks(void)
{
    GpioCtl g = on();
    TEST_ASSERT_EQUAL(GPIO_OK, gpioDo(g, tbeam(), 13, GPIO_OP_ON));
    TEST_ASSERT_EQUAL_UINT64(B(13), g.held);
    TEST_ASSERT_EQUAL_UINT64(B(13), g.out);
    TEST_ASSERT_EQUAL_UINT64(B(13), g.level);
    TEST_ASSERT_EQUAL(GPIO_DIR_OUT, gpioDirOf(g, 13));
    TEST_ASSERT_EQUAL(GPIO_OK, gpioDo(g, tbeam(), 13, GPIO_OP_OFF));
    TEST_ASSERT_EQUAL_UINT64(B(13), g.out);
    TEST_ASSERT_EQUAL_UINT64(0, g.level);
    TEST_ASSERT_EQUAL(GPIO_OK, gpioDo(g, tbeam(), 25, GPIO_OP_ON));
    TEST_ASSERT_EQUAL_UINT64(B(13) | B(25), g.held);
    TEST_ASSERT_EQUAL_UINT64(B(25), g.level);
    TEST_ASSERT_EQUAL(GPIO_OK, gpioDo(g, tbeam(), 25, GPIO_OP_IN));
    TEST_ASSERT_EQUAL(GPIO_DIR_IN, gpioDirOf(g, 25));
    TEST_ASSERT_EQUAL_UINT64(B(13) | B(25), g.held);
    TEST_ASSERT_EQUAL_UINT64(B(13), g.out);
    TEST_ASSERT_EQUAL_UINT64(0, g.level);
    TEST_ASSERT_EQUAL(GPIO_OK, gpioDo(g, tbeam(), 25, GPIO_OP_RELEASE));
    TEST_ASSERT_EQUAL(GPIO_DIR_NONE, gpioDirOf(g, 25));
    TEST_ASSERT_EQUAL_UINT64(B(13), g.held);
}

void test_release_only_touches_held_pins(void)
{
    // master off, nothing held: release of ANY pin (free, forbidden, strapping, flash, UART, out of range) is a
    // no-op and must not ask the glue to touch hardware (a stray pinMode(INPUT) would kill the LoRa CS / UART0)
    GpioCtl g;
    const int pins[] = {13, 6, 5, 1, 2, 34, 99, -1};
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++)
    {
        const GpioRes r = gpioDo(g, tbeam(), pins[i], GPIO_OP_RELEASE);
        TEST_ASSERT_EQUAL(GPIO_NOT_HELD, r);
        TEST_ASSERT_FALSE(gpioNeedsHardware(r));
    }
    TEST_ASSERT_EQUAL_UINT64(0, g.held | g.out | g.level);
    // master on does not change that
    GpioCtl h = on();
    TEST_ASSERT_EQUAL(GPIO_NOT_HELD, gpioDo(h, tbeam(), 5, GPIO_OP_RELEASE));
    // a held pin releases with a hardware effect, even after the master went off (state was dropped then: not held)
    TEST_ASSERT_EQUAL(GPIO_OK, gpioDo(h, tbeam(), 13, GPIO_OP_ON));
    h.master = false; // master flag flipped without gpioSetMaster: the pin is still held
    const GpioRes r = gpioDo(h, tbeam(), 13, GPIO_OP_RELEASE);
    TEST_ASSERT_EQUAL(GPIO_OK, r);
    TEST_ASSERT_TRUE(gpioNeedsHardware(r));
    TEST_ASSERT_EQUAL_UINT64(0, h.held | h.out | h.level);
    // second release of the same pin: nothing left
    TEST_ASSERT_EQUAL(GPIO_NOT_HELD, gpioDo(h, tbeam(), 13, GPIO_OP_RELEASE));
    // refusals and successes elsewhere keep their hardware verdicts
    TEST_ASSERT_FALSE(gpioNeedsHardware(GPIO_REFUSED_MASTER));
    TEST_ASSERT_FALSE(gpioNeedsHardware(GPIO_REFUSED_CLASS));
}

void test_release_on_function_claim(void)
{
    GpioCtl g = on();
    gpioDo(g, tbeam(), 13, GPIO_OP_ON);
    gpioDo(g, tbeam(), 25, GPIO_OP_ON);
    TEST_ASSERT_FALSE(gpioReleaseForFunction(g, 4)); // not held: no hardware effect
    TEST_ASSERT_TRUE(gpioReleaseForFunction(g, 13));
    TEST_ASSERT_EQUAL_UINT64(B(25), g.held);
    TEST_ASSERT_EQUAL_UINT64(B(25), g.out);
    TEST_ASSERT_EQUAL_UINT64(B(25), g.level);
    // the pin is now claimed by the function: gpio control refuses it
    PinClass why = PIN_FREE;
    TEST_ASSERT_EQUAL(GPIO_REFUSED_CLASS, gpioDo(g, tbeam(B(13)), 13, GPIO_OP_ON, &why));
    TEST_ASSERT_EQUAL(PIN_CLAIMED, why);
    TEST_ASSERT_FALSE(gpioReleaseForFunction(g, 13)); // second time: nothing to release
}

void test_master_off_releases_all(void)
{
    GpioCtl g = on();
    gpioDo(g, tbeam(), 13, GPIO_OP_ON);
    gpioDo(g, tbeam(), 25, GPIO_OP_IN);
    TEST_ASSERT_EQUAL_UINT64(B(13) | B(25), gpioSetMaster(g, false));
    TEST_ASSERT_FALSE(g.master);
    TEST_ASSERT_EQUAL_UINT64(0, g.held | g.out | g.level);
    TEST_ASSERT_EQUAL(GPIO_REFUSED_MASTER, gpioDo(g, tbeam(), 13, GPIO_OP_ON));
    TEST_ASSERT_EQUAL_UINT64(0, gpioSetMaster(g, true)); // on: nothing was held
}

void test_no_table_board_offers_nothing(void)
{
    PinContext c = tbeam();
    c.table = pinBoardFor("wiscore_rak4631");
    TEST_ASSERT_NULL(c.table);
    GpioCtl g = on();
    PinClass why = PIN_FREE;
    TEST_ASSERT_EQUAL(GPIO_REFUSED_CLASS, gpioDo(g, c, 13, GPIO_OP_ON, &why));
    GpioPinRow rows[8];
    TEST_ASSERT_EQUAL(0, gpioSnapshot(g, c, 0, rows, 8));
    PinContext n;
    n.chip = PIN_CHIP_NRF52840;
    TEST_ASSERT_EQUAL(0, gpioSnapshot(g, n, 0, rows, 8));
}

void test_snapshot_exactly_offered_pins(void)
{
    GpioCtl g = on();
    GpioPinRow rows[8];
    size_t n = gpioSnapshot(g, tbeam(), 0, rows, 8);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL(13, rows[0].pin);
    TEST_ASSERT_EQUAL(25, rows[1].pin);
    TEST_ASSERT_EQUAL(GPIO_DIR_NONE, rows[0].dir);
    TEST_ASSERT_EQUAL(-1, rows[0].level); // untouched: never read
    TEST_ASSERT_EQUAL(PIN_FREE, rows[0].cls);
    gpioDo(g, tbeam(), 13, GPIO_OP_ON);
    gpioDo(g, tbeam(), 25, GPIO_OP_IN);
    n = gpioSnapshot(g, tbeam(), B(25) | B(4), rows, 8);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL(GPIO_DIR_OUT, rows[0].dir);
    TEST_ASSERT_EQUAL(1, rows[0].level);
    TEST_ASSERT_EQUAL(GPIO_DIR_IN, rows[1].dir);
    TEST_ASSERT_EQUAL(1, rows[1].level);
    // a claimed pin drops out of the offer
    TEST_ASSERT_EQUAL(1, gpioSnapshot(g, tbeam(B(13)), 0, rows, 8));
    TEST_ASSERT_EQUAL(25, rows[0].pin);
    // row limit
    TEST_ASSERT_EQUAL(1, gpioSnapshot(g, tbeam(), 0, rows, 1));
}

void test_snapshot_heltec_v3(void)
{
    PinContext c;
    c.chip = PIN_CHIP_ESP32S3;
    c.table = pinBoardFor("heltec_wifi_lora_32_V3");
    GpioPinRow rows[8];
    GpioCtl g = on();
    TEST_ASSERT_EQUAL(4, gpioSnapshot(g, c, 0, rows, 8));
    TEST_ASSERT_EQUAL(2, rows[0].pin);
    TEST_ASSERT_EQUAL(6, rows[3].pin);
}

void test_dir_names(void)
{
    TEST_ASSERT_EQUAL_STRING("out", gpioDirName(GPIO_DIR_OUT));
    TEST_ASSERT_EQUAL_STRING("in", gpioDirName(GPIO_DIR_IN));
    TEST_ASSERT_EQUAL_STRING("-", gpioDirName(GPIO_DIR_NONE));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_master_off_refuses_everything);
    RUN_TEST(test_non_free_pins_refused_with_reason);
    RUN_TEST(test_claimed_pin_refused);
    RUN_TEST(test_transitions_and_masks);
    RUN_TEST(test_release_only_touches_held_pins);
    RUN_TEST(test_release_on_function_claim);
    RUN_TEST(test_master_off_releases_all);
    RUN_TEST(test_no_table_board_offers_nothing);
    RUN_TEST(test_snapshot_exactly_offered_pins);
    RUN_TEST(test_snapshot_heltec_v3);
    RUN_TEST(test_dir_names);
    return UNITY_END();
}
