# Track 2 — Draw buffers, partial vs full refresh, flush, DMA and SPI throughput on ESP32-S3

See `00-CONTEXT.md` for shared hardware/software facts; not repeated here.

## TL;DR for the coding agent

1. The repo's current combo — **one screen-sized buffer (`buf2 = NULL`), `full_refresh = 0`** — is
   **legal** in LVGL 8.3 and is not itself a source of corruption. It just means every invalidated
   rectangle is rendered and flushed in one chunk (buffer is big enough to hold any rectangle up to
   full-screen), and only the _changed_ rectangle is sent over SPI instead of the whole frame. The
   G07 pixel-vs-byte-count bug (`lv_disp_draw_buf_init` 4th arg) is **already fixed** in the current
   tree (`lv_disp_draw_buf_init(&draw_buf, buf, NULL, TFT_WIDTH * TFT_HEIGHT)` — pixel count, correct).
2. `disp_flush()` (`src/t-deck/tdeck_main.cpp:458`) already uses `area->x1/y1` and computed `w,h` for
   `setAddrWindow`/`pushColors` — it does **not** hardcode a full-frame write. So flipping
   `full_refresh` 1→0 does not by itself break the flush path. If a redraw regression appears after
   the flip, look at **invalidation gaps** (an object changed without `lv_obj_invalidate()` being
   called on its ancestor chain), not the flush function — see Track on invalidation/dirty-tracking.
3. `full_refresh = 1` masked bugs of exactly that class for a long time: with `full_refresh=1`,
   `refr_area()` always redraws `(0,0)-(hor_res-1,ver_res-1)` regardless of what was actually
   invalidated (`lv_refr.c:638-648`), so a missed/incorrect invalidation was invisible — the next
   unrelated redraw (e.g. the 500 ms battery-label tick) repainted everything anyway. Partial refresh
   removes that safety net: a missing invalidation now means the object genuinely never appears.
4. The draw buffer is single-buffered (`buf2 == NULL`), so **every** `refr_area_part()` call blocks
   on `while(draw_buf->flushing) wait_cb()` before rendering the next area
   (`lv_refr.c:709-712`) — this is a no-op today because `disp_flush()` is fully synchronous
   (`lv_disp_flush_ready()` is called before `xSemaphoreGive`), but it means **there is currently no
   parallelism between rendering and flushing at all**. This is the actual cause of "full-screen
   repaints are slow" — not a buffer-size bug, a blocking-I/O bug.
5. **Do not add DMA by only replacing `pushColors` with `pushPixelsDMA`.** The draw buffer that DMA
   reads from must live in **internal DMA-capable RAM** (`heap_caps_malloc(size, MALLOC_CAP_DMA |
MALLOC_CAP_INTERNAL)`), not PSRAM. `LV_MEM_CUSTOM_ALLOC = ps_malloc` (PSRAM) is correct for the
   **LVGL object heap** but wrong for a **DMA-flushed pixel buffer** on this IDF 4.4.x /
   `espressif32@6.6.0` toolchain: the SPI master driver will silently insert a temporary internal
   bounce buffer and `memcpy` on every transfer if the source is PSRAM and not explicitly flagged,
   which defeats the point of DMA and adds per-flush heap churn.
6. Recommended buffer regime for this 320×240×16bpp panel: **two small partial buffers**, each
   ~1/10 to 1/4 screen (15 KB–38 KB), in internal DRAM, plus `tft.initDMA()` +
   `tft.pushPixelsDMA()`/`pushImageDMA()` in `flush_cb`. This is the only combination that gives real
   non-blocking flush. A single full-screen buffer, even swapped to non-blocking DMA, still forces
   `refr_area_part()`'s single-buffer wait (`buf1 && !buf2` is true) — DMA would help nothing there.
7. Keep `disp_drv.hor_res = TFT_HEIGHT; disp_drv.ver_res = TFT_WIDTH;` (the manual 320×240 landscape
   swap) and leave `sw_rotate = 0`, `rotated = 0`. This is the **correct** low-cost pattern for a
   panel driven with `tft.setRotation(1)`: TFT_eSPI does the rotation in hardware/its own coordinate
   remap, LVGL just needs to be told the resulting logical resolution. The GT911 touch driver already
   compensates independently (`touch.setMaxCoordinates(320,240); touch.setSwapXY(true);
touch.setMirrorXY(false,true);`, `tdeck_main.cpp:138-140`) — do **not** also enable
   `sw_rotate`/`rotated` on the LVGL side, that would double-rotate coordinates.
8. The TFT and SD card share one physical SPI bus with only one home-grown binary semaphore
   (`xSemaphore`) serializing access — and it currently guards **only** the TFT flush
   (`tdeck_main.cpp:463`) and the debug panel-readback CRC (`tdeck_debug.cpp:311`). **SD card reads
   (audio playback, log/settings I/O) are not observed to take this semaphore anywhere in
   `src/t-deck/` or `src/esp32/esp32_audio.cpp`.** This is a real, unguarded bus-contention hazard,
   independent of the buffer/refresh question, and is the most likely root cause of the "playing
   audio blocks/corrupts the UI" symptom class if it manifests as garbage pixels rather than a
   frozen UI. See Finding 12.
9. At `SPI_FREQUENCY 40000000` (current setting, `lib/TFT_eSPI/User_Setups/Setup210_LilyGo_T_Deck.h:47`),
   a full 320×240×16bpp frame's SPI data phase is 30.7 ms minimum; measured full-frame flush in this
   repo is 36.7 ms avg (`docs/tdeck-findings-20260828.md`) — consistent, ~6 ms is `setAddrWindow` +
   software FIFO-refill overhead in blocking `pushColors`. 80 MHz would roughly halve that to ~15 ms
   data phase, and is very likely reliable on this hardware (short fixed PCB traces, not jumper
   wires) — but has not been measured on this board; treat as an experiment, not a given.

## Findings

### 1. The four LVGL 8.3 buffering modes, exact contracts (source: `lib/lvgl/src/hal/lv_hal_disp.h`, `lib/lvgl/src/core/lv_refr.c`)

**Claim.** LVGL 8.3 has three independent driver flags — `direct_mode`, `full_refresh`, and the
presence/absence/size of `buf2` — that combine into four practically distinct regimes:

| Mode                                          | `buf1` | `buf2`            | `full_refresh`                                      | `direct_mode` | Behaviour                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| --------------------------------------------- | ------ | ----------------- | --------------------------------------------------- | ------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A. One small partial buffer                   | small  | `NULL`            | 0                                                   | 0             | Default LVGL mode. Buffer < screen size; large invalidated areas are rendered/flushed in row-chunks of `get_max_row()` rows. Rendering **blocks** on the previous flush before reusing the buffer (`lv_refr.c:709`: `draw_buf->buf1 && !draw_buf->buf2` is true).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| B. Two small partial buffers                  | small  | small (same size) | 0                                                   | 0             | Enables real overlap: `draw_buf_flush()` swaps `buf_act` to the free buffer right after `flush_cb` returns (`lv_refr.c:1307`), and the wait-for-previous-flush happens **inside `draw_buf_flush()`**, gated on `!full_sized` (`lv_refr.c:1283`), not in the renderer. This is the only mode where LVGL itself doesn't force a stall between rendering and a DMA-driven flush.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| C. Two full-screen buffers + `full_refresh=1` | full   | full (same size)  | 1                                                   | 0             | Classic double buffering. `refr_area()` always redraws the whole logical screen into `buf_act` regardless of what was invalidated (`lv_refr.c:638-648`); `flush_cb` typically just hands the whole finished buffer to the panel (or, for a true framebuffer LCD controller, swaps a pointer). No SPI/pixel savings from partial updates — this mode exists for panels/controllers where "partial update" isn't meaningfully cheaper (parallel RGB LCD, or where tearing must be avoided by only ever presenting a complete frame).                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| D. `direct_mode = 1`                          | full   | `NULL` or full    | — (irrelevant, direct_mode branch is checked first) | 1             | LVGL draws **directly at the object's absolute screen coordinates** into a persistent, screen-sized buffer that is never cleared between frames (`refr_area()`: `draw_ctx->clip_area = area_p` but `draw_ctx->buf_area` is the _full_ screen — i.e., indices are absolute, not chunk-relative). With **one** buffer, this is essentially "single persistent framebuffer, redraw only what changed, buffer content between frames is authoritative" — cheap and correct, closest in spirit to what a good MCU GUI wants. With **two** buffers, LVGL additionally runs a synchronization pass (`lv_refr.c:518-560`, `_lv_disp_get_next_sync_area` machinery) that copies untouched-this-frame regions from the currently-displayed buffer into the newly-targeted buffer before drawing, because with two real framebuffers the "other" one is stale outside of what THIS frame touched. `direct_mode` + `buf2==NULL` skips that sync entirely (`lv_refr.c:521`: `if(draw_buf->buf2 == NULL) return;`). |

**Why.** All four fall out of two checks in `lv_refr.c`:

- `refr_area()` line 638: `if(full_refresh || direct_mode)` picks the "draw against the whole
  logical screen" path (full_refresh clips to the whole screen and redraws everything every time;
  direct_mode clips to the actual invalidated area but still addresses the buffer at absolute
  screen coordinates).
- `refr_area_part()` line 708: `full_sized = draw_buf->size == hor_res*ver_res` — a **screen-sized**
  buffer (single or double) forces the "wait for previous flush before drawing" path that a genuinely
  small double-buffer setup skips.

**Symptom if violated / misunderstood.**

- Believing you need `full_refresh=1` for a screen-sized buffer to work: false — a screen-sized
  single buffer works fine with `full_refresh=0` (this repo's current state); it only forfeits the
  DMA-overlap benefit, it does not misbehave.
- Setting `direct_mode=1` with two buffers but not understanding the sync pass: the "other" buffer's
  untouched regions will show stale content — this is why the sync machinery exists; do not attempt
  `direct_mode` with 2 buffers unless you keep this sync in mind, and note it is **not implemented**
  in this repo's config (direct_mode is 0), so it is not currently a live concern.

**Is a screen-sized single buffer with `full_refresh=0` illegal?** **No.** It is legal and is exactly
what commit `0a11757c` ("experiment(t-deck): full_refresh=0, nothing else") put into the tree. LVGL
handles it via the "normal" partial-refresh branch in `refr_area()` (the `else` after the
`full_refresh || direct_mode` check), computing `max_row = get_max_row(...)` from the buffer's pixel
capacity — for a full-screen buffer this number is ≥ screen height, so any invalidated rectangle
(even the whole screen) fits in one chunk. The single-buffer wait in `refr_area_part()` still applies
(`buf1 && !buf2`), but since `disp_flush()` is fully synchronous this wait is a no-op in wall-clock
terms — the actual work already finished before the function returns.

**Fix / recommendation.** Do not change modes to "fix" this axis in isolation. If redraw problems
persist after the `full_refresh=0` flip, the bug is almost certainly a missing `lv_obj_invalidate()`
call somewhere in the object/update code (Track: invalidation & dirty tracking), not a buffering-mode
illegality.

**Source.** `lib/lvgl/src/hal/lv_hal_disp.h:91-103` (buf2/direct_mode/full_refresh field docs),
`lib/lvgl/src/core/lv_refr.c:518-560` (direct-mode sync), `:600-693` (`refr_area`/`refr_area_part`),
`:1054-1090` (`get_max_row`), `:1272-1313` (`draw_buf_flush`, buffer swap timing) — all vendored
in-repo, read directly.

### 2. `lv_disp_draw_buf_init()` 4th argument — pixel count, not byte count (G07 class of bug)

**Claim.** `lv_disp_draw_buf_init(lv_disp_draw_buf_t*, void* buf1, void* buf2, uint32_t
size_in_px_cnt)` — the doc comment in `lv_hal_disp.h:223` is explicit: _"size of the `buf1` and
`buf2` in **pixel count**"_. This is the single most common LVGL porting mistake because most
example code allocates `buf1` with `malloc(w * h * sizeof(lv_color_t))` (a byte count) and it is easy
to pass that same expression as the 4th argument by reflex.

**Why.** `draw_buf->size` (pixel count) is used directly as a pixel-address stride/limit throughout
`lv_refr.c` — most importantly in `get_max_row()`: `max_row = draw_buf->size / area_w` (line 1056).
If `size` is passed as a **byte** count on a 16-bit-color panel (`sizeof(lv_color_t) == 2`), `size` is
2x too large.

**Symptom class if too large (byte count passed where pixel count expected).**

- `get_max_row()` computes a row count up to 2x too big for the actual buffer capacity. LVGL will
  then have `refr_area_part()`/the draw context write pixels **past the end of the real allocation**
  — a heap overrun of up to (allocated_px_count) more bytes than exist, corrupting whatever heap
  metadata or adjacent allocation follows. On ESP32 this typically shows up as: silent corruption
  (wrong colors/garbage in unrelated UI elements), a `heap: multi_heap.c` assert/abort a while later
  (temporally decoupled from the actual overrun, notoriously hard to bisect), or a crash inside an
  unrelated `malloc`/`free` call. **This repo's own comment at `tdeck_main.cpp:383-386` documents
  exactly this failure mode for this exact codebase**: _"Harmless while full_refresh=1 and buf2==NULL
  keep the partial-render paths unreachable; with partial refresh it is a ~150 KB overflow."_ — i.e.
  it was a live landmine that `full_refresh=1` was masking, precisely analogous to Finding 3.
- **Current state: already fixed.** `tdeck_main.cpp:387` passes `TFT_WIDTH * TFT_HEIGHT` (pixel
  count) as the 4th argument, matching commit `1932da46 fix(t-deck): pass pixel count, not byte
count, to lv_disp_draw_buf_init (G07)`. Verify this stays true after any future buffer-size change
  — if the buffer is resized to e.g. `TFT_WIDTH * (TFT_HEIGHT/4)` pixels for a partial buffer, the 4th
  argument must track pixel count, and the `malloc`/`heap_caps_malloc` byte size must independently be
  `pixel_count * sizeof(lv_color_t)`.

**Symptom class if too small** (pixel count passed but the real allocation is smaller than claimed,
or a byte count passed where LVGL interprets it as pixels but the actual buffer is even smaller than
that already-too-small number implies for the _opposite_ mistake — i.e. under-claiming capacity).
This direction is less dangerous but still wrong: `get_max_row()` returns a row count that is smaller
than what the buffer can actually hold — LVGL simply chunks more aggressively than necessary. No
overrun, but more flush calls than needed, i.e. a pure performance regression (more `flush_cb`
invocations, more per-flush fixed overhead — see Finding 3 below). If the mismatch is severe (buffer
genuinely too small for even one scanline: `size / area_w < 1`), `get_max_row()`'s rounding logic can
return 0, which `refr_area()`'s `for(row = ...; row + max_row - 1 <= y2; row += max_row)` loop turns
into an infinite loop (row never advances) — a hang, not a crash. LVGL's own rounder-callback path
already logs `"Can't set draw_buf height using the round function. (Wrong round_cb or to small
draw_buf)"` for a related case (`lv_refr.c:1080`) but there is **no** guard against `max_row == 0`
from a plain (non-rounded) too-small buffer in 8.3 — treat "buffer must hold at least 1 full row of
the widest expected invalidated area" as a hard invariant.

**Fix (code pattern).**

```c
#define DRAW_BUF_PX_CNT   (TFT_WIDTH * SOME_FRACTION_OF_HEIGHT)   // pixel count
static lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(
        DRAW_BUF_PX_CNT * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
...
lv_disp_draw_buf_init(&draw_buf, buf1, buf2, DRAW_BUF_PX_CNT);   // pixel count, matches malloc math
```

Keep the pixel-count constant as the single source of truth and derive both the `malloc` byte size
and the `lv_disp_draw_buf_init` argument from it, rather than writing the multiplication twice (the
G07 bug was exactly two independent, silently-diverging expressions for "buffer size").

**Source.** `lib/lvgl/src/hal/lv_hal_disp.h:210-225` (doc comment, authoritative); `lib/lvgl/src/core/lv_refr.c:1054-1090` (`get_max_row`, consumer of `draw_buf->size`); `src/t-deck/tdeck_main.cpp:383-387` (in-repo bug comment + current fixed code); git log `1932da46`.

### 3. Buffer sizing guidance for 320×240×16bpp

**Claim / concrete numbers** (2 bytes/px, `LV_COLOR_DEPTH 16`, no alpha channel in the draw buffer):

| Fraction of screen           | Pixels | Bytes              | Rows (of 320 wide) | Flush calls for a full-screen invalidation |
| ---------------------------- | ------ | ------------------ | ------------------ | ------------------------------------------ |
| Full screen                  | 76 800 | 153 600 B (150 KB) | 240                | 1                                          |
| 1/2                          | 38 400 | 76 800 B (75 KB)   | 120                | 2                                          |
| 1/4                          | 19 200 | 38 400 B (37.5 KB) | 60                 | 4                                          |
| 1/10 (LVGL's stated minimum) | 7 680  | 15 360 B (15 KB)   | 24                 | 10                                         |

**Why the 1/10 floor exists.** Each `flush_cb` invocation carries fixed overhead independent of
payload size: `setAddrWindow()` (a handful of command+data bytes plus, in this repo, an
`xSemaphoreTake`/`Give` pair and a `startWrite()`/`endWrite()` transaction boundary — each
`beginTransaction`/`endTransaction` equivalent has real cost: SPI clock reprogramming, CS toggling,
mode/bit-order setup). Below ~1/10 screen, the fixed per-call overhead starts to dominate over the
marginal cost of transferring more pixels per call, so shrinking the buffer further buys back RAM
without buying back time — LVGL's docs state this explicitly (see Sources). Above 1/10, the marginal
gain per doubled buffer size drops sharply (SPI transfer time is linear in payload either way; you're
only amortizing the fixed per-call cost, which is already small relative to a several-KB transfer).

**Trade-off in this repo's numbers.** Measured full-frame flush (1 call, 76 800 px) is 36.7 ms avg. A
1/10-screen buffer partitions the same 76 800 px into 10 flush calls of 7 680 px each; at 40 MHz the
data phase per call is `7680*16/40e6 = 3.07 ms`, ×10 = 30.7 ms plus 10× the fixed per-call overhead
(vs 1× for the full buffer) — so a full redraw is _slightly_ slower in wall-clock SPI time with a
small buffer, in exchange for: (a) ~10x less RAM, (b) the ability to double-buffer within a modest RAM
budget, and (c) — the actually decisive factor for this device — the ability to interleave other work
between chunks once flush is asynchronous (DMA). For a device whose actual invalidated areas are
mostly small (header label, one row of a message list) rather than full-screen, buffer size barely
matters for those redraws (they fit in 1-2 chunks either way); it mainly matters for full-screen
events (tab switch, drawer open — measured 384-691 kpx per tab switch in
`docs/tdeck-findings-20260828.md`, i.e. up to ~9x one screen, already tiled today).

**Recommendation for this board.** Two buffers of 1/8 to 1/10 screen (~15-19 KB each, ~30-38 KB
total) in internal DRAM is a good balance: fits comfortably in the ~300+ KB of internal SRAM typically
free on an ESP32-S3 Arduino sketch with BLE/WiFi stacks resident, small enough that the double-buffer
RAM cost is negligible, and large enough to stay above LVGL's no-further-benefit floor.

**Source.** LVGL 8.3 docs, Display porting guide (fetched `https://lvgl.io/docs/open/8.3/porting/display`,
2026 — 301-redirected from `docs.lvgl.io/8.3/porting/display.html`): _"a larger buffer results in
better performance but above 1/10 screen sized buffer(s) there is no significant performance
improvement. Therefore it's recommended to choose the size of the draw buffer(s) to be at least 1/10
screen sized."_ Repo timing: `docs/tdeck-findings-20260828.md` §2.

### 4. PSRAM vs internal DRAM for the draw buffer (critical)

**Claim.** On this toolchain (`espressif32@6.6.0` → Arduino-ESP32 core 2.x → ESP-IDF **4.4.x**), the
SPI master driver's documented DMA buffer contract is: _"Allocated in DMA-capable internal memory. If
external PSRAM is enabled, this means using `pvPortMallocCaps(size, MALLOC_CAP_DMA)`"_ and _"32-bit
aligned (starting from a 32-bit boundary and having a length of multiples of 4 bytes)"_ — and if this
is not satisfied, _"the transaction efficiency will be affected due to the allocation and copying of
temporary buffers"_ (i.e., the driver bounce-buffers through an internal temp allocation and memcpy's
your PSRAM data into it, per transaction, transparently). `MALLOC_CAP_DMA` on ESP32/ESP32-S3
**excludes** `MALLOC_CAP_SPIRAM` — DMA-capable memory here means internal SRAM. (IDF 5.x introduced
`SPI_TRANS_DMA_USE_PSRAM` allowing genuine DMA-from-PSRAM without a bounce copy, subject to bus
bandwidth sharing with the PSRAM cache — but this repo is on IDF 4.4.x, not 5.x, so treat that flag as
**not available**; UNVERIFIED whether the pinned arduino-esp32 2.x / IDF 4.4.x backport includes it —
did not confirm in the vendored `lib/TFT_eSPI` or Arduino core sources, and TFT_eSPI's own DMA path
(`Processors/TFT_eSPI_ESP32_S3.c`, read directly) does not set any such flag on its `spi_transaction_t`
(`trans.flags = 0`).)

**Why this matters here specifically.** `setupLvgl()` currently allocates the draw buffer with
`ps_malloc()` (PSRAM), falling back to internal `malloc()` only if PSRAM allocation fails
(`tdeck_main.cpp:351-370`). This is fine for the _current_ fully-blocking `pushColors()` flush (no
DMA involved, PSRAM access is just slower per-byte than internal SRAM for the CPU-driven copy loop
inside `pushColors`, which is already dwarfed by SPI transfer time). It becomes actively
counterproductive the moment DMA is introduced: every `pushPixelsDMA`/`pushImageDMA` call against a
PSRAM source would silently cost an extra internal-buffer allocation + `memcpy` of the whole chunk on
top of the DMA transfer itself, adding CPU time and heap churn back into the "non-blocking" path —
partially or fully negating the reason to add DMA. Per-flush heap allocation on ESP32 is also exactly
the pattern flagged elsewhere in this codebase's history as dangerous under BLE load (`printf malloc
starves NimBLE` — malloc above 64 B on the flush-frequency hot path is a known problem class here).

**PSRAM's own cost profile (why it's slow for CPU-driven access too, general ESP32-S3 facts).** PSRAM
on ESP32-S3 shares the same octal/quad SPI-like bus (referred to as "MSPI"/`SPI0`/`SPI1` internally)
used to fetch flash-cached instructions, and is accessed through the CPU's external-memory cache, not
as a flat zero-wait-state address space like internal SRAM. Sequential/cached access is reasonable;
random small access and any access pattern that misses cache is markedly slower than internal SRAM —
commonly cited ballpark from Espressif/community benchmarks is a few times slower for read/write
throughput and materially higher latency per access (UNVERIFIED exact multiplier for this specific
octal-PSRAM T-Deck Plus module; did not find a number specific to this board's PSRAM chip/speed grade
in the time budgeted — treat "PSRAM is slower, avoid it on any latency- or DMA-sensitive hot path" as
confirmed, the precise factor as unconfirmed).

**Recommendation for this device (8 MB PSRAM, ESP32-S3).**

- **Draw buffer(s)** (the pixel data DMA reads from): allocate in **internal DRAM** via
  `heap_caps_malloc(px_cnt * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)`. With the
  Finding-3 sizing (two buffers × ~15-19 KB), this is a trivially affordable ~30-38 KB out of a
  budget on the order of several hundred KB of internal SRAM — no realistic memory pressure argument
  for putting it in PSRAM.
- **LVGL object heap** (`LV_MEM_CUSTOM_ALLOC = ps_malloc`, i.e. `lv_obj_t`, styles, string copies,
  image descriptors, animation state): **keep in PSRAM**, as configured today. This memory is not
  DMA'd, is not on any interrupt-latency-sensitive path, and is exactly the kind of "lots of
  medium/large long-lived allocations" workload PSRAM is fine for. The current config is correct here
  — do not move the object heap to internal RAM; internal RAM is the scarce resource this device
  needs to protect for DMA buffers, BLE/WiFi stack, and stacks/queues, not for LVGL objects.
- If DMA is _not_ adopted (blocking `pushColors` stays), the current PSRAM draw buffer is not wrong,
  merely leaves the "avoid DMA-bounce-copy" question moot — but internal RAM is still marginally
  faster for the `pushColors` software copy loop itself and costs nothing to switch, so there is no
  reason not to make this change regardless of whether DMA follows.

**Fix (code).**

```c
#define DRAW_BUF_PX_CNT (TFT_WIDTH * (TFT_HEIGHT / 8))   // ~1/8 screen, tune per Finding 3
static lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(
        DRAW_BUF_PX_CNT * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
static lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(
        DRAW_BUF_PX_CNT * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
if (!buf1 || !buf2) { /* internal RAM exhausted: fall back to a single, smaller internal buffer
                          before ever falling back to PSRAM+DMA — a PSRAM buffer without DMA
                          (plain pushColors) is a safer degraded mode than PSRAM+DMA */ }
lv_disp_draw_buf_init(&draw_buf, buf1, buf2, DRAW_BUF_PX_CNT);
```

**Source.** ESP-IDF v4.4 SPI Master Driver docs
(`https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32/api-reference/peripherals/spi_master.html`,
fetched 2026): DMA buffer must be `MALLOC_CAP_DMA`-allocated internal memory, 4-byte aligned, or the
driver silently bounce-copies. General `MALLOC_CAP_DMA` excludes `MALLOC_CAP_SPIRAM`: ESP-IDF Heap
Memory Allocation docs family (`docs.espressif.com/.../system/mem_alloc.html`), confirmed via search
summary, not independently re-verified by direct fetch — mark the exact wording UNVERIFIED but the
conclusion (MALLOC_CAP_DMA memory on this chip is internal SRAM, not PSRAM, absent the IDF5-only
`SPI_TRANS_DMA_USE_PSRAM` opt-in) is consistent across every source checked. `printfMalloc starves
NimBLE` heap-churn lesson: user's own project memory, cited for the "avoid per-hot-path malloc" pattern
match, not the PSRAM/DMA fact itself.

### 5. TFT_eSPI specifics: blocking vs DMA flush, canonical non-blocking pattern

**Claim — API surface (read directly from `lib/TFT_eSPI/TFT_eSPI.h` and
`lib/TFT_eSPI/Processors/TFT_eSPI_ESP32_S3.c`, this exact vendored version):**

- `tft.initDMA(bool ctrl_cs = false)` — one-time setup. Calls `spi_bus_initialize(spi_host, &buscfg,
DMA_CHANNEL)` **and** `spi_bus_add_device(...)` (`TFT_eSPI_ESP32_S3.c:861-864`) — i.e. it claims the
  SPI bus at the ESP-IDF driver level. `queue_size = 1` in the device config (line 857) — TFT_eSPI's
  own DMA path never has more than one transaction in flight.
- `tft.pushPixelsDMA(uint16_t* image, uint32_t len)` / `tft.pushImageDMA(x,y,w,h,image[,buffer])` —
  **each of these calls `dmaWait()` as its very first action** (`:637`, `:681`, `:736/765`) — i.e.
  they block until any _previous_ DMA transaction on this device has completed before queuing the new
  one. This is the mechanism that makes it safe to call `lv_disp_flush_ready()` immediately after
  issuing the DMA push: the _next_ time that same physical SPI transaction is reused (next flush
  call), TFT_eSPI itself will not proceed until the hardware confirms the prior transfer is done.
- `tft.dmaBusy()` — non-blocking poll (checks `spi_device_get_trans_result(dmaHAL, &rtrans, 0)`,
  0-tick timeout). `tft.dmaWait()` — blocking wait (`portMAX_DELAY`). Neither is required in the
  common `flush_cb` pattern below because `pushPixelsDMA`/`pushImageDMA` already call `dmaWait()`
  internally; call `dmaWait()` yourself only if you need to guarantee completion at a point that is
  _not_ immediately followed by another `pushPixelsDMA`/`pushImageDMA` call (e.g. before reading back
  the panel, or before touching a buffer from a different task).
- 64 KB DMA transaction ceiling: both `pushPixelsDMA` and `pushImageDMA` internally chunk any transfer
  over `0x4000` pixels (32 768 px = 64 KB at 16bpp) into blocking `pushPixels()` calls first, then DMA
  the remainder — comment in source: _"DMA byte count for transmit is 64Kbytes maximum... equivalent
  to an area of ~320 x 100 pixels"_. At the Finding-3 buffer sizes (≤1/4 screen = 19 200 px) this
  ceiling is never hit; only relevant if a much larger single buffer is used with DMA.
- `startWrite()`/`endWrite()` bracket a logical transaction (asserts/deasserts `TFT_CS`, applies the
  SPI clock/mode settings) — matches "`SUPPORT_TRANSACTIONS` is mandatory for ESP32" comment at the
  top of `TFT_eSPI_ESP32_S3.h`, i.e. this HAL toggle cannot be turned off on this chip.

**Canonical non-blocking `flush_cb` for LVGL 8.3 + TFT_eSPI + two small internal-DRAM buffers:**

```c
static bool s_dma_ready = false;

void setupLvglDma() {
    s_dma_ready = tft.initDMA();   // once, after tft.begin()/setRotation(); before lv_disp_drv_register
}

static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
        tft.startWrite();
        tft.setAddrWindow(area->x1, area->y1, w, h);
        // pushPixelsDMA() itself calls dmaWait() first -- this blocks here only if the PREVIOUS
        // flush's DMA has not yet finished, which is the correct place to absorb that wait.
        tft.pushPixelsDMA((uint16_t *)&color_p->full, w * h);
        tft.endWrite();
        xSemaphoreGive(xSemaphore);
    }
    // Safe to call immediately: the buffer just handed to DMA will not be reused by the renderer
    // until the NEXT-but-one flush, and that flush's pushPixelsDMA() will itself block on this
    // transfer's completion before touching the hardware again.
    lv_disp_flush_ready(disp);
}
```

**Caveat on `endWrite()` with DMA.** `endWrite()` deasserts CS. If `initDMA(false)` (the default,
`ctrl_cs=false`) is used, TFT_eSPI does **not** manage CS via the DMA transaction's own pre/post
callbacks — `startWrite()`/`endWrite()` around the _queuing_ call is what asserts/deasserts CS, and
since `pushPixelsDMA` only _queues_ the transfer (`spi_device_queue_trans`, non-blocking hardware
handoff) rather than waiting for it, calling `endWrite()` immediately after **can deassert CS while
the DMA engine is still clocking out the transfer**, which is very likely to produce a torn/garbled
write to the panel. **This needs to be verified against this exact TFT_eSPI version before shipping**
— either (a) call `tft.dmaWait()` explicitly before `endWrite()` (which makes the flush blocking again
and defeats the purpose), or (b) use `initDMA(true)` so TFT_eSPI's own DMA transaction controls CS via
hardware (`spics_io_num = TFT_CS` when `ctrl_cs=true`, `TFT_eSPI_ESP32_S3.c:842-843`) and skip the
app-level `startWrite()`/`endWrite()` CS toggling for the DMA-pushed portion of the transaction. Given
the queue depth of 1 (line 857 `queue_size=1`), the _simplest correct_ pattern that avoids this
question entirely is to **not call `endWrite()` right after `pushPixelsDMA()`**; instead defer
`endWrite()`/CS-deassert to the _start_ of the _next_ flush (right after that flush's internal
`dmaWait()` has resolved the previous transfer), or keep CS permanently asserted for the whole
LVGL-owned flush burst and only release it once per `lv_disp_flush_is_last(disp)==true`
(`lv_hal_disp.h:345`, already available). Mark this specific CS-timing detail **UNVERIFIED against
real hardware** — it was not measured in this research pass; flag it for the harness
(`docs/tdeck-findings-20260828.md` methodology) before trusting it in production.

**`SPI_FREQUENCY` ceiling.** Current: `#define SPI_FREQUENCY 40000000` in
`lib/TFT_eSPI/User_Setups/Setup210_LilyGo_T_Deck.h:47` (27 MHz commented out above it). General
community consensus for ST7789 + ESP32(-S3) + TFT_eSPI (WebSearch, several Bodmer/TFT_eSPI GitHub
issues and community writeups, 2026): 40 MHz is reliable across essentially all wiring; 80 MHz works
when traces are short and solid (a fixed on-PCB display like the T-Deck Plus's is the good case, not
the risky breadboard case) — one source measured a full 240×240 clear at 80 MHz taking ~3 ms.
**UNVERIFIED for this specific board/panel**: nobody has measured 80 MHz on this T-Deck Plus unit; if
raised, it should be an isolated, revertible experiment with the existing redraw-log harness
(`docs/tdeck-findings-20260828.md` methodology), not a blind change alongside other buffer/DMA work.

**PSRAM source buffers and TFT_eSPI DMA.** Given Finding 4, do not pass a `ps_malloc`'d buffer to
`pushPixelsDMA`/`pushImageDMA` on this toolchain — the underlying `spi_device_queue_trans` call
(`TFT_eSPI_ESP32_S3.c:662/703/788`) hands the raw pointer straight to the IDF driver with `trans.flags
= 0` (no PSRAM-DMA opt-in flag set anywhere in this vendored file), so a PSRAM buffer here relies
entirely on the driver's own bounce-buffer fallback (Finding 4) — works, but silently reintroduces a
blocking memcpy into the "non-blocking" path.

**Source.** `lib/TFT_eSPI/TFT_eSPI.h:757-793` (public DMA API declarations, read directly);
`lib/TFT_eSPI/Processors/TFT_eSPI_ESP32_S3.c:580-880` (full DMA implementation, read directly);
`lib/TFT_eSPI/Processors/TFT_eSPI_ESP32_S3.h:1-40` (`SUPPORT_TRANSACTIONS` mandatory comment);
`lib/TFT_eSPI/User_Setups/Setup210_LilyGo_T_Deck.h` (current pin/frequency config, read directly);
WebSearch summary of TFT_eSPI/ESP32-S3 SPI frequency discussions (Bodmer/TFT_eSPI GitHub issues,
atomic14.com writeup) — frequency ceiling claim, general community consensus not a primary spec.

### 6. Shared SPI bus: TFT + SD card on the same physical bus

**Claim — what this repo actually does today** (all read directly from `src/t-deck/tdeck_main.cpp`
and `src/t-deck/tdeck_debug.cpp`):

- `initTDeck()` calls `SPI.begin(TDECK_SPI_SCK, TDECK_SPI_MISO, TDECK_SPI_MOSI)` (the global Arduino
  `SPI` object, used later for `SD.begin(TDECK_SDCARD_CS, SPI, 800000U)`) **before** `tft.begin()`.
  TFT_eSPI's non-DMA ESP32 path drives the SPI peripheral registers directly (not through this same
  `SPIClass` object) using the pins from `Setup210_LilyGo_T_Deck.h` (`TFT_MOSI=41, TFT_MISO=38,
TFT_SCLK=40`) — which are the **same physical pins** as `TDECK_SPI_*`. Two independent software
  paths (raw TFT_eSPI register pokes, and the IDF-driver-backed Arduino `SPIClass` used for SD) share
  one physical bus with **no driver-level arbitration** between them — this is exactly why the app
  added its own `xSemaphore`.
- `xSemaphore` (a binary semaphore) currently guards: (a) `disp_flush()` — every LVGL flush
  (`tdeck_main.cpp:463-476`); (b) `tdeck_dbg_screencrc()` — the debug panel-readback path
  (`tdeck_debug.cpp:311-329`), which explicitly forces `TDECK_SDCARD_CS` and `LORA_CS` high before
  reading the panel, with the comment _"Keep other SPI slaves off the shared MISO line, as the wake
  path does"_ — i.e. the firmware author already knows the shared-MISO hazard is real and defends
  against it on this one path.
- **No occurrence of `xSemaphoreTake(xSemaphore, ...)` was found around SD card access** (`SD.begin`,
  file reads for the SD-backed audio path in `lib/ESP32-audioI2S`/`src/esp32/esp32_audio.cpp`, or any
  settings/log file I/O). `esp32_audio.cpp` has its own, unrelated `audioSemaphore` that only
  serializes audio-subsystem state (mute/setup), not SPI bus access.
- `SD.begin(TDECK_SDCARD_CS, SPI, 800000U)` runs the SD card at a deliberately slow 800 kHz — likely
  chosen precisely because this bus-sharing setup is fragile at higher SD clock rates, or because the
  card module needs it; either way it is far below the TFT's 40 MHz, meaning any accidental
  interleave is also a large clock-domain mismatch window.

**What breaks (general, sourced from TFT_eSPI/community discussion, WebSearch 2026, GitHub
Bodmer/TFT_eSPI discussion #1885 "Some thoughts about caveats with TFT_eSPI combined with SD card
sharing same SPI bus"):**

- **MISO tristating.** Many cheap microSD breakout modules do not properly tristate their MISO output
  when their own CS is deasserted — if the TFT is mid-transfer while the SD module's CS happens to be
  low (or its MISO driver is simply non-compliant), both devices drive MISO simultaneously →
  electrical contention, corrupted reads for whichever device is trying to read (the TFT does read
  back via `readRect`/`readPixel`, used in this repo's `tdeck_dbg_screencrc()`), and in the worst case
  can stress the GPIO drivers. This is a hardware-level defect in the SD module, not fixable in
  software beyond "never let both devices' transactions overlap in time."
- **Corrupted SD reads under concurrent TFT writes.** Documented community report (same discussion):
  concurrent async SD file listing + TFT display updates from different code paths caused SD read
  corruption — the failure mode runs both directions, not just "TFT gets garbage."
  `beginTransaction`/`startWrite`-`endWrite` settings on one device do not protect the other device's
  transaction if nothing serializes _which_ device is allowed to be mid-transfer at any given moment;
  transaction settings (clock, mode, bit order) apply electrically to the whole bus for whichever
  device's CS happens to be asserted, they do not create mutual exclusion by themselves.
- **DMA in flight while another device asserts CS**: not directly documented for this repo's stack
  (DMA is not currently used), but follows directly from the same electrical-contention argument: an
  in-flight DMA transfer to the TFT with no coordinating semaphore, interleaved with an SD transaction
  on another task/core, is the same MISO-contention hazard, just with the CPU less able to "notice" and
  serialize it manually mid-transfer (a DMA transfer, once queued, runs on hardware without CPU
  supervision until `dma_end_callback`/`dmaWait()` — if something else asserts its CS mid-transfer, the
  bus contention window is exactly the DMA transfer's whole duration, not a few instructions).

**Is `startWrite`/`endWrite` enough?** **No**, not on its own. `startWrite`/`endWrite` (and
`beginTransaction`) correctly manage _one device's_ CS assertion and SPI parameter reprogramming, but
provide no cross-device mutual exclusion unless every participant on the bus (TFT_eSPI's internal
driver **and** the Arduino `SD`/`SPIClass` path **and** anything else on the bus, e.g. LoRa if it ever
shares pins) is funneled through the _same_ semaphore before touching the bus at all. This repo's
`xSemaphore` is the right idea but is **incompletely applied** — it protects the TFT side only.
**Concrete fix**: wrap every SD-card SPI operation (not just TFT flush/readback) in the same
`xSemaphoreTake(xSemaphore, ...)`/`xSemaphoreGive(xSemaphore)` pair, including whatever the audio
subsystem's SD file reads ultimately call down into.

**Should the SD card get its own SPI host on an S3?** **Yes, this is the architecturally correct fix**
and is more robust than semaphore discipline alone. ESP32-S3 has a flexible GPIO matrix — unlike the
original ESP32 (where VSPI/HSPI have fixed IOMUX pins for best performance), an S3 can route **any**
SPI-capable host (SPI2/`FSPI` and SPI3/`HSPI`, both general-purpose) to **the same physical GPIOs**
via the matrix, meaning the SD card and the TFT can each get their own independent `spi_bus_initialize`
host while still using the same wires — the ESP-IDF _does_ support one host servicing multiple GPIO
sets or shared pins with proper CS separation, but the cleaner and lower-risk option community sources
converge on is: **give TFT and SD genuinely separate hosts** so each has its own IDF-level bus
lock/queue and neither driver needs to know about the other. Practically for this codebase: `initDMA()`
already calls `spi_bus_initialize()` itself (Finding 5) — if DMA is adopted, this call **must not**
target the same host that `SPI.begin()` (used for SD) has already initialized, or `spi_bus_initialize`
will fail (`ESP_ERROR_CHECK` will abort on `ESP_ERR_INVALID_STATE`). Route the two through distinct
`spi_host_device_t` values (this repo's `TFT_eSPI_ESP32_S3.c:38-42` selects `spi_host` from
`USE_HSPI_PORT`/default macros — confirm which one is active and put SD on the other), and keep the
app-level `xSemaphore` anyway for the MISO-contention argument above (separate hosts stop the two IDF
drivers from corrupting each other's _transaction state_, but do not stop two simultaneous electrical
transfers on physically shared wires from contending on MISO — that part is a wiring-level property no
amount of host separation fixes; the two fixes are complementary, not substitutes for each other).

**Source.** All in-repo facts read directly (`tdeck_main.cpp`, `tdeck_debug.cpp`,
`Setup210_LilyGo_T_Deck.h`, `TFT_eSPI_ESP32_S3.c` spi_host selection). External: WebSearch results,
2026 — GitHub `Bodmer/TFT_eSPI` discussion #1885 ("Some thoughts about caveats with TFT_eSPI combined
with SD card sharing same SPI bus"), discussion #2717, issue #3601, issue #1132 ("initDMA conflicts
with non-DMA operations"); Espressif ESP-IDF v6.0.1 docs page _"Sharing the SPI Bus Among SD Cards and
Other SPI Devices - ESP32-S3"_ (title only confirmed via search result, not fetched in full — mark
UNVERIFIED for exact recommended API calls, but the page's existence and title confirm this is an
officially documented, known-tricky scenario specifically called out for the S3).

### 7. Partial-refresh artifact catalogue

For each artifact: root cause, and fix, given this repo's architecture.

| Artifact                                                                                                         | Root cause                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         | Fix                                                                                                                                                                                                                                                                                                                              |
| ---------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Tearing** (part of an old frame and part of a new frame visible simultaneously)                                | The panel is mid-scan-out while the SPI write updates the same region it is reading, OR (this repo's actual risk) a DMA'd buffer is overwritten by the renderer before the DMA finished draining it. Not a display-controller-scanout race here (ST7789 over SPI has its own internal GRAM, not a live-scanned framebuffer read concurrently by a video timing engine the way a parallel-RGB panel would be) — the practical tearing risk on this hardware is specifically the DMA/buffer-reuse race in Finding 5 (CS/endWrite timing) or a genuinely too-small buffer regime that lets `lv_disp_flush_ready()` fire before the SPI engine actually finished.                                                                                                                                                                                                                                                                                                                                                                                                                                                      | Never call `lv_disp_flush_ready()` (or let `pushPixelsDMA`'s internal state make it _appear_ safe to) before the transfer is truly complete for the buffer about to be reused. With `pushPixelsDMA`, this is handled by its own `dmaWait()` gate on next use — audit the `endWrite()` timing per Finding 5.                      |
| **Torn/stale rectangles left on screen**                                                                         | A region was invalidated and correctly redrawn _in the buffer_, but its `flush_cb` call was skipped, targeted the wrong `area`, or the invalidation never reached `lv_refr` (missing `lv_obj_invalidate()` on a style/content change that doesn't go through a standard LVGL setter). Distinct from tearing: the pixel data sent was simply never sent, not sent-while-being-overwritten.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          | Use the repo's existing `--redrawlog on` / `[REDRAW]`/`[REFR]` instrumentation (`docs/tdeck-findings-20260828.md` §1) to confirm an `[INVAL]`→`[REFR]`→`[FLUSH]` chain exists for the missing region; if `[INVAL]` never fires, the bug is in the object-update code (out of this track's scope), not the buffer/refresh config. |
| **Ghosting** (a previous frame's content bleeds through under new, semi-transparent or non-fully-opaque content) | Only meaningful with `screen_transp`/alpha blending against a buffer that wasn't cleared, or — more relevant here — a `direct_mode` misuse where the persistent buffer's old pixels under a since-shrunk/moved object were never actually repainted (their area was never re-invalidated because the _new_ layout doesn't cover them). Not applicable in this repo's current single-buffer, non-direct-mode, opaque-background configuration; flag only if `direct_mode` is adopted later.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         | Ensure whatever moved/resized/hid an object invalidates **both** its old and new bounding rectangle, not just the new one — this is a general LVGL object-update discipline issue, independent of buffer config.                                                                                                                 |
| **"Half the screen updates"**                                                                                    | Almost always the single-buffer-plus-chunking behavior working as designed but visually surprising: a large invalidated area spanning more rows than `get_max_row()` allows gets flushed in multiple `setAddrWindow`+`pushColors` calls; if one of the chunk flushes is skipped (e.g. an early `return` in `disp_flush()` on a semaphore-take failure) only part of the area reaches the panel. In this repo, `xSemaphoreTake(xSemaphore, portMAX_DELAY)` cannot itself silently skip (infinite wait), but any _future_ change to a timed take (`pdMS_TO_TICKS(...)` instead of `portMAX_DELAY`) reintroduces exactly this risk — a timed-out flush must not silently drop the chunk and still claim success.                                                                                                                                                                                                                                                                                                                                                                                                      | Keep flush-time semaphore/mutex waits unbounded (`portMAX_DELAY`) on this single-core-contended resource, or explicitly handle a timeout by retrying rather than dropping the chunk.                                                                                                                                             |
| **Flicker**                                                                                                      | Typically `full_refresh=1` redrawing the whole screen (including unchanged background) every cycle at a visible cadence, especially combined with a background fill pass that briefly shows a solid color before the foreground redraws on top. Partial refresh (this repo's direction) directly fixes this class, since unrelated pixels are never touched.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       | Already the motivation for the `full_refresh=0` experiment on this branch; no further buffer-side fix needed for this specific artifact once invalidation correctness (out of scope here) is solid.                                                                                                                              |
| **Wrong colours after switching `full_refresh` 1→0**                                                             | Not a buffering-mode effect per se (`disp_flush()` reads `area`/`color_p` correctly regardless of mode, Finding — see TL;DR #2). If this is actually observed, suspect: (a) a widget that painted itself relying on **residual content from the previous full-screen redraw** being present underneath it (e.g. assumed the background was always freshly cleared because `full_refresh=1` always cleared+redrew everything) and now shows the truly-stale pixels from whatever was there before partial refresh started only touching the changed rectangle; (b) an `LV_OPA_COVER`-adjacent transparency bug that was invisible when the whole screen (including the object's true background) was always repainted together.                                                                                                                                                                                                                                                                                                                                                                                     | Audit any custom drawing code (canvas objects, direct buffer pokes outside standard LVGL widgets, if any exist in this codebase) for an implicit assumption that "the area under me is always freshly painted this frame."                                                                                                       |
| **Driver assumes the whole frame is always sent** (why `full_refresh=1` "worked")                                | Some display drivers/controllers, or downstream code (e.g. a screenshot/CRC feature, a second consumer of the framebuffer, external mirroring) may have been written assuming every `flush_cb` call carries a **complete, self-consistent** frame — reasonable under `full_refresh=1` (every flush _is_ a complete frame) but silently wrong under partial refresh (a flush only carries **one rectangle**, and the "current frame" is only ever fully consistent >**on the physical panel's own GRAM**<, never in host RAM, once a small buffer is in use). **This repo has exactly this pattern**: `tdeck_dbg_screencrc()`/`tdeck_dbg_screendump` style tooling that reads back pixels **from the panel itself** (`tft.readRect(...)`, not from a host-side framebuffer) is correctly immune to this (it reads the authoritative post-flush state), but any _future_ debug/mirroring feature that tries to read from `buf`/`draw_buf.buf_act` directly and treat it as "the current screen" would be wrong under partial refresh — it only ever holds the most recently flushed rectangle, not the whole screen. | Any tooling that wants "what's on screen right now" must read it from the panel (`tft.readRect`, as the existing CRC tool does) or maintain its own full-screen shadow buffer updated incrementally per flush — never assume `draw_buf.buf1`/`buf2` holds a complete frame once buffers are smaller than the screen.             |

**Source.** Repo-derived reasoning against the exact vendored `lv_refr.c`/`tdeck_main.cpp`/`tdeck_debug.cpp`
control flow already cited above; general artifact taxonomy is standard embedded-graphics knowledge,
not independently sourced per-row beyond what's cited.

### 8. `sw_rotate`/`rotated` cost, and the manual `hor_res`/`ver_res` swap

**Claim.** This repo does **not** use LVGL's rotation machinery at all: `disp_drv.rotated` and
`disp_drv.sw_rotate` are both left at their zero default (`lv_disp_drv_init()` zeroes the whole struct,
`lib/lvgl/src/hal/lv_hal_disp.c:84-97`, confirmed neither field is touched in `setupLvgl()`). Instead:

- `tft.setRotation(1)` (`tdeck_main.cpp:125`) puts the **physical panel driver** into landscape mode —
  TFT_eSPI's own coordinate remap for the ST7789 controller handles turning logical (x,y) writes into
  the correct GRAM addresses for the rotated orientation; this happens entirely inside TFT_eSPI/the
  panel controller, LVGL never sees it.
- `disp_drv.hor_res = TFT_HEIGHT (320); disp_drv.ver_res = TFT_WIDTH (240);` (`tdeck_main.cpp:394-395`)
  tells LVGL the display's _logical_ resolution matches the now-rotated 320×240 landscape geometry
  (`TFT_WIDTH`/`TFT_HEIGHT` in `Setup210_LilyGo_T_Deck.h` are the panel's native, pre-rotation
  240×320 portrait dimensions — the swap in `tdeck_main.cpp` is what turns that into 320×240 for
  LVGL).
- Touch: `TouchDrvGT911` is independently told about the rotated geometry —
  `touch.setMaxCoordinates(320, 240); touch.setSwapXY(true); touch.setMirrorXY(false, true);`
  (`tdeck_main.cpp:138-140`) — and `touchpad_read()` (`tdeck_main.cpp:806-829`) passes `x[0]`/`y[0]`
  straight through with no further transform, i.e. it trusts the touch driver's own rotation config to
  already match LVGL's 320×240 coordinate space.

**Is this correct?** **Yes.** This is the standard, lower-cost pattern for TFT_eSPI + LVGL when the
underlying panel driver natively supports the rotation you want (ST7789 does, via its `MADCTL`
register, which is exactly what `tft.setRotation()` programs) — `sw_rotate`/`rotated` exist in LVGL
for the opposite situation: a panel/driver that has **no** native rotation support, forcing LVGL to
transpose pixel data in software before handing it to `flush_cb` (`lv_refr.c`'s `draw_buf_rotate_90`/
`draw_buf_rotate_180` functions, real CPU cost per flushed pixel — `LV_ATTRIBUTE_FAST_MEM`-tagged in
source, i.e. LVGL's own authors consider it hot enough to warrant a fast-memory placement hint). Using
`sw_rotate` here as well as `tft.setRotation()` would double-rotate (or at best redundantly burn CPU
re-deriving what the panel already does natively) — correctly avoided.

**What it "breaks" for touch (only if changed carelessly).** The touch coordinate mapping is **not**
automatically consistent with LVGL's `hor_res`/`ver_res` swap — it is a _separate_, manually configured
transform on the GT911 driver (`setSwapXY`/`setMirrorXY`/`setMaxCoordinates`) that happens to have been
set to match. If `tft.setRotation()`'s argument is ever changed (e.g. to `3` to flip orientation, or to
support a settings-driven screen-rotation feature), the touch driver's `setSwapXY`/`setMirrorXY`
arguments **must be updated in lockstep** — there is no code-level link between the two; getting this
out of sync produces touch input that is offset, mirrored, or transposed relative to what's drawn,
while the display itself looks fine. This is a real, silent-desync risk for any future
rotation-related feature work, not a bug in the current fixed-orientation configuration.

**Source.** All read directly from `lib/lvgl/src/hal/lv_hal_disp.c:84-97`,
`lib/lvgl/src/core/lv_refr.c` (`draw_buf_rotate_90`/`_180`, `LV_ATTRIBUTE_FAST_MEM` tag),
`src/t-deck/tdeck_main.cpp:125,138-140,394-395,806-829`,
`lib/TFT_eSPI/User_Setups/Setup210_LilyGo_T_Deck.h:6-7`.

### 9. Measured / computed SPI throughput expectations

**Claim — arithmetic (verifiable by the coding agent from first principles).**

- Full 320×240×16bpp frame = 76 800 px × 16 bit = 1 228 800 bits = 153 600 bytes.
- Standard 4-wire SPI (this hardware, not quad/octal for the TFT) transfers 1 data bit per SPI clock
  cycle: time = bits / clock_hz.
  - At 40 MHz (current `SPI_FREQUENCY`): `1 228 800 / 40 000 000 = 0.0307 s = 30.7 ms` (data phase
    only, excludes command bytes, CS setup, and any software loop overhead).
  - At 80 MHz (untested candidate): `15.36 ms` data phase.
- **Repo's own measurement** (`docs/tdeck-findings-20260828.md`, `full_refresh=1` baseline, commit
  `15ab3897`): full-screen flush (`INSTR-FLUSH`) avg **36.7 ms**, max 36.8 ms, at 76 800 px per flush.
  `36.7 - 30.7 = 6.0 ms` of overhead beyond the theoretical SPI data-phase minimum — attributable to
  `setAddrWindow()` command/parameter bytes, the blocking `pushColors()` software loop's per-chunk FIFO
  refills (it is not DMA, so the CPU is actively pumping the hardware SPI TX FIFO in software), and the
  `xSemaphoreTake`/`Give` pair. This is a good, internally consistent cross-check: the arithmetic model
  and the measured number agree to within ~20%, with the delta fully explained by known non-data-phase
  costs.
- **Achievable frame rate, full-screen, blocking, 40 MHz**: `1000 / 36.7 ≈ 27` flushes/s in principle
  for flush time alone; the repo's own full **render+flush** cycle (`monitor_cb`) measured **56.9 ms
  mean / 61 ms max**, i.e. ~17.6 Hz achievable full-screen refresh rate including LVGL's own CPU-side
  rendering cost, not just SPI. (Measured _actual_ cadence was only 2.52/s in that baseline run because
  the workload only asked for a repaint every ~400 ms — the 17.6 Hz figure is the ceiling, not the
  observed rate.)
- **Achievable frame rate, partial refresh (measured, `full_refresh=0`)**: idle px/s dropped 14x (193
  280 → 13 859 px/s) and mean refresh time dropped to **7.7 ms** (`docs/tdeck-findings-20260828.md`
  §3) — consistent with only a small header-label region (order of a few thousand px) being redrawn
  per tick instead of the full 76 800 px frame; `7.7 ms` at ~30 ms/full-frame-at-40MHz implies roughly
  a ~10-15% of full-screen area being touched per refresh in that workload, matching "two header
  labels" being a small fraction of 320×240.
- **80 MHz projection (not measured on this board)**: if the 6 ms fixed overhead is assumed roughly
  constant (it is dominated by command bytes + semaphore + FIFO-refill call overhead, not the clock
  rate itself, though the FIFO-refill component would shrink somewhat too), a full-frame blocking
  flush would land around `15.36 + ~5-6 ≈ 20-21 ms`, i.e. roughly **45-50 Hz** flush-only ceiling —
  worth confirming experimentally with the existing harness before relying on this number for any
  frame-budget decision.

**Source.** Arithmetic is self-contained (bits ÷ Hz), verifiable independently. Measured figures:
`docs/tdeck-findings-20260828.md` §§0, 2, 3 (cited, not reproduced wholesale per shared-context
instruction — pull the exact tables from that file if more precision is needed than reproduced above).

## Rules to hand the coding agent

1. Do not "fix" the buffering _mode_ (single vs double buffer, `full_refresh`, `direct_mode`) as a
   response to redraw-not-appearing bugs. First rule out a missing `lv_obj_invalidate()` call — the
   current single full-screen-buffer + `full_refresh=0` combination is legal and functionally correct
   per LVGL's own source (Finding 1).
2. Before resizing the draw buffer, re-verify the pixel-count-vs-byte-count invariant from Finding 2 by
   inspection: the `lv_disp_draw_buf_init()` 4th argument and the `heap_caps_malloc`/`ps_malloc` byte
   size must be derived from **one** shared pixel-count constant, never written as two independent
   expressions.
3. Do not shrink the draw buffer below LVGL's documented 1/10-screen floor (≥ 7 680 px / 15 360 B for
   this panel) without a specific, measured reason — smaller buys negligible RAM savings and costs
   real flush-call overhead (Finding 3), and risks the `max_row == 0` hang class if it ever drops below
   one full scanline.
4. If DMA is introduced (`tft.initDMA()` + `pushPixelsDMA`/`pushImageDMA`), the draw buffer **must**
   move to `heap_caps_malloc(size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)` (internal DRAM), never
   `ps_malloc`/PSRAM (Finding 4). Leave `LV_MEM_CUSTOM_ALLOC = ps_malloc` (the LVGL object heap)
   untouched in PSRAM — that allocation is correctly placed today.
5. Real non-blocking flush requires **two** small buffers (Finding 1, mode B) — DMA alone on a single
   or full-screen-sized buffer does not unlock render/flush overlap, because LVGL's own
   `refr_area_part()` wait (`buf1 && !buf2`, or `full_sized`) still serializes it.
6. Before shipping a DMA `flush_cb`, resolve and test the `endWrite()`/CS-deassert timing question
   raised in Finding 5 — do not assume `pushPixelsDMA()` + immediate `endWrite()` +
   `lv_disp_flush_ready()` is safe without checking it against real hardware with the existing
   redraw-log/CRC harness (`docs/tdeck-findings-20260828.md`).
7. Extend `xSemaphore` to guard **every** SPI transaction that touches the shared TFT/SD bus, including
   SD file I/O used by the audio subsystem — today only the TFT flush and the debug panel-readback path
   take it (Finding 6). This is a correctness gap independent of any buffering/DMA change.
8. If pursuing the SPI-host-separation fix for TFT vs SD (Finding 6), sequence it **before** adding
   `tft.initDMA()` — `initDMA()` calls `spi_bus_initialize()` itself and will abort
   (`ESP_ERROR_CHECK`) if it targets a host already claimed by `SPI.begin()` for the SD card.
9. Keep `disp_drv.rotated = 0` and `disp_drv.sw_rotate = 0`. The manual `hor_res`/`ver_res` swap +
   `tft.setRotation(1)` pattern is correct and should not be replaced with LVGL-side rotation (Finding
   8). If display orientation ever becomes runtime-configurable, update `tft.setRotation()` and the
   GT911 `setSwapXY`/`setMirrorXY`/`setMaxCoordinates` calls together — they are not linked by any
   shared code path today.
10. Any future debug/mirroring tool that wants "the current full-screen contents" must read from the
    panel (`tft.readRect`, as `tdeck_dbg_screencrc()` already does), never from `draw_buf.buf1`/`buf2`
    directly once buffers are smaller than the screen (Finding 7, "driver assumes whole frame" row).
11. Treat 80 MHz `SPI_FREQUENCY` and the exact `endWrite()`/DMA-CS timing as **experiments to run and
    measure**, not settings to flip alongside other changes — both are plausible-but-unverified on this
    exact board per Findings 5 and 9's arithmetic section.

## Open questions / UNVERIFIED

- Exact PSRAM read/write throughput multiplier vs internal SRAM for this specific T-Deck Plus PSRAM
  chip/speed grade — confirmed PSRAM is markedly slower and DMA-incompatible without a bounce copy on
  this IDF version, but no board-specific number was found in the time budgeted.
- Whether the arduino-esp32 2.x / IDF 4.4.x backport used by `espressif32@6.6.0` includes any form of
  `SPI_TRANS_DMA_USE_PSRAM` (an IDF 5.x feature) — assumed **not** present; TFT_eSPI's own vendored DMA
  code sets `trans.flags = 0` regardless, so even if the flag existed in the underlying IDF it is not
  being used by this library version.
- Exact `endWrite()`/CS-deassert-vs-DMA-in-flight interaction for `initDMA(ctrl_cs=false)` in this
  exact TFT_eSPI version (Finding 5) — reasoned from source but not empirically confirmed; flagged as
  the single highest-risk unverified claim in this document because it directly affects data
  integrity, not just performance.
- Whether 80 MHz SPI is actually reliable on the T-Deck Plus's specific PCB/panel — plausible from
  general community experience with short fixed traces, not measured on this unit.
- Full content of the ESP-IDF v6.0.1 "Sharing the SPI Bus Among SD Cards and Other SPI Devices -
  ESP32-S3" page (title/existence confirmed via search, not fetched) — likely contains authoritative,
  version-specific API guidance beyond what was reconstructed here from community discussion; worth a
  direct fetch before implementing the SPI-host-separation fix.
- Whether `TDECK_TFT_CS` (GPIO 12) is genuinely software-controlled in hardware — the vendored
  `Setup210_LilyGo_T_Deck.h` comment says `// Not connected` next to `#define TFT_CS 12`, which
  contradicts the repo actively doing `pinMode(TDECK_TFT_CS, OUTPUT); digitalWrite(TDECK_TFT_CS,
HIGH)` on the same GPIO number during init (`tdeck_main.cpp:91,94`) and TFT_eSPI's `startWrite`/
  `endWrite` toggling CS through the same define. Treated the comment as stale/copy-pasted rather than
  a hardware fact, since CS clearly does need to work for the display to function at all — but this was
  not independently confirmed against a schematic.

## Sources

- `lib/lvgl/src/hal/lv_hal_disp.h` — vendored LVGL 8.3.11, driver struct + `lv_disp_draw_buf_init` doc
  comment (pixel count). Read directly.
- `lib/lvgl/src/hal/lv_hal_disp.c` — `lv_disp_drv_init` defaults, `lv_disp_flush_ready` implementation.
  Read directly.
- `lib/lvgl/src/core/lv_refr.c` — full refresh/flush/direct-mode/rotate control flow, `get_max_row`,
  `draw_buf_flush`, `refr_area`/`refr_area_part`. Read directly, this is the primary source for
  Findings 1-3, 7, 8.
- `src/t-deck/tdeck_main.cpp` — `setupLvgl()`, `disp_flush()`, `initTDeck()`, `touchpad_read()`. Read
  directly.
- `src/t-deck/tdeck_debug.cpp` — `tdeck_dbg_screencrc()` (panel readback, shared-bus CS defense). Read
  directly.
- `src/t-deck/lv_conf.h` — color depth, mem custom alloc, rotation-buffer constant. Read directly.
- `variants/t_deck_plus/configuration.h` — `TDECK_TFT_CS` pin definition. Read directly.
- `lib/TFT_eSPI/TFT_eSPI.h`, `lib/TFT_eSPI/Processors/TFT_eSPI_ESP32_S3.c`,
  `TFT_eSPI_ESP32_S3.h` — DMA API, `initDMA`/`pushPixelsDMA`/`pushImageDMA`/`dmaBusy`/`dmaWait`
  implementation, `SUPPORT_TRANSACTIONS` note, SPI host selection. Read directly.
- `lib/TFT_eSPI/User_Setups/Setup210_LilyGo_T_Deck.h` — active pin map, `SPI_FREQUENCY 40000000`.
  Read directly.
- `docs/tdeck-findings-20260828.md` — in-repo measured flush/refresh timings, full_refresh=1 vs 0
  comparison. Cited, not reproduced wholesale.
- LVGL 8.3 docs, Display porting guide — `https://lvgl.io/docs/open/8.3/porting/display` (redirected
  from `docs.lvgl.io/8.3/porting/display.html`), fetched 2026: buffer modes, 1/10-screen sizing
  recommendation, pixel-count clarification.
- ESP-IDF v4.4 SPI Master Driver docs —
  `https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32/api-reference/peripherals/spi_master.html`,
  fetched 2026: DMA buffer memory-capability and alignment requirements, bounce-buffer fallback
  behavior.
- WebSearch: ESP32-S3 `heap_caps_malloc`/`MALLOC_CAP_DMA`/PSRAM DMA constraints (general ESP-IDF heap
  docs family, `SPI_TRANS_DMA_USE_PSRAM` mention).
- WebSearch: TFT_eSPI + ESP32-S3 SPI frequency reliability discussions (Bodmer/TFT_eSPI GitHub issues,
  atomic14.com ESP32-S3/ST7789 hardware-SPI writeup).
- WebSearch + GitHub `Bodmer/TFT_eSPI` discussion #1885 ("Some thoughts about caveats with TFT_eSPI
  combined with SD card sharing same SPI bus"), and related discussions/issues #2717, #3601, #1132 —
  shared-bus MISO tristating and corruption reports, `initDMA`/non-DMA conflict reports.
- Espressif ESP-IDF docs, "Sharing the SPI Bus Among SD Cards and Other SPI Devices - ESP32-S3" (title
  confirmed via search only, not fetched in full) —
  `https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/sdspi_share.html`.
