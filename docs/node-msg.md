# Implementation plan BP-11 — the node refuses to re-transmit its own back-pressure wording

**DECIDED 2026-09-04: Option A (strict block). IMPLEMENTED (BP-11), gate green, bench
cases 1-4 passed on DK5EN-98, case 5 deferred. NOT PUBLISHED: operator decision 2026-09-04, no upstream PR, no push -- the change
stays local on `fork-main` until that is lifted.** Evidence and root
cause: §1. The decision and its reasoning: §2. Code: `src/backpressure.h`
(`bpIsOwnWording()`), `src/loop_functions.cpp` (`sendMessage()`), `src/bp_notice_frame.h`
(corrected comment), new `test/test_bp_echo_guard/`. Protocol reference:
[`backpressure-protocol.md`](backpressure-protocol.md).

---

## 1. Why

A client attached to the node (phone app, web GUI or an EXTUDP program) feeds a
back-pressure notice back into `sendMessage()` as if it were a typed message. The node is
still in the QRT band, so `bpEmitNack()` prepends `QRT NOT SENT - ` and hands the result
back to the same client, which feeds it in again. Every round adds one prefix. When the
ring finally drains, the accumulated text is accepted and goes **on the air**.

Field evidence (mcmap `messages_query`, 24 h window ending 2026-09-04 07:00 CEST):

| Fact                          | Value                                                                                                                         |
| ----------------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| Senders affected              | `IZ5CND-1`, `IZ5CND-10` — same operator, FW 4.35s                                                                             |
| Frames on the air             | 46 matching `TX buffer`, 45 matching `NOT SENT`                                                                               |
| msg-ids                       | `462F82A6…AE` consecutive — the `(_GW_ID<<10)\|node_msgid` scheme of `sendMessage()`                                          |
| First frames of a cycle       | unprefixed notices (`QRS - slow down…`, `QRT - stopping…`) at 1788472821/823/835                                              |
| Proof the client auto-retries | `QRT NOT SENT - QRT NOT SENT - {CET}2026-09-04 04:31:29` — a periodic timestamp message re-sent with the nack banner glued on |

Two consequences the guard has to address separately:

1. **Pure echo.** The text is nothing but our own wording. It carries no operator content
   and must never be transmitted.
2. **Amplification.** The text is real operator content wearing one or more nack banners.
   Blocking it outright would permanently silence a message the operator does want to send
   (the `{CET}` timestamp above), because the client will keep retrying the mangled form.

`aprsmsg.msg_app_offline = true` in `bp_notice_frame.h:43` does not prevent any of this:
bit `0x20` means "the app was offline, this is catch-up traffic" (`lora_functions.cpp:1141`),
a marker for the app, not a send inhibit. The comment there
(`Rückmeldungen niemals announcen`) overstates what the flag does and is corrected as part
of this change.

The real root cause sits in the client, not in the firmware. This plan is a
**node-side containment**: whatever a client does, the node stops putting its own
back-pressure wording on the air. The client bug is reported separately (§8).

---

## 2. Recorded decision (2026-09-04)

Two options were on the table for a message whose text carries our wording but also real
operator content — `QRT NOT SENT - Hallo Welt`:

- **Option A — strict block.** Anything starting with a nack prefix, or matching one of the
  four notice texts exactly, is refused outright. Simplest rule, smallest code, no text is
  ever rewritten.
- **Option B — strip, then block only the pure echo.** Leading nack prefixes are stripped;
  if what remains is exactly one of the four notice texts (or empty), the message is
  blocked, otherwise the stripped remainder is sent.

**The operator decided Option A.** Reasoning:

Field timing is what settled it. `mcmap messages_query` shows a machine client, not a
phone app, answering every nack within milliseconds: `IZ5CND-10` produced msg-ids
`462F82A6`…`AE` — nine frames inside 3 seconds, 1788473096–099 UTC — with the stacked
prefix depth climbing from 2 to 5 frame over frame; `IZ5CND-1` shows the same pattern in
the same minute. The firmware itself supplies a client capable of that speed:
`sendExternNotice()` emits the nack as `{"type":"msg","dst":"*","msg":"QRT NOT SENT - ..."}`
over EXTUDP, and `getExtern()` accepts exactly that shape as a send command — it checks
only `type`/`dst`/`msg`, nothing else. Two nodes whose EXTUDP peer is set to each other
would ping-pong every nack between them with no app involved at all (hypothesis; `IZ5CND`'s
own EXTUDP configuration was not verified, and does not need to be to justify the guard).

Option B does not close that loop, it only slows it down. The stripped remainder is real
operator content, so it is re-sent — and lands right back in the QRT band it was refused
from in the first place, since the ring had not drained in the meantime. That remainder
gets nacked again, re-injected again by the same client, and stripped again: a busy loop of
refuse + nack + re-injected datagram, one round trip per prefix, for the whole length of the
QRT episode. Every one of those retries that happens to still be pending when the ring
finally drains goes out over the air once — Option B still radiates the operator's text, at
an unpredictable point in an episode that can run for minutes. Worse, if a second node
bridges its EXTUDP peer into the first, that second node would end up transmitting content
the first node explicitly refused, under the second node's own callsign. Option A ends the
loop at the very first echo: block, no nack, no notice, silence. The client already
received the one nack for the operator's original text; a client that re-sends the wrong
text back at the node has a bug the node should not try to route around.

One correction folded into the plan at this point: the field frame
`QRT NOT SENT - {CET}2026-09-04 04:31:29` reads as though `{CET}` were a destination, but
it is the payload marker of the periodic time broadcast — `sendMessage()` gives `{CET}`
payloads status `0xFF` and never queues them for retransmission, so `{CET}` cannot itself
be what re-injects the frame. The original message was `{*}{CET}...` — destination `*` —
and the nack body is built from `strMsg` _after_ the `{ZIEL}` parse, so what the client
re-injects has the shape `{*}QRT NOT SENT - ...`. That is why `bpIsOwnWording()` is wired
in after the `{ZIEL}` block: by the time it runs, the `{ZIEL}` prefix is already gone and
only the wording itself is left to classify.

---

## 3. Design

### 3.1 Seam

The predicate goes into `src/backpressure.h`, which is header-only, Arduino-free and
already in the native envs. `loop_functions.cpp` is in **no** native `build_src_filter`
(BACKLOG `CQ-06`), so anything put there is bench-provable only. Keeping all the logic in
the header and only three lines of wiring in `sendMessage()` is what makes this testable
at all.

### 3.2 The literals come from the existing accessors

The guard must never hold a second copy of the wording. It iterates the enums and calls
`bpNackPrefix()` / `bpNoticeText()`, so a future wording change cannot silently unhook it:

- prefixes: `BP_NACK_QRT` → `QRT NOT SENT - `, `BP_NACK_QTA` → `QTA NOT SENT - `
- notice texts: `BP_NOTICE_QRS` → `QRS - slow down, TX buffer is filling`,
  `BP_NOTICE_QRT` → `QRT - stopping to accept new messages, TX buffer full`,
  `BP_NOTICE_QTA` → `QTA - message discarded, TX buffer full`,
  `BP_NOTICE_QRV` → `QRV - ready again, TX buffer clear`

### 3.3 New API in `src/backpressure.h`

```c
/// True if, after leading spaces, `text` either starts with one of the two
/// bpNackPrefix() literals ("QRT NOT SENT - ", "QTA NOT SENT - ") or equals
/// exactly one of the four bpNoticeText() literals. Exact, case-sensitive
/// comparison against the literals returned by the accessors -- no second
/// copy of the wording. No allocation.
inline bool bpIsOwnWording(const char *text);
```

Rule, in order:

1. Skip leading spaces.
2. If what remains starts with `bpNackPrefix(BP_NACK_QRT)` or
   `bpNackPrefix(BP_NACK_QTA)` → true. One prefix is enough; a stacked or amplified echo
   (`QRT NOT SENT - QRT NOT SENT - …`) still starts with the first prefix, so it is caught
   without ever having to walk past it.
3. Else if what remains compares equal to one of the four `bpNoticeText()` literals
   (`BP_NOTICE_QRS`…`BP_NOTICE_QRV`) → true. This is the bare-notice echo, the one with no
   prefix at all (`QRV - ready again, TX buffer clear` fed straight back in).
4. Else → false.

Deliberately exact and case-sensitive: only the literals this firmware itself emits are
caught. A text that merely _contains_ the wording somewhere in the middle passes untouched —
quoting a notice mid-sentence is legitimate operator traffic. Lower-case or tab-prefixed
variants are not caught either, by the same reasoning: they are not byte-identical to
anything the node itself would ever send.

Cost: byte comparisons over at most ~200 bytes, no allocation, no `String` temporaries — it
runs on the nRF52 4 KB loop stack (N-22).

### 3.4 Wiring in `sendMessage()`

Insertion point: `src/loop_functions.cpp`, immediately **after** the `{ZIEL}` block and the
DM-to-own-call check (currently ending at ~`:3893`), **before** the BP-06
`snprintf(bp_origin_dst, …)`, before `bp_state.refusing()` is consulted, and before any
msg-id is minted. Reasons:

- `strMsg` is fully decoded and `{ZIEL}`-stripped there, so the field shape is covered:
  the re-injected frame is `{*}QRT NOT SENT - …` (destination `*`, from the {CET} payload's
  original `{*}{CET}…` — see §2), and by this point in `sendMessage()` the `{ZIEL}` prefix
  is already gone, leaving only the wording for `bpIsOwnWording()` to classify.
- Running before `bp_origin_dst` is latched, before `bp_state.refusing()` and before any
  msg-id is minted means a blocked message leaves **no** trace in the BP state machine —
  the same guarantee BP-07's refuse path gives for a real refusal.

```c
if(bpIsOwnWording(strMsg.c_str()))
{
    Serial.printf("[BP];echo;ms;%lu\n", (unsigned long)millis());
    return BP_SEND_INVALID;
}
```

Properties this keeps:

- **No nack, no notice, no `bp_state` call** on a hit — a receipt for an echo would open
  the next loop right back up.
- **No msg-id consumed**, same reasoning as BP-07's refuse path.
- `BP_SEND_INVALID` as the return, not a new enum value: the semantics ("text this node
  will not send") already match, and all eight call sites plus the web GUI's
  `sendmessage invalid` answer already handle it.
- **No operator text in the marker**, ever — not even under `bLORADEBUG`, and unlike
  `[BP];nack;` there is no `txt;` field at all here. BP-10 H3 exists because a raw text in
  a `[BP]` line can forge a marker for `tools/serial_monitor.py` and `tools/loganalyse.sh`;
  this marker carries only the millisecond timestamp.
- **Unconditional on origin.** All eight `sendMessage()` callers are local text inputs —
  BLE phone, serial `::`, the web GUI, EXTUDP's `getExtern()`, T-Deck and T-Deck Pro. Relay
  and server traffic never passes through `sendMessage()`, so the guard can never block a
  foreign frame; there was no need to gate it by transport.

---

## 4. Files

| File                            | Change                                                           | Owner |
| ------------------------------- | ---------------------------------------------------------------- | ----- |
| `src/backpressure.h`            | `bpIsOwnWording()`, doc comment                                  | wave  |
| `src/loop_functions.cpp`        | the `if` above in `sendMessage()`                                | wave  |
| `src/bp_notice_frame.h`         | correct the `msg_app_offline` comment (it is not a send inhibit) | wave  |
| `test/test_bp_echo_guard/`      | new native suite                                                 | wave  |
| `platformio.ini`                | add `test_bp_echo_guard` to `[env:native]` `test_filter`         | wave  |
| `docs/backpressure-protocol.md` | new subsection: what a client must not do, `[BP];echo;` marker   | wave  |
| `docs/BACKLOG.md`               | BP-11 row under the BP block (BP-10 is the last one used)        | wave  |
| `docs/CHANGELOG-stability.md`   | entry                                                            | wave  |

Publication: none. The original brief (an upstream PR carrying only the three `src/`
files) was withdrawn by the operator the same day; the PR worktree was removed unpushed.
`tools/loganalyse.sh` has no `[BP]` parsing of any kind today, so a `[BP];echo;` section
is out of scope for this item; it stays a plain console marker until a parser is built.

Single wave, single writer — the files are few and the change is one mechanism.

---

## 5. Tests

### 5.1 Native (`test/test_bp_echo_guard`, `[env:native]`)

Fails-before against today's tree (no `bpIsOwnWording()` exists, so the suite is new code
plus the guard; the fails-before proof is the bench case in §5.2 and the `test_backpressure`
integration case below).

1. Each of the four `bpNoticeText()` literals, bare → true (a bare-notice echo).
2. **Drift guard:** loop over `BP_NOTICE_QRS..BP_NOTICE_QRV` and both `BP_NACK_QRT`/
   `BP_NACK_QTA`, assert every `bpNoticeText(n)`/`bpNackPrefix(n)` is caught. A reworded
   notice or prefix can never slip past the guard unnoticed.
3. One to five stacked `QRT NOT SENT - ` prefixes in front of a notice → true (the observed
   shapes, up to the 5-deep frame `462F830C`).
4. Mixed `QRT`/`QTA` prefixes, in any order → true.
5. Prefix with real operator content behind it (`QRT NOT SENT - bench text`) → true —
   Option A blocks on the prefix alone, it does not look at what follows.
6. The field case, `QRT NOT SENT - QRT NOT SENT - 2026-09-04 04:31:29` (`{CET}` already
   consumed by the caller before `bpIsOwnWording()` runs) → true.
7. Ordinary text → false; empty string → false.
8. Text containing the wording mid-sentence (`Ich sah QRV - ready again, TX buffer clear`)
   → false — the prefix/notice match must anchor at the start.
9. Off-by-one variants of a prefix or notice text (one byte added, removed, or changed) →
   false; the comparison is exact, not a fuzzy match.
10. Leading spaces tolerated (still caught); lower-case (`qrt not sent - `) and
    tab-prefixed (`\tQRT NOT SENT - `) variants → false, documented as such — they are not
    byte-identical to anything the node itself emits.
11. `nullptr` / zero-length input → false, no read past the end.

### 5.2 Bench (the only end-to-end proof, CQ-06)

Node on serial (`docs/` runbook conventions; `::text` sends, see the serial-inject notes):

1. `::QRV - ready again, TX buffer clear` → exactly one `[BP];echo;`, **no**
   `[BP];nack;`, no `[BP];notice;`, no TX ring entry, `node_msgid` unchanged.
2. `::QRT NOT SENT - bench 1` → `[BP];echo;`, nothing goes on the air.
3. `::{*}QRT NOT SENT - bench 2` (the field shape, destination `*`) → `[BP];echo;`,
   nothing goes on the air.
4. Regression: an ordinary `::bench 3` is unaffected, no `[BP];echo;` line, message sent.
5. Loop closure: flood the ring into the QRT band, then feed the resulting nack text back
   in over the same transport. Expect exactly one `[BP];echo;` and **no** second
   `[BP];nack;` — the echo is blocked before it can be refused and nacked again.

### 5.3 Gate (per `CLAUDE.md`)

Run 2026-09-04: full native suite, 578 cases across all 12 native envs, all green (240 in
`native` incl. the 10 new cases); sequential `pio run` for `heltec_wifi_lora_32_V3`,
`wiscore_rak4631`, `ttgo_tbeam`, `t_deck`, `t_deck_plus`, `t_deck_pro`, all SUCCESS.
**§5.2 run 2026-09-04 08:26 on `DK5EN-98`** (Heltec V3, build Sep 4 08:14:48, flashed over
WiFi via `tools/webflash.py`, driven over the 2323 console and the web GUI): case 1
`::QRV - ready again, TX buffer clear` -> exactly one `[BP];echo;ms;80968`, no `[BP];nack;`,
no `[BP];notice;`; case 2 `::QRT NOT SENT - bench 1` -> one `[BP];echo;`; case 3
`::{*}QRT NOT SENT - bench 2` (field shape) -> one `[BP];echo;`; web path
`?sendmessage&tocall=*&message=QRT NOT SENT - bench web` -> HTTP answer
`sendmessage invalid` plus one `[BP];echo;`; case 4 `::{TEST}bench 3 082607` -> no
`[BP];echo;`, accepted and mirrored to EXTUDP with msg-id `1AE1E147`. Server side
(`mcmap messages_query`, same window): nothing from `DK5EN-98` carrying the wording.
Case 5 (ring flood into the QRT band, then feed the nack back) is deferred to a USB bench
node -- not run on the live gateway; the block sits before `refusing()`, so cases 1-3
already show that an echo never reaches the refuse/nack path regardless of ring state.

---

## 6. Risks

- **The guard is containment, not the fix.** It stops the node from radiating its own
  wording back at itself; it does nothing about a client that keeps re-injecting other
  traffic, and it does not tell the operator their original message is not coming back —
  the client already had the one nack that told it so.
- **Wording drift.** Covered by test 5.1/2, the drift guard, but only for the literals
  behind `bpNoticeText()`/`bpNackPrefix()`. If a future change introduces a _new_ class of
  node-generated user-visible text, it needs the same treatment; noted in the protocol doc.
- **`sendMessage()` stays untested natively** (CQ-06, backlog). The wiring itself rests on
  the bench cases in §5.2 alone.

---

## 7. Out of scope

- Changing the notice sender back from the node callsign to a pseudo-sender. That was the
  BP-01 operator decision (McApp files non-callsign senders as spam, group 9999) and
  reversing it re-opens the problem it solved.
- A dedicated frame type or status byte for notices so a client cannot mistake them for
  outbox items (BP-01 note L5, still open). That is the structurally correct fix, needs an
  app-side counterpart, and belongs in its own item.
- Server-side filtering. Cosmetic — the HF load stays.

---

## 8. Follow-ups to file with BP-11

- Identify the re-injecting client (EXTUDP peer or app) together with the operator, with
  the `IZ5CND-1` / `IZ5CND-10` evidence from §1 as the starting point — the node-to-node
  EXTUDP bridge hypothesis from §2 first, since the firmware itself supplies the mechanism
  that would produce exactly this timing without any app involved. Whichever client turns
  out to be responsible, a frame recognised by `bpIsOwnWording()` must never be re-sent.
- Interim advice to the operator: stop the client that re-injects, or the node keeps
  refusing every echo silently until the loop is broken at its source.
- L5 (dedicated notice channel) — reference this document from the BACKLOG row.
