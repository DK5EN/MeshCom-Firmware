// CLK-01 W0 (docs/campaign-clk-01-20261107.md) -- display-clock lag instrumentation.
// Whole file is INSTRUMENT_ENABLED only; nothing of it exists in a normal build.
#include "clock_instrument.h"

#if INSTRUMENT_ENABLED

#include <Arduino.h>
#include <string.h>
#include "configuration_global.h"
#include "clock.h"
#include "clock_tick.h"
#include "b64enc.h"
#include "watchdog_feed.h"
#include "oled_shot.h"
#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)
#include "t-deck/tdeck_debug.h"
#endif

extern Clock MyClock;

namespace {

enum TimeSrcMode { TS_ALL = 0, TS_CET = 1, TS_NONE = 2 };

TimeSrcMode g_mode = TS_ALL;            // default all; RAM only, a reboot resets it
char        g_pending[8] = "";          // announced source of the next set
char        g_lastSrc[8] = "INIT";      // last ACCEPTED named source, printed as src in [CLOCK];stat

// per-source rate limit of [CLOCK];set
struct SetSlot { char src[8]; uint32_t lastLogMs; bool used; };
SetSlot g_slots[8];

uint32_t g_ticks = 0;
uint32_t g_lateSum = 0;
uint32_t g_lateMax = 0;
uint32_t g_setMs = 0;                   // millis() of the last clock set
uint32_t g_countedSinceSet = 0;         // seconds counted since then
int32_t  g_lostMs = 0;                  // clockLostMs() at the last handled tick
uint32_t g_lastStatMs = 0;

uint32_t g_lastDrawLogMs = 0;
uint32_t g_lastSkipLogMs = 0;
bool     g_drawLogged = false;
bool     g_skipLogged = false;

void takePending(char *dst, size_t cap)
{
    if (g_pending[0] == '\0')
        snprintf(dst, cap, "OTHER");
    else
        snprintf(dst, cap, "%s", g_pending);
    g_pending[0] = '\0';
}

const char *modeName(TimeSrcMode m)
{
    return m == TS_CET ? "cet" : (m == TS_NONE ? "none" : "all");
}

} // namespace

void clockInstrSrc(const char *src)
{
    snprintf(g_pending, sizeof(g_pending), "%s", src ? src : "");
}

bool clockInstrCetOnly(void)
{
    return g_mode == TS_CET;
}

bool clockInstrGated(void)
{
    return g_mode != TS_ALL;
}

bool clockInstrAccept(void)
{
    bool ok;
    if (g_mode == TS_ALL)
        ok = true;
    else if (g_mode == TS_CET)
        ok = (strcmp(g_pending, "CET") == 0);
    else
        ok = false;
    if (!ok)
        g_pending[0] = '\0';            // ignored: the announcement is spent
    return ok;
}

void clockInstrOnSet(time_t old_epoch, time_t new_epoch, uint32_t into_second_ms, uint32_t now_ms)
{
    char src[8];
    takePending(src, sizeof(src));
    if (strcmp(src, "OTHER") != 0)
        snprintf(g_lastSrc, sizeof(g_lastSrc), "%s", src);

    // new time minus the node's time right now (epoch of the last tick + ms into that second)
    const int64_t delta_ms = ((int64_t)new_epoch - (int64_t)old_epoch) * 1000 - (int64_t)into_second_ms;

    // after a set the lost-time bookkeeping restarts
    g_setMs = now_ms;
    g_countedSinceSet = 0;
    g_lostMs = 0;

    // rate limit: |delta| >= 1 s always, otherwise once per 60 s per source
    SetSlot *slot = NULL;
    for (size_t i = 0; i < sizeof(g_slots) / sizeof(g_slots[0]); i++)
    {
        if (g_slots[i].used && strcmp(g_slots[i].src, src) == 0) { slot = &g_slots[i]; break; }
        if (!g_slots[i].used && slot == NULL) slot = &g_slots[i];
    }
    const bool big = (delta_ms >= 1000 || delta_ms <= -1000);
    bool due = big;
    if (slot != NULL)
    {
        if (!slot->used)
        {
            slot->used = true;
            snprintf(slot->src, sizeof(slot->src), "%s", src);
            due = true;                 // first set of this source
        }
        else if ((uint32_t)(now_ms - slot->lastLogMs) >= 60000u)
            due = true;
        if (due)
            slot->lastLogMs = now_ms;
    }
    else
        due = true;                     // table full: log rather than hide
    if (!due)
        return;
    Serial.printf("[CLOCK];set;src;%s;old;%lu;new;%lu;delta_ms;%ld\n", src,
                  (unsigned long)old_epoch, (unsigned long)new_epoch, (long)clockDeltaClampMs(delta_ms));
}

void clockInstrOnTick(uint32_t now_ms, uint32_t next_ms, uint32_t secs)
{
    const uint32_t late = clockTickLateMs(now_ms, next_ms);
    g_ticks++;
    g_lateSum += late;
    if (late > g_lateMax)
        g_lateMax = late;
    g_countedSinceSet += secs;
    g_lostMs = clockLostMs(now_ms, g_setMs, g_countedSinceSet);
}

static void printStat(time_t epoch, uint32_t now_ms)
{
    Serial.printf("[CLOCK];stat;epoch;%lu;ms;%lu;ticks;%lu;late_sum_ms;%lu;late_max_ms;%lu;lost_ms;%ld;src;%s\n",
                  (unsigned long)epoch, (unsigned long)now_ms, (unsigned long)g_ticks,
                  (unsigned long)g_lateSum, (unsigned long)g_lateMax, (long)g_lostMs, g_lastSrc);
}

void clockInstrStatPoll(time_t epoch, uint32_t now_ms)
{
    if ((uint32_t)(now_ms - g_lastStatMs) < 60000u)
        return;
    g_lastStatMs = now_ms;
    printStat(epoch, now_ms);
}

void clockInstrCmdClockstat(void)
{
    g_lastStatMs = millis();
    printStat(MyClock.Epoch(), millis());
}

void clockInstrCmdTimesrc(const char *arg)
{
    while (arg != NULL && *arg == ' ')
        arg++;
    TimeSrcMode m;
    if (arg != NULL && strncmp(arg, "all", 3) == 0)       m = TS_ALL;
    else if (arg != NULL && strncmp(arg, "cet", 3) == 0)  m = TS_CET;
    else if (arg != NULL && strncmp(arg, "none", 4) == 0) m = TS_NONE;
    else
    {
        Serial.printf("[CLOCK];timesrc;err;usage --timesrc all|cet|none;now;%s\n", modeName(g_mode));
        return;
    }
    g_mode = m;
    Serial.printf("[CLOCK];timesrc;%s\n", modeName(g_mode));
}

void clockInstrDispDraw(int h, int m, int s, int type)
{
    const uint32_t now = millis();
    if (g_drawLogged && (uint32_t)(now - g_lastDrawLogMs) < 900u)
        return;
    g_drawLogged = true;
    g_lastDrawLogMs = now;
    Serial.printf("[DISPCLK];draw;hms;%02d:%02d:%02d;type;%d\n", h, m, s, type);
}

void clockInstrDispSkip(const char *reason)
{
    const uint32_t now = millis();
    if (g_skipLogged && (uint32_t)(now - g_lastSkipLogMs) < 900u)
        return;
    g_skipLogged = true;
    g_lastSkipLogMs = now;
    Serial.printf("[DISPCLK];skip;%s\n", reason);
}

void clockInstrCmdOledshot(void)
{
#if MC_HAS_U8G2
    const uint8_t *buf = NULL;
    size_t len = 0;
    int w = 0, h = 0;
    const int rc = oledShotSource(&buf, &len, &w, &h);
    if (rc != 0)
    {
        Serial.printf("[OLEDSHOT];err;%s\n", rc == 1 ? "no display" : (rc == 2 ? "display off" : "not a full frame"));
        return;
    }
    // 1024 raw bytes -> 1368 chars + NUL
    static char line[1400];
    if (len > OLED_SHOT_BYTES * 4u || b64Encode(buf, len, line, sizeof(line)) == 0)
    {
        Serial.printf("[OLEDSHOT];err;buffer too large\n");
        return;
    }
    Serial.printf("[OLEDSHOT];%dx%d;%s\n", w, h, line);
#else
    Serial.printf("[OLEDSHOT];err;no display\n");
#endif
}

void clockInstrCmdTdshot(void)
{
#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)
    const uint8_t *buf = tdeck_dbg_shadow_buf();
    if (buf == NULL)
    {
        // first use: allocate the PSRAM copy and force a full redraw; it fills while the UI redraws
        if (!tdeck_dbg_shadow_on())
        {
            Serial.printf("[TDSHOT];err;no memory\n");
            return;
        }
        Serial.printf("[TDSHOT];err;shadow was off, now on - repeat --tdshot after the next redraw\n");
        return;
    }
    const size_t total = tdeck_dbg_shadow_bytes();
    if (total != (size_t)320 * 240 * 2)
    {
        Serial.printf("[TDSHOT];err;unexpected frame size %u\n", (unsigned)total);
        return;
    }
    const size_t CHUNK = 768;           // raw bytes per line -> 1024 base64 chars
    const size_t nchunks = (total + CHUNK - 1) / CHUNK;
    char line[1032];
    Serial.printf("[TDSHOT];begin;320x240;%u;%u\n", (unsigned)total, (unsigned)nchunks);
    for (size_t i = 0; i < nchunks; i++)
    {
        const size_t off = i * CHUNK;
        const size_t n = (total - off < CHUNK) ? (total - off) : CHUNK;
        b64Encode(buf + off, n, line, sizeof(line));
        Serial.printf("[TDSHOT];chunk;%u;%s\n", (unsigned)i, line);
        Serial.flush();                 // drain the TX buffer
        // yield() does not feed the ESP32 task WDT (watchdog_feed.h); 150 KiB at 115200 baud blocks
        // the loop task ~15 s. Allowed by watchdog_feed.h: "Nur an nachweislich endlich begrenzten
        // Stellen einsetzen" -- this loop is bounded to nchunks (200) iterations.
        meshcom_wdt_feed();
        yield();
    }
    Serial.printf("[TDSHOT];end\n");
#else
    Serial.printf("[TDSHOT];err;T-Deck only\n");
#endif
}

#endif // INSTRUMENT_ENABLED
