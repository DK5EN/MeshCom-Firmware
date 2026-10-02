# Track 4 — Scrolling and scrollbars, pop-ups/modals, images and icons in LVGL 8.3

See `00-CONTEXT.md` for shared hardware/config facts; not repeated here.

## TL;DR for the coding agent

1. Every scroll step — drag, momentum, or an `LV_ANIM_ON` animated jump — calls `lv_obj_invalidate(obj)`
   on the **whole scrollable container**, every single step/frame. On this panel (no DMA, ~478 ns/px
   flush, measured 36.7 ms to push a full 320x240 frame) an animated `lv_obj_scroll_to_view` easily
   costs several full "partial-refresh" cycles' worth of SPI time. Prefer `LV_ANIM_OFF` for
   programmatic scrolls (append-to-bottom, tab switch) unless the animation is the point.
2. `msg_list_append_bubble()`'s fast path (`src/t-deck/lv_obj_functions.cpp:2793-2799`) is confirmed
   unbounded — it has no matching delete, unlike the model (`msg_tabs_trim_history`, cap
   `MSG_TAB_MAX_MESSAGES = 50`, `:182`). Fix: after appending, if `lv_obj_get_child_cnt(msg_list) >
MSG_TAB_MAX_MESSAGES`, delete the oldest wrapper (`lv_obj_get_child(msg_list, 0)`) with a plain
   `lv_obj_del()` — this call site is application code, not an event callback of the row being
   deleted, so `lv_obj_del_async` is not required here.
3. `lv_obj_del_async` is required only when the delete happens **inside `LV_EVENT_DELETE`** of the
   object (or an object in the branch being torn down) — LVGL's own doc comment for
   `lv_obj_del_async` says exactly this: "Useful for cases where you can't delete an object directly
   in an `LV_EVENT_DELETE` handler". Deleting an _ancestor_ from a **`LV_EVENT_CLICKED`** handler of a
   descendant (e.g. a delete button inside a message bubble) is safe with a plain `lv_obj_del()` —
   this is exactly what LVGL's own `lv_msgbox`'s built-in close button does
   (`lv_msgbox.c:198-208`, `msgbox_close_click_event_cb` calls `lv_msgbox_close()` → `lv_obj_del()`
   synchronously), and this repo's own `bubble_delete_event_cb` already does the equivalent
   (`lv_obj_functions.cpp:3101`, click on a bubble's trash icon triggers `msg_render_active_tab()` →
   `lv_obj_clean(msg_list)`, deleting the very wrapper that owns the button that fired the event).
   Both are safe because `lv_obj_del()` fires an indev "reset query" that clears any indev
   pointer to the deleted subtree before the next input read (`lv_indev.c:86`,
   `indev_proc_reset_query_handler`). Do not "fix" this existing pattern to be async — it is already
   correct.
4. This repo has **no working pop-up/modal/toast code at all**: `lv_obj_functions.h:19` declares
   `void lv_msgbox(char* hinweis, char* mtext);` but it is never defined anywhere in `src/`
   (`grep -rn "lv_msgbox" src/` finds only the declaration) — it is dead, unused code, not a live
   custom wrapper. Any pop-up/dialog feature must be built from scratch against LVGL's real
   `lv_msgbox_create()` API (`LV_USE_MSGBOX 1` is already enabled in `lv_conf.h:553`). Do not reuse
   or extend the declared-but-undefined `lv_msgbox()`.
5. The map view is already doing the right thing for tile drawing and this is a confirmed non-issue:
   `sdmap_load_tile()` (`tdeck_sdmap.cpp`) decodes the PNG **once** with `lodepng_decode32`, converts
   to a raw `lv_color_t` buffer in `ps_malloc` memory, and assigns it via `lv_img_dsc_t` with
   `cf = LV_IMG_CF_TRUE_COLOR` and `LV_IMG_SRC_VARIABLE`. With `LV_IMG_CACHE_DEF_SIZE == 0` (both
   this repo's `lv_conf.h:147` and the LVGL 8.3 built-in default), the decoder's `open` and `close`
   run on **every redraw** of that `lv_img` — but for a `TRUE_COLOR` + variable source, "open" is a
   single pointer assignment (`lv_img_decoder.c:390-397`), not a decode. So cache=0 costs nothing
   extra here. It would cost a full re-decode per redraw only if the source were a **file** (PNG/BIN)
   or `LV_IMG_CF_INDEXED_*` (palette rebuilt on every open, `lv_img_decoder.c:404-451`) — do not
   introduce either of those for the map tile path.
6. Dropdowns: `lv_dropdown_open()` reparents the pre-created (hidden) options list to
   `lv_obj_get_screen(dropdown_obj)`, **not** `lv_layer_top()`
   (`lib/lvgl/src/widgets/lv_dropdown.c:462`, `lv_obj_set_parent(dropdown->list,
lv_obj_get_screen(dropdown_obj))`) — correct any assumption otherwise. The list object itself is
   allocated once at `lv_dropdown_create()` time and only shown/hidden/reparented on open/close, so
   opening a dropdown is not a fresh allocation.
7. `lv_obj_set_scrollbar_mode(msg_list, LV_SCROLLBAR_MODE_AUTO)` is already set correctly
   (`lv_obj_functions.cpp:1445`); the scrollbar is a style part (`LV_PART_SCROLLBAR`), not a child
   object, drawn from `lv_obj_scroll.c` and invalidated via `lv_obj_scrollbar_invalidate()`
   (`:622`), which explicitly invalidates the scrollbar strip in addition to whatever else moved.
8. An opaque overlay is cheap; a semi-transparent one is not, because of `LV_LAYER_SIMPLE_BUF_SIZE`
   (`lv_conf.h:139`, `24 * 1024` bytes = 12,288 pixels at 16bpp). Any object drawn with
   `style_opa < 255` (a translucent modal backdrop, a toast with a shadow) is composited into a
   "simple layer" buffer chunk-by-chunk if it's bigger than that many pixels — a full-screen
   320x240 (76,800 px) translucent backdrop needs **~7 chunk round-trips** of render-to-layer +
   blend-back before the final `flush_cb` push, all software, all on one core. Avoid full-screen
   semi-transparent backdrops on this hardware; use an opaque or `LV_OPA_COVER` backdrop, or size the
   overlay to only the area that actually needs to look layered.

## Findings

### F1. Animated scroll invalidates the whole container on every step

**Claim:** `lv_obj_scroll_to()`/`lv_obj_scroll_to_x/y()`/`lv_obj_scroll_by()` with `anim_en =
LV_ANIM_ON` drive an LVGL animation that calls the internal `_lv_obj_scroll_by_raw()` on every
animation tick; that function unconditionally ends with `lv_obj_invalidate(obj)` — the **whole**
scrollable object's own area, not just the delta strip — plus it repositions every child's cached
coordinates via `lv_obj_move_children_by()` on every tick.

**Why:** LVGL's scroll model repaints by area invalidation, and the scrollable container is
invalidated as one rectangle each step because content underneath moved; LVGL 8.3 has no
"blit-and-shift" fast path for scrolling.

**Symptom if violated (i.e. if you assume scrolling is cheap and animate liberally):** on a
single-buffered, no-DMA SPI panel, an animated scroll of e.g. the ~300x180 px message list area
(54,000 px) at the measured ~478 ns/px full-refresh flush rate costs roughly 26 ms of SPI push
_per animation frame_, on top of the LVGL render (rect/label draws) for every bubble redrawn that
frame. A default LVGL scroll animation runs several hundred ms at the default `LV_DISP_DEF_REFR_PERIOD
10` ms tick — i.e. potentially dozens of full 26 ms+ pushes for one scroll gesture, visible as multi-
hundred-ms UI stalls, worse if `disp_drv.full_refresh` is 1 (a full 320x240 flush, ~36.7 ms measured,
regardless of what changed).

**Fix:**

```c
/* Prefer this for programmatic "jump to bottom" / tab-switch scrolls: */
lv_obj_scroll_to_view(last, LV_ANIM_OFF);   /* one paint, not N */

/* Only use LV_ANIM_ON where the animation is user-facing and short, e.g.
   a manual "scroll to new message" affordance the user explicitly triggered. */
```

Also keep `msg_list`'s scrollable area no larger than the visible viewport (it already is, since it
lives inside a tab content object sized to the screen) — do not make the scroll container taller
than necessary, since the invalidated rectangle is the container's own geometry, not the visible
clip only, when using non-`LV_OBJ_FLAG_CLIP_CORNER`/default draw paths this still clips to the
container's own box, so keeping the container small (the tab page, ~300x180) rather than screen-sized
already helps.

**Source:** `lib/lvgl/src/core/lv_obj_scroll.c:64,424,` `_lv_obj_scroll_by_raw` (search
`lv_obj_invalidate(obj); return LV_RES_OK;` at the end of that function); measured flush numbers from
`docs/tdeck-findings-20260828.md` lines 40-44, 56-57 (avg flush 36.7 ms full screen, partial-refresh
mean 7.7 ms, `pushColors` no-DMA blocking).

### F2. `LV_OBJ_FLAG_SCROLL_ELASTIC` / `SCROLL_MOMENTUM` / `SCROLL_ONE` / `SCROLL_CHAIN_*`

**Claim:** These are `lv_obj` flags (`lv_obj_add_flag`/`clear_flag`), not separate setter functions.
`LV_OBJ_FLAG_SCROLLABLE = (1L<<4)`, `SCROLL_ELASTIC = (1L<<5)`, `SCROLL_MOMENTUM = (1L<<6)`,
`SCROLL_ONE = (1L<<7)`, `SCROLL_CHAIN_HOR = (1L<<8)`, `SCROLL_CHAIN_VER = (1L<<9)`, `SCROLL_CHAIN =
HOR|VER`. All are **on by default** on any object created with default flags plus
`LV_OBJ_FLAG_SCROLLABLE` (containers created via `lv_obj_create` get scrollable+elastic+momentum+
chain by default in 8.3).

- `SCROLL_ELASTIC`: allows dragging past the scroll limit with a slowed rubber-band effect, then
  snap-back animation on release — the snap-back is itself an `LV_ANIM_ON` scroll (see F1), so
  elastic overscroll on a long message list produces an extra invalidation-heavy animation on every
  drag that goes past an edge.
- `SCROLL_MOMENTUM`: continues scrolling (another animation) after a fast flick/release — same cost
  profile as F1, but user-triggered and unbounded in duration until it decelerates to zero.
- `SCROLL_ONE`: restricts one scroll gesture to move exactly one snappable child (needs scroll-snap
  set on an axis) — not relevant unless snapping is added.
- `SCROLL_CHAIN_HOR/VER`: whether reaching this object's scroll limit hands the remaining scroll
  delta to its parent (e.g. dragging inside `msg_list` at its top propagating into the tab view).

**Why relevant here:** momentum and elastic are both "free" animations the framework starts on your
behalf from touch/trackball input; on this hardware every frame of those animations is a real SPI
flush. They are not free CPU-side conveniences the way they might be on a GPU-composited system.

**Symptom if left on unexamined:** flicking the message list produces a multi-second train of
full-container invalidations while momentum decays — feels laggy/stuttery, and on a shared TFT/SD
SPI bus (context: `xSemaphore`), any concurrent SD read (e.g. loading another map tile) queues behind
that same bus and stalls the momentum-driven flush chain further.

**Fix (recommendation, not yet applied in repo):**

```c
lv_obj_clear_flag(msg_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);   /* stop after release, no coast */
lv_obj_clear_flag(msg_list, LV_OBJ_FLAG_SCROLL_ELASTIC);    /* hard-stop at the ends, no rubber-band anim */
```

This trades a "polished" feel for materially fewer forced full-container repaints on drag-release.
Evaluate against actual touch feel before committing — this is a UX trade-off the operator should
confirm, not a pure bug fix.

**Source:** `lib/lvgl/src/core/lv_obj.h:94-102` (flag bit definitions and doc comments);
`lib/lvgl/8.3` scroll overview (WebFetch of `https://lvgl.io/docs/open/8.3/overview/scroll`,
confirms elastic/momentum/scroll_dir/snap semantics as summarized above) — UNVERIFIED: the exact
default-on flag set for `lv_obj_create()` was not independently re-verified against the 8.3
`lv_obj_class` default style in this pass; treat "on by default" as consistent with long-standing
LVGL behavior but confirm with `lv_obj_has_flag()` if precision matters.

### F3. `lv_obj_set_scrollbar_mode()` and scrollbar draw cost

**Claim:** `void lv_obj_set_scrollbar_mode(lv_obj_t *obj, lv_scrollbar_mode_t mode)` with
`LV_SCROLLBAR_MODE_OFF` (never), `_ON` (always), `_ACTIVE` (only while actively scrolling), `_AUTO`
(only when content overflows). The scrollbar is **not** a child `lv_obj_t` — it is drawn as
`LV_PART_SCROLLBAR`, a style part read via `lv_obj_get_style_*(obj, LV_PART_SCROLLBAR)` (width,
color, opa, padding, radius all stylable) and painted by `lv_obj_scroll.c`'s internal draw routine
during the object's own draw pass. `lv_obj_scrollbar_invalidate(obj)` (`lv_obj_scroll.c:622`)
invalidates just the scrollbar's hor/ver rectangle areas — it is called whenever the scrollbar needs
a repaint independent of content (e.g. mode changes) and is also implicitly covered any time the
whole container is invalidated (F1), since the scrollbar is drawn as part of that same object's
render.

**Why it matters on this hardware:** `LV_SCROLLBAR_MODE_ON` (always visible) adds a persistent
thin strip to the invalidated/redrawn area on literally every scroll-container repaint, even ones
that had nothing to do with scrolling (e.g. any content invalidation inside `msg_list` while a
scrollbar is shown repaints the strip too, because the whole container area is one invalidation
rectangle under `full_refresh`, or an overlapping rectangle union under partial refresh). `_AUTO`
avoids this entirely when content fits.

**This repo's current settings are already reasonable:** `msg_list` uses `LV_SCROLLBAR_MODE_AUTO`
(`lv_obj_functions.cpp:1445`); most setup-screen fields correctly use `_OFF` since they are not
meant to scroll, and a few (`setup_wifissid`, `setup_wifipassword`, `setup_utc`, `setup_txpower`,
`setup_stone`, `setup_mtone`) use `_AUTO`. No `_ON` (always-visible) scrollbar exists in the current
codebase — do not add one without a specific reason; prefer `_AUTO`.

**Fix/rule:** default to `LV_SCROLLBAR_MODE_AUTO` for any new scrollable widget; only use `_ON` if a
persistently-visible scroll affordance is a deliberate UX requirement, and if so, accept the extra
strip repaint cost as a known trade-off.

**Source:** `lib/lvgl/src/core/lv_obj_scroll.h:33-36` (enum), `:63` (setter signature), `:290`
(`lv_obj_scrollbar_invalidate` declaration), `lv_obj_scroll.c:622-631` (implementation, invalidates
hor/ver scrollbar areas only if their computed size is > 0); grep of `src/t-deck/lv_obj_functions.cpp`
for `set_scrollbar_mode` confirms current usage as listed above.

### F4. The message-list widget choice: current per-message `lv_obj` tree vs `lv_list`/`lv_table`/`lv_textarea`

**Claim:** The current implementation (`msg_list_append_bubble`, `lv_obj_functions.cpp:2842+`)
builds, per message, a small `lv_obj` tree: `wrapper` (flex column, 100% width) → `bubble_obj`
(flex column, max-width 85% of screen, own style) → `header_row` → (label(s), optional `del_btn`
button + its own label). That is **5-9 `lv_obj`s per message** depending on whether a delete button
is present (System messages skip it) — matching the already-established repo finding of "~9 LVGL
objects (~1.9 KB PSRAM) per message" for the worst case (`docs/tdeck-gui-verdict.md` finding H1).

Compared against the three plausible alternatives for this hardware:

| Widget                                                          | Per-row memory                                                                                                                                                                                                                              | Redraw cost                                                                                                                                                                 | Fits variable-height wrapped bubbles + per-sender color + delete button?                                                                                                                |
| --------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Current: nested `lv_obj` + `lv_label` (as-is)                   | ~1.9 KB/msg (9 objs) worst case                                                                                                                                                                                                             | Each bubble is its own invalidatable subtree; full active-tab switch does `lv_obj_clean()` + full rebuild (`msg_render_active_tab`) — O(N) object churn on every tab switch | Yes — this is exactly why it was built this way                                                                                                                                         |
| `lv_list` (`LV_USE_LIST 1`, already enabled)                    | `lv_list_add_btn`/`add_text` still creates a button/label per row — comparable object count to current, but less layout flexibility (single-line-oriented rows, no easy per-row max-width flex-wrap bubble)                                 | Similar per-row invalidation profile                                                                                                                                        | Poor fit — no first-class support for right/left-aligned variable-width wrapped bubbles with an embedded delete icon                                                                    |
| `lv_table` (`LV_USE_TABLE 1`, already enabled)                  | Cheaper per logical row (grid cells, not full `lv_obj` subtrees) — but **fixed row height model**, cell content is plain text draw, no per-cell child widgets (no delete button per row), no natural word-wrap-to-fit variable bubble width | Whole-table row-height recompute on any cell text change is coarser-grained; not built for chat bubbles                                                                     | Poor fit — table cells are not object trees, so "delete this message" needs custom hit-testing, and multi-line variable-height wrapping isn't table's model                             |
| `lv_textarea` (`LV_USE_TEXTAREA 1`, already enabled), read-only | One object, one growing text buffer                                                                                                                                                                                                         | Cheapest to redraw for pure append (single text object, no per-message subtree)                                                                                             | No per-message styling (colors, alignment, delete button, timestamp) — would need to fake all bubble semantics inline in text, losing the whole delete-by-message and colored-bubble UX |

**Recommendation:** Keep the current per-message `lv_obj` tree approach — it is the only one of the
four that supports the actual UX (colored/aligned bubbles, per-message delete, wrapped variable
width) without hacks, and PSRAM (where LVGL's heap lives, `LV_MEM_CUSTOM_ALLOC ps_malloc`) has
headroom (H1 correction: ~2.8 KB/message in PSRAM, ~2,800-message headroom before exhaustion on an
8.13 MB PSRAM budget, not the ~390 originally estimated — see `docs/tdeck-gui-verdict.md`
"Corrections entered after the verdict was written"). The real defect is not the widget choice, it
is the **lack of trimming on the fast path** — fix that (F5/F6) rather than migrating widgets.

**Source:** `src/t-deck/lv_obj_functions.cpp:2842-2980` (`msg_list_append_bubble` body, read in full
during this research pass); `src/t-deck/lv_conf.h:507,512,547` (`LV_USE_TEXTAREA`, `LV_USE_TABLE`,
`LV_USE_LIST` all `1`); `docs/tdeck-gui-verdict.md` finding H1 and its later correction (PSRAM, not
internal heap, ~2.8 KB/message).

### F5. Trimming the append-only message list: the exact fix

**Claim:** `msg_tabs_add_message()`'s fast path for the already-open tab
(`lv_obj_functions.cpp:2793-2799`) is:

```c
if (index == msg_active_tab_index)
{
    // Already active, just append to view
    msg_list_append_bubble(bubble);

    lv_obj_t *last = lv_obj_get_child(msg_list, -1);
    if(last != NULL)
        lv_obj_scroll_to_view(last, LV_ANIM_ON);
}
```

This appends unconditionally. The model was already trimmed to `MSG_TAB_MAX_MESSAGES = 50` two
lines earlier (`entry->bubbles.push_back(bubble); msg_tabs_trim_history(entry->bubbles);` at
`:2737-2738`), but the _view_ (`msg_list`'s children) has no matching trim call anywhere on this
path. `lv_obj_clean(msg_list)` only happens in `msg_list_clear()`, reached from
`msg_render_active_tab()` (full tab-switch rebuild) or `msg_list_show_hint()` — never from this fast
path. So every message delivered onto the **currently open** tab grows the view by one bubble
subtree forever, confirmed on hardware (60 injected messages → model stayed at 50, view grew to 60,
`docs/tdeck-gui-verdict.md` "H1 measured on hardware").

**Why it happens:** the fast path was written to optimize the common case (avoid a full
`lv_obj_clean()` + rebuild of all 50 rows on every incoming message) but the optimization only
handled the "add" side, not the "evict oldest" side.

**Symptom if left unfixed:** unbounded PSRAM growth (~2.8 KB/message, per the corrected H1 estimate)
for as long as a group tab stays open and active while messages keep arriving — on the order of
thousands of messages before exhaustion, but real and unbounded, and it silently "heals" the moment
the user switches tabs (which calls `msg_render_active_tab()` → clean + rebuild from the
already-trimmed model) — this is exactly why it survived investigation: the evidence disappears on
tab switch.

**Fix:**

```c
if (index == msg_active_tab_index)
{
    msg_list_append_bubble(bubble);

    /* Mirror the model trim (MSG_TAB_MAX_MESSAGES) on the view side. */
    uint32_t child_cnt = lv_obj_get_child_cnt(msg_list);
    while (child_cnt > MSG_TAB_MAX_MESSAGES)
    {
        lv_obj_t *oldest = lv_obj_get_child(msg_list, 0);
        if (oldest == NULL) break;
        lv_obj_del(oldest);   /* not _async: this runs from application code
                                  (message-arrival handling), not from oldest's
                                  own event callback — see F6/point 3. */
        child_cnt--;
    }

    lv_obj_t *last = lv_obj_get_child(msg_list, -1);
    if(last != NULL)
        lv_obj_scroll_to_view(last, LV_ANIM_OFF);   /* also apply F1: OFF, not ON, for an
                                                         append-to-bottom jump */
}
```

Deleting child index 0 repeatedly is O(N) per delete in LVGL's child array (`lv_obj_del` removes
from a `lv_ll` linked list plus a paired coordinate array depending on version internals) but N is
capped at 50 here, so this is cheap and bounded — do not defer this to `lv_obj_del_async` (that would
just queue up to 1-a-few pending deletes per message via `lv_async_call`, adding indirection for no
benefit since this is not the event-callback-reentrancy case async exists for).

**Source:** direct read of `src/t-deck/lv_obj_functions.cpp:2677-2842` (full `msg_tabs_add_message`
and `msg_list_append_bubble`), `:182` (`MSG_TAB_MAX_MESSAGES`), `:2368-2375` (`msg_list_clear`);
`docs/tdeck-gui-verdict.md` finding H1 and its hardware-measured correction.

### F6. When `lv_obj_del_async` actually is required

**Claim:** LVGL 8.3's own header comment on `lv_obj_del_async` is precise about the one case it
exists for: _"Helper function for asynchronously deleting objects. Useful for cases where you can't
delete an object directly in an `LV_EVENT_DELETE` handler (i.e. parent)."_
(`lib/lvgl/src/core/lv_obj_tree.h:71-78`). It works by queuing the delete via `lv_async_call()`
(`lv_obj_tree.c:121-124`), which runs the delete on the next `lv_timer_handler()`/`lv_task_handler()`
pass, outside the current event-dispatch call stack.

The general LVGL 8.3 event-safety rule (confirmed by reading `lv_indev.c` and by LVGL's own
`lv_msgbox` implementation, since the docs page for events did not state this explicitly when
fetched):

- Deleting an object, or an ancestor of the object, from that object's own **`LV_EVENT_CLICKED`**
  (or most other non-`LV_EVENT_DELETE`) handler is **safe with plain `lv_obj_del()`**. LVGL's indev
  read loop calls `indev_proc_reset_query_handler()` immediately after each read/process pass and
  before touching `indev_obj_act` again (`lv_indev.c:85-87`, comment: _"The active object might be
  deleted even in the read function"_), which clears any indev's cached pointer into a deleted
  subtree. LVGL's own built-in `lv_msgbox` close button relies on exactly this: its `CLICKED`
  handler calls `lv_msgbox_close(mbox)` → `lv_obj_del(mbox)` (or `lv_obj_del(parent)` for the
  modal-backdrop case) synchronously, deleting the very button that raised the event
  (`lib/lvgl/src/extra/widgets/msgbox/lv_msgbox.c:187-208`).
- Deleting an object **from inside its own `LV_EVENT_DELETE` handler** (e.g. cleanup code that tries
  to also delete a sibling/parent while the tree is already mid-teardown) is unsafe and needs
  `lv_obj_del_async` to defer the second delete to outside the current teardown call stack.
- As a corollary, deleting from inside a `for`/`while` loop that is itself iterating
  `lv_obj_get_child_cnt()`/`lv_obj_get_child(obj, i)` by index while also deleting children is
  fragile (indices shift) — F5's fix above works around this by always deleting index `0` and
  re-reading the count each iteration, never caching indices across a delete.

**Symptom if this rule is violated in the wrong direction (using `lv_obj_del_async` where plain
`lv_obj_del` would do):** not a crash, but every deletion is deferred by one `lv_task_handler()` tick
— for a chat delete-button click this adds a visible one-frame delay before the row disappears, and
for the F5 trim loop it would mean the child count check needs to run again next tick until it
converges, uselessly slower.

**Symptom if violated in the unsafe direction (synchronous delete from `LV_EVENT_DELETE`, or from a
context where LVGL has not yet run its reset-query pass, e.g. a raw FreeRTOS timer callback outside
`lv_task_handler`):** use-after-free / heap corruction as LVGL's event/indev machinery continues
touching a freed `lv_obj_t*` — this is the classic LVGL msgbox-close crash reported across the LVGL
forum/GitHub issues for versions before this safety mechanism existed, and remains a risk for any
hand-written `LV_EVENT_DELETE` handler that deletes further objects.

**Fix (pattern to use whenever a new modal/toast is added):**

```c
static void my_dialog_close_cb(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_current_target(e);
    lv_msgbox_close(mbox);        /* plain, synchronous, safe: CLICKED handler, not DELETE */
}

/* A timer-driven auto-close (toast), NOT inside any lv_obj event context: */
static void toast_timeout_cb(lv_timer_t *t)
{
    lv_obj_t *toast = (lv_obj_t *)t->user_data;
    lv_obj_del_async(toast);      /* safe from a timer callback too, and avoids any
                                      chance of deleting mid-render if the timer fires
                                      while LVGL is mid-draw of that object */
    lv_timer_del(t);
}
```

**Source:** `lib/lvgl/src/core/lv_obj_tree.h:71-78`, `lv_obj_tree.c:121-124`;
`lib/lvgl/src/extra/widgets/msgbox/lv_msgbox.c:187-208`; `lib/lvgl/src/core/lv_indev.c:74-88`.
The general "delete inside LV_EVENT_DELETE needs async" framing is the header's own words, not
inferred; the "CLICKED is safe" framing is inferred from the msgbox source + indev reset mechanism,
since the fetched 8.3 events doc page did not state it explicitly — mark that specific inference as
**UNVERIFIED against an explicit LVGL doc statement**, though it is corroborated by two independent
first-party code paths (LVGL's own msgbox, and this repo's own working `bubble_delete_event_cb`).

### F7. `lv_msgbox` in 8.3: creation, modal variant, closing

**Claim:** `lv_obj_t * lv_msgbox_create(lv_obj_t *parent, const char *title, const char *txt, const
char *btn_txts[], bool add_close_btn)`. Passing `parent == NULL` makes it a **modal** dialog: LVGL
allocates an auto-parent backdrop object (`lv_msgbox_backdrop_class`) sized to `LV_PCT(100) x
LV_PCT(100)` and created **on `lv_layer_top()`** (`lv_msgbox.c:66`, `lv_obj_class_create_obj(
&lv_msgbox_backdrop_class, lv_layer_top())`), then creates the actual message box as a child of that
backdrop, and tags it `LV_MSGBOX_FLAG_AUTO_PARENT` so `lv_msgbox_close()`/`_close_async()` know to
delete the backdrop (not just the box) when closing (`lv_msgbox.c:187-198`). Passing a real `parent`
skips the backdrop and the box behaves like an ordinary child object (non-modal, no dimming, no
input-blocking) — this is the distinction to design around: **modal = `parent == NULL`**, nothing
else makes it modal.

`lv_msgbox_get_active_btn()` / `_get_active_btn_text()` read which button (in the `lv_btnmatrix`
built from `btn_txts`) was pressed; read these from a `LV_EVENT_VALUE_CHANGED` handler on the
box, then call `lv_msgbox_close(mbox)` (safe synchronously per F6, since this is a normal
`VALUE_CHANGED`/`CLICKED` event, not `DELETE`).

**Why it matters for this hardware:** the default backdrop class (`lv_msgbox_backdrop_class`) in
stock LVGL 8.3 is typically styled with a semi-transparent dim background — see F8/point 7 for the
redraw-cost implication of that opacity on this panel. If a modal is added, either style the
backdrop opaque (cheap) or accept the layer-compositing cost documented in F8 for the translucent
default.

**Fix (worked pattern for this repo, given no existing `lv_msgbox` usage exists to copy from):**

```c
static const char *btns[] = {"OK", ""};

lv_obj_t *mbox = lv_msgbox_create(NULL, "Error", "SD card not found", btns, false);
lv_obj_add_event_cb(mbox, [](lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t *mbox = lv_event_get_current_target(e);
    lv_msgbox_close(mbox);   /* safe: VALUE_CHANGED, not DELETE; see F6 */
}, LV_EVENT_VALUE_CHANGED, NULL);
lv_obj_center(mbox);
```

**Source:** `lib/lvgl/src/extra/widgets/msgbox/lv_msgbox.c:60-113` (create, backdrop-on-layer_top),
`:187-198` (close/close_async, auto-parent handling), `lv_msgbox.h:63-87` (full public API read in
full). Confirmed against repo: `LV_USE_MSGBOX 1` (`lv_conf.h:553`); no live usage exists
(`grep -rn "lv_msgbox" src/` → only the dead declaration at `lv_obj_functions.h:19`).

### F8. `lv_layer_top()` / `lv_layer_sys()`, input-swallowing, and the real cost of translucency

**Claim:** `static inline lv_obj_t * lv_layer_top(void)` and `lv_layer_sys(void)`
(`lib/lvgl/src/core/lv_disp.h:191,200`) return per-display singleton container objects sitting above
the normal screen stack (`disp->top_layer`, `disp->sys_layer`) — `top_layer` is meant for
application-level overlays (the modal `lv_msgbox` backdrop lives here, per F7), `sys_layer` is meant
for system-level overlays (e.g. an on-screen keyboard, cursor) that should sit above even
`top_layer` content. Neither is cleared automatically between screens; objects placed there persist
across `lv_scr_load()` calls, which is the point (they are meant to be screen-independent chrome).

To make an overlay swallow touch/trackball input so it doesn't fall through to whatever is
underneath, add `LV_OBJ_FLAG_CLICKABLE` to the overlay's root object — a clickable object with no
`LV_EVENT_CLICKED` handler still consumes the press (indev hit-testing stops at the first clickable
object under the point), which is the entire mechanism modal dialogs rely on to be modal.

**Why translucency is expensive here specifically:** any object drawn with `opa < LV_OPA_COVER`
(255) that is not a "simple" flat-color case gets composited through a **simple layer** buffer
(`LV_LAYER_SIMPLE_BUF_SIZE = 24 * 1024` bytes in this repo's `lv_conf.h:139`, with a
`LV_LAYER_SIMPLE_FALLBACK_BUF_SIZE = 3 * 1024` fallback if the 24 KB allocation fails). At
`LV_COLOR_DEPTH 16`, 24 KB = 12,288 pixels per layer-buffer chunk. A full-screen 320x240 (76,800 px)
translucent modal backdrop therefore cannot be composited in one pass — LVGL renders and blends it
**in ~7 chunks** (76,800 / 12,288 ≈ 6.25, rounded up), each chunk requiring: render the underlying
content for that chunk into the layer buffer, render/blend the translucent object on top, then copy
the blended result back into the real draw buffer — i.e. roughly double the pixel-touching work of
an opaque redraw of the same area, done in a no-DMA, single-core-blocking software pipeline, _before_
the final `flush_cb` SPI push (measured ~478 ns/px, ~36.7 ms for a full 320x240 push) even starts.
UNVERIFIED precise cycle count (that depends on `LV_DRAW_COMPLEX` code paths not traced line-by-line
in this pass), but the chunking mechanism and the 12,288-px chunk size are directly read from this
repo's own config plus LVGL source, not estimated.

**Symptom if a full-screen translucent overlay is added carelessly:** a visible extra stall opening
any dialog (dozens of ms beyond the already-expensive full-frame flush), worse if it coincides with
an SD-card map-tile load contending for the shared TFT/SD SPI bus.

**Fix:** for any overlay/backdrop on this hardware, prefer `lv_obj_set_style_bg_opa(backdrop,
LV_OPA_COVER, LV_PART_MAIN)` (fully opaque, e.g. solid dark gray) over a translucent scrim; if a
translucent look is a hard requirement, keep the translucent area as small as possible (e.g. only a
toast's own small bounding box, not the whole screen) so it fits inside one 12,288-px layer chunk.

**Source:** `lib/lvgl/src/core/lv_disp.h:174-175,191-208` (top_layer/sys_layer, inline accessors);
`src/t-deck/lv_conf.h:127-139` (comment block explaining "simple layers", buffer size definitions,
read in full); `lib/lvgl/src/extra/widgets/msgbox/lv_msgbox.c:66` (backdrop created on
`lv_layer_top()`, corroborating layer_top's intended use for modals in this exact version).

### F9. Non-blocking toast pattern, and why a `while` + `lv_task_handler()` wait loop is wrong

**Claim:** The correct non-blocking pattern for a self-dismissing toast is a one-shot
`lv_timer_create()` whose callback deletes the toast (via `lv_obj_del_async` if there is any chance
the timer fires concurrently with other LVGL activity on the same object — see F6 — a plain
`lv_obj_del()` is also fine here since the timer callback runs from inside `lv_task_handler()`, the
same call stack as everything else in this single-threaded UI model, so there's no real reentrancy
hazard, but `_async` costs nothing extra and is the more defensive default for timer-driven deletes).

```c
static void toast_dismiss_cb(lv_timer_t *t)
{
    lv_obj_t *toast = (lv_obj_t *)lv_timer_get_user_data(t);
    lv_obj_del(toast);        /* or lv_obj_del_async(toast); see note above */
    /* lv_timer_del is not needed: a ready one-shot timer is auto-deleted by LVGL
       when its repeat count reaches 0, IF lv_timer_set_repeat_count(t, 1) was used. */
}

lv_obj_t *toast = /* ... create it ... */;
lv_timer_t *t = lv_timer_create(toast_dismiss_cb, 2000, toast);
lv_timer_set_repeat_count(t, 1);
```

**Why a `while(...) { lv_task_handler(); delay(5); }` wait-for-dialog-result loop is wrong here
specifically:** the context file already flags that this exact anti-pattern exists in this codebase
(`addMessage()` in `tdeck_main.cpp`, and around `lv_obj_functions.cpp:4307`) for other purposes.
Reusing it to "wait for the user to press OK on a dialog" would: (a) block whatever task called it —
if that's a network/RX-handling task, incoming mesh traffic stalls until the user dismisses the
dialog; (b) re-enter `lv_task_handler()` recursively if the wait loop is itself called from inside an
LVGL event callback (the normal place a "show dialog and wait" call would be made), which is
explicitly unsafe in LVGL — `lv_task_handler()`/`lv_timer_handler()` is documented as non-reentrant;
(c) still burns full flush cycles (F1/F8 costs) on every `delay(5)` iteration while getting nothing
done. The correct shape is always event-driven: register a callback, return immediately, let the
normal `lv_task_handler()` call in the Arduino main loop drive dismissal/continuation.

**Fix:** never call `lv_task_handler()` from inside code that is itself running as a result of
`lv_task_handler()` (i.e. from any LVGL event/timer/animation callback). For "do X after the user
responds", attach the continuation to the dialog's `LV_EVENT_VALUE_CHANGED`/`_CLICKED` callback
instead of blocking for it.

**Source:** repeated in `00-CONTEXT.md` ("busy-wait loops such as `addMessage()`... and around
`lv_obj_functions.cpp:4307`") — cited per instructions, not re-derived; `lv_timer_create`/
`lv_timer_set_repeat_count` behavior (auto-delete on repeat-count exhaustion) is standard LVGL 8.3
timer semantics, consistent with `lib/lvgl/src/misc/lv_timer.h` (function existence spot-checked,
full timer-lifecycle trace not repeated here since Track scope is scroll/popup/image, not timers).

### F10. Dropdown option-list cost: `set_options` copies, `set_options_static` does not

**Claim:** `lv_dropdown_set_options(lv_obj_t *obj, const char *options)` computes `strlen(options)+1`,
frees any previous non-static buffer, then `lv_mem_alloc(len)` + copies the string in (confirmed by
reading the full function body). `lv_dropdown_set_options_static(obj, options)` instead stores the
pointer directly (`dropdown->static_txt = 1` path) and never copies — the caller must guarantee the
string outlives the dropdown (e.g. a `static const char[]` or a string that is never freed/mutated
for the dropdown's lifetime).

**Why it matters here:** this repo calls `lv_dropdown_set_options(dropdown_mapselect,
getMapDropbox().c_str())` and `lv_dropdown_set_options(dropdown_country, getCountryDropbox().c_str())`
(`lv_obj_functions.cpp:1264,1186`, and again in `tdeck_main.cpp:266`) with the result of an
`Arduino String`'s `.c_str()` — `getMapDropbox()`/`getCountryDropbox()` almost certainly return a
temporary `String` by value, so `set_options` is already the correct choice here (the source pointer
is not guaranteed to outlive the call) — do **not** "optimize" these to `_static`, that would be a
dangling-pointer bug (the temporary `String` is destroyed right after `.c_str()` is read, and even if
it weren't, the dropdown would then hold a pointer into memory whose lifetime it does not control).
`_static` is only safe for genuinely static/immutable string literals, such as
`lv_dropdown_set_options(dropdown_aprs, (char*)"Runner\nCar\nCycle\nBike\nWX\nPhone\nBulli\nHouse\nNode")`
at `:816` — that one is a string literal (effectively static storage) and _could_ safely use
`_static` to skip the copy, though the copy cost for a short fixed string like this is negligible
either way, so this is a minor/optional optimization, not a defect.

**Redraw behaviour when the option list is longer than the screen:** the list object created by
`lv_dropdown_list_create()` is itself a scrollable container (an `lv_obj` with the list styled onto
it) sized to fit available vertical space between the dropdown and the screen edge
(`lv_dropdown_open()`, the `list_h`/`LV_VER_RES` clamping logic at `lv_dropdown.c:487-514`) — if
options overflow that height, the list becomes internally scrollable exactly like any other
scrollable `lv_obj` (subject to the same F1/F3 costs), not resized past the screen.

**Fix:** leave the two dynamic-string dropdowns (`dropdown_mapselect`, `dropdown_country`) using
`set_options` as-is; optionally switch the one static-literal dropdown (`dropdown_aprs`) to
`lv_dropdown_set_options_static` if minimizing that one small heap churn is desired — purely
optional, not a bug fix.

**Source:** `lib/lvgl/src/widgets/lv_dropdown.c`, `lv_dropdown_set_options` (full body read),
`lv_dropdown_set_options_static` (signature + start of body read), `lv_dropdown_open` (`:455-530`,
full body read for sizing/clamping logic); repo call sites via
`grep -n "lv_dropdown_set_options" -r src/t-deck`.

### F11. Image color formats, `LV_COLOR_16_SWAP`, and the byte-order trap

**Claim:** `LV_COLOR_16_SWAP` is `0` in this repo's config (`lv_conf.h:30`) — LVGL's internal
`lv_color_t` for `LV_COLOR_DEPTH 16` is therefore stored in native (non-byte-swapped) RGB565, and
`lv_color_make(r,g,b)` (used in `tdeck_sdmap.cpp` to build the decoded tile buffer,
`dst[i] = lv_color_make(rgba32[i*4+0], ...)`) produces values in that same native order. TFT_eSPI's
`pushColors()` (the actual SPI write path in `disp_flush`, per `00-CONTEXT.md`) expects big-endian
RGB565 by default for most ST7789 setups; whether a byte-order mismatch shows up as "wrong colours"
depends on whether `LV_COLOR_16_SWAP`/TFT_eSPI's own swap setting and the panel's actual bit order
are consistent — **this repo already renders correctly today** (no reported "wrong colours" symptom
for the map tiles or icons), so the current `LV_COLOR_16_SWAP=0` + however `pushColors` is invoked
is a matched, working pair; do not change `LV_COLOR_16_SWAP` without also re-verifying the on-panel
color output, since flipping just one side of this pair is exactly what causes the classic
"red/blue channels swapped" bug.

**True-color-with-alpha vs chroma-key cost:** `LV_IMG_CF_TRUE_COLOR_ALPHA` (used for
`mouse_cursor_icon`, `mouse_cursor_icon.c:79`, 14x20 px cursor) stores a per-pixel alpha byte
alongside RGB565 (`LV_IMG_PX_SIZE_ALPHA_BYTE`-sized pixels, effectively 3 bytes/px at 16bpp+8-bit
alpha) and requires per-pixel alpha blending against the destination on every draw — real but small
here (280 px total). `LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED` instead reserves one exact color value as
"transparent" and does a cheaper equality test instead of blending, at the cost of not being able to
use that exact color anywhere else in the image and no partial/anti-aliased transparency (hard edges
only). For the map tiles (`LV_IMG_CF_TRUE_COLOR`, no alpha at all — opaque tile, `tdeck_sdmap.cpp`),
neither cost applies; for any new UI icon needing a transparent background, chroma-key is
meaningfully cheaper than true-color-alpha on this CPU-bound software renderer if the icon has
hard-edged transparency (typical for simple glyph-style icons) — reserve true-color-alpha for icons
that need actual soft/anti-aliased edges.

**Symptom catalogue (see F14) covers the specific failure modes for wrong `cf`/format choices.**

**Fix/rule:** for new opaque icons/images, default to `LV_IMG_CF_TRUE_COLOR`; for icons needing a
transparent background with simple hard edges, use `LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED` with a
reserved magenta-class key color; only reach for `LV_IMG_CF_TRUE_COLOR_ALPHA` when true
anti-aliased/partial transparency is required (as the existing cursor icon does, appropriately, given
it needs to look good over arbitrary backgrounds).

**Source:** `src/t-deck/lv_conf.h:30` (`LV_COLOR_16_SWAP 0`); `src/t-deck/mouse_cursor_icon.c:74-80`
(existing `LV_IMG_CF_TRUE_COLOR_ALPHA` usage, read in full); `tdeck_sdmap.cpp` (`LV_IMG_CF_TRUE_COLOR`
usage for tiles, read in full, F5/point 5 above); `00-CONTEXT.md` for the TFT_eSPI/`pushColors`
flush path. The exact TFT_eSPI-side byte-order convention was **not independently re-verified** in
this pass (would require reading `lib/TFT_eSPI` source, out of this track's scope) — flag as
UNVERIFIED that `LV_COLOR_16_SWAP=0` is provably "correct" rather than merely "currently not visibly
broken"; do not treat "not reported as broken" as proof of correctness if this area is touched.

### F12. The image decoder cache: `LV_IMG_CACHE_DEF_SIZE` and the map-tile path

**Claim:** `LV_IMG_CACHE_DEF_SIZE` is `0` both by this repo's explicit `lv_conf.h:147` setting and by
LVGL 8.3's own internal fallback default when unset (`lv_conf_internal.h:351-355`, `#ifndef
LV_IMG_CACHE_DEF_SIZE ... #define LV_IMG_CACHE_DEF_SIZE 0`) — so this repo did not accidentally
disable a cache that LVGL 8.3 otherwise defaults to enabling; **0 is the vendored default**, this is
a deliberate-or-inherited config, not a regression.

With cache size 0, `_lv_img_cache_open()` (`lv_img_cache.c`) skips the cache-array lookup entirely
(the whole `#if LV_IMG_CACHE_DEF_SIZE` block compiles out) and always performs a fresh
`lv_img_decoder_open()`; after each draw, `draw_cleanup()` in `lv_draw_img.c:367-374` explicitly
closes the decoder immediately (`#if LV_IMG_CACHE_DEF_SIZE == 0 → lv_img_decoder_close(&cache->dec_dsc)`)
instead of leaving it open for reuse. **What "open" actually costs depends entirely on the color
format and source type** (read in full, `lv_img_decoder.c:326-410`):

- `LV_IMG_SRC_VARIABLE` + `LV_IMG_CF_TRUE_COLOR*`: `dsc->img_data = ((lv_img_dsc_t*)dsc->src)->data;`
  — a single pointer assignment, O(1), no allocation, no copy. **This is the map-tile path and the
  cursor-icon path in this repo — cache=0 costs nothing extra for either.**
- `LV_IMG_SRC_VARIABLE` + `LV_IMG_CF_INDEXED_*`: allocates a palette + opacity array
  (`lv_mem_alloc`) and rebuilds it from the source data **on every single open**, i.e. every redraw
  with cache=0 — this is the format that would actually pay a real per-redraw cost if it were used.
- `LV_IMG_SRC_FILE` + `.bin` built-in format: opens a file handle and (for `TRUE_COLOR`) leaves
  line-by-line reads for the draw loop, or (for `ALPHA_8BIT`/`RGB565A8`) reads the whole image into a
  fresh `lv_mem_alloc` buffer on every open — this repo does not use file-sourced LVGL images at all
  (`LV_USE_FS_* ` are all `0`, `lv_conf.h:612-639`; the map path manually reads via `SD.h` + `lodepng`
  instead, bypassing LVGL's file decoder entirely), so this cost path is moot here.

**This directly confirms and is consistent with `docs/tdeck-gui-verdict.md`'s cleared item:**
_"`LV_IMG_CACHE_DEF_SIZE=0` does not cause repeated tile decoding (tiles are pre-decoded to a raw
buffer)"_ — this research independently re-derived the same conclusion from the LVGL 8.3 source, not
merely cited the doc.

**What raising the cache would cost/gain:** raising `LV_IMG_CACHE_DEF_SIZE` to e.g. 2-4 would let
LVGL keep 2-4 `_lv_img_cache_entry_t` decoder-state slots warm (skips the open/close pair on repeat
draws of the _same_ source pointer within a short window) — for the current `TRUE_COLOR`+variable
tile/cursor sources this buys essentially nothing (open/close are already O(1) no-ops for that
format), so raising it would add a small fixed RAM/CPU cost (the cache array itself, plus a linear
scan over `entry_cnt` on every open) for no measurable benefit given the current image formats in
use. **Recommendation: leave `LV_IMG_CACHE_DEF_SIZE` at 0** unless a future change introduces
`LV_IMG_CF_INDEXED_*` images or LVGL file-sourced (`.bin`/decoder-registered) images, in which case
re-evaluate.

**Source:** `src/t-deck/lv_conf.h:142-147` (comment block + setting, read in full);
`lib/lvgl/src/lv_conf_internal.h:351-355` (built-in default confirmation);
`lib/lvgl/src/draw/lv_img_cache.c:1-90` (`_lv_img_cache_open`, `#if LV_IMG_CACHE_DEF_SIZE` guards,
read through the cache-disabled branch); `lib/lvgl/src/draw/lv_draw_img.c:367-374` (`draw_cleanup`,
the `#if LV_IMG_CACHE_DEF_SIZE == 0` immediate-close path); `lib/lvgl/src/draw/lv_img_decoder.c:326-410`
(`lv_img_decoder_built_in_open`, all format branches read in full); `src/t-deck/tdeck_sdmap.cpp`
(actual tile-loading code, read in full — confirms `LV_IMG_SRC_VARIABLE` + `LV_IMG_CF_TRUE_COLOR`
usage); `docs/tdeck-gui-verdict.md` (existing cleared-finding citation, corroborated not just
repeated).

### F13. Map tile drawing pattern: already correct; `lv_canvas` as the documented alternative

**Claim:** The existing `sdmap_load_tile()` pattern (decode PNG once via `lodepng_decode32` on SD
read → build a raw `lv_color_t` buffer in `ps_malloc` PSRAM → `lv_img_cache_invalidate_src(&sdmap_dsc)`
→ free the old buffer → point a static `lv_img_dsc_t` at the new buffer → `lv_img_set_src(img,
&sdmap_dsc)`) is architecturally the right approach: decode once, blit via LVGL's cheap `TRUE_COLOR`+
`VARIABLE` path (F12) on every redraw, and explicitly invalidate the image-cache entry keyed on that
`lv_img_dsc_t` pointer before swapping the backing buffer — **this invalidate call matters even
though `LV_IMG_CACHE_DEF_SIZE=0` makes the runtime cache itself a no-op**, because
`lv_img_cache_invalidate_src()` also has effects beyond the size-0 cache array (it is defensive/
correct regardless of cache size, and future-proofs against ever raising `LV_IMG_CACHE_DEF_SIZE`).
Freeing `sdmap_buf` only _after_ calling invalidate and _before_ reassigning `sdmap_dsc.data` avoids
a window where the `lv_img_dsc_t` points at freed memory.

**`lv_canvas` as the documented general-purpose alternative for anything drawing incrementally (not
this repo's current need, since the tile is redrawn wholesale each time, but relevant if a future
feature needs incremental map annotations — e.g. plotting `map_point`/`map_point_label` overlays
directly into pixel data instead of as separate `lv_obj` markers):**

```c
lv_obj_t *canvas = lv_canvas_create(parent);
static lv_color_t cbuf[LV_CANVAS_BUF_SIZE_TRUE_COLOR(320, 240)];  /* macro expands via
    LV_IMG_BUF_SIZE_TRUE_COLOR(w,h) = w*h*sizeof(lv_color_t) + header,
    i.e. 320*240*2 = 153,600 bytes at 16bpp for a full-screen canvas — put this in PSRAM
    (ps_malloc), not a stack/static internal-RAM buffer, given the internal-heap pressure
    already documented for this board. */
lv_canvas_set_buffer(canvas, cbuf, 320, 240, LV_IMG_CF_TRUE_COLOR);
lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
lv_canvas_copy_buf(canvas, decoded_tile_rgb565, tile_x, tile_y, tile_w, tile_h);
/* then invalidate only the changed sub-rectangle: */
lv_obj_invalidate_area(canvas, &changed_area);
```

`lv_canvas_copy_buf()` blits a raw pixel-format-matching buffer into the canvas at an offset (cheap
memcpy-per-row, no decode); `lv_canvas_fill_bg()` clears/solid-fills; both operate on the canvas's
own already-decoded RAM buffer, so redrawing a canvas costs exactly the LVGL-render + flush cost of
whatever rectangle you invalidate — **the same invalidate-the-changed-area discipline as everything
else in this document** — never re-read the tile from SD inside a draw/redraw path; SD reads belong
only in the "load a new tile" code path (as the current `sdmap_load_tile` already does), not in any
per-frame draw callback, because the TFT and SD share one SPI bus (`00-CONTEXT.md`) and any SD read
during a redraw would serialize behind/in front of TFT pushes on that bus.

**Fix/rule:** if map annotations (points, tracks) are ever moved from separate `lv_obj` markers into
pixel data for performance, use `lv_canvas_draw_*`/`lv_canvas_copy_buf` + `lv_obj_invalidate_area`
on just the changed sub-rectangle, backed by a PSRAM buffer sized via `LV_CANVAS_BUF_SIZE_TRUE_COLOR`
(w,h) — do not reach for a full-screen canvas unless the whole screen actually needs pixel-level
compositing; the current `lv_img`-based tile approach remains the right tool for "one bitmap, blitted
wholesale" and should not be migrated to `lv_canvas` without a concrete reason (e.g. combining tile +
overlay pixels into one draw to reduce object count).

**Source:** `src/t-deck/tdeck_sdmap.cpp:230-296` (full tile-load function, read in full);
`lib/lvgl/src/widgets/lv_canvas.h:48-272` (full public API, `LV_CANVAS_BUF_SIZE_TRUE_COLOR` macro
defined as `LV_IMG_BUF_SIZE_TRUE_COLOR(w,h)`, read in full); `00-CONTEXT.md` for the shared TFT/SD
SPI-bus constraint (cited, not re-derived — this track did not re-verify the semaphore/HAL-lock
mechanics, that is `docs/tdeck-gui-verdict.md`'s territory per the corrections already logged there:
"the SPI bus IS arbitrated by the Arduino HAL's per-bus mutex, not by `xSemaphore`").

### F14. Icons: font glyphs vs `lv_img` C arrays — recommendation for this hardware

**Claim/recommendation:** Two realistic options for adding UI icons (e.g. status glyphs, nav icons)
on this hardware:

1. **Merged icon font (glyphs baked into an LVGL font, drawn via `lv_label`)**: cheapest at runtime
   — icon draw reuses the exact same glyph-rasterization/blitting path as any text label (1-bit or
   antialiased alpha-coverage bitmap per glyph, single-color tinting via the label's text color
   style), and reuses whatever font-rendering code is already linked in (this repo already links
   `LV_FONT_MONTSERRAT_28`, so the font pipeline is not "extra" code). Storage is compact (glyph
   bitmaps only, no per-icon `lv_img_dsc_t`/palette overhead). The existing codebase already uses
   `LV_SYMBOL_TRASH` (`lv_obj_functions.cpp:2951`) — LVGL's own built-in symbol font glyphs — proving
   this pipeline is already in active use for the trash-can delete icon. Downsides: single flat
   color per icon (no multi-color icons without tricks), and adding _custom_ (non-built-in) icon
   glyphs requires font-conversion tooling (e.g. LVGL's online font converter merging a custom icon
   TTF into the Montserrat range) as a one-time asset-pipeline step, not a runtime cost.
2. **`lv_img` C arrays (`lv_img_dsc_t` + `LV_IMG_CF_TRUE_COLOR[_ALPHA]`)**: needed for genuinely
   multi-color or photographic icons (the existing `mouse_cursor_icon.c` is exactly this, for a
   cursor that needs to look like a real pointer graphic, not a glyph). Cost per F11/F12: with
   `LV_IMG_CACHE_DEF_SIZE=0` but `TRUE_COLOR*`+variable-source, draw cost is still O(1) open + a
   per-pixel blit — comparable to a glyph draw in raw cost, but the _storage_ cost is higher (full
   RGB565[+alpha] per pixel vs. 1-8 bits/px for a font glyph's coverage map), which matters more
   given this board's `LV_FONT_MONTSERRAT_28`-only font budget choice (suggesting flash/PSRAM asset
   size is already being watched).

**Recommendation:** default to the built-in symbol font (`LV_SYMBOL_*`) or a merged custom icon font
for any single-color UI icon (status indicators, buttons, nav) — matches the existing
`LV_SYMBOL_TRASH` precedent and is cheaper in both flash/PSRAM and draw cost. Reserve `lv_img` C
arrays for the handful of genuinely multi-color/photographic assets (the cursor is the only current
example; a future app logo or a multi-color status glyph would be the same category).

**Source:** `src/t-deck/lv_obj_functions.cpp:2951` (`LV_SYMBOL_TRASH` usage, confirms symbol-font
pipeline already active); `src/t-deck/mouse_cursor_icon.c` (confirms `lv_img` C-array pipeline
already active, for the one case that needs it); `src/t-deck/lv_conf.h:364-388` (font budget —
only Montserrat 28 compiled in, supporting the "assets are being kept lean" inference). The
per-glyph vs per-pixel storage/cost comparison above is general LVGL 8.3 architecture knowledge
(font glyphs are coverage bitmaps, drawn through `lv_draw_label`/`lv_draw_letter`, not through the
`lv_img` decoder pipeline at all) rather than something independently re-traced through
`lv_draw_label.c` in this pass — mark the exact per-glyph bit-depth claim (1-8 bits/px depending on
`LV_FONT_MONTSERRAT_XX` build option, subpixel vs plain) as **UNVERIFIED in this pass** (not
re-checked against `src/t-deck/lv_conf.h`'s specific font AA/bpp settings for the one enabled size).

### F15. Failure catalogue: images

| Symptom                                                                                        | Likely cause                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  | Where to look                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| ---------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Image not shown at all                                                                         | `lv_img_set_src()` never called, or called with a `NULL`/uninitialized `lv_img_dsc_t.data`; or the `lv_img` object's parent/self has `LV_OBJ_FLAG_HIDDEN`; or size is 0x0 because `header.w`/`header.h` were never set before `lv_img_set_src`                                                                                                                                                                                                                                                                                                                                | Check `dsc->header.w/h` and `dsc->data` are both set **before** `lv_img_set_src`; the built-in decoder's `LV_IMG_SRC_VARIABLE` open path checks `((lv_img_dsc_t*)dsc->src)->data == NULL` and returns `LV_RES_INV` (silently — the image area just stays whatever it was, `lv_img_decoder.c:353-355`)                                                                                                                                                             |
| Wrong colours (channels swapped / tinted)                                                      | `LV_COLOR_16_SWAP` mismatch between LVGL's internal format and how the source bytes were produced (e.g. a PNG/RGB888→RGB565 conversion tool that assumes the opposite byte order), or `lv_color_make()` fed `(b,g,r)` instead of `(r,g,b)`                                                                                                                                                                                                                                                                                                                                    | F11; verify `lv_color_make` argument order matches `(r,g,b)` at every call site producing raw pixel data (`tdeck_sdmap.cpp`'s loop does `lv_color_make(rgba32[i*4+0], rgba32[i*4+1], rgba32[i*4+2])` = R,G,B — correct order)                                                                                                                                                                                                                                     |
| Garbled/scrambled image (diagonal tearing, shifted rows)                                       | `header.w` doesn't match the actual stride of the pixel data (row width mismatch), or `data_size` is smaller than `w*h*bytes_per_px` so the decoder/blitter reads past the buffer into adjacent PSRAM                                                                                                                                                                                                                                                                                                                                                                         | Recompute `data_size` as `pixelCount * sizeof(lv_color_t)` exactly as `tdeck_sdmap.cpp:254` already does — do not hardcode a stride                                                                                                                                                                                                                                                                                                                               |
| Image shown once, then blank after scroll (or after the container it's in is scrolled/resized) | The `lv_img_dsc_t` the object points to was invalidated/freed but `lv_img_set_src` was never called again to re-point it (stale pointer, decoder's `open` for `TRUE_COLOR`+`VARIABLE` just reads `data` — if `data` is now dangling, this is a use-after-free, not a benign blank), or the image object's own area was never re-invalidated after the underlying buffer changed                                                                                                                                                                                               | Always pair "swap the backing buffer" with `lv_img_cache_invalidate_src(&dsc)` **before** freeing the old buffer (as `tdeck_sdmap.cpp:274` already does) and `lv_obj_invalidate(img_obj)` (implicit inside `lv_img_set_src`, but if the buffer contents change **without** calling `lv_img_set_src` again — e.g. mutating `sdmap_buf` in place — you must call `lv_obj_invalidate(img)` yourself, since LVGL has no way to know pixel-in-place mutation happened) |
| `lv_img` with a source that was freed (crash or garbage pixels)                                | The classic case: `free(buf); lv_img_set_src(img, &dsc)` was called (or the reverse order — `lv_img_set_src` first, `free` second, with no `lv_img_cache_invalidate_src` in between and cache size > 0 in some other config) — for `TRUE_COLOR`+`VARIABLE` with cache=0 this repo is not exposed to the _cached, stale-entry_ variant of this bug (F12), but is still exposed to the plain "freed before/without ever un-pointing the `lv_img_dsc_t`" variant if any future code path frees `sdmap_buf` without going through the existing `sdmap_load_tile` replace-sequence | Never call `free()` on a buffer an `lv_img_dsc_t` still points to without first either calling `lv_img_set_src(img, NULL)`-equivalent (point it elsewhere) or ensuring the object is deleted/hidden and will not be redrawn before the free — `tdeck_sdmap.cpp`'s existing order (invalidate → free old → assign new) is the template to copy for any new image-swapping code                                                                                     |

**Source:** synthesized from the source reads above (F11-F13); the "not shown" and "shown once then
blank" rows are direct readings of `lv_img_decoder_built_in_open`'s `LV_IMG_SRC_VARIABLE` NULL-check
and of the cache-invalidate-then-free ordering in `tdeck_sdmap.cpp`, not general LVGL folklore.

## Rules to hand the coding agent

1. Any programmatic scroll that is not itself the intended visible animation (append-to-bottom,
   tab-switch positioning, "jump to message") MUST use `LV_ANIM_OFF`, not `LV_ANIM_ON`. Reserve
   `LV_ANIM_ON` for a user-triggered, deliberately-animated scroll only.
2. When appending to `msg_list` on the fast (already-active-tab) path in `msg_tabs_add_message()`,
   after `msg_list_append_bubble()` you MUST trim the view to `MSG_TAB_MAX_MESSAGES` by deleting
   oldest children (`lv_obj_get_child(msg_list, 0)`) with plain `lv_obj_del()`, mirroring the
   model-side `msg_tabs_trim_history()` cap. This is the concrete fix for the H1 defect.
3. Use `lv_obj_del_async()` only when the delete happens from inside that object's own
   `LV_EVENT_DELETE` handler, or from a context outside `lv_task_handler()`'s call stack (e.g. an
   ISR — though none exist in `src/t-deck/` per the shared context). A plain `lv_obj_del()` from a
   `CLICKED`/`VALUE_CHANGED` handler, a timer callback, or ordinary application code (message
   arrival, tab switch) is safe and preferred — do not add `_async` defensively where it is not
   needed; it adds a one-tick delay for no safety benefit outside the `LV_EVENT_DELETE` case.
4. Do not introduce or resurrect the dead `void lv_msgbox(char*, char*)` declaration in
   `lv_obj_functions.h:19`. Any new dialog/modal must call LVGL's real `lv_msgbox_create()` API
   directly (`LV_USE_MSGBOX` is already `1`).
5. Any modal built with `lv_msgbox_create(NULL, ...)` (or any hand-rolled overlay on
   `lv_layer_top()`) must default to an opaque backdrop (`bg_opa = LV_OPA_COVER`) on this hardware.
   A translucent full-screen backdrop is a measured-mechanism-confirmed cost (F8, ~7 layer-buffer
   chunks for a 320x240 area against the 24 KB `LV_LAYER_SIMPLE_BUF_SIZE`) and should require an
   explicit product decision, not be a default.
6. Any toast/auto-dismissing notification MUST use `lv_timer_create()` (one-shot, `repeat_count=1`)
   - `lv_obj_del()`/`lv_obj_del_async()` in the timer callback. Never write a `while(...) {
lv_task_handler(); delay(N); }` loop to wait for a dialog result or a timeout — this repo already
     has that anti-pattern elsewhere (`00-CONTEXT.md`) and it must not be extended to new UI code.
7. Do not change `LV_IMG_CACHE_DEF_SIZE` away from `0` unless a future change introduces
   `LV_IMG_CF_INDEXED_*` images or LVGL file-sourced images (`LV_USE_FS_*`) — for the current
   `TRUE_COLOR`+`VARIABLE`-source images (map tiles, cursor icon) it has no effect on redraw cost
   (F12), so raising it only adds overhead for no benefit given current asset formats.
8. Any new map-tile/pixel-buffer swap code must follow the existing `tdeck_sdmap.cpp` ordering
   exactly: `lv_img_cache_invalidate_src(&dsc)` → free the old buffer → assign the new buffer →
   `lv_img_set_src()`. Never free a buffer an `lv_img_dsc_t` still references without this sequence
   (F15).
9. Never perform an SD-card read (or any other shared-SPI-bus I/O) from inside a per-frame LVGL
   draw/redraw/invalidate-triggered callback. SD reads belong only in explicit "load a new
   tile/asset" code paths, mirroring `sdmap_load_tile()`.
10. Prefer the built-in LVGL symbol font (`LV_SYMBOL_*`) or a merged custom icon font for
    single-color UI icons; reserve `lv_img` C arrays for genuinely multi-color/photographic assets.
11. Default new scrollable containers to `LV_SCROLLBAR_MODE_AUTO`; do not add
    `LV_SCROLLBAR_MODE_ON` (always-visible) without a specific, deliberate UX reason, given the
    extra per-repaint strip cost it adds.
12. For dropdown option strings sourced from a temporary/non-static string (e.g. an `Arduino
String::c_str()`), always use `lv_dropdown_set_options()` (copies), never
    `_set_options_static()`. Reserve `_static` for genuine C string-literal option lists only.

## Open questions / UNVERIFIED

- The exact TFT_eSPI/`pushColors` byte-order convention on this specific ST7789 wiring was not
  independently re-verified (F11) — `LV_COLOR_16_SWAP=0` appears consistent with current
  not-reported-broken color output, but that is not proof; would need `lib/TFT_eSPI` source review
  (out of this track's scope) if colours are ever touched.
- The precise LVGL 8.3 events-doc statement on delete-from-own-event-handler safety could not be
  fetched verbatim (the docs page returned did not contain that guidance); the "CLICKED is safe,
  DELETE is not" rule (F6) is corroborated by two independent first-party source reads (LVGL's own
  `lv_msgbox`, and this repo's working `bubble_delete_event_cb`) plus the `lv_obj_del_async` header
  doc comment, but not by an explicit prose statement from the LVGL manual itself.
- Exact per-glyph bit-depth/coverage-format for `LV_FONT_MONTSERRAT_28` as compiled in this repo
  (F14) was not re-traced through `lv_draw_label.c`/the font's own `.c` data; the general
  font-vs-image cost comparison is standard LVGL architecture, not project-specific measurement.
- The precise CPU-cycle cost of the ~7-chunk simple-layer compositing for a full-screen translucent
  overlay (F8) was not benchmarked or traced through `LV_DRAW_COMPLEX` draw-context code path by
  path; the chunk _count_ (12,288 px per chunk from `LV_LAYER_SIMPLE_BUF_SIZE`) is source-verified,
  the _relative_ "roughly double the pixel work of an opaque redraw" framing is a reasoned estimate,
  not a measurement.
- Default on/off state of `SCROLL_ELASTIC`/`SCROLL_MOMENTUM`/etc. flags for objects created via
  `lv_obj_create()` in this exact 8.3 vendored build was asserted from general LVGL knowledge and a
  partial WebFetch summary, not from tracing `lv_obj_class_t` default style initialization in
  `lv_obj.c` line by line — treat as very likely correct (long-standing LVGL default) but not
  independently re-derived from this repo's exact source in this pass.

## Sources

- `lib/lvgl/src/core/lv_obj_scroll.c` — scroll implementation; `_lv_obj_scroll_by_raw` invalidate
  call (F1), `lv_obj_scrollbar_invalidate` (F3). Read in full for the relevant functions.
- `lib/lvgl/src/core/lv_obj_scroll.h` — public scroll API and enum definitions (F1-F3). Read in full.
- `lib/lvgl/src/core/lv_obj.h:94-102` — `LV_OBJ_FLAG_SCROLL*` bit definitions (F2). Read directly.
- `lib/lvgl/src/core/lv_obj_tree.h` / `.c` — `lv_obj_del_async` declaration/doc-comment and
  implementation via `lv_async_call` (F6). Read in full.
- `lib/lvgl/src/core/lv_indev.c:74-88` — indev reset-query-after-delete mechanism (F6). Read directly.
- `lib/lvgl/src/extra/widgets/msgbox/lv_msgbox.c` / `.h` — full `lv_msgbox` implementation: modal
  backdrop on `lv_layer_top()`, close/close_async, and the synchronous-delete-from-CLICKED pattern
  (F6, F7). Read in full.
- `lib/lvgl/src/core/lv_disp.h:174-208` — `lv_layer_top`/`lv_layer_sys` inline accessors (F8).
  Read directly.
- `lib/lvgl/src/widgets/lv_dropdown.c` — `lv_dropdown_set_options`/`_static`, `lv_dropdown_open`
  list-reparenting-to-screen and sizing logic (F10). Read in full for relevant functions.
- `lib/lvgl/src/draw/lv_img_cache.c` — `_lv_img_cache_open`, cache-disabled compile path (F12).
  Read in full for relevant sections.
- `lib/lvgl/src/draw/lv_draw_img.c:330-374` — `draw_cleanup`, cache-size-0 immediate-close (F12).
  Read directly.
- `lib/lvgl/src/draw/lv_img_decoder.c:326-460` — `lv_img_decoder_built_in_open`, all format
  branches (F11, F12). Read in full.
- `lib/lvgl/src/widgets/lv_canvas.h` — full canvas public API and `LV_CANVAS_BUF_SIZE_*` macros
  (F13). Read in full.
- `lib/lvgl/src/lv_conf_internal.h:351-355` — built-in `LV_IMG_CACHE_DEF_SIZE` default confirmation
  (F12). Read directly.
- `src/t-deck/lv_conf.h` — this repo's actual LVGL config: cache/layer/color/font/widget-enable
  settings (multiple findings). Read the relevant sections directly via grep + targeted reads.
- `src/t-deck/lv_obj_functions.cpp` — message-list/tab implementation
  (`msg_tabs_add_message`, `msg_list_append_bubble`, `msg_tabs_trim_history`,
  `bubble_delete_event_cb`, `msg_render_active_tab`, scroll/scrollbar call sites, dropdown call
  sites). Read the relevant functions in full.
- `src/t-deck/lv_obj_functions.h:19` — dead `lv_msgbox` declaration (F7, rule 4). Read directly.
- `src/t-deck/tdeck_sdmap.cpp` — map-tile decode/load/assign pipeline (F5/point5, F13, F15).
  Read in full.
- `src/t-deck/mouse_cursor_icon.c` — existing `lv_img` C-array icon example (F11, F14). Read in full.
- `docs/tdeck-findings-20260828.md` — measured flush/refresh timings used for F1/F8 quantification
  (avg full flush 36.7 ms, mean full refresh 56.9 ms, partial refresh mean 7.7 ms). Cited per
  instructions, not repeated wholesale.
- `docs/tdeck-gui-verdict.md` — finding H1 (unbounded message-list view growth) and its later
  hardware-measured correction (PSRAM not internal heap, ~2.8 KB/message); cleared finding on
  `LV_IMG_CACHE_DEF_SIZE=0` and pre-decoded tiles (independently re-derived from source in F12, not
  just cited). Cited per instructions, not repeated wholesale.
- `https://lvgl.io/docs/open/8.3/overview/scroll` (redirect target of
  `https://docs.lvgl.io/8.3/overview/scroll.html`) — WebFetch summary corroborating scroll-flag
  semantics (F2). Used as corroboration alongside source reads, not as the sole source.
- `https://lvgl.io/docs/open/8.3/overview/event` (redirect target of
  `https://docs.lvgl.io/8.3/overview/event.html`) — WebFetch attempted; did not contain explicit
  delete-from-event-handler guidance (see Open questions). Fetched but not usable as a citation for
  F6's specific claim.
