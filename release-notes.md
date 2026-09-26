> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**The Sunday 27 September 2026 build: neo plus two new features, the neighbour matrix and store-and-forward for direct messages.** It is `v4.35t.09.26-neo` — official `4.35t` plus upstream `dev` up to [`6cc8b552`](https://github.com/icssw-org/MeshCom-Firmware/commit/6cc8b552) plus the neo code-quality campaign — with two feature branches on top, given equal weight below:

1. **The neighbour matrix (MeshCom 5 topology, stages 1–3).** One topology store replaces the MHeard table, the path table and the earlier dense matrix. It knows who hears whom, counts neighbours symmetrically, and can take relay decisions per frame (`--nbrrelay`, off by default).
2. **Store-and-forward for direct messages (S&F).** Duplicate DMs are acknowledged instead of shown twice, a failed DM says so, an optional outbox resends a DM until it is acknowledged (`--dmretry`, off by default), and ESP32-S3 and RAK4631 nodes can act as a store node that holds a DM for an absent station and delivers it when that station is heard again (`--store`, off by default).

**This breaks the neo promise on purpose.** neo so far meant "the same frames on the air as official `4.35t`". This build adds new behaviour; what changes on the air with default settings is listed plainly below. If you want the pure neo line, `v4.35t.09.26-neo` stays available as a release and in the web flasher.

**How to tell this build apart:** `FLASH_VERSION 20260927` in the boot log (`[INIT]...FLASH layout 20260724 ok, build 20260927`) and in the `DM` setlog line. The version field on the air stays `4.35t` — it is a fixed five characters. The build date in `--info` is the evening of 26 September, the day the image was built. `FLASH_STRUCT_VERSION` stands at `20260724`, unchanged — **your configuration survives this update.** The new S&F settings live in their own storage (ESP32 NVS keys, nRF52 `/dm.cfg` and `/msgstore.cfg`).

## Upgrade note — only for nodes that ran an earlier neighbour-matrix test build

A node that ran a neighbour-matrix build from before 25 September (only DK5EN test nodes did; no such build was ever released) reads its old NBR bits as KISS bits and comes up with KISS/TCP on and KISS TX on, without auth. Right after flashing such a node send `--kiss tx off`, `--kiss meta off`, `--kiss off`. Every other node is not affected.

## Neighbour matrix (MeshCom 5 topology)

Full list with evidence: [`docs/CHANGELOG-meshcom5.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.27-neo/docs/CHANGELOG-meshcom5.md). Concept paper: `docs/meshcom5-topologie/`.

- **One store instead of three tables.** The MHeard table, the path table and the dense neighbour matrix are replaced by an edge pool with a query layer, direct-neighbour slots and a horizon table for senders beyond two hops. Web MHeard and path pages, `--mheard`, `--path`, `--neighbours`, the phone MH frames and the T-Deck tables all read from it.
- **More capacity, less RAM.** 64 neighbours and 256 edges on classic ESP32, 128 and 512 on ESP32-S3 and nRF52 (was 13 or 21 matrix rows and 10 to 80 MHeard entries). Static RAM is still lower than before on every family.
- **Symmetric neighbour count (NCNT).** A neighbour counts when this node heard it in the last 60 min and it heard this node back within 12 h — or its link is strong enough to assume symmetry (`--nbrsym`, on by default). The value on the air is capped at 99. Older firmware sends the one-sided MHeard count of the last hour, which is usually larger, so a mixed network shows different numbers for the same site.
- **Relay decision per frame** (`--nbrrelay off|count|on`, **off by default**). `count` computes and logs the decision without acting on it; `on` applies it: a queued relay is cancelled or held back when the neighbours that need the frame have demonstrably heard it from someone else already. Coverage uses counted edges with decay, not single hits.
- **HN neighbourhood report** (`--nbrreport off|auto|on`, **auto by default**). A node that is neither mesh relay nor gateway sends a short `HN` frame every 15 min (first one 5 min after boot), `max_hop 0`, never relayed, about 4 s of airtime per hour. It tells its neighbours whom it hears so they can count edges they cannot hear themselves.
- **Server frames: the via is reset to the destination** before the via check, on ESP32 and nRF52 — a via set in another region names nodes nobody here hears.
- **Phone app:** the MH frame (`0x44`) keeps its 13 fields in the old order and appends 7 (`AGE`, `HM`, `ROLE`, `EX`, `NB`, `GW`, `VIA`); the new fields are dropped first when a frame would exceed 244 bytes, so existing apps see no difference. Live MH frames at most once per neighbour and minute.
- **Web, console, display:** `--mheard` and `--path` print one `key=value` line per entry; the web path page is a real table with an age column and now also holds the neighbour rows; MHeard is shown as readable cards; the info page lists every settings switch. T-Deck and T-Deck Pro with persist-to-SD keep the topology across a reboot (`/topo.dat`).
- **Diagnostics:** `--nbrcheck` verifies the internal masks against the edge pool; `--nbrdebug on` logs `[NBR]` lines (format in `docs/nbr-logformat.md`, evaluation with `tools/nbrlog.py`).

## Store-and-forward for direct messages

Full list: [`docs/CHANGELOG-snf.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.27-neo/docs/CHANGELOG-snf.md). Design: `docs/dm-transport-impl-plan-20260913.md`; port record and bench protocol: `docs/snf-port-campaign.md`.

- **Duplicate DMs are acknowledged, not shown twice.** A DM to this node that arrives again with the same sender and `{NNN}` — relayed copy, sender retry, server copy — is acknowledged again (at most once per 30 s) and neither displayed nor forwarded to the phone a second time, on LoRa and on both server paths.
- **A failed DM says so.** When the TX ring gives up on a DM the user sent, the app gets status `0x03` (failed) and the web message page marks it.
- **A `{` inside DM text no longer breaks the acknowledgement.** It is sent as `(`; a leading `{ping}` or `{SET}` tag stays as it is.
- **Message transport protection** (`--dmretry off|3|9`, web setup, **off by default**). A DM is kept in an outbox (five slots on S3 and RAK4631, three on classic ESP32) and resent on a ladder until the destination acknowledges it, at most 3 or 9 sends. A full outbox refuses a new DM with `OUTBOX FULL NOT SENT`. Pings are never retried.
- **Store node** (ESP32-S3 and RAK4631 only, `--store off|own|list|heard`, web setup and a new **Mailbox** page, **off by default**). Keeps DMs for destinations that do not answer and delivers them over one hop when the destination is heard again — directly, and from a frame the destination itself originates. Which destinations qualify: the node's own base call sign (any SSID), a list, or every station heard directly in the last 12 hours (read from the neighbour matrix).
- **Custody notice** (`--storenotice on|off`). A store node tells the sender it holds the DM (`:stoNNN`); the sender's app shows status `0x04` (held) with the holder's call sign until the destination's own acknowledgement arrives. A sender on older firmware sees the notice as one ordinary text message.
- **Counters:** a `DM` setlog line (sent, echo, gateway ack, ack, gave up, attempts, outbox full, re-acks, ack round-trip histogram), plus `OUTBOX` and `MBOX` lines when those roles are on.

## Relation to `v4.35t.09.26-neo`

Everything in `v4.35t.09.26-neo` is in this build, including the `fork-main` pieces it already carried (`RADIO_TX` log line at every transmit site, `WSPWD`/`ASYM` in `SN1`, `sendPing()` reporting a refused ping, the EXTUDP boot line with a DNS target). The only upstream commits not merged are `5efa2171` and its two follow-ups — the repeat-count bits in `msg_id`, whose mask upstream itself keeps commented out ("discussion ongoing"); no behaviour difference.

## What changes on the air

With default settings, compared with `v4.35t.09.26-neo`:

1. **HN neighbourhood report** every 15 min from nodes that are neither mesh relay nor gateway (`--nbrreport auto`). Switch it off with `--nbrreport off`.
2. **NCNT is symmetric** and capped at 99 in `R<n>`, `/N`, the HEY signal group and telemetry — usually a smaller number than before.
3. **A duplicate DM is acknowledged again**, at most once per 30 s per DM.
4. **`{` inside DM text goes out as `(`**.

Everything else — `--nbrrelay`, `--dmretry`, `--store`, and with it the `:sto` notice — is off until you switch it on. Once on: the relay decision can suppress a retransmission; the outbox resends a DM up to 3 or 9 times; a store node sends a `:stoNNN` notice and later a one-hop delivery. The store node runs independently of `--mesh off`.

## Supported Hardware

### Verification for this release

- **Host suite:** 44 native environments, 1379/1379 test cases; `test/golden/selftest.sh` green.
- **Build:** all 32 release environments build clean. RAK4631 flash at 96.4 % (785,504 of 815,104 bytes).
- **This exact image has not run on a board.** It differs from the bench builds below by the `fork-main` catch-up (log lines, `SN1` resend, ping reporting) and the `FLASH_VERSION` stamp, not in the neighbour-matrix or S&F code.
- **Store-and-forward bench, 26 September, LoRa only, 2 dBm:** RAK4631 DK5EN-90 as store node (`--store heard`) and T-Beam DK5EN-92 as receiver, both on the S&F branch; Heltec V3 DK5EN-1 as sender on the neighbour-matrix build. Two runs, both PASS: the DM to the absent T-Beam was held (visible on the Mailbox page), `:sto` reached the sender, and after the T-Beam came back the RAK delivered the DM over one hop and the T-Beam's acknowledgement reached the sender. Logs: `docs/bench/snf-20260926/`.
- **Neighbour-matrix bench:** T-Deck Plus (topology survives a reboot, neighbours back within 78 s), Heltec V3, T-Beam v1.2, RAK4631 concurrency stress (2 runs, 0 consistency violations, no reset, heap stable); a replay of a 3-day field capture against a frozen copy of the old code (0 decision mismatches in 2,697 frame groups). Field run on DK5EN-1 and DK5EN-98 since 26 September, not yet evaluated.

### Built and shipped, not on our bench

Every other board in the 32 release environments builds from the same source but had no bench time on either feature.

## Known gaps, stated plainly

- **Most of the S&F test plan is still open.** Bench-proven is the store node's basic case: one absent destination, one held DM, one delivery. Not yet run on hardware: the `--dmretry` ladder, a sender on this firmware showing "held" in the app, two store nodes at once, a store node that is also a gateway, a store-node reboot with pending messages, expiry and cooldown, the caps under 25 queued DMs, the Mailbox page's Deliver/Purge buttons. The list with test IDs is in `docs/snf-port-campaign.md`.
- **Time to delivery depends on channel load.** A store node delivers only after it hears the destination's own frame. On the bench a returning node's position beacon waited about four minutes behind relay traffic before it went out.
- **A `:sto` notice can appear although the destination did acknowledge** — when the store node missed that acknowledgement. A sender on this firmware sees "held" briefly, then "acknowledged".
- **RAK4631 flash is nearly full** (96.4 % (785,504 of 815,104 bytes)).
- **The neighbour-matrix field run is not evaluated yet**, and `--nbrrelay on` has field time only on two DK5EN nodes.
- **Mixed networks show different NCNT values** for the same site (symmetric here, one-sided in older firmware).
- **The server upload still carries the received via of a relayed frame**; resetting it is left for stage 4 of the topology concept.
- Everything [`docs/CHANGELOG-neo.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.27-neo/docs/CHANGELOG-neo.md) lists as "kein Hardware-Nachweis" carries over, as do the known gaps of `v4.35t.09.26-neo`: the larger BLE command ring is unmeasured on hardware, the safeboot rework is ahead of upstream (open [PR #1162](https://github.com/icssw-org/MeshCom-Firmware/pull/1162)), and E22_XML-DevKitC and the T-Beam family run with thin link-time headroom.

## Installing

**[Web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/)** — flash over USB from the browser, 30 boards, board detection built in. `v4.35t.09.27-neo` is the default there; `v4.35t.09.26-neo` stays selectable.

Otherwise, pick the asset for your board below. ESP32 boards take the `.bin` over USB or, if the node is already reachable, over WiFi with `tools/webflash.py`. The three nRF52 boards (`wiscore_rak4631`, `heltec_t114`, `t_echo`) ship both a `.uf2` — double-tap reset, copy the file to the volume that appears — and a DFU `.zip` for `adafruit-nrfutil` over serial.

`FLASH_STRUCT_VERSION` is unchanged, so your settings survive.

## Changelogs

- [Neighbour matrix — `docs/CHANGELOG-meshcom5.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.27-neo/docs/CHANGELOG-meshcom5.md)
- [Store-and-forward — `docs/CHANGELOG-snf.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.27-neo/docs/CHANGELOG-snf.md)
- [neo code-quality campaign — `docs/CHANGELOG-neo.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.27-neo/docs/CHANGELOG-neo.md)

## Upstream

Neither feature has been offered upstream yet. Please report bugs that also exist in the official firmware to [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) directly.
