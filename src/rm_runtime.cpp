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
#include "rm_sender_policy.h"
#include "rm_validate.h"

static_assert(RM_POLICY_COOLDOWN_MS == RM_RATE_MS, "sender spacing must equal the target's rate limit");
static_assert(RM_POLICY_WINDOW_MS >= RM_REJ_WINDOW_MS + 60000u, "the sender budget window must exceed the target's reject window");

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

constexpr uint8_t kSentN = RM_POLICY_MAX_ENTRIES;   // 12 slots; an entry that still counts toward a budget is never evicted
struct SentSlot
{
    RmSent pub;
    uint8_t key[32];       // K of the TARGET; live only while keyLive
    bool keyLive;
    uint32_t verifiedMs;   // millis() when the verified reply was booked
    bool expired;          // older than RM_CACHE_MS: aged out of the sender policy (guards the millis() wrap)
};
SentSlot s_sent[kSentN];   // newest first
uint8_t s_nsent = 0;
uint32_t s_lastSent = 0;   // persisted ("rm_snd"), loaded at rmInit()

// One command waiting behind an automatic sync (sender without a trusted clock and without a learnt
// counter mark for the target). It holds the target's key like a sent entry does and is wiped on every exit.
struct PendingCmd
{
    bool used;
    char dst[10];
    uint8_t key[32];
    char cmd[16];
    char args[RM_MAX_ARGS + 1];
    uint32_t syncMs;       // sentMs of the sync entry this command waits for
};
PendingCmd s_pend;
RmProof s_proof[RM_PROOF_N]; // per-target key proof, RAM only (rm_sender_policy.h)

// Why the last chained command was not sent (one slot, cleared by the next accepted send to that
// target); rmGetTargets() reports it so the GUI can show a sentence instead of silence.
struct ChainErr
{
    char dst[10];
    const char *tok;       // static token of the table (rm_sender_policy.h), nullptr = none
};
ChainErr s_chainErr;

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
    char buf[64];
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
    static char wire[144]; // "RM1 <10> <108> <16>" = 140 + NUL
    static char out[160];
    static_assert(sizeof(wire) >= 4 + 10 + 1 + RM_MAX_RESULT + 1 + 16 + 1, "wire holds the longest reply");
    static_assert(sizeof(out) >= 2 + 9 + 1 + sizeof(wire), "out wraps wire with :{CALL}");

    // the single send-side choke point: whatever a caller passes goes out sanitised (and clamped)
    char clean[RM_MAX_RESULT + 1];
    snprintf(clean, sizeof(clean), "%s", result);
    rmSanitizeResult(clean);
    size_t n = rmReply(c, clean, meshcom_settings.node_call, src, meshcom_settings.node_passwd, wire, sizeof(wire));
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
        // s=<letters> p=<cur>/<max> replace gw= and mesh=; led= stays as the capability flag of older
        // consumers. Worst case 61 of 63 characters, see rm_sender_policy.h and its native test.
        char ver[12];
        snprintf(ver, sizeof(ver), "%s%s", SOURCE_VERSION, SOURCE_VERSION_SUB);
        RmSwitches sw = {};
        sw.gps = stGps();
        sw.track = stTrack();
        sw.display = stDisplay();
        sw.mesh = stMesh();
        sw.gateway = stGateway();
#if defined(REMOTE_LED_PIN)
        sw.ledSupported = true;
        sw.led = bRemoteLed;
#endif
        rmFormatStatus(res, n, ver, (uint32_t)(millis() / 60000UL), (int)global_proz, freeHeapKb(), sw,
                       (int)meshcom_settings.node_power, (int)TX_POWER_MAX);
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
// short fingerprint of a key: first 4 bytes of SHA-256("RMFP" + key); the key itself is never stored
void keyFp(const uint8_t key[32], uint8_t fp[4])
{
    uint8_t buf[36];
    memcpy(buf, "RMFP", 4);
    memcpy(buf + 4, key, 32);
    uint8_t h[32];
    sha256(buf, sizeof(buf), h);
    memcpy(fp, h, 4);
    hmac_sha256_detail::wipe(buf, sizeof(buf));
    hmac_sha256_detail::wipe(h, sizeof(h));
}

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
        if ((uint32_t)(now - s_sent[i].pub.sentMs) > RM_CACHE_MS)
        {
            s_sent[i].expired = true; // sticks: ages the entry out of the sender policy even after a millis() wrap
            if (s_sent[i].keyLive)
                wipeKey(s_sent[i]);
        }
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
            e.verifiedMs = millis();
            uint8_t fp[4];
            keyFp(e.key, fp); // the key of the MATCHED entry, before wipeKey()
            rmProofVerified(s_proof, e.pub.dst, fp);
            if (strcmp(e.pub.cmd, "sync") == 0) // the sync reply advertises the receiver generation (rm=<n>)
            {
                int cap = rmCapLevel(result);
                if (cap < 0)
                    cap = 0;
                if (cap > 9)
                    cap = 9;
                rmProofSetCap(s_proof, e.pub.dst, fp, (uint8_t)cap);
            }
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

void pendingStep(uint32_t now); // below: runs the command queued behind an automatic sync
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

    pendingStep(millis()); // a command queued behind an automatic sync goes out once the sync is verified

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

        rmSanitizeResult(result); // before the cache: a replay sends exactly what was first sent
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
        snprintf(result, sizeof(result), "ok ctr=%lu v=%s%s rm=%d", (unsigned long)s_state.hwm,
                 SOURCE_VERSION, SOURCE_VERSION_SUB, RM_CAP_LEVEL);
        rmSanitizeResult(result);
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

void polEntryOf(const SentSlot &e, RmPolEntry &p)
{
    p.sentMs = e.pub.sentMs;
    p.replied = e.pub.replied;
    p.verified = e.pub.verified;
    p.replyErr = e.pub.verified && rmReplyIsErr(e.pub.reply);
    p.expired = e.expired;
}

// policy decision for one target (upper-case call), from the sent book
RmPolDecision policyFor(const char *to, uint32_t now, bool force = false)
{
    RmPolEntry pe[kSentN];
    uint8_t n = 0;
    for (uint8_t i = 0; i < s_nsent; i++)
        if (strcmp(s_sent[i].pub.dst, to) == 0)
            polEntryOf(s_sent[i], pe[n++]);
    const RmProof *pf = rmProofFind(s_proof, to);
    return rmPolicyMaySend(pe, n, now, rmProofLimit(pf), pf != nullptr && pf->oneShot, force);
}

void dropPending()
{
    hmac_sha256_detail::wipe(s_pend.key, sizeof(s_pend.key));
    memset(&s_pend, 0, sizeof(s_pend));
}

// slot to free for a new entry: -2 a slot is free, -1 none (all still count), else the index to drop
int bookVictim(uint32_t now)
{
    RmPolEntry pe[kSentN];
    for (uint8_t i = 0; i < s_nsent; i++)
        polEntryOf(s_sent[i], pe[i]);
    return rmBookVictim(pe, s_nsent, kSentN, now);
}

// slots a new entry can take now (free + no longer counting); a chain needs 2 before its sync leaves
uint8_t bookRoom(uint32_t now)
{
    RmPolEntry pe[kSentN];
    for (uint8_t i = 0; i < s_nsent; i++)
        polEntryOf(s_sent[i], pe[i]);
    return rmBookRoom(pe, s_nsent, kSentN, now);
}

// Builds, persists the counter (not for sync), sends and books one frame. key stays the caller's: a
// copy goes into the pending-reply entry (live for RM_CACHE_MS, wiped by wipeKey()).
bool bookAndSend(const char *to, const uint8_t key[32], uint32_t ctr, const char *cmd, const char *args,
                 uint32_t now, char *err, size_t errN)
{
    const bool isSync = (strcmp(cmd, "sync") == 0);

    // a slot must be free (or hold an entry that no longer counts) BEFORE a counter is consumed
    int victim = bookVictim(now);
    if (victim == -1)
    {
        setErr(err, errN, "busy");
        return false;
    }

    // wire: "RM1 <10> <15 cmd> <39 args> <16>" = 87 + NUL; out adds ":{<9>}" (12) = 99 + NUL
    static char wire[96];
    static char out[128];
    static_assert(sizeof(wire) >= 4 + 10 + 1 + 15 + 1 + RM_MAX_ARGS + 1 + 16 + 1, "wire holds the longest command");
    static_assert(sizeof(out) >= 2 + 9 + 1 + sizeof(wire) - 1, "out wraps wire with :{CALL}");
    size_t n = rmBuildCommand(to, meshcom_settings.node_call, ctr, cmd, args, key, wire, sizeof(wire));
    if (n == 0)
    {
        setErr(err, errN, "cmd");
        return false;
    }

    // the counter reaches flash BEFORE the frame leaves: a reboot must never re-use it
    if (!isSync && !rmSndSave(ctr))
    {
        memset(wire, 0, sizeof(wire));
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
        Serial.printf("[RM];send_failed;%d\n", rc);
        setErr(err, errN, "send");
        return false;
    }

    // book it (newest first); the oldest entry that no longer counts drops out, its key with it
    victim = bookVictim(now);
    if (victim >= 0)
    {
        wipeKey(s_sent[victim]);
        memmove(&s_sent[victim], &s_sent[victim + 1], sizeof(SentSlot) * (size_t)(s_nsent - 1 - victim));
        s_nsent--;
    }
    memmove(&s_sent[1], &s_sent[0], sizeof(SentSlot) * (size_t)(s_nsent));
    s_nsent++;
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
    refreshReplyWanted();
    if (s_chainErr.tok != nullptr && strcmp(s_chainErr.dst, to) == 0)
        s_chainErr.tok = nullptr; // an accepted send to that target ends the old chain error

    Serial.printf("[RM];send;%s;ctr;%lu\n", to, (unsigned long)ctr);
    return true;
}

// fromChain: the call comes from pendingStep() (the command that waited for its sync): no pending
// check, no second automatic sync.
bool sendKeyImpl(const char *dst, const uint8_t key[32], const char *cmd, const char *args, char *err,
                 size_t errN, uint32_t *ctrOut, bool *viaSync, bool fromChain, bool force = false)
{
    if (!s_inited)
        rmInit();
    if (viaSync != nullptr)
        *viaSync = false;
    if (args == nullptr)
        args = "";
    if (key == nullptr)
    {
        setErr(err, errN, "passwd");
        return false;
    }

    // dst: own copy folded to upper case, then ONE shared validator (rm_validate.h); never our own call
    char to[RM_CALL_MAX + 1];
    size_t dl = 0;
    if (dst != nullptr)
        for (; dst[dl] != '\0' && dl < RM_CALL_MAX; dl++)
        {
            char ch = dst[dl];
            if (ch >= 'a' && ch <= 'z')
                ch = (char)(ch - 'a' + 'A');
            to[dl] = ch;
        }
    if (dst == nullptr || dst[dl] != '\0')
    {
        setErr(err, errN, "dst");
        return false;
    }
    to[dl] = '\0';
    if (!rmValidateCall(to) || strcmp(to, meshcom_settings.node_call) == 0)
    {
        setErr(err, errN, "dst");
        return false;
    }

    if (cmd == nullptr || !rmCommandAllowed(cmd, args, TX_POWER_MAX) || strlen(cmd) >= sizeof(s_pend.cmd) ||
        strlen(args) >= sizeof(s_pend.args))
    {
        setErr(err, errN, "cmd");
        return false;
    }
    const bool isSync = (strcmp(cmd, "sync") == 0);
    const uint32_t now = millis();

    // a command is already queued behind this target's automatic sync: a second click must not stack
    if (!fromChain && s_pend.used && strcmp(s_pend.dst, to) == 0)
    {
        setErr(err, errN, "busy");
        return false;
    }

    // key proof: a changed fingerprint for a known target clears "proven" and arms the one-shot; a first
    // key arms nothing; the same key changes nothing
    uint8_t fp[4];
    keyFp(key, fp);
    RmProof *pf = rmProofSetKey(s_proof, to, fp);

    // sender policy BEFORE any counter is touched: spacing of 10 s, 120 s window, 2 unanswered sends
    // while the key is unproven (10 once a reply verified); force only spends the armed one-shot
    const RmPolDecision d = policyFor(to, now, force && !fromChain);
    if (!d.allowed)
    {
        setErr(err, errN, d.reason == RM_POL_LIMIT ? "limit" : "busy");
        return false;
    }

    uint32_t t = 0;
    const bool clock = clockUnix(t);
    uint32_t ph = 0;
    const bool mark = peerGet(to, ph);

    // automatic sync: only without a trusted clock AND without a learnt mark for this target
    if (!fromChain && rmPolicyNeedSync(clock, mark, isSync))
    {
        if (s_pend.used) // one chain at a time (one key, one command held)
        {
            setErr(err, errN, "busy");
            return false;
        }
        if (bookRoom(now) < 2) // sync AND the queued command each need a slot: refuse before the sync leaves
        {
            setErr(err, errN, "busy");
            return false;
        }
        if (!bookAndSend(to, key, 0, "sync", "", now, err, errN))
            return false;
        if (d.usedForce && pf != nullptr)
            pf->oneShot = false; // the forced send left: the one-shot is spent
        s_pend.used = true;
        snprintf(s_pend.dst, sizeof(s_pend.dst), "%s", to);
        memcpy(s_pend.key, key, sizeof(s_pend.key));
        snprintf(s_pend.cmd, sizeof(s_pend.cmd), "%s", cmd);
        snprintf(s_pend.args, sizeof(s_pend.args), "%s", args);
        s_pend.syncMs = now;
        Serial.printf("[RM];chain;%s;sync_first\n", to);
        if (viaSync != nullptr)
            *viaSync = true;
        if (ctrOut != nullptr)
            *ctrOut = 0;
        setErr(err, errN, "");
        return true;
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
        if (clock && t > ctr)
            ctr = t;
        if (mark)
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

    if (!bookAndSend(to, key, ctr, cmd, args, now, err, errN))
        return false;
    if (d.usedForce && pf != nullptr)
        pf->oneShot = false; // the forced send left: the one-shot is spent
    if (ctrOut != nullptr)
        *ctrOut = ctr;
    setErr(err, errN, "");
    return true;
}

void setChainErr(const char *dst, const char *tok)
{
    snprintf(s_chainErr.dst, sizeof(s_chainErr.dst), "%s", dst);
    s_chainErr.tok = tok;
}

void pendingStep(uint32_t now)
{
    if (!s_pend.used)
        return;
    int idx = -1;
    for (uint8_t i = 0; i < s_nsent; i++)
        if (s_sent[i].pub.ctr == 0 && s_sent[i].pub.sentMs == s_pend.syncMs && strcmp(s_sent[i].pub.dst, s_pend.dst) == 0)
        {
            idx = (int)i;
            break;
        }
    const bool gone = (idx < 0);
    const SentSlot *e = gone ? nullptr : &s_sent[idx];
    const RmPendAction act =
        rmPendingDecide(!gone && e->pub.verified, !gone && e->pub.replied, !gone && e->expired, gone,
                        (uint32_t)(now - s_pend.syncMs), gone ? 0u : (uint32_t)(now - e->verifiedMs));
    if (act == RM_PEND_WAIT)
        return;

    // SEND and DROP both end the chain: the held key is wiped on every path
    char dst[10];
    snprintf(dst, sizeof(dst), "%s", s_pend.dst);
    if (act == RM_PEND_SEND)
    {
        char err[16] = {0};
        uint32_t c = 0;
        const bool ok = sendKeyImpl(s_pend.dst, s_pend.key, s_pend.cmd, s_pend.args, err, sizeof(err), &c, nullptr, true);
        Serial.printf("[RM];chain;%s;%s\n", dst, ok ? "sent" : err);
        if (!ok)
            setChainErr(dst, rmTokenStatic(err, "send"));
    }
    else
    {
        Serial.printf("[RM];chain;%s;%s\n", dst, gone ? "lost" : "no_sync_answer");
        setChainErr(dst, gone ? "lost" : "nosync");
    }
    dropPending();
}
} // namespace

bool rmSendCommandKey(const char *dst, const uint8_t key[32], const char *cmd, const char *args, char *err,
                      size_t errN, uint32_t *ctrOut, bool *viaSync, bool force)
{
    return sendKeyImpl(dst, key, cmd, args, err, errN, ctrOut, viaSync, false, force);
}

bool rmSendCommand(const char *dst, const char *passwd, const char *cmd, const char *args, char *err,
                   size_t errN, uint32_t *ctrOut, bool *viaSync, bool force)
{
    // password: strip trailing spaces (every key derivation does), then the shared validator; a raw
    // string longer than node_passwd (14) is refused, never truncated
    size_t pl = (passwd != nullptr) ? strlen(passwd) : 0;
    const size_t raw = pl;
    while (pl > 0 && passwd[pl - 1] == ' ')
        pl--;
    if (raw > RM_PASSWD_MAX || !rmValidatePasswordN(passwd, pl))
    {
        if (viaSync != nullptr)
            *viaSync = false;
        setErr(err, errN, "passwd");
        return false;
    }

    uint8_t key[32];
    rmDeriveKey(passwd, key);
    const bool ok = sendKeyImpl(dst, key, cmd, args, err, errN, ctrOut, viaSync, false, force);
    hmac_sha256_detail::wipe(key, sizeof(key));
    return ok;
}

uint8_t rmGetSent(RmSent *out, uint8_t max)
{
    if (out == nullptr)
        return 0;
    const uint32_t now = millis();
    uint8_t n = (s_nsent < max) ? s_nsent : max;
    for (uint8_t i = 0; i < n; i++)
    {
        out[i] = s_sent[i].pub;
        RmPolEntry pe;
        polEntryOf(s_sent[i], pe);
        const RmEntryState st = rmPolicyState(pe, now);
        out[i].state = (uint8_t)st;
        out[i].stateName = rmStateName(st);
        out[i].msg = rmEntryMessage(st, s_sent[i].pub.reply);
    }
    return n;
}

uint8_t rmGetTargets(RmTarget *out, uint8_t max)
{
    if (out == nullptr)
        return 0;
    const uint32_t now = millis();
    uint8_t n = 0;
    for (uint8_t i = 0; i < s_nsent && n < max; i++)
    {
        bool seen = false;
        for (uint8_t j = 0; j < n; j++)
            if (strcmp(out[j].dst, s_sent[i].pub.dst) == 0)
                seen = true;
        if (seen)
            continue;
        RmTarget &t = out[n++];
        memset(&t, 0, sizeof(t));
        snprintf(t.dst, sizeof(t.dst), "%s", s_sent[i].pub.dst);
        const RmPolDecision d = policyFor(t.dst, now);
        t.locked = (d.reason == RM_POL_LIMIT);
        t.retryS = d.retryS;
        t.canForce = d.canForce;
        const RmProof *pf = rmProofFind(s_proof, t.dst);
        t.cap = (pf != nullptr) ? pf->cap : 0;
        t.pending = s_pend.used && strcmp(s_pend.dst, t.dst) == 0;
        if (s_chainErr.tok != nullptr && strcmp(s_chainErr.dst, t.dst) == 0)
        {
            t.chainErr = s_chainErr.tok;
            t.chainMsg = rmErrTokenMessage(s_chainErr.tok);
        }
    }
    return n;
}

bool rmTargetMaySend(const char *dst, uint32_t *retryS, bool *canForce)
{
    if (canForce != nullptr)
        *canForce = false;
    if (dst == nullptr)
        return false;
    char to[RM_CALL_MAX + 1];
    size_t i = 0;
    for (; dst[i] != '\0' && i < RM_CALL_MAX; i++)
        to[i] = (dst[i] >= 'a' && dst[i] <= 'z') ? (char)(dst[i] - 'a' + 'A') : dst[i];
    to[i] = '\0';
    const RmPolDecision d = policyFor(to, millis()); // the SAME decision as the send path
    if (retryS != nullptr)
        *retryS = d.retryS;
    if (canForce != nullptr)
        *canForce = d.canForce;
    return d.allowed;
}

void rmForgetTarget(const char *dst)
{
    if (dst == nullptr)
        return;
    char to[RM_CALL_MAX + 1];
    size_t n = 0;
    for (; dst[n] != '\0' && n < RM_CALL_MAX; n++)
        to[n] = (dst[n] >= 'a' && dst[n] <= 'z') ? (char)(dst[n] - 'a' + 'A') : dst[n];
    to[n] = '\0';
    if (s_pend.used && strcmp(s_pend.dst, to) == 0)
        dropPending();
    if (s_chainErr.tok != nullptr && strcmp(s_chainErr.dst, to) == 0)
        s_chainErr.tok = nullptr;
    rmProofForget(s_proof, to);
    // keys go, the entries STAY and keep counting toward the budget until they leave the window
    for (uint8_t i = 0; i < s_nsent; i++)
        if (strcmp(s_sent[i].pub.dst, to) == 0)
            wipeKey(s_sent[i]);
    refreshReplyWanted();
    Serial.printf("[RM];forget;%s\n", to);
}

void rmRuntimeReceiverUnlock(void)
{
    rmReceiverUnlock(s_state);
    Serial.printf("[RM];unlock\n");
}
