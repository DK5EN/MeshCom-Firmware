/**
 * TEMPORARY MEASUREMENT SCAFFOLDING (OTA-LOWHEAP-01) -- see src/instrument_heaptrace.h for the line format
 * and the transport rules.
 */

#include "instrument_heaptrace.h"
#include "instrument_heap.h"   // instrument_heap_is_glue (shared glue filter)

#if INSTR_HEAPTRACE_ON

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <esp_debug_helpers.h>
#include <soc/soc_memory_layout.h>   /* esp_ptr_internal (inline, IRAM-safe) */
#include <esp_attr.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>   /* the waiting task keeps its watchdog fed */
#include <driver/uart.h>
#include <esp_spi_flash.h>   /* spi_flash_cache_enabled (IDF 4.4, IRAM) */
#if __has_include(<esp_rom_sys.h>)
#include <esp_rom_sys.h>
#define HT_ROM_PRINTF esp_rom_printf
#else
#include <rom/ets_sys.h>
#define HT_ROM_PRINTF ets_printf
#endif

#define HT_RING_BYTES   2048u    /* formatted lines incl. 1 length byte per line */
#define HT_LINE_MAX     128u
#define HT_PCS          3
#define HT_BAUD         921600
#define HT_BAUD_STR     "921600"
#define HT_LINE_BUF     (HT_LINE_MAX + 1u)   /* sizeof(ht_line_t) */
#define HT_MIN_STACK    (768u + HT_LINE_BUF) /* task stack that must be left when the hook is entered: backtrace + line buffer */
#define HT_DRAIN_STACK  3072u
#define HT_WAIT_MAX_US  2000000  /* deadlock guard: a producer waits at most this long for ring room */
#define HT_DRAIN_CHUNK  256u

static uint8_t           s_ring[HT_RING_BYTES];
static uint32_t          s_head = 0, s_tail = 0;            /* monotonic byte counters, guarded by s_mux */
static portMUX_TYPE      s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_drops = 0;
static volatile uint32_t s_wait_ticks = 0;   /* ticks producers spent waiting for ring room (perturbation) */
static volatile uint32_t s_events = 0;                       /* also keeps the IRAM entry points from becoming tail calls */
static volatile uint8_t  s_state = 0;                        /* 0 = not started, 1 = starting, 2 = drain task running, 3 = start failed */
static volatile TaskHandle_t s_drain = NULL;
static volatile TaskHandle_t s_owner[2] = { NULL, NULL };    /* re-entry guard per core */

static inline TaskHandle_t ht_me(void)
{
    TaskHandle_t me = xTaskGetCurrentTaskHandle();
    return me ? me : (TaskHandle_t)1;   /* sentinel before the scheduler runs */
}

/* False = this task is already inside the tracer (its own allocation) or is the drain task: no event. */
static inline bool ht_enter(uint8_t core, TaskHandle_t me)
{
    if (core > 1 || me == s_drain || s_owner[core] == me)
        return false;
    s_owner[core] = me;
    return true;
}

static inline void ht_leave(uint8_t core, TaskHandle_t me)
{
    if (s_owner[core] == me)
        s_owner[core] = NULL;
}

/* ------------------------------------------------------------------ line builder (no snprintf: stack and malloc free) */
typedef struct { char b[HT_LINE_MAX]; uint8_t n; } ht_line_t;

static void ht_c(ht_line_t *l, char c)
{
    if (l->n < HT_LINE_MAX - 1)
        l->b[l->n++] = c;
}

static void ht_hex8(ht_line_t *l, uint32_t v)
{
    for (int sh = 28; sh >= 0; sh -= 4)
        ht_c(l, "0123456789abcdef"[(v >> sh) & 0xF]);
}

static void ht_dec(ht_line_t *l, uint32_t v)
{
    char t[10];
    int i = 0;
    do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (i)
        ht_c(l, t[--i]);
}

static void ht_str(ht_line_t *l, const char *s)
{
    while (*s)
        ht_c(l, *s++);
}

/* Field text: at most max chars, anything outside [0-9A-Za-z._-] (incl. ';' and spaces) becomes '_'. */
static void ht_name(ht_line_t *l, const char *s, uint8_t max)
{
    for (uint8_t i = 0; i < max && s[i]; i++)
    {
        char c = s[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '.' || c == '_' || c == '-';
        ht_c(l, ok ? c : '_');
    }
}

static inline uint32_t ht_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ------------------------------------------------------------------ transport */
static void ht_drain_task(void *arg)
{
    (void)arg;
    s_drain = xTaskGetCurrentTaskHandle();   /* before anything else: the task's own allocations make no events */
    /* Bench 23.11.2026: 115200 baud carries ~250 lines/s; the waiting producers then stall the node (web 503, no
     * console login). Switch UART0 to HT_BAUD: announce at the old rate, let it drain, pause so the capture
     * (tools/bench/serial_capture.py --heaptrace-baud) can follow, then switch. Boot output before stays 115200. */
    {
        static const char k[] = "[HT];i;0;baud;" HT_BAUD_STR "\n";
        uart_write_bytes(UART_NUM_0, k, sizeof(k) - 1);
        uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(500));
        vTaskDelay(pdMS_TO_TICKS(300));
        uart_set_baudrate(UART_NUM_0, HT_BAUD);
    }
    uint8_t buf[HT_DRAIN_CHUNK];
    for (;;)
    {
        uint32_t n = 0;
        portENTER_CRITICAL(&s_mux);
        while (s_head != s_tail)
        {
            uint32_t len = s_ring[s_tail % HT_RING_BYTES];
            if (n + len > sizeof(buf))
                break;
            for (uint32_t i = 0; i < len; i++)
                buf[n + i] = s_ring[(s_tail + 1 + i) % HT_RING_BYTES];
            n += len;
            s_tail += 1 + len;
        }
        portEXIT_CRITICAL(&s_mux);
        if (n == 0)
        {
            vTaskDelay(1);
            continue;
        }
        uart_write_bytes(UART_NUM_0, (const char *)buf, n);   /* whole lines per call: not torn by other uart writers */
    }
}

/* Put one finished line into the ring without waiting. False = no room. */
static bool ht_ring_push(const ht_line_t *l)
{
    uint32_t need = (uint32_t)l->n + 1;
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    if (HT_RING_BYTES - (s_head - s_tail) >= need)
    {
        s_ring[s_head % HT_RING_BYTES] = l->n;
        for (uint32_t i = 0; i < l->n; i++)
            s_ring[(s_head + 1 + i) % HT_RING_BYTES] = (uint8_t)l->b[i];
        s_head += need;
        ok = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

/* Lazy start, called with the tracer guard held. Needs the scheduler and the UART0 driver (Serial.begin done). */
static bool ht_start_drain(void)
{
    if (s_state == 2)
        return true;
    if (s_state != 0 || xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || !uart_is_driver_installed(UART_NUM_0))
        return false;
    uint8_t expect = 0;
    if (!__atomic_compare_exchange_n(&s_state, &expect, (uint8_t)1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return false;
    TaskHandle_t h = NULL;
    if (xTaskCreatePinnedToCore(ht_drain_task, "httrace", HT_DRAIN_STACK, NULL, 1, &h, tskNO_AFFINITY) != pdPASS)
    {
        s_state = 3;   /* lines are dropped while the UART driver is installed, else esp_rom_printf */
        return false;
    }
    s_state = 2;
    /* The drain task's own stack and TCB come from the heap but are not traced (the task makes no events): say so. */
    ht_line_t l;
    l.n = 0;
    ht_str(&l, "[HT];i;");
    ht_dec(&l, ht_ms());
    ht_str(&l, ";drain;stack;");
    ht_dec(&l, (uint32_t)(HT_DRAIN_STACK + sizeof(StaticTask_t)));
    ht_c(&l, '\n');
    if (!ht_ring_push(&l))
        __atomic_fetch_add(&s_drops, 1, __ATOMIC_RELAXED);
    return true;
}

static inline bool ht_can_wait(uint8_t core)
{
    return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING && xTaskGetCurrentTaskHandle() != xTaskGetIdleTaskHandleForCPU(core);
}

/* Wait until the ring has room for need bytes. False = no room (drop). */
static bool ht_wait_room(uint8_t core, uint32_t need)
{
    int64_t t0 = esp_timer_get_time();
    for (;;)
    {
        portENTER_CRITICAL(&s_mux);
        uint32_t room = HT_RING_BYTES - (s_head - s_tail);
        portEXIT_CRITICAL(&s_mux);
        if (room >= need)
            return true;
        if (!ht_can_wait(core) || esp_timer_get_time() - t0 > HT_WAIT_MAX_US)
            return false;
        /* Bench 23.11.2026: at 115200 baud loopTask waited here longer than the 5 s task watchdog in total and the
         * V3 reset (TASK_WDT). The wait is the operator's choice (complete trace), so the waiting task feeds its own
         * watchdog; only a task subscribed to it is touched (esp_task_wdt_status). */
        if (esp_task_wdt_status(NULL) == ESP_OK)
            esp_task_wdt_reset();
        vTaskDelay(1);
        __atomic_fetch_add(&s_wait_ticks, 1, __ATOMIC_RELAXED);
    }
}

/* Write one finished line. Caller holds the tracer guard. */
static void ht_emit(ht_line_t *l, uint8_t core)
{
    if (!ht_start_drain())
    {
        /* Drain task starting (1) or failed (3) with Serial up: esp_rom_printf would tear against Serial, so drop
         * (state 1 is brief). Before Serial.begin (no UART driver) the direct ROM output is the only path. */
        if ((s_state == 1 || s_state == 3) && uart_is_driver_installed(UART_NUM_0))
        {
            __atomic_fetch_add(&s_drops, 1, __ATOMIC_RELAXED);
            return;
        }
        l->b[l->n] = 0;
        HT_ROM_PRINTF("%s", l->b);
        return;
    }
    uint32_t need = (uint32_t)l->n + 1;
    if (!ht_wait_room(core, need))
    {
        __atomic_fetch_add(&s_drops, 1, __ATOMIC_RELAXED);
        return;
    }
    if (!ht_ring_push(l))   /* another producer may have taken the room since the check */
        __atomic_fetch_add(&s_drops, 1, __ATOMIC_RELAXED);
}

/* ------------------------------------------------------------------ events */
/* Frame 0 = this function, 1 = IRAM entry point (instrument_heaptrace_*), 2 = the heap_caps wrapper, 3.. = its
 * callers (pc1..pc3). The two esp_backtrace_get_next_frame() calls below skip frames 0->1->2, so the entry points
 * must stay real frames: they are noinline (in the trace-only build the wrappers share this file and the compiler
 * could otherwise inline them into the wrapper, which would make the skip land one frame too far). */
static void __attribute__((noinline)) ht_event(char op, void *ptr, size_t size, void *oldp)
{
    uint8_t core = (uint8_t)xPortGetCoreID();
    TaskHandle_t me = ht_me();
    if (!ht_enter(core, me))
        return;

    uint32_t t = ht_ms();
    bool sched = xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
    ht_line_t l;   /* the stack check in ht_ctx_ok() ran before this frame was built */
    l.n = 0;
    ht_str(&l, "[HT];");
    ht_c(&l, op);
    ht_c(&l, ';');
    ht_dec(&l, t);
    ht_c(&l, ';');
    ht_hex8(&l, (uint32_t)ptr);
    ht_c(&l, ';');
    ht_dec(&l, (uint32_t)size);
    ht_c(&l, ';');
    if (sched)
    {
        const char *n = pcTaskGetName(NULL);
        ht_name(&l, n ? n : "-", 8);
    }
    else
        ht_c(&l, '-');

    uint32_t pc[HT_PCS] = {0, 0, 0};
    esp_backtrace_frame_t f;
    esp_backtrace_get_start(&f.pc, &f.sp, &f.next_pc);
    f.exc_frame = NULL;
    if (esp_backtrace_get_next_frame(&f) && esp_backtrace_get_next_frame(&f))   /* f.pc = the wrapper */
    {
        int n = 0, seen = 0;
        while (n < HT_PCS && seen < 14)   /* allocator glue (malloc, operator new, ...) is skipped, as in hw_track */
        {
            seen++;
            if (!esp_backtrace_get_next_frame(&f))
                break;
            const uint32_t p = (f.pc & 0x80000000u) ? ((f.pc & 0x3fffffffu) | 0x40000000u) : f.pc;
            if (instrument_heap_is_glue(p))
                continue;
            pc[n++] = p;
        }
    }
    for (int i = 0; i < HT_PCS; i++)
    {
        ht_c(&l, ';');
        ht_hex8(&l, pc[i]);
    }
    if (op == 'r')
    {
        ht_c(&l, ';');
        ht_hex8(&l, (uint32_t)oldp);
    }
    ht_c(&l, '\n');
    ht_emit(&l, core);
    ht_leave(core, me);
}

static void __attribute__((noinline)) ht_realloc_pre(void)
{
    uint8_t core = (uint8_t)xPortGetCoreID();
    TaskHandle_t me = ht_me();
    if (!ht_enter(core, me))
        return;
    if (s_state == 2)
        (void)ht_wait_room(core, HT_LINE_MAX + 1);
    ht_leave(core, me);
}

/* Context gate of every event hook. Runs in the hook, BEFORE ht_event builds its line buffer. Drops and counts:
 * ISR / critical section / suspended scheduler, flash cache disabled (flash-resident code must not run), less than
 * HT_MIN_STACK bytes of task stack left. */
static inline __attribute__((always_inline)) bool ht_ctx_ok(void)
{
    bool ok = !xPortInIsrContext() && xPortCanYield() && spi_flash_cache_enabled();
    if (ok && xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        const uint32_t sp = (uint32_t)__builtin_frame_address(0);
        const uint32_t base = (uint32_t)pxTaskGetStackStart(NULL);
        if (sp > base && sp - base < HT_MIN_STACK)
            ok = false;
    }
    if (!ok && s_state != 1)   /* state 1: the drain task's own TCB/stack, never meant to be traced */
        __atomic_fetch_add(&s_drops, 1, __ATOMIC_RELAXED);
    return ok;
}

/* Hook entry points: IRAM, noinline (frame skip in ht_event). */
#define HT_HOOK IRAM_ATTR __attribute__((noinline))

extern "C" {

HT_HOOK void instrument_heaptrace_alloc(void *p, size_t size)
{
    if (p == NULL || !esp_ptr_internal(p))
        return;
    if (!ht_ctx_ok())
        return;
    ht_event('a', p, size, NULL);
    s_events++;
}

HT_HOOK void instrument_heaptrace_realloc_pre(void)
{
    if (xPortInIsrContext() || !xPortCanYield() || !spi_flash_cache_enabled())
        return;
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)   /* same stack margin as ht_ctx_ok, no drop: no line */
    {
        const uint32_t sp = (uint32_t)__builtin_frame_address(0);
        const uint32_t base = (uint32_t)pxTaskGetStackStart(NULL);
        if (sp > base && sp - base < HT_MIN_STACK)
            return;
    }
    ht_realloc_pre();
    s_events++;
}

HT_HOOK void instrument_heaptrace_realloc(void *newp, void *oldp, size_t size)
{
    bool newi = newp != NULL && esp_ptr_internal(newp);
    bool oldi = oldp != NULL && esp_ptr_internal(oldp);
    if (size == 0)                     /* realloc(p, 0) frees p */
    {
        if (!oldi)
            return;
        if (!ht_ctx_ok())
            return;
        ht_event('f', oldp, 0, NULL);
        s_events++;
        return;
    }
    if (newp == NULL || (!newi && !oldi))   /* failed (block unchanged) or not internal on both sides */
        return;
    if (!ht_ctx_ok())
        return;
    if (oldp == NULL)
        ht_event('a', newp, size, NULL);            /* plain alloc (newi, as !newi && !oldi returned above) */
    else if (oldi && newi)
        ht_event('r', newp, size, oldp);
    else if (oldi)
        ht_event('o', oldp, 0, NULL);               /* moved internal -> PSRAM: own kind, written after the realloc
                                                     * (may be late against a reuse of oldp; the decoder knows) */
    else
        ht_event('a', newp, size, NULL);            /* moved PSRAM -> internal: a new internal block */
    s_events++;
}

HT_HOOK void instrument_heaptrace_free(void *p)
{
    if (p == NULL || !esp_ptr_internal(p))
        return;
    if (!ht_ctx_ok())
        return;
    ht_event('f', p, 0, NULL);
    s_events++;
}

}   /* extern "C" */

/* ------------------------------------------------------------------ marker */
bool instrument_heaptrace_enabled(void)
{
    return true;
}

void instrument_heaptrace_mark(const char *name)
{
    if (xPortInIsrContext() || !xPortCanYield())
        return;
    uint8_t core = (uint8_t)xPortGetCoreID();
    TaskHandle_t me = ht_me();
    if (!ht_enter(core, me))
        return;
    ht_line_t l;
    l.n = 0;
    ht_str(&l, "[HT];m;");
    ht_dec(&l, ht_ms());
    ht_c(&l, ';');
    ht_name(&l, name ? name : "", 32);
    ht_str(&l, ";ifree;");
    ht_dec(&l, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    ht_str(&l, ";iblk;");
    ht_dec(&l, (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    ht_str(&l, ";itotal;");
    ht_dec(&l, (uint32_t)heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    ht_str(&l, ";drops;");
    ht_dec(&l, s_drops);
    ht_str(&l, ";waitms;");
    ht_dec(&l, (uint32_t)(s_wait_ticks * portTICK_PERIOD_MS));
    ht_c(&l, '\n');
    ht_emit(&l, core);
    ht_leave(core, me);
}

/* ------------------------------------------------------------------ wrappers without INSTR_HEAPWATCH
 * With INSTR_HEAPWATCH the wrappers live in instrument_heap.cpp and carry the hook calls. Without it nothing
 * defined __wrap_heap_caps_* (the commands there only print "unavailable"), so a build with the --wrap link
 * flags failed to link; these wrappers close that gap for the trace-only configuration. */
#if !defined(INSTR_HEAPWATCH)
extern "C" {
void *__real_heap_caps_malloc(size_t size, uint32_t caps);
void *__real_heap_caps_calloc(size_t n, size_t size, uint32_t caps);
void *__real_heap_caps_realloc(void *ptr, size_t size, uint32_t caps);
void  __real_heap_caps_free(void *ptr);
void *__real_heap_caps_malloc_default(size_t size);
void *__real_heap_caps_realloc_default(void *ptr, size_t size);

IRAM_ATTR void *__wrap_heap_caps_malloc(size_t size, uint32_t caps)
{
    void *p = __real_heap_caps_malloc(size, caps);
    instrument_heaptrace_alloc(p, size);
    return p;
}

IRAM_ATTR void *__wrap_heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
    void *p = __real_heap_caps_calloc(n, size, caps);
    instrument_heaptrace_alloc(p, n * size);
    return p;
}

IRAM_ATTR void *__wrap_heap_caps_malloc_default(size_t size)
{
    void *p = __real_heap_caps_malloc_default(size);
    instrument_heaptrace_alloc(p, size);
    return p;
}

IRAM_ATTR void *__wrap_heap_caps_realloc(void *ptr, size_t size, uint32_t caps)
{
    instrument_heaptrace_realloc_pre();
    void *p = __real_heap_caps_realloc(ptr, size, caps);
    instrument_heaptrace_realloc(p, ptr, size);
    return p;
}

IRAM_ATTR void *__wrap_heap_caps_realloc_default(void *ptr, size_t size)
{
    instrument_heaptrace_realloc_pre();
    void *p = __real_heap_caps_realloc_default(ptr, size);
    instrument_heaptrace_realloc(p, ptr, size);
    return p;
}

IRAM_ATTR void __wrap_heap_caps_free(void *ptr)
{
    instrument_heaptrace_free(ptr);
    __real_heap_caps_free(ptr);
}
}
#endif  /* !INSTR_HEAPWATCH */

#endif  /* INSTR_HEAPTRACE_ON */
