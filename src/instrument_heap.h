/**
 * TEMPORARY MEASUREMENT SCAFFOLDING (OTA-LOWHEAP-01) -- not intended for upstream.
 * Included from instrument.h on ESP32 INSTRUMENT builds only.
 *
 * Internal-heap tracking by link-time wrappers around heap_caps_* (needs
 * -DINSTR_HEAPWATCH=1 and the -Wl,--wrap=heap_caps_{malloc,calloc,realloc,free,
 * malloc_default,realloc_default} flags). Without INSTR_HEAPWATCH each command
 * prints `[INSTR-HW];unavailable`. Tracking is armed from the first allocation.
 * The wrappers live in IRAM and only call the real function when running in an ISR or with interrupts
 * masked (skips are counted, `isrskips`); a free in that state leaves a stale live entry.
 *
 * Log lines (';' separated, like the other [INSTR-*] lines):
 *   [INSTR-STEP];t_ms;<ms>;dir;down|up;ifree;<B>;iblk;<B>;task;<n>;site;<id|->;size;<B>;op;alloc|free|blk;own;<name|->
 *   [INSTR-OWN];owner;<name>;live;<B>;blocks;<n>;peak;<B>;allocs;<n>;frees;<n>;lo;0x..;hi;0x..   (--heapowners)
 *   [INSTR-OWN];sum;tracked;<B>;untracked;<B>;free;<B>;largest;<B>;drops;<n>
 *     lo/hi = lowest/highest address of the owner's live blocks. peak/allocs/frees restart with "reset".
 *
 * Limits of the live entry (8 B): owner 4 bit, site 8 bit (site table <= 256), size 20 bit (1 MB - 4).
 * -DHW_OWNER_ONLY=1 builds without backtraces and site table (live table + owners only).
 * Site slots with live == 0 are recycled once the table is full (counted as `recycles`).
 *   [INSTR-SITE];id;<n>;pcs;0x..,0x..,0x..,0x..;task;<n>;live;<n>;bytes;<B>;allocs;<n>;frees;<n>
 *   [INSTR-MAP];region;<i>;start;0x..;end;0x..;size;<B>
 *   [INSTR-MAP];blk;addr;0x..;size;<B>;kind;used|gap;site;<id|->;own;<name|->
 *       used = tracked live block; gap = space between two tracked blocks of one heap region, minus 16 B
 *       neighbour overhead (CONFIG_HEAP_POISONING_LIGHT canaries + block header). A gap is free memory OR
 *       used memory that is not tracked (allocated before tracking, dropped inserts), never "free" for sure.
 *   [INSTR-MAP];sum;used;<B>;untracked;<B>;free;<B>;holes;<n>;largest;<B>;drops;<n>
 *   [INSTR-HW];sum;live;<n>;bytes;<B>;drops;<n>;reent;<n>;isrskips;<n>     (dump)
 *   [INSTR-GAP];sum;blocks;<n>;total;<B>;reserve_stop;0|1
 */
#pragma once

#include <stdint.h>

/** Heap owners (4 bit id in the live table). Classified from the allocating task's name at alloc time;
 *  instrument_heap_owner_push() overrides it for the calling task. Names: see kOwnName in instrument_heap.cpp. */
enum {
    INSTR_OWN_OTHER = 0, INSTR_OWN_BLE, INSTR_OWN_WIFI, INSTR_OWN_TCPIP, INSTR_OWN_LOOP, INSTR_OWN_AU,
    INSTR_OWN_CONSOLE, INSTR_OWN_IPC, INSTR_OWN_TIMER, INSTR_OWN_MAIN, INSTR_OWN_IDLE, INSTR_OWN_TLS,
    INSTR_OWN_EVT, INSTR_OWN_PROBE, INSTR_OWN_USER1, INSTR_OWN_USER2,
    INSTR_OWN_COUNT                      /* 16: the id has 4 bits */
};

/** --heapwatch on|off|dump|clear. on/off arm/disarm tracking (armed from boot by default),
 *  clear empties live table and site table, dump lists live allocations ([INSTR-HW];live...). */
void instrument_heapwatch(const char *arg);

/** --heapgap: grabs the largest free internal blocks (scheduler suspended, up to 16, >= 4 KB), then
 *  prints [INSTR-GAP];blk / ;gap (bytes between neighbouring blocks, < 2 KB) / ;sum after resuming. */
void instrument_heapgap(void);

/** "on [bytes]" | "off": [INSTR-STEP] events when internal free crosses a step boundary
 *  (default 5120 B from boot, 1 KB hysteresis, minimum 1024). Empty arg prints the setting. */
void instrument_heapstep(const char *arg);

/** "live" (sites with live > 0, by live bytes desc) | "all": [INSTR-SITE] lines. */
void instrument_heapsites(const char *arg);

/** "" | "reset": one [INSTR-OWN] line per owner with allocations plus a sum line. reset restarts peak/allocs/frees. */
void instrument_heapowners(const char *arg);

/** Live tracked internal allocations sorted by address plus the gaps between them: [INSTR-MAP]. */
void instrument_heapmap(void);

/** Drain the step-event ring and sample the largest free block. Called from instrument_note_loop_tick(). */
void instrument_heap_tick(void);

#ifdef __cplusplus
extern "C" {
#endif
/** Tag the calling task's allocations with `owner` (INSTR_OWN_*) until the matching pop. Nestable (4 deep),
 *  at most 4 tasks at a time; excess calls are ignored. No-ops without INSTR_HEAPWATCH. */
void instrument_heap_owner_push(uint8_t owner);
void instrument_heap_owner_pop(void);
/** Reset the low-water mark of internal free to the current value and return it (also without INSTR_HEAPWATCH). */
uint32_t instrument_heap_lowmark_reset(void);
/** Lowest internal free since the last reset. Without INSTR_HEAPWATCH nothing is tracked and this returns
 *  0xFFFFFFFF (= unknown); the IDF since-boot minimum is deliberately not substituted, it is no window minimum. */
uint32_t instrument_heap_lowmark_get(void);
#ifdef __cplusplus
}
#endif
