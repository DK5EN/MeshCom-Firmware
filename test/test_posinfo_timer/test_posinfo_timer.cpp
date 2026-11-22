// POSINFO-LAG-01: position beacon must not wait an extra 30 s (src/posinfo_timer.h).
// D6: automatic beacons keep POSINFO_AUTO_MIN_MS from the last position TX of any kind.
//   pio test -e native -f test_posinfo_timer
//
// Before-runs (the Sim must go red against the old behaviour):
//   PLATFORMIO_BUILD_FLAGS="-DSIM_OLD_LAG=1"     idle passes refresh posinfo_timer_min (HEAD before POSINFO-LAG-01)
//   PLATFORMIO_BUILD_FLAGS="-DSIM_OLD_SPACING=1" 30 s spacing from posinfo_timer_min only
// Source scan of the mains: POSINFO_SCAN_ROOT=<dir with src/esp32/esp32_main.cpp, src/nrf52/nrf52_main.cpp>
// points it at a copy (default: the real tree).
#include <unity.h>
#include <stdint.h>
#include <stdlib.h>
#include <fstream>
#include <sstream>
#include <string>

#include <beacon_rate.h>
#include <posinfo_timer.h>

void setUp(void) {}
void tearDown(void) {}

#ifdef SIM_OLD_SPACING
static const uint32_t kMinMs = 30000;
#else
static const uint32_t kMinMs = POSINFO_AUTO_MIN_MS;
#endif

struct Sim
{
    uint32_t timer = 0;       // posinfo_timer
    uint32_t timer_min = 0;   // posinfo_timer_min
    bool shot = false;
    bool pos_first = false;   // bPosFirst
    bool have_own = false;    // haveOwnPosTx()
    uint32_t own_tx = 0;      // lastOwnPosTxMs()
    int sends = 0;
    uint32_t last_send = 0;
    uint32_t min_gap = 0xFFFFFFFFu;
    uint32_t first_send = 0;

    // one main-loop pass, mirrors the posinfo block of both mains
    void pass(uint32_t now, uint32_t interval_s)
    {
        bool first_window = (now > 100000 && now < 130000 && pos_first);
        bool due = posinfoDue(now, timer, interval_s, first_window, shot);
#ifdef SIM_OLD_SPACING
        bool ok = posinfoSpacingOk(now, timer_min, kMinMs);
#else
        bool ok = posinfoAutoSpacingOk(now, timer_min, have_own, own_tx, kMinMs);
#endif
        if (due && ok)
        {
            if (sends > 0 && (uint32_t)(now - last_send) < min_gap)
                min_gap = (uint32_t)(now - last_send);
            if (sends == 0)
                first_send = now;
            sends++;
            last_send = now;
            pos_first = false;
            shot = false;
            timer = now;
            timer_min = now;
            have_own = true;      // sendPosition() stamps lastOwnPosTx
            own_tx = now;
        }
#ifdef SIM_OLD_LAG
        else if (!due)
        {
            timer_min = now;      // the removed else branch
        }
#endif
    }

    // instant beacon (--sendpos, button, EXTUDP): stamps lastOwnPosTx, never posinfo_timer_min
    void instant(uint32_t now)
    {
        have_own = true;
        own_tx = now;
    }
};

// run passes every 100 ms from t0 until t1
static void run(Sim &s, uint32_t t0, uint32_t t1, uint32_t interval_s)
{
    for (uint32_t t = t0; (uint32_t)(t - t0) <= (uint32_t)(t1 - t0); t += 100)
        s.pass(t, interval_s);
}

static void test_periodic_fires_within_1s_of_due(void)
{
    Sim s;
    s.timer = 200000; s.timer_min = 200000;
    uint32_t due = 200000 + 120 * 1000u;
    run(s, 200000, due + 70000, 120);
    TEST_ASSERT_TRUE(s.sends >= 1);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(due + 1000, s.first_send);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(due, s.first_send);
}

static void test_shot_fires_promptly(void)
{
    Sim s;
    s.timer = 200000; s.timer_min = 200000;
    run(s, 200000, 270000, 900);          // idle 70 s, nothing due
    TEST_ASSERT_EQUAL_INT(0, s.sends);
    s.shot = true;
    run(s, 270100, 280000, 900);
    TEST_ASSERT_EQUAL_INT(1, s.sends);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(270100 + 1000, s.first_send);
}

static void test_min_spacing_60s_holds(void)
{
    Sim s;
    s.timer = 200000; s.timer_min = 200000;
    run(s, 200000, 260000, 900);
    s.shot = true;
    s.pass(260000, 900);                   // send 1 (60 s after timer_min)
    TEST_ASSERT_EQUAL_INT(1, s.sends);
    s.shot = true;                         // new request right after
    run(s, 260100, 319900, 900);
    TEST_ASSERT_EQUAL_INT(1, s.sends);     // held back for < 60 s
    run(s, 320000, 321000, 900);
    TEST_ASSERT_EQUAL_INT(2, s.sends);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(60000, s.min_gap);
}

static void test_millis_rollover(void)
{
    Sim s;
    uint32_t t0 = 0xFFFFFFFFu - 50000u;
    s.timer = t0; s.timer_min = t0;
    uint32_t due1 = t0 + 120 * 1000u;      // wraps
    run(s, t0, t0 + 260000u, 120);         // runs past two intervals across the wrap
    TEST_ASSERT_EQUAL_INT(2, s.sends);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(1000u, (uint32_t)(s.first_send - due1));
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(120000u, s.min_gap);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(121000u, s.min_gap);
}

// 100-130 s after boot: first-fix window. Before POSINFO-LAG-01 the 30 s spacing opened at 130000,
// exactly when the window closed, so it never fired; now it fires ~100 s.
static void test_first_window_fires_at_100s(void)
{
    Sim s;
    s.pos_first = true;                    // timer = timer_min = 0 (boot), interval 30 min: not due
    run(s, 0, 140000, 1800);
    TEST_ASSERT_EQUAL_INT(1, s.sends);
    TEST_ASSERT_GREATER_THAN_UINT32(100000, s.first_send);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(101000, s.first_send);
}

// D6: --sendpos 1 s before the periodic beacon is due -> the periodic waits until 60 s after it
static void test_auto_waits_for_instant_beacon(void)
{
    Sim s;
    s.timer = 200000; s.timer_min = 200000;
    uint32_t due = 200000 + 120 * 1000u;   // 320000
    run(s, 200000, due - 1000, 120);
    s.instant(due - 1000);
    run(s, due - 900, due + 100000, 120);
    TEST_ASSERT_EQUAL_INT(1, s.sends);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(due - 1000 + 60000, s.first_send);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(due - 1000 + 61000, s.first_send);
}

// D6: track mode with a 10 s interval: 60 s floor
static void test_track_mode_60s_floor(void)
{
    Sim s;
    run(s, 0, 300000, 10);
    TEST_ASSERT_EQUAL_INT(5, s.sends);     // 60, 120, 180, 240, 300 s
    TEST_ASSERT_EQUAL_UINT32(60000, s.min_gap);
}

// stationary 1800 s interval is unaffected by the floor, an old instant beacon does not delay it
static void test_stationary_1800s_unaffected(void)
{
    Sim s;
    s.timer = 200000; s.timer_min = 200000;
    uint32_t due = 200000 + 1800 * 1000u;
    run(s, 200000, 1000000, 1800);
    s.instant(1000000);
    run(s, 1000100, due + 1000, 1800);
    TEST_ASSERT_EQUAL_INT(1, s.sends);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(due, s.first_send);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(due + 1000, s.first_send);
}

static void test_auto_spacing_helper(void)
{
    const uint32_t m = 60000;
    // nothing yet from either stamp
    TEST_ASSERT_TRUE(posinfoAutoSpacingOk(100000, 0, false, 0, m));
    // last automatic send 59.9 s ago
    TEST_ASSERT_FALSE(posinfoAutoSpacingOk(100000, 40100, false, 0, m));
    // instant beacon later than the automatic one wins
    TEST_ASSERT_FALSE(posinfoAutoSpacingOk(100000, 0, true, 60000, m));
    TEST_ASSERT_TRUE(posinfoAutoSpacingOk(100000, 0, true, 40000, m));
    // instant beacon older than the automatic one does not matter
    TEST_ASSERT_FALSE(posinfoAutoSpacingOk(100000, 90000, true, 10000, m));
    // rollover: own TX 30 s before the wrap, now 10 s after it (40 s elapsed), timer_min long ago
    TEST_ASSERT_FALSE(posinfoAutoSpacingOk(10000, 0xFFFFFFFFu - 100000u, true, 0xFFFFFFFFu - 30000u, m));
    // rollover: own TX 70 s before now across the wrap
    TEST_ASSERT_TRUE(posinfoAutoSpacingOk(10000, 0xFFFFFFFFu - 100000u, true, 0xFFFFFFFFu - 60000u, m));
}

// ---- source scan: both mains must use the helpers and never refresh posinfo_timer_min on idle passes ----

static std::string slurpNoWs(const char *rel)
{
    std::string root;
    const char *env = getenv("POSINFO_SCAN_ROOT");
    if (env && *env)
    {
        root = std::string(env) + "/";
    }
    else
    {
        std::string file(__FILE__);
        const std::string suffix = "test/test_posinfo_timer/test_posinfo_timer.cpp";
        root = file.substr(0, file.size() - suffix.size());
    }
    std::ifstream f((root + rel).c_str(), std::ios::binary);
    TEST_ASSERT_TRUE_MESSAGE(f.good(), rel);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string raw = ss.str(), out;
    for (char c : raw)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            out += c;
    return out;
}

static void test_mains_stamp_timer_min_only_on_send(void)
{
    const char *files[] = {"src/esp32/esp32_main.cpp", "src/nrf52/nrf52_main.cpp"};
    for (const char *rel : files)
    {
        std::string s = slurpNoWs(rel);
        TEST_ASSERT_TRUE_MESSAGE(s.find("posinfoDue(") != std::string::npos, rel);
        size_t sp = s.find("posinfoAutoSpacingOk(");
        TEST_ASSERT_TRUE_MESSAGE(sp != std::string::npos, rel);
        const std::string stamp = "posinfo_timer_min=millis()";
        size_t first = s.find(stamp);
        TEST_ASSERT_TRUE_MESSAGE(first != std::string::npos, rel);
        TEST_ASSERT_TRUE_MESSAGE(first > sp, rel);                              // inside the send branch
        TEST_ASSERT_TRUE_MESSAGE(s.find(stamp, first + 1) == std::string::npos, rel);   // exactly once
        TEST_ASSERT_TRUE_MESSAGE(s.find("ncnt_hold") == std::string::npos, rel);
    }
}

// D6: EXTUDP telemetry is automatic traffic -- its immediate position beacon must pass the same 60 s gate
// and fall back to posinfo_shot (deferred, not lost) while the gate is shut.
static void test_extudp_tele_beacon_gated(void)
{
    std::string s = slurpNoWs("src/extudp_functions.cpp");
    size_t fn = s.find("voidhandleExternTelemetry(");
    TEST_ASSERT_TRUE_MESSAGE(fn != std::string::npos, "handleExternTelemetry");
    size_t end = s.find("voidgetExtern(", fn);
    TEST_ASSERT_TRUE_MESSAGE(end != std::string::npos, "getExtern after handleExternTelemetry");
    std::string body = s.substr(fn, end - fn);
    size_t gate = body.find("posinfoAutoSpacingOk(");
    size_t tx = body.find("sendPosition(0x9999");
    TEST_ASSERT_TRUE_MESSAGE(gate != std::string::npos, "EXTUDP tele: no POSINFO_AUTO_MIN_MS gate");
    TEST_ASSERT_TRUE_MESSAGE(tx != std::string::npos && gate < tx, "EXTUDP tele: gate must precede the TX");
    TEST_ASSERT_TRUE_MESSAGE(body.find("posinfo_shot=true") != std::string::npos, "EXTUDP tele: deferred shot");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_periodic_fires_within_1s_of_due);
    RUN_TEST(test_shot_fires_promptly);
    RUN_TEST(test_min_spacing_60s_holds);
    RUN_TEST(test_millis_rollover);
    RUN_TEST(test_first_window_fires_at_100s);
    RUN_TEST(test_auto_waits_for_instant_beacon);
    RUN_TEST(test_track_mode_60s_floor);
    RUN_TEST(test_stationary_1800s_unaffected);
    RUN_TEST(test_auto_spacing_helper);
    RUN_TEST(test_mains_stamp_timer_min_only_on_send);
    RUN_TEST(test_extudp_tele_beacon_gated);
    return UNITY_END();
}
