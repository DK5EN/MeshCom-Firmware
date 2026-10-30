// UDP-05 (DK5EN/map#3): a message sent through EXTUDP went out on LoRa but
// never showed in the node's own WebUI chat, because sendMessage() echoed
// the outgoing frame into the phone ring only when hasMsgFromPhone was set
// (BLE app and WebUI sends).
//
// phoneEchoWanted() (src/backpressure.h) is the whole rule: echo for a
// phone/WebUI send and for ORIGIN_EXTUDP. ORIGIN_SERIAL never echoes here --
// sendMessage()'s bConsoleText branch already echoes a "::" console send, a
// second echo would show it twice. ORIGIN_GUI (T-Deck, own display path) and
// ORIGIN_NONE (relay, ACK, beacon) never echo. The gateway "server reached"
// ack frame is not part of this rule; it stays under hasMsgFromPhone at the
// call site.
//
//   pio test -e native -f test_phone_echo_origin

#include <unity.h>

#include <backpressure.h>

void setUp(void) {}
void tearDown(void) {}

// ---- a: the new origins -------------------------------------------------

static void test_extudp_send_is_echoed(void)
{
    TEST_ASSERT_TRUE(phoneEchoWanted(false, ORIGIN_EXTUDP));
}

// ---- b: unchanged behaviour ----------------------------------------------

// Already echoed via bConsoleText -- a TRUE here would be a double echo.
static void test_serial_send_is_not_echoed_twice(void)
{
    TEST_ASSERT_FALSE(phoneEchoWanted(false, ORIGIN_SERIAL));
}

// Relay, ACK and beacon paths never set an origin and never set
// hasMsgFromPhone -- they must never land in the chat as an own send.
static void test_untagged_send_is_not_echoed(void)
{
    TEST_ASSERT_FALSE(phoneEchoWanted(false, ORIGIN_NONE));
}

static void test_tdeck_gui_send_is_not_echoed(void)
{
    TEST_ASSERT_FALSE(phoneEchoWanted(false, ORIGIN_GUI));
}

// BLE app and WebUI set hasMsgFromPhone and their own origin: echoed once,
// exactly as before.
static void test_phone_and_web_sends_are_echoed(void)
{
    TEST_ASSERT_TRUE(phoneEchoWanted(true, ORIGIN_BLE));
    TEST_ASSERT_TRUE(phoneEchoWanted(true, ORIGIN_WEB));
}

// hasMsgFromPhone alone decides for a phone send, whatever the origin tag.
static void test_from_phone_always_echoed(void)
{
    TEST_ASSERT_TRUE(phoneEchoWanted(true, ORIGIN_NONE));
    TEST_ASSERT_TRUE(phoneEchoWanted(true, ORIGIN_GUI));
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_extudp_send_is_echoed);
    RUN_TEST(test_serial_send_is_not_echoed_twice);
    RUN_TEST(test_untagged_send_is_not_echoed);
    RUN_TEST(test_tdeck_gui_send_is_not_echoed);
    RUN_TEST(test_phone_and_web_sends_are_echoed);
    RUN_TEST(test_from_phone_always_echoed);

    return UNITY_END();
}
