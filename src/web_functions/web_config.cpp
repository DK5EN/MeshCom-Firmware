#if !defined(NRF52_SERIES) || HAS_NRF_ETH   // web GUI is reachable over Ethernet only on nRF52
// FORMAL-02 wave 8 step 9g: config export/import glue of the web server (moved out of web_functions.cpp as is)
#include <Arduino.h>
#include <configuration.h>
#include <debugconf.h>
#include "web_config.h"
#include "web_functions.h"   // send_http_header, RESPONSE_TYPE_*
#include "web_rm_util.h"     // extern web_client (reused, not redeclared)
#include "web_io.h"          // webReadBody, webSendFixedHead
#include "web_body.h"        // WB_SHORT
#include <config_json.h>     // CS-03: config download/upload as one JSON object, CONFIG_JSON_MAX
#include "../reboot_defer.h"   // rebootDeferArm: the restart after the import runs from the main loop
#include <prod_lock.h>       // prodLockNormalise(): an imported file may hold values the setters now refuse
#include <instrument.h>      // INSTRUMENT_ENABLED
#include <loop_functions.h>  // save_settings()

/**
 * ###########################################################################################################################
 * CS-03: config download / upload
 *
 * One buffer serves both directions -- the export never runs while an upload
 * body is being read. The worst-case export (every string field filled to its
 * limit) measures about 3.1 kB, so CONFIG_JSON_MAX is roughly a factor of two
 * of headroom and at the same time the hard cap on what an upload may send.
 *
 * On the heap, NOT in BSS: 6 kB of static buffer overflows dram0_0_seg on the
 * plain ESP32 (E22-DevKitC links with ~4 kB to spare). It is allocated for the
 * duration of one /config.json or POST /config request and released again --
 * both are rare, operator-triggered requests.
 */
static char *web_cfg_buf = NULL;

static bool web_cfg_alloc(void)
{
    if (web_cfg_buf == NULL)
        web_cfg_buf = (char *)malloc(CONFIG_JSON_MAX + 1);

    if (web_cfg_buf != NULL)
        web_cfg_buf[0] = '\0';

    return web_cfg_buf != NULL;
}

static void web_cfg_free(void)
{
    if (web_cfg_buf != NULL)
    {
        free(web_cfg_buf);
        web_cfg_buf = NULL;
    }
}

/**
 * Reads the request BODY.
 *
 * work_webpage() has only ever read the request HEADER -- it stops at the
 * blank line and answers. A file upload is the first thing here that carries
 * a body, so this is the reader for it: bounded by the Content-Length the
 * caller picked out of the header, hard-capped at CONFIG_JSON_MAX, and given
 * up on after WEB_TIMEOUT_TIME without a byte so a lying Content-Length
 * cannot pin the loop.
 *
 * @return bytes read into web_cfg_buf (NUL-terminated), or negative:
 *         -1 no/!invalid Content-Length, -2 too large, -3 short read.
 */
static int web_read_body(long content_length)
{
    // the I/O loop (idle WEB_TIMEOUT_TIME, total 4 s, WDT feed) is web_io.cpp; WB_NOLEN/SIZE/SHORT are -1/-2/-3
    return webReadBody(web_cfg_buf, CONFIG_JSON_MAX + 1, content_length, WEB_TIMEOUT_TIME, 4000);
}

/**
 * GET /config.json -- the whole configuration as a downloadable file.
 *
 * Sends its own header instead of send_http_header(): only this response
 * carries a Content-Disposition, which is what turns a browser navigation
 * into a download instead of a page full of JSON.
 */
void sub_config_download(void)
{
    if (!web_cfg_alloc())
    {
        send_http_header(422, RESPONSE_TYPE_JSON);
        web_client.println("{\"error\":\"out of memory\"}");
        return;
    }

    size_t n = configExportJson(web_cfg_buf, CONFIG_JSON_MAX);

    if (n == 0)
    {
        web_cfg_free();
        send_http_header(422, RESPONSE_TYPE_JSON);
        web_client.println("{\"error\":\"config export did not fit the buffer\"}");
        return;
    }

    /* the callsign goes into a filename -- keep it to characters that cannot
     * break out of the quoted header value or of a directory */
    char fname[16];
    size_t fi = 0;
    for (const char *p = meshcom_settings.node_call; *p && fi < sizeof(fname) - 1; p++)
    {
        char c = *p;
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-')
            fname[fi++] = c;
    }
    fname[fi] = '\0';
    if (fi == 0)
        snprintf(fname, sizeof(fname), "node");

    // D14: CRLF line endings and one head writer (was "200 OK \n" and bare \n after Content-Disposition / Content-Length)
    char extra[128];
    snprintf(extra, sizeof(extra), "Content-Disposition: attachment; filename=\"meshcom-%s.json\"\r\nPragma: no-cache\r\nExpires: 0\r\n", fname);
    webSendFixedHead(200, "application/json", (size_t)n, extra, "no-cache, no-store, must-revalidate");

    web_client.print(web_cfg_buf);

    web_cfg_free();
}

/**
 * POST /config -- restore a configuration file and reboot once.
 *
 * Nothing is written unless configImportJson() accepted every value; on
 * failure the answer is a 400 with the reason and NVRAM is untouched. On
 * success the response is flushed and the connection closed BEFORE the reset,
 * otherwise the browser never sees the answer. Reboot pattern copied from
 * commandAction()'s --cleanflash/--reboot (command_functions.cpp).
 */
void sub_config_upload(long content_length)
{
    char err[160] = {0};

    if (!web_cfg_alloc())
    {
        send_http_header(400, RESPONSE_TYPE_TEXT);
        web_client.printf("<p>upload rejected: out of memory</p>");
        return;
    }

    int n = web_read_body(content_length);

    if (n < 0)
    {
        web_cfg_free();
        send_http_header(n == WB_SHORT ? 408 : 400, RESPONSE_TYPE_TEXT); // B10: a stalled body is a 408
        web_client.printf("<p>upload rejected: %s</p>",
                          (n == -2) ? "file too large" : (n == -1) ? "no Content-Length" : "incomplete upload");
        /* own marker: these codes are the body reader's, not configImportJson()'s */
        Serial.printf("[CONFIG];upload;rc;%d;len;%ld\n", n, content_length);
        return;
    }

    int rc = configImportJson(web_cfg_buf, (size_t)n, err, sizeof(err));

    web_cfg_free();

    if (rc != 0)
    {
        send_http_header(400, RESPONSE_TYPE_TEXT);
        web_client.printf("<p>config import failed: %s</p>", err);
        return;
    }

    // Production lock: a file from an earlier build may hold values the setters refuse now (ptime 1800,
    // pingmax 100, locked spectrum/NBR values). The import accepts them; repair before they are persisted.
    {
#if defined(NRF52_SERIES)
        const bool hasAu = false;
#else
        const bool hasAu = true;
#endif
        const int locked = prodLockNormalise(meshcom_settings, INSTRUMENT_ENABLED != 0, hasAu);
        if(locked > 0)
            Serial.printf("[CONFIG];import;normalised;%d\n", locked);
    }

    save_settings();

    send_http_header(200, RESPONSE_TYPE_TEXT);
    web_client.printf("<html><body><h3>config imported</h3><p>%s</p><p>the node reboots now.</p></body></html>", err);
    web_client.flush();
    web_client.stop();

    Serial.printf("[CONFIG];reboot;after;import\n");

    // B9 (FORMAL-02 9h): the answer is out; the main loop restarts the node a moment later (it used to block the request for two seconds)
    // 2 s, as the old blocking delay: ESP32 stop() does not linger, so the answer may still need a TCP resend
    rebootDeferArm(2000, "config import");
}
#endif
