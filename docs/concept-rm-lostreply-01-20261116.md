# Concept: RM reply lost on a full TX ring

**A remote-management reply that meets a TX ring full of prio-1 frames is dropped for good, and the sender only learns it as `noanswer` after 171 s. Fix: an RM reply may take the slot of the oldest queued ACK, nothing else changes.**

Recommendation: option D2 below (narrow ACK eviction for own RM replies), one wave on `fork-dev`, host test first, then one bench recheck on the mock setup of 2026-11-15. Backlog item `RM-LOSTREPLY-01`.

Status 2026-11-16: concept, no code changed. Operator decisions open, see the last section.

## Key facts

- **The drop rule:** on a full ring a newcomer only evicts a strictly worse priority, the oldest of the worst class. Same or better priority everywhere means the newcomer is dropped (`src/txring_functions.cpp:1017`, drop branch `:1036-1043`, return `RING_ENQUEUE_FULL` `:1055`).
- **An RM reply is prio 1:** it is an own DM, and an own DM classifies as `MSG_PRIO_CRITICAL`, the same class as every ACK (`src/txring_functions.cpp:132`, `:188`; classes `src/configuration_global.h:488-492`).
- **Text ACKs are prio 1 too:** `SendAckMessage()` builds a TEXT frame `"<CALL 9>:ackNNN"` and enqueues it send-once, stored `DONE` (`src/loop_functions.cpp:6058-6063`, `:6098`). The bench flood was exactly these frames.
- **The reply goes out once:** `sendReply()` hands the reply to `sendMessage()`, which routes RM1 replies to `addTxRingEntryOnce()` (`src/rm_runtime.cpp:182`; `src/loop_functions.cpp:5130`, `:5140`; rule `src/dm_text_escape.h:115`).
- **A ring drop kills the server path too:** on `RING_ENQUEUE_FULL` `sendMessage()` returns `BP_SEND_DROPPED` (-2) without any UDP uplink, by operator decision E4 of 2026-09-01 (`src/loop_functions.cpp:5171-5177`). The caller only prints `[RM];reply;send_failed;-2` (`src/rm_runtime.cpp:184`).
- **Recovery exists, but needs a repeat:** the sender repeats an RM1 command at 0, 42, 84 and 126 s (`src/rm_sender_policy.h:32-37`). The target answers a repeat from its reply cache, at most once per 10 s (`src/rm_runtime.cpp:729`, `:737`).
- **The 171 s verdict:** `noanswer` is the end of the ladder (126 s) plus 45 s allowance (`src/rm_sender_policy.h:38`, `:47`).
- **The repro, 2026-11-15:** ring 20/20 of prio-1 text ACKs under airgap gave `RING_DROP_NEW ... msg_id=EA25A1A3`, `send_failed;-2`, sender `noanswer` after 171 s (`docs/bench-b1-20261115.md:250`).
  - Control with room in the ring: `ok` in 8 to 10 s (`tools/bench/runs/harness/b1m-rm-lostreply-01-20261115.summary`).
- **The repro had no ladder:** under airgap the sender's first copy never aired, so no repeat ran and the cache path got no chance (same summary file, "No RM1RETRY line").
- **Who can fill a ring with prio 1:** user messages are refused at 80 % of `MAX_RING` (16 of 20), but ACKs, relays and beacons are never refused (`src/backpressure.h:30-34`). Only ACKs can push a ring to 20/20 of prio 1.
- **ACKs already count as the cheapest prio-1 frames:** only ACKs get a prio-1 TTL, 90 s (`src/txring_ttl.h:14-24`, `src/configuration_global.h:509`).
- **Field load:** the 93-h soak on the production gateway saw a peak of 5/20 and 0 drops (`docs/campaign-tx-pool.md:9`).

## Root cause

| Layer  | What happens                                                                                                      |
| ------ | ----------------------------------------------------------------------------------------------------------------- |
| Cause  | The eviction rule treats an RM reply like any prio-1 frame. Against a ring of prio-1 ACKs it has no victim.       |
| Cause  | The reply is send-once. After a drop nothing holds or retries it; recovery depends on a later command repeat.     |
| Effect | `RING_DROP_NEW`, `BP_SEND_DROPPED`, no RF frame and no server uplink for the reply.                               |
| Effect | The command did run on the target. The operator sees `noanswer` and cannot tell "not executed" from "reply lost". |
| Effect | A manual resend uses a new counter and runs the command a second time.                                            |

The ring is not wrong in general. The tail-drop rule for equal priority is deliberate and pinned by `test/test_txring_flood/test_txring_flood.cpp:1-21`, because it keeps a DM flood from evicting the node's own parked messages.

The gap is narrower: an RM reply is the answer to an authenticated command that already took effect. It is worth more than a queued ACK, but today the ring cannot tell the two apart.

## Options

| Option                                                                | Effect                                                                          | Cost                                                                                                                                   | Risk                                                                                                                                             | Verdict                                |
| --------------------------------------------------------------------- | ------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------- |
| A. Do nothing, rely on the sender's repeats and the reply cache       | Reply recovered if the ring has room at one of the later copies                 | None                                                                                                                                   | Lost when a prio-1 flood lasts over the 126 s ladder; bench shows a steady flood keeps the ring at 20/20                                         | Not enough                             |
| B1. Reserve one ring slot for RM replies                              | Reply always finds a slot                                                       | Slot is 260 B (`UDP_TX_BUF_SIZE` + 5) plus metadata, or one slot less for everyone; backpressure thresholds shift                      | Touches depth counting, `txq`, the gauges and every caller of `txRingDepth()`                                                                    | Reject: too wide for a rare case       |
| B2. Separate small queue for RM replies                               | Same                                                                            | About 170 B RAM, a second TX source in `doTX()`                                                                                        | Both radio paths, `EXTERNAL_RADIO` ownership and CSMA pick-next logic need a second source                                                       | Reject                                 |
| C. Hold the reply and retry on `send_failed` with backoff             | Reply goes out once a slot frees, until a deadline                              | About 170 B static RAM, a tick in `rmDrain()`; airtime neutral                                                                         | Each failed try raises a QTA episode and nack line unless gated on depth; a steady flood refills each freed slot first, so it can still lose     | Keep as fallback, not now              |
| D1. Give RM replies a new class above ACKs (prio 0)                   | Reply evicts any prio-1 frame                                                   | New class in CSMA tables, stat arrays indexed 1 to 5, `txq` prio counts, web gauge; classification must parse the text                 | Reply could evict own parked DMs waiting for ACK; shortest CSMA wait changes channel fairness                                                    | Reject                                 |
| D. Evict the oldest same-prio frame, for every newcomer               | Any prio-1 newcomer wins against a prio-1 ring                                  | Small code change                                                                                                                      | A remote DM flood makes the node ACK and so evict its own parked DMs; breaks the pinned tail-drop policy                                         | Reject                                 |
| **D2. An own RM reply may evict the oldest queued ACK, nothing else** | Reply always enqueues when an ACK is waiting; lower-prio victims still go first | 0 B RAM, estimated 150 to 250 B flash; airtime: one ACK not sent, its DM sender may repeat once; reply size unchanged                  | Low: only own replies, only to authenticated commands, cached and sync replies limited to one per 10 s; parked DMs and in-flight slots untouched | **Recommended**                        |
| E. Gateway: send the reply to the server only when the ring is full   | Sender reachable via server gets the reply                                      | Small                                                                                                                                  | Breaks operator decision E4 (no backbone for what does not go on RF); helps gateways only                                                        | Operator question, not part of the fix |
| F1. Sender: longer `noanswer` timeout                                 | Verdict comes later, reply still lost                                           | Constant change, but `RM_NOANSWER_MS` is tied by static asserts to the policy window and lockout math (`src/rm_sender_policy.h:47-51`) | Delays the operator's feedback for no gain                                                                                                       | Reject                                 |
| F2. Sender: more repeat copies                                        | More chances for the cache path                                                 | One more command frame on air per copy (up to 99 B with the DM wrapper, `src/rm_runtime.cpp:902`); lockout budget window grows         | Adds airtime to every command to fix a rare target-side drop                                                                                     | Reject                                 |

Why D2 over C: D2 is deterministic. The reply enqueues in the same call, under the same lock, so a steady ACK flood cannot win the race against it. C keeps polling a ring that a flood refills faster than the poll.

Why D2 is safe against abuse: a reply exists only after an authenticated command (`rmCheck()` verdicts `RM_OK`, `RM_REFUSED`, `RM_CACHED`, `RM_SYNC`); every reject is silent (`src/rm_runtime.cpp:764-767`). Without the key nobody can make the node evict an ACK.

## Recommendation

Implement D2. The ring gets a new send-once entry point for own RM replies. When no strictly worse victim exists, that entry point takes the slot of the oldest ACK that is still queued (`DONE`, not in flight).

Leave the sender unchanged. Its ladder and the target's reply cache stay the second line of defence; a cached reply goes through the same entry point.

Do not build C now. After D2, a reply is only lost when 20 prio-1 frames that are not ACKs are queued, and backpressure caps own user traffic at 16. Revisit only if the field shows `[RM];reply;send_failed` again.

## Implementation plan

### Exclusive file set

| File                                           | Change                                                                  |
| ---------------------------------------------- | ----------------------------------------------------------------------- |
| `src/txring_functions.cpp`                     | Core gets the ACK-eviction flag and branch; new wrapper                 |
| `src/loop_functions.h`                         | Declaration of the new wrapper next to `addTxRingEntryOnce()` (line 96) |
| `src/loop_functions.cpp`                       | `sendMessage()` routes own RM1 replies to the new wrapper (line 5140)   |
| `test/test_txring_flood/test_txring_flood.cpp` | New test cases and two frame builders                                   |

No other file. `rm_runtime.cpp`, `dm_text_escape.h`, `pn_retry.h`, CSMA and backpressure stay as they are.

### Step 1: red test first

1. In `src/txring_functions.cpp` add the wrapper as a plain pass-through, so the tests compile and fail on behaviour:
   `int addTxRingEntryRmReply(const uint8_t* frame, uint16_t len, const char* source) { return addTxRingEntryOnce(frame, len, source, 0); }`
2. Declare it in `src/loop_functions.h` below `addTxRingEntryOnce()`, no default arguments.
3. Add the tests below to `test/test_txring_flood/test_txring_flood.cpp` and register them in `main()` after line 440.
4. Run `pio test -e native_aprs -f test_txring_flood` through the scratchpad `piolock.sh` wrapper. Expected: tests 1, 2 and 4 red with `RING_ENQUEUE_FULL` (-2); tests 3, 5 and 6 green.

Frame builders, using the existing `buildFrame()` (`test_txring_flood.cpp:75`):

- `buildTextAckFrame(id)`: TEXT, source `"DK5EN-1"`, destination `"DK5EN-90"`, payload `"DK5EN-90 :ack123"` (call padded to 9 characters, so `txRingFrameIsAck()` matches).
- `buildRmReplyFrame(id)`: TEXT, source `"DK5EN-1"`, destination `"DK5EN-90"`, payload `"RM1 1794773697 ok v=4.40a 0123456789abcdef"`.
- Binary ACK: the existing `buildAckFrame()`.

Fill helper: enqueue `MAX_RING` text ACKs via `addTxRingEntryOnce()` with `mc_test_set_millis()` set so the oldest entry is NOT slot 0 (for example slot `i` at `(MAX_RING - i) * 1000` ms). That proves "oldest", not "first free".

| #   | Test                                       | Setup                                                                                                                      | Asserts                                                                                                                             | Before fix |
| --- | ------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------- | ---------- |
| 1   | `test_rm_reply_evicts_oldest_text_ack`     | Ring full of text ACKs                                                                                                     | Slot returned is the oldest ACK's; `ringPriority` 1; status `DONE`; msg id is the reply's; `stat_drop_count[1]` 1; depth `MAX_RING` | red        |
| 2   | `test_rm_reply_evicts_binary_ack`          | Ring full of binary ACKs (0x41)                                                                                            | Same as 1                                                                                                                           | red        |
| 3   | `test_rm_reply_prefers_lower_prio_victim`  | 19 text ACKs plus one position (prio 4)                                                                                    | Reply takes the position's slot; `stat_drop_count[4]` 1, `stat_drop_count[1]` 0; all 19 ACKs still queued                           | green      |
| 4   | `test_rm_reply_skips_inflight_ack`         | Ring full of text ACKs, all but the newest set to `RING_STATUS_INFLIGHT`                                                   | Reply takes the newest ACK's slot (the only queued one); no in-flight slot changed                                                  | red        |
| 5   | `test_rm_reply_never_evicts_non_ack_prio1` | Ring full of own DMs (`addTxRingEntry(..., RING_STATUS_READY, ...)`, payload with `{001}`), some set to `RING_STATUS_SENT` | `RING_ENQUEUE_FULL`; every slot byte-identical to before                                                                            | green      |
| 6   | `test_ack_and_once_text_keep_tail_drop`    | Ring full of text ACKs                                                                                                     | A new ACK via `addTxRingEntryOnce()` and a `{ping}` DM via `addTxRingEntryOnce()` both get `RING_ENQUEUE_FULL`                      | green      |

Tests 3, 5 and 6 are guards: they pin that the rule stays narrow. Test 5 is the abuse case of option D.

### Step 2: the eviction branch

In `addTxRingEntryCore()` (`src/txring_functions.cpp:842`):

1. Add a last parameter `bool evictAck`. The core is `static` with three callers: `addTxRingEntry()` and `addTxRingEntryOnce()` pass `false`, the new wrapper passes `true`.
2. Add a `static int oldestQueuedAckSlot(void)` helper above the core. It runs under the caller's lock and prints nothing.
   It returns the oldest slot with `ringPriority == MSG_PRIO_CRITICAL`, status `RING_STATUS_DONE` and `txRingFrameIsAck()` true, or -1. Age compare is wrap-safe as in the worst-slot scan.
3. Replace the condition at line 1017 by a victim choice, and keep the existing eviction block unchanged apart from using `victim`:

```cpp
int victim = -1;
if(worst_slot >= 0 && prio < worst_prio)
    victim = worst_slot;                    // existing rule first
else if(evictAck && prio == MSG_PRIO_CRITICAL)
    victim = oldestQueuedAckSlot();         // RM-LOSTREPLY-01
if(victim >= 0) { /* existing droppedOld block, dropPrio = ringPriority[victim] */ }
else            { /* existing droppedNew block */ }
```

4. Replace the pass-through wrapper from step 1 by
   `return addTxRingEntryCore(frame, len, RING_STATUS_READY, RING_STATUS_DONE, source, 0, false, RING_KIND_OTHER, nullptr, nullptr, true);`
   Use whatever constant `addTxRingEntryOnce()` gets as `kind` by default (0 in `src/loop_functions.h:98`).
5. No new log tag. The existing `RING_DROP_PRIO` line then reads `prio=1 ... replaced_by_prio=1`, which only this path can produce.

Rerun the suite: all six tests green, the rest of `native_aprs` unchanged.

### Step 3: route the reply

In `sendMessage()` at `src/loop_functions.cpp:5140`, pick the wrapper:

```cpp
int w;
if(bUseOnce && bDM && dmTextIsRm1Reply(aprsmsg.msg_payload))
    w = addTxRingEntryRmReply(msg_buffer, (uint16_t)aprsmsg.msg_len, "user_msg");
else
    w = bUseOnce ? addTxRingEntryOnce(...) : addTxRingEntry(...);   // unchanged
```

Keep the source label `"user_msg"`: a new label would need a new setlog source code. `bDM` is in scope (`:4902`). A group text starting with `RM1 ` stays on the old path, as `dmTextNoRetransmit()` already does.

### Step 4: gate

- `tools/regression.sh --stage 1,2`, one `pio` process at a time.
- Builds of every board env (REVIEW.md R2). Expected: RAM unchanged, flash plus a few hundred bytes.
- Code review against REVIEW.md R1 to R4 before the commit.

### Bench recheck

Same method as the 2026-11-15 repro, on the local mock server (`docs/bench-b1-20261115.md:250`, summary file above). Bench rules apply: `identity_guard.py` first, `lsof` per port, 2 dBm, `--airgap on` on both nodes, DMs between own nodes only, no broadcast.

1. Flash the fix as INSTRUMENT image to DK5EN-1 (target) and DK5EN-90 (sender), with `MC_AU_DEV_CHANNEL=1`.
2. Fill the ring of DK5EN-1 to 20/20 with prio-1 text ACKs, by injecting DMs to DK5EN-1 through the mock (8 per s), as on 2026-11-15.
3. Send RM `status` from DK5EN-90.
4. Pass at DK5EN-1: `RING_DROP_PRIO ... prio=1 type=3A ... replaced_by_prio=1`, no `[RM];reply;send_failed`, no `[BP];nack;QTA` for the reply.
5. Pass at DK5EN-90: entry `st:"ok"` with `ver` 1 in about 10 s, as in the control run.
6. Control: same command with a free ring, still `ok`, no `RING_DROP_PRIO`.
7. Restore `--airgap off` on both and check `[AIRGAP];off`.

Summary file: `tools/bench/runs/harness/rm-lostreply-01-fix-<date>.summary`.

## Platform impact

| Platform         | Impact                                                                                                                                                                        |
| ---------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ESP32 (all)      | No RAM change. Flash plus about 150 to 250 B (estimate). The core has no lock on ESP32 today; the new branch adds no new shared state.                                        |
| nRF52 (RAK4631)  | Same. The ACK scan runs inside the existing `taskENTER_CRITICAL()` section, only on a full ring and only for RM replies: at most 20 slots, each a short scan to the first `:` |
| `MAX_RING` 10    | The developer T-Beam build (`src/configuration_global.h:361`) behaves the same with half the slots.                                                                           |
| External radio   | `EXT_PENDING` (0x80) and `INFLIGHT` (0x81) are never `DONE`, so the new victim scan cannot touch an owned slot.                                                               |
| LoRa byte budget | Unchanged: no new field, no new frame, reply size as before.                                                                                                                  |

## Not verified / gaps

- **Flash size:** the 150 to 250 B are an estimate, no build was run for this concept.
- **Routing in `sendMessage()`:** `loop_functions.cpp` is not part of any native env, so step 3 is checked by review and the bench recheck only. `dmTextIsRm1Reply()` itself is host-tested (`test/test_dm_text_escape/test_dm_text_escape.cpp:169-171`).
- **Field recovery without the fix:** the repro ran with the sender under airgap, so its repeat ladder never ran. How often the cache path alone recovers a reply in real RF was not measured.
- **The evicted ACK:** the DM sender then sees no ACK. Fork firmware repeats a DM on its 42 s ladder and the target ACKs the copy; other firmware or apps may show the DM as unacknowledged. Not tested.
- **Remaining loss case:** 20 queued prio-1 frames none of which is an ACK. Backpressure should make this unreachable; no test proves it end to end.
- **Concurrency on ESP32:** the core runs without a lock on ESP32. The new branch follows the existing eviction code; no new race was analysed beyond that.

## Operator decisions

1. **ACK for reply:** may an RM reply cost one queued ACK, so a DM sender may repeat once? Recommended: yes.
2. **Hold-and-retry (option C):** build it now as a second safety net, or only if the field shows the drop again? Recommended: only on field evidence.
3. **Server fallback on gateways (option E):** keep decision E4 (no backbone for what does not go on RF) also for RM replies? Recommended: keep E4.
