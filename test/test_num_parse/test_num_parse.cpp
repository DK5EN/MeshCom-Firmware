// FORMAL-02 wave 7a: src/num_parse.h (mcParseInt, mcParseDec3, mcTrimSpan, mcFormatDec3), and the B5
// behaviour change against TODAY's parsers of the 9 call sites (reference models below).
#include <unity.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <map>
#include <vector>
#include <utility>
#include <command_setters.h> // cmdDecimalComma, cmdArgDouble, cmdInRange: the very code the sites run
#include <msgstore_api.h> // MSGSTORE_SLOTS_MAX (50): --storeslots 1..50, mailbox slots 0..49
#include <num_parse.h>

void setUp(void) {}
void tearDown(void) {}

static bool I(const char *s, int32_t lo, int32_t hi, int32_t *o) { return mcParseInt(s, strlen(s), lo, hi, o); }
static bool D(const char *s, int32_t lo, int32_t hi, int32_t *o) { return mcParseDec3(s, strlen(s), lo, hi, o); }

struct Case { const char *s; bool ok; int32_t v; };

static void test_int_table(void)
{
    static const Case t[] = {
        {"0", true, 0}, {"-0", true, 0}, {"5", true, 5}, {"-5", true, -5}, {"007", true, 7},
        {"49", true, 49}, {"50", true, 50}, {"51", false, 0}, {"-50", true, -50}, {"-51", false, 0},
        {"", false, 0}, {"-", false, 0}, {"+5", false, 0}, {" 5", false, 0}, {"5 ", false, 0},
        {"5x", false, 0}, {"x5", false, 0}, {"1.5", false, 0}, {"1,5", false, 0}, {"--5", false, 0},
        {"5-", false, 0}, {"abc", false, 0}, {"nan", false, 0}, {"0x10", false, 0},
    };
    for (const Case &c : t)
    {
        int32_t o = 12345;
        bool r = I(c.s, -50, 50, &o);
        TEST_ASSERT_EQUAL_MESSAGE(c.ok, r, c.s);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(c.ok ? c.v : 12345, o, c.s); // untouched on refusal
    }
    int32_t o = 7;
    TEST_ASSERT_FALSE(mcParseInt(0, 0, 0, 9, &o));
    TEST_ASSERT_FALSE(mcParseInt("5", 0, 0, 9, &o)); // n == 0
    TEST_ASSERT_FALSE(mcParseInt("5", 1, 9, 0, &o)); // lo > hi
    TEST_ASSERT_TRUE(mcParseInt("12x", 2, 0, 99, &o)); // only n bytes count
    TEST_ASSERT_EQUAL_INT32(12, o);
    TEST_ASSERT_FALSE(mcParseInt("1\0" "2", 3, 0, 99, &o)); // embedded NUL
}

static void test_int_edges(void)
{
    int32_t o = 0;
    TEST_ASSERT_TRUE(I("1", 1, 168, &o) && o == 1);
    TEST_ASSERT_TRUE(I("168", 1, 168, &o) && o == 168);
    TEST_ASSERT_FALSE(I("0", 1, 168, &o));
    TEST_ASSERT_FALSE(I("169", 1, 168, &o));
    TEST_ASSERT_TRUE(I("2147483647", INT32_MIN, INT32_MAX, &o) && o == INT32_MAX);
    TEST_ASSERT_TRUE(I("-2147483648", INT32_MIN, INT32_MAX, &o) && o == INT32_MIN);
    TEST_ASSERT_FALSE(I("2147483648", INT32_MIN, INT32_MAX, &o));
    TEST_ASSERT_FALSE(I("-2147483649", INT32_MIN, INT32_MAX, &o));
    TEST_ASSERT_FALSE(I("4294967296", INT32_MIN, INT32_MAX, &o));
    TEST_ASSERT_FALSE(I("99999999999999999999999999999999", INT32_MIN, INT32_MAX, &o));
    TEST_ASSERT_TRUE(I("00000000000000000000000000000000000000042", 0, 100, &o) && o == 42);
    TEST_ASSERT_TRUE(I("-00000000000000000000000000000000000000000", INT32_MIN, 0, &o) && o == 0);
    TEST_ASSERT_FALSE(I("-2147483648", -2147483647, 0, &o)); // lo edge: INT32_MIN below lo
    TEST_ASSERT_TRUE(I("2147483647", INT32_MAX, INT32_MAX, &o) && o == INT32_MAX);
    std::string longs(100000, '9'); // 100k digits: no UB, refused
    TEST_ASSERT_FALSE(mcParseInt(longs.c_str(), longs.size(), INT32_MIN, INT32_MAX, &o));
    std::string zeros(100000, '0');
    TEST_ASSERT_TRUE(mcParseInt(zeros.c_str(), zeros.size(), INT32_MIN, INT32_MAX, &o) && o == 0);
}

static void test_dec_table(void)
{
    static const Case t[] = {
        {"0", true, 0}, {"-0", true, 0}, {"-0,0", true, 0}, {"-0.000", true, 0}, {"5", true, 5000},
        {"1.25", true, 1250}, {"1,25", true, 1250}, {"-0,5", true, -500}, {"-0.5", true, -500},
        {"0.001", true, 1}, {"-0.001", true, -1}, {"1.50", true, 1500}, {"1.5", true, 1500},
        {"49.999", true, 49999}, {"50", true, 50000}, {"50.000", true, 50000}, {"50.001", false, 0},
        {"-50", true, -50000}, {"-50,000", true, -50000}, {"-50.001", false, 0},
        {"0.0004", false, 0}, {"1.2345", false, 0}, {"1.5abc", false, 0}, {"5x", false, 0},
        {"", false, 0}, {"-", false, 0}, {".", false, 0}, {",", false, 0}, {"5.", false, 0},
        {".5", false, 0}, {"-.5", false, 0}, {"1.2.3", false, 0}, {"1,2,3", false, 0},
        {"1.2,3", false, 0}, {"+1.5", false, 0}, {" 1.5", false, 0}, {"1.5 ", false, 0},
        {"nan", false, 0}, {"inf", false, 0}, {"1e3", false, 0}, {"1.5-", false, 0},
    };
    for (const Case &c : t)
    {
        int32_t o = 12345;
        bool r = D(c.s, -50000, 50000, &o);
        TEST_ASSERT_EQUAL_MESSAGE(c.ok, r, c.s);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(c.ok ? c.v : 12345, o, c.s);
    }
}

static void test_dec_edges(void)
{
    int32_t o = 0;
    // windcal 0.1 .. 100
    TEST_ASSERT_TRUE(D("0.1", 100, 100000, &o) && o == 100);
    TEST_ASSERT_FALSE(D("0.099", 100, 100000, &o));
    TEST_ASSERT_TRUE(D("100", 100, 100000, &o) && o == 100000);
    TEST_ASSERT_FALSE(D("100.001", 100, 100000, &o));
    TEST_ASSERT_FALSE(D("0", 100, 100000, &o));
    TEST_ASSERT_TRUE(D("2147483.647", INT32_MIN, INT32_MAX, &o) && o == INT32_MAX);
    TEST_ASSERT_FALSE(D("2147483.648", INT32_MIN, INT32_MAX, &o));
    TEST_ASSERT_TRUE(D("-2147483.648", INT32_MIN, INT32_MAX, &o) && o == INT32_MIN);
    TEST_ASSERT_FALSE(D("-2147483.649", INT32_MIN, INT32_MAX, &o));
    TEST_ASSERT_FALSE(D("2147484", INT32_MIN, INT32_MAX, &o));
    TEST_ASSERT_FALSE(D("99999999999999999999999999999", INT32_MIN, INT32_MAX, &o));
    TEST_ASSERT_FALSE(D("0.5", 2, 1, &o)); // lo > hi
    TEST_ASSERT_TRUE(D("000000000000000000000000001,5", 0, 5000, &o) && o == 1500);
    std::string longs(100000, '9');
    TEST_ASSERT_FALSE(mcParseDec3(longs.c_str(), longs.size(), INT32_MIN, INT32_MAX, &o));
    std::string zeros(100000, '0');
    TEST_ASSERT_TRUE(mcParseDec3(zeros.c_str(), zeros.size(), INT32_MIN, INT32_MAX, &o) && o == 0);
    TEST_ASSERT_FALSE(mcParseDec3(0, 0, 0, 9, &o));
    TEST_ASSERT_FALSE(mcParseDec3("1.5", 0, 0, 9000, &o));
    TEST_ASSERT_TRUE(mcParseDec3("1.59", 3, 0, 9000, &o) && o == 1500); // n cuts the input
}

// ---- reference models, written differently (validate by shape, then strtoll on copies) ----
static bool allDigits(const std::string &p) { if (p.empty()) return false; for (char c : p) if (c < '0' || c > '9') return false; return true; }

static bool refInt(const std::string &s, int32_t lo, int32_t hi, int32_t *o)
{
    std::string b = s;
    if (!b.empty() && b[0] == '-') b = b.substr(1);
    if (!allDigits(b)) return false;
    errno = 0;
    long long v = strtoll(s.c_str(), 0, 10); // s is "-?digits" here
    if (errno) return false;
    if (v < lo || v > hi) return false;
    *o = (int32_t)v;
    return true;
}

static bool refDec(const std::string &s, int32_t lo, int32_t hi, int32_t *o)
{
    bool neg = !s.empty() && s[0] == '-';
    std::string b = neg ? s.substr(1) : s;
    size_t sep = b.find_first_of(",.");
    std::string ip = b.substr(0, sep), fp = sep == std::string::npos ? "" : b.substr(sep + 1);
    if (sep != std::string::npos && b.find_first_of(",.", sep + 1) != std::string::npos) return false;
    if (!allDigits(ip)) return false;
    if (sep != std::string::npos && (!allDigits(fp) || fp.size() > 3)) return false;
    while (fp.size() < 3) fp += '0';
    long long v = strtoll(ip.c_str(), 0, 10) * 1000 + strtoll(fp.c_str(), 0, 10);
    if (neg) v = -v;
    if (v < lo || v > hi) return false;
    *o = (int32_t)v;
    return true;
}

static void test_exhaustive(void)
{
    static const char alpha[] = {'0', '1', '5', '9', '-', '+', '.', ',', ' ', 'x'};
    // the brief's alphabet is 0-9; the digits 0,1,5,9 stand for the digit class (each digit weighs the same
    // in the grammar), plus '2' and '7' to reach non-trivial values: 12 symbols
    static const char alpha2[] = {'0', '1', '2', '5', '7', '9', '-', '+', '.', ',', ' ', 'x'};
    (void)alpha;
    const size_t A = sizeof(alpha2);
    struct R { int32_t lo, hi; };
    static const R ri[] = {{-50, 50}, {0, 9}, {INT32_MIN, INT32_MAX}, {10, 700}};
    static const R rd[] = {{-50000, 50000}, {100, 100000}, {INT32_MIN, INT32_MAX}, {-1500, 2250}};
    unsigned long cases = 0, accI = 0, accD = 0;
    for (size_t len = 0; len <= 4; len++)
    {
        size_t total = 1;
        for (size_t k = 0; k < len; k++) total *= A;
        for (size_t idx = 0; idx < total; idx++)
        {
            std::string s;
            size_t x = idx;
            for (size_t k = 0; k < len; k++) { s += alpha2[x % A]; x /= A; }
            for (const R &r : ri)
            {
                int32_t a = -777, b = -888;
                bool ra = mcParseInt(s.data(), s.size(), r.lo, r.hi, &a);
                bool rb = refInt(s, r.lo, r.hi, &b);
                if (ra != rb || (ra && a != b)) TEST_FAIL_MESSAGE(("int mismatch: '" + s + "'").c_str());
                if (!ra && a != -777) TEST_FAIL_MESSAGE("int wrote on refusal");
                accI += ra; cases++;
            }
            for (const R &r : rd)
            {
                int32_t a = -777, b = -888;
                bool ra = mcParseDec3(s.data(), s.size(), r.lo, r.hi, &a);
                bool rb = refDec(s, r.lo, r.hi, &b);
                if (ra != rb || (ra && a != b)) TEST_FAIL_MESSAGE(("dec mismatch: '" + s + "'").c_str());
                if (!ra && a != -777) TEST_FAIL_MESSAGE("dec wrote on refusal");
                accD += ra; cases++;
            }
        }
    }
    printf("exhaustive: %lu comparisons, %lu int accepts, %lu dec accepts\n", cases, accI, accD);
    TEST_ASSERT_TRUE(accI > 1000 && accD > 1000);
}

// ---- mcTrimSpan ----
static std::string trimmed(const std::string &in)
{
    const char *p = in.data();
    size_t n = in.size();
    mcTrimSpan(&p, &n);
    return std::string(p, n);
}

static void test_trim(void)
{
    TEST_ASSERT_EQUAL_STRING("1.5", trimmed(" 1.5").c_str());
    TEST_ASSERT_EQUAL_STRING("1.5", trimmed("1.5 ").c_str());
    TEST_ASSERT_EQUAL_STRING("-2,5", trimmed("\t-2,5\t").c_str());
    TEST_ASSERT_EQUAL_STRING("a b", trimmed("  \t a b \t  ").c_str());
    TEST_ASSERT_EQUAL_STRING("", trimmed("").c_str());
    TEST_ASSERT_EQUAL_STRING("", trimmed(" \t \t").c_str());
    TEST_ASSERT_EQUAL_STRING("\n5", trimmed(" \n5").c_str()); // only space and tab
    TEST_ASSERT_EQUAL_STRING("5\r", trimmed("5\r ").c_str());
    const char *p = 0; size_t n = 3;
    mcTrimSpan(&p, &n); // null span untouched, no crash
    TEST_ASSERT_EQUAL_UINT(3, n);
    mcTrimSpan(0, 0);
    // after the trim the strict helpers accept what the form sends
    int32_t o = 0;
    std::string a = trimmed(" 1.5"), b = trimmed("1.5 "), c = trimmed("\t-2,5\t"), d = trimmed(" 42 ");
    TEST_ASSERT_TRUE(mcParseDec3(a.data(), a.size(), -50000, 50000, &o) && o == 1500);
    TEST_ASSERT_TRUE(mcParseDec3(b.data(), b.size(), -50000, 50000, &o) && o == 1500);
    TEST_ASSERT_TRUE(mcParseDec3(c.data(), c.size(), -50000, 50000, &o) && o == -2500);
    TEST_ASSERT_TRUE(mcParseInt(d.data(), d.size(), 0, 100, &o) && o == 42);
    TEST_ASSERT_FALSE(D(" 1.5", -50000, 50000, &o)); // untrimmed stays strict
    // exhaustive: trimming never grows and strips exactly the leading/trailing space/tab run
    static const char al[] = {' ', '\t', 'a', '\n'};
    for (size_t len = 0; len <= 6; len++)
    {
        size_t total = 1;
        for (size_t k = 0; k < len; k++) total *= sizeof(al);
        for (size_t idx = 0; idx < total; idx++)
        {
            std::string s; size_t x = idx;
            for (size_t k = 0; k < len; k++) { s += al[x % sizeof(al)]; x /= sizeof(al); }
            size_t lo = 0, hi = s.size();
            while (lo < hi && (s[lo] == ' ' || s[lo] == '\t')) lo++;
            while (hi > lo && (s[hi - 1] == ' ' || s[hi - 1] == '\t')) hi--;
            TEST_ASSERT_EQUAL_STRING_MESSAGE(s.substr(lo, hi - lo).c_str(), trimmed(s).c_str(), s.c_str());
        }
    }
}

// ---- mcFormatDec3 ----
static void test_format(void)
{
    char b[32];
    TEST_ASSERT_EQUAL_UINT(6, mcFormatDec3(-500, b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("-0.500", b); // the "%d.%03d" of (m/1000, m%1000) would give "0.500"
    TEST_ASSERT_EQUAL_UINT(5, mcFormatDec3(1250, b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("1.250", b);
    TEST_ASSERT_EQUAL_UINT(5, mcFormatDec3(0, b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("0.000", b);
    TEST_ASSERT_EQUAL_STRING("-0.001", (mcFormatDec3(-1, b, sizeof(b)), b));
    TEST_ASSERT_EQUAL_STRING("-50.000", (mcFormatDec3(-50000, b, sizeof(b)), b));
    TEST_ASSERT_EQUAL_STRING("100.000", (mcFormatDec3(100000, b, sizeof(b)), b));
    TEST_ASSERT_EQUAL_UINT(12, mcFormatDec3(INT32_MIN, b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("-2147483.648", b);
    TEST_ASSERT_EQUAL_UINT(11, mcFormatDec3(INT32_MAX, b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("2147483.647", b);
    // capacity: exactly len + 1 fits, one less does not (and the buffer holds an empty string)
    TEST_ASSERT_EQUAL_UINT(12, mcFormatDec3(INT32_MIN, b, 13));
    memset(b, 'x', sizeof(b));
    TEST_ASSERT_EQUAL_UINT(0, mcFormatDec3(INT32_MIN, b, 12));
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)b[0]);
    memset(b, 'x', sizeof(b));
    TEST_ASSERT_EQUAL_UINT(0, mcFormatDec3(-500, b, 6));
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)b[0]);
    TEST_ASSERT_EQUAL_UINT(6, mcFormatDec3(-500, b, 7));
    TEST_ASSERT_EQUAL_UINT(0, mcFormatDec3(5, b, 0));
    TEST_ASSERT_EQUAL_UINT(0, mcFormatDec3(5, 0, 10));
    // exhaustive -100000..100000 against snprintf of a double, and the round trip through mcParseDec3
    for (int32_t m = -100000; m <= 100000; m++)
    {
        char ref[32];
        snprintf(ref, sizeof(ref), "%.3f", (double)m / 1000.0);
        const size_t n = mcFormatDec3(m, b, sizeof(b));
        if (n != strlen(ref) || strcmp(b, ref) != 0) TEST_FAIL_MESSAGE(ref);
        int32_t back = 99;
        if (!mcParseDec3(b, n, INT32_MIN, INT32_MAX, &back) || back != m) TEST_FAIL_MESSAGE(ref);
    }
    // the extremes round trip too
    static const int32_t ex[] = {INT32_MIN, INT32_MIN + 1, INT32_MAX, INT32_MAX - 1, -999, 999, -1000, 1000};
    for (int32_t m : ex)
    {
        const size_t n = mcFormatDec3(m, b, sizeof(b));
        int32_t back = 99;
        TEST_ASSERT_TRUE(n > 0 && mcParseDec3(b, n, INT32_MIN, INT32_MAX, &back) && back == m);
    }
}

// ---- B5: today's parsers of the call sites (reference, String-free) vs the target (trim + helper) ----
// Sites (tempoffsetindoor/outdoor run the same code, mboxpurge/mboxdeliver too: one model each):
enum Site { S_WINDPIN, S_WINDCAL, S_STORETIME, S_STORESLOTS, S_TEMPOFF, S_MBOX, S_N };
static const char *const siteName[S_N] = {"windpin", "windcal", "storetime", "storeslots", "tempoff", "mbox"};

struct Res { bool ok; int32_t v; };

// Arduino String::toInt() = atol(); a long is 32 bit on the boards and saturates (newlib): clamp the 64-bit host value.
static int32_t atolSat(const std::string &s)
{
    long long v = strtoll(s.c_str(), 0, 10);
    if (v > INT32_MAX) v = INT32_MAX;
    if (v < INT32_MIN) v = INT32_MIN;
    return (int32_t)v;
}

static bool inR(int64_t v, int32_t lo, int32_t hi) { return v >= lo && v <= hi; }

static Res todayInt(Site site, const std::string &s)
{
    switch (site)
    {
    case S_WINDPIN: // web_setup.cpp:563, then the console range 0..48 (command_functions.cpp:3038)
    {
        int pin = 0; char tail = 0;
        if (sscanf(s.c_str(), "%d %c", &pin, &tail) != 1) return {false, 0};
        const int32_t v = atolSat(s); // device value (saturating); host sscanf %d would wrap
        return inR(v, 0, 48) ? Res{true, v} : Res{false, 0};
    }
    case S_STORETIME: { const int32_t v = atolSat(s); return inR(v, 1, 168) ? Res{true, v} : Res{false, 0}; }
    case S_STORESLOTS: { const int32_t v = atolSat(s); return inR(v, 1, 50) ? Res{true, v} : Res{false, 0}; }
    case S_MBOX: { const int32_t v = atolSat(s); return inR(v, 0, 49) ? Res{true, v} : Res{false, 0}; } // slot < s_slots <= 50
    default: break;
    }
    return {false, 0};
}

// windcal / tempoff: snprintf into char[16], cmdDecimalComma, sscanf("%f")==1, then the console's cmdStoreFloat
// (cmdArgDouble + cmdInRange). Value in thousandths.
static Res todayDec(Site site, const std::string &s)
{
    char value[16];
    snprintf(value, sizeof(value), "%s", s.c_str());
    cmdDecimalComma(value);
    float f = 0.0f;
    if (sscanf(value, "%f", &f) != 1) return {false, 0};
    bool ok = false;
    const double d = cmdArgDouble(value, &ok);
    if (!ok) return {false, 0};
    const double lo = (site == S_WINDCAL) ? 0.1 : -50.0, hi = (site == S_WINDCAL) ? 100.0 : 50.0;
    if (!cmdInRange(d, lo, hi)) return {false, 0};
    return {true, (int32_t)llround(d * 1000.0)};
}

static Res today(Site site, const std::string &s)
{
    return (site == S_WINDCAL || site == S_TEMPOFF) ? todayDec(site, s) : todayInt(site, s);
}

static Res target(Site site, const std::string &raw)
{
    const std::string t = trimmed(raw);
    int32_t o = 0;
    bool ok = false;
    switch (site)
    {
    case S_WINDPIN: ok = mcParseInt(t.data(), t.size(), 0, 48, &o); break;
    case S_WINDCAL: ok = mcParseDec3(t.data(), t.size(), 100, 100000, &o); break;
    case S_STORETIME: ok = mcParseInt(t.data(), t.size(), 1, 168, &o); break;
    case S_STORESLOTS: ok = mcParseInt(t.data(), t.size(), 1, MSGSTORE_SLOTS_MAX, &o); break;
    case S_TEMPOFF: ok = mcParseDec3(t.data(), t.size(), -50000, 50000, &o); break;
    case S_MBOX: ok = mcParseInt(t.data(), t.size(), 0, MSGSTORE_SLOTS_MAX - 1, &o); break;
    default: break;
    }
    return ok ? Res{true, o} : Res{false, 0};
}

// Why the target refuses `t` (the trimmed text): first violated rule, independent of the helper's code.
enum Why { W_EMPTY, W_PLUS, W_NOINT, W_NOFRAC, W_FRAC4, W_WS, W_GARBAGE, W_RANGE, W_TARGET_ONLY, W_VALUE, W_N };
static const char *const whyName[W_N] = {"empty", "plus", "noint", "nofrac", "frac4", "ws", "garbage", "range", "target-only", "value"};

static Why badChar(char c) { return isspace((unsigned char)c) ? W_WS : W_GARBAGE; }

static Why whyRefused(const std::string &t, bool dec)
{
    if (t.empty()) return W_EMPTY;
    if (t[0] == '+') return W_PLUS;
    size_t i = (t[0] == '-') ? 1 : 0;
    if (i >= t.size()) return W_EMPTY; // lone "-"
    if (!isdigit((unsigned char)t[i]))
        return (dec && (t[i] == ',' || t[i] == '.')) ? W_NOINT : badChar(t[i]);
    while (i < t.size() && isdigit((unsigned char)t[i])) i++;
    if (i == t.size()) return W_RANGE;
    if (!dec || (t[i] != ',' && t[i] != '.')) return badChar(t[i]);
    i++;
    size_t nf = 0;
    while (i < t.size() && isdigit((unsigned char)t[i]))
    {
        if (++nf > 3) return W_FRAC4;
        i++;
    }
    if (i < t.size()) return badChar(t[i]);
    return nf == 0 ? W_NOFRAC : W_RANGE;
}

// The kind of one difference, or -1 when today and the target agree (accept/refuse and value).
static int diffKind(Site site, const std::string &s)
{
    const Res a = today(site, s), b = target(site, s);
    if (a.ok == b.ok && (!a.ok || a.v == b.v)) return -1;
    if (!a.ok && b.ok) return W_TARGET_ONLY;
    if (a.ok && b.ok) return W_VALUE;
    return whyRefused(trimmed(s), site == S_WINDCAL || site == S_TEMPOFF);
}

// The pinned difference table. ID B5 for every row: (site, kind) -> count over the exhaustive set below
// (alphabet {0,1,5,-,+,.,",",space,tab,LF,a}, length 0..4: 16105 strings) plus the edge list, and the reason.
struct B5Row { Site site; Why why; unsigned count; const char *reason; };
static const B5Row b5Table[] = {
    {S_WINDPIN, W_PLUS, 118, "B5: a leading \"+\" read as a sign today, refused now (no GUI path sends it)"},
    {S_WINDPIN, W_WS, 447, "B5: LF/CR/FF/VT at an end or a blank inside (\"1 5\" read as 1 today): only space and tab are trimmed, the rest refuses"},
    {S_WINDCAL, W_PLUS, 396, "B5: a leading \"+\" read as a sign today, refused now (no GUI path sends it)"},
    {S_WINDCAL, W_NOINT, 645, "B5: \".5\" / \"-.5\" (no digit before the separator) read as a number today, refused now"},
    {S_WINDCAL, W_NOFRAC, 168, "B5: \"5.\" / \"5,\" (separator without fraction digit) read as a number today, refused now"},
    {S_WINDCAL, W_FRAC4, 1, "B5: 4+ fraction digits (\"1.2345\") read as a float today, refused now: no exact thousandths"},
    {S_WINDCAL, W_WS, 1881, "B5: LF/CR/FF/VT at an end or a blank inside (\"1 5\" read as 1 today): only space and tab are trimmed, the rest refuses"},
    {S_WINDCAL, W_GARBAGE, 1773, "B5: trailing text or a stray character (\"5x\", \"1.5.2\", \"1e1\", \"5a\") ends the scan today and the prefix is accepted, refused now"},
    {S_WINDCAL, W_TARGET_ONLY, 1, "B5: input over 15 characters was cut by the char[16] copy to a number the range refused; now parsed whole and accepted"},
    {S_STORETIME, W_PLUS, 344, "B5: a leading \"+\" read as a sign today, refused now (no GUI path sends it)"},
    {S_STORETIME, W_WS, 1661, "B5: LF/CR/FF/VT at an end or a blank inside (\"1 5\" read as 1 today): only space and tab are trimmed, the rest refuses"},
    {S_STORETIME, W_GARBAGE, 2267, "B5: trailing text or a stray character (\"5x\", \"1.5.2\", \"1e1\", \"5a\") ends the scan today and the prefix is accepted, refused now"},
    {S_STORESLOTS, W_PLUS, 311, "B5: a leading \"+\" read as a sign today, refused now (no GUI path sends it)"},
    {S_STORESLOTS, W_WS, 1545, "B5: LF/CR/FF/VT at an end or a blank inside (\"1 5\" read as 1 today): only space and tab are trimmed, the rest refuses"},
    {S_STORESLOTS, W_GARBAGE, 2072, "B5: trailing text or a stray character (\"5x\", \"1.5.2\", \"1e1\", \"5a\") ends the scan today and the prefix is accepted, refused now"},
    {S_TEMPOFF, W_PLUS, 526, "B5: a leading \"+\" read as a sign today, refused now (no GUI path sends it)"},
    {S_TEMPOFF, W_NOINT, 1052, "B5: \".5\" / \"-.5\" (no digit before the separator) read as a number today, refused now"},
    {S_TEMPOFF, W_NOFRAC, 232, "B5: \"5.\" / \"5,\" (separator without fraction digit) read as a number today, refused now"},
    {S_TEMPOFF, W_FRAC4, 2, "B5: 4+ fraction digits (\"1.2345\") read as a float today, refused now: no exact thousandths"},
    {S_TEMPOFF, W_WS, 2714, "B5: LF/CR/FF/VT at an end or a blank inside (\"1 5\" read as 1 today): only space and tab are trimmed, the rest refuses"},
    {S_TEMPOFF, W_GARBAGE, 2525, "B5: trailing text or a stray character (\"5x\", \"1.5.2\", \"1e1\", \"5a\") ends the scan today and the prefix is accepted, refused now"},
    {S_TEMPOFF, W_VALUE, 1, "B5: input over 15 characters was cut by the char[16] copy and stored as 0; now the whole text is parsed (1.5)"},
    {S_MBOX, W_EMPTY, 82, "B5: empty, blank or lone \"-\" gave atol() = 0 = slot 0, so the web could purge or deliver slot 0 by accident; refused now"},
    {S_MBOX, W_PLUS, 1733, "B5: a leading \"+\" read as a sign today, refused now (no GUI path sends it)"},
    {S_MBOX, W_WS, 3714, "B5: LF/CR/FF/VT at an end or a blank inside (\"1 5\" read as 1 today): only space and tab are trimmed, the rest refuses"},
    {S_MBOX, W_GARBAGE, 9125, "B5: trailing text or a stray character (\"5x\", \"1.5.2\", \"1e1\", \"5a\") ends the scan today and the prefix is accepted, refused now"},
};

struct EdgeRow { const char *in; unsigned mask; const char *reason; }; // mask: bit per Site that differs
static const EdgeRow edgeTable[] = {
    {"+5", 0x3f, "B5: + sign accepted by all six parsers today"},
    {"5.", 0x3e, "B5: trailing separator; sscanf %f / atol read 5 (windpin already refused: %c takes the dot)"},
    {"1.2345", 0x3e, "B5: 4 fraction digits; windcal/tempoff read the float, atol sites read 1"},
    {"5x", 0x3e, "B5: trailing text; all but windpin read 5 (windpin: %c takes the x)"},
    {"", 0x20, "B5: empty reads as slot 0 for mbox (atol); every other site refused already"},
    {"  ", 0x20, "B5: blank reads as slot 0 for mbox; all else refused already"},
    {"\n5", 0x3f, "B5: LF is not trimmed (space and tab only); scanf/atol skip it today"},
    {"5\n", 0x3f, "B5: LF is not trimmed; scanf/atol skip it today"},
    {"\t5\t", 0x00, "agrees: space and tab are trimmed before the helper"},
    {" 5", 0x00, "agrees: trimmed"},
    {"5 ", 0x00, "agrees: trimmed"},
    {"nan", 0x20, "B5: mbox only (atol = 0 = slot 0); %f nan fails the range test today"},
    {"inf", 0x20, "B5: mbox only (atol = 0 = slot 0); inf fails the range test today"},
    {"-inf", 0x20, "B5: mbox only (atol = 0 = slot 0)"},
    {"1e3", 0x2c, "B5: atol sites read 1; windcal/tempoff read 1000 and refuse by range, as now"},
    {"1e1", 0x3e, "B5: exponent form accepted as 10 by the float sites, as 1 by the atol sites"},
    {"1E1", 0x3e, "B5: as 1e1"},
    {".5", 0x32, "B5: no digit before the separator: windcal/tempoff accept 0.5; atol reads 0 (mbox slot 0)"},
    {"-.5", 0x30, "B5: tempoff accepts -0.5; mbox atol = -0 = slot 0"},
    {"5,", 0x3e, "B5: trailing separator, comma form"},
    {"1 5", 0x3e, "B5: inner blank; scanf/atol stop at it and read 1 (windpin refuses: %c sees 5)"},
    {"0", 0x00, "agrees"},
    {"-0", 0x00, "agrees"},
    {"99999999999", 0x00, "agrees: refused (saturates to INT32_MAX on the boards, out of every range)"},
    {"-99999999999", 0x00, "agrees: refused (saturates to INT32_MIN)"},
    {"000000000000000000000000001,5", 0x3e, "B5: 29 characters cut to 15 by the char[16] copy (= 0 today); parsed whole now"},
    {"0.00000000000000001", 0x30, "B5: cut to 15 characters = 0.0000000000000, accepted by tempoff today; 4+ fraction digits refuse now"},
    {"1.5", 0x2c, "B5: atol sites read 1 (decimal into an integer field); windcal/tempoff agree"},
    {"1,5", 0x2c, "B5: as 1.5"},
    {"-0,5", 0x20, "B5: mbox atol = -0 = slot 0; the other sites agree"},
    {"5.000", 0x2c, "B5: atol sites read 5 (decimal into an integer field)"},
    {"a", 0x20, "B5: a non-number reads as slot 0 for mbox"},
    {"abc", 0x20, "B5: as a"},
    {"--5", 0x20, "B5: mbox atol = 0 = slot 0"},
    {"1.2.3", 0x3e, "B5: stray second separator; scanf/atol read 1.2 / 1"},
};

static std::vector<std::string> exhaustiveSet(void)
{
    static const char al[] = {'0', '1', '5', '-', '+', '.', ',', ' ', '\t', '\n', 'a'};
    std::vector<std::string> v;
    for (size_t len = 0; len <= 4; len++)
    {
        size_t total = 1;
        for (size_t k = 0; k < len; k++) total *= sizeof(al);
        for (size_t idx = 0; idx < total; idx++)
        {
            std::string s; size_t x = idx;
            for (size_t k = 0; k < len; k++) { s += al[x % sizeof(al)]; x /= sizeof(al); }
            v.push_back(s);
        }
    }
    return v;
}

static void test_b5(void)
{
    std::vector<std::string> all = exhaustiveSet();
    for (const EdgeRow &e : edgeTable) all.push_back(e.in);
    std::map<std::pair<int, int>, unsigned> got;
    for (const std::string &s : all)
        for (int site = 0; site < S_N; site++)
        {
            const int k = diffKind((Site)site, s);
            if (k >= 0) got[{site, k}]++;
        }
    for (const auto &kv : got)
        printf("B5 count %-10s %-11s %u\n", siteName[kv.first.first], whyName[kv.first.second], kv.second);
    fflush(stdout);
    // exact: every counted (site, kind) is a table row with that count, every row is hit
    TEST_ASSERT_EQUAL_UINT(sizeof(b5Table) / sizeof(b5Table[0]), got.size());
    for (const B5Row &r : b5Table)
    {
        auto it = got.find({(int)r.site, (int)r.why});
        const std::string msg = std::string(siteName[r.site]) + "/" + whyName[r.why];
        TEST_ASSERT_TRUE_MESSAGE(it != got.end(), msg.c_str());
        TEST_ASSERT_EQUAL_UINT_MESSAGE(r.count, it->second, msg.c_str());
        TEST_ASSERT_TRUE(r.reason && r.reason[0]);
    }
}

static void test_b5_edges(void)
{
    unsigned bad = 0;
    for (const EdgeRow &e : edgeTable)
    {
        unsigned mask = 0;
        for (int site = 0; site < S_N; site++)
            if (diffKind((Site)site, e.in) >= 0) mask |= 1u << site;
        if (mask != e.mask)
        {
            printf("edge mismatch |%s| expected %02x got %02x\n", e.in, e.mask, mask); // all of them, then fail
            bad++;
        }
        TEST_ASSERT_TRUE(e.reason && e.reason[0]);
    }
    fflush(stdout);
    TEST_ASSERT_EQUAL_UINT(0, bad);
}

// gpio<N>: web_setup.cpp:764 atoi on the name tail; webGuardIsGpioParam admits only 1+ digits and num.length() <= 3,
// so the site only ever sees 1..3 digit strings: today and target must agree on every one of them (no B5 row).
static void test_gpio_no_difference(void)
{
    unsigned n = 0;
    for (size_t len = 1; len <= 3; len++)
    {
        size_t total = 1;
        for (size_t k = 0; k < len; k++) total *= 10;
        for (size_t idx = 0; idx < total; idx++)
        {
            std::string s(len, '0'); size_t x = idx;
            for (size_t k = len; k > 0; k--) { s[k - 1] = (char)('0' + x % 10); x /= 10; }
            int32_t o = -1;
            const int a = atoi(s.c_str());
            TEST_ASSERT_TRUE_MESSAGE(mcParseInt(s.data(), s.size(), 0, 999, &o), s.c_str());
            TEST_ASSERT_EQUAL_INT_MESSAGE(a, o, s.c_str());
            n++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(1110, n);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_int_table);
    RUN_TEST(test_int_edges);
    RUN_TEST(test_dec_table);
    RUN_TEST(test_dec_edges);
    RUN_TEST(test_exhaustive);
    RUN_TEST(test_trim);
    RUN_TEST(test_format);
    RUN_TEST(test_b5);
    RUN_TEST(test_b5_edges);
    RUN_TEST(test_gpio_no_difference);
    return UNITY_END();
}
