// Recording stubs for everything the request path of src/web_functions/web_functions.cpp calls but does
// not decide: the page handlers, the Remote and config handlers, the node functions, the setup rungs,
// sendMessage(), the AU gate. Each one leaves one trace line (rig/web_rig.h) and, where the real handler
// reads a request body or writes a short fixed answer, does exactly that part.
//
// NO INCLUDE GUARD AND NO INCLUDES: this file is #included inside the namespace of the unit under test
// (web_frozen today, the live request module later), after rig/web_rig.h, the real headers
// (web_functions.h, web_setup.h, web_nodefunctioncalls.h, web_rm_parse.h, backpressure.h) and the
// declarations of the unit's own body readers. The live wiring includes the very same file, so both
// entry points leave identical traces for identical calls.
//
// Needs from the including unit, as macros (the unit's own body readers are what the differential compares):
//   WEB_RIG_RM_READ(buf, cap, content_length)   the Remote body reader     (rmReadBody)
//   WEB_RIG_READ_BODY(content_length)           the config body reader     (web_read_body)
//   WEB_RIG_CFG_ALLOC() / WEB_RIG_CFG_FREE() / WEB_RIG_CFG_BUF   the config buffer of the unit
// Everything with external linkage in the real build (webFunctionCall, webSetup_*, sendMessage, msgstore*, the
// AU gate, webTxBeginChunked, getTimeString) is in rig/web_stubs.cpp, once for every entry point.

static std::string wrTrunc(const char *s, size_t n, size_t cap = 120)
{
    std::string r(s, n > cap ? cap : n);
    if (n > cap)
        r += "...";
    return r;
}

// ---- pages and JSON status endpoints: the handler id is the whole answer
#define WR_STUB0(fn)                \
    void fn(void) { webRigTrace(#fn); }
WR_STUB0(sub_page_login)
WR_STUB0(sub_page_unknown)
WR_STUB0(sub_page_info)
WR_STUB0(sub_page_position)
WR_STUB0(sub_page_wx)
WR_STUB0(sub_page_setup)
WR_STUB0(sub_page_messages)
WR_STUB0(sub_page_mailbox)
WR_STUB0(sub_page_rxlog)
WR_STUB0(sub_page_path)
WR_STUB0(sub_page_mheard)
WR_STUB0(sub_page_neighbours)
WR_STUB0(sub_page_spectrum)
WR_STUB0(sub_page_mcp23017)
WR_STUB0(sub_page_pins)
WR_STUB0(sub_page_remote)
WR_STUB0(sub_content_messages)
WR_STUB0(sub_rm_status)
WR_STUB0(sub_rm_nodes_get)
WR_STUB0(sub_rm_heard)
WR_STUB0(sub_pin_status)
WR_STUB0(sub_pin_assign)
WR_STUB0(sub_gpio_status)
WR_STUB0(sub_au_status)
WR_STUB0(sub_screen_oled)
WR_STUB0(sub_screen_rgb565)
WR_STUB0(sub_config_download)

void deliver_scaffold(bool bget_password) { webRigTrace("deliver_scaffold password=%d", bget_password ? 1 : 0); }

// ---- handlers that read the request body: the real body reader runs, the result is the trace
static void wrRmBody(const char *id, long content_length)
{
    char body[RM_FORM_BODY_MAX + 1];
    const char *err = WEB_RIG_RM_READ(body, sizeof(body), content_length);
    webRigTrace("%s content_length=%ld err=%s body='%s'", id, content_length, err ? err : "(none)",
                webRigEsc(wrTrunc(body, strlen(body))).c_str());
}
void sub_rm_send(long content_length) { wrRmBody("sub_rm_send", content_length); }
void sub_rm_passwd(long content_length) { wrRmBody("sub_rm_passwd", content_length); }
void sub_web_passwd(long content_length) { wrRmBody("sub_web_passwd", content_length); }
void sub_rm_nodes_post(long content_length) { wrRmBody("sub_rm_nodes_post", content_length); }

void sub_config_upload(long content_length)
{
    if (!WEB_RIG_CFG_ALLOC())
    {
        webRigTrace("sub_config_upload content_length=%ld alloc-failed", content_length);
        return;
    }
    const int n = WEB_RIG_READ_BODY(content_length);
    webRigTrace("sub_config_upload content_length=%ld rc=%d data='%s'", content_length, n,
                n > 0 ? webRigEsc(wrTrunc(WEB_RIG_CFG_BUF, (size_t)n)).c_str() : "");
    WEB_RIG_CFG_FREE();
}

// ---- small environment: remote address, RNG, the gzip assets (7 bytes each, one with a NUL)
static const char *web_remote_ip(char *buf, size_t n)
{
    snprintf(buf, n, "192.0.2.77");
    return buf;
}
static bool web_auth_rng(uint8_t *out, size_t n) { return webRigRng(out, n); }

static const uint8_t web_mc_css_gz[] = {'C', 'S', 'S', 0, 'G', 'Z', 1};
static const uint8_t web_mc_js_gz[] = {'J', 'S', 0, 'G', 'Z', 2, 2};
static const uint8_t web_mc_rm_js_gz[] = {'R', 'M', 0, 'G', 'Z', 3, 3};
static const uint8_t web_mc_au_js_gz[] = {'A', 'U', 0, 'G', 'Z', 4, 4};
