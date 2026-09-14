# DM transport campaign — bench session plan (pick up here)

Written 2026-09-14 at the end of the implementation campaign. Everything below is what a later
session needs to run the bench without re-deriving the state. Code state: fork-main `c2db86bb`.

## 1. Where we are

All five stages are in the tree, each advisor-approved, none flashed. Nothing has run on a radio.

| Stage | Content                                                | Commits                      | Verdict                                            |
| ----- | ------------------------------------------------------ | ---------------------------- | -------------------------------------------------- |
| 0     | ACK repair, DM/ring counters, `--airgap`               | 7aeb2ac5, 4a569e6f, fe3f0640 | `docs/review/fable-dm-stage0-verdict-20260913.md`  |
| 2.1   | Destination dedup on source + NNN, three ingress paths | 44506d0b, 0633e542           | `docs/review/fable-dm-stage21-verdict-20260914.md` |
| 3     | Store node, mailbox page, settings                     | 59f21e5b, 96050d72, be141ecc | `docs/review/fable-dm-stage3-verdict-20260914.md`  |
| 4     | Custody notice `:stoNNN`, status 0x04                  | 66dea241, 150b0a4a, 4440acc0 | `docs/review/fable-dm-stage4-verdict-20260914.md`  |
| 1     | Outbox + ladder behind `--dmretry off\|3\|9`           | 731e0ebc, 1595542c, c2db86bb | `docs/review/fable-dm-stage1-verdict-20260914.md`  |
| 2.2   | Bounded ACK repeats                                    | deferred (operator)          | —                                                  |

Plans: `docs/dm-transport-impl-plan-20260913.md` (master table, the authority),
`docs/dm-stage1-plan-20260914.md`, `docs/dm-stage3-wave-plan-20260914.md`,
`docs/dm-stage4-plan-20260914.md`. Client side: `docs/client-integration-store-forward.md`.
Commands: `docs/commands-store-node.md`, `docs/commands-dm-retry.md`, `docs/bench-airgap.md`.

Gate at `c2db86bb`: native 436/436 across 13 envs; Heltec V3, RAK4631, T-Deck Plus, T-Beam
build; T-Beam image carries no mailbox strings.

Defaults on a freshly flashed node: `--dmretry off`, `--store off`, `--storenotice on` (only
matters once `--store` is on). A node on this firmware with everything at default behaves as
before, except: duplicate DMs for it are re-acked, fresh-id copies are folded, a failed DM reports
0x03, and a `:sto` text is consumed instead of displayed.

## 2. The four nodes and how they are flashed

| Node     | Board / env                         | Role in the tests                   | Flash                                                                   | Console                                                             |
| -------- | ----------------------------------- | ----------------------------------- | ----------------------------------------------------------------------- | ------------------------------------------------------------------- |
| DK5EN-93 | Heltec V3, `heltec_wifi_lora_32_V3` | sender                              | USB serial `/dev/cu.usbserial-0001`                                     | net console TCP 2323 (`tools/hmac_connect.py`)                      |
| DK5EN-90 | RAK4631, `wiscore_rak4631`          | store node                          | USB `/dev/cu.usbmodem2101` (UF2, see CLAUDE.md) or DFU                  | serial only, `serial_session.py --dtr auto`; no 2323, web GUI by IP |
| DK5EN-92 | T-Beam v1.2, `ttgo_tbeam`           | relay; ineligible board as receiver | USB serial `/dev/cu.usbserial-573C0005841`                              | net console 2323 (needs `--webserver on`)                           |
| DK5EN-14 | T-Deck Plus, `t_deck_plus`          | destination (the absent one)        | **OTA** via `tools/webflash.py --host dk5en-14.local --env t_deck_plus` | net console 2323                                                    |

Port names move; match by USB serial (`ioreg`) as in `docs/BACKLOG.md` §3.8f. The T-Deck reboots
on every USB port open, which is why it is the OTA node. OTA needs the safeboot partition on the
node (it has it) and a complete upload: the app slot is single, an aborted upload leaves safeboot
in charge until a full upload succeeds (`docs/` safeboot notes, memory `safeboot-single-app-slot`).
Build the safeboot image before the board env when flashing the Heltec over USB.

**Instrument build for every node.** `--airgap` and `--injectraw` exist only with
`PLATFORMIO_BUILD_FLAGS="-D INSTRUMENT_ENABLED=1"`. Build and flash sequentially, never two
`pio run` on the same env at once. After the bench, string-scan a release image for `AIRGAP`
(must be 0) before anything ships.

```
PLATFORMIO_BUILD_FLAGS="-D INSTRUMENT_ENABLED=1" pio run -e heltec_wifi_lora_32_V3 --target upload
PLATFORMIO_BUILD_FLAGS="-D INSTRUMENT_ENABLED=1" pio run -e ttgo_tbeam --target upload
PLATFORMIO_BUILD_FLAGS="-D INSTRUMENT_ENABLED=1" pio run -e wiscore_rak4631      # then UF2 per CLAUDE.md
PLATFORMIO_BUILD_FLAGS="-D INSTRUMENT_ENABLED=1" pio run -e t_deck_plus
python3 tools/webflash.py --host dk5en-14.local --env t_deck_plus
```

**Traffic discipline:** DMs only between the four bench callsigns, never to `*` or a foreign
callsign; `--mesh off` and `--gateway off` on every node unless a test needs them (T-3.5 and
T-4.7 need a gateway). Keep `--setlog on` on all four so the `DM`, `MBOX`, `OUTBOX` lines land in
the console every five minutes; `--loradebug on` on the store node for the `[MC-DBG]` lines.

Net console is single-client: one `hmac_connect.py` per node at a time. Markers to grep for:
`[AIRGAP];on|off`, `[REACK]`, `[DMDUP]`, `[HELD]`, `[STORE];…`, `[MBOX];…`, `[OUTBOX];refuse;full`,
`[MC-DBG] RETRANSMIT_GIVEUP…`, `[DMRETRY];…`.

## 3. Test order

Run in this order; each block proves the next block's prerequisite.

### Block A — stage 0 (sender 93, destination 14, relay 92)

| ID    | How                                                                                  | Expect                                                               |
| ----- | ------------------------------------------------------------------------------------ | -------------------------------------------------------------------- |
| T-0.4 | On 14: `--airgap on`, wait 60 s of traffic, `--airgap off`                           | no TX while on, `--mheard` unchanged, normal after, no reboot        |
| T-0.2 | Send DM 93 -> 14; capture its raw frame; replay on 14 with `--injectraw <hex>` twice | one extra `:ackNNN` (`[REACK]`), replay within 30 s: `[REACK-LIMIT]` |
| T-0.3 | DM 93 -> a silent callsign (e.g. 14 airgapped); wait for give-up                     | exactly one 0x03 on the phone/BLE side; broadcast give-up: none      |
| T-0.1 | 20 DMs 93 -> 14; watch settings writes                                               | zero writes from the ACK path                                        |
| T-0.5 | M0-1: one node on the live net, `--mesh on`, one hour                                | `ring=enq:.. ovw:..` in the DM line; record the numbers in the plan  |

### Block B — stage 2.1 (needs block A)

Replay a captured DM with a changed msg_id and the same `{NNN`: one display, `[DMDUP]`, an ack.
Repeat via the server path on a gateway node if available.

### Block C — stage 1 (sender 93 `--dmretry 9`, destination 14, relay 92)

| ID    | How                                                                                        | Expect                                                                     |
| ----- | ------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------- |
| T-1.7 | `--dmretry off`, send a DM, capture                                                        | byte-identical to before, three same-id retries                            |
| T-1.4 | `--dmretry 9`, once with 92 relaying, once with 92 off                                     | with relay: no same-id retry; without: exactly one at attempt 2            |
| T-1.1 | 14 airgapped **after** first receipt (ack suppressed)                                      | 9 attempts, fresh ids from 2 or 3, each re-acked, one display              |
| T-1.2 | 14 un-airgapped before attempt 2                                                           | every remaining attempt stops                                              |
| T-1.3 | Sender beaconing hard (short `--utcoff`/pos interval) so the first id leaves the own table | ack still stops the ladder (the F1 fix); gateway variant via 92 as gateway |
| T-1.5 | Force QRT on 93 during a ladder                                                            | no attempt while latched                                                   |
| T-1.9 | 6 DMs in flight                                                                            | 6th refused, `OUTBOX FULL NOT SENT` notice, `[OUTBOX];refuse;full`         |
| T-1.8 | Destination on upstream 4.35t, `--dmretry 3`                                               | three copies there (documented), acks matched here                         |

### Block D — stage 3 (store node 90 `--store heard`, needs blocks A and C)

| ID    | How                                                   | Expect                                                                                                                              |
| ----- | ----------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| T-3.1 | 14 airgapped, DM 93 -> 14, wait, `--airgap off` on 14 | one delivery at hop 0 within a beacon interval, ack reaches 93; **on the Heltec as store node too** (proof of the S3 loop-tick fix) |
| T-3.2 | Watch 92 during T-3.1                                 | `hop0` skip logged, never forwarded                                                                                                 |
| T-3.3 | 14 never acks (airgap after hearing)                  | 9 sends, 1 h cooldown, 9 more; drop at `storetime`                                                                                  |
| T-3.4 | Two store nodes (90 and 93 with `--store`)            | one delivery, peer cancel; then mutually airgapped: two, caps hold                                                                  |
| T-3.5 | 90 as gateway, server-injected DM for an unknown call | no phantom neighbour, no stale replay                                                                                               |
| T-3.6 | Reboot 90 with entries                                | mailbox empty, drop logged                                                                                                          |
| T-3.7 | 14 on upstream 4.35t                                  | DM displayed, plain ack matched                                                                                                     |
| T-3.8 | String-scan the release T-Beam image                  | no `MBOX`, no `mailbox`, no `AIRGAP`                                                                                                |
| T-3.9 | 25 DMs to an absent 14                                | caps hold, no QRT, ring flat                                                                                                        |

### Block E — stage 4 (needs block D, `--storenotice on` on 90)

| ID    | How                                          | Expect                                                              |
| ----- | -------------------------------------------- | ------------------------------------------------------------------- |
| T-4.1 | T-3.1 setup, watch 93                        | `[HELD] by DK5EN-90`, ladder keeps running, ack later flips to 0x02 |
| T-4.2 | 93 on upstream 4.35t                         | one short text `DK5EN-93 :sto017 DK5EN-14` displayed, no ack        |
| T-4.3 | 93 re-floods (fresh id) while held           | no second notice                                                    |
| T-4.4 | Ladder on 93 gives up while held             | no 0x03, mark stays held                                            |
| T-4.5 | Two store nodes                              | two held frames, different holders                                  |
| T-4.6 | 93 two hops away                             | notice relayed once; `--storenotice off` sends nothing              |
| T-4.7 | 93 as gateway, notice arrives via the server | held mark, nothing displayed (proof of the S4 server-path fix)      |

## 4. What to record

- Pass/fail per ID into the stage tables of the plans, and the numbers from T-0.5 (M0-1) into
  `docs/dm-transport-impl-plan-20260913.md` under "Open measurement".
- Anything that fails: the console capture (net console output to a file under
  `tools/bench/runs/`), the commit, the node, and a BACKLOG entry.
- After the bench: release framing per `docs/release-notes-full-delta-framing` memory; stages 0
  and 2.1 are upstream-PR sized, 1/3/4 are fork-only until the role is accepted upstream.

## 5. Known pitfalls that cost time before

- `DEBUG_MSG` compiles away in release builds; bench markers are raw `Serial.printf`.
- `serial_session.py --wait-boot` returns before WiFi is up; wait for `[BOOT];ready` plus ~3 s
  before injecting.
- nRF52 `printf` has no `%lld`.
- The RAK is mute on serial without DTR; the T-Deck and Heltec reboot on port open.
- Parallel `pio run` on one env corrupts `.pio/build`.
- The T-Beam has ~6.6 kB RAM headroom and is deliberately not a store node.
