/**
 * TEMPORARY MEASUREMENT SCAFFOLDING (OTA-LOWHEAP-01) -- see src/instrument_heap.h.
 *
 * Compiles to nothing unless INSTRUMENT_ENABLED and ESP32 (this file is built by every
 * board env). The tracking itself additionally needs INSTR_HEAPWATCH plus the
 * -Wl,--wrap=heap_caps_* link flags; without it every command prints "unavailable".
 */

#include "instrument.h"
#include "instrument_heap.h"
#include "instrument_heaptrace.h"

#if INSTRUMENT_ENABLED && defined(ESP32)

#include "configuration_global.h"
#include "printfdeb_functions.h"
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <esp_debug_helpers.h>
#include <soc/soc_memory_layout.h>   /* esp_ptr_internal (inline, IRAM-safe) */
#include <esp_attr.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>
#include <soc/soc_memory_layout.h>
#include "heap_memory_layout.h"

/* Long listings run in the caller's task (usually the Arduino loop task, subscribed to the 5 s task WDT
 * with CONFIG_ESP_TASK_WDT_PANIC=1). A UART/net-console listing of ~1000 lines takes 9-20 s, so feed the
 * WDT and give the idle task a tick every 16 printed lines. A caller that is not subscribed answers
 * ESP_ERR_NOT_FOUND, which is expected; any other error is reported once. */
static void hw_wdt_feed(void)
{
    static bool warned = false;
    esp_err_t e = esp_task_wdt_reset();
    if (e != ESP_OK && e != ESP_ERR_NOT_FOUND && !warned)
    {
        warned = true;
        printfdeb("[INSTR-HW];wdt_reset;err;%d\n", (int)e);
    }
}

static void hw_pace(uint32_t *lines)
{
    if ((++*lines & 15u) == 0)
    {
        hw_wdt_feed();
        vTaskDelay(pdMS_TO_TICKS(20));   /* lets the net console (MSG_DONTWAIT, 2323) drain before the next lines */
    }
}

#if defined(INSTR_HEAPWATCH) || defined(INSTR_HEAPTRACE)

extern "C" {
/* Allocator glue that must not be reported as the call site. */
void *pvPortMalloc(size_t);
void *_malloc_r(struct _reent *, size_t);
void *_calloc_r(struct _reent *, size_t, size_t);
void *_realloc_r(struct _reent *, void *, size_t);
void *_Znwj(size_t);
void *_Znaj(size_t);
void *_ZnwjRKSt9nothrow_t(size_t, const void *);
void *_ZnajRKSt9nothrow_t(size_t, const void *);
}

/* Window sizes (bytes) read from `nm -S` of the heltec_wifi_lora_32_V3 elf; a pc inside [fn, fn+win) is glue.
 * Shared by the allocation table (INSTR_HEAPWATCH) and the serial trace (INSTR_HEAPTRACE). */
typedef struct { uintptr_t fn; uint16_t win; } hw_glue_t;
static const hw_glue_t kGlue[] = {
    { (uintptr_t)&malloc,               0x40 },
    { (uintptr_t)&calloc,               0x40 },
    { (uintptr_t)&realloc,              0x40 },
    { (uintptr_t)&pvPortMalloc,         0x40 },
    { (uintptr_t)&_malloc_r,            0x40 },
    { (uintptr_t)&_calloc_r,            0x40 },
    { (uintptr_t)&_realloc_r,           0x40 },
    { (uintptr_t)&_Znwj,                0x40 },
    { (uintptr_t)&_Znaj,                0x40 },
    { (uintptr_t)&_ZnwjRKSt9nothrow_t,  0x40 },
    { (uintptr_t)&_ZnajRKSt9nothrow_t,  0x40 },
};

bool instrument_heap_is_glue(uint32_t pc)
{
    for (size_t i = 0; i < sizeof(kGlue) / sizeof(kGlue[0]); i++)
        if (pc >= kGlue[i].fn && pc < kGlue[i].fn + kGlue[i].win)
            return true;
    return false;
}

#endif  /* INSTR_HEAPWATCH || INSTR_HEAPTRACE */

#if defined(INSTR_HEAPWATCH)

/* Static RAM budget (all .bss, i.e. taken from the heap that is being measured):
 *   live table  HW_LIVE_N  x  8 B
 *   site table  HW_SITES   x 40 B (+ 1 KB hash index)
 *   step ring   HW_RING_N  x 28 B
 * RTC slow/fast memory cannot hold them: 8 KB each on the ESP32-S3, s_rtc and g_loopCrumb already
 * live in RTC slow, and the tables need > 8 KB each. */
#ifndef HW_LIVE_N
#define HW_LIVE_N 1280          /* open-addressing hash, linear probing */
#endif
#define HW_LIVE_CAP (HW_LIVE_N * 85 / 100)   /* inserts stop here (load <= 85 %), the rest counts as drops */
/* HW_OWNER_ONLY=1: owner mode. No backtraces and no site table (one dummy slot, no hash index): saves the
 * site table (~10.2 KB + 1 KB index) and the per-allocation backtrace; the live table and the owner table remain. */
#ifndef HW_OWNER_ONLY
#define HW_OWNER_ONLY 0
#endif
#if HW_OWNER_ONLY
#undef HW_SITES
#define HW_SITES 1
#else
#ifndef HW_SITES
#define HW_SITES 256
#endif
#define HW_SITE_HASH 512        /* power of two, > 2 x HW_SITES */
#define HW_SITE_TOMB 0xFFFFu    /* deleted hash index entry (site recycling) */
#define HW_SITE_TOMB_MAX 128    /* rebuild the index when this many tombstones accumulated */
#endif
static_assert(HW_SITES >= 1 && HW_SITES <= 256, "site id has 8 bits in the live entry");
#define HW_PCS 4
#define HW_RING_N 64
#define HW_SITE_NONE 0xFFFFu
#define HW_SIZE_BITS 20
#define HW_SIZE_MASK ((1u << HW_SIZE_BITS) - 1u)
#define HW_SITE_SHIFT 20
#define HW_SITE_FIELD 0xFFu     /* 8 bits: limits the site table to 256 entries */
#define HW_OWN_SHIFT 28         /* 4 bits: owner id, INSTR_OWN_* */
#define HW_OWN_NONE 0xFFu       /* "no owner" marker in events (not stored in the live table) */
#define HW_OVR_TASKS 4          /* tasks that can hold an owner override at the same time */
#define HW_OVR_DEPTH 4          /* nesting depth of instrument_heap_owner_push per task */

typedef struct {
    uint32_t ptr;               /* 0 = empty */
    uint32_t meta;              /* owner << 28 | site << 20 | size (requested, rounded up to 4, clamped to 1 MB - 4) */
} hw_live_t;

typedef struct {
    uint32_t live, peak, allocs, frees;   /* bytes, bytes, counts */
    uint16_t blocks;
} hw_own_t;

typedef struct {
    volatile TaskHandle_t h;    /* NULL = free slot */
    uint8_t own[HW_OVR_DEPTH];
    uint8_t depth;
} hw_ovr_t;

static const char *const kOwnName[INSTR_OWN_COUNT] = {
    "other", "BLE", "wifi", "tcpip", "loop", "AU", "console", "ipc", "timer", "main", "idle", "TLS", "evt", "probe",
    "user1", "user2"
};

/* Task name (first 8 chars, as captured) -> owner. First match wins. exact = the name must end after the prefix. */
typedef struct { const char *pfx; uint8_t len; uint8_t exact; uint8_t own; } hw_class_t;
#define HW_C(p, e, o) { p, (uint8_t)(sizeof(p) - 1), e, o }
static const hw_class_t kClass[] = {
    HW_C("btContro", 0, INSTR_OWN_BLE),  HW_C("nimble", 0, INSTR_OWN_BLE), HW_C("BTU", 0, INSTR_OWN_BLE),
    HW_C("btdm", 0, INSTR_OWN_BLE),      HW_C("BTC", 0, INSTR_OWN_BLE),
    HW_C("wifi", 0, INSTR_OWN_WIFI),     HW_C("pp", 1, INSTR_OWN_WIFI),
    HW_C("tiT", 1, INSTR_OWN_TCPIP),
    HW_C("loopTask", 0, INSTR_OWN_LOOP),
    HW_C("fwnet", 0, INSTR_OWN_AU),
    HW_C("con_", 0, INSTR_OWN_CONSOLE),
    HW_C("ipc", 0, INSTR_OWN_IPC),
    HW_C("esp_time", 0, INSTR_OWN_TIMER), HW_C("Tmr Svc", 0, INSTR_OWN_TIMER),
    HW_C("main", 1, INSTR_OWN_MAIN),     HW_C("-", 1, INSTR_OWN_MAIN),
    HW_C("IDLE", 0, INSTR_OWN_IDLE),
    HW_C("sys_evt", 0, INSTR_OWN_EVT),
    HW_C("instr_pr", 0, INSTR_OWN_PROBE),
};

typedef struct {
    uint32_t pc[HW_PCS];
    char     task[8];           /* first allocator, not necessarily NUL terminated */
    uint32_t live_bytes;
    uint32_t allocs;
    uint32_t frees;
    uint16_t live;
    uint16_t used;              /* 1 = slot in use */
} hw_site_t;

typedef struct {
    uint32_t ms, ifree, iblk, size;
    uint16_t site;
    uint8_t  dir;               /* 0 down, 1 up */
    uint8_t  op;                /* 0 alloc, 1 free, 2 blk */
    uint8_t  own;               /* INSTR_OWN_*, HW_OWN_NONE for tick events */
    char     task[8];
} hw_ev_t;

static hw_live_t s_live[HW_LIVE_N];
static hw_site_t s_site[HW_SITES];
#if !HW_OWNER_ONLY
static uint16_t  s_site_idx[HW_SITE_HASH];      /* id + 1, 0 = empty, HW_SITE_TOMB = deleted */
static uint32_t  s_site_tomb = 0;
static uint32_t  s_site_rc = 0;                 /* recycle scan cursor */
#endif
static hw_own_t  s_own[INSTR_OWN_COUNT];
static hw_ovr_t  s_ovr[HW_OVR_TASKS];
static uint32_t  s_hw_site_recycles = 0;        /* site slots reused (live == 0) after the table was full */
static hw_ev_t   s_ring[HW_RING_N];
static uint32_t  s_ring_head, s_ring_tail;       /* free-running; head - tail <= HW_RING_N */

static portMUX_TYPE s_hw_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_hw_armed = true;          /* armed from the first allocation */
static volatile uint32_t s_hw_live = 0;
static volatile uint32_t s_hw_sites = 0;
static uint32_t s_hw_drops = 0;                  /* live table full */
static uint32_t s_hw_site_drops = 0;             /* site table full (counted into the overflow site) */
static uint32_t s_ring_drops = 0;
static volatile TaskHandle_t s_hw_owner[2] = {NULL, NULL};   /* task currently inside the tracker, per core */
static volatile uint32_t s_hw_reent = 0;         /* re-entries skipped (same task already inside the tracker) */
static volatile uint32_t s_hw_isr_skips = 0;     /* calls from ISR / interrupts disabled: not tracked (frees leave stale entries) */
static volatile uint32_t s_hw_bt_skips = 0;      /* backtraces skipped because less than HW_MIN_STACK bytes were left */
#define HW_MIN_STACK 768u                        /* bytes of task stack that must be left for the backtrace */

static volatile uint32_t s_step = 5120;
static volatile bool     s_step_on = true;
static volatile TaskHandle_t s_step_mute_task = NULL;   /* task that is printing a step event: its own allocations make no events */
static int32_t           s_bkt = -1;
static volatile uint32_t s_iblk = 0;             /* last sampled largest internal block */
static int32_t           s_iblk_bkt = -1;
static volatile uint32_t s_low = 0xFFFFFFFFu;
static uint32_t          s_last_sample_ms = 0;

#define HW_HYST 1024u

extern "C" {
void *__real_heap_caps_malloc(size_t size, uint32_t caps);
void *__real_heap_caps_calloc(size_t n, size_t size, uint32_t caps);
void *__real_heap_caps_realloc(void *ptr, size_t size, uint32_t caps);
void  __real_heap_caps_free(void *ptr);
void *__real_heap_caps_malloc_default(size_t size);
void *__real_heap_caps_realloc_default(void *ptr, size_t size);

}

/* hw_is_glue: shared with the serial trace (instrument_heaptrace.cpp), see instrument_heap_is_glue() above. */
static inline bool hw_is_glue(uint32_t pc)
{
    return instrument_heap_is_glue(pc);
}

static inline uint32_t hw_hash_ptr(uint32_t p)
{
    return ((p >> 2) * 2654435761u) % HW_LIVE_N;
}

static inline uint32_t hw_ifree_now(void)
{
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

static inline void hw_task_name(char out[8])
{
    out[0] = '-';
    memset(out + 1, 0, 7);
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        const char *n = pcTaskGetName(NULL);
        if (n)
            strncpy(out, n, 8);
    }
}

/* ------------------------------------------------------------------ ring (caller holds lock) */
static void hw_ring_push_locked(uint32_t ifree, uint32_t size, uint16_t site, uint8_t dir, uint8_t op, uint8_t own, const char *task)
{
    if (s_ring_head - s_ring_tail >= HW_RING_N)
    {
        s_ring_drops++;
        return;
    }
    hw_ev_t *e = &s_ring[s_ring_head % HW_RING_N];
    e->ms = (uint32_t)(esp_timer_get_time() / 1000);
    e->ifree = ifree;
    e->iblk = s_iblk;
    e->size = size;
    e->site = site;
    e->dir = dir;
    e->op = op;
    e->own = own;
    memcpy(e->task, task, 8);
    s_ring_head++;
}

/* Called after every internal alloc/free with the exact free figure. Updates low mark and step events. */
static void hw_note(TaskHandle_t me, uint32_t size, uint16_t site, uint8_t op, uint8_t own, const char *task)
{
    uint32_t f = hw_ifree_now();
    if (f < s_low)
    {
        portENTER_CRITICAL_SAFE(&s_hw_mux);
        if (f < s_low)
            s_low = f;
        portEXIT_CRITICAL_SAFE(&s_hw_mux);
    }
    if (!s_step_on || s_step_mute_task == me)
        return;
    uint32_t step = s_step;
    /* s_bkt holds the free figure of the last reported step. A step is reported only when free has moved a
     * full step away from it: a fixed 5 KB grid flapped 30 times a second on the T-Beam (2026-11-23), driven by
     * the 1.3-1.5 KB TCP buffers of the net console that carries these very lines. */
    if (s_bkt < 0)
    {
        s_bkt = (int32_t)f;
        return;
    }
    int dir = -1;
    if (f + step <= (uint32_t)s_bkt)
        dir = 0;
    else if (f >= (uint32_t)s_bkt + step)
        dir = 1;
    if (dir < 0)
        return;
    portENTER_CRITICAL_SAFE(&s_hw_mux);
    s_bkt = (int32_t)f;
    hw_ring_push_locked(f, size, site, (uint8_t)dir, op, own, task);
    portEXIT_CRITICAL_SAFE(&s_hw_mux);
}

/* ------------------------------------------------------------------ owners */
static uint8_t hw_classify(const char task[8])
{
    for (size_t i = 0; i < sizeof(kClass) / sizeof(kClass[0]); i++)
    {
        const hw_class_t *c = &kClass[i];
        if (strncmp(task, c->pfx, c->len) == 0 && (!c->exact || task[c->len] == 0))
            return c->own;
    }
    return INSTR_OWN_OTHER;
}

/* Override of the calling task, if any (lock held; slots of other tasks are only claimed under the lock). */
static inline uint8_t hw_owner_ovr_locked(TaskHandle_t me, uint8_t dflt)
{
    for (int i = 0; i < HW_OVR_TASKS; i++)
        if (s_ovr[i].h == me && s_ovr[i].depth)
            return s_ovr[i].own[s_ovr[i].depth - 1];
    return dflt;
}

/* ------------------------------------------------------------------ site table (caller holds lock) */
#if HW_OWNER_ONLY
static uint16_t hw_site_get_locked(const uint32_t *pc, const char *task) { (void)pc; (void)task; return 0; }
#else
static inline uint32_t hw_site_hash(const uint32_t *pc)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < HW_PCS; i++)
        h = (h ^ pc[i]) * 16777619u;
    return (h ^ (h >> 15)) & (HW_SITE_HASH - 1);
}

/* Rebuild the hash index from the site table (drops all tombstones). */
static void hw_site_reindex_locked(void)
{
    memset(s_site_idx, 0, sizeof(s_site_idx));
    for (uint32_t id = 0; id < HW_SITES - 1; id++)
    {
        if (!s_site[id].used)
            continue;
        uint32_t k = hw_site_hash(s_site[id].pc);
        while (s_site_idx[k] != 0)
            k = (k + 1) & (HW_SITE_HASH - 1);
        s_site_idx[k] = (uint16_t)(id + 1);
    }
    s_site_tomb = 0;
}

/* Table full: pick a site with live == 0 (round robin from the last pick), unhook it from the hash index
 * (tombstone: open addressing cannot just clear the slot) and return its id, or -1 if every site is live. */
static int32_t hw_site_recycle_locked(void)
{
    for (uint32_t n = 0; n < HW_SITES - 1; n++)
    {
        uint32_t id = (s_site_rc + n) % (HW_SITES - 1);
        const hw_site_t *s = &s_site[id];
        if (!s->used || s->live != 0)
            continue;
        uint32_t k = hw_site_hash(s->pc);
        for (uint32_t m = 0; m < HW_SITE_HASH; m++, k = (k + 1) & (HW_SITE_HASH - 1))
        {
            if (s_site_idx[k] == id + 1)
            {
                s_site_idx[k] = HW_SITE_TOMB;
                s_site_tomb++;
                break;
            }
            if (s_site_idx[k] == 0)
                break;
        }
        s_site_rc = id + 1;
        s_hw_site_recycles++;
        return (int32_t)id;
    }
    return -1;
}

static uint16_t hw_site_overflow_locked(void)
{
    s_hw_site_drops++;
    hw_site_t *o = &s_site[HW_SITES - 1];
    if (!o->used)
    {
        o->used = 1;
        memcpy(o->task, "overflow", 8);
    }
    return HW_SITES - 1;
}

static uint16_t hw_site_get_locked(const uint32_t *pc, const char *task)
{
    uint32_t k = hw_site_hash(pc);
    uint32_t ins = 0xFFFFFFFFu;            /* first tombstone on the probe path */
    uint32_t n = 0;
    for (; n < HW_SITE_HASH; n++, k = (k + 1) & (HW_SITE_HASH - 1))
    {
        uint16_t v = s_site_idx[k];
        if (v == 0)
            break;
        if (v == HW_SITE_TOMB)
        {
            if (ins == 0xFFFFFFFFu)
                ins = k;
            continue;
        }
        if (memcmp(s_site[v - 1].pc, pc, sizeof(s_site[0].pc)) == 0)
            return (uint16_t)(v - 1);
    }
    /* not found: k is the first empty slot (n < HW_SITE_HASH) unless the probe wrapped */
    uint32_t pos = ins != 0xFFFFFFFFu ? ins : (n < HW_SITE_HASH ? k : 0xFFFFFFFFu);
    if (pos == 0xFFFFFFFFu)
        return hw_site_overflow_locked();
    int32_t id;
    if (s_hw_sites < HW_SITES - 1)         /* last slot is the overflow bucket */
        id = (int32_t)s_hw_sites++;
    else
        id = hw_site_recycle_locked();
    if (id < 0)
        return hw_site_overflow_locked();
    hw_site_t *s = &s_site[id];
    memset(s, 0, sizeof(*s));
    memcpy(s->pc, pc, sizeof(s->pc));
    memcpy(s->task, task, 8);
    s->used = 1;
    if (s_site_idx[pos] == HW_SITE_TOMB)
        s_site_tomb--;
    s_site_idx[pos] = (uint16_t)(id + 1);
    if (s_site_tomb > HW_SITE_TOMB_MAX)
        hw_site_reindex_locked();
    return (uint16_t)id;
}
#endif

/* ------------------------------------------------------------------ live table (caller holds lock) */
static int32_t hw_live_find_locked(uint32_t ptr)
{
    uint32_t i = hw_hash_ptr(ptr);
    for (uint32_t n = 0; n < HW_LIVE_N; n++, i = (i + 1) % HW_LIVE_N)
    {
        if (s_live[i].ptr == ptr)
            return (int32_t)i;
        if (s_live[i].ptr == 0)
            return -1;
    }
    return -1;
}

static void hw_live_erase_locked(uint32_t i)
{
    uint32_t j = i;
    for (;;)
    {
        j = (j + 1) % HW_LIVE_N;
        if (s_live[j].ptr == 0)
            break;
        uint32_t k = hw_hash_ptr(s_live[j].ptr);
        bool stay = (i <= j) ? (i < k && k <= j) : (i < k || k <= j);
        if (stay)
            continue;
        s_live[i] = s_live[j];
        i = j;
    }
    s_live[i].ptr = 0;
    s_live[i].meta = 0;
}

/* Remove ptr; returns true and fills size/site if it was tracked. Lock held by caller. */
static bool hw_remove_locked(uint32_t ptr, uint32_t *size, uint16_t *site, uint8_t *own)
{
    int32_t i = hw_live_find_locked(ptr);
    if (i < 0)
        return false;
    uint32_t m = s_live[i].meta;
    uint32_t sz = m & HW_SIZE_MASK;
    uint16_t sid = (uint16_t)((m >> HW_SITE_SHIFT) & HW_SITE_FIELD);
    uint8_t ow = (uint8_t)(m >> HW_OWN_SHIFT);
    hw_own_t *o = &s_own[ow];
    o->live = o->live >= sz ? o->live - sz : 0;
    if (o->blocks)
        o->blocks--;
    o->frees++;
    hw_site_t *s = &s_site[sid];
    if (s->live)
        s->live--;
    s->live_bytes = s->live_bytes >= sz ? s->live_bytes - sz : 0;
    s->frees++;
    hw_live_erase_locked((uint32_t)i);
    s_hw_live--;
    *size = sz;
    *site = sid;
    *own = ow;
    return true;
}

/* Insert a live entry (lock held). Returns false when the table is at its cap. force = ignore the cap
 * (restoring an entry that was removed a moment ago). */
static bool hw_insert_locked(uint32_t ptr, uint16_t sid, uint8_t own, uint32_t sz, bool force)
{
    if (s_hw_live >= (force ? HW_LIVE_N - 1 : HW_LIVE_CAP))
    {
        s_hw_drops++;
        return false;
    }
    uint32_t i = hw_hash_ptr(ptr);
    while (s_live[i].ptr != 0)
        i = (i + 1) % HW_LIVE_N;
    s_live[i].ptr = ptr;
    s_live[i].meta = ((uint32_t)own << HW_OWN_SHIFT) | ((uint32_t)(sid & HW_SITE_FIELD) << HW_SITE_SHIFT) | sz;
    s_hw_live++;
    hw_own_t *o = &s_own[own];
    o->live += sz;
    o->blocks++;
    if (o->live > o->peak)
        o->peak = o->live;
    hw_site_t *s = &s_site[sid];
    s->live++;
    s->live_bytes += sz;
    return true;
}

typedef struct {
    uint32_t size;
    uint16_t site;
    uint8_t  own;
    bool     was;           /* the block was tracked and has been removed from the table */
    bool     in;            /* internal RAM */
} hw_saved_t;

static inline bool hw_untrack(void *ptr, uint32_t *size, uint16_t *site, uint8_t *own)
{
    *size = 0;
    *site = HW_SITE_NONE;
    *own = HW_OWN_NONE;
    if (ptr == NULL || s_hw_live == 0 || !esp_ptr_internal(ptr))
        return false;
    portENTER_CRITICAL_SAFE(&s_hw_mux);
    bool r = hw_remove_locked((uint32_t)ptr, size, site, own);
    portEXIT_CRITICAL_SAFE(&s_hw_mux);
    return r;
}

/* Re-entry guard per task: an allocation made by the tracker itself (same task, same core) is not tracked;
 * another task that preempts the tracker on that core is. me is never NULL (sentinel before the scheduler). */
static inline TaskHandle_t hw_me(void)
{
    TaskHandle_t me = xTaskGetCurrentTaskHandle();
    return me ? me : (TaskHandle_t)1;
}

static inline bool hw_enter(uint8_t core, TaskHandle_t me)
{
    if (core > 1)
        return false;
    if (s_hw_owner[core] == me)
    {
        s_hw_reent++;
        return false;
    }
    s_hw_owner[core] = me;
    return true;
}

static inline void hw_leave(uint8_t core, TaskHandle_t me)
{
    if (s_hw_owner[core] == me)
        s_hw_owner[core] = NULL;
}

/* Record a successful allocation. The backtrace and the free-size read run outside the lock.
 * Called only from the wrappers (so frame 1 of the backtrace is the wrapper) and never from an ISR. */
static void __attribute__((noinline)) hw_track(void *ptr, size_t size)
{
    if (ptr == NULL || !esp_ptr_internal(ptr))
        return;
    uint8_t core = (uint8_t)xPortGetCoreID();
    TaskHandle_t me = hw_me();
    if (!hw_enter(core, me))
        return;

    char task[8];
    hw_task_name(task);

    uint32_t pc[HW_PCS] = {0, 0, 0, 0};
    uint8_t own = hw_classify(task);
    if (s_hw_armed)
    {
#if !HW_OWNER_ONLY
        esp_backtrace_frame_t f;
        esp_backtrace_get_start(&f.pc, &f.sp, &f.next_pc);   /* frame 0 = hw_track */
        f.exc_frame = NULL;
        bool bt = true;
        if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        {
            /* bytes of stack left below sp (the high-water mark would scan the whole untouched stack on every call) */
            uint32_t base = (uint32_t)pxTaskGetStackStart(NULL);
            if (f.sp > base && f.sp - base < HW_MIN_STACK)
            {
                bt = false;
                s_hw_bt_skips++;
            }
        }
        if (bt && esp_backtrace_get_next_frame(&f))           /* f.pc = the wrapper */
        {
            int n = 0, seen = 0;
            while (n < HW_PCS && seen < 14)
            {
                seen++;
                if (!esp_backtrace_get_next_frame(&f))
                    break;
                uint32_t p = (f.pc & 0x80000000u) ? ((f.pc & 0x3fffffffu) | 0x40000000u) : f.pc;
                if (n == 0 && hw_is_glue(p))
                    continue;
                pc[n++] = p;
            }
        }
#endif

        uint32_t sz = (uint32_t)((size + 3) & ~(size_t)3);
        if (sz > HW_SIZE_MASK - 3)
            sz = HW_SIZE_MASK - 3;
        uint16_t sid;
        portENTER_CRITICAL_SAFE(&s_hw_mux);
        sid = hw_site_get_locked(pc, task);
        own = hw_owner_ovr_locked(me, own);
        uint32_t old_sz;
        uint16_t old_sid;
        uint8_t old_own;
        (void)hw_remove_locked((uint32_t)ptr, &old_sz, &old_sid, &old_own);   /* same block seen twice: replace */
        if (hw_insert_locked((uint32_t)ptr, sid, own, sz, false))
        {
            s_site[sid].allocs++;
            s_own[own].allocs++;
        }
        portEXIT_CRITICAL_SAFE(&s_hw_mux);
        hw_note(me, (uint32_t)size, sid, 0, own, task);
    }
    else
        hw_note(me, (uint32_t)size, HW_SITE_NONE, 0, own, task);
    hw_leave(core, me);
}

static void __attribute__((noinline)) hw_after_free(uint32_t size, uint16_t site, uint8_t own)
{
    uint8_t core = (uint8_t)xPortGetCoreID();
    TaskHandle_t me = hw_me();
    if (!hw_enter(core, me))
        return;
    char task[8];
    hw_task_name(task);
    if (own == HW_OWN_NONE)
        own = hw_classify(task);
    hw_note(me, size, site, 1, own, task);
    hw_leave(core, me);
}

/* Slow-path halves of the wrappers. Flash code: only called when hw_skip() is false (no ISR, interrupts on). */
static void __attribute__((noinline)) hw_free_pre(void *ptr, hw_saved_t *sv)
{
    sv->in = ptr != NULL && esp_ptr_internal(ptr);
    sv->was = hw_untrack(ptr, &sv->size, &sv->site, &sv->own);   /* before the real free: the address can be reused at once */
}

static void __attribute__((noinline)) hw_free_post(const hw_saved_t *sv)
{
    if (sv->in)
        hw_after_free(sv->was ? sv->size : 0, sv->site, sv->own);
}

/* realloc: the old entry is removed BEFORE the real call (a moved block can be handed out again at once).
 * hw_realloc_post puts it back if the real call fails and returns true if the result is a block to track. */
static void __attribute__((noinline)) hw_realloc_pre(void *ptr, hw_saved_t *sv)
{
    sv->in = ptr != NULL && esp_ptr_internal(ptr);
    sv->was = hw_untrack(ptr, &sv->size, &sv->site, &sv->own);
}

static bool __attribute__((noinline)) hw_realloc_post(void *p, void *ptr, size_t size, const hw_saved_t *sv)
{
    if (p == NULL && size != 0)
    {
        if (sv->was)                                    /* failed: the old block is still live */
        {
            portENTER_CRITICAL_SAFE(&s_hw_mux);
            if (hw_insert_locked((uint32_t)ptr, sv->site, sv->own, sv->size, true))
            {
                hw_site_t *s = &s_site[sv->site];
                if (s->frees)
                    s->frees--;                         /* hw_remove_locked counted a free that did not happen */
                if (s_own[sv->own].frees)
                    s_own[sv->own].frees--;
            }
            portEXIT_CRITICAL_SAFE(&s_hw_mux);
        }
        return false;
    }
    if (size == 0 && sv->was)
        hw_after_free(sv->size, sv->site, sv->own);   /* realloc(ptr, 0) is a free */
    return p != NULL;
}

/* True = call the real function and do nothing else. Inlined into the IRAM wrappers: heap_caps_* may be
 * called from IRAM ISRs and with the flash cache off, where nothing in flash or .rodata may be touched.
 * xPortCanYield() is false in an ISR and whenever interrupts are masked (critical sections, flash ops).
 * Nothing is tracked while s_hw_ready is false. */
static volatile uint32_t s_hw_skip_frees = 0;   /* internal frees skipped (ISR / interrupts masked) */

/* Tracking runs from the first allocation. (A boot loop on 2026-11-23 was first blamed on that; the real cause
 * was a flash at the wrong offset that overwrote safeboot. The T-Deck booted fine with tracking from boot.)
 * s_hw_ready stays as a switch for a build that must defer tracking to the first loop tick. */
static volatile bool s_hw_ready = true;

static inline __attribute__((always_inline)) bool hw_skip(void)
{
    if (!s_hw_ready)
        return true;
    if (xPortInIsrContext() || !xPortCanYield())
    {
        s_hw_isr_skips++;
        return true;
    }
    return false;
}

extern "C" {
IRAM_ATTR void *__wrap_heap_caps_malloc(size_t size, uint32_t caps)
{
    void *p = __real_heap_caps_malloc(size, caps);
    instrument_heaptrace_alloc(p, size);
    if (!hw_skip())
        hw_track(p, size);
    return p;
}

IRAM_ATTR void *__wrap_heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
    void *p = __real_heap_caps_calloc(n, size, caps);
    instrument_heaptrace_alloc(p, n * size);
    if (!hw_skip())
        hw_track(p, n * size);
    return p;
}

IRAM_ATTR void *__wrap_heap_caps_malloc_default(size_t size)
{
    void *p = __real_heap_caps_malloc_default(size);
    instrument_heaptrace_alloc(p, size);
    if (!hw_skip())
        hw_track(p, size);
    return p;
}

IRAM_ATTR void *__wrap_heap_caps_realloc(void *ptr, size_t size, uint32_t caps)
{
    instrument_heaptrace_realloc_pre();
    if (hw_skip())
    {
        void *q = __real_heap_caps_realloc(ptr, size, caps);
        instrument_heaptrace_realloc(q, ptr, size);
        return q;
    }
    hw_saved_t sv;
    hw_realloc_pre(ptr, &sv);
    void *p = __real_heap_caps_realloc(ptr, size, caps);
    instrument_heaptrace_realloc(p, ptr, size);
    if (hw_realloc_post(p, ptr, size, &sv))
        hw_track(p, size);
    return p;
}

IRAM_ATTR void *__wrap_heap_caps_realloc_default(void *ptr, size_t size)
{
    instrument_heaptrace_realloc_pre();
    if (hw_skip())
    {
        void *q = __real_heap_caps_realloc_default(ptr, size);
        instrument_heaptrace_realloc(q, ptr, size);
        return q;
    }
    hw_saved_t sv;
    hw_realloc_pre(ptr, &sv);
    void *p = __real_heap_caps_realloc_default(ptr, size);
    instrument_heaptrace_realloc(p, ptr, size);
    if (hw_realloc_post(p, ptr, size, &sv))
        hw_track(p, size);
    return p;
}

IRAM_ATTR void __wrap_heap_caps_free(void *ptr)
{
    instrument_heaptrace_free(ptr);
    if (hw_skip())
    {
        if (ptr != NULL && esp_ptr_internal(ptr))
            s_hw_skip_frees++;          /* leaves a stale live entry: --heapwatch clear when this grows */
        __real_heap_caps_free(ptr);
        return;
    }
    hw_saved_t sv;
    hw_free_pre(ptr, &sv);
    __real_heap_caps_free(ptr);
    hw_free_post(&sv);
}
}

/* ------------------------------------------------------------------ low-water API */
extern "C" uint32_t instrument_heap_lowmark_reset(void)
{
    uint32_t f = hw_ifree_now();
    portENTER_CRITICAL(&s_hw_mux);
    s_low = f;
    portEXIT_CRITICAL(&s_hw_mux);
    return f;
}

extern "C" uint32_t instrument_heap_lowmark_get(void)
{
    uint32_t l = s_low;
    uint32_t f = hw_ifree_now();
    return (l == 0xFFFFFFFFu || f < l) ? f : l;
}

/* ------------------------------------------------------------------ owner override API */
extern "C" void instrument_heap_owner_push(uint8_t owner)
{
    if (owner >= INSTR_OWN_COUNT)
        return;
    TaskHandle_t me = hw_me();
    portENTER_CRITICAL(&s_hw_mux);
    hw_ovr_t *slot = NULL;
    for (int i = 0; i < HW_OVR_TASKS; i++)
    {
        if (s_ovr[i].h == me)
        {
            slot = &s_ovr[i];
            break;
        }
        if (!slot && s_ovr[i].h == NULL)
            slot = &s_ovr[i];
    }
    if (slot && slot->depth < HW_OVR_DEPTH)
    {
        slot->own[slot->depth++] = owner;
        slot->h = me;
    }
    portEXIT_CRITICAL(&s_hw_mux);
}

extern "C" void instrument_heap_owner_pop(void)
{
    TaskHandle_t me = hw_me();
    portENTER_CRITICAL(&s_hw_mux);
    for (int i = 0; i < HW_OVR_TASKS; i++)
        if (s_ovr[i].h == me)
        {
            if (s_ovr[i].depth && --s_ovr[i].depth == 0)
                s_ovr[i].h = NULL;
            break;
        }
    portEXIT_CRITICAL(&s_hw_mux);
}

/* ------------------------------------------------------------------ commands */
static void hw_clear(void)
{
    portENTER_CRITICAL(&s_hw_mux);
    memset(s_live, 0, sizeof(s_live));
    memset(s_site, 0, sizeof(s_site));
#if !HW_OWNER_ONLY
    memset(s_site_idx, 0, sizeof(s_site_idx));
    s_site_tomb = 0;
    s_site_rc = 0;
#endif
    memset(s_own, 0, sizeof(s_own));
    s_hw_live = 0;
    s_hw_sites = 0;
    s_hw_drops = 0;
    s_hw_site_drops = 0;
    s_hw_site_recycles = 0;
    portEXIT_CRITICAL(&s_hw_mux);
}

static void hw_pcs_str(const hw_site_t *s, char *out, size_t n)
{
    size_t o = 0;
    out[0] = 0;
    for (int k = 0; k < HW_PCS && o < n; k++)
        o += snprintf(out + o, n - o, "%s0x%08lx", k ? "," : "", (unsigned long)s->pc[k]);
}

static void hw_task_str(const char *t, char out[9])
{
    memcpy(out, t, 8);
    out[8] = 0;
    for (int i = 0; i < 8; i++)
        if (out[i] == ';' || out[i] == '%' || (out[i] != 0 && (out[i] < 0x20 || out[i] > 0x7e)))
            out[i] = '.';
}

static hw_site_t hw_site_copy(uint32_t id)
{
    hw_site_t s;
    portENTER_CRITICAL(&s_hw_mux);
    s = s_site[id];
    portEXIT_CRITICAL(&s_hw_mux);
    return s;
}

void instrument_heapwatch(const char *arg)
{
    while (arg && *arg == ' ')
        arg++;
    if (arg == NULL || *arg == 0)
        arg = "";
    if (strncmp(arg, "on", 2) == 0)
    {
        s_hw_armed = true;
        printfdeb("[INSTR-HW];on\n");
    }
    else if (strncmp(arg, "off", 3) == 0)
    {
        s_hw_armed = false;
        printfdeb("[INSTR-HW];off;live;%u\n", (unsigned)s_hw_live);
    }
    else if (strncmp(arg, "clear", 5) == 0)
    {
        hw_clear();
        printfdeb("[INSTR-HW];cleared\n");
    }
    else if (strncmp(arg, "dump", 4) == 0)
    {
        /* Copy a chunk of slots under the lock, print outside it. */
        uint32_t n = 0, bytes = 0, lines = 0;
        hw_live_t chunk[64];
        for (uint32_t base = 0; base < HW_LIVE_N; base += 64)
        {
            uint32_t cnt = HW_LIVE_N - base < 64 ? HW_LIVE_N - base : 64;
            portENTER_CRITICAL(&s_hw_mux);
            memcpy(chunk, &s_live[base], cnt * sizeof(hw_live_t));
            portEXIT_CRITICAL(&s_hw_mux);
            for (uint32_t i = 0; i < cnt; i++)
            {
                if (chunk[i].ptr == 0)
                    continue;
                uint32_t sz = chunk[i].meta & HW_SIZE_MASK;
                hw_site_t s = hw_site_copy((chunk[i].meta >> HW_SITE_SHIFT) & HW_SITE_FIELD);
                char pcs[HW_PCS * 11 + 1], tk[9];
                hw_pcs_str(&s, pcs, sizeof(pcs));
                hw_task_str(s.task, tk);
                n++;
                bytes += sz;
                printfdeb("[INSTR-HW];live;ptr;0x%08lx;size;%lu;task;%s;pc;%s\n",
                          (unsigned long)chunk[i].ptr, (unsigned long)sz, tk, pcs);
                hw_pace(&lines);
            }
        }
        printfdeb("[INSTR-HW];sum;live;%lu;bytes;%lu;drops;%lu;reent;%lu;isrskips;%lu;skipfrees;%lu\n",
                  (unsigned long)n, (unsigned long)bytes, (unsigned long)s_hw_drops, (unsigned long)s_hw_reent,
                  (unsigned long)s_hw_isr_skips, (unsigned long)s_hw_skip_frees);
    }
    else
        printfdeb("[INSTR-HW];usage;on|off|dump|clear;armed;%d;live;%u;sites;%u;drops;%lu;sitedrops;%lu;ringdrops;%lu;"
                  "reent;%lu;isrskips;%lu;btskips;%lu;skipfrees;%lu;recycles;%lu;mode;%s\n",
                  (int)s_hw_armed, (unsigned)s_hw_live, (unsigned)s_hw_sites, (unsigned long)s_hw_drops,
                  (unsigned long)s_hw_site_drops, (unsigned long)s_ring_drops, (unsigned long)s_hw_reent,
                  (unsigned long)s_hw_isr_skips, (unsigned long)s_hw_bt_skips,
                  (unsigned long)s_hw_skip_frees, (unsigned long)s_hw_site_recycles, HW_OWNER_ONLY ? "owner" : "site");
}

void instrument_heapstep(const char *arg)
{
    while (arg && *arg == ' ')
        arg++;
    if (arg && strncmp(arg, "off", 3) == 0)
        s_step_on = false;
    else if (arg && strncmp(arg, "on", 2) == 0)
    {
        long v = atol(arg + 2);
        if (v > 0)
            s_step = (uint32_t)(v < 1024 ? 1024 : v);
        s_bkt = -1;                 /* re-baseline at the next allocator call */
        s_step_on = true;
    }
    printfdeb("[INSTR-HW];step;%s;%lu;ringdrops;%lu\n", s_step_on ? "on" : "off", (unsigned long)s_step,
              (unsigned long)s_ring_drops);
}

void instrument_heap_tick(void)
{
    s_hw_ready = true;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (s_step_on && now - s_last_sample_ms >= 250)
    {
        s_last_sample_ms = now;
        uint32_t blk = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        uint32_t step = s_step;
        int32_t b = (int32_t)(blk / step);
        uint32_t prev = s_iblk;
        s_iblk = blk;
        if (s_iblk_bkt < 0)
            s_iblk_bkt = b;
        else if (b != s_iblk_bkt && (b < s_iblk_bkt || blk >= prev + HW_HYST || blk + HW_HYST <= prev))
        {
            uint8_t dir = b > s_iblk_bkt ? 1 : 0;
            s_iblk_bkt = b;
            char tk[8] = {'t', 'i', 'c', 'k', 0, 0, 0, 0};
            portENTER_CRITICAL(&s_hw_mux);
            hw_ring_push_locked(hw_ifree_now(), blk, HW_SITE_NONE, dir, 2, HW_OWN_NONE, tk);
            portEXIT_CRITICAL(&s_hw_mux);
        }
    }
    for (int guard = 0; guard < 16; guard++)
    {
        hw_ev_t e;
        bool have = false;
        portENTER_CRITICAL(&s_hw_mux);
        if (s_ring_head != s_ring_tail)
        {
            e = s_ring[s_ring_tail % HW_RING_N];
            s_ring_tail++;
            have = true;
        }
        portEXIT_CRITICAL(&s_hw_mux);
        if (!have)
            break;
        char tk[9], sid[8];
        hw_task_str(e.task, tk);
        if (e.site == HW_SITE_NONE)
            strcpy(sid, "-");
        else
            snprintf(sid, sizeof(sid), "%u", (unsigned)e.site);
        s_step_mute_task = xTaskGetCurrentTaskHandle();   /* allocations made by this task's printing make no events */
        printfdeb("[INSTR-STEP];t_ms;%lu;dir;%s;ifree;%lu;iblk;%lu;task;%s;site;%s;size;%lu;op;%s;own;%s\n",
                  (unsigned long)e.ms, e.dir ? "up" : "down", (unsigned long)e.ifree, (unsigned long)e.iblk, tk, sid,
                  (unsigned long)e.size, e.op == 0 ? "alloc" : (e.op == 1 ? "free" : "blk"),
                  e.own < INSTR_OWN_COUNT ? kOwnName[e.own] : "-");
        s_step_mute_task = NULL;
    }
}

void instrument_heapsites(const char *arg)
{
    while (arg && *arg == ' ')
        arg++;
#if HW_OWNER_ONLY
    (void)arg;
    printfdeb("[INSTR-HW];sites;unavailable;mode;owner\n");
    return;
#endif
    bool live_only = !(arg && strncmp(arg, "all", 3) == 0);
    /* Selection by repeated maximum, ordered by (live bytes desc, id asc): no sort buffer needed. */
    uint32_t last_bytes = 0xFFFFFFFFu, last_id = 0;
    bool first = true;
    uint32_t shown = 0, lines = 0;
    for (uint32_t pass = 0; pass < HW_SITES; pass++)
    {
        int32_t best = -1;
        uint32_t bb = 0;
        hw_site_t bs;
        portENTER_CRITICAL(&s_hw_mux);
        for (uint32_t i = 0; i < HW_SITES; i++)
        {
            const hw_site_t *s = &s_site[i];
            if (!s->used || (live_only && s->live == 0))
                continue;
            if (!first && !(s->live_bytes < last_bytes || (s->live_bytes == last_bytes && i > last_id)))
                continue;
            if (best < 0 || s->live_bytes > bb)
            {
                best = (int32_t)i;
                bb = s->live_bytes;
            }
        }
        if (best >= 0)
            bs = s_site[best];
        portEXIT_CRITICAL(&s_hw_mux);
        if (best < 0)
            break;
        first = false;
        last_bytes = bb;
        last_id = (uint32_t)best;
        char pcs[HW_PCS * 11 + 1], tk[9];
        hw_pcs_str(&bs, pcs, sizeof(pcs));
        hw_task_str(bs.task, tk);
        printfdeb("[INSTR-SITE];id;%lu;pcs;%s;task;%s;live;%u;bytes;%lu;allocs;%lu;frees;%lu\n", (unsigned long)best, pcs, tk,
                  (unsigned)bs.live, (unsigned long)bs.live_bytes, (unsigned long)bs.allocs, (unsigned long)bs.frees);
        shown++;
        hw_pace(&lines);
    }
    printfdeb("[INSTR-HW];sites;shown;%lu;total;%u;sitedrops;%lu;recycles;%lu\n", (unsigned long)shown, (unsigned)s_hw_sites,
              (unsigned long)s_hw_site_drops, (unsigned long)s_hw_site_recycles);
}

/* Bytes between the end of one tracked block (ptr + requested size, rounded to 4) and the next tracked block's
 * ptr when nothing lies between them (CONFIG_HEAP_POISONING_LIGHT, IDF 4.4 multi_heap_poisoning.c): tail canary 4
 * + block header 4 + head 8 (canary + alloc_size) = 16 B. */
#define HW_MAP_NBR_OVERHEAD 16u
#define MAP_CHUNK 128
#define MAP_BUF 16
typedef struct { uint32_t addr, size; uint16_t site; uint8_t own; } map_blk_t;

void instrument_heapmap(void)
{
    /* Heap regions (soc regions minus static data), internal only. */
    size_t maxr = soc_get_available_memory_region_max_count();
    soc_memory_region_t regs[maxr ? maxr : 1];
    size_t nr = soc_get_available_memory_regions(regs);
    uint32_t rs[24], re[24];
    uint32_t nint = 0;
    for (size_t i = 0; i < nr && nint < 24; i++)
    {
        if (!(soc_memory_types[regs[i].type].caps[0] & MALLOC_CAP_INTERNAL) &&
            !(soc_memory_types[regs[i].type].caps[1] & MALLOC_CAP_INTERNAL) &&
            !(soc_memory_types[regs[i].type].caps[2] & MALLOC_CAP_INTERNAL))
            continue;
        rs[nint] = (uint32_t)regs[i].start;
        re[nint] = (uint32_t)(regs[i].start + regs[i].size);
        printfdeb("[INSTR-MAP];region;%lu;start;0x%08lx;end;0x%08lx;size;%lu\n", (unsigned long)nint,
                  (unsigned long)rs[nint], (unsigned long)re[nint], (unsigned long)regs[i].size);
        nint++;
    }

    uint32_t last = 0, used = 0, holes = 0, nblk = 0, lines = 0;
    bool have_prev = false;
    uint32_t prev_end = 0;
    for (;;)
    {
        map_blk_t buf[MAP_BUF];
        uint32_t nb = 0;
        for (uint32_t base = 0; base < HW_LIVE_N; base += MAP_CHUNK)
        {
            uint32_t cnt = HW_LIVE_N - base < MAP_CHUNK ? HW_LIVE_N - base : MAP_CHUNK;
            portENTER_CRITICAL(&s_hw_mux);
            for (uint32_t i = base; i < base + cnt; i++)
            {
                uint32_t p = s_live[i].ptr;
                if (p == 0 || p <= last)
                    continue;
                if (nb == MAP_BUF && p >= buf[nb - 1].addr)
                    continue;
                uint32_t j = nb < MAP_BUF ? nb++ : MAP_BUF - 1;
                while (j > 0 && buf[j - 1].addr > p)
                {
                    buf[j] = buf[j - 1];
                    j--;
                }
                buf[j].addr = p;
                buf[j].size = s_live[i].meta & HW_SIZE_MASK;
                buf[j].site = (uint16_t)((s_live[i].meta >> HW_SITE_SHIFT) & HW_SITE_FIELD);
                buf[j].own = (uint8_t)(s_live[i].meta >> HW_OWN_SHIFT);
            }
            portEXIT_CRITICAL(&s_hw_mux);
        }
        if (nb == 0)
            break;
        for (uint32_t i = 0; i < nb; i++)
        {
            const map_blk_t *b = &buf[i];
            if (have_prev && b->addr > prev_end + HW_MAP_NBR_OVERHEAD)
            {
                /* same heap region on both sides? then the space between them is a gap: free, or used but not tracked
                 * (allocated before tracking, SPI/DMA-capable internal blocks of other allocators, dropped inserts) */
                bool same = false;
                for (uint32_t r = 0; r < nint; r++)
                    if (prev_end >= rs[r] && b->addr < re[r])
                        same = true;
                if (same)
                {
                    printfdeb("[INSTR-MAP];blk;addr;0x%08lx;size;%lu;kind;gap;site;-;own;-\n", (unsigned long)prev_end,
                              (unsigned long)(b->addr - prev_end - HW_MAP_NBR_OVERHEAD));
                    holes++;
                    hw_pace(&lines);
                }
            }
            printfdeb("[INSTR-MAP];blk;addr;0x%08lx;size;%lu;kind;used;site;%u;own;%s\n", (unsigned long)b->addr,
                      (unsigned long)b->size, (unsigned)b->site, kOwnName[b->own]);
            hw_pace(&lines);
            used += b->size;
            nblk++;
            prev_end = b->addr + b->size;
            have_prev = true;
            last = b->addr;
        }
        hw_wdt_feed();      /* the table scan of one pass can take a while too */
    }

    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);
    uint32_t total_used = (uint32_t)info.total_allocated_bytes;
    printfdeb("[INSTR-MAP];sum;used;%lu;untracked;%lu;free;%lu;holes;%lu;largest;%lu;drops;%lu;blk;%lu\n",
              (unsigned long)used, (unsigned long)(total_used > used ? total_used - used : 0),
              (unsigned long)info.total_free_bytes, (unsigned long)holes, (unsigned long)info.largest_free_block,
              (unsigned long)s_hw_drops, (unsigned long)nblk);   /* blk = printed blk lines: detects truncation */
}

void instrument_heapowners(const char *arg)
{
    while (arg && *arg == ' ')
        arg++;
    bool reset = arg && strncmp(arg, "reset", 5) == 0;
    uint32_t lo[INSTR_OWN_COUNT], hi[INSTR_OWN_COUNT];
    for (int i = 0; i < INSTR_OWN_COUNT; i++)
    {
        lo[i] = 0xFFFFFFFFu;
        hi[i] = 0;
    }
    for (uint32_t base = 0; base < HW_LIVE_N; base += MAP_CHUNK)
    {
        uint32_t cnt = HW_LIVE_N - base < MAP_CHUNK ? HW_LIVE_N - base : MAP_CHUNK;
        portENTER_CRITICAL(&s_hw_mux);
        for (uint32_t i = base; i < base + cnt; i++)
        {
            uint32_t p = s_live[i].ptr;
            if (p == 0)
                continue;
            uint8_t o = (uint8_t)(s_live[i].meta >> HW_OWN_SHIFT);
            if (p < lo[o])
                lo[o] = p;
            if (p > hi[o])
                hi[o] = p;
        }
        portEXIT_CRITICAL(&s_hw_mux);
    }
    uint32_t tracked = 0, lines = 0;
    for (int i = 0; i < INSTR_OWN_COUNT; i++)
    {
        hw_own_t o;
        portENTER_CRITICAL(&s_hw_mux);
        o = s_own[i];
        if (reset)
        {
            s_own[i].peak = s_own[i].live;
            s_own[i].allocs = 0;
            s_own[i].frees = 0;
        }
        portEXIT_CRITICAL(&s_hw_mux);
        tracked += o.live;
        if (o.live == 0 && o.peak == 0 && o.allocs == 0 && o.frees == 0)
            continue;
        printfdeb("[INSTR-OWN];owner;%s;live;%lu;blocks;%lu;peak;%lu;allocs;%lu;frees;%lu;lo;0x%08lx;hi;0x%08lx\n",
                  kOwnName[i], (unsigned long)o.live, (unsigned long)o.blocks, (unsigned long)o.peak,
                  (unsigned long)o.allocs, (unsigned long)o.frees, (unsigned long)(lo[i] == 0xFFFFFFFFu ? 0 : lo[i]),
                  (unsigned long)hi[i]);
        hw_pace(&lines);
    }
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);
    uint32_t total_used = (uint32_t)info.total_allocated_bytes;
    printfdeb("[INSTR-OWN];sum;tracked;%lu;untracked;%lu;free;%lu;largest;%lu;drops;%lu\n", (unsigned long)tracked,
              (unsigned long)(total_used > tracked ? total_used - tracked : 0), (unsigned long)info.total_free_bytes,
              (unsigned long)info.largest_free_block, (unsigned long)s_hw_drops);
}

#else  /* !INSTR_HEAPWATCH */

void instrument_heapwatch(const char *arg)  { (void)arg; printfdeb("[INSTR-HW];unavailable\n"); }
void instrument_heapstep(const char *arg)   { (void)arg; printfdeb("[INSTR-HW];unavailable\n"); }
void instrument_heapsites(const char *arg)  { (void)arg; printfdeb("[INSTR-HW];unavailable\n"); }
void instrument_heapmap(void)               { printfdeb("[INSTR-HW];unavailable\n"); }
void instrument_heapowners(const char *arg) { (void)arg; printfdeb("[INSTR-HW];unavailable\n"); }
extern "C" void instrument_heap_owner_push(uint8_t owner) { (void)owner; }
extern "C" void instrument_heap_owner_pop(void)           {}
void instrument_heap_tick(void)             {}

extern "C" uint32_t instrument_heap_lowmark_reset(void)
{
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

extern "C" uint32_t instrument_heap_lowmark_get(void)
{
    return 0xFFFFFFFFu;     /* unknown: no tracking, and the IDF since-boot minimum would pass for a window minimum */
}

#endif  /* INSTR_HEAPWATCH */

/* ------------------------------------------------------------------ --heapgap (probing, independent of the wrappers) */
#define GAP_MAX_BLOCKS 16
#define GAP_MIN_BLOCK  4096
#define GAP_MAX_GAP    2048
#define GAP_RESERVE    24576   /* stop grabbing while total internal free would drop below this (WiFi/BLE/TLS need it) */
#define GAP_COPY       64   /* per gap; 16 x 64 B of .bss in every INSTRUMENT ESP32 build */

typedef struct { uint32_t addr; uint32_t size; } gap_blk_t;
static gap_blk_t s_gap_blk[GAP_MAX_BLOCKS];
static uint8_t   s_gap_bytes[GAP_MAX_BLOCKS][GAP_COPY];
static uint16_t  s_gap_len[GAP_MAX_BLOCKS];      /* gap after block i (0 = none / too large) */
static uint16_t  s_gap_copied[GAP_MAX_BLOCKS];

void instrument_heapgap(void)
{
    uint32_t n = 0;
    bool reserve_stop = false;
    void *ptrs[GAP_MAX_BLOCKS];

    vTaskSuspendAll();
    while (n < GAP_MAX_BLOCKS)
    {
        size_t big = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (big < GAP_MIN_BLOCK)
            break;
        if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < (size_t)big + GAP_RESERVE)
        {
            reserve_stop = true;
            break;
        }
        void *p = heap_caps_malloc(big, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (p == NULL)
            break;
        ptrs[n] = p;
        s_gap_blk[n].addr = (uint32_t)p;
        s_gap_blk[n].size = (uint32_t)big;
        n++;
    }
    /* insertion sort by address */
    for (uint32_t i = 1; i < n; i++)
    {
        gap_blk_t k = s_gap_blk[i];
        uint32_t j = i;
        while (j > 0 && s_gap_blk[j - 1].addr > k.addr)
        {
            s_gap_blk[j] = s_gap_blk[j - 1];
            j--;
        }
        s_gap_blk[j] = k;
    }
    for (uint32_t i = 0; i < GAP_MAX_BLOCKS; i++)
    {
        s_gap_len[i] = 0;
        s_gap_copied[i] = 0;
    }
    for (uint32_t i = 0; i + 1 < n; i++)
    {
        uint32_t end = s_gap_blk[i].addr + s_gap_blk[i].size;
        if (s_gap_blk[i + 1].addr > end && s_gap_blk[i + 1].addr - end < GAP_MAX_GAP)
        {
            uint32_t len = s_gap_blk[i + 1].addr - end;
            uint32_t c = len < GAP_COPY ? len : GAP_COPY;
            memcpy(s_gap_bytes[i], (const void *)end, c);
            s_gap_len[i] = (uint16_t)len;
            s_gap_copied[i] = (uint16_t)c;
        }
    }
    for (uint32_t i = 0; i < n; i++)
        heap_caps_free(ptrs[i]);
    xTaskResumeAll();

    uint32_t total = 0, lines = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        total += s_gap_blk[i].size;
        printfdeb("[INSTR-GAP];blk;addr;0x%08lx;size;%lu\n", (unsigned long)s_gap_blk[i].addr, (unsigned long)s_gap_blk[i].size);
        hw_pace(&lines);
    }
    for (uint32_t i = 0; i + 1 < n; i++)
    {
        if (s_gap_len[i] == 0)
            continue;
        char hex[GAP_COPY * 2 + 1];
        char asc[GAP_COPY + 1];
        for (uint32_t k = 0; k < s_gap_copied[i]; k++)
        {
            uint8_t b = s_gap_bytes[i][k];
            snprintf(hex + k * 2, 3, "%02x", b);
            asc[k] = (b >= 0x20 && b < 0x7f && b != ';' && b != '%') ? (char)b : '.';
        }
        hex[s_gap_copied[i] * 2] = 0;
        asc[s_gap_copied[i]] = 0;
        printfdeb("[INSTR-GAP];gap;after;0x%08lx;len;%u;hex;%s;ascii;%s\n",
                  (unsigned long)(s_gap_blk[i].addr + s_gap_blk[i].size), (unsigned)s_gap_len[i], hex, asc);
        hw_pace(&lines);
    }
    printfdeb("[INSTR-GAP];sum;blocks;%lu;total;%lu;reserve_stop;%d\n", (unsigned long)n, (unsigned long)total,
              (int)reserve_stop);
}

#endif  /* INSTRUMENT_ENABLED && ESP32 */
