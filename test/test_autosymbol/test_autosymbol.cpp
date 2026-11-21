// GPS-AUTOSYM-02/03: automatic symbol hysteresis (src/autosymbol.h) and its call order in setSMartBeaconing().
//   pio test -e native -f test_autosymbol
#include <unity.h>
#include <stdint.h>
#include <fstream>
#include <sstream>
#include <string>

#include <autosymbol.h>

void setUp(void) {}
void tearDown(void) {}

static void test_stationary_after_bike_burst_returns_to_runner(void)
{
    AutoSymbolState s = {};
    autoSymbolNote(&s, 3.0, 200000);
    TEST_ASSERT_EQUAL_CHAR('b', autoSymbolPick(&s, '/', '[', 200000));
    for (uint32_t t = 210000; t <= 330000; t += 10000) {
        autoSymbolNote(&s, 0.0, t);
        char want = (t <= 310000) ? 'b' : '[';
        TEST_ASSERT_EQUAL_CHAR_MESSAGE(want, autoSymbolPick(&s, '/', 'b', t), "stationary hold");
    }
}

static void test_note_thresholds(void)
{
    AutoSymbolState s = {};
    autoSymbolNote(&s, 1.4, 1000);
    TEST_ASSERT_EQUAL_CHAR('[', autoSymbolPick(&s, '/', '[', 1000));
    autoSymbolNote(&s, 1.41, 1000);
    TEST_ASSERT_EQUAL_CHAR('b', autoSymbolPick(&s, '/', '[', 1000));
    AutoSymbolState c = {};
    autoSymbolNote(&c, 7.0, 1000);
    TEST_ASSERT_EQUAL_CHAR('b', autoSymbolPick(&c, '/', '[', 1000));
    autoSymbolNote(&c, 7.01, 1000);
    TEST_ASSERT_EQUAL_CHAR('>', autoSymbolPick(&c, '/', '[', 1000));
}

static void test_not_applicable_symbols_untouched(void)
{
    AutoSymbolState s = {};
    autoSymbolNote(&s, 8.0, 1000);
    TEST_ASSERT_EQUAL_CHAR('[', autoSymbolPick(&s, '\\', '[', 1000));
    TEST_ASSERT_EQUAL_CHAR('k', autoSymbolPick(&s, '/', 'k', 1000));
    TEST_ASSERT_EQUAL_CHAR('-', autoSymbolPick(&s, '/', '-', 1000));
}

static void test_millis_rollover(void)
{
    AutoSymbolState s = {};
    autoSymbolNote(&s, 3.0, 0xFFFFFF00u);
    TEST_ASSERT_EQUAL_CHAR('b', autoSymbolPick(&s, '/', '[', 0x00000100u));
    TEST_ASSERT_EQUAL_CHAR('b', autoSymbolPick(&s, '/', '[', 0x0001D3BFu));
    TEST_ASSERT_EQUAL_CHAR('[', autoSymbolPick(&s, '/', 'b', 0x0001D3C0u));
}

static void test_boot_window_slow_walk_is_runner(void)
{
    AutoSymbolState s = {};
    autoSymbolNote(&s, 1.2, 30000);
    TEST_ASSERT_EQUAL_CHAR('[', autoSymbolPick(&s, '/', '[', 30000));
}

static void test_boot_window_bike_is_bike_not_car(void)
{
    AutoSymbolState s = {};
    autoSymbolNote(&s, 2.0, 30000);
    TEST_ASSERT_EQUAL_CHAR('b', autoSymbolPick(&s, '/', '[', 30000));
}

static void test_boot_window_car_is_car(void)
{
    AutoSymbolState s = {};
    autoSymbolNote(&s, 8.0, 30000);
    TEST_ASSERT_EQUAL_CHAR('>', autoSymbolPick(&s, '/', '[', 30000));
}

static void test_boot_window_nothing_seen_is_runner(void)
{
    AutoSymbolState s = {};
    TEST_ASSERT_EQUAL_CHAR('[', autoSymbolPick(&s, '/', '>', 5000));
}

static void test_hold_expires_from_the_sample_not_from_boot(void)
{
    AutoSymbolState s = {};
    autoSymbolNote(&s, 2.0, 30000);
    TEST_ASSERT_EQUAL_CHAR('b', autoSymbolPick(&s, '/', '[', 149999));
    TEST_ASSERT_EQUAL_CHAR('[', autoSymbolPick(&s, '/', 'b', 150000));
}

static std::string slurp(const char *rel)
{
    std::string file(__FILE__);
    const std::string suffix = "test/test_autosymbol/test_autosymbol.cpp";
    std::string root = file.substr(0, file.size() - suffix.size());
    std::ifstream f((root + rel).c_str(), std::ios::binary);
    TEST_ASSERT_TRUE_MESSAGE(f.good(), rel);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void test_setsmartbeaconing_calls_pick_before_both_returns(void)
{
    std::string s = slurp("src/loop_functions.cpp");
    size_t fn = s.find("unsigned int setSMartBeaconing(");
    TEST_ASSERT_TRUE(fn != std::string::npos);
    size_t pick = s.find("autoSymbolPick(", fn);
    size_t note = s.find("autoSymbolNote(", fn);
    size_t track = s.find("if(!bDisplayTrack)", fn);
    size_t still = s.find("if (speed_mps < 1.0)", fn);
    TEST_ASSERT_TRUE_MESSAGE(pick != std::string::npos, "autoSymbolPick( call missing in setSMartBeaconing");
    TEST_ASSERT_TRUE_MESSAGE(note != std::string::npos, "autoSymbolNote( call missing in setSMartBeaconing");
    TEST_ASSERT_TRUE_MESSAGE(track != std::string::npos && still != std::string::npos, "anchors");
    TEST_ASSERT_TRUE_MESSAGE(pick < track, "pick must precede the TRACK-off return");
    TEST_ASSERT_TRUE_MESSAGE(note < track, "note must precede the TRACK-off return");
    TEST_ASSERT_TRUE_MESSAGE(pick < still, "pick must precede the stationary return");
    TEST_ASSERT_TRUE_MESSAGE(s.find("static unsigned long last_car_speed_ts") == std::string::npos, "old statics still present");
    TEST_ASSERT_TRUE_MESSAGE(s.find("#include \"autosymbol.h\"") != std::string::npos, "include");
}

// --- own speed estimate (no valid GPS speed, e.g. RAK4631 feeds tinyGPSPlus, not gps) ---------

static void test_speed_first_sample_is_zero(void)
{
    AutoSymbolSpeed e = {};
    TEST_ASSERT_TRUE(autoSymbolSpeed(&e, false, 0.0, 48.0, 16.0, 5000) == 0.0);
}

static void test_speed_two_samples_100m_10s(void)
{
    AutoSymbolSpeed e = {};
    autoSymbolSpeed(&e, false, 0.0, 48.0, 16.0, 5000);
    // 100 m north: 100 / 111195 m per degree of latitude
    double v = autoSymbolSpeed(&e, false, 0.0, 48.0 + 100.0 / 111195.0, 16.0, 15000);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 10.0, v);
}

static void test_speed_reference_always_updates(void)
{
    // moves 100 m then stands still: the second step must see speed 0, not a stale reference
    AutoSymbolSpeed e = {};
    double lat2 = 48.0 + 100.0 / 111195.0;
    autoSymbolSpeed(&e, false, 0.0, 48.0, 16.0, 0);
    autoSymbolSpeed(&e, false, 0.0, lat2, 16.0, 10000);
    for (uint32_t t = 20000; t <= 200000; t += 10000)
        TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, autoSymbolSpeed(&e, false, 0.0, lat2, 16.0, t));
}

static void test_speed_far_reference_does_not_look_like_car(void)
{
    // TRACK-off scenario: 1 km away from some old point, parked: stays 0
    AutoSymbolSpeed e = {};
    for (uint32_t t = 0; t <= 100000; t += 10000)
        TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, autoSymbolSpeed(&e, false, 0.0, 48.01, 16.01, t));
}

static void test_speed_millis_wrap(void)
{
    AutoSymbolSpeed e = {};
    autoSymbolSpeed(&e, false, 0.0, 48.0, 16.0, 0xFFFFFFFFu - 4999u);   // 5 s before wrap
    double v = autoSymbolSpeed(&e, false, 0.0, 48.0 + 100.0 / 111195.0, 16.0, 5000u);   // 10 s later
    TEST_ASSERT_FLOAT_WITHIN(0.1, 10.0, v);
}

static void test_speed_elapsed_zero_keeps_previous(void)
{
    AutoSymbolSpeed e = {};
    autoSymbolSpeed(&e, false, 0.0, 48.0, 16.0, 1000);
    double v1 = autoSymbolSpeed(&e, false, 0.0, 48.0 + 100.0 / 111195.0, 16.0, 11000);
    double v2 = autoSymbolSpeed(&e, false, 0.0, 48.0 + 200.0 / 111195.0, 16.0, 11000);   // same millis
    TEST_ASSERT_TRUE(v1 == v2);
}

static void test_speed_valid_gps_wins_and_refreshes_reference(void)
{
    AutoSymbolSpeed e = {};
    TEST_ASSERT_TRUE(autoSymbolSpeed(&e, true, 3.5, 48.0, 16.0, 1000) == 3.5);
    // GPS speed lost: the next estimate uses the last GPS-valid position, 100 m / 10 s
    double v = autoSymbolSpeed(&e, false, 0.0, 48.0 + 100.0 / 111195.0, 16.0, 11000);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 10.0, v);
}

static void test_speed_feeds_symbol_with_track_off(void)
{
    // end to end: 3 m/s walk-bike pace without GPS speed switches the symbol to bike
    AutoSymbolState s = {};
    AutoSymbolSpeed e = {};
    double lat = 48.0;
    for (uint32_t t = 0; t <= 60000; t += 10000) {
        autoSymbolNote(&s, autoSymbolSpeed(&e, false, 0.0, lat, 16.0, t), t);
        lat += 30.0 / 111195.0;   // 30 m per 10 s
    }
    TEST_ASSERT_EQUAL_CHAR('b', autoSymbolPick(&s, '/', '[', 60000));
}

static void test_speed_parked_jitter_at_3s_rate_stays_runner(void)
{
    // RAK4631 calls every ~3 s with raw positions and no GPS speed: a parked fix that wanders
    // 5 m between samples must not read as bike (5 m / 3 s = 1.7 m/s > AUTOSYM_BIKE_MPS)
    AutoSymbolState s = {};
    AutoSymbolSpeed e = {};
    for (uint32_t t = 0, i = 0; t <= 300000; t += 3000, i++) {
        double lat = 48.0 + ((i & 1u) ? 5.0 / 111195.0 : 0.0);
        autoSymbolNote(&s, autoSymbolSpeed(&e, false, 0.0, lat, 16.0, t), t);
    }
    TEST_ASSERT_FALSE(s.bike_seen);
    TEST_ASSERT_EQUAL_CHAR('[', autoSymbolPick(&s, '/', '[', 300000));
}

static void test_speed_zero_zero_sample_is_skipped(void)
{
    // a sentence with satellites but no location gives (0,0); it must not become the reference
    AutoSymbolSpeed e = {};
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, autoSymbolSpeed(&e, false, 0.0, 0.0, 0.0, 0));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, autoSymbolSpeed(&e, false, 0.0, 48.0, 16.0, 10000));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, autoSymbolSpeed(&e, false, 0.0, 0.0, 0.0, 20000));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, autoSymbolSpeed(&e, false, 0.0, 48.0, 16.0, 30000));
}

static void test_setsmartbeaconing_track_path_speed_is_head_form(void)
{
    // The TRACK path keeps its speed computation exactly as at 0872bf96 (fallback = distance / interval).
    std::string s = slurp("src/loop_functions.cpp");
    size_t fn = s.find("unsigned int setSMartBeaconing(");
    TEST_ASSERT_TRUE(fn != std::string::npos);
    size_t track = s.find("if(!bDisplayTrack)", fn);
    TEST_ASSERT_TRUE(track != std::string::npos);
    const char *head =
        "    // Use GPS speed if available (more accurate than distance/interval)\n"
        "    double speed_mps = 0.0;\n"
        "\n"
        "    if(gps.speed.isValid())\n"
        "        speed_mps = gps.speed.mps();\n"
        "    else\n"
        "        speed_mps = distance / gps_refresh_intervall; // Fallback\n";
    size_t h = s.find(head, fn);
    TEST_ASSERT_TRUE_MESSAGE(h != std::string::npos, "TRACK-path speed lines differ from HEAD form");
    TEST_ASSERT_TRUE_MESSAGE(h > track, "TRACK-path speed must sit below the TRACK-off gate");
}

static void test_setsmartbeaconing_does_not_read_last_pos_before_track_gate(void)
{
    // posinfo_last_lat is only maintained by the TRACK path: reading it before the gate gives a stale reference.
    std::string s = slurp("src/loop_functions.cpp");
    size_t fn = s.find("unsigned int setSMartBeaconing(");
    size_t track = s.find("if(!bDisplayTrack)", fn);
    TEST_ASSERT_TRUE(fn != std::string::npos && track != std::string::npos);
    std::string before = s.substr(fn, track - fn);
    TEST_ASSERT_TRUE_MESSAGE(before.find("posinfo_last_lat") == std::string::npos,
                             "setSMartBeaconing must not read posinfo_last_lat before the TRACK-off gate");
    TEST_ASSERT_TRUE_MESSAGE(before.find("autoSymbolSpeed(") != std::string::npos,
                             "autosymbol must use its own speed estimate (autoSymbolSpeed)");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_stationary_after_bike_burst_returns_to_runner);
    RUN_TEST(test_note_thresholds);
    RUN_TEST(test_not_applicable_symbols_untouched);
    RUN_TEST(test_millis_rollover);
    RUN_TEST(test_boot_window_slow_walk_is_runner);
    RUN_TEST(test_boot_window_bike_is_bike_not_car);
    RUN_TEST(test_boot_window_car_is_car);
    RUN_TEST(test_boot_window_nothing_seen_is_runner);
    RUN_TEST(test_hold_expires_from_the_sample_not_from_boot);
    RUN_TEST(test_setsmartbeaconing_calls_pick_before_both_returns);
    RUN_TEST(test_speed_first_sample_is_zero);
    RUN_TEST(test_speed_two_samples_100m_10s);
    RUN_TEST(test_speed_reference_always_updates);
    RUN_TEST(test_speed_far_reference_does_not_look_like_car);
    RUN_TEST(test_speed_millis_wrap);
    RUN_TEST(test_speed_elapsed_zero_keeps_previous);
    RUN_TEST(test_speed_valid_gps_wins_and_refreshes_reference);
    RUN_TEST(test_speed_feeds_symbol_with_track_off);
    RUN_TEST(test_speed_parked_jitter_at_3s_rate_stays_runner);
    RUN_TEST(test_speed_zero_zero_sample_is_skipped);
    RUN_TEST(test_setsmartbeaconing_track_path_speed_is_head_form);
    RUN_TEST(test_setsmartbeaconing_does_not_read_last_pos_before_track_gate);
    return UNITY_END();
}
