# Disappearing delivery ticks — Fable Verdict and Improvement Plan (2026-10-04)

> **Status 2026-10-04:** Findings 1-3 fixed on `fork-dev` (B1-B4 plus the advisor's late-ACK
> must-fix), bench-verified on DK5EN-92/-14. Fork only. Implementation log:
> [`webgui-ack-ticks-impl-plan.md`](webgui-ack-ticks-impl-plan.md). The MCApp part (section C)
> went to the webapp agent as `~/Desktop/webapp-ack-matcher-issue.md`.

Report: DK1TCP-77 (Pit), 2026-10-04 11:22 — "mit den 'Rückmeldungs-Haken' geht es in der Web-GUI
manchmal noch etwas durcheinander. Bereits quittierte Hak. verschwinden wieder."

Pit uses the **firmware web GUI** (`http://<node>/` → Messages), not MCApp. The first RCA of this
session targeted MCApp and is largely refuted for the visible symptom (section C). The firmware
cause is confirmed (section A).

Method: 7 independent finders (one angle each), 4 adversarial verifiers that executed the real code
(MCProxy storage on a temp SQLite DB, webapp store in a scratch vitest, firmware code paths plus the
DK5EN-98 field capture for rates). Scratch evidence:
`/private/tmp/claude-501/.../scratchpad/ackrca/` (`f1`-`f7` finders, `v1`-`v4` verifiers).

## A. Firmware web GUI — confirmed root cause

### Finding 1: delivery status lives only in a 20-slot ring shared with all own TX

- **Files:** `src/web_functions/web_functions.cpp:3150-3175` (`sub_content_messages`, served by
  `/?getmessages` at `:752-756`), `src/loop_functions.cpp:463,777-821,5160,5243`,
  `src/esp32/udp_frame_esp32.cpp:498`, `src/nrf52/udp_frame_nrf52.cpp:490`,
  `src/configuration_global.h:307-344` (`MAX_RING` 20, 10 on `ENABLE_TBEAM`)
- **Severity:** high (user-visible, every node, worst on gateways)
- **Mechanism:** the tick of an own message is computed at render time from
  `checkOwnTx(msg_id)` → `own_msg_id[i][4]`. No other per-message status exists in the web path.
  `own_msg_id` is a round-robin ring written by every own frame: `sendMessage`, `SendAckMessage` (one
  per DM we acknowledge), `sendPosition`, `sendAPPPosition`, `sendHey`, `sendPing`, `SendPong`,
  `sendTelemetry`, `sendNbrReport`, `sendInjectedPosition`, and on a gateway every new server→LoRa
  frame. When the DM's slot is overwritten, `checkOwnTx` returns -1 and the bubble renders with no
  tick. The message itself stays listed, because `phoneRing` is fed by different traffic (text only
  on a web-only node).
- **Browser side:** the 30 s `autorefresh` (`web_functions.cpp:918`) calls `updateMessages`;
  `mcProcessMessages` (`:1008`) replaces an element already on screen with the new tick-less HTML. A
  message that has already left `phoneRing` keeps its last ticked HTML in `mcHistory` — which is why
  it is only "manchmal".
- **Status lifetime (DK5EN-98 capture):** gateway server→LoRa inserts per active hour: median 11, p90
  21, max 86. Estimated lifetime of a DM status: about 50-85 min on a gateway, about 14 min in a peak
  hour, about 2.5-3.5 h on a non-gateway node. `phoneRing` holds about 2 h (gateway) and 4-6 h (quiet
  node).
- **Failure scenario:** Pit sends a DM, gets ☑ (ACK). Within the next hour his node sends positions,
  HEYs and acks for incoming DMs (or forwards server traffic as a gateway) and overwrites the slot;
  the next 30 s poll replaces the ☑ bubble with a bare one.

### Finding 2: received messages can show ✓/☑ on a gateway

- **File:** `src/web_functions/web_functions.cpp:3233` (received branch prints `ccheck` too),
  `src/esp32/udp_frame_esp32.cpp:498`, `src/lora_functions.cpp:481-497`
- **Severity:** low (misleading, not lossy)
- **Mechanism:** a gateway inserts foreign server-forwarded `*`/group frames into `own_msg_id`
  (documented in `src/ack_attribution.h:83`). A relay echo sets 0x01, a 0x41 ACK sets 0x02. The
  received-message branch prints the same `ccheck`, so a foreign message shows a tick.
- **Fix:** render `ccheck` only in the own-message branch.

### Finding 3: the status setter is scattered over nine sites, and TD-11 misses five

- **Sites:** `lora_functions.cpp:497` (0x41 ACK), `:1501` (heard), `:1689` (ack), `:1733` (held),
  `:3136` (failed); `udp_frame_esp32.cpp:343` (server-path ACK), `:404` (held);
  `udp_frame_nrf52.cpp:306`, `:384`.
- **Severity:** medium (root of the duplication the fix has to touch; also a latent T-Deck gap)
- **Mechanism:** `tdeck_set_msg_status()` (TD-11) is called at four of them (`:1506,1692,1736,3140`).
  The gateway 0x41 ACK and the server-path ACK/held on both platforms do not reach the T-Deck bubble.
- **Fix:** one setter `setOwnMsgStatus(idx, state)` that writes `own_msg_id[idx][4]`, the durable
  status store (Finding 1 fix) and the TD-11 hook, called from all nine sites.

### Refuted on the firmware side (do not re-investigate)

- A later event downgrading 0x02 → 0x01 — refuted: guarded at `lora_functions.cpp:1495`, `:3134`,
  `:1730`.
- An ack arriving under a PN-retry variant id and missing the slot — refuted: `pnOwnTxLookupId`
  folds back to the original id (`pn_retry.h:78-90`); PN retries take no slots.
- nRF52 `memset(own_msg_id)` on a settings reload — refuted: `nrf52_main.cpp:523` runs in
  `nrf52setup()` at boot only.
- "The page polls every 1 s" (first RCA) — wrong: 30 s `setInterval`; `updateMessages` only delays
  its request by 1 s.

## B. Firmware improvement plan

Upstream rule: minimal, targeted, one PR. Recommended: **B1 + B2 + B3 as one PR**, B4 optional.

| Step | Change                                                                                                                                                                                                                                                                                    | Closes                        | Cost                         |
| ---- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------- | ---------------------------- |
| B1   | Durable status table for own **text** messages only: `{uint32 msg_id; uint8 state}` × `MAX_RING`, written only on `sendMessage` (insert) and by the state setter; separate from `own_msg_id`, so positions, HEY, acks and gateway forwards no longer evict it. Web render reads it first. | Finding 1                     | ~100 B RAM, ~40 lines        |
| B2   | `setOwnMsgStatus(idx, state)` replaces the nine direct writes; updates `own_msg_id`, the B1 table (monotone: never lower 0x02/0x03/0x04 to 0x01; same rank rules as today's guards) and calls `tdeck_set_msg_status`.                                                                     | Finding 3, T-Deck gap         | refactor of 9 sites          |
| B3   | Received branch of `sub_content_messages` stops printing `ccheck`.                                                                                                                                                                                                                        | Finding 2                     | 1 line                       |
| B4   | Browser belt: `mcProcessMessages` keeps the existing tick when the new HTML has none (status never legitimately regresses, see refuted list).                                                                                                                                             | residual after a full B1 wrap | JS one-liner, lost on reload |

Rejected: raising `MAX_RING` (RAM on every board, a gateway peak still flushes it); splitting the
echo-suppression role out of `own_msg_id` (correct but touches dedup semantics, not minimal).

B1 sizing: the B1 table must outlive `phoneRing` text entries. Since `phoneRing` holds roughly 20
frames and only own text messages enter B1, `MAX_RING` entries suffice; an own DM still listed will
always still have its status.

### Tests (regression test fails before, passes after)

- **Native Unity (`[env:native*]`, host gate stage 1):** send one own text message, set ACK via the
  setter, then call `insertOwnTx` 25 times (positions/acks); assert the B1 lookup still returns 0x02.
  Before the fix the only lookup is `checkOwnTx`, which returns -1 → fails.
- Monotone rule: ACK then heard → status stays 0x02; held then ACK → 0x02; failed never overwritten
  by heard.
- **jsdom harness** (`tools/webgui_badge_test.js`): feed a ticked bubble, then the same `data-id`
  without a tick; assert the tick stays (B4 only).
- Received-message render: a foreign message whose id is in `own_msg_id` renders without a tick (B3).
- Bench (stage 3, DK5EN-1 or -92 to an own node, group TEST rules): send a DM, wait for ☑, trigger 25
  own frames (`--sendpos` loop), reload the Messages page, check the ☑ is still there.

## C. MCApp (webapp + MCProxy) — first RCA re-verified

Not what Pit uses, but checked because the first RCA claimed it.

- **Confirmed (mechanism):** `proxy:initial` overwrites `msg_ack`/`msg_sent` from DB
  `acked`/`send_success` (`webapp/src/stores/messages.ts:1110-1116`) and re-puts the result to
  IndexedDB.
- **Refuted (as cause of a visible loss):** an ack MCProxy matched is always in the DB before it is
  published (`ingest.py:1310-1339`, `:2019-2023`), and the snapshot ships it. A false ✓✓ from the
  lax client matcher is cleared and immediately restored by the `acks[]` replay inside the same
  synchronous handler, so nothing visibly flickers.
- **Real but narrow:** a true ack that reached the browser only over the mcmap internet WebSocket
  (MCProxy does not ingest it) is erased at the next reconnect. Requires `internetEnabled`, which is
  off by default (`userSettings.ts:111`).
- **Correctness bug (separate from the symptom):** the client matcher `findAckMessage`
  (`webapp/src/services/messageProcessor/ackMatch.ts`) strips SSIDs (`normalizeCallsign`) and never
  checks the ack's addressee. Executed example: an overheard `DK1TCP-12 → DH6MAV :ack201` marks our
  own `→ DK1TCP-77 {201` as delivered; MCProxy (`_inline_ack_original`) rejects it. The replay keeps
  that false ✓✓ alive until the ack drops out of the newest 200.

### MCApp plan (lower priority, separate repos, not a firmware PR)

1. webapp: align `findAckMessage` with `_inline_ack_original` (full SSID, original sender == ack
   addressee); shared `ack_match_vectors.json` replayed in both repos, including wrong-addressee and
   wrong-SSID cases.
2. webapp: only after step 1 — store `ack_origin` (`server` / `local` / `www`) on the row and let the
   snapshot downgrade only rows without a server-confirmed or internet-derived origin. Regression
   test: own DM, live ack, `initial` without `acked` → `msg_ack` stays true.
3. MCProxy: publish the node/gateway `sent` event only on a DB match (`ingest.py:1466-1481`). Hygiene
   only; refuted as user-visible because `msg_echo` keeps the single ✓.

### Refuted on the MCApp side (do not re-investigate)

- Two DB rows per DM (PN retry / BLE+UDP twin) ordering the snapshot so the unacked row wins —
  refuted by execution: ingest dedups direct DMs on `msg_core` (`ingest.py:372`, `:396-403`); the
  firmware never forwards its own retry copies to the client.
- Stale snapshot overtaking a fresh live ack — refuted: `/events` registers the client before the
  snapshot read, queues live events behind it; webapp processes `proxy:initial` synchronously.
- Unmatched node/gateway `sent` clearing the ✓ — refuted: the bubble cannot exist before the row
  (router awaits storage, `main.py:408`), and `msg_echo` keeps the ✓.
- Node clock skew making the client accept an ack the server rejects (M4) — refuted; the asymmetry
  runs the other way (server accepts, client rejects).
- "Server wins" downgrade is dead code since the 14-day hydrate horizon — refuted: it still clears the
  client matcher's false positives.
