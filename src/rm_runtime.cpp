// rm_runtime.cpp -- see rm_runtime.h. Loop task only.
#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "configuration.h"
#include "backpressure.h"
#include "command_functions.h"
#include "counters_store.h"
#include "loop_functions.h"
#include "loop_functions_extern.h"
#include "remote_cmd.h"
#include "rm_queue.h"
#include "rm_runtime.h"

#if defined(NRF52_SERIES)
extern uint32_t nrf52_getFreeHeap(void); // nrf52_main.cpp
#endif

RmStats g_rmStats = {};

namespace
{
// The reply must reach the TX ring and get its slot on air before the node goes down.
constexpr uint32_t kRebootDelayMs = 8000;

RmState s_state;
bool s_inited = false;
bool s_rebootPending = false;
uint32_t s_rebootAtMs = 0;

// --- state probes for the on/off table -------------------------------------------------------
bool stGps() { return bGPSON; }
bool stTrack() { return bDisplayTrack; }
bool stDisplay() { return !bDisplayOff; }
bool stGateway() { return bGATEWAY; }
bool stMesh() { return bMESH; }

struct RmToggle
{
    const char *name;    // remote form: "<name> on|off"
    const char *consOn;  // the ONE console string that is ever handed to commandAction()
    const char *consOff;
    bool (*state)(void); // runtime flag the console command sets
};

const RmToggle kToggles[] = {
    {"gps", "--gps on", "--gps off", stGps},
    {"track", "--track on", "--track off", stTrack},
    {"display", "--display on", "--display off", stDisplay},
    {"gateway", "--gateway on", "--gateway off", stGateway},
    {"mesh", "--mesh on", "--mesh off", stMesh},
};

// commandAction() takes a writable char*; the table literal is copied, never the received text.
void runConsole(const char *literal)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%s", literal);
    commandAction(buf, false);
}

uint32_t freeHeapKb()
{
#if defined(NRF52_SERIES)
    return nrf52_getFreeHeap() / 1024u;
#elif defined(ESP32)
    return (uint32_t)ESP.getFreeHeap() / 1024u;
#else
    return 0;
#endif
}

bool passwdEmpty()
{
    // node_passwd is char[15], space padded; the core strips trailing spaces, so "all spaces" is empty
    for (size_t i = 0; i < sizeof(meshcom_settings.node_passwd); i++)
    {
        char ch = meshcom_settings.node_passwd[i];
        if (ch == '\0')
            break;
        if (ch != ' ')
            return false;
    }
    return true;
}

void sendReply(const RmCmd &c, const char *result, const char *src)
{
    static char wire[128];
    static char out[160];

    size_t n = rmReply(c, result, meshcom_settings.node_call, src, meshcom_settings.node_passwd, wire, sizeof(wire));
    if (n == 0)
    {
        Serial.printf("[RM];reply;build_failed\n");
        return;
    }
    // DM form as the KISS inject path builds it (kiss_functions.cpp): ":{CALL}text"
    snprintf(out, sizeof(out), ":{%s}%s", src, wire);
    int rc = sendMessage(out, (int)strlen(out));
    if (rc != BP_SEND_OK)
        Serial.printf("[RM];reply;send_failed;%d\n", rc);
}

// Executes an RM_OK command through the fixed table. Fills result ("ok ..." / "err ...") and
// returns true when the command took effect. reboot only sets *reboot, the loop does the rest.
bool execute(const RmCmd &c, char *res, size_t n, bool *reboot)
{
    const char *cmd = c.cmd;
    const char *a = c.args;

    if (strcmp(cmd, "reboot") == 0)
    {
        *reboot = true;
        snprintf(res, n, "ok rebooting");
        return true;
    }

    if (strcmp(cmd, "status") == 0)
    {
        snprintf(res, n, "ok v=%s%s up=%lu bat=%d heap=%lu gw=%d mesh=%d", SOURCE_VERSION, SOURCE_VERSION_SUB,
                 (unsigned long)(millis() / 60000UL), (int)global_proz, (unsigned long)freeHeapKb(),
                 bGATEWAY ? 1 : 0, bMESH ? 1 : 0);
        return true;
    }

    if (strcmp(cmd, "sendpos") == 0 || strcmp(cmd, "sendtrack") == 0)
    {
        runConsole(strcmp(cmd, "sendpos") == 0 ? "--sendpos" : "--sendtrack");
        snprintf(res, n, "ok sent");
        return true;
    }

    for (size_t i = 0; i < sizeof(kToggles) / sizeof(kToggles[0]); i++)
    {
        const RmToggle &t = kToggles[i];
        if (strcmp(cmd, t.name) != 0)
            continue;
        const bool want = (strcmp(a, "on") == 0);
        runConsole(want ? t.consOn : t.consOff);
        if (t.state() != want)
        {
            snprintf(res, n, "err failed");
            return false;
        }
        snprintf(res, n, "ok %s=%s", t.name, want ? "on" : "off");
        return true;
    }

    if (strcmp(cmd, "txpower") == 0)
    {
        const int v = atoi(a); // rmCheck() allowlisted: 1..3 digits, 0 <= v <= TX_POWER_MAX
        char line[24];
        snprintf(line, sizeof(line), "--txpower %d", v);
        runConsole(line);
        if (meshcom_settings.node_power != v)
        {
            snprintf(res, n, "err failed");
            return false;
        }
        snprintf(res, n, "ok txpower=%d", v);
        return true;
    }

    if (strcmp(cmd, "setout") == 0)
    {
        // a = "<a|b><0-7> <on|off>" (allowlisted). --setout only acts on pins set to OUTPUT.
        const int pin = (a[0] == 'b' ? 8 : 0) + (a[1] - '0');
        const int mask = 0x0001 << pin;
        const bool want = (strcmp(a + 3, "on") == 0);
        if ((meshcom_settings.node_mcp17io & mask) == 0)
        {
            snprintf(res, n, "err not output");
            return false;
        }
        char line[24];
        snprintf(line, sizeof(line), "--setout %c%c %s", a[0], a[1], want ? "on" : "off");
        runConsole(line);
        if (((meshcom_settings.node_mcp17out & mask) != 0) != want)
        {
            snprintf(res, n, "err failed");
            return false;
        }
        snprintf(res, n, "ok %c%c=%s", a[0], a[1], want ? "on" : "off");
        return true;
    }

    snprintf(res, n, "err blocked"); // unreachable: rmCheck() only passes the table above
    return false;
}

void countReject(RmVerdict v)
{
    switch (v)
    {
    case RM_REJ_FORMAT:   g_rmStats.rej_format++; break;
    case RM_REJ_TAG:      g_rmStats.rej_tag++; break;
    case RM_REJ_REPLAY:   g_rmStats.rej_replay++; break;
    case RM_REJ_BLOCKED:  g_rmStats.rej_blocked++; break;
    case RM_REJ_RATE:     g_rmStats.rej_rate++; break;
    case RM_REJ_LOCKOUT:  g_rmStats.rej_lockout++; break;
    case RM_REJ_DISABLED: g_rmStats.rej_disabled++; break;
    default: break;
    }
}
} // namespace

void rmInit(void)
{
    rmStateInit(s_state, rmHwmLoad());
    s_inited = true;
    Serial.printf("[RM];init;hwm;%lu\n", (unsigned long)s_state.hwm);
}

void rmDrain(void)
{
    if (!s_inited)
        rmInit();

    // Deferred reboot first: the reply has had kRebootDelayMs to leave through the TX ring.
    if (s_rebootPending && (int32_t)(millis() - s_rebootAtMs) >= 0)
    {
        s_rebootPending = false;
        Serial.printf("[RM];reboot\n");
        runConsole("--reboot now"); // the --reboot path: net console stop, delay, ESP.restart()/NVIC_SystemReset()
        return;
    }

    static char src[RM_QUEUE_SRC_LEN];
    static char text[RM_QUEUE_TEXT_LEN];
    if (!rmQueuePop(src, sizeof(src), text, sizeof(text)))
        return;

    // Off or no key: RM1 DMs are not ours. Dropped without a marker (ordinary text for everybody else).
    if (meshcom_settings.node_rm != 1 || passwdEmpty())
    {
        g_rmStats.rej_disabled++;
        return;
    }

    static RmCmd cmd;
    if (!rmParse(text, cmd))
    {
        g_rmStats.rej_format++;
        Serial.printf("[RM];reject;format\n");
        return;
    }

    const uint32_t now = millis();
    const RmVerdict v = rmCheck(s_state, cmd, meshcom_settings.node_call, src, meshcom_settings.node_passwd,
                                TX_POWER_MAX, now);

    switch (v)
    {
    case RM_OK:
    {
        g_rmStats.ok++;

        // The mark reaches flash BEFORE the command runs: a command that crashes or reboots the node
        // must not stay replayable. RM_OK guarantees cmd.ctr > hwm.
        bool saved = rmHwmSave(cmd.ctr);

        char result[RM_MAX_RESULT + 1];
        bool reboot = false;
        bool done = false;
        if (saved)
            done = execute(cmd, result, sizeof(result), &reboot);
        else
            snprintf(result, sizeof(result), "err storage");   // fail closed: no mark, no execution

        // CONTRACT (remote_cmd.h): rmAccept() for EVERY RM_OK, a failed execution included.
        const uint32_t hwm = rmAccept(s_state, cmd, result, millis());
        if (!saved || hwm != cmd.ctr)
        {
            if (!rmHwmSave(hwm))
                Serial.printf("[RM];hwm_save;failed\n");
        }

        Serial.printf("[RM];%s;ctr;%lu\n", done ? "ok" : "fail", (unsigned long)cmd.ctr);
        sendReply(cmd, result, src);

        if (reboot)
        {
            s_rebootPending = true;
            s_rebootAtMs = millis() + kRebootDelayMs;
        }
        break;
    }
    case RM_CACHED:
    {
        // Advisor RM W2 #2: a sniffed valid frame re-injected inside the 10 min
        // cache window must not turn the node into a reply amplifier -- at most
        // one cached reply per RM_RATE_MS.
        static uint32_t s_lastCachedMs = 0;
        static bool s_haveCached = false;
        const uint32_t nowCached = millis();
        g_rmStats.cached++;
        if(s_haveCached && (uint32_t)(nowCached - s_lastCachedMs) < RM_RATE_MS)
        {
            Serial.printf("[RM];cached;ctr;%lu;suppressed\n", (unsigned long)cmd.ctr);
            break;
        }
        s_haveCached = true;
        s_lastCachedMs = nowCached;
        Serial.printf("[RM];cached;ctr;%lu\n", (unsigned long)cmd.ctr);
        sendReply(cmd, s_state.lastReply, src); // lost-reply recovery: same result, nothing executed
        break;
    }
    case RM_SYNC:
    {
        g_rmStats.sync++;
        char result[RM_MAX_RESULT + 1];
        snprintf(result, sizeof(result), "ok ctr=%lu v=%s%s", (unsigned long)s_state.hwm, SOURCE_VERSION,
                 SOURCE_VERSION_SUB);
        Serial.printf("[RM];sync;ctr;%lu\n", (unsigned long)s_state.hwm);
        sendReply(cmd, result, src);
        break;
    }
    default: // every reject: silent on the air, counter and marker only
        countReject(v);
        Serial.printf("[RM];reject;%s\n", rmVerdictName(v));
        break;
    }
}
