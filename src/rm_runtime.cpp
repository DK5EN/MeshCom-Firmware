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
#include "hmac_sha256.h"
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

// --- RM-09: executed-command log and sent-command book ---------------------------------------
constexpr uint8_t kLogN = 5;
RmLogEntry s_log[kLogN];   // newest first
uint8_t s_nlog = 0;

constexpr uint8_t kSentN = 5;
struct SentSlot
{
    RmSent pub;
    uint8_t key[32];       // K of the TARGET; live only while keyLive
    bool keyLive;
};
SentSlot s_sent[kSentN];   // newest first
uint8_t s_nsent = 0;
uint32_t s_lastSent = 0;   // persisted ("rm_snd"), loaded at rmInit()

// hwm per managed node as learnt from verified replies (RAM only; the sync command refreshes it)
struct PeerHwm
{
    char dst[10];
    uint32_t hwm;
    bool used;
};
PeerHwm s_peer[4];
uint8_t s_peerNext = 0;
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
#if defined(REMOTE_LED_PIN)
        const size_t used = strlen(res);
        if (used < n)
            snprintf(res + used, n - used, " led=%d", bRemoteLed ? 1 : 0);
#endif
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

    if (strcmp(cmd, "led") == 0)
    {
#if defined(REMOTE_LED_PIN)
        // a = "on" | "off" (allowlisted). Held until "led off" or the next boot, no timer.
        const bool want = (strcmp(a, "on") == 0);
        bRemoteLed = want;
        pinMode(REMOTE_LED_PIN, OUTPUT);
        digitalWrite(REMOTE_LED_PIN, want ? HIGH : LOW);
        snprintf(res, n, "ok led=%s", want ? "on" : "off");
        return true;
#else
        snprintf(res, n, "err unsupported");
        return false;
#endif
    }

    snprintf(res, n, "err blocked"); // unreachable: rmCheck() only passes the table above
    return false;
}

// --- RM-09 helpers ----------------------------------------------------------------------------
void wipeKey(SentSlot &e)
{
    hmac_sha256_detail::wipe(e.key, sizeof(e.key));
    e.keyLive = false;
}

// rmqReplyWanted(): the receive hook queues replies only while a verifiable reply can still arrive.
void refreshReplyWanted()
{
    bool want = false;
    for (uint8_t i = 0; i < s_nsent; i++)
        if (s_sent[i].keyLive)
            want = true;
    rmqReplyWanted() = want;
}

// keys older than RM_CACHE_MS (the target's lost-reply window) are of no use any more
void expireKeys(uint32_t now)
{
    for (uint8_t i = 0; i < s_nsent; i++)
        if (s_sent[i].keyLive && (uint32_t)(now - s_sent[i].pub.sentMs) > RM_CACHE_MS)
            wipeKey(s_sent[i]);
    refreshReplyWanted();
}

void logExecuted(const char *src, const RmCmd &c, const char *result, uint32_t now)
{
    memmove(&s_log[1], &s_log[0], sizeof(RmLogEntry) * (kLogN - 1));
    RmLogEntry &e = s_log[0];
    memset(&e, 0, sizeof(e));
    e.ms = now;
    snprintf(e.src, sizeof(e.src), "%s", src);
    e.ctr = c.ctr;
    if (c.args[0] != '\0')
        snprintf(e.cmd, sizeof(e.cmd), "%s %s", c.cmd, c.args);
    else
        snprintf(e.cmd, sizeof(e.cmd), "%s", c.cmd);
    snprintf(e.result, sizeof(e.result), "%s", result);
    if (s_nlog < kLogN)
        s_nlog++;
}

void peerSet(const char *dst, uint32_t hwm)
{
    for (uint8_t i = 0; i < 4; i++)
        if (s_peer[i].used && strcmp(s_peer[i].dst, dst) == 0)
        {
            if (hwm > s_peer[i].hwm)
                s_peer[i].hwm = hwm;
            return;
        }
    PeerHwm &p = s_peer[s_peerNext];
    s_peerNext = (uint8_t)((s_peerNext + 1) % 4);
    snprintf(p.dst, sizeof(p.dst), "%s", dst);
    p.hwm = hwm;
    p.used = true;
}

bool peerGet(const char *dst, uint32_t &hwm)
{
    for (uint8_t i = 0; i < 4; i++)
        if (s_peer[i].used && strcmp(s_peer[i].dst, dst) == 0)
        {
            hwm = s_peer[i].hwm;
            return true;
        }
    return false;
}

// true when the node clock is trustworthy (NTP, RTC or GPS fix) and plausible (>= 2024-01-01)
bool clockUnix(uint32_t &out)
{
    if (!(bNTPDateTimeValid || bRTCON || posinfo_fix) || meshcom_settings.node_date_year < 2024)
        return false;
    const uint32_t t = (uint32_t)getUnixClock();
    if (t < 1704067200u)
        return false;
    out = t;
    return true;
}

// ctr of a leading "ctr=<n>" inside a sync result "ok ctr=<n> v=..."
bool parseSyncCtr(const char *result, uint32_t &out)
{
    if (strncmp(result, "ok ctr=", 7) != 0)
        return false;
    uint64_t v = 0;
    const char *p = result + 7;
    if (*p < '0' || *p > '9')
        return false;
    while (*p >= '0' && *p <= '9')
    {
        v = v * 10 + (uint64_t)(*p - '0');
        if (v > 0xFFFFFFFFULL)
            return false;
        p++;
    }
    out = (uint32_t)v;
    return true;
}

// Loop task: matches one queued reply against the pending sent commands.
void handleReply(const char *src, const char *text)
{
    // ctr of the reply ("RM1 <ctr> ok|err ...", rmIsReply() was true in the receive hook)
    uint64_t ctr = 0;
    const char *p = text + 4;
    while (*p >= '0' && *p <= '9')
    {
        ctr = ctr * 10 + (uint64_t)(*p - '0');
        if (ctr > 0xFFFFFFFFULL)
            return;
        p++;
    }

    for (uint8_t i = 0; i < s_nsent; i++)
    {
        SentSlot &e = s_sent[i];
        if (!e.keyLive || e.pub.verified || e.pub.ctr != (uint32_t)ctr || strcmp(e.pub.dst, src) != 0)
            continue;

        char result[RM_MAX_RESULT + 1];
        const bool ok =
            rmVerifyReply(text, e.pub.dst, meshcom_settings.node_call, e.pub.ctr, e.key, result, sizeof(result));
        e.pub.replied = true;
        e.pub.verified = ok;
        if (ok)
        {
            snprintf(e.pub.reply, sizeof(e.pub.reply), "%s", result);
            wipeKey(e);
            uint32_t h = 0;
            if (e.pub.ctr == 0 && parseSyncCtr(result, h))
                peerSet(e.pub.dst, h);
            else if (e.pub.ctr != 0)
                peerSet(e.pub.dst, e.pub.ctr); // the target accepted this ctr (ok and err alike)
        }
        else // keep the key: an authentic reply may still follow and then overwrites this one
            snprintf(e.pub.reply, sizeof(e.pub.reply), "%.*s", (int)(sizeof(e.pub.reply) - 1), text);
        Serial.printf("[RM];reply;%s;ctr;%lu;verified;%d\n", e.pub.dst, (unsigned long)e.pub.ctr, ok ? 1 : 0);
        refreshReplyWanted();
        return;
    }
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
    s_lastSent = rmSndLoad();
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

    // RM-09: replies to commands we sent (one per pass), then key expiry
    if (s_nsent > 0)
    {
        if (rmReplyPop(src, sizeof(src), text, sizeof(text)))
            handleReply(src, text);
        expireKeys(millis());
    }
    else
    {
        // nothing pending: drop a stray reply that slipped in before the flag was cleared
        while (rmReplyPop(src, sizeof(src), text, sizeof(text)))
        {
        }
    }

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

        logExecuted(src, cmd, result, millis());
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

// ---- RM-09 ----------------------------------------------------------------------------------------

void rmGetStatus(RmStatus &out)
{
    if (!s_inited)
        rmInit();
    memset(&out, 0, sizeof(out));
    out.on = (meshcom_settings.node_rm == 1);
    out.passwdSet = !passwdEmpty();
    const uint32_t now = millis();
    if (s_state.lockActive && (int32_t)(now - s_state.lockUntilMs) < 0)
    {
        out.lockActive = true;
        out.lockRemainS = ((uint32_t)(s_state.lockUntilMs - now) + 999u) / 1000u;
    }
    out.hwm = s_state.hwm;
    out.stats = g_rmStats;
    out.nlog = s_nlog;
    for (uint8_t i = 0; i < s_nlog; i++)
        out.log[i] = s_log[i];
}

namespace
{
void setErr(char *err, size_t errN, const char *why)
{
    if (err != nullptr && errN > 0)
        snprintf(err, errN, "%s", why);
}
} // namespace

bool rmSendCommand(const char *dst, const char *passwd, const char *cmd, const char *args, char *err,
                   size_t errN, uint32_t *ctrOut)
{
    if (!s_inited)
        rmInit();
    if (args == nullptr)
        args = "";

    // dst: own copy, folded to upper case, a call incl. SSID (9 chars at most), never our own
    char to[10];
    size_t dl = 0;
    if (dst != nullptr)
        for (; dst[dl] != '\0' && dl < sizeof(to) - 1; dl++)
        {
            char ch = dst[dl];
            if (ch >= 'a' && ch <= 'z')
                ch = (char)(ch - 'a' + 'A');
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-'))
                break;
            to[dl] = ch;
        }
    if (dst == nullptr || dl == 0 || dst[dl] != '\0')
    {
        setErr(err, errN, "dst");
        return false;
    }
    to[dl] = '\0';
    if (strcmp(to, meshcom_settings.node_call) == 0)
    {
        setErr(err, errN, "dst");
        return false;
    }

    // password: 1..14 characters after stripping trailing spaces (node_passwd is char[15])
    size_t pl = (passwd != nullptr) ? strlen(passwd) : 0;
    while (pl > 0 && passwd[pl - 1] == ' ')
        pl--;
    if (pl == 0 || (passwd != nullptr && strlen(passwd) > sizeof(meshcom_settings.node_passwd) - 1))
    {
        setErr(err, errN, "passwd");
        return false;
    }

    if (cmd == nullptr || !rmCommandAllowed(cmd, args, TX_POWER_MAX))
    {
        setErr(err, errN, "cmd");
        return false;
    }
    const bool isSync = (strcmp(cmd, "sync") == 0);

    // the target rate-limits to one accepted command per RM_RATE_MS: do not waste airtime on a refusal
    const uint32_t now = millis();
    for (uint8_t i = 0; i < s_nsent; i++)
        if (strcmp(s_sent[i].pub.dst, to) == 0 && (uint32_t)(now - s_sent[i].pub.sentMs) < RM_RATE_MS)
        {
            setErr(err, errN, "busy");
            return false;
        }

    // counter: max(last sent + 1, unix time, known hwm of the target + 1); sync always 0
    uint32_t ctr = 0;
    if (!isSync)
    {
        if (s_lastSent == 0xFFFFFFFFu)
        {
            setErr(err, errN, "ctr");
            return false;
        }
        ctr = s_lastSent + 1;
        uint32_t t = 0;
        if (clockUnix(t) && t > ctr)
            ctr = t;
        uint32_t ph = 0;
        if (peerGet(to, ph))
        {
            if (ph == 0xFFFFFFFFu)
            {
                setErr(err, errN, "ctr");
                return false;
            }
            if (ph + 1 > ctr)
                ctr = ph + 1;
        }
    }

    uint8_t key[32];
    rmDeriveKey(passwd, key);

    static char wire[96];
    static char out[128];
    size_t n = rmBuildCommand(to, meshcom_settings.node_call, ctr, cmd, args, key, wire, sizeof(wire));
    if (n == 0)
    {
        hmac_sha256_detail::wipe(key, sizeof(key));
        setErr(err, errN, "cmd");
        return false;
    }

    // the counter reaches flash BEFORE the frame leaves: a reboot must never re-use it
    if (!isSync && !rmSndSave(ctr))
    {
        hmac_sha256_detail::wipe(key, sizeof(key));
        Serial.printf("[RM];snd_save;failed\n");
        setErr(err, errN, "store");
        return false;
    }
    if (!isSync)
        s_lastSent = ctr;

    // DM form as sendReply() builds it: ":{CALL}text"
    snprintf(out, sizeof(out), ":{%s}%s", to, wire);
    const int rc = sendMessage(out, (int)strlen(out));
    memset(wire, 0, sizeof(wire)); // the tag is not needed any more
    memset(out, 0, sizeof(out));
    if (rc != BP_SEND_OK)
    {
        hmac_sha256_detail::wipe(key, sizeof(key));
        Serial.printf("[RM];send_failed;%d\n", rc);
        setErr(err, errN, "send");
        return false;
    }

    // book it (newest first); the oldest entry drops out, its key with it
    if (s_nsent == kSentN)
        wipeKey(s_sent[kSentN - 1]);
    else
        s_nsent++;
    memmove(&s_sent[1], &s_sent[0], sizeof(SentSlot) * (kSentN - 1));
    SentSlot &e = s_sent[0];
    memset(&e, 0, sizeof(e));
    snprintf(e.pub.dst, sizeof(e.pub.dst), "%s", to);
    e.pub.ctr = ctr;
    if (args[0] != '\0')
        snprintf(e.pub.cmd, sizeof(e.pub.cmd), "%s %s", cmd, args);
    else
        snprintf(e.pub.cmd, sizeof(e.pub.cmd), "%s", cmd);
    e.pub.sentMs = now;
    memcpy(e.key, key, sizeof(e.key));
    e.keyLive = true;
    hmac_sha256_detail::wipe(key, sizeof(key));
    refreshReplyWanted();

    Serial.printf("[RM];send;%s;ctr;%lu\n", to, (unsigned long)ctr);
    if (ctrOut != nullptr)
        *ctrOut = ctr;
    setErr(err, errN, "");
    return true;
}

uint8_t rmGetSent(RmSent *out, uint8_t max)
{
    if (out == nullptr)
        return 0;
    uint8_t n = (s_nsent < max) ? s_nsent : max;
    for (uint8_t i = 0; i < n; i++)
        out[i] = s_sent[i].pub;
    return n;
}
