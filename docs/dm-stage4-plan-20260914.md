# DM stage 4 — sender-visible custody notice

Draft for review, 2026-09-14. Not approved, nothing in code. Derived from T9 option B in
`docs/dm-reliability-and-store-node-verdict-20260913.md` (~:266-278), review finding F9, decision
D3, and the stage 3 code in `src/msgstore*.cpp`.

## Status

| Item                 | State                                 |
| -------------------- | ------------------------------------- |
| Plan                 | draft, awaiting operator review       |
| Decisions 1-5 below  | open                                  |
| Recommended ordering | after the stage 0/3 bench and stage 1 |

## 1. What the operator gets

Today (stage 3, D3) a store node that takes a DM into custody tells nobody. The sender's app shows
the message as "heard" at best, the ladder runs to its end, and the sender concludes the DM failed
even though a neighbour of the destination is holding it and will deliver it.

Stage 4 gives the sender a third, honest state: **held by DK5EN-90**. The sender's ladder keeps
running (D3 stays), the destination's real `:ackNNN` still flips the message to delivered, and a
give-up on a held message no longer reports failure.

## 2. Why not a `0x41`

Unchanged from T9: every node that owns the msg_id stops its ring slot on a `0x41` and marks the
message acknowledged; byte 10 is never read (`src/lora_functions.cpp:412`,
`src/ack_functions.h:60`). A `0x41` from a store node converts "stored" into "delivered" on the
whole installed fleet. Option C is therefore out until the fleet has updated, which is not a
horizon we plan against.

## 3. The notice frame (T9 option B)

An ordinary text frame from the store node to the sender:

```
source        DK5EN-90            (the store node)
destination   DK5EN-93            (the DM's original sender)
payload       "DK5EN-93 :sto017 DK5EN-14"
max_hop       max_hop_text -- the notice must travel back to a sender that is normally several hops away
msg_id        millis(), no flash write, never insertOwnTx(), never uploaded
```

- `%-9.9s:sto%03u` mirrors the `:ack` payload layout so the parser is the existing one with a
  different tag. The trailing destination call is the readable part for old firmware (decision 2).
- Verified against the parsers (T9): no `:ack`, no `:rej`, no `{`, so on **old firmware** it falls
  through to plain display as a short DM from the store node. It is never acked (no `{NNN`), never
  stored by another store node (the store hook requires a `{`), never deduped by stage 2.1 (no
  NNN tag), and relay copies are handled by the msg_id ring like any text.
- On **new firmware** the sender parses `:sto`, does not display it, and updates state (section 4).

## 4. Sender side

`own_msg_id[][4]` gains `0x04 = held`:

| From        | Event                          | To    | Phone frame                                                    |
| ----------- | ------------------------------ | ----- | -------------------------------------------------------------- |
| 0x00 / 0x01 | `:stoNNN` from store node      | 0x04  | status `0x04`, attribution = store call                        |
| 0x04        | `:ackNNN` from destination     | 0x02  | status `0x02` (unchanged path)                                 |
| 0x04        | ladder gives up                | 0x04  | nothing (decision 3) — not failed                              |
| 0x02 / 0x03 | `:stoNNN`                      | stays | nothing — final states are never downgraded                    |
| 0x04        | second `:stoNNN`, other holder | 0x04  | status `0x04` again with the new call (at most one per holder) |

- Phone frame: `buildAckPhoneFrame(msg_id, 0x04, store_call)` — the frame already carries a
  length-prefixed callsign (stage 0 relied on the same for `0x03`). `ACK_STATUS_HELD 0x04` in
  `src/ack_attribution.h`.
- msg_id from NNN: the same reconstruction the `:ack` path uses today; once stage 1's outbox
  exists, the outbox is the authority (it maps NNN to the original msg_id and every attempt).
- Web GUI: envelope mark `&#x2709;` with `title="held by DK5EN-90"` next to the existing check,
  box and ballot-X marks.
- Hook: in `OnRxDone()`'s DM-for-me branch, `:sto` is tested right after `:ack`/`:rej` and before
  the `{NNN` and plain-display paths; rate-limited to one state update per (holder, NNN) per hour
  so a replayed notice is not amplified into phone frames.
- Give-up (stage 0.3 path): if the message is `0x04`, skip the `0x03` frame and the failed mark,
  count `dmstat_giveup_held` instead.

## 5. Store node side

- Trigger: `msgstoreStore()` returned a **new** slot (not a refresh, not a replace). Refreshes
  never re-notify.
- Reach: the notice is sent at `max_hop_text`, relayed like any DM back to the sender. A hop-0
  variant was considered and rejected: the sender is normally several hops away, so a notice that
  only neighbours can hear would not reach the one node it exists for.
- Scheduling: the hook only flags `notice_pending` on the entry; `msgstoreLoop()` sends it as the
  next mailbox action, ahead of ladder steps, under the same node caps (30 s gap, 20 per hour, no
  action under QRS/QRT or above 25 % utilisation). A notice is one action; it competes with
  deliveries on purpose so the node ceiling stays the single bound.
- Never for a `msg_server`-flagged frame? No — same correction as stage 3 F2: on RF the flag only
  says a gateway touched the copy. The notice goes out whenever the entry is new.
- Counters: `notified`, `notice_blocked` (cap). Mailbox page: a "notified" tick per entry.
- Setting: `--storenotice on|off`, default `on`, persisted next to the other store settings (own
  key / `/msgstore.cfg` v2 with a version bump that keeps v1 readable).

## 6. Airtime and behaviour on the installed fleet

| Situation                               | Cost                                  | What the sender sees                     |
| --------------------------------------- | ------------------------------------- | ---------------------------------------- |
| Sender is a neighbour of the store node | 1 frame at hop 0, ~0.85 s             | new fw: held mark; old fw: short DM text |
| Sender is far, `direct`                 | nothing                               | nothing (as today)                       |
| Sender is far, `all`                    | 1 text relayed like a DM              | as above                                 |
| Two store nodes hold the same DM        | 2 notices (one each), both under caps | held by A, then held by B                |

The notice is bounded by the stage 3 node ceiling, so the worst case per node stays 20 frames per
hour across deliveries and notices together.

## 7. Wave plan

Three writers, disjoint, one message, same carve-out pattern as stage 3:

| Owner | Files                                                                                                                                                                                                      | Content                                                                                         |
| ----- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| A     | `src/msgstore.cpp`, `msgstore_api.h` (additive), `msgstore_glue.cpp`, `src/lora_functions.cpp` (sender `:sto` hook, give-up gating), `test/test_msgstore`, new `src/sto_notice.h` + `test/test_sto_notice` | notice scheduling and frame, sender state machine, native tests for build/parse and transitions |
| B     | `src/web_functions/*`                                                                                                                                                                                      | held mark on the messages page, notified tick on the mailbox page, setup row                    |
| C     | `src/command_functions.cpp`, `src/msgstore_settings.*`, `docs/commands-store-node.md`                                                                                                                      | `--storenotice`, persistence v2, `--info` line                                                  |

Orchestrator carve-outs before dispatch: `ACK_STATUS_HELD` in `ack_attribution.h`, the
`notice_pending`/`notified` fields and `msgstoreNoticeMode()` declarations in `msgstore_api.h`,
native env entry for `test_sto_notice`.

Outside this repo, follow-ups: mc-chat and MCProxy learn status `0x04` (ours); McApp is not
blocked on, as with `0x03`.

## 8. Gate and bench

Native suite, sequential builds (V3, RAK, T-Deck Plus, T-Beam as the ineligible board; the
sender-side parse and mark compile everywhere, the store-node side only under `ENABLE_MSGSTORE`),
string scan, advisor pass, then:

| ID    | Test                                                 | Expect                                                                          |
| ----- | ---------------------------------------------------- | ------------------------------------------------------------------------------- |
| T-4.1 | Sender next to the store node, destination airgapped | Held mark within the jitter, ladder keeps running, ack later flips to delivered |
| T-4.2 | Same, sender on upstream 4.35t                       | One short text displayed, no ack emitted, no crash                              |
| T-4.3 | Sender re-floods the DM (fresh id) while held        | No second notice; refresh only                                                  |
| T-4.4 | Ladder gives up on a held message                    | No `0x03`, mark stays held; delivery later flips it                             |
| T-4.5 | Two store nodes hold the same DM                     | Two held frames with different callsigns, caps hold                             |
| T-4.6 | `--storenotice all`, sender two hops away            | Notice relayed once, sender marks held; `direct` sends nothing                  |

## 9. Decisions (operator, 2026-09-14)

1. **Reach:** the notice is relayed at the normal text hop count. A hop-0 variant was rejected as
   illogical: the sender is normally several hops away. Setting is `--storenotice on|off`.
2. **Payload:** `DK5EN-93 :sto017 DK5EN-14`, with the readable destination suffix for old firmware.
3. **Give-up on a held message:** suppressed. The message stays "held by DK5EN-90" until the
   destination's ACK flips it to delivered. Accepted limitation: a store node that later drops the
   entry cannot tell the sender; the GUI mark shows the age.
4. **Status `0x04`, attribution = holder.** No objection from the app side; a useful extension of
   the protocol. mc-chat and MCProxy follow.
5. **Ordering:** now, before the bench and stage 1.

## S4-1 implementation notes (2026-09-14)

- Frame from `glueNotify()`: source and path = store node, destination = the DM's sender,
  `max_hop` from `max_hop_text`, msg_id = millis, enqueued READY then DONE like a delivery, never
  `insertOwnTx()`, never uploaded. A pending notice pre-empts the ladder pick for that one tick
  and counts as one mailbox action.
- Sender side compiles on every board (`src/sto_notice.cpp` is platform-neutral): `:sto` is
  parsed between the `:ack`/`:rej` branch and the `{NNN` branch of the DM-for-me path, the frame
  is consumed there (no display, no phone text, no ack), `own_msg_id[][4] = 0x04` only from 0x00,
  0x01 or 0x04, one phone frame per (holder, NNN) per hour, holder table 16 entries for the GUI
  mark, cleared on the destination's ack. Give-up on a held message counts `giveuph=` in the DM
  line instead of sending 0x03.
- Settings: NVS key `store_notice` on ESP32, `/msgstore.cfg` v2 (`MBX2`) on nRF52 with v1 still
  readable. `--storenotice on|off`, default on.
- Tests: `test_sto_notice` 24 cases, `test_msgstore` 51, `test_dm_stats` updated.
