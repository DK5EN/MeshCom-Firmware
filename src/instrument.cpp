/**
 * TEMPORARY MEASUREMENT SCAFFOLDING -- see src/instrument.h for rationale and
 * removal instructions.
 *
 * Output uses ';' as field separator, matching the existing [HEAP]/[PSRM]
 * lines: printfdebRewriteFormat() renders ';' as a space in normal mode and as
 * a real separator under `--debug csv`, so a scripted run gets parseable CSV
 * from the same build a human reads comfortably.
 */

#include "instrument.h"
#include "configuration_global.h"

#if INSTRUMENT_ENABLED

#if defined(ESP32)
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <WiFi.h>
#else
#include <malloc.h>
extern int dbgHeapTotal(void);   // nrf52_main.cpp: __HeapLimit - __HeapBase
extern int dbgHeapUsed(void);    // mallinfo().uordblks
#endif
#include "printfdeb_functions.h"
#include <string.h>

/* Provided by src/t-deck/lv_obj_functions.cpp, which owns these objects.
 * Only linked for the T-Deck variants -- guarded identically there. */
#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)
extern uint32_t instrument_msg_list_children(void);
extern uint32_t instrument_persisted_msg_count(void);
extern uint32_t instrument_active_tab_bubble_count(void);
extern uint32_t instrument_tab_count(void);
extern uint32_t instrument_tab_msg_total(void);
extern int      map_point_count;
#endif

/* --airgap on|off (plan stage 0.5): RAM-only, never persisted -- see the
 * extern declaration and doc comment in instrument.h. */
bool bAirgap = false;

static uint32_t s_flush_n    = 0;
static uint64_t s_flush_us   = 0;
static uint32_t s_flush_max  = 0;

static uint32_t s_loop_n     = 0;
static uint64_t s_loop_us    = 0;
static uint32_t s_loop_max   = 0;
static uint32_t s_loop_last  = 0;

/* TM-13: sections, keyed by the literal's address. */
/* TM-13 slot capacity. Was 16, raised to 48 on 2026-09-17 while instrumenting
 * ETH-03. The tree now carries 42 distinct INSTR_SECTION() names and slots are
 * handed out first-come for the life of a boot (instrument_reset() clears the
 * counts but keeps the name-to-slot binding, by design). Sixteen was therefore
 * not a budget but a silent cutoff: instrument_note_section() checks
 * `i < INSTR_SECTION_SLOTS` below and, when the table is full, simply returns
 * -- no warning, no crash, just a section that never appears in the report.
 *
 * On the RAK4631 gateway path at least nine sections run every loop pass
 * before the EXTUDP block is reached, plus five more when bGATEWAY is on, so
 * the four new extudp_* / webserver_loop probes would very plausibly have been
 * the ones cut. A measurement that is missing precisely the rows you added is
 * worse than no measurement, because it reads as "the section never ran".
 *
 * Costs shipping images nothing: this entire file is inside
 * #if INSTRUMENT_ENABLED. On a measurement build it is 32 extra SectionStat
 * entries, 20 bytes each, ~640 B. */
#define INSTR_SECTION_SLOTS 48
static const uint32_t INSTR_GAP_REPORT_US = 250000;   /* a loop gap this long names its section */
struct SectionStat { const char *name; uint32_t n; uint64_t us; uint32_t max; };
static SectionStat  s_sect[INSTR_SECTION_SLOTS];
static const char  *s_iter_worst_name = NULL;    /* longest section since the last tick */
static uint32_t     s_iter_worst_us   = 0;
static uint32_t     s_iter_sect_us    = 0;       /* time inside any section since the last tick */
static uint32_t     s_gap_reports     = 0;

#if defined(ESP32)
/* CDC-01 (2026-09-05): a measurement that only shows while the USB host is
 * away (cable pulled) cannot be read live, and opening the port again resets
 * the chip. RTC slow memory survives that reset (not a power cycle), so the
 * loop-gap evidence is mirrored there and reported once at the next boot
 * ([INSTR-PREV]). Written on every tick: three word stores, negligible. */
#include <esp_attr.h>
struct InstrRtcCarry
{
    uint32_t magic;
    uint32_t gaps;
    uint32_t loop_max_us;
    uint32_t loop_n;
    uint32_t up_ms;
    uint32_t worst_gap_ms;     /* the longest gap above the threshold ... */
    char     worst_gap_in[12]; /* ... and the section it was attributed to */
};
static const uint32_t INSTR_RTC_MAGIC = 0x43444331;   /* 'CDC1' */
RTC_NOINIT_ATTR static struct InstrRtcCarry s_rtc;
#endif

void instrument_note_section(const char *name, uint32_t us)
{
    int i;
    for (i = 0; i < INSTR_SECTION_SLOTS; i++)
    {
        if (s_sect[i].name == name)
            break;
        if (s_sect[i].name == NULL)
        {
            s_sect[i].name = name;
            break;
        }
    }
    if (i < INSTR_SECTION_SLOTS)
    {
        s_sect[i].n++;
        s_sect[i].us += us;
        if (us > s_sect[i].max)
            s_sect[i].max = us;
    }
    s_iter_sect_us += us;
    if (us > s_iter_worst_us)
    {
        s_iter_worst_us = us;
        s_iter_worst_name = name;
    }
}

void instrument_note_flush(uint32_t us)
{
    s_flush_n++;
    s_flush_us += us;
    if (us > s_flush_max)
        s_flush_max = us;
}

/* ---- T-Deck Plus heap/WiFi investigation: [INSTR-HEAP], alloc-fail capture, --wifips ---- */
static volatile uint32_t s_af_n = 0, s_af_size = 0, s_af_caps = 0;
static const char *volatile s_af_fn = NULL;
static uint32_t s_heaplog_s = 30;
static uint32_t s_heaplog_last_ms = 0;

void instrument_note_alloc_fail(size_t size, uint32_t caps, const char *fn)
{
    /* any task, possibly with the heap locked: plain stores only */
    s_af_n = s_af_n + 1u;
    s_af_size = (uint32_t)size;
    s_af_caps = caps;
    s_af_fn = fn;
}

#if defined(ESP32)
static void instr_alloc_fail_cb(size_t size, uint32_t caps, const char *fn)
{
    instrument_note_alloc_fail(size, caps, fn);
}
#endif

void instrument_report_heapwifi(void)
{
#if defined(ESP32)
    const uint32_t ci = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    wifi_ps_type_t ps = WIFI_PS_NONE;
    int psv = (esp_wifi_get_ps(&ps) == ESP_OK) ? (int)ps : -1;
    const char *fn = s_af_fn;
    #if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)
    printfdeb("[INSTR-HEAP];up_s;%lu;ifree;%u;imin;%u;iblk;%u;dfree;%u;dblk;%u;pfree;%u;pblk;%u;afail;%u;afsz;%u;afcaps;%x;affn;%s;pmsgs;%u;tabs;%u;tabmsgs;%u;wifi;%i;rssi;%i;ps;%i\n",
    #else
    printfdeb("[INSTR-HEAP];up_s;%lu;ifree;%u;imin;%u;iblk;%u;dfree;%u;dblk;%u;pfree;%u;pblk;%u;afail;%u;afsz;%u;afcaps;%x;affn;%s;wifi;%i;rssi;%i;ps;%i\n",
    #endif
              (unsigned long)(millis() / 1000UL),
              (unsigned)heap_caps_get_free_size(ci),
              (unsigned)heap_caps_get_minimum_free_size(ci),
              (unsigned)heap_caps_get_largest_free_block(ci),
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
              (unsigned)s_af_n, (unsigned)s_af_size, (unsigned)s_af_caps,
              fn != NULL ? fn : "-"
    #if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)
              , (unsigned)instrument_persisted_msg_count(),
              (unsigned)instrument_tab_count(),
              (unsigned)instrument_tab_msg_total()
    #endif
              , (int)WiFi.status(), (int)WiFi.RSSI(), psv);
#else
    printfdeb("[INSTR-HEAP];not_available_on_this_board\n");
#endif
}

void instrument_heaplog_set(uint32_t s)
{
    s_heaplog_s = s;
    s_heaplog_last_ms = millis();
}

static void instrument_heaplog_tick(void)
{
    if (s_heaplog_s == 0)
        return;
    uint32_t now = millis();
    if ((uint32_t)(now - s_heaplog_last_ms) >= s_heaplog_s * 1000UL)
    {
        s_heaplog_last_ms = now;
        instrument_report_heapwifi();
    }
}

void instrument_wifips(const char *mode)
{
#if defined(ESP32)
    wifi_ps_type_t want;
    if (mode != NULL && strncasecmp(mode, "none", 4) == 0)
        want = WIFI_PS_NONE;
    else if (mode != NULL && strncasecmp(mode, "min", 3) == 0)
        want = WIFI_PS_MIN_MODEM;
    else if (mode != NULL && strncasecmp(mode, "max", 3) == 0)
        want = WIFI_PS_MAX_MODEM;
    else
    {
        printfdeb("[INSTR-WIFIPS];err;usage --wifips none|min|max\n");
        return;
    }
    /* WIFI-PS-01: IDF aborts (RESET_REASON=4 PANIC) on PS none while the BT
     * controller runs (WiFi/BLE coexistence needs modem sleep). Refuse instead. */
    if (want == WIFI_PS_NONE && esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE)
    {
        printfdeb("[INSTR-WIFIPS];refused;none;reason;bt_enabled\n");
        return;
    }
    esp_err_t rc = esp_wifi_set_ps(want);
    wifi_ps_type_t now = WIFI_PS_NONE;
    int nv = (esp_wifi_get_ps(&now) == ESP_OK) ? (int)now : -1;
    printfdeb("[INSTR-WIFIPS];set;%s;rc;%i;now;%i\n", mode, (int)rc, nv);
#else
    printfdeb("[INSTR-WIFIPS];not_available_on_this_board\n");
#endif
}

void instrument_note_loop_tick(void)
{
    instrument_heaplog_tick();

    uint32_t now = micros();

    /* Skip the very first tick: there is no previous timestamp to subtract,
     * and skip an implausible gap after a reset so one outlier cannot poison
     * the maximum. */
    if (s_loop_last != 0)
    {
        uint32_t d = now - s_loop_last;      /* wraps correctly on uint32 */
        s_loop_n++;
        s_loop_us += d;
        if (d > s_loop_max)
            s_loop_max = d;
#if defined(ESP32)
        if (s_rtc.magic == INSTR_RTC_MAGIC)
        {
            s_rtc.loop_n++;
            s_rtc.up_ms = now / 1000;
            if (d > s_rtc.loop_max_us)
                s_rtc.loop_max_us = d;
            if (d > INSTR_GAP_REPORT_US)
            {
                s_rtc.gaps++;
                if (d / 1000 > s_rtc.worst_gap_ms)
                {
                    s_rtc.worst_gap_ms = d / 1000;
                    const char *nm = s_iter_worst_name != NULL ? s_iter_worst_name : "unattributed";
                    strncpy(s_rtc.worst_gap_in, nm, sizeof(s_rtc.worst_gap_in) - 1);
                    s_rtc.worst_gap_in[sizeof(s_rtc.worst_gap_in) - 1] = '\0';
                }
            }
        }
#endif
        if (d > INSTR_GAP_REPORT_US)
        {
            /* Attribute the gap: the longest section of that iteration, or
             * "unattributed" when no instrumented section ran (the blocker
             * is outside every section -- that is a finding too). */
            s_gap_reports++;
            /* sections_ms << gap ms means the blocker sits in code no
             * INSTR_SECTION covers yet -- add one there. */
            printfdeb("[INSTR-LOOP];gap;ms;%lu;in;%s;section_ms;%lu;sections_ms;%lu\n",
                      (unsigned long)(d / 1000),
                      s_iter_worst_name != NULL ? s_iter_worst_name : "unattributed",
                      (unsigned long)(s_iter_worst_us / 1000),
                      (unsigned long)(s_iter_sect_us / 1000));
        }
    }

    s_loop_last = now;
    s_iter_worst_us = 0;
    s_iter_worst_name = NULL;
    s_iter_sect_us = 0;
}

void instrument_report_prev_boot(void)
{
#if defined(ESP32)
    /* boot-time alloc-fail capture; fw_update_net's per-job arming replaces the
     * slot later and forwards to instrument_note_alloc_fail() */
    heap_caps_register_failed_alloc_callback(instr_alloc_fail_cb);
    /* Raw Serial.printf, not printfdeb: this is a bench marker that must
     * survive --debug off (see the FL-01 note in tdeck_main.cpp). */
    if (s_rtc.magic == INSTR_RTC_MAGIC)
        Serial.printf("[INSTR-PREV];valid;1;gaps;%lu;loop_max_us;%lu;loop_n;%lu;up_ms;%lu;threshold_ms;%lu;worst_ms;%lu;in;%s\n",
                      (unsigned long)s_rtc.gaps, (unsigned long)s_rtc.loop_max_us,
                      (unsigned long)s_rtc.loop_n, (unsigned long)s_rtc.up_ms,
                      (unsigned long)(INSTR_GAP_REPORT_US / 1000),
                      (unsigned long)s_rtc.worst_gap_ms,
                      s_rtc.worst_gap_in[0] ? s_rtc.worst_gap_in : "-");
    else
        Serial.printf("[INSTR-PREV];valid;0\n");
    s_rtc.magic = INSTR_RTC_MAGIC;
    s_rtc.gaps = 0; s_rtc.loop_max_us = 0; s_rtc.loop_n = 0; s_rtc.up_ms = 0;
    s_rtc.worst_gap_ms = 0; s_rtc.worst_gap_in[0] = '\0';
#endif
}

void instrument_reset(void)
{
    s_flush_n = 0; s_flush_us = 0; s_flush_max = 0;
    s_loop_n  = 0; s_loop_us  = 0; s_loop_max  = 0;
    s_loop_last = 0;
    for (int i = 0; i < INSTR_SECTION_SLOTS; i++)
    {
        s_sect[i].n = 0; s_sect[i].us = 0; s_sect[i].max = 0;   /* keep the name: slot order is stable */
    }
    s_gap_reports = 0;
#if defined(ESP32)
    /* CDC-01: the RTC carry starts here too, so an armed measurement does
     * not include the boot-time gap (WiFi/GPS start, ~3 s). */
    s_rtc.magic = INSTR_RTC_MAGIC;
    s_rtc.gaps = 0; s_rtc.loop_max_us = 0; s_rtc.loop_n = 0; s_rtc.up_ms = 0;
    s_rtc.worst_gap_ms = 0; s_rtc.worst_gap_in[0] = '\0';
#endif
    printfdeb("[INSTR];reset\n");
}

void instrument_report_heap(const char *tag)
{
    /* All four internal figures are MALLOC_CAP_INTERNAL on purpose. The
     * largest-free-block is the discriminating one: fragmentation starves
     * allocations while the free total still looks healthy. */
#if defined(ESP32)
    printfdeb("[INSTR-HEAP];%s;int_free;%u;int_min;%u;int_largest;%u;psram_free;%u;psram_largest;%u\n",
              (tag && *tag) ? tag : "-",
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
              (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
#else
    {
        struct mallinfo mi = mallinfo();
        int total = dbgHeapTotal();
        printfdeb("[INSTR-HEAP];%s;int_free;%d;int_min;%d;int_largest;%d;psram_free;0;psram_largest;0\n",
                  tag, total - (int)mi.uordblks, -1, (int)mi.fordblks);
    }
#endif
}

void instrument_heapeat(uint32_t kb)
{
#if defined(ESP32)
    static void   **s_eat = NULL;      /* pointer table, PSRAM when present */
    static uint32_t s_eat_n = 0;       /* chunks held */
    static uint32_t s_eat_cap = 0;     /* table capacity */
    if(kb == 0)
    {
        for(uint32_t i = 0; i < s_eat_n; i++)
            heap_caps_free(s_eat[i]);
        s_eat_n = 0;
    }
    else
    {
        uint32_t want = s_eat_n + kb * 4;      /* 4 chunks of 256 B per KB */
        if(want > s_eat_cap)
        {
            void **nt = (void **)heap_caps_malloc(want * sizeof(void *), MALLOC_CAP_SPIRAM);
            if(nt == NULL)
                nt = (void **)heap_caps_malloc(want * sizeof(void *), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if(nt == NULL)
            {
                printfdeb("[INSTR-HEAPEAT];table_alloc_failed\n");
                return;
            }
            if(s_eat_n)
                memcpy(nt, s_eat, s_eat_n * sizeof(void *));
            if(s_eat)
                heap_caps_free(s_eat);
            s_eat = nt;
            s_eat_cap = want;
        }
        for(uint32_t i = 0; i < kb * 4; i++)
        {
            void *p = heap_caps_malloc(256, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if(p == NULL)
                break;
            s_eat[s_eat_n++] = p;
        }
    }
    printfdeb("[INSTR-HEAPEAT];held_kb;%u;ifree;%u\n", (unsigned)(s_eat_n / 4),
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
#else
    (void)kb;
    printfdeb("[INSTR-HEAPEAT];not_available_on_this_board\n");
#endif
}

void instrument_report_timing(void)
{
    uint32_t flush_avg = (s_flush_n > 0) ? (uint32_t)(s_flush_us / s_flush_n) : 0;
    uint32_t loop_avg  = (s_loop_n  > 0) ? (uint32_t)(s_loop_us  / s_loop_n)  : 0;

    printfdeb("[INSTR-FLUSH];n;%u;total_us;%u;avg_us;%u;max_us;%u\n",
              (unsigned)s_flush_n,
              (unsigned)s_flush_us,
              (unsigned)flush_avg,
              (unsigned)s_flush_max);

    printfdeb("[INSTR-LOOP];n;%u;total_us;%u;avg_us;%u;max_us;%u\n",
              (unsigned)s_loop_n,
              (unsigned)s_loop_us,
              (unsigned)loop_avg,
              (unsigned)s_loop_max);

    /* TM-13: one line per instrumented section that ran since the reset. */
    for (int i = 0; i < INSTR_SECTION_SLOTS && s_sect[i].name != NULL; i++)
    {
        if (s_sect[i].n == 0)
            continue;
        printfdeb("[INSTR-SECT];%s;n;%u;total_us;%u;avg_us;%u;max_us;%u\n",
                  s_sect[i].name,
                  (unsigned)s_sect[i].n,
                  (unsigned)s_sect[i].us,
                  (unsigned)(s_sect[i].us / s_sect[i].n),
                  (unsigned)s_sect[i].max);
    }
    printfdeb("[INSTR-GAPS];n;%u;threshold_ms;%u\n", (unsigned)s_gap_reports, (unsigned)(INSTR_GAP_REPORT_US / 1000));
}

void instrument_report_gui(void)
{
#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)
    /* msg_list_children is the view; persisted/active_tab are the model.
     * H1 predicts the first grows without bound while the second stays put. */
    printfdeb("[INSTR-GUI];msg_list_children;%u;active_tab_bubbles;%u;persisted_msgs;%u;map_points;%i\n",
              (unsigned)instrument_msg_list_children(),
              (unsigned)instrument_active_tab_bubble_count(),
              (unsigned)instrument_persisted_msg_count(),
              map_point_count);
#else
    printfdeb("[INSTR-GUI];not_available_on_this_board\n");
#endif
}

#if defined(ESP32)
/* ---- R1-F: OnRxDone print/total split and IRQ->read latency ---------------- */
#define ONRX_RING   16
#define ONRX_PHASES 12

struct OnrxRec
{
    uint32_t seq, total_us, print_us, uart_us, tcp_us, bytes, len;
    int32_t  irq_to_read_us;     /* -1 = no ISR stamp for this frame */
    uint8_t  nph;
    const char *ph_name[ONRX_PHASES];
    uint32_t    ph_us[ONRX_PHASES];
};

volatile uint32_t instr_irq_us         = 0;
volatile int32_t  instr_irq_to_read_us = -1;
volatile bool     instr_onrx_active    = false;
void             *instr_onrx_task      = NULL;

static OnrxRec  s_onrx_ring[ONRX_RING];
static uint32_t s_onrx_head = 0;          /* next pop  */
static uint32_t s_onrx_cnt  = 0;
static uint32_t s_onrx_lost = 0;
static uint32_t s_onrx_seq  = 0;
static OnrxRec  s_onrx_cur;
static uint32_t s_onrx_t0   = 0;
static uint32_t s_onrx_tlast = 0;
static portMUX_TYPE s_onrx_mux = portMUX_INITIALIZER_UNLOCKED;

/* Consume-once: the stamp is taken with one atomic exchange, so a stamp the ISR
 * writes after this point is kept for the next frame. No stamp -> -1. */
void instrument_irq_read(void)
{
    uint32_t st = __atomic_exchange_n(&instr_irq_us, 0u, __ATOMIC_ACQ_REL);
    if (st)
    {
        int32_t d = (int32_t)(micros() - st);
        instr_irq_to_read_us = d < 0 ? 0 : d;   /* st carries |1: may be 1 us ahead */
    }
    else
        instr_irq_to_read_us = -1;
}

void instrument_irq_drop(void)
{
    (void)__atomic_exchange_n(&instr_irq_us, 0u, __ATOMIC_ACQ_REL);
    instr_irq_to_read_us = -1;
}

void instrument_onrx_begin(uint16_t len)
{
    memset(&s_onrx_cur, 0, sizeof(s_onrx_cur));
    s_onrx_cur.seq = ++s_onrx_seq;
    s_onrx_cur.len = len;
    s_onrx_cur.irq_to_read_us = instr_irq_to_read_us;
    instr_irq_to_read_us = -1;
    instr_onrx_task = (void *)xTaskGetCurrentTaskHandle();
    s_onrx_t0 = s_onrx_tlast = micros();
    instr_onrx_active = true;
}

void instrument_onrx_phase(const char *name)
{
    if (!instr_onrx_active)
        return;
    uint32_t now = micros();
    if (s_onrx_cur.nph < ONRX_PHASES)
    {
        s_onrx_cur.ph_name[s_onrx_cur.nph] = name;
        s_onrx_cur.ph_us[s_onrx_cur.nph]   = now - s_onrx_tlast;
        s_onrx_cur.nph++;
    }
    s_onrx_tlast = now;
}

void instrument_onrx_print(uint32_t print_us, uint32_t uart_us, uint32_t tcp_us, uint32_t bytes)
{
    if (!instr_onrx_active || (void *)xTaskGetCurrentTaskHandle() != instr_onrx_task)
        return;
    s_onrx_cur.print_us += print_us;
    s_onrx_cur.uart_us  += uart_us;
    s_onrx_cur.tcp_us   += tcp_us;
    s_onrx_cur.bytes    += bytes;
}

void instrument_onrx_end(void)
{
    if (!instr_onrx_active)
        return;
    instrument_onrx_phase("exit");
    s_onrx_cur.total_us = micros() - s_onrx_t0;
    instr_onrx_active = false;

    portENTER_CRITICAL(&s_onrx_mux);
    if (s_onrx_cnt == ONRX_RING)
    {
        s_onrx_head = (s_onrx_head + 1) % ONRX_RING;
        s_onrx_cnt--;
        s_onrx_lost++;
    }
    s_onrx_ring[(s_onrx_head + s_onrx_cnt) % ONRX_RING] = s_onrx_cur;
    s_onrx_cnt++;
    portEXIT_CRITICAL(&s_onrx_mux);
}

void instrument_onrx_drain(void)
{
    /* two lines per tick at most: the lines themselves are slow on a UART */
    for (int k = 0; k < 2; k++)
    {
        OnrxRec r;
        uint32_t lost;
        portENTER_CRITICAL(&s_onrx_mux);
        if (s_onrx_cnt == 0)
        {
            portEXIT_CRITICAL(&s_onrx_mux);
            return;
        }
        r = s_onrx_ring[s_onrx_head];
        s_onrx_head = (s_onrx_head + 1) % ONRX_RING;
        s_onrx_cnt--;
        lost = s_onrx_lost;
        portEXIT_CRITICAL(&s_onrx_mux);

        char ph[ONRX_PHASES * 16 + 4];
        size_t n = 0;
        ph[0] = 0;
        for (uint8_t i = 0; i < r.nph && n < sizeof(ph) - 16; i++)
            n += snprintf(ph + n, sizeof(ph) - n, "%s%s:%lu", i ? "," : "", r.ph_name[i], (unsigned long)r.ph_us[i]);

        printfdeb("[INS] ONRX seq=%lu total_us=%lu print_us=%lu bytes=%lu len=%lu irq_to_read_us=%ld ph=%s uart_us=%lu tcp_us=%lu lost=%lu txbuf=%lu/%d\n",
                  (unsigned long)r.seq, (unsigned long)r.total_us, (unsigned long)r.print_us,
                  (unsigned long)r.bytes, (unsigned long)r.len, (long)r.irq_to_read_us,
                  ph, (unsigned long)r.uart_us, (unsigned long)r.tcp_us, (unsigned long)lost,
                  (unsigned long)console_txbuf_bytes, console_txbuf_ok);
    }
}

void instrument_onrx_boot(void)
{
    Serial.printf("[INS] UARTTXBUF bytes=%lu ok=%d\n", (unsigned long)console_txbuf_bytes, console_txbuf_ok);
    Serial.printf("[INS] ONRX_READY\n");
}

/* ---------------------------------------------------------------------------
 * --heapwatch / --heapgap: find the long-lived allocation that splits the
 * largest internal block. See src/instrument.h.
 * ------------------------------------------------------------------------- */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <esp_debug_helpers.h>
#include <soc/soc_memory_layout.h>

#if defined(INSTR_HEAPWATCH)

#define HW_ENTRIES 256
#define HW_PCS 5

typedef struct {
    void    *ptr;
    uint32_t size;
    uint32_t caps;
    char     task[9];
    uint32_t pc[HW_PCS];
} hw_entry_t;

static hw_entry_t s_hw[HW_ENTRIES];
static portMUX_TYPE s_hw_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_hw_armed = false;
static volatile uint32_t s_hw_live = 0;
static uint32_t s_hw_drops = 0;
static volatile uint8_t s_hw_busy[2] = {0, 0};   /* per-core re-entry guard */

extern "C" {
void *__real_heap_caps_malloc(size_t size, uint32_t caps);
void *__real_heap_caps_calloc(size_t n, size_t size, uint32_t caps);
void *__real_heap_caps_realloc(void *ptr, size_t size, uint32_t caps);
void  __real_heap_caps_free(void *ptr);
void *__real_heap_caps_malloc_default(size_t size);
void *__real_heap_caps_realloc_default(void *ptr, size_t size);
}

/* Remove the entry for ptr (caller holds the lock). */
static inline void hw_remove_locked(void *ptr)
{
    for (uint32_t i = 0; i < HW_ENTRIES; i++)
        if (s_hw[i].ptr == ptr)
        {
            s_hw[i].ptr = NULL;
            s_hw_live--;
            return;
        }
}

static inline void hw_untrack(void *ptr)
{
    if (ptr == NULL || s_hw_live == 0)
        return;
    portENTER_CRITICAL_SAFE(&s_hw_mux);
    hw_remove_locked(ptr);
    portEXIT_CRITICAL_SAFE(&s_hw_mux);
}

/* Record a successful allocation. Runs outside the lock until the table write. */
static void hw_track(void *ptr, size_t size, uint32_t caps)
{
    if (ptr == NULL || !s_hw_armed || xPortInIsrContext() || !esp_ptr_internal(ptr))
        return;
    uint8_t core = (uint8_t)xPortGetCoreID();
    if (core > 1 || s_hw_busy[core])
        return;
    s_hw_busy[core] = 1;

    hw_entry_t e;
    e.ptr = ptr;
    e.size = (uint32_t)size;
    e.caps = caps;
    e.task[0] = '-';
    e.task[1] = 0;
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        const char *n = pcTaskGetName(NULL);
        if (n)
        {
            strncpy(e.task, n, 8);
            e.task[8] = 0;
        }
    }
    memset(e.pc, 0, sizeof(e.pc));
    {
        esp_backtrace_frame_t f;
        esp_backtrace_get_start(&f.pc, &f.sp, &f.next_pc);   /* frame 0 = this function; skip it and the wrapper */
        f.exc_frame = NULL;
        (void)esp_backtrace_get_next_frame(&f);               /* f.pc = the wrapper's caller chain start */
        for (int i = 0; i < HW_PCS; i++)
        {
            if (!esp_backtrace_get_next_frame(&f))
                break;
            e.pc[i] = f.pc;
        }
    }

    portENTER_CRITICAL_SAFE(&s_hw_mux);
    int slot = -1;
    for (uint32_t i = 0; i < HW_ENTRIES; i++)
    {
        if (s_hw[i].ptr == ptr) { slot = (int)i; break; }          /* same block seen twice: replace */
        if (slot < 0 && s_hw[i].ptr == NULL) slot = (int)i;
    }
    if (slot >= 0)
    {
        if (s_hw[slot].ptr == NULL)
            s_hw_live++;
        s_hw[slot] = e;
    }
    else
        s_hw_drops++;
    portEXIT_CRITICAL_SAFE(&s_hw_mux);
    s_hw_busy[core] = 0;
}

extern "C" {
void *__wrap_heap_caps_malloc(size_t size, uint32_t caps)
{
    void *p = __real_heap_caps_malloc(size, caps);
    hw_track(p, size, caps);
    return p;
}

void *__wrap_heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
    void *p = __real_heap_caps_calloc(n, size, caps);
    hw_track(p, n * size, caps);
    return p;
}

void *__wrap_heap_caps_malloc_default(size_t size)
{
    void *p = __real_heap_caps_malloc_default(size);
    hw_track(p, size, MALLOC_CAP_DEFAULT);
    return p;
}

void *__wrap_heap_caps_realloc(void *ptr, size_t size, uint32_t caps)
{
    void *p = __real_heap_caps_realloc(ptr, size, caps);
    if (size == 0 || p != NULL)
        hw_untrack(ptr);                  /* old block is gone on success (or size 0 = free) */
    hw_track(p, size, caps);
    return p;
}

void *__wrap_heap_caps_realloc_default(void *ptr, size_t size)
{
    void *p = __real_heap_caps_realloc_default(ptr, size);
    if (size == 0 || p != NULL)
        hw_untrack(ptr);
    hw_track(p, size, MALLOC_CAP_DEFAULT);
    return p;
}

void __wrap_heap_caps_free(void *ptr)
{
    hw_untrack(ptr);                      /* before the real free: the address can be reused at once */
    __real_heap_caps_free(ptr);
}
}

static void hw_clear(void)
{
    portENTER_CRITICAL(&s_hw_mux);
    memset(s_hw, 0, sizeof(s_hw));
    s_hw_live = 0;
    s_hw_drops = 0;
    portEXIT_CRITICAL(&s_hw_mux);
}

void instrument_heapwatch(const char *arg)
{
    while (arg && *arg == ' ')
        arg++;
    if (arg == NULL || *arg == 0)
        arg = "";
    if (strncmp(arg, "on", 2) == 0)
    {
        s_hw_armed = false;
        hw_clear();
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
        /* No snapshot table (it would cost another 10 KB of .bss, i.e. heap): copy one entry at a time. */
        uint32_t n = 0, bytes = 0;
        for (uint32_t i = 0; i < HW_ENTRIES; i++)
        {
            hw_entry_t e;
            portENTER_CRITICAL(&s_hw_mux);
            e = s_hw[i];
            portEXIT_CRITICAL(&s_hw_mux);
            if (e.ptr == NULL)
                continue;
            n++;
            bytes += e.size;
            char pcs[HW_PCS * 11 + 1];
            size_t o = 0;
            pcs[0] = 0;
            for (int k = 0; k < HW_PCS && e.pc[k]; k++)
                o += snprintf(pcs + o, sizeof(pcs) - o, "%s0x%08lx", k ? "," : "", (unsigned long)e.pc[k]);
            printfdeb("[INSTR-HW];live;ptr;0x%08lx;size;%lu;task;%s;pc;%s\n",
                      (unsigned long)e.ptr, (unsigned long)e.size, e.task, pcs);
        }
        printfdeb("[INSTR-HW];sum;live;%lu;bytes;%lu;drops;%lu\n",
                  (unsigned long)n, (unsigned long)bytes, (unsigned long)s_hw_drops);
    }
    else
        printfdeb("[INSTR-HW];usage;on|off|dump|clear;armed;%d\n", (int)s_hw_armed);
}

#else  /* !INSTR_HEAPWATCH */

void instrument_heapwatch(const char *arg)
{
    (void)arg;
    printfdeb("[INSTR-HW];unavailable\n");
}

#endif  /* INSTR_HEAPWATCH */

#define GAP_MAX_BLOCKS 16
#define GAP_MIN_BLOCK  4096
#define GAP_MAX_GAP    2048
#define GAP_COPY       64   /* per gap; 16 x 64 B of .bss in every INSTRUMENT ESP32 build */

typedef struct { uint32_t addr; uint32_t size; } gap_blk_t;
static gap_blk_t s_gap_blk[GAP_MAX_BLOCKS];
static uint8_t   s_gap_bytes[GAP_MAX_BLOCKS][GAP_COPY];
static uint16_t  s_gap_len[GAP_MAX_BLOCKS];      /* gap after block i (0 = none / too large) */
static uint16_t  s_gap_copied[GAP_MAX_BLOCKS];

void instrument_heapgap(void)
{
    uint32_t n = 0;
    void *ptrs[GAP_MAX_BLOCKS];

    vTaskSuspendAll();
    while (n < GAP_MAX_BLOCKS)
    {
        size_t big = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (big < GAP_MIN_BLOCK)
            break;
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

    uint32_t total = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        total += s_gap_blk[i].size;
        printfdeb("[INSTR-GAP];blk;addr;0x%08lx;size;%lu\n", (unsigned long)s_gap_blk[i].addr, (unsigned long)s_gap_blk[i].size);
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
    }
    printfdeb("[INSTR-GAP];sum;blocks;%lu;total;%lu\n", (unsigned long)n, (unsigned long)total);
}
#endif  /* ESP32 */

#endif  /* INSTRUMENT_ENABLED */
