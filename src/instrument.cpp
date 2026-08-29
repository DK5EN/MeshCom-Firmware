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

#if INSTRUMENT_ENABLED

#if defined(ESP32)
#include <esp_heap_caps.h>
#else
#include <malloc.h>
extern int dbgHeapTotal(void);   // nrf52_main.cpp: __HeapLimit - __HeapBase
extern int dbgHeapUsed(void);    // mallinfo().uordblks
#endif
#include "printfdeb_functions.h"

/* Provided by src/t-deck/lv_obj_functions.cpp, which owns these objects.
 * Only linked for the T-Deck variants -- guarded identically there. */
#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)
extern uint32_t instrument_msg_list_children(void);
extern uint32_t instrument_persisted_msg_count(void);
extern uint32_t instrument_active_tab_bubble_count(void);
extern int      map_point_count;
#endif

static uint32_t s_flush_n    = 0;
static uint64_t s_flush_us   = 0;
static uint32_t s_flush_max  = 0;

static uint32_t s_loop_n     = 0;
static uint64_t s_loop_us    = 0;
static uint32_t s_loop_max   = 0;
static uint32_t s_loop_last  = 0;

void instrument_note_flush(uint32_t us)
{
    s_flush_n++;
    s_flush_us += us;
    if (us > s_flush_max)
        s_flush_max = us;
}

void instrument_note_loop_tick(void)
{
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
    }

    s_loop_last = now;
}

void instrument_reset(void)
{
    s_flush_n = 0; s_flush_us = 0; s_flush_max = 0;
    s_loop_n  = 0; s_loop_us  = 0; s_loop_max  = 0;
    s_loop_last = 0;
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

#endif  /* INSTRUMENT_ENABLED */
