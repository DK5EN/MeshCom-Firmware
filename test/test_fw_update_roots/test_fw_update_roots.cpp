// Host test for the host -> trust anchor selector of src/fw_update_roots.h (OTA-LOWHEAP-01 fix B2).
//
//   pio test -e native_fw_update_roots
//
// The Auto Update talks to two TLS families only (Sectigo for github.com, Let's Encrypt for
// githubusercontent.com). Parsing all five bundled roots costs ~50 small allocations; the
// selector hands McTlsClient the one root a host needs, and the full bundle for anything else.

#include <unity.h>

#include <stdio.h>
#include <string.h>

#include <fw_update_roots.h>

void setUp(void) {}
void tearDown(void) {}

static int countCerts(const char *pem)
{
    int n = 0;
    for (const char *p = pem; (p = strstr(p, "-----BEGIN CERTIFICATE-----")) != nullptr; p++)
        n++;
    return n;
}

static void test_github_hosts_get_sectigo_e46(void)
{
    const char *hosts[] = {"api.github.com", "github.com", "codeload.github.com", "uploads.github.com"};
    for (const char *h : hosts) {
        TEST_ASSERT_EQUAL_PTR_MESSAGE(FW_ROOT_SECTIGO_E46_PEM, fwRootForHost(h), h);
    }
}

static void test_githubusercontent_hosts_get_isrg_yr(void)
{
    const char *hosts[] = {"release-assets.githubusercontent.com", "objects.githubusercontent.com",
                           "raw.githubusercontent.com"};
    for (const char *h : hosts) {
        TEST_ASSERT_EQUAL_PTR_MESSAGE(FW_ROOT_ISRG_YR_PEM, fwRootForHost(h), h);
    }
}

static void test_selected_root_is_exactly_one_certificate_from_the_bundle(void)
{
    const char *roots[] = {fwRootForHost("api.github.com"), fwRootForHost("objects.githubusercontent.com")};
    for (const char *r : roots) {
        TEST_ASSERT_EQUAL_INT(1, countCerts(r));
        TEST_ASSERT_NOT_NULL(strstr(FW_UPDATE_ROOTS_PEM, r));
        TEST_ASSERT_TRUE(strlen(r) < strlen(FW_UPDATE_ROOTS_PEM) / 2);
    }
    TEST_ASSERT_EQUAL_INT(5, countCerts(FW_UPDATE_ROOTS_PEM));
}

static void test_unknown_host_gets_full_bundle(void)
{
    TEST_ASSERT_EQUAL_PTR(FW_UPDATE_ROOTS_PEM, fwRootForHost("example.com"));
    TEST_ASSERT_EQUAL_PTR(FW_UPDATE_ROOTS_PEM, fwRootForHost("192.168.1.1"));
    TEST_ASSERT_EQUAL_PTR(FW_UPDATE_ROOTS_PEM, fwRootForHost(""));
    TEST_ASSERT_EQUAL_PTR(FW_UPDATE_ROOTS_PEM, fwRootForHost(nullptr));
}

static void test_suffix_needs_a_dot_boundary(void)
{
    TEST_ASSERT_EQUAL_PTR(FW_UPDATE_ROOTS_PEM, fwRootForHost("evilgithub.com"));
    TEST_ASSERT_EQUAL_PTR(FW_UPDATE_ROOTS_PEM, fwRootForHost("github.com.evil.example"));
    TEST_ASSERT_EQUAL_PTR(FW_UPDATE_ROOTS_PEM, fwRootForHost("notgithubusercontent.com"));
    TEST_ASSERT_EQUAL_PTR(FW_UPDATE_ROOTS_PEM, fwRootForHost("githubusercontent.com.evil.example"));
}

static void test_case_port_and_trailing_dot(void)
{
    TEST_ASSERT_EQUAL_PTR(FW_ROOT_SECTIGO_E46_PEM, fwRootForHost("API.GitHub.com"));
    TEST_ASSERT_EQUAL_PTR(FW_ROOT_SECTIGO_E46_PEM, fwRootForHost("api.github.com:443"));
    TEST_ASSERT_EQUAL_PTR(FW_ROOT_SECTIGO_E46_PEM, fwRootForHost("api.github.com."));
    TEST_ASSERT_EQUAL_PTR(FW_ROOT_ISRG_YR_PEM, fwRootForHost("Release-Assets.GithubUserContent.com:443"));
    TEST_ASSERT_EQUAL_PTR(FW_UPDATE_ROOTS_PEM, fwRootForHost("example.com:443"));
}

static void test_bundle_text_is_unchanged_by_the_split(void)
{
    // The five anchors, in the original order (fingerprints are in the header comments).
    TEST_ASSERT_TRUE(strlen(FW_UPDATE_ROOTS_PEM) > 6000);
    TEST_ASSERT_EQUAL_INT(5, countCerts(FW_UPDATE_ROOTS_PEM));
    TEST_ASSERT_NOT_NULL(strstr(FW_UPDATE_ROOTS_PEM, FW_ROOT_SECTIGO_E46_PEM));
    TEST_ASSERT_NOT_NULL(strstr(FW_UPDATE_ROOTS_PEM, FW_ROOT_ISRG_YR_PEM));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_github_hosts_get_sectigo_e46);
    RUN_TEST(test_githubusercontent_hosts_get_isrg_yr);
    RUN_TEST(test_selected_root_is_exactly_one_certificate_from_the_bundle);
    RUN_TEST(test_unknown_host_gets_full_bundle);
    RUN_TEST(test_suffix_needs_a_dot_boundary);
    RUN_TEST(test_case_port_and_trailing_dot);
    RUN_TEST(test_bundle_text_is_unchanged_by_the_split);
    return UNITY_END();
}
