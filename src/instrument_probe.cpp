/**
 * TEMPORARY MEASUREMENT SCAFFOLDING -- see src/instrument_probe.h.
 */

#include "instrument_probe.h"
#include "instrument.h"
#include <Arduino.h>
#include "printfdeb_functions.h" // printfdeb(): reaches USB serial AND the 2323 net console (Serial.printf does not)

#if INSTRUMENT_ENABLED && defined(ESP32)

#include <esp_heap_caps.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "configuration_global.h"
#include "loop_functions_extern.h"
#include "meshcom_settings.h" // meshcom_settings.node_updchan (read only)
#include "esp32/fw_update_net.h"
#include "web_functions/web_commonServer.h"
#include "web_functions/web_functions.h"

extern CommonWebClient web_client; // web_functions.cpp: the page renderers write here
void sub_page_neighbours(void);    // defined in web_functions.cpp, not declared in web_functions.h

// instrument_heap_lowmark_reset()/_get() come from instrument_heap.h (via instrument.h).
// _get() returns 0xFFFFFFFF when there is no low mark.
#define LOWMARK_UNKNOWN 0xFFFFFFFFu

#define PROBE_SETTLE_MS 10000UL
#define PROBE_TLS_MIN_BLK 4096 /* bytes: below this the tls probe refuses (constructor new would abort) */
#define PROBE_WORKER_STACK 12288 /* bytes, internal RAM; counted in the measurement (task created after the start sample) */
#define PROBE_WORKER_GRACE_MS 100 /* wait after the worker ended so the idle task frees its stack/TCB before the end sample */
#define PROBE_ALL_WAIT_MS 120000UL /* "all": longest wait for an idle AU job / worker before aborting */
#define PROBE_HOST "api.github.com" // the AU host (GitHub releases API); never a MeshCom host

namespace
{
enum Kind : uint8_t { K_SYNC, K_ASYNC };
enum Rc : uint8_t { RC_OK, RC_TIMEOUT, RC_ERR };

struct Probe
{
    const char *id;
    const char *desc;
    Kind kind;
    Rc (*start)(void);   // sync: does the work, returns rc. async: starts it, RC_OK = running, else final rc
    int (*poll)(void);   // async only: 0 = running, 1 = ok, 2 = err
    void (*cleanup)(void);
    uint32_t timeoutMs;
};

// run state
int s_cur = -1;            // running probe index, -1 idle
uint32_t s_t0 = 0, s_deadline = 0;
uint32_t s_startFree = 0, s_lowSampled = 0;
bool s_all = false;
int s_next = 0;            // next index for "all"
uint32_t s_settleUntil = 0;
bool s_settling = false;
uint32_t s_allWaitSince = 0;

// worker task state (dns, tls); only the worker writes s_wOk/s_wDone/s_wBusy after creation
volatile bool s_wBusy = false; // worker task exists
volatile bool s_wDone = false;
volatile bool s_wOk = false;
volatile bool s_wThrew = false;
volatile uint32_t s_wHwm = 0;
int s_wMode = 0; // 0 dns, 1 tls
uint32_t s_wGraceT = 0;

uint32_t ifree()
{
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}
uint32_t iblk()
{
    return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
}
void sampleLow()
{
    uint32_t f = ifree();
    if (f < s_lowSampled)
        s_lowSampled = f;
}

bool wifiUp()
{
    return WiFi.status() == WL_CONNECTED;
}

bool auBusy()
{
    FwNetStatus st;
    fwNetGetStatus(st);
    return st.state == FWS_BUSY;
}

// Reason a probe may not start right now (nullptr = free): AU job running or a worker still alive.
const char *holdOff()
{
    if (auBusy())
        return "au";
    if (s_wBusy)
        return "worker";
    return nullptr;
}

// ---- probes -----------------------------------------------------------------

// AU check only: same call as console `--update check` (command_functions.cpp, "check").
Rc au_start()
{
    FwNetStatus st;
    fwNetGetStatus(st);
    if (st.state == FWS_BUSY)
        return RC_ERR;
    return fwNetStart(FWJ_CHECK, (uint8_t)meshcom_settings.node_updchan) ? RC_OK : RC_ERR;
}
int au_poll()
{
    FwNetStatus st;
    fwNetGetStatus(st);
    if (st.state == FWS_BUSY)
        return 0;
    return st.state == FWS_DONE_OK ? 1 : 2;
}

// dns and tls run in a worker task: hostByName (up to ~15 s), connect and the TLS handshake block, and the
// loop task is subscribed to the 5 s task watchdog. The worker stack is internal heap and is part of what is
// measured (begin() samples before xTaskCreate; poll waits PROBE_WORKER_GRACE_MS after the worker ended).
void workerMain(void *)
{
    bool ok = false;
    if (s_wMode == 0)
    {
        IPAddress ip;
        ok = WiFi.hostByName(PROBE_HOST, ip) == 1;
    }
    else
    {
        // The WiFiClientSecure constructor does `new sslclient_context`; this build has no C++ exceptions, so a
        // failed new aborts the node. Refuse instead when no block of PROBE_TLS_MIN_BLK is left.
        if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < PROBE_TLS_MIN_BLK)
        {
            ok = false;
            s_wThrew = true;   /* reported as threw;1 = refused for low heap */
        }
        else
        {
            WiFiClientSecure c;
            c.setInsecure();    // bare handshake, certificate not verified
            c.setTimeout(4);    // seconds
            c.setHandshakeTimeout(4); // seconds (default 120)
            ok = c.connect(PROBE_HOST, 443, 4000) != 0; // ms; no HTTP request is sent
            c.stop();
        }
    }
    s_wOk = ok;
    s_wHwm = (uint32_t)uxTaskGetStackHighWaterMark(nullptr);   /* bytes left at the deepest point */
    s_wDone = true;
    s_wBusy = false;
    vTaskDelete(nullptr); // the idle task on core 0 frees stack and TCB
}

Rc worker_start(int mode)
{
    if (!wifiUp() || s_wBusy)
        return RC_ERR;
    s_wMode = mode;
    s_wOk = false;
    s_wThrew = false;
    s_wHwm = 0;
    s_wDone = false;
    s_wGraceT = 0;
    s_wBusy = true;
    // core 0 (WiFi/lwIP side): its idle task reclaims the deleted worker; the loop task runs on core 1
    if (xTaskCreatePinnedToCore(workerMain, "instr_probe", PROBE_WORKER_STACK, nullptr, 1, nullptr, 0) != pdPASS)
    {
        s_wBusy = false;
        return RC_ERR;
    }
    printfdeb("[INSTR-PROBE];note;worker_stack_bytes;%u;counted_in_peak_used\n", (unsigned)PROBE_WORKER_STACK);
    return RC_OK;
}
int worker_poll()
{
    if (!s_wDone)
        return 0;
    if (s_wGraceT == 0)
        s_wGraceT = millis() | 1;
    if ((int32_t)(millis() - s_wGraceT) < (int32_t)PROBE_WORKER_GRACE_MS)
        return 0;
    printfdeb("[INSTR-PROBE];note;worker_stack_left_min;%u;lowheap_refused;%d\n", (unsigned)s_wHwm, (int)s_wThrew);
    return s_wOk ? 1 : 2;
}
Rc dns_start() { return worker_start(0); }
Rc tls_start() { return worker_start(1); }

// WiFi scan, async (WiFi.scanNetworks(true)), finished when scanComplete() reports a result.
Rc scan_start()
{
    // only when STA is already enabled: scanNetworks() would otherwise enableSTA() (WiFi init, config writes)
    if (!(WiFi.getMode() & WIFI_MODE_STA))
        return RC_ERR;
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING)
        return RC_ERR; // the normal scan path owns the radio right now
    return WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING ? RC_OK : RC_ERR;
}
int scan_poll()
{
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING)
        return 0;
    return n >= 0 ? 1 : 2;
}
void scan_cleanup()
{
    WiFi.scanDelete();
}

// Web page renderers into a discard sink: the global web_client is not connected, so
// CommonWebClient::write() aborts the transfer at the first flush (fd < 0) and drops the rest,
// while the page builder still runs completely. txEnd() releases the 1.4 kB send buffer.
template <void (*Fn)(void)> Rc page_run()
{
    if (web_client.connected())
        return RC_ERR; // command came from the web GUI: web_client is that live connection, do not write into it
    Fn();
    web_client.txEnd();
    return RC_OK;
}

#define PAGE_PROBE(id, fn, d) {id, "web page render into discard sink: " d, K_SYNC, page_run<fn>, nullptr, nullptr, 0}

const Probe kProbes[] = {
    {"au_check", "AU check only (fwNetStart FWJ_CHECK, as --update check): GitHub release query over TLS", K_ASYNC,
     au_start, au_poll, nullptr, 90000},
    {"dns", "DNS lookup of the AU host (WiFi.hostByName) in a worker task", K_ASYNC, dns_start, worker_poll, nullptr, 20000},
    {"tls", "bare TLS connect+handshake+close to the AU host, no request, in a worker task", K_ASYNC, tls_start, worker_poll,
     nullptr, 15000},
    {"wifi_scan", "async WiFi scan (WiFi.scanNetworks(true))", K_ASYNC, scan_start, scan_poll, scan_cleanup, 20000},
    PAGE_PROBE("page_info", sub_page_info, "info"),
    PAGE_PROBE("page_setup", sub_page_setup, "setup"),
    PAGE_PROBE("page_messages", sub_page_messages, "messages"),
    PAGE_PROBE("page_rxlog", sub_page_rxlog, "rxlog"),
    PAGE_PROBE("page_mheard", sub_page_mheard, "mheard"),
    PAGE_PROBE("page_path", sub_page_path, "path"),
    PAGE_PROBE("page_neighbours", sub_page_neighbours, "neighbours"),
    PAGE_PROBE("page_position", sub_page_position, "position"),
    PAGE_PROBE("page_wx", sub_page_wx, "wx"),
#if defined(ENABLE_MSGSTORE)
    PAGE_PROBE("page_mailbox", sub_page_mailbox, "mailbox"),
#endif
};
const int kN = (int)(sizeof(kProbes) / sizeof(kProbes[0]));

// ---- runner -----------------------------------------------------------------

int findProbe(const char *id)
{
    for (int i = 0; i < kN; i++)
        if (strcmp(kProbes[i].id, id) == 0)
            return i;
    return -1;
}

void finish(Rc rc)
{
    const Probe &p = kProbes[s_cur];
    if (p.cleanup)
        p.cleanup();
    uint32_t dt = millis() - s_t0;
    uint32_t endFree = ifree();
    uint32_t low = s_lowSampled; // tick-sampled floor
    uint32_t l = instrument_heap_lowmark_get();
    if (l != LOWMARK_UNKNOWN && l < low)
        low = l; // heapwatch low mark; unknown -> the sampled floor stands alone
    if (endFree < low)
        low = endFree;
    uint32_t peak = s_startFree > low ? s_startFree - low : 0;
    printfdeb("[INSTR-PROBE];name;%s;phase;end;ifree;%u;iblk;%u;low;%u;peak_used;%u;delta;%d;dt_ms;%u;rc;%s\n", p.id,
                  (unsigned)endFree, (unsigned)iblk(), (unsigned)low, (unsigned)peak, (int)((int32_t)s_startFree - (int32_t)endFree),
                  (unsigned)dt, rc == RC_OK ? "ok" : (rc == RC_TIMEOUT ? "timeout" : "err"));
    s_cur = -1;
    if (s_all)
    {
        s_settling = true;
        s_settleUntil = millis() + PROBE_SETTLE_MS;
    }
}

void begin(int idx)
{
    const Probe &p = kProbes[idx];
    s_cur = idx;
    instrument_heap_lowmark_reset();
    s_startFree = ifree();
    s_lowSampled = s_startFree;
    printfdeb("[INSTR-PROBE];name;%s;phase;start;ifree;%u;iblk;%u\n", p.id, (unsigned)s_startFree, (unsigned)iblk());
    s_t0 = millis();
    Rc rc = p.start();
    sampleLow();
    if (p.kind == K_SYNC || rc != RC_OK)
    {
        finish(rc);
        return;
    }
    s_deadline = s_t0 + p.timeoutMs;
}
} // namespace

void instrument_heapprobe(const char *argIn)
{
    // the console hook passes the argument with its leading space (msg_text+11): trim both ends
    char arg[24];
    const char *src = argIn ? argIn : "";
    while (*src == ' ' || *src == '\t')
        src++;
    size_t n = strnlen(src, sizeof(arg) - 1);
    memcpy(arg, src, n);
    arg[n] = '\0';
    while (n > 0 && (arg[n - 1] == ' ' || arg[n - 1] == '\t' || arg[n - 1] == '\r' || arg[n - 1] == '\n'))
        arg[--n] = '\0';

    if (*arg == '\0' || strcmp(arg, "list") == 0)
    {
        for (int i = 0; i < kN; i++)
            printfdeb("[INSTR-PROBE];list;%s;%s\n", kProbes[i].id, kProbes[i].desc);
        return;
    }
    if (s_cur >= 0 || s_all)
    {
        printfdeb("[INSTR-PROBE];busy\n");
        return;
    }
    if (strcmp(arg, "all") == 0)
    {
        s_all = true;
        s_next = 0;
        s_settling = false;
        s_allWaitSince = 0;
        return; // the tick starts the first probe (once AU and worker are idle)
    }
    int i = findProbe(arg);
    if (i < 0)
    {
        printfdeb("[INSTR-PROBE];unknown;%s\n", arg);
        return;
    }
    const char *hold = holdOff();
    if (hold)
    {
        printfdeb("[INSTR-PROBE];busy;%s\n", hold);
        return;
    }
    begin(i);
}

void instrument_probe_tick(void)
{
    if (s_cur >= 0)
    {
        sampleLow();
        const Probe &p = kProbes[s_cur];
        int r = p.poll();
        if (r != 0)
            finish(r == 1 ? RC_OK : RC_ERR);
        else if ((int32_t)(millis() - s_deadline) >= 0)
            finish(RC_TIMEOUT);
        return;
    }
    if (!s_all)
        return;
    if (s_settling && (int32_t)(millis() - s_settleUntil) < 0)
        return;
    // hold off while an AU job runs or a worker task lives (e.g. after an au_check/tls timeout)
    const char *hold = holdOff();
    if (hold)
    {
        if (s_allWaitSince == 0)
            s_allWaitSince = millis() | 1;
        if ((int32_t)(millis() - s_allWaitSince) >= (int32_t)PROBE_ALL_WAIT_MS)
        {
            s_all = false;
            s_settling = false;
            printfdeb("[INSTR-PROBE];all;aborted;busy;%s\n", hold);
        }
        return;
    }
    s_allWaitSince = 0;
    s_settling = false;
    if (s_next >= kN)
    {
        s_all = false;
        printfdeb("[INSTR-PROBE];all;done\n");
        return;
    }
    begin(s_next++);
}

#else // not an INSTRUMENT ESP32 build

void instrument_heapprobe(const char *arg)
{
    (void)arg;
#if INSTRUMENT_ENABLED
    printfdeb("[INSTR-PROBE];unavailable;esp32 only\n");
#endif
}
void instrument_probe_tick(void) {}

#endif
