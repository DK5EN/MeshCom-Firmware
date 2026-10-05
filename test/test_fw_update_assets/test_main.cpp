// AU W1 (#1187): src/fw_update_assets.h -- env to release-asset name, and the streaming
// scanner over the GitHub releases/latest JSON. Fixtures are real API responses fetched
// 2026-10-05 (dk5en-latest.json = DK5EN/MeshCom-Firmware v4.40a.10.02, icssw-latest.json =
// icssw-org v4.40a) plus a synthetic fork release with .bin.zz assets inserted
// (dk5en-zz-synthetic.json; the T-Beam-1W.bin.zz asset has its "name" key LAST).
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "fw_update_assets.h"

// __FILE__ is relative under PlatformIO's native runner, so walk up from the working
// directory until the test directory and platformio.ini sit side by side.
static bool file_exists(const std::string &p)
{
    std::ifstream f(p.c_str());
    return f.good();
}

static std::string read_fixture(const char *name)
{
    const std::string rel = std::string("test/test_fw_update_assets/fixtures/") + name;
    char cwd_buf[4096];
    TEST_ASSERT_NOT_NULL_MESSAGE(getcwd(cwd_buf, sizeof(cwd_buf)), "getcwd() failed");
    std::string dir(cwd_buf);
    for (int hops = 0; hops < 16; hops++)
    {
        if (file_exists(dir + "/" + rel) && file_exists(dir + "/platformio.ini"))
            break;
        const size_t pos = dir.find_last_of('/');
        if (pos == std::string::npos || pos == 0)
            TEST_FAIL_MESSAGE("could not derive repo root");
        dir = dir.substr(0, pos);
    }
    std::ifstream f((dir + "/" + rel).c_str(), std::ios::binary);
    TEST_ASSERT_TRUE_MESSAGE(f.good(), "could not open fixture");
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// chunk 0 = the whole document in one call.
static bool scan(const std::string &json, const char *want, size_t chunk, FwReleaseInfo &out)
{
    FwJsonScan s;
    fwScanInit(s, want);
    if (chunk == 0)
        fwScanFeed(s, json.data(), json.size());
    else
        for (size_t i = 0; i < json.size(); i += chunk)
            fwScanFeed(s, json.data() + i, (json.size() - i < chunk) ? json.size() - i : chunk);
    return fwScanResult(s, out);
}

static const size_t kChunks[] = {1, 7, 64, 4096, 0};

static std::string sha_hex(const uint8_t *d)
{
    char b[65];
    for (int i = 0; i < 32; i++)
        snprintf(b + 2 * i, 3, "%02x", d[i]);
    return std::string(b, 64);
}

// Independent expectation: plain text search in the fixture (asset objects there carry
// name before size, digest and browser_download_url).
struct Expect
{
    bool ok;
    uint32_t size;
    std::string digest; // 64 hex
    std::string url;
};

static std::string value_after(const std::string &j, size_t from, const char *key, bool quoted)
{
    const std::string k = std::string("\"") + key + "\": " + (quoted ? "\"" : "");
    const size_t p = j.find(k, from);
    if (p == std::string::npos)
        return "";
    const size_t b = p + k.size();
    const size_t e = j.find(quoted ? '"' : ',', b);
    return j.substr(b, e - b);
}

static Expect expect_for(const std::string &j, const std::string &name)
{
    Expect e = {false, 0, "", ""};
    const size_t p = j.find("\"name\": \"" + name + "\"");
    if (p == std::string::npos)
        return e;
    e.size = (uint32_t)strtoul(value_after(j, p, "size", false).c_str(), nullptr, 10);
    const std::string dg = value_after(j, p, "digest", true);
    e.digest = dg.substr(7);
    e.url = value_after(j, p, "browser_download_url", true);
    e.ok = true;
    return e;
}

// ---------------------------------------------------------------------------------------

static void test_asset_name_plain_and_renames()
{
    char b[64];
    TEST_ASSERT_TRUE(fwAssetName("heltec_wifi_lora_32_V3", b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("heltec_wifi_lora_32_V3.bin.zz", b);
    TEST_ASSERT_TRUE(fwAssetName("ttgo_tbeam_SX1262", b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("ttgo_tbeam_SX1262.bin.zz", b);
    TEST_ASSERT_TRUE(fwAssetName("E22_1262_S3-DevKitC-1-N16R8", b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("E22_1262_S3-DevKitC-1-N16R8.bin.zz", b);
    TEST_ASSERT_TRUE(fwAssetName("t_deck_plus", b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("t_deck_plus.bin.zz", b);

    TEST_ASSERT_TRUE(fwAssetName("LilyGo_T-Beam-1W", b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("T-Beam-1W.bin.zz", b);
    TEST_ASSERT_TRUE(fwAssetName("LilyGo_T3_S3_V1_3", b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("T3_S3_V13.bin.zz", b);
    TEST_ASSERT_TRUE(fwAssetName("LilyGo_T_Connect_Pro", b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("t_connect_pro.bin.zz", b);
    // The renamed targets themselves are not renamed again.
    TEST_ASSERT_TRUE(fwAssetName("T-Beam-1W", b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("T-Beam-1W.bin.zz", b);
}

static void test_asset_name_bounds()
{
    char b[64];
    TEST_ASSERT_FALSE(fwAssetName(nullptr, b, sizeof(b)));
    TEST_ASSERT_FALSE(fwAssetName("", b, sizeof(b)));
    TEST_ASSERT_FALSE(fwAssetName("t_deck", nullptr, 10));
    TEST_ASSERT_FALSE(fwAssetName("t_deck", b, 0));
    // "t_deck.bin.zz" is 13 chars + NUL = 14 bytes
    char small[14];
    TEST_ASSERT_TRUE(fwAssetName("t_deck", small, sizeof(small)));
    TEST_ASSERT_EQUAL_STRING("t_deck.bin.zz", small);
    char tiny[13];
    memset(tiny, 'x', sizeof(tiny));
    TEST_ASSERT_FALSE(fwAssetName("t_deck", tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_CHAR('\0', tiny[0]);
}

static void test_state_is_bounded()
{
    TEST_ASSERT_TRUE(sizeof(FwJsonScan) < 1024);
}

static void test_dk5en_plain_bin_all_chunk_sizes()
{
    const std::string j = read_fixture("dk5en-latest.json");
    TEST_ASSERT_TRUE(j.size() > 60000);
    for (size_t c : kChunks)
    {
        FwReleaseInfo r;
        TEST_ASSERT_TRUE(scan(j, "heltec_wifi_lora_32_V3.bin", c, r));
        TEST_ASSERT_EQUAL_STRING("v4.40a.10.02", r.tag);
        TEST_ASSERT_FALSE(r.draft);
        TEST_ASSERT_FALSE(r.prerelease);
        TEST_ASSERT_TRUE(r.found);
        TEST_ASSERT_EQUAL_UINT32(1537856u, r.size);
        TEST_ASSERT_TRUE(r.haveDigest);
        TEST_ASSERT_EQUAL_STRING("6246b26d3a7b9fbddde110571767d81ca0c0b0903364abe2f03d600b8b24992c",
                                 sha_hex(r.sha256).c_str());
        TEST_ASSERT_EQUAL_STRING(
            "https://github.com/DK5EN/MeshCom-Firmware/releases/download/v4.40a.10.02/"
            "heltec_wifi_lora_32_V3.bin",
            r.url);
    }
}

static void check_every_asset(const char *file, const char *tag, size_t chunk)
{
    const std::string j = read_fixture(file);
    // collect the asset names with a plain search (release "name" is the tag, skipped)
    std::vector<std::string> names;
    size_t p = 0;
    while ((p = j.find("\"name\": \"", p)) != std::string::npos)
    {
        p += 9;
        const std::string n = j.substr(p, j.find('"', p) - p);
        if (n.find('.') != std::string::npos && n.compare(0, 3, "v4.") != 0)
            names.push_back(n);
    }
    TEST_ASSERT_TRUE(names.size() >= 38);
    for (const std::string &n : names)
    {
        const Expect e = expect_for(j, n);
        TEST_ASSERT_TRUE_MESSAGE(e.ok, n.c_str());
        FwReleaseInfo r;
        TEST_ASSERT_TRUE_MESSAGE(scan(j, n.c_str(), chunk, r), n.c_str());
        TEST_ASSERT_EQUAL_STRING(tag, r.tag);
        TEST_ASSERT_TRUE_MESSAGE(r.found, n.c_str());
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(e.size, r.size, n.c_str());
        TEST_ASSERT_TRUE_MESSAGE(r.haveDigest, n.c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE(e.digest.c_str(), sha_hex(r.sha256).c_str(), n.c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE(e.url.c_str(), r.url, n.c_str());
    }
}

static void test_dk5en_every_asset_matches_independent_search()
{
    check_every_asset("dk5en-latest.json", "v4.40a.10.02", 64);
    check_every_asset("dk5en-latest.json", "v4.40a.10.02", 1);
}

static void test_icssw_every_asset_matches_independent_search()
{
    check_every_asset("icssw-latest.json", "v4.40a", 4096);
    check_every_asset("icssw-latest.json", "v4.40a", 7);
}

static void test_icssw_renamed_assets()
{
    const std::string j = read_fixture("icssw-latest.json");
    const char *names[] = {"T-Beam-1W.bin", "T3_S3_V13.bin", "t_connect_pro.bin"};
    for (const char *n : names)
    {
        FwReleaseInfo r;
        TEST_ASSERT_TRUE(scan(j, n, 4096, r));
        TEST_ASSERT_EQUAL_STRING("v4.40a", r.tag);
        TEST_ASSERT_TRUE_MESSAGE(r.found, n);
        TEST_ASSERT_TRUE(strstr(r.url, "icssw-org/MeshCom-Firmware/releases/download/v4.40a/") != nullptr);
    }
}

static void test_missing_asset_is_not_found_but_release_is_read()
{
    const std::string j = read_fixture("dk5en-latest.json");
    for (size_t c : kChunks)
    {
        FwReleaseInfo r;
        // the real release has no .zz yet (they are added by the release process)
        TEST_ASSERT_TRUE(scan(j, "heltec_wifi_lora_32_V3.bin.zz", c, r));
        TEST_ASSERT_EQUAL_STRING("v4.40a.10.02", r.tag);
        TEST_ASSERT_FALSE(r.found);
        TEST_ASSERT_EQUAL_UINT32(0u, r.size);
        TEST_ASSERT_FALSE(r.haveDigest);
        TEST_ASSERT_EQUAL_CHAR('\0', r.url[0]);
        // prefix of a real name must not match
        TEST_ASSERT_TRUE(scan(j, "heltec_wifi_lora_32_V", c, r));
        TEST_ASSERT_FALSE(r.found);
        // the release name (top level "name") is not an asset name
        TEST_ASSERT_TRUE(scan(j, "v4.40a.10.02", c, r));
        TEST_ASSERT_FALSE(r.found);
    }
    FwReleaseInfo r;
    TEST_ASSERT_TRUE(scan(j, "", 4096, r));
    TEST_ASSERT_FALSE(r.found);
    TEST_ASSERT_TRUE(scan(j, nullptr, 4096, r));
    TEST_ASSERT_FALSE(r.found);
    // a name longer than the scanner's copy never matches and never overflows
    TEST_ASSERT_TRUE(scan(j, "0123456789012345678901234567890123456789012345678901234567890.bin", 64, r));
    TEST_ASSERT_FALSE(r.found);
}

static void test_synthetic_zz_assets_found()
{
    const std::string j = read_fixture("dk5en-zz-synthetic.json");
    for (size_t c : kChunks)
    {
        char n[64];
        FwReleaseInfo r;
        TEST_ASSERT_TRUE(fwAssetName("heltec_wifi_lora_32_V3", n, sizeof(n)));
        TEST_ASSERT_TRUE(scan(j, n, c, r));
        TEST_ASSERT_EQUAL_STRING("v4.40a.10.02", r.tag);
        TEST_ASSERT_TRUE(r.found);
        TEST_ASSERT_EQUAL_UINT32(799685u, r.size);
        TEST_ASSERT_TRUE(r.haveDigest);
        TEST_ASSERT_EQUAL_STRING("c3ac9c1b58bdf193c788e2170b0f997d7b3d6dd40f81d4c664350508efc1d1b7",
                                 sha_hex(r.sha256).c_str());
        TEST_ASSERT_EQUAL_STRING(
            "https://github.com/DK5EN/MeshCom-Firmware/releases/download/v4.40a.10.02/"
            "heltec_wifi_lora_32_V3.bin.zz",
            r.url);
        // the plain asset next to it is still its own asset
        TEST_ASSERT_TRUE(scan(j, "heltec_wifi_lora_32_V3.bin", c, r));
        TEST_ASSERT_TRUE(r.found);
        TEST_ASSERT_EQUAL_UINT32(1537856u, r.size);
    }
}

static void test_synthetic_zz_name_key_last()
{
    // T-Beam-1W.bin.zz carries its keys in the order url, ..., size, digest, ..., name.
    const std::string j = read_fixture("dk5en-zz-synthetic.json");
    const size_t at = j.find("\"name\": \"T-Beam-1W.bin.zz\"");
    TEST_ASSERT_TRUE(at != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"size\": 811066") < at); // size precedes the name: premise
    char n[64];
    TEST_ASSERT_TRUE(fwAssetName("LilyGo_T-Beam-1W", n, sizeof(n)));
    for (size_t c : kChunks)
    {
        FwReleaseInfo r;
        TEST_ASSERT_TRUE(scan(j, n, c, r));
        TEST_ASSERT_TRUE(r.found);
        TEST_ASSERT_EQUAL_UINT32(811066u, r.size);
        TEST_ASSERT_TRUE(r.haveDigest);
        TEST_ASSERT_EQUAL_STRING("2017ffa35bb8ed3cbe2b948da2a58c994edba40668564ee54b6b973217be9360",
                                 sha_hex(r.sha256).c_str());
        TEST_ASSERT_EQUAL_STRING(
            "https://github.com/DK5EN/MeshCom-Firmware/releases/download/v4.40a.10.02/T-Beam-1W.bin.zz",
            r.url);
    }
}

// ---- inline documents -----------------------------------------------------------------

static const char kHeader[] = "{\"tag_name\":\"v9.9z\",\"draft\":false,\"prerelease\":false,";

static std::string doc(const std::string &assets_body, const char *head = kHeader)
{
    return std::string(head) + "\"assets\":[" + assets_body + "]}";
}

static void test_toplevel_fields_not_overridden_by_subobjects()
{
    // author and uploader repeat tag_name/draft/prerelease/assets/name/size/url keys
    const std::string j =
        "{\"author\":{\"tag_name\":\"evil\",\"draft\":true,\"prerelease\":true,"
        "\"assets\":[{\"name\":\"fw.bin.zz\",\"size\":1,\"browser_download_url\":\"http://evil\"}]},"
        "\"tag_name\":\"v1.2c\",\"draft\":false,\"prerelease\":false,"
        "\"assets\":[{\"uploader\":{\"name\":\"fw.bin.zz\",\"size\":2,\"digest\":\"sha256:00\","
        "\"browser_download_url\":\"http://evil2\",\"draft\":true},"
        "\"name\":\"fw.bin.zz\",\"size\":42,\"digest\":\"sha256:" +
        std::string(64, 'a') + "\",\"browser_download_url\":\"https://ok/fw.bin.zz\"}],"
                               "\"author2\":{\"tag_name\":\"late-evil\",\"prerelease\":true}}";
    for (size_t c : kChunks)
    {
        FwReleaseInfo r;
        TEST_ASSERT_TRUE(scan(j, "fw.bin.zz", c, r));
        TEST_ASSERT_EQUAL_STRING("v1.2c", r.tag);
        TEST_ASSERT_FALSE(r.draft);
        TEST_ASSERT_FALSE(r.prerelease);
        TEST_ASSERT_TRUE(r.found);
        TEST_ASSERT_EQUAL_UINT32(42u, r.size);
        TEST_ASSERT_EQUAL_STRING("https://ok/fw.bin.zz", r.url);
        TEST_ASSERT_EQUAL_STRING(std::string(64, 'a').c_str(), sha_hex(r.sha256).c_str());
    }
}

static void test_draft_and_prerelease_flags()
{
    const std::string a = "{\"name\":\"fw.bin.zz\",\"size\":5,\"browser_download_url\":\"u\"}";
    struct Case
    {
        const char *head;
        bool draft, pre;
    } cases[] = {
        {"{\"tag_name\":\"v1\",\"draft\":true,\"prerelease\":false,", true, false},
        {"{\"tag_name\":\"v1\",\"draft\":false,\"prerelease\":true,", false, true},
        {"{\"tag_name\":\"v1\",\"draft\": true , \"prerelease\" :true,", true, true},
        {"{\"draft\":true,\"prerelease\":true,\"tag_name\":\"v1\",", true, true},
        {"{\"tag_name\":\"v1\",\"draft\":null,\"prerelease\":false,", false, false},
    };
    for (const Case &cs : cases)
        for (size_t c : kChunks)
        {
            FwReleaseInfo r;
            TEST_ASSERT_TRUE(scan(doc(a, cs.head), "fw.bin.zz", c, r));
            TEST_ASSERT_EQUAL(cs.draft, r.draft);
            TEST_ASSERT_EQUAL(cs.pre, r.prerelease);
            TEST_ASSERT_TRUE(r.found);
        }
}

static void test_asset_field_order_and_missing_digest()
{
    const std::string dig = "\"digest\":\"sha256:" + std::string(63, '1') + "f\"";
    const std::string orders[] = {
        "{\"browser_download_url\":\"https://x/a\",\"size\":7," + dig + ",\"name\":\"a.bin\"}",
        "{\"size\":7,\"name\":\"a.bin\",\"browser_download_url\":\"https://x/a\"," + dig + "}",
        "{" + dig + ",\"browser_download_url\":\"https://x/a\",\"name\":\"a.bin\",\"size\":7}",
    };
    for (const std::string &o : orders)
        for (size_t c : kChunks)
        {
            // an unrelated asset before and after must not leak candidate fields
            const std::string body = "{\"name\":\"z.bin\",\"size\":999,\"browser_download_url\":\"https://x/z\","
                                     "\"digest\":\"sha256:" +
                                     std::string(64, '9') + "\"}," + o +
                                     ",{\"name\":\"y.bin\",\"size\":1,\"browser_download_url\":\"https://x/y\"}";
            FwReleaseInfo r;
            TEST_ASSERT_TRUE(scan(doc(body), "a.bin", c, r));
            TEST_ASSERT_TRUE(r.found);
            TEST_ASSERT_EQUAL_UINT32(7u, r.size);
            TEST_ASSERT_EQUAL_STRING("https://x/a", r.url);
            TEST_ASSERT_TRUE(r.haveDigest);
            TEST_ASSERT_EQUAL_UINT8(0x11, r.sha256[0]);
            TEST_ASSERT_EQUAL_UINT8(0x1f, r.sha256[31]);
        }
    // digest missing or malformed: asset still usable, haveDigest false
    const std::string bad[] = {
        "{\"name\":\"a.bin\",\"size\":7,\"browser_download_url\":\"https://x/a\"}",
        "{\"name\":\"a.bin\",\"size\":7,\"digest\":null,\"browser_download_url\":\"https://x/a\"}",
        "{\"name\":\"a.bin\",\"size\":7,\"digest\":\"md5:abcd\",\"browser_download_url\":\"https://x/a\"}",
        "{\"name\":\"a.bin\",\"size\":7,\"digest\":\"sha256:abcd\",\"browser_download_url\":\"https://x/a\"}",
        "{\"name\":\"a.bin\",\"size\":7,\"digest\":\"sha256:" + std::string(65, 'a') +
            "\",\"browser_download_url\":\"https://x/a\"}",
        "{\"name\":\"a.bin\",\"size\":7,\"digest\":\"sha256:" + std::string(63, 'a') +
            "g\",\"browser_download_url\":\"https://x/a\"}",
    };
    for (const std::string &b : bad)
    {
        FwReleaseInfo r;
        TEST_ASSERT_TRUE(scan(doc(b), "a.bin", 7, r));
        TEST_ASSERT_TRUE_MESSAGE(r.found, b.c_str());
        TEST_ASSERT_FALSE_MESSAGE(r.haveDigest, b.c_str());
    }
    // size or url missing: not found
    FwReleaseInfo r;
    TEST_ASSERT_TRUE(scan(doc("{\"name\":\"a.bin\",\"browser_download_url\":\"https://x/a\"}"), "a.bin", 7, r));
    TEST_ASSERT_FALSE(r.found);
    TEST_ASSERT_TRUE(scan(doc("{\"name\":\"a.bin\",\"size\":7}"), "a.bin", 7, r));
    TEST_ASSERT_FALSE(r.found);
}

static void test_first_matching_asset_wins()
{
    const std::string body = "{\"name\":\"a.bin\",\"size\":1,\"browser_download_url\":\"https://first\","
                             "\"digest\":\"sha256:" +
                             std::string(64, '1') +
                             "\"},"
                             "{\"name\":\"a.bin\",\"size\":2,\"browser_download_url\":\"https://second\","
                             "\"digest\":\"sha256:" +
                             std::string(64, '2') + "\"}";
    for (size_t c : kChunks)
    {
        FwReleaseInfo r;
        TEST_ASSERT_TRUE(scan(doc(body), "a.bin", c, r));
        TEST_ASSERT_EQUAL_UINT32(1u, r.size);
        TEST_ASSERT_EQUAL_STRING("https://first", r.url);
        TEST_ASSERT_EQUAL_UINT8(0x11, r.sha256[5]);
    }
}

static void test_string_escapes_whitespace_numbers_nesting()
{
    // pretty-printed, escapes in names/urls, numbers of every shape, nested arrays/objects,
    // braces and quotes inside strings that must not confuse the structure
    const std::string j =
        "\n {\n \"id\" : -12.5e+3 ,\n \"body\" : \"text with } ] { [ \\\" and \\\\ and \\u007d\",\n"
        " \"tag_name\" : \"v\\u0034.40a\\/x\" ,\n \"draft\" : false , \"prerelease\":false,\n"
        " \"extra\": [ [ 1 , 2.5 , [ ] ] , { } , { \"a\" : { \"b\" : [ null , true ] } } ] ,\n"
        " \"assets\" : [\n  { \"x\" : [ { \"name\" : \"fw.bin.zz\" } ] , \"name\" : \"other\\\"s.bin\" ,"
        " \"size\" : 3 , \"browser_download_url\" : \"u\" },\n"
        "  { \"label\" : \"\" , \"size\" : 1234567 ,\n"
        "    \"name\" : \"f\\u0077.bin\\u002ezz\" ,\n"
        "    \"browser_download_url\" : \"https://h/p?a=1\\u0026b=2\\/c\\\\d\" , \"download_count\" : 0 }\n"
        " ] ,\n \"tarball_url\" : \"x\" }\n";
    for (size_t c : kChunks)
    {
        FwReleaseInfo r;
        TEST_ASSERT_TRUE(scan(j, "fw.bin.zz", c, r));
        TEST_ASSERT_EQUAL_STRING("v4.40a/x", r.tag);
        TEST_ASSERT_TRUE(r.found);
        TEST_ASSERT_EQUAL_UINT32(1234567u, r.size);
        TEST_ASSERT_EQUAL_STRING("https://h/p?a=1&b=2/c\\d", r.url);
        TEST_ASSERT_FALSE(r.haveDigest);
        // the escaped quote name matches only its decoded form
        TEST_ASSERT_TRUE(scan(j, "other\"s.bin", c, r));
        TEST_ASSERT_TRUE(r.found);
        TEST_ASSERT_EQUAL_UINT32(3u, r.size);
        TEST_ASSERT_TRUE(scan(j, "other\\\"s.bin", c, r));
        TEST_ASSERT_FALSE(r.found);
    }
}

static void test_size_saturates_instead_of_wrapping()
{
    const std::string j =
        doc("{\"name\":\"a.bin\",\"size\":99999999999999999999,\"browser_download_url\":\"u\"}");
    FwReleaseInfo r;
    TEST_ASSERT_TRUE(scan(j, "a.bin", 4096, r));
    TEST_ASSERT_TRUE(r.found);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, r.size);
}

static void test_long_url_is_not_found_not_a_crash()
{
    const std::string pre = "https://github.com/DK5EN/MeshCom-Firmware/releases/download/v1/";
    for (size_t len : {254u, 255u, 256u, 257u, 4000u})
    {
        const std::string url = pre + std::string(len - pre.size(), 'x');
        const std::string a = "{\"name\":\"a.bin\",\"size\":5,\"browser_download_url\":\"" + url + "\"}";
        for (size_t c : kChunks)
        {
            FwReleaseInfo r;
            TEST_ASSERT_TRUE(scan(doc(a), "a.bin", c, r));
            TEST_ASSERT_EQUAL_STRING("v9.9z", r.tag);
            if (len <= 255)
            {
                TEST_ASSERT_TRUE(r.found);
                TEST_ASSERT_EQUAL_UINT32(len, strlen(r.url));
                TEST_ASSERT_EQUAL_STRING(url.c_str(), r.url);
            }
            else
            {
                TEST_ASSERT_FALSE(r.found);
                TEST_ASSERT_EQUAL_CHAR('\0', r.url[0]);
            }
        }
    }
    // an over-long url on a non-matching asset does not disturb a later match
    const std::string body = "{\"name\":\"z.bin\",\"size\":1,\"browser_download_url\":\"" +
                             std::string(1000, 'y') +
                             "\"},{\"name\":\"a.bin\",\"size\":5,\"browser_download_url\":\"https://ok\"}";
    FwReleaseInfo r;
    TEST_ASSERT_TRUE(scan(doc(body), "a.bin", 7, r));
    TEST_ASSERT_TRUE(r.found);
    TEST_ASSERT_EQUAL_STRING("https://ok", r.url);
}

static void test_overlong_tag_and_keys()
{
    FwReleaseInfo r;
    // tag of 23 chars fits, 24 does not (result false)
    TEST_ASSERT_TRUE(scan("{\"tag_name\":\"" + std::string(23, 't') + "\",\"assets\":[]}", "a.bin", 5, r));
    TEST_ASSERT_EQUAL_UINT32(23u, strlen(r.tag));
    TEST_ASSERT_FALSE(scan("{\"tag_name\":\"" + std::string(24, 't') + "\",\"assets\":[]}", "a.bin", 5, r));
    // very long unknown keys and values everywhere
    const std::string j = "{\"" + std::string(5000, 'k') + "\":\"" + std::string(5000, 'v') +
                          "\",\"tag_name\":\"v1\",\"assets\":[{\"" + std::string(300, 'q') +
                          "\":1,\"name\":\"a.bin\",\"size\":2,\"browser_download_url\":\"u\"}]}";
    TEST_ASSERT_TRUE(scan(j, "a.bin", 64, r));
    TEST_ASSERT_TRUE(r.found);
    // a key that has the right prefix but is longer must not match
    TEST_ASSERT_TRUE(scan("{\"tag_name\":\"v1\",\"assets\":[{\"name\":\"a.bin\",\"sizeX\":2,"
                          "\"browser_download_url_extra_long_key_x\":\"u\"}]}",
                          "a.bin", 64, r));
    TEST_ASSERT_FALSE(r.found);
}

static void test_missing_tag_or_wrong_root_is_false()
{
    FwReleaseInfo r;
    TEST_ASSERT_FALSE(scan("{\"assets\":[{\"name\":\"a.bin\",\"size\":1,\"browser_download_url\":\"u\"}]}",
                           "a.bin", 7, r));
    TEST_ASSERT_FALSE(r.found);
    TEST_ASSERT_FALSE(scan("{\"message\":\"Not Found\",\"status\":\"404\"}", "a.bin", 7, r));
    TEST_ASSERT_FALSE(scan("[]", "a.bin", 7, r));
    TEST_ASSERT_FALSE(scan("\"tag_name\"", "a.bin", 7, r));
    TEST_ASSERT_FALSE(scan("tag_name", "a.bin", 7, r));
    TEST_ASSERT_FALSE(scan("", "a.bin", 7, r));
    TEST_ASSERT_FALSE(scan("{\"tag_name\":\"v1\"}}", "a.bin", 7, r)); // extra close
    // "assets" that is not an array of objects is tolerated
    TEST_ASSERT_TRUE(scan("{\"tag_name\":\"v1\",\"assets\":null}", "a.bin", 7, r));
    TEST_ASSERT_FALSE(r.found);
    TEST_ASSERT_TRUE(scan("{\"tag_name\":\"v1\",\"assets\":[1,\"x\",[],null,{}]}", "a.bin", 7, r));
    TEST_ASSERT_FALSE(r.found);
}

static void test_malformed_documents_do_not_crash()
{
    const char *bad[] = {
        "{\"tag_name\":\"v1\",\"assets\":[{\"name\":\"a.bin\",\"size\":1,\"browser_download_url\":\"u\"]}",
        "{\"tag_name\":\"v1\",\"assets\":{\"name\":\"a.bin\"]}",
        "{\"tag_name\":\"v\\q1\"}",
        "{\"tag_name\":\"v\\u00zz\"}",
        "}}}}{{{{",
        "{\"a\":\"\\",
        "{\"tag_name\":\"v1\"}{\"tag_name\":\"v2\"}",
        "{\"tag_name\":\"v1\"} trailing",
        "\x01\x02\xff\xfe{",
    };
    for (const char *b : bad)
        for (size_t c : kChunks)
        {
            FwReleaseInfo r;
            const bool ok = scan(b, "a.bin", c, r);
            TEST_ASSERT_FALSE_MESSAGE(ok && r.found, b);
        }
    // depth bomb
    std::string deep(5000, '[');
    FwReleaseInfo r;
    TEST_ASSERT_FALSE(scan("{\"tag_name\":\"v1\",\"x\":" + deep, "a.bin", 64, r));
    // feeding after an error is harmless
    FwJsonScan s;
    fwScanInit(s, "a.bin");
    fwScanFeed(s, "]", 1);
    fwScanFeed(s, "{\"tag_name\":\"v1\"}", 17);
    TEST_ASSERT_FALSE(fwScanResult(s, r));
    fwScanFeed(s, nullptr, 5);
}

static void test_truncated_documents_never_report_found()
{
    // every prefix of a small document, one byte at a time
    const std::string j = doc("{\"name\":\"a.bin\",\"size\":7,\"digest\":\"sha256:" + std::string(64, 'c') +
                              "\",\"browser_download_url\":\"https://x/a\"}");
    const size_t full_found_at = j.find("https://x/a\"}") + 13; // asset object closed here
    const size_t tag_done_at = j.find("v9.9z\"") + 6;
    for (size_t cut = 0; cut <= j.size(); cut++)
    {
        FwReleaseInfo r;
        const bool ok = scan(j.substr(0, cut), "a.bin", 1, r);
        if (cut < full_found_at)
            TEST_ASSERT_FALSE_MESSAGE(ok && r.found, "found before the asset object closed");
        else
            TEST_ASSERT_TRUE(ok && r.found);
        if (cut < tag_done_at)
            TEST_ASSERT_FALSE_MESSAGE(ok, "tag not complete yet");
    }
    // prefixes of the real document at odd offsets
    const std::string real = read_fixture("dk5en-latest.json");
    for (size_t cut = 0; cut < real.size(); cut += 997)
    {
        FwReleaseInfo r;
        (void)scan(real.substr(0, cut), "heltec_wifi_lora_32_V3.bin", 4096, r);
    }
    // the real document cut inside the wanted asset: found must be false
    const size_t at = real.find("\"name\": \"heltec_wifi_lora_32_V3.bin\"");
    FwReleaseInfo r;
    const bool ok = scan(real.substr(0, at + 300), "heltec_wifi_lora_32_V3.bin", 64, r);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(r.found);
    TEST_ASSERT_EQUAL_STRING("v4.40a.10.02", r.tag);
}

static void test_chunking_is_equivalent_on_every_split_of_a_small_document()
{
    const std::string j = "{ \"tag_name\" : \"v\\u0031.0\" , \"assets\" : [ { \"name\" : \"a\\u002ebin\" , "
                          "\"size\" : 12 , \"browser_download_url\" : \"h\\/p\" } ] }";
    for (size_t split = 0; split <= j.size(); split++)
    {
        FwJsonScan s;
        fwScanInit(s, "a.bin");
        fwScanFeed(s, j.data(), split);
        fwScanFeed(s, j.data() + split, j.size() - split);
        FwReleaseInfo r;
        TEST_ASSERT_TRUE(fwScanResult(s, r));
        TEST_ASSERT_EQUAL_STRING("v1.0", r.tag);
        TEST_ASSERT_TRUE(r.found);
        TEST_ASSERT_EQUAL_UINT32(12u, r.size);
        TEST_ASSERT_EQUAL_STRING("h/p", r.url);
    }
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_asset_name_plain_and_renames);
    RUN_TEST(test_asset_name_bounds);
    RUN_TEST(test_state_is_bounded);
    RUN_TEST(test_dk5en_plain_bin_all_chunk_sizes);
    RUN_TEST(test_dk5en_every_asset_matches_independent_search);
    RUN_TEST(test_icssw_every_asset_matches_independent_search);
    RUN_TEST(test_icssw_renamed_assets);
    RUN_TEST(test_missing_asset_is_not_found_but_release_is_read);
    RUN_TEST(test_synthetic_zz_assets_found);
    RUN_TEST(test_synthetic_zz_name_key_last);
    RUN_TEST(test_toplevel_fields_not_overridden_by_subobjects);
    RUN_TEST(test_draft_and_prerelease_flags);
    RUN_TEST(test_asset_field_order_and_missing_digest);
    RUN_TEST(test_first_matching_asset_wins);
    RUN_TEST(test_string_escapes_whitespace_numbers_nesting);
    RUN_TEST(test_size_saturates_instead_of_wrapping);
    RUN_TEST(test_long_url_is_not_found_not_a_crash);
    RUN_TEST(test_overlong_tag_and_keys);
    RUN_TEST(test_missing_tag_or_wrong_root_is_false);
    RUN_TEST(test_malformed_documents_do_not_crash);
    RUN_TEST(test_truncated_documents_never_report_found);
    RUN_TEST(test_chunking_is_equivalent_on_every_split_of_a_small_document);
    return UNITY_END();
}
