// FORMAL-02 B8: pure helpers of src/spectral_sweep.h.
//
//   pio test -e native_spectral_sweep

#include <unity.h>

#include <string>

#include <spectral_sweep.h>

#include "../support/repo_file.h"

void setUp(void) {}
void tearDown(void) {}

// The loop of the old page (web_functions.cpp sub_page_spectrum), verbatim float arithmetic.
static int oldPageStepCount(float start, float end, float step)
{
    int n = 0;
    float cur = start;
    while (cur < end)
    {
        n++;
        cur += step;
    }
    return n;
}

// The old shading formula: res[i] is uint16_t, spec_samples is int -> integer division.
static float oldAlpha(uint16_t res, int samples)
{
    float alpha = (res / samples);
    if (alpha < 0.2)
        alpha = 0.2;
    return alpha;
}

static void test_range_433(void)
{
    SpecSweepRange r = specSweepRangeFor(433.175f);
    TEST_ASSERT_EQUAL_FLOAT(432.0f, r.start);
    TEST_ASSERT_EQUAL_FLOAT(434.0f, r.end);
    TEST_ASSERT_EQUAL_FLOAT(0.025f, r.step);
    TEST_ASSERT_EQUAL_INT(2048, r.samples);
}

static void test_range_869(void)
{
    SpecSweepRange r = specSweepRangeFor(869.525f);
    TEST_ASSERT_EQUAL_FLOAT(863.0f, r.start);
    TEST_ASSERT_EQUAL_FLOAT(870.0f, r.end);
    TEST_ASSERT_EQUAL_FLOAT(0.100f, r.step);
    TEST_ASSERT_EQUAL_INT(2048, r.samples);
}

static void test_step_counts_match_old_loop(void)
{
    SpecSweepRange a = specSweepRangeFor(433.175f);
    SpecSweepRange b = specSweepRangeFor(869.525f);
    TEST_ASSERT_EQUAL_INT(oldPageStepCount(a.start, a.end, a.step), specSweepStepCount(a));
    TEST_ASSERT_EQUAL_INT(oldPageStepCount(b.start, b.end, b.step), specSweepStepCount(b));
    TEST_ASSERT_EQUAL_INT(81, specSweepStepCount(a));
    TEST_ASSERT_EQUAL_INT(71, specSweepStepCount(b));
    // The cap in specSweepStepCount() makes a check of its own result vacuous: the UNCAPPED count of the
    // old loop must fit the result store, or the cap would silently cut the sweep.
    TEST_ASSERT_TRUE(oldPageStepCount(a.start, a.end, a.step) <= SPS_MAX_STEPS);
    TEST_ASSERT_TRUE(oldPageStepCount(b.start, b.end, b.step) <= SPS_MAX_STEPS);
}

static void test_level_table(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, specSweepLevel(0, 2048));
    TEST_ASSERT_EQUAL_UINT8(1, specSweepLevel(1, 2048));      // tiny but non-zero stays visible
    TEST_ASSERT_EQUAL_UINT8(8, specSweepLevel(1024, 2048));   // 7.5 rounds to 8
    TEST_ASSERT_EQUAL_UINT8(15, specSweepLevel(2048, 2048));
    TEST_ASSERT_EQUAL_UINT8(15, specSweepLevel(60000, 2048)); // clamp
    TEST_ASSERT_EQUAL_UINT8(0, specSweepLevel(5, 0));
}

static void test_alpha_table(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.2f, specSweepAlpha(1));   // 1/15 < 0.2
    TEST_ASSERT_EQUAL_FLOAT(0.2f, specSweepAlpha(3));   // 0.2 exactly
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.533f, specSweepAlpha(8));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, specSweepAlpha(15));
}

// Shading regression: half the samples in a bin must not shade like a near-empty one.
static void test_shading_regression(void)
{
    float fixedA = specSweepAlpha(specSweepLevel(1024, 2048));
    TEST_ASSERT_FLOAT_WITHIN(0.04f, 0.5f, fixedA);
    TEST_ASSERT_TRUE(fixedA > 0.4f);
}

static void test_old_formula_was_flat(void)
{
    // Documents the defect: the old formula returns 0.2 for 1024/2048 (and for 2047/2048).
    TEST_ASSERT_EQUAL_FLOAT(0.2f, oldAlpha(1024, 2048));
    TEST_ASSERT_EQUAL_FLOAT(0.2f, oldAlpha(2047, 2048));
}

static void test_nibble_roundtrip(void)
{
    uint8_t row[SPS_BYTES_PER_STEP];
    memset(row, 0, sizeof row);
    for (uint8_t i = 0; i < SPS_BINS; i++)
        specSweepSetNibble(row, i, (uint8_t)(i % 16));
    for (uint8_t i = 0; i < SPS_BINS; i++)
        TEST_ASSERT_EQUAL_UINT8(i % 16, specSweepGetNibble(row, i));
    specSweepSetNibble(row, 4, 0);   // overwrite does not disturb the neighbour
    TEST_ASSERT_EQUAL_UINT8(0, specSweepGetNibble(row, 4));
    TEST_ASSERT_EQUAL_UINT8(5, specSweepGetNibble(row, 5));
    TEST_ASSERT_EQUAL_UINT8(3, specSweepGetNibble(row, 3));
}

static void sink(void *ctx, const char *t)
{
    static_cast<std::string *>(ctx)->append(t);
}

static void test_render_three_steps(void)
{
    static SpecSweepResult r;
    memset(&r, 0, sizeof r);
    r.start = 432.0f;
    r.end = 432.075f;
    r.step = 0.025f;
    r.samples = 2048;
    r.steps = 3;
    r.own_freq = 432.025f;
    r.own_bw_khz = 125.0f;
    specSweepSetNibble(r.packed + 0 * SPS_BYTES_PER_STEP, 0, 15);
    specSweepSetNibble(r.packed + 1 * SPS_BYTES_PER_STEP, 2, 8);
    specSweepSetNibble(r.packed + 2 * SPS_BYTES_PER_STEP, 32, 1);

    std::string out;
    specSweepRenderText(sink, &out, r);

    // num_fsteps = 3, end_x = 60 + 4*10 = 100, end_y = 10 + 330 = 340, marker w = round(0.125/0.025*10) = 50,
    // centre = 60 + 9.99999 truncated to uint16 = 69 (float noise of the old expression, kept byte-identical), start = 69 - 25 = 44.
    const std::string want =
        "<svg viewbox=\"0, 0, 140, 400\" id=\"spectrum_display\" style=\"font-size:var(--fs-s);\">"
        "<line x1=\"60\" y1=\"340\" x2=\"100\" y2=\"340\" style=\"stroke:black;stroke-width:1\"/>\n"
        "<line x1=\"60\" y1=\"10\" x2=\"60\" y2=\"340\" style=\"stroke:black;stroke-width:1\"/>\n"
        "<text x=\"60\" y=\"380\" style=\"color: black;\">Freq [MHz]</text>\n"
        "<text x=\"0\" y=\"0\" f style=\"color: black;\" transform=\"translate(10, 340) rotate(-90)\")>RSSI [dBm]</text>\n"
        "<rect width=\"50\" height=\"330\" x=\"44\" y=\"10\" fill=\"rgba(0, 110, 129, 0.2)\" />"
        "<line x1=\"69\" y1=\"10\" x2=\"69\" y2=\"340\" style=\"stroke:rgba(114, 0, 129, 0.2); stroke-width:1\"/>\n";
    TEST_ASSERT_EQUAL_STRING(want.c_str(), out.substr(0, want.size()).c_str());

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("<text x=\"20\" y=\"16\" style=\"color: black;\">-11</text>\n"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("<rect width=\"10\" height=\"10\" x=\"60\" y=\"10\" fill=\"rgba(0,0,0,1.000)\"/>"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("<rect width=\"10\" height=\"10\" x=\"70\" y=\"30\" fill=\"rgba(0,0,0,0.533)\"/>"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("<rect width=\"10\" height=\"10\" x=\"80\" y=\"330\" fill=\"rgba(0,0,0,0.200)\"/>"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("<line x1=\"60\" y1=\"340\" x2=\"60\" y2=\"350\" style=\"stroke:grey; stroke-width:1\"/>\n"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("<text x=\"50\" y=\"365\" style=\"color: black;\">432.000</text>\n"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("</svg>\r\n<p>The Node scans the frequency range (432.000MHz - 432.075MHz) in small steps using a given scan bandwith (0.025MHz).</p>\n<p>It performs 2048 measurements each step and puts them into 33 power-bins."));
}

// ---- source pins: the hardware path has no native harness (RadioLib, SPI) ----------------------------------

// Body of the function whose definition line starts with `sig`: from there to the first "\n}\n".
static std::string fnBody(const std::string &src, const char *sig)
{
    const size_t at = src.find(sig);
    TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, sig);
    const size_t end = src.find("\n}\n", at);
    TEST_ASSERT_TRUE_MESSAGE(end != std::string::npos, sig);
    return src.substr(at, end - at);
}

static bool has(const std::string &s, const char *needle)
{
    return s.find(needle) != std::string::npos;
}

static int countOf(const std::string &s, const char *needle)
{
    int n = 0;
    for (size_t at = s.find(needle); at != std::string::npos; at = s.find(needle, at + 1))
        n++;
    return n;
}

static void test_pin_setup_uses_chip_helpers(void)
{
    const std::string src = repo_slurp("src/esp32/esp32_main.cpp");
    const std::string setup = fnBody(src, "void esp32setup()\n{");
    TEST_ASSERT_TRUE(has(setup, "loraChipBegin()"));
    TEST_ASSERT_TRUE(has(setup, "loraApplyChipExtras();"));
    // the chip settings live in the helpers only; a copy in setup would drift from the post-scan restore
    TEST_ASSERT_FALSE(has(setup, "radio.begin("));
    TEST_ASSERT_FALSE(has(setup, "setCurrentLimit"));
    TEST_ASSERT_FALSE(has(setup, "setRxBoostedGainMode"));
    TEST_ASSERT_FALSE(has(setup, "setTCXO"));
    const std::string extras = fnBody(src, "int loraApplyChipExtras(void)");
    TEST_ASSERT_TRUE(has(extras, "setTCXO(1.8)"));
    TEST_ASSERT_TRUE(has(extras, "setRxBoostedGainMode(bBOOSTEDGAIN)"));
    TEST_ASSERT_TRUE(has(extras, "setCurrentLimit(CURRENT_LIMIT)"));
    TEST_ASSERT_TRUE(has(fnBody(src, "int loraChipBegin(void)"), "setDio2AsRfSwitch(true)"));
}

static void test_pin_finish_scan_restores_setup_state(void)
{
    const std::string scan = repo_slurp("src/spectral_scan.cpp");
    const std::string fin = fnBody(scan, "int sx126x_spectral_finish_scan()");
    TEST_ASSERT_TRUE(has(fin, "loraChipBegin()"));
    TEST_ASSERT_TRUE(has(fin, "loraApplyChipExtras()"));
    TEST_ASSERT_TRUE(has(fin, "lora_setchip_meshcom()"));
    TEST_ASSERT_FALSE(has(fin, "radio.begin("));
    TEST_ASSERT_TRUE(has(fin, "[LoRa] spectrum re-init failed rc=%d step=%s"));
    TEST_ASSERT_TRUE(has(fin, "return rc;"));
    // advisor rework 2: setup only logs an extras error and still applies the node's channel and LoRa parameters;
    // after a scan the same must happen, so setchip runs right after the extras, not gated on their result
    TEST_ASSERT_TRUE_MESSAGE(has(fin, "const int extras = loraApplyChipExtras();\n    const bool setchip_ok = lora_setchip_meshcom();"),
                             "lora_setchip_meshcom() must run after a successful begin whatever the extras returned");
    // the legacy console scan has no caller for the status and says so
    TEST_ASSERT_TRUE(has(scan, "(void)sx126x_spectral_finish_scan();"));
}

static void test_pin_rearm_checks_start_receive(void)
{
    const std::string src = repo_slurp("src/esp32/esp32_main.cpp");
    const std::string rearm = fnBody(src, "bool loraRxRearmAfterScan(void)");
    TEST_ASSERT_TRUE(has(rearm, "const int state = radio.startReceive("));
    TEST_ASSERT_TRUE(has(rearm, "state != RADIOLIB_ERR_NONE"));
    TEST_ASSERT_TRUE(has(rearm, "[LoRa] spectrum re-init failed rc=%d step=rx"));
    TEST_ASSERT_TRUE(has(rearm, "return false;"));
}

static void test_pin_disarm_refuses_midair_and_no_radio(void)
{
    const std::string src = repo_slurp("src/esp32/esp32_main.cpp");
    const std::string dis = fnBody(src, "bool loraRxDisarmForScan(void)");
    TEST_ASSERT_TRUE(has(dis, "!bRadio"));
    TEST_ASSERT_TRUE(has(dis, "loraPacketMidAir()"));
    const std::string mid = fnBody(src, "static bool loraPacketMidAir(void)");
    TEST_ASSERT_TRUE(has(mid, "loraIrqPacketActive("));
    TEST_ASSERT_TRUE(has(mid, "loraRssiIsSignal("));
    // one definition of the mid-air IRQ mask: the RX-timeout path, the TX gate and the disarm share it
    TEST_ASSERT_EQUAL_INT(1, countOf(src, "RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED"));
    TEST_ASSERT_EQUAL_INT(1, countOf(src, "-126.0f"));
    TEST_ASSERT_TRUE(countOf(src, "loraIrqPacketActive(") >= 4);
    TEST_ASSERT_TRUE(countOf(src, "loraRssiIsSignal(") >= 3);
}

static void test_pin_sweep_reports_failed_restore(void)
{
    const std::string sw = repo_slurp("src/spectral_sweep.cpp");
    TEST_ASSERT_TRUE(has(sw, "SPS_REINIT"));
    TEST_ASSERT_TRUE(has(sw, "SPS_NORADIO"));
    const std::string restore = fnBody(sw, "static bool spectralSweepRestore(void)");
    TEST_ASSERT_TRUE(has(restore, "sx126x_spectral_finish_scan()"));
    TEST_ASSERT_TRUE(has(restore, "loraRxRearmAfterScan()"));
    // no call of finish/rearm outside the shared restore except the nomem path (chip untouched there)
    TEST_ASSERT_EQUAL_INT(1, countOf(sw, "sx126x_spectral_finish_scan()"));
    TEST_ASSERT_TRUE(has(sw, "return restored ? rc : (int8_t)SPS_REINIT;"));
    const std::string web = repo_slurp("src/web_functions/web_functions.cpp");
    TEST_ASSERT_TRUE(has(web, "The radio could not be restored after the scan (see the serial log). Reboot the node."));
    TEST_ASSERT_TRUE(has(web, "<p>No radio.</p>"));
    TEST_ASSERT_TRUE(has(web, ".then(function(){if(cpage=='spectrum')loadPage('spectrum',csender,false);})"));
    TEST_ASSERT_TRUE(has(repo_slurp("src/web_functions/Web-API_documentation.txt"), "/callfunction/?spectrumscan"));
}

// SPECTRUM-TDECKPLUS-01: a status read right after SetSpectrScanParams stalls the patched scan for good
// (Heltec V3 and T-Deck Plus, 2026-11-28: every step timed out). The poll must wait before every read.
static void test_pin_scan_waits_before_first_status_read(void)
{
    const std::string scan = repo_slurp("src/spectral_scan.cpp");
    const std::string fn = fnBody(scan, "uint16_t *sx126x_spectral_scan_freq(float freq, uint16_t samples)");
    const size_t start = fn.find("radio.spectralScanStart(samples)");
    const size_t wait = fn.find("delay(SPECTRAL_POLL_MS);");
    const size_t read = fn.find("radio.spectralScanGetStatus()");
    TEST_ASSERT_TRUE(start != std::string::npos && wait != std::string::npos && read != std::string::npos);
    TEST_ASSERT_TRUE_MESSAGE(start < wait && wait < read, "wait before the first status read, never read right after the start");
    TEST_ASSERT_EQUAL_INT(1, countOf(fn, "radio.spectralScanGetStatus()"));   // the only read sits behind the wait
    TEST_ASSERT_TRUE(has(fn, "> SPECTRAL_STEP_TIMEOUT_MS"));                 // still bounded
    TEST_ASSERT_TRUE(has(scan, "#define SPECTRAL_POLL_MS 10UL"));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_range_433);
    RUN_TEST(test_range_869);
    RUN_TEST(test_step_counts_match_old_loop);
    RUN_TEST(test_level_table);
    RUN_TEST(test_alpha_table);
    RUN_TEST(test_shading_regression);
    RUN_TEST(test_old_formula_was_flat);
    RUN_TEST(test_nibble_roundtrip);
    RUN_TEST(test_render_three_steps);
    RUN_TEST(test_pin_setup_uses_chip_helpers);
    RUN_TEST(test_pin_finish_scan_restores_setup_state);
    RUN_TEST(test_pin_rearm_checks_start_receive);
    RUN_TEST(test_pin_disarm_refuses_midair_and_no_radio);
    RUN_TEST(test_pin_sweep_reports_failed_restore);
    RUN_TEST(test_pin_scan_waits_before_first_status_read);
    return UNITY_END();
}
