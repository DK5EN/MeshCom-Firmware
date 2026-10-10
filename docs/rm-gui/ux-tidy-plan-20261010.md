# Remote page: UI tidy-up verdict and implementation plan (2026-10-10)

Scope: the Remote page (`GET /?page=remote`, `src/web_functions/web_rm_page.cpp`) and the TX power
field of the local Settings page. Reference look: the Settings page (`web_functions.cpp`, "Common
Settings" card) and the LoRa Queue card (`web_functions.cpp:1720`). This document amends
`docs/rm-gui/verdict-ux.md`; where the two disagree, this one wins.

Hand this to the implementing agent as is. Section 3 is the work list in execution order.

## Status (resume point)

| Step | State                                                                                                                                                                                                            |
| ---- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 3.1  | done 27248f12, ff7adee1, fd899435. Deviations: stepper floor is max(pmin, 0) (managing allowlist is 0..max); status reply now 63-70 chars (> legacy 63, accepted); "smaller of both maxima" note not implemented |
| 3.2  | done fd899435; Position "Source" line is still a body div, moves with 3.4                                                                                                                                        |
| 3.3  | done (wave 2), RAK +136 B                                                                                                                                                                                        |
| 3.4  | done (waves 3 and 4); RAK 665084 B after wave 4                                                                                                                                                                  |
| 3.5  | pending (wave 4)                                                                                                                                                                                                 |
| 3.6  | pending (wave 5)                                                                                                                                                                                                 |
| 3.7  | pending; RAK flash baseline is cfadb4cf = 662060 B (79d32c70 = 661124 B); after wave 1: 662756 B                                                                                                                 |

RAK flash limit raised by the operator to +4 kB vs cfadb4cf (662060 B), i.e. 666156 B max; after wave 3: 664660 B.

Env name in this repo is `heltec_wifi_lora_32_V3` (not `heltec_v3`).

## 1. Verdict

The Settings page reads as one system: every row is `label | control | action`, the controls sit in
one column, the action buttons are right-aligned in a fixed column, groups are collapsible cards.
The Remote page breaks every one of those rules inside "Node settings" and half of them in
"Actions". The result is clutter, not a lack of features.

Concrete defects, with cause:

| #   | Symptom (screenshot)                                                                     | Cause in the code                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| --- | ---------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| D1  | Action buttons wrap 4 + 1, "Restart" orphaned on its own row                             | `.rmtiles` is `auto-fill, minmax(8.5em, 1fr)`: the column count depends on card width, five tiles never align.                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| D2  | Switches (GPS, Track, Display, ...) sit at different x positions                         | `rmSwRow` uses `.rmrow` (flex, content-sized label), so each switch starts after its own label. The Settings page puts the switch in a grid column.                                                                                                                                                                                                                                                                                                                                                                                                     |
| D3  | "Node settings" is a wall: bold word glued to a Read button, then free-flowing text      | `rmRenderCards` appends `<strong>` + `<button>` with no layout wrapper, then one `<div>` per value line, then inputs and a Set button, all as flow content.                                                                                                                                                                                                                                                                                                                                                                                             |
| D4  | Name / APRS text: counter, "Stored exactly as typed." and Set stacked under the input    | `rmWBuild` appends cnt / hint / note divs and the Set button as siblings after the input, no row.                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| D5  | Position: after Read, the inputs are filled correctly but a red "Latitude must be ..."   | **Duplicate ids.** The read-only line is created with `x.id='rm_f_'+d.c+'_'+d.f[j][0]` (`rm_f_pos_lat`) and the input in `rmWBuild` gets the same id (`rm_f_pos_lat`). `rmWUpd` calls `rmEl('rm_f_pos_lat').value`, gets the `<div>` (first match), `.value` is `undefined`, the regex fails. See 3.2.                                                                                                                                                                                                                                                  |
| D6  | TX queue is six text lines, the LoRa Queue card on the same node shows bars and a status | `rmDefs.txq` renders through the generic key/value line renderer; the global `.mcq-*` CSS and the counter grid `.mbx-counters` already exist and are not used.                                                                                                                                                                                                                                                                                                                                                                                          |
| D7  | Mailbox is ten text lines                                                                | Same as D6.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| D8  | Heard list: unstyled rows, button not aligned, input + "Look up" glued to the list       | `rmMhCard` builds a `<table>` without the `.rmtab` class and appends the lookup input and the detail block as flow siblings.                                                                                                                                                                                                                                                                                                                                                                                                                            |
| D9  | TX power stepper stops at 15 dBm and goes down to 0 dBm regardless of the target board   | `rmCap()` falls back to 15 when the node has not reported `p=cur/max`; the floor is hard-coded 0 (`rmTx.val<=0`). The status and radio replies carry `p=cur/max` only (`rmFormatStatus`, `rmFmtRadio`); `TX_POWER_MIN` is never sent. Boards differ: RAK4631 / Wireless Paper / E213 min 2, T-Beam -4..20, Heltec V2 -4..15, default -9..22 (`variants/*/configuration.h`, `src/configuration_default.h:109`). The managing node's allowlist is `0..maxTxPower` (`src/remote_cmd.cpp:177`), so a RAK target silently gets `--txpower 0` and rejects it. |
| D10 | Local Settings page: "TX Power" is a free text field, placeholder "15", no range         | `web_functions.cpp:3119` uses `_create_setup_textinput_element(..., "15", "txpower", 2, ...)`; the board range is known at compile time.                                                                                                                                                                                                                                                                                                                                                                                                                |

D5 is a real bug with a wrong user-facing message, fix it first. The user's alternative (truncate the
values the node sends) is not needed: the node already sends 5 decimals (`rm_format.h:39`), the
regex allows 6, and the typed value "48.40760" passes `rmChk` when the right element is read.

## 2. Design: one layout idiom for the whole page

Adopt the Settings-page idiom everywhere on the Remote page. One rule set, no per-card exceptions:

1. **Row grid.** Every setting is one grid row `label | control | action`. Reuse the global `.grid`
   and define one page-local column set, `.rmg{grid-template-columns:minmax(7em,max-content) 1fr max-content;}`.
   Labels in column 1 (plain text, not bold; bold is for card titles only). The control (value text,
   input, switch, stepper) in column 2. The action button (Read, Set, Apply, Details) in column 3,
   right-aligned. Switches sit in column 2 with `justify-self:start`, so all switches share one x.
2. **Cards, not headings.** Each group of the former "Node settings" becomes its own
   `.cardlayout.collapsablecard` with the shared `.cardlabel` / `.cardtoggle` markup, exactly like
   the Settings page: Radio, Identity (Name, APRS text), Position, Queues (TX queue, Mailbox, Max
   hop), Heard list. Advanced stays. The outer "Node settings" wrapper card is removed.
3. **Read button in the card header row.** Each card gets one `.rmrow.rmsplit` header line:
   `<span class="font-small">last read 2 min ago</span> ... [Read]`. Not a bold word glued to a
   button. The "cap<2" note (old firmware) stays as the first line of the first card only.
4. **Read-only values are a key/value grid**, not one `<div>` per line: `.rmkv{display:grid;grid-template-columns:max-content 1fr;gap:2px 10px;font-variant-numeric:tabular-nums;}`.
   Keys in `font-small`, values normal.
5. **Counters are tiles.** Reuse the global `.mbx-counters` grid (already in the shared CSS,
   `web_functions.cpp:1500`) for TX queue and Mailbox counters: one tile per counter, number on top,
   `font-small` caption below. Same rendering as the Mailbox page.
6. **Bars where the LoRa Queue card has bars.** TX queue: queued/capacity as a segment bar using
   the global `.mcq-bar` / `.mcq-cell` / `.mcq-cell-empty` classes (reuse, do not copy); channel use as
   a single fill bar; state as a coloured word (`rmok` for quiet, an orange class for "slow down",
   `rmbad` for "hold"), same wording and colours as the LoRa Queue card ("QUIET / QRS / QRT" stays
   the vocabulary of that card; on the Remote page keep the plain words already in `rmDefs` but colour
   them the same way).
7. **Buttons share one width inside a group.** Actions: `grid-template-columns:repeat(3,1fr)` for
   the five tiles, Restart spans into the second row with `grid-column:3` so the layout is
   `[Refresh status][Send position][Send track]` / `[Re-sync counter][ ][Restart]`. Restart keeps
   `.rmwarn`. No `auto-fill`.
8. **Inputs fill their column.** `.rmg input[type=text]{width:100%;box-sizing:border-box;}`.
   Three position inputs share column 2 as an inner 3-column grid (`.rmg3{display:grid;grid-template-columns:1fr 1fr 5em;gap:8px;}`).
9. **Helper text goes where the Settings page puts it:** one `font-small` line under the row
   (counter "6/19", validation hint). "Stored exactly as typed." becomes one line at the top of the
   Identity card, not under every field. The counter sits right-aligned under the input
   (`text-align:right`).
10. **Mobile.** At `max-width:600px` the row grid collapses to `label` on its own line, then
    `control | action`. One media rule, same breakpoint the `.rmtab` rules use.

CSS budget: the page already ships ~1.3 kB of CSS. The new rules replace the old `.rmrow`-centred
ones; net growth must stay under 400 bytes (RAK4631 flash is at 96 %). Reuse the global classes
(`grid`, `mbx-counters`, `mcq-bar`, `mcq-cell`, `mcq-cell-empty`, `rmtab`, `font-small`, `rmok`,
`rmbad`) before adding any.

Everything in the scaffold JS keeps the file's hard rules (header comment of `web_rm_page.cpp`):
one string literal per `print`, at most 512 B, no comments in the JS, `textContent` for server
data, the single `printf` stays the `rmLen` line.

## 3. Work list, in order

Each step ends green on `tools/regression.sh --stage 1,2`. Steps 3.1 and 3.2 are independent of
the layout work and can ship alone.

### 3.1 TX power range from the target board (D9, D10)

Firmware, both reply formats carry the floor:

- `src/rm_sender_policy.h` `rmFormatStatus()`: add a `pmin` parameter and emit `p=%d/%d pmin=%d`
  (a **new token**, not a third `/` field, so older parsers keep matching `p=cur/max` and ignore the
  rest). Call site `src/rm_runtime.cpp:205` passes `(int)TX_POWER_MIN`.
- `src/rm_format.h` `rmFmtRadio()` / `RmRadioIn`: add `pmin`, emit ` pmin=%d` after `p=`. Call
  site `src/rm_exec_read.cpp:68` passes `(int)TX_POWER_MIN`. Update the format table comment
  (`rm_format.h:30 ff`) and `src/web_functions/Web-API_documentation.txt` if it lists the reply.
- Budget check: longest status line and longest radio body must still fit `RM_FMT_BODY_MAX` (105)
  and the status frame limit. `pmin=-20` is 9 characters. Add the worst case to the existing
  length tests in `test/test_remote_cmd` and `test/test_radio_units` (fixtures at
  `test_radio_units.cpp:228`, `test_remote_cmd/test_main.cpp:1569/1580`,
  `test_rm_sender_policy/test_main.cpp:584`).
- Status parser `src/rm_sender_policy.h:539`: parse an optional `pmin=` token into `o.min`
  (default 0 when absent, so old targets behave as today).

Web JS (`web_rm_page.cpp`):

- `rmKnFor`: add `min:null`. `rmParseStatus`: accept `pmin=`. `rmApplyEntry` for `status` and
  `radio`: store `k.min`. The `radio` regex becomes a `pmin=` lookup next to the `p=` one.
- `rmTxBuild`: floor is `k.min` when known, else 0; cap is `k.max` when known. **Drop the 15
  fallback.** When neither is known, render the stepper disabled and the note "Press Refresh status
  to learn the power range of the node." When known, the note reads "Range 2 to 22 dBm on this
  node." Clamp `rmTx.val` into `[min, max]` after every status.
- The managing node's allowlist stays `0..TX_POWER_MAX` of the managing node (`remote_cmd.cpp:177`,
  `rm_runtime.cpp:890`); the target rejects out-of-range values itself. Document that in the
  `rmTxBuild` note only if a target reports a `max` above the managing node's own: then the
  stepper caps at the smaller of the two and the note says so.

Local Settings page (`web_functions.cpp:3119`):

- Replace the free-text field with a number input: `type="number" min=TX_POWER_MIN max=TX_POWER_MAX step=1`,
  placeholder = `TX_OUTPUT_POWER`. `_create_setup_textinput_element` has no number variant; add a
  small `_create_setup_numberinput_element(id, label, value, min, max, param)` next to it rather
  than widening the existing signature. Add a `font-small` hint line "Allowed on this board:
  -9 to 22 dBm" from the macros. Server side (`web_setup.cpp:130`) already routes to `--txpower`,
  which range-checks (`command_functions.cpp:5022`); nothing to change there.

Tests: `tools/webgui_rm_test.js` gets (a) status with `pmin=2 p=2/22` leaves "-" disabled at 2,
(b) status without `pmin` floors at 0, (c) no status at all disables the stepper and shows the
"Refresh status" note, (d) radio reply with `pmin` updates the floor. Existing fixtures
(`STATUS_NEW` at line 322 and the `p=17/22` strings) stay valid.

### 3.2 Position false error (D5)

- Rename every read-only value element to the `rm_v_<card>_<key>` namespace (`rmRenderCards`,
  the `x.id='rm_f_'+d.c+'_'+d.f[j][0]` line). Inputs keep `rm_f_<card>_<key>`, which `rmWUpd`,
  `rmWire`, `rmClrIn` and `rmIn` rely on.
- For Position, drop the four read-only lines entirely: the inputs are prefilled with the same
  values (`rmWBuild` already does that), and only "Source: GPS on, no fix" remains as a `font-small`
  line in the header row.
- Regression test in `tools/webgui_rm_test.js`: page with a verified `pos` read in `sent[]`
  (`ok 48.40760 11.73850 482 nofix`), then `rmWUpd` must leave `rm_f_pos_hint` empty and
  `rm_f_pos_set` enabled; typing `48.4076` keeps it enabled. The existing checks that read
  `tx('rm_f_pos_lat')` (line 1153) move to the new `rm_v_` ids or to the input values.
- Add a jsdom invariant to the harness: after `rmRenderCards()`, no duplicate ids in `#rm_page`
  (one loop over `querySelectorAll('[id]')`). This catches the whole class of bug.

### 3.3 Actions card (D1, D2)

- `.rmtiles` becomes `grid-template-columns:repeat(3,1fr)`; Restart gets `style` via a class
  `.rmtile-end{grid-column:3;}`. Keep the five tile labels.
- `rmSwRow` renders into a `.grid.rmg` wrapper: `<span>label</span><input switch>` with an empty
  third cell, so every switch sits in column 2 at the same x. Keep ids `rm_sw_<n>`, `data-sw`,
  `data-cf` and the "Really? tap again" arming text (the tests key on them).
- The "State from the last answer ..." note stays as the last `font-small` line.

### 3.4 Split "Node settings" into cards (D3, D4)

- `sub_page_remote()`: replace the single "Node settings" card with five `.cardlayout.collapsablecard`
  cards, ids `rm_card_radio`, `rm_card_ident`, `rm_card_pos`, `rm_card_queues`, `rm_card_mh`, each
  with a `<div>` body that the JS fills. Radio, Identity and Position open by default; Queues and
  Heard list closed (teaser text as on the Settings page: "Open this for ...").
- `rmDefs` gets a `card` key per entry so `rmRenderCards` routes each def into its card body;
  `name` and `atxt` both go to `rm_card_ident`; `txq`, `mbox`, `maxhop` go to `rm_card_queues`.
- Header row per def: `.rmrow.rmsplit` with the def label (`<b>`), the "last read N min ago" age
  from `rmLast`, and the Read button. Inside the Queues card there are three such header rows, one
  per def.
- Writable defs (`rmWBuild`): one `.grid.rmg` row `label | input | Set`. Counter and hint go in a
  `font-small` line spanning columns 2..3 (`grid-column:2/4`). "Stored exactly as typed." becomes
  one line under the Identity card title.
- Position: row `Position | [lat][lon][alt] (.rmg3) | Set`, hint line below.
- Radio: read-only values as a `.rmkv` grid; TX power as a row `TX power | [-] 2 dBm [+] | Apply`
  with the range note below (3.1). Ids `rm_rtxdn`, `rm_rtxval`, `rm_rtxup`, `rm_rtxapply`,
  `rm_rtxnote` unchanged.
- Max hop: `.rmkv` with two rows.

### 3.5 TX queue and Mailbox as the LoRa Queue card (D6, D7)

- TX queue: header row; then a segment bar `queued / capacity` built from `q=a/b` using the global
  `.mcq-bar` with `a` filled cells (class `.mcq-cell` plus a page-local fill colour) and `b-a`
  `.mcq-cell-empty`; caption line "2/20 queued, state quiet" with the state word coloured; then a
  channel-use fill bar (`rx+tx` not available here, one bar for `u`); then the three counters
  Sent / Retransmitted / Dropped as `.mbx-counters` tiles.
- Mailbox: header row; "Mode: off" line; "Used 0/50 slots, 0 bytes, actions 0/20 this hour" as
  one `.rmkv`; the six counters Stored / Delivered / Acknowledged / Dropped / Blocked / Notified as
  `.mbx-counters` tiles. The "This node has no mailbox." line stays for `err unsupported`.
- Both renderers are small dedicated functions (`rmTxqView(p,o)`, `rmMboxView(p,o)`) called from
  `rmRenderCards` instead of the generic line loop; the generic loop stays for radio, sens, maxhop.

### 3.6 Heard list (D8)

- Header row: `Heard list | progress text | [Read] [Stop]`.
- The list is a `.rmtab.font-small` table with a `thead` (`node`, `heard`, empty) and a `tbody`;
  age right-aligned (`text-align:right` on column 2), Details button in column 3 `white-space:nowrap`.
  The existing `@media (max-width:600px)` rules for `.rmtab` apply.
- Lookup is one `.grid.rmg` row: `Other node | [input] | Look up`, placed **above** the table so it
  does not float under a list of unknown length.
- Details render as a `.rmkv` grid under a `<b>` call-sign line, in a bordered sub-box
  (`.rmdet{border-top:solid 1px #e0e0e0;margin-top:6px;padding-top:4px;}`).

### 3.7 Verification and docs

- `tools/regression.sh --stage 1,2` green (Unity envs incl. `test_remote_cmd`, `test_radio_units`,
  `test_rm_sender_policy`; `tools/webgui_rm_test.js` in stage 2, line 217 of the runner).
- Build `heltec_v3`, `ttgo_tbeam` and `wiscore_rak4631`; report the flash delta of the RAK build
  against `79d32c70`. Reject the change if the RAK image grows by more than 1 kB.
- Visual check on the bench: DK5EN-92 (T-Beam, `pmin=-4 p=2/20`) managing DK5EN-1 (Heltec V3,
  `pmin=-9 p=2/22`) and DK5EN-90 (RAK, `pmin=2 p=2/22`): the stepper floor must differ per
  target. Screenshots at 1200 px and at 390 px width of Actions, Radio, Position, Queues, Heard
  list. Bench rules apply: `tools/bench/identity_guard.py` first, TX power at most 2 dBm, test
  traffic to `TEST` only.
- Update `docs/rm-gui/verdict-ux.md` (layout rules section) and `docs/rm-gui/impl-plan.md` (C4)
  with a pointer to this file. Add the D5 id-collision rule to the header comment of
  `web_rm_page.cpp`: "read-only values use `rm_v_`, inputs use `rm_f_`; ids are unique per page".

## 4. Out of scope

- The Settings page itself (reference look, not touched except the TX Power field in 3.1).
- Changing the RM wire protocol beyond the additive `pmin=` token.
- The LoRa Queue card and the Mailbox page (their CSS is reused, not edited).
- The German PR text for upstream: write it after the bench check, as a separate step.
