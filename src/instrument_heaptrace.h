/**
 * TEMPORARY MEASUREMENT SCAFFOLDING (OTA-LOWHEAP-01) -- not intended for upstream.
 *
 * Allocation trace over UART0: every internal-RAM heap_caps malloc/calloc/realloc/free is written as one text
 * line, so a host tool can rebuild the heap map per caller. Replaces the in-RAM table of instrument_heap.cpp
 * for measurements. Compiled only with INSTRUMENT_ENABLED && ESP32 && -DINSTR_HEAPTRACE=1 and the
 * -Wl,--wrap=heap_caps_{malloc,calloc,realloc,free,malloc_default,realloc_default} link flags.
 * INSTR_HEAPWATCH may be on or off: with it on the hooks sit in the wrappers of instrument_heap.cpp, with it
 * off instrument_heaptrace.cpp defines the wrappers itself.
 *
 * Lines (';' separated, numbers hex without 0x unless marked dec, no spaces inside fields):
 *   [HT];a;<t_ms dec>;<ptr 8hex>;<size dec>;<task8>;<pc1>;<pc2>;<pc3>            alloc (malloc/calloc)
 *   [HT];r;<t_ms>;<newptr>;<size>;<task8>;<pc1>;<pc2>;<pc3>;<oldptr>             realloc (oldptr 00000000 = plain alloc)
 *   [HT];f;<t_ms>;<ptr>;0;<task8>;<pc1>;<pc2>;<pc3>                              free (also realloc(p, 0))
 *   [HT];o;<t_ms>;<oldptr>;0;<task8>;<pc1>;<pc2>;<pc3>                           realloc moved the block internal -> PSRAM
 *   [HT];m;<t_ms>;<name>;ifree;<dec>;iblk;<dec>;drops;<dec>                      marker (instrument_heaptrace_mark)
 * pc1..pc3 = return addresses above the wrapper (wrapper's caller first), code-region normalised; the glue
 * (malloc, pvPortMalloc, _malloc_r, operator new ...) is NOT skipped here, the host tool resolves it from the ELF.
 *   [HT];i;<t_ms dec>;drain;stack;<bytes dec>                                   info: drain task started (its own
 *                                                                              stack + TCB are NOT traced)
 * Realloc between regions: internal -> PSRAM is written as "o" of the old pointer, PSRAM -> internal as "a" of
 * the new pointer, internal -> internal as "r", PSRAM -> PSRAM nothing. The "o" of a moving realloc is written
 * AFTER the real realloc (the old address is gone by then), so the host tool must tolerate a reuse in between.
 * Only internal-RAM pointers are traced. The free line is written BEFORE the real free, the alloc line after
 * the real alloc, so one address never shows alloc-alloc-free for two owners.
 *
 * Transport: a 2 KB ring of formatted lines drained by the low-priority task "httrace" (prio 1, any core),
 * started lazily by the first event after the scheduler runs and the UART0 driver is installed. Until then
 * (early boot, before Serial.begin) every line goes out directly by esp_rom_printf. Ring full: the allocating
 * task waits (vTaskDelay(1)) until there is room; at most 2 s, then the line is dropped and counted. In an ISR,
 * with interrupts masked, with the scheduler suspended, in the idle task and in the drain task itself a full ring
 * (or the context) drops the line and counts it; the count is the "drops" field of the marker.
 * An allocation made while a line is formatted or pushed is not traced (per-core/per-task guard).
 * Target boards: Serial must be UART0 (Heltec V3, T-Beam). On boards whose Serial is USB-CDC (T-Deck) the output
 * path (uart_write_bytes / esp_rom_printf on UART0) is not supported.
 * Dropped (counted in "drops") as well: hooks entered with less than HT_MIN_STACK bytes of task stack left, and
 * hooks entered while the flash cache is disabled. While the drain task is starting (or failed to start) and the
 * UART0 driver is installed, lines are dropped instead of going out by esp_rom_printf (would tear against Serial).
 * Realloc: room for one line is awaited BEFORE the real realloc, so the window in which another task could
 * reuse the old address before the "r" line is out stays small (not zero: the host tool must tolerate it).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(INSTRUMENT_ENABLED) && INSTRUMENT_ENABLED && defined(ESP32) && defined(INSTR_HEAPTRACE)
#define INSTR_HEAPTRACE_ON 1

/** True in a trace build (compile-time constant). */
bool instrument_heaptrace_enabled(void);

/** Marker line with the current internal free / largest block and the drop count. Safe to call from any task. */
void instrument_heaptrace_mark(const char *name);

/* Hooks for the heap_caps wrappers (IRAM-safe entry points; the work runs in flash only in task context). */
extern "C" {
void instrument_heaptrace_alloc(void *p, size_t size);                    /* after the real malloc/calloc */
void instrument_heaptrace_realloc_pre(void);                              /* before the real realloc */
void instrument_heaptrace_realloc(void *newp, void *oldp, size_t size);   /* after the real realloc */
void instrument_heaptrace_free(void *p);                                  /* BEFORE the real free */
}

#else
#define INSTR_HEAPTRACE_ON 0

static inline bool instrument_heaptrace_enabled(void)                                      { return false; }
static inline void instrument_heaptrace_mark(const char *name)                              { (void)name; }
static inline void instrument_heaptrace_alloc(void *p, size_t size)                         { (void)p; (void)size; }
static inline void instrument_heaptrace_realloc_pre(void)                                   {}
static inline void instrument_heaptrace_realloc(void *newp, void *oldp, size_t size)        { (void)newp; (void)oldp; (void)size; }
static inline void instrument_heaptrace_free(void *p)                                       { (void)p; }

#endif
