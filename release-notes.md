> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**The Sunday 27 September 2026 build, dated 28 September: neo plus the neighbour matrix and store-and-forward from `v4.35t.09.27-neo`, plus personal-message (DM) retries in the new XOR format.** It is `v4.35t.09.28-neo` — everything in `v4.35t.09.27-neo` unchanged, with the direct-message retry mechanism replaced end to end.

**How to tell this build apart:** `FLASH_VERSION 20260928` in the boot log (`[INIT]...FLASH layout 20260724 ok, build 20260928`) and in the `DM` setlog line. The version field on the air stays `4.35t` — a fixed five characters. `FLASH_STRUCT_VERSION` stands at `20260724`, unchanged — **your configuration survives this update.** The new retry format needs no settings storage of its own.

## Personal-message (DM) retries: the XOR format

Full design: [`docs/pn-retry-xor-impl-plan.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.28-neo/docs/pn-retry-xor-impl-plan.md), concept paper [`docs/pn-zustellung-dedup.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.28-neo/docs/pn-zustellung-dedup.md). Upstream: [PR #1168](https://github.com/icssw-org/MeshCom-Firmware/pull/1168) (merged into `dev` 2026-09-27).

- **Retry k (1..3) of a DM this node sent carries the original `msg_id` with bits 10–11 XOR k and a recomputed FCS.** The first send is byte-identical to before. Old relays, which dedup on the full 32-bit `msg_id`, forward the retry as a new frame instead of dropping it as a duplicate of the original — the problem this format is built to fix.
- **An echo of the DM no longer ends the retry ladder.** Only the destination's `:ackNNN` does, over LoRa or over the server; a server-side ack now also stops the waiting ring slot. A DM sent on behalf of a KISS client keeps the old behaviour: byte-identical retries, released on the first echo.
- **Repeat copies are recognised by their bit pattern** and are not uploaded to the server, EXTUDP or KISS again, and not stored again by a store node. A DM addressed to this node that arrives again is re-acknowledged but not shown twice.
- **`--dmretry` and the DM outbox are removed.** With the XOR format, the outbox ladder and the ring retry had become nearly identical, so only one path is left: every DM is sent once and retried up to 3 times, 40 s apart. The `--dmretry off|3|9` setting, its web setup select, the `--info` line and the `OUTBOX FULL NOT SENT` refusal are gone (mode 9 was already unusable with three retry-id bit variants). A DM held by a store node still uses the app's "held" status; a DM the ring gives up on shows "failed" unless a store node holds it.
- **A race between an incoming ack and a retry copy still in the send queue is fixed (M1).** Before this fix, an ack that arrived while a retry copy was queued but not yet sent could miss that copy, letting it go out anyway and its echo restart the wait — up to three redundant copies and a spurious "failed" after an ack.

## Compatibility, stated plainly

This retry format now applies to **every** DM, not only ones that previously opted into `--dmretry 3`:

- **Older receivers may show the same DM up to 4 times** (once per retry-id bit variant) — they dedup on the full `msg_id`, which the XOR bits deliberately change.
- **Older store nodes delay delivery by up to roughly 2 minutes** — they treat every retry copy as a new send and reset the hold timer each time.
- **A stored `dm_retry` setting (ESP32 NVS, nRF52 `/dm.cfg`) is read and ignored.** There is no `--dmretry` command left to set it.
- **The `OUTBOX FULL NOT SENT` refusal no longer exists** on this firmware, though its text prefix stays in the echo guard so an older sender's retry format is still recognised.
- **The central MeshCom server must dedup on `msg_id & 0xFFFFF3FF`**, not the full 32-bit id, before this firmware is widely deployed — see [`docs/pn-retry-server.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.28-neo/docs/pn-retry-server.md). Until it does, a region with many nodes on this firmware may see each PN's retries surfaced by the server as separate messages.

## Also in this delta

- **Soak tooling.** `tools/bench/soak_dm.py` sends test DMs between two of our own nodes over their web servers (never `*` or a group); `tools/soakstatus.py` evaluates a capture window for reboots, heap drift, DM counters, retry markers, per-DM ack time and neighbour-matrix consistency. Protocol: [`docs/soak-xor-20260927.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.28-neo/docs/soak-xor-20260927.md).
- **A host-suite gap from the outbox removal is closed.** `native_udp_frame_twin` had lost its fake for the new server-ack ring stop and stopped linking partway through this delta; it now carries that fake and a test for the own-DM server-ack branch.

## Relation to `v4.35t.09.27-neo`

Everything in `v4.35t.09.27-neo` is in this build unchanged: the neighbour matrix (MeshCom 5 topology, stages 1–3), store-and-forward for direct messages (stages 0, 1, 2.1, 3, 4), and everything it in turn carried from `v4.35t.09.26-neo`. Only the DM retry mechanism itself changes, as described above.

## What changes on the air

With default settings, compared with `v4.35t.09.27-neo`:

1. **A DM retry no longer has the same `msg_id` as the original send** — bits 10–11 carry the retry count XORed in, so mesh relays and dedup-by-`msg_id` receivers see it as a new frame.
2. **A DM retry no longer stops on an echo of the DM**, only on the destination's ack.
3. Everything already listed as changing on the air in `v4.35t.09.27-neo` (the `HN` neighbourhood report, symmetric `NCNT`, duplicate-DM re-acknowledgement, `{` in DM text going out as `(`) is unchanged here.

## Supported Hardware

### Verification for this release

- **Host suite:** 45 native environments, 1,384 test cases; `test/golden/selftest.sh` green.
- **Build:** 32 release environments build clean. RAK4631 flash usage 96.1 % (783,580 of 815,104 bytes), down from 96.4 % in the previous release: the outbox removal deletes more code than the retry-id header adds.
- **PN-XOR retry bench, 27 September, DK5EN-1 and DK5EN-98 (both Heltec V3), direct LoRa link:** both nodes have run this exact firmware since 17:25/17:32 (build `ce9bf157`; the two commits since then only add soak tooling and the `FLASH_VERSION` stamp, no source change). A 24 h soak has been running since 17:39; the interim result at ~20 minutes in was PASS on every acceptance criterion (no reboot, heap stable, every test DM acked — one in 4.6 s via the server path, one in 10.0 s via direct LoRa, neither needed a retry, zero neighbour-matrix consistency violations). Both nodes hear each other directly at a strong link, so this soak proves 24 h stability and the ack path, not multi-hop loss recovery. The final 24 h verdict is not in yet.
- **RAK4631 and every other board in the 32 release environments** build from the same source but had no bench time on the XOR retry format this cycle.

### Built and shipped, not on our bench

Every board other than DK5EN-1 and DK5EN-98 above.

## Known gaps, stated plainly

- **The final 24 h soak verdict is not in at the time of writing.** The interim report is PASS; re-run `tools/soakstatus.py` after the window closes for the final BLUF.
- **Multi-hop loss recovery is not bench-proven (`BACKLOG` PN-01).** DK5EN-1 and DK5EN-98 hear each other directly, so the retry ladder has not been exercised against a lost last hop or a marginal link. That needs a node pair with a real multi-hop or fringe-signal path.
- **The central server's masked dedup is not shipped.** Until it is, a network with several nodes on this firmware may show a PN's retries as separate messages on server-fed paths (APRS-IS, mcmap, other gateways' server feeds).
- **Everything `v4.35t.09.27-neo` and `v4.35t.09.26-neo` already listed as unproven carries over unchanged** — see their sections in [`release.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.28-neo/release.md), and [`docs/CHANGELOG-neo.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.28-neo/docs/CHANGELOG-neo.md) for the neo campaign's own gaps.

## Installing

**[Web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/)** — flash over USB from the browser, 30 boards, board detection built in. From this release on, only `v4.35t.09.28-neo` is offered there; older releases are removed from the flasher (their GitHub release objects stay).

Otherwise, pick the asset for your board below. ESP32 boards take the `.bin` over USB or, if the node is already reachable, over WiFi with `tools/webflash.py`. The three nRF52 boards (`wiscore_rak4631`, `heltec_t114`, `t_echo`) ship both a `.uf2` — double-tap reset, copy the file to the volume that appears — and a DFU `.zip` for `adafruit-nrfutil` over serial.

`FLASH_STRUCT_VERSION` is unchanged, so your settings survive.

## Changelogs

- [Personal-message retries — `docs/CHANGELOG-snf.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.28-neo/docs/CHANGELOG-snf.md)
- [Neighbour matrix — `docs/CHANGELOG-meshcom5.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.28-neo/docs/CHANGELOG-meshcom5.md)
- [Store-and-forward — `docs/CHANGELOG-snf.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.28-neo/docs/CHANGELOG-snf.md)
- [neo code-quality campaign — `docs/CHANGELOG-neo.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.28-neo/docs/CHANGELOG-neo.md)

## Upstream

The XOR retry format is [PR #1168](https://github.com/icssw-org/MeshCom-Firmware/pull/1168), merged into upstream `dev` on 2026-09-27. The neighbour matrix and store-and-forward have not been offered upstream yet. Please report bugs that also exist in the official firmware to [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) directly.
