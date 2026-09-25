// V4 scratch: exact MH JSON lengths with the firmware's ArduinoJson 7.4.3
#include <ArduinoJson.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static float mheardRoundDist(double dist) {  // copy of src/mheard_record.h
    if (dist < 0.0) return -1.0f;
    char tmp[32]; snprintf(tmp, sizeof(tmp), "%.1lf", dist); return (float)atof(tmp);
}

struct V {
    const char *call, *date, *time; uint8_t plt, hw, mod; int16_t rssi; int8_t snr; double dist;
    uint8_t pl, mesh; int ncnt;
};
struct N { int age; int8_t hm; const char *role; int ex, nb, gw, via; };

static size_t frame(const V &v, const N *n, bool pos, char *out = nullptr) {
    JsonDocument d;
    d["TYP"] = "MH"; d["CALL"] = v.call; d["DATE"] = v.date; d["TIME"] = v.time;
    d["PLT"] = v.plt; d["HW"] = v.hw; d["MOD"] = v.mod; d["RSSI"] = v.rssi; d["SNR"] = v.snr;
    d["DIST"] = v.dist; d["PL"] = v.pl; d["MESH"] = v.mesh; d["NCNT"] = v.ncnt;
    if (n) { d["AGE"] = n->age; d["HM"] = n->hm; d["ROLE"] = n->role; d["EX"] = n->ex; d["NB"] = n->nb; d["GW"] = n->gw; d["VIA"] = n->via; }
    if (pos) { d["LAT"] = -48.4231; d["LON"] = -123.7869; d["ALT"] = 12345; }
    char buf[400]; size_t l = serializeJson(d, buf, sizeof(buf));
    if (out) strcpy(out, buf);
    return l + 1; // + 0x44
}

static void dist1(const char *lbl, double x) {
    JsonDocument d; d["DIST"] = x; char b[64]; serializeJson(d, b, sizeof(b));
    printf("  %-34s %-26s len(DIST value)=%zu\n", lbl, b, strlen(b) - 9);
}

int main() {
    printf("ArduinoJson %s, sizeof(JsonFloat)=%zu\n", ARDUINOJSON_VERSION, sizeof(ArduinoJson::JsonFloat));
    printf("DIST serialisation (value as the firmware stores it):\n");
    dist1("generator literal 1234.5", 1234.5);
    dist1("live: 1234.567891234 (double)", 1234.567891234);
    dist1("live: 12.3456789123", 12.3456789123);
    dist1("live: 0.123456789123", 0.123456789123);
    dist1("live: 57.29 km/1000 style 57.2918", 57291.8/1000.0);
    dist1("live: 9876.54321987", 9876.54321987);
    dist1("live: 20015.08634", 20015.0863412);
    dist1("live tiny: 1.23456789123e-6", 1.23456789123e-6);
    dist1("live not computed: -1", -1.0);
    dist1("list: mheardRoundDist(1234.56)", (double)mheardRoundDist(1234.56));
    dist1("list: mheardRoundDist(1.34)", (double)mheardRoundDist(1.34));
    dist1("list: mheardRoundDist(9.95)", (double)mheardRoundDist(9.94));
    dist1("list: mheardRoundDist(12345.64)", (double)mheardRoundDist(12345.64));
    dist1("list: mheardRoundDist(20015.08)", (double)mheardRoundDist(20015.08));
    // exhaustive check of the list path: every 0.1 km step 0..20100 km
    size_t mx = 0; double at = 0; int bad = 0;
    for (long i = 0; i <= 201000; i++) {
        double x = i / 10.0 + 0.0123;
        JsonDocument d; d["DIST"] = (double)mheardRoundDist(x); char b[64]; serializeJson(d, b, sizeof(b));
        size_t l = strlen(b) - 9; if (l > mx) { mx = l; at = x; }
        char ref[32]; snprintf(ref, sizeof ref, "%.1lf", x); size_t rl = strlen(ref); if (ref[rl-1]=='0') rl -= 2; // ".0" stripped
        if (l != rl) bad++;
    }
    printf("  list path sweep 0..20100 km in 0.1 steps: max DIST len %zu (at %.4f), %d values not printed as %%.1f\n", mx, at, bad);
    // live path sweep: random-ish doubles
    size_t lmx = 0; double lat_ = 0;
    for (long i = 1; i <= 2000000; i++) {
        double x = i * 0.0100003917 + 1e-9 * (i % 997);
        JsonDocument d; d["DIST"] = x; char b[64]; serializeJson(d, b, sizeof(b));
        size_t l = strlen(b) - 9; if (l > lmx) { lmx = l; lat_ = x; }
    }
    printf("  live path sweep 0.01..20000 km unrounded double: max DIST len %zu (at %.9f)\n", lmx, lat_);

    char out[400];
    V gen = {"OE7XWT-12", "2026-09-24", "08:27:33", 64, 127, 255, -160, -20, 1234.5, 15, 1, 127};
    N gnew = {720, -20, "S", 127, 127, 1, 1};
    printf("\nGenerator values through ArduinoJson: base %zu, +7 %zu, +7+pos %zu (paper: 162/225/268)\n",
           frame(gen, nullptr, false), frame(gen, &gnew, false), frame(gen, &gnew, true));

    // realistic worst: live path, unrounded double DIST, else generator values
    V live = gen; live.dist = 1234.567891234;
    printf("Realistic live (DIST unrounded double): base %zu, +7 %zu\n", frame(live, nullptr, false), frame(live, &gnew, false, out));
    printf("   %s\n", out);
    V lst = gen; lst.dist = (double)mheardRoundDist(12345.64);
    printf("Realistic list (DIST float-rounded, 5 int digits): base %zu, +7 %zu\n", frame(lst, nullptr, false), frame(lst, &gnew, false));

    // CALL 20 chars matching checkRegexCall ^[0-9A-Z]?[A-Z]?[0-9]+[A-Z][A-Z]?[A-Z]?[%-]?[0-9]?[0-9]?$
    V c20 = live; c20.call = "0A123456789012ABC-99";
    printf("Live, CALL 20 chars (regex-valid): base %zu, +7 %zu\n", frame(c20, nullptr, false), frame(c20, &gnew, false));

    // type-maximal per C type of the firmware fields (payload type limited to : ! @ by decodeAPRS; HW masked 0x7F; MESH bool)
    V tm = {"0A123456789012ABC-99", "2026-09-24", "08:27:33", 64, 127, 255, -32768, -128, 1.23456789123e-6, 255, 1, 255};
    N tn = {65535, -128, "S", 128, 128, 1, 1};
    printf("Type-max (live types, NCNT uint8, DIST tiny double): base %zu, +7 %zu\n", frame(tm, nullptr, false), frame(tm, &tn, false, out));
    printf("   %s\n", out);
    V tm2 = tm; tm2.call = "OE7XWT-12"; tm2.dist = 1234.567891234;
    N tn2 = {720, -128, "S", 128, 128, 1, 1};
    printf("Type-max numerics, CALL 9, DIST realistic double, AGE<=720: base %zu, +7 %zu\n", frame(tm2, nullptr, false), frame(tm2, &tn2, false));
    V tm3 = tm2; tm3.ncnt = -2147483647 - 1;
    printf("  same with list-path NCNT as int (INT_MIN): base %zu, +7 %zu\n", frame(tm3, nullptr, false), frame(tm3, &tn2, false));
        // proposed frame, field ranges from the paper's 4.3 encodings (CALL<=9, RSSI -160..95, PL 4 bit, NCNT 8 bit)
    V p = {"OE7XWT-12", "2026-09-24", "08:27:33", 64, 127, 255, -160, -128, 1234.567891234, 15, 1, 255};
    N pn = {720, -128, "S", 128, 128, 1, 1};
    printf("Proposed, encoding-max (SNR/HM -128, DIST unrounded double, AGE<=720): +7 %zu\n", frame(p, &pn, false));
    N pn2 = pn; pn2.age = 65535; V p2 = p; p2.dist = 1.23456789123e-6;
    printf("Proposed, + AGE 65535 + DIST exponent form: +7 %zu\n", frame(p2, &pn2, false));
    V p3 = p; p3.dist = (double)mheardRoundDist(12345.64); N pn3 = pn;
    printf("Proposed, DIST rounded to 0.1 km as float (5 int digits): +7 %zu\n", frame(p3, &pn3, false));
    // today live, realistic + regex-valid 20-char CALL + PL 3 digits (source path up to 120 chars)
    V t = live; t.call = "0A123456789012ABC-99"; t.pl = 110;
    printf("Today live, CALL 20 + PL 110: base %zu\n", frame(t, nullptr, false));
    return 0;
}
