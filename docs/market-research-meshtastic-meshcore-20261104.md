# Meshtastic vs MeshCore: market research for a ham LoRa mesh

Research date: 2026-11-04. MeshCom itself was deliberately not examined.

## How to read this

- Unmarked table cells are what the cited source states. **[I]** marks my inference, **[Measured]** a count I made today from a public API or repo, **[2nd]** a secondary source (community or press), **[Unverified]** a gap.
- Each section has numbered sources; cells cite them as [n].
- Version basis:
  - Meshtastic stable is **2.7.26** (2026-06-24, GitHub "latest"). The newest build is **2.8.2 alpha** (2026-11-04).
  - meshtastic.org/docs now renders **"Version: 2.8"** by default, with "2.7 and below" as a selector, so the default docs describe firmware that is still alpha.
  - Docs were read from `meshtastic/meshtastic` master (mesh-alg last changed 2026-10-09). Source was read from `meshtastic/firmware` `develop`, so it may be ahead of any release.
  - MeshCore is **v1.18.0** (2026-10-15). Docs and source were read from `meshcore-dev/MeshCore` `main` (FAQ last changed 2026-10-15).

## Recent changes to flag

| Date       | Project    | Change                                                                                                                                 |
| ---------- | ---------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| 2026-11-04 | Meshtastic | 2.8.2 alpha: Compatible (default) no longer signs. Key verification refused in ham mode. The 11-02 build was revoked for a stack bug.  |
| 2026-10-15 | MeshCore   | v1.18.0: loop detect "minimal" is now the default. Companions can accept remote CLI from contacts.                                     |
| 2026-10-01 | Meshtastic | 2.8.1 alpha: US LongTurbo on first device-screen setup, node ID from public key, signing, telemetry/position opt-in, ham-band regions. |
| 2026-09-02 | Community  | Philly Mesh announced it would move its infrastructure to MeshCore at 500 kHz, citing FCC and interference.                            |
| 2026-06-02 | MeshCore   | `flood.max.unscoped` merged. `flood.max.advert` (default 8) is also now in the source.                                                 |
| 2026-04-23 | MeshCore   | Core team announced a split over trademark filings. The project moved to meshcore.io.                                                  |

## 1. Routing

| Aspect          | Meshtastic                                                                                                                                                            | MeshCore                                                                                                                                                                      |
| --------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Broadcast       | Managed flooding. A node listens first and cancels if it hears a rebroadcast. Low-SNR receivers get a smaller contention window, so far nodes relay first [1].        | Channels always flood through repeaters. Repeaters can limit by hops with `flood.max` [5]. Lower hop-count packets get priority (source) [8].                                 |
| Hop limit       | Max 7, default 3 ("Really, 3 is fine") [2]. Encoded in 3 header bits [1].                                                                                             | 64 stated, 0-63 in the format. 1-byte path 64 hops, 2-byte 32, 3-byte 21 [5][6]. Default `flood.max` 64, adverts 8 [7][8].                                                    |
| Direct messages | Since 2.6, next-hop routing. First flood, then learn the relay from the returning response. Per hop, one-byte next-hop field; last retry falls back to flood [1].     | Source routing. First flood, the destination floods back a path, then the full path is embedded in each packet. Only matching repeaters forward [5].                          |
| Path failure    | Falls back to managed flooding on the last retransmission [1].                                                                                                        | Fails after 3 retries, the app resets the path and floods on the last retry by default [5]. **[2nd]** Austin Mesh says 4 retries [9].                                         |
| Density scaling | 2.8 hop scaling lowers hop limit on routine broadcasts only (position, telemetry, NodeInfo, NeighborInfo) when channel use is ≥20% over 3 five-minute samples [3][4]. | Region scoping (v1.10+): transport codes, `region denyf *` blocks unscoped floods [7]. Default scope since v1.15; the blog names Germany as using `denyf *` [10].             |
| Roles           | CLIENT, CLIENT_MUTE, CLIENT_HIDDEN, CLIENT_BASE, TRACKER, LOST_AND_FOUND, SENSOR, TAK, TAK_TRACKER, ROUTER, ROUTER_LATE; REPEATER deprecated in 2.7.11 [11].          | Separate firmware per role: companion, repeater, room server (plus sensor) [5].                                                                                               |
| Who relays      | CLIENT relays if nobody else did. ROUTER and ROUTER_LATE always rebroadcast once. CLIENT_BASE always relays for favourites. CLIENT_MUTE never [1][11].                | "MeshCore clients do not repeat." Only repeaters and room servers with `set repeat on` [5]. Exception to check: the protocol doc lists "Client repeat" for firmware v9+ [13]. |
| Limiting relay  | Rebroadcast modes ALL, LOCAL_ONLY, KNOWN_ONLY, CORE_PORTNUMS_ONLY, NONE [11]. Project warns misplaced Routers cause collisions and hop inflation [12].                | Admin-set per repeater: `set repeat`, `flood.max*`, region rules, `loop.detect` [7].                                                                                          |

**[I]** Both flood group traffic, and both learn a path for DMs after an initial flood. The difference is where the state lives: Meshtastic holds next-hop state in each relay and costs 1 header byte, while MeshCore carries the whole path in each packet and costs 1-3 bytes per hop, up to 64 bytes. The strongest capacity difference is that MeshCore end-user radios do not relay by design, while Meshtastic clients relay by default.

Discrepancy: the CLI doc says `loop.detect` defaults to `off` [7], while the v1.18.0 blog [14] and source [8] say `minimal`.

Sources:

1. https://meshtastic.org/docs/overview/mesh-algo/
2. https://meshtastic.org/docs/configuration/radio/lora/
3. https://github.com/meshtastic/firmware/pull/10176 (merged 2026-06-04)
4. https://github.com/meshtastic/firmware/blob/develop/src/mesh/Default.h
5. https://github.com/meshcore-dev/MeshCore/blob/main/docs/faq.md
6. https://github.com/meshcore-dev/MeshCore/blob/main/docs/packet_format.md
7. https://github.com/meshcore-dev/MeshCore/blob/main/docs/cli_commands.md
8. https://github.com/meshcore-dev/MeshCore/blob/main/examples/simple_repeater/MyMesh.cpp and https://github.com/meshcore-dev/MeshCore/blob/main/src/Mesh.cpp
9. https://www.austinmesh.org/learn/meshcore-vs-meshtastic (verified 2026-07-09)
10. https://blog.meshcore.io/2026/04/17/default-scope
11. https://meshtastic.org/docs/configuration/radio/device/
12. https://meshtastic.org/blog/choosing-the-right-device-role/ (2024-11-10, updated 2025-09-24)
13. https://github.com/meshcore-dev/MeshCore/blob/main/docs/companion_protocol.md
14. https://blog.meshcore.io/2026/10/15/release-1-18-0

## 2. Channel capacity

| Aspect                   | Meshtastic                                                                                                                                                                                   | MeshCore                                                                                                                                                                  |
| ------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Presets                  | 9 general: Short/Medium/Long Turbo etc., 21.88 to 0.34 kbps. Narrowband families Lite (125 kHz), Narrow (62.5), Tiny (15.6) in 2.8 [1].                                                      | No firmware preset list. Suggested presets are served from `api.meshcore.nz` and are "just suggestions" [5]. 27 entries **[Measured]** [6].                               |
| Default                  | LongFast SF11/250 kHz/CR4-5. US first setup on device screen: LongTurbo SF11/500/CR4-8 [1][2].                                                                                               | Build default 869.618, BW62.5, SF8 [7]. App USA 910.525, SF7, BW62.5, CR5. EU/UK narrow 869.618, SF8, BW62.5, CR8 [6]. "Many regions" moved to narrow as of Oct 2025 [8]. |
| Regulatory duty cycle    | Per region table: US 100%, EU_868 10%, EU_866 2.5/10%. Hard stop, rolling hour, user override flag [3].                                                                                      | No region logic. Airtime budget over a 1-hour window [9]. **Default 50% (af=1)**, `set dutycycle` 1-100 since v1.15; user is responsible for jurisdiction [10].           |
| Carrier sense            | CSMA/CA with CAD mandatory. Contention window grows with channel utilisation. 2.8.2 "Smarter Listen Before Talk" [11][12].                                                                   | Hardware CAD `off` by default, `int.thresh` default 0 (disabled) [10]. `rxdelay` (weak-signal delay) experimental, off [10].                                              |
| Congestion thresholds    | Channel utilisation gate 40%, "polite" 25%. Own-TX metadata gate at half the duty cycle [13]. NodeInfo at 40%, new-node replies and position at 25%, except position for tracker roles [14]. | No utilisation gates found **[I]** (read of `Dispatcher.cpp`, `MyMesh.cpp`). Static `txdelay` 0.5, `direct.txdelay` 0.3 scaling by airtime [10].                          |
| Per-type interval limits | NodeInfo 3 h, min 1 h. Telemetry 1 h, min 30 min, routers 12 h. Position 1 h, routers 12 h, smart minimum 5 min [4][11].                                                                     | No periodic telemetry or position [8][17]. Repeater flood advert 47 h, range 3-168 [10]. Client adverts user-initiated [8].                                               |
| Busy-mesh throttling     | Above 40 online nodes: `ScaledInterval = Interval * (1 + (N-40) * 2^SF / (BW_kHz*100))`. LongFast: +0.08192 per node; 62 nodes stretches 1 h to ~168 min [11].                               | Admin-driven only: region deny, `flood.max*`, loop detect [10].                                                                                                           |
| Per-node rate limits     | 2.8 Traffic Management: position dedup on by default (6 h identical). Per-sender rate limit exists but is **opt-in** [15][16].                                                               | `anon_limiter` 4 per 3 min, `discover_limiter` 4 per 2 min [9]. No per-sender forwarding limit found **[I]**.                                                             |
| Defaults for traffic     | Telemetry off by default since 2.7.13-2.7.15 [18]. Position opt-in from 2.8 [12]. Precise position blocked on public channels in 2.8 [12].                                                   | Zero-hop repeater advert every 2 min on factory-fresh nodes, 0 once configured [10].                                                                                      |

Discrepancy: the FAQ says flood advert every 12 h [8]; the CLI doc and source say 47 h [9][10]. **[I]** The FAQ is stale.

**[I]** Text messages in Meshtastic are not gated by utilisation thresholds. The callers of `isTxAllowedChannelUtil` are position, NodeInfo, telemetry, neighbour info, range test and S&F, not text [19]. Only the regional duty-cycle hard stop applies to text. **[I]** The observed order of response is: reduce metadata first, then hops on metadata, with no default drop of user text. The project does not state this as a policy.

Sources:

1. https://meshtastic.org/docs/overview/radio-settings/
2. https://meshtastic.org/docs/getting-started/joining-your-local-mesh/
3. https://github.com/meshtastic/meshtastic/blob/master/docs/blocks/_lora-regions.mdx
4. https://github.com/meshtastic/firmware/blob/develop/src/mesh/Default.h
5. https://github.com/meshcore-dev/MeshCore/blob/main/docs/radio_presets.md
6. https://api.meshcore.nz/api/v1/config (fetched 2026-11-04)
7. https://github.com/meshcore-dev/MeshCore/blob/main/platformio.ini
8. https://github.com/meshcore-dev/MeshCore/blob/main/docs/faq.md
9. https://github.com/meshcore-dev/MeshCore/blob/main/src/Dispatcher.cpp and https://github.com/meshcore-dev/MeshCore/blob/main/examples/simple_repeater/MyMesh.cpp
10. https://github.com/meshcore-dev/MeshCore/blob/main/docs/cli_commands.md
11. https://meshtastic.org/docs/overview/mesh-algo/
12. https://github.com/meshtastic/firmware/releases/tag/v2.8.2.d5f1619 and https://github.com/meshtastic/firmware/releases/tag/v2.8.1.8e6a88d
13. https://github.com/meshtastic/firmware/blob/develop/src/airtime.h
14. https://github.com/meshtastic/firmware/blob/develop/src/modules/NodeInfoModule.cpp, https://github.com/meshtastic/firmware/blob/develop/src/mesh/MeshService.cpp and https://github.com/meshtastic/firmware/blob/develop/src/modules/PositionModule.cpp
15. https://github.com/meshtastic/firmware/blob/develop/src/mesh/NodeDB.cpp (comment: rate limiting "stay[s] opt-in")
16. https://github.com/meshtastic/firmware/pull/9358 ("When reaching 1000+ nodes, there needs to be more controls")
17. https://www.austinmesh.org/learn/meshcore-vs-meshtastic **[2nd]**
18. https://github.com/meshtastic/firmware/releases/tag/v2.7.15.567b8ea
19. GitHub code search for `isTxAllowedChannelUtil` in meshtastic/firmware, 2026-11-04

## 3. Messaging

| Aspect            | Meshtastic                                                                                                                                                                                  | MeshCore                                                                                                                                                                       |
| ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Channels          | Channel 0 plus secondaries 1-7, each with its own key. Renaming channel 0 moves the frequency slot [2].                                                                                     | Group channels by shared key. Default public channel key published. QR/link `meshcore://channel/add` [5].                                                                      |
| DMs               | PKI only since 2.7.15 [13]; no fallback to channel key. Fails with `PKI_SEND_FAIL_PUBLIC_KEY` if key unknown [3][4].                                                                        | Per-contact, routed by learned path, flood on first send [5].                                                                                                                  |
| ACK and retry     | Broadcast: implicit ACK by overhearing a relay, max 3 sends. DM: max 5 sends and a real end-to-end ACK [1].                                                                                 | DM delivery report returns via flood with the path [5]. `multi.acks` optional, default 0 [6]. **[I]** Channels have no ACK (no such field in the payload format [7]).          |
| Delivery status   | Android: queued, on device, heard by a node, recipient acknowledged, failed. Apple: similar [8]. Docs warn a relay ACK does not prove delivery [2]. 2.8.1 adds cryptographic ACK proof [3]. | Companion protocol returns an expected-ACK tag on send [9]. **[Unverified]** app display semantics.                                                                            |
| Store and forward | S&F module. ESP32 with PSRAM only. Server does not know what was missed, so duplicates. Not over LoRa on the default public channel. Heartbeat every 15 min [10].                           | Room servers: BBS with history, pushes the last 32 unseen messages on login. Run on a dedicated device [5]. DMs are not offered store-and-forward in the FAQ **[Unverified]**. |
| Size limit        | `DATA_PAYLOAD_LEN` 233 bytes including protobuf [11]. Max 239 packet data; 12 bytes more for PKC/AEAD; 66 bytes more when signed [1][4].                                                    | `MAX_TEXT_LEN` 160, `MAX_PACKET_PAYLOAD` 184, path up to 64 [7][12]. Channel text carries "name: msg" inside the ciphertext [7].                                               |

Sources:

1. https://meshtastic.org/docs/overview/mesh-algo/
2. https://meshtastic.org/docs/getting-started/joining-your-local-mesh/
3. https://meshtastic.org/docs/overview/encryption/
4. https://github.com/meshtastic/meshtastic/blob/master/docs/about/overview/encryption/limitations.mdx
5. https://github.com/meshcore-dev/MeshCore/blob/main/docs/faq.md
6. https://github.com/meshcore-dev/MeshCore/blob/main/docs/cli_commands.md
7. https://github.com/meshcore-dev/MeshCore/blob/main/docs/payloads.md
8. https://meshtastic.org/docs/faq/
9. https://github.com/meshcore-dev/MeshCore/blob/main/docs/companion_protocol.md
10. https://meshtastic.org/docs/configuration/module/store-and-forward-module/
11. https://github.com/meshtastic/protobufs/blob/master/meshtastic/mesh.proto
12. https://github.com/meshcore-dev/MeshCore/blob/main/src/MeshCore.h and https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/BaseChatMesh.h
13. https://github.com/meshtastic/firmware/releases/tag/v2.7.15.567b8ea

## 4. Security

| Aspect                | Meshtastic                                                                                                                                                                                                               | MeshCore                                                                                                                                                                     |
| --------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Default channel       | Payload always encrypted, but default key `AQ==` is published. AES-CTR; AES-CCM per channel optional since 2.8.1 [1].                                                                                                    | Public channel key published in the FAQ [5]. 16-byte key, 2-byte MAC constant [6][7].                                                                                        |
| DMs                   | X25519 + AES-256-CCM since 2.5 [1][2].                                                                                                                                                                                   | Ed25519 identities; hashes and 2-byte MAC [6][7]. V2 packet protocol with "new encryption specs" is an open roadmap item [8].                                                |
| Signing               | XEdDSA, 66 bytes, from 2.8. Policy COMPATIBLE (default), BALANCED, STRICT. **2.8.2: COMPATIBLE does not sign; only STRICT drops for lacking a signature** [3][4].                                                        | Adverts carry a 64-byte Ed25519 signature; forged ones rejected [6][9]. Channel messages unsigned: "any channel-key holder can choose any sender name" [6].                  |
| Sender authentication | Without a signature, a channel sender is "a claim, not a proof" [1]. Node number is CRC-32 of public key in 2.8, and a key from signed NodeInfo is accepted only if the CRC matches [2].                                 | DMs authenticated by shared key **[I]**. Node hash in routing is just the first public key byte; collisions are accepted [5][6].                                             |
| Key verification      | TOFU, "essentially a hard requirement" [2]. Red key icon on change [2]. Key verification with security number [3]. Users told to confirm keys out of band [1]. NodeDB 120 entries on most boards, with a warm store [2]. | Contact share by QR or `meshcore://contact/add` [5]. **[Unverified]** key-change warning and any stated TOFU model.                                                          |
| Ham mode              | Licensed mode: PKI off, plaintext. Key verification refused [3][10]. See section 5.                                                                                                                                      | None found. See section 5.                                                                                                                                                   |
| Remote admin          | Up to 3 admin public keys, PKI DM, session passkey valid 5 min. Legacy admin channel off by default. Managed mode optional [1][11].                                                                                      | Password login. **Default admin password `password`** [5][12]. Guest default conflicts: FAQ `hello`, CLI doc blank [5][12]. ACL per node [12]. BLE PIN default `123456` [5]. |
| Commercial gating     | None stated for admin.                                                                                                                                                                                                   | App "wait gate" for remote management and T-Deck admin are paid unlocks. T-Deck firmware and native apps are not open source [5].                                            |

Docs conflict, verified in code: the encryption pages say "Nodes on firmware 2.8 and later sign their broadcasts" and "Nodes in licensed mode ... sign every packet that fits" [1][2]. The 2.8.2 release note, the security page, PR #12191 and `Router.cpp` say otherwise: Compatible never signs, and ham mode signs only under Balanced or Strict [3][4][10]. **[I]** A default-configured 2.8.2 ham node therefore sends unsigned plaintext. The blog's "licensed amateur operators get signed plaintext" is true only with a non-default policy [13].

Note also the migration cost in Meshtastic: 2.7 and older strip signatures when relaying [3], and 2.8.0 signatures do not interoperate with 2.8.1 [13].

Sources:

1. https://meshtastic.org/docs/overview/encryption/
2. https://github.com/meshtastic/meshtastic/blob/master/docs/about/overview/encryption/limitations.mdx
3. https://meshtastic.org/docs/configuration/radio/security/ and https://github.com/meshtastic/firmware/pull/12201
4. https://github.com/meshtastic/firmware/pull/12191 (merged 2026-10-22) and https://github.com/meshtastic/firmware/releases/tag/v2.8.2.d5f1619
5. https://github.com/meshcore-dev/MeshCore/blob/main/docs/faq.md
6. https://github.com/meshcore-dev/MeshCore/blob/main/docs/payloads.md
7. https://github.com/meshcore-dev/MeshCore/blob/main/src/MeshCore.h
8. https://github.com/meshcore-dev/MeshCore/blob/main/README.md
9. https://github.com/meshcore-dev/MeshCore/blob/main/src/Mesh.cpp
10. https://github.com/meshtastic/firmware/blob/develop/src/mesh/Router.cpp
11. https://meshtastic.org/docs/configuration/remote-admin/
12. https://github.com/meshcore-dev/MeshCore/blob/main/docs/cli_commands.md
13. https://meshtastic.org/blog/lets-talk-about-2-8/

## 5. Licensed ham radio stance

| Aspect           | Meshtastic                                                                                                                                                                             | MeshCore                                                                                                                                                                        |
| ---------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Explicit mode    | Yes: `is_licensed`, CLI `--set-ham CALLSIGN` [1].                                                                                                                                      | None **[Measured]**: no amateur, ham, callsign or Part 97 text in FAQ, CLI doc, README, payloads; GitHub code search "amateur" and "callsign" 0 hits [5]. **[2nd]** agrees [6]. |
| Call sign        | Long name should be the call sign [1]. Ham mode sets NodeInfo to 600 s, exempt from the 1 h floor and scaling [2]. FAQ cites 47 CFR 97.119 [3].                                        | No field. Node name is free text [5][7].                                                                                                                                        |
| Encryption       | Plaintext, FAQ cites 97.113(a)(4) [3]. Licensed nodes neither send nor decrypt PKI [8].                                                                                                | No documented way to disable **[I]**. Default public key published [7] but packets remain ciphertext.                                                                           |
| Mixing           | Licensed and unlicensed modes are not interoperable, not planned [3]. 2.8: licensed nodes do not relay to or from unlicensed users [9], and do not rebroadcast encrypted packets [10]. | Not addressed.                                                                                                                                                                  |
| Ham bands        | **New in 2.8:** ITU1/2/3 2 m, 1.25 m (ITU2), 70 cm regions, selectable only in licensed mode [4]. Narrow (62.5 kHz) and Tiny (15.6 kHz) presets [11].                                  | Radio range down to 150 MHz since v1.15 [12]. Presets include EU 433 narrow [13]. No ham-specific handling.                                                                     |
| Stated rationale | Privileges: up to 10 W per 97.313(j), higher gain antennas. Restrictions: plaintext and ID [3].                                                                                        | Not stated. Describes uses including "law enforcement and private security" [7].                                                                                                |

**[I]** Meshtastic now has a real ham path, but it is a separate island by design, and it is alpha only. MeshCore's encryption-first design and missing call-sign handling leave a licensed-only niche unoccupied by it. Whether any operator runs it legally on amateur allocations is outside what I could verify.

Sources:

1. https://meshtastic.org/docs/configuration/radio/user/
2. https://github.com/meshtastic/firmware/blob/develop/src/modules/NodeInfoModule.cpp
3. https://meshtastic.org/docs/faq/
4. https://github.com/meshtastic/meshtastic/blob/master/docs/blocks/_lora-regions.mdx
5. https://github.com/meshcore-dev/MeshCore (docs dir and code search, 2026-11-04)
6. https://hamradiofornontechies.com/meshcore-vs-meshtastic-ham-radio (undated, "checked in September 2026") **[2nd]**
7. https://github.com/meshcore-dev/MeshCore/blob/main/docs/faq.md
8. https://github.com/meshtastic/firmware/pull/12201
9. https://github.com/meshtastic/firmware/pull/9958
10. https://github.com/meshtastic/firmware/blob/develop/src/mesh/NodeDB.cpp
11. https://meshtastic.org/docs/overview/radio-settings/
12. https://github.com/meshcore-dev/MeshCore/releases/tag/repeater-v1.15.0
13. https://api.meshcore.nz/api/v1/config

## 6. Infrastructure

| Aspect            | Meshtastic                                                                                                                                                                                       | MeshCore                                                                                                                                              |
| ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| Internet bridging | MQTT module in firmware. Per-channel uplink/downlink, optional JSON (not nRF52), TLS, phone proxy [1].                                                                                           | No MQTT in the firmware repo **[Measured]** (tree search). Bridge firmware options exist [5]. Observers feed a third-party analyser [6].              |
| Central server    | Project-run broker `mqtt.meshtastic.org`, public default credentials in source [2].                                                                                                              | App radio presets come from `api.meshcore.nz` [7]. Project-run map and flasher [6].                                                                   |
| Protections       | Public broker: **zero-hop into local meshes**, port filter, imprecise position only on default key [3]. `OK to MQTT` default false, a "polite request" [4]. Topic wildcard removed Aug 2024 [8]. | None needed in firmware. Region and flood controls are RF-side [5].                                                                                   |
| Public map        | Third party maps from MQTT (meshmap.net, liamcottle). Map reporting opt-in [1].                                                                                                                  | https://map.meshcore.io, user upload from app [6].                                                                                                    |
| Dependency        | RF mesh needs none. Project warns default key on a private broker can flood the mesh [3].                                                                                                        | "No central server or internet required" [9]. **[I]** The first-run preset choice depends on the API, with deep links only "planned" per the doc [7]. |
| Recent changes    | 2.8.2: gateways uplink relayed PKI DMs, still end-to-end encrypted [10]. 2.7.11: UDP off by default after feedback [11].                                                                         | None found.                                                                                                                                           |

Sources:

1. https://meshtastic.org/docs/configuration/module/mqtt/
2. https://github.com/meshtastic/firmware/blob/develop/src/mesh/Default.h
3. https://meshtastic.org/docs/software/integrations/mqtt/
4. https://meshtastic.org/docs/configuration/radio/lora/
5. https://github.com/meshcore-dev/MeshCore/blob/main/docs/cli_commands.md
6. https://github.com/meshcore-dev/MeshCore/blob/main/docs/faq.md
7. https://github.com/meshcore-dev/MeshCore/blob/main/docs/radio_presets.md
8. https://meshtastic.org/blog/recent-public-mqtt-broker-changes/ (2024-08-24; position data was "over 50% of MQTT traffic")
9. https://github.com/meshcore-dev/MeshCore/blob/main/README.md
10. https://github.com/meshtastic/firmware/releases/tag/v2.8.2.d5f1619
11. https://github.com/meshtastic/firmware/releases/tag/v2.7.11.ee68575

## 7. Updates

| Aspect                                  | Meshtastic                                                                                                                                                                                      | MeshCore                                                                                                                                                                                |
| --------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Paths                                   | Web Flasher (recommended), CLI, app-driven BLE OTA, Wi-Fi OTA (Android lists it), USB. **No OTA over LoRa** [1][2][3].                                                                          | flasher.meshcore.io. ESP32: `start ota` opens a Wi-Fi hotspot with upload page. nRF: `start ota` then Nordic DFU app. Companion OTA from v1.15 [5].                                     |
| Remote trigger                          | Not over LoRa [3]. Remote admin cannot install firmware **[I]**.                                                                                                                                | Admin logs in over RF and sends `start ota`, but transfer needs someone local on BLE or Wi-Fi [5].                                                                                      |
| Failure handling                        | nRF52: failure leaves "non-working state", physical access needed [2]. ESP32 OTA partition table: `app` ota_0 plus `flashApp` ota_1 [4]. ESP32 BLE waits for verification [14].                 | OTAFIX bootloader recommended: falls back to DFU if app invalid; third-party, listed boards [5].                                                                                        |
| Rollback / A-B                          | Not documented. **[I]** ota_1 holds the OTA loader (`MeshtasticOTA`), not a second app [4][6], so no A/B.                                                                                       | Not documented.                                                                                                                                                                         |
| Channels                                | Alpha and Beta; docs recommend latest beta [3]. GitHub marks Beta as non-prerelease. Revoked builds are relabelled.                                                                             | Single stream, no prerelease. Separate tags per role. `dev` branch exists [7].                                                                                                          |
| Releases, last 12 months **[Measured]** | **19 tags** (2025-11-04 to 2026-11-04): 2 Beta (2.7.15, 2.7.26), 13 Alpha, **4 revoked** (2.7.17.83c6161, 2.8.0 twice, 2.8.2.4c89b69). Last stable is over 4 months old [7].                    | **11 versions**, v1.10.0 to v1.18.0, 33 tags (3 per version), 0 prerelease, none revoked by label [7]. Notes moved to the blog from v1.15.                                              |
| EOL policy                              | No formal lifecycle doc found. SECURITY.md: **2.8.x supported, ≤2.7.x not**, although 2.8 is alpha and 2.7.26 is stable [8].                                                                    | SECURITY.md: 1.15+ supported, latest only, **no backports** [9]. No EOL policy found.                                                                                                   |
| Minimum version                         | Android `MIN_FW_VERSION=2.5.14`, `ABS_MIN_FW_VERSION=2.3.15` [10]. Apple: `minimumVersion="2.5.14"` with a full-screen gate [11]. On air: 2.8 no longer relays nodes without hop tracking [12]. | Not enforced on air by version. Pre-1.14 repeaters silently drop 2/3-byte paths; community asked to move only when most repeaters upgrade [5]. App version required for features [13].  |
| Breaking changes                        | Migration warnings in release notes. 2.8: node ID changes, filesystem format changes, full erase on bootloop, 2.8.0 revoked with incompatible signing [12]. Legacy DMs ended 2.7.15 [15].       | Soft: protocol extended via path hash mode and transport codes. v1.18 blog labels nothing breaking, though routing defaults changed [13]. V2 packet format still under discussion [16]. |

**[I]** Meshtastic's EOL control is through the app gate and on-air relay rules. MeshCore's rollout control is social, with regional coordination, which keeps mixed fleets working but delays new features for the whole region.

Sources:

1. https://meshtastic.org/docs/getting-started/flashing-firmware/esp32/
2. https://meshtastic.org/docs/getting-started/flashing-firmware/nrf52/ota/
3. https://meshtastic.org/docs/faq/ and https://github.com/meshtastic/meshtastic/blob/master/docs/software/android/user/firmware.md (2026-10-05)
4. https://github.com/meshtastic/firmware/blob/develop/partition-table.csv
5. https://github.com/meshcore-dev/MeshCore/blob/main/docs/faq.md
6. https://github.com/meshtastic/firmware/blob/develop/src/platform/esp32/MeshtasticOTA.cpp
7. https://github.com/meshtastic/firmware/releases and https://github.com/meshcore-dev/MeshCore/releases (GitHub API, 2026-11-04)
8. https://github.com/meshtastic/firmware/blob/develop/SECURITY.md
9. https://github.com/meshcore-dev/MeshCore/blob/main/SECURITY.md
10. https://github.com/meshtastic/Meshtastic-Android/blob/main/config.properties
11. https://github.com/meshtastic/Meshtastic-Apple/blob/main/Meshtastic/Views/Connect/FirmwareUpdateGate.swift
12. https://meshtastic.org/blog/lets-talk-about-2-8/, https://github.com/meshtastic/firmware/pull/9476, https://github.com/meshtastic/firmware/releases/tag/v2.8.1.8e6a88d
13. https://blog.meshcore.io/2026/10/15/release-1-18-0
14. https://github.com/meshtastic/meshtastic/blob/master/docs/software/apple/user/firmware.md
15. https://github.com/meshtastic/firmware/releases/tag/v2.7.15.567b8ea
16. https://github.com/meshcore-dev/MeshCore/blob/main/README.md

## 8. Hardware

| Aspect                    | Meshtastic                                                                                                                                                                                                          | MeshCore                                                                                                                                                                      |
| ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Scope **[Measured]**      | `variants/` with 202 PlatformIO files: esp32 34, esp32c3 6, esp32c6 3, esp32s2 1, esp32s3 69, nrf52840 62, nrf54l15 2, rp2040 11, rp2350 5, stm32 6, native 2 [1].                                                  | 92 variant dirs; helpers for esp32, nrf52, rp2040, stm32 [5]. Up-to-date list is the flasher [6].                                                                             |
| Official tiers            | `supportLevel`: **1 flagship, 2 niche, 3 legacy** ("capability grade"), plus `activelySupported` flag (lifecycle) and `isMaker` [2][3]. Display rungs: SUPPORTED, MAKER, COMMUNITY.                                 | No tiers found.                                                                                                                                                               |
| Becoming supported        | "New Board" issue form: "will not guarantee". SoC, LoRa IC, link [4]. Top rung tied to "Backer/Partner relationship", hard-coded in the flasher [2]. Trademark grants run two years, revocable [7].                 | No criteria found; CONTRIBUTING has no board section [8]. Practice appears to be PR-driven: release notes credit new boards by PR [9] **[I]**.                                |
| Level scan **[Measured]** | Boards per arch: classic ESP32 10 at level 3, 1 at level 2, 7 not actively supported. ESP32-S3 37 at level 1. nRF52840 31 at level 1. RP2040 2 at level 2, 2 at level 3 [1]. Parse of 123 env entries; approximate. | Not applicable.                                                                                                                                                               |
| Memory and flash          | No published minimum found. 4 MB partition layout with 2.3 MB app [10]. NodeDB 120 on nRF52840/generic ESP32 [11]. Node count sized by memory class in 2.8 [12]. Traffic Mgmt off on tiny class [11].               | No published minimum found.                                                                                                                                                   |
| Dropped or limited        | nRF52832 target removed in 2.8 [12]. **HTTP server deprecated on classic ESP32 at 2.7.21** "due to resource constraints" [13]. STM32WL: no signing, no traffic management, no hop scaling [11][14].                 | None stated in v1.18 [15]. No stated dropped platform found.                                                                                                                  |
| Platform stance           | nRF52 preferred for solar/handset. ESP32 for house power, Wi-Fi, RAM. "Strongly" recommends SX126x/LR11xx over SX127x [16]. S&F server requires ESP32 PSRAM [17].                                                   | No stated stance found. Classic ESP32 and SX127x boards still appear present, judging by variant names (`lilygo_tbeam_SX1276`, `heltec_v2`, `lilygo_tlora_v2_1`) **[I]** [5]. |
| Radio note                | SF5/6 only on 2nd gen radios; falls back to SF11 on older [18]. Philly: "legacy firmware accepts LongTurbo ... but cannot actually run it" [19] **[2nd]**.                                                          | Multibyte paths and narrow settings. FAQ warns TX settings can damage some high-power boards [6].                                                                             |

Sources:

1. https://github.com/meshtastic/firmware/tree/develop/variants (tree and `custom_meshtastic_*` keys, 2026-11-04)
2. https://github.com/meshtastic/web-flasher/blob/main/utils/deviceTier.ts
3. https://github.com/meshtastic/Meshtastic-Android/blob/main/core/model/src/commonMain/kotlin/org/meshtastic/core/model/HardwareSupportTier.kt
4. https://github.com/meshtastic/firmware/blob/develop/.github/ISSUE_TEMPLATE/New%20Board.yml
5. https://github.com/meshcore-dev/MeshCore/tree/main/variants
6. https://github.com/meshcore-dev/MeshCore/blob/main/docs/faq.md
7. https://meshtastic.org/docs/legal/licensing-and-trademark/
8. https://github.com/meshcore-dev/MeshCore/blob/main/CONTRIBUTING.md
9. https://github.com/meshcore-dev/MeshCore/releases/tag/repeater-v1.15.0
10. https://github.com/meshtastic/firmware/blob/develop/partition-table.csv
11. https://github.com/meshtastic/firmware/blob/develop/src/mesh/mesh-pb-constants.h
12. https://meshtastic.org/blog/lets-talk-about-2-8/
13. https://github.com/meshtastic/firmware/releases/tag/v2.7.21.1370b23
14. https://meshtastic.org/docs/configuration/radio/security/
15. https://blog.meshcore.io/2026/10/15/release-1-18-0
16. https://meshtastic.org/docs/hardware/devices/
17. https://meshtastic.org/docs/configuration/module/store-and-forward-module/
18. https://meshtastic.org/docs/overview/radio-settings/
19. https://phillymesh.net/2026/08/12/freq-testing-report/

## 9. Regions, US and Japan, and what happened under growth

### Frequency plans

| Aspect                        | Meshtastic                                                                                                                                                                         | MeshCore                                                                                                                                   |
| ----------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| US                            | 902-928 MHz, 100% duty, 30 dBm. LongFast slot 20 at 906.875 MHz; LongTurbo slot 14 at 908.750 MHz [1][2].                                                                          | App "USA": 910.525, SF7, BW62.5, CR5. "USA - Southern California": 927.875, 3-byte paths. "Canada": 910.525, 3-byte [3].                   |
| US default change             | LongTurbo only on first region pick on the device screen. App and CLI still LongFast. "Two nodes set up on the same day ... can sit nearly 2 MHz apart" [2][4]. Merged 2026-08-28. | Issue #945 (2025-10-15, open) requests a 500 kHz "USA Regulatory" preset. API still lists 62.5 kHz [3][5].                                 |
| Japan                         | `JP` 920.5-923.5 MHz, 100% duty, 13 dBm, default LongFast [1][6]. No Japan-specific LBT setting found in docs; CAD applies generally **[I]**.                                      | **No Japan entry in the preset API** [3]. Issue #2079 (2026-03-18, open) proposes `JP_STRICT` under ARIB STD-T108: ≤13 dBm, LBT ≥5 ms [7]. |
| Japan presence **[Measured]** | 53 nodes in today's meshmap.net MQTT snapshot, 39 on LongFast [8]. Not representative.                                                                                             | 2 map entries in a Japan bounding box, last 30 days [9].                                                                                   |

### 47 CFR 15.247 and US default bandwidth

- The text says: "Systems using digital modulation techniques may operate in the 902-928 MHz ... The minimum 6 dB bandwidth shall be at least 500 kHz" with a 1 W limit [10].
- Whether this applies is disputed in the community. Hackaday reports no public FCC statement on LoRa mesh [11] **[2nd]**. This is not a legal finding.
- Meshtastic's PR and release notes describe the LongTurbo change without giving an FCC reason [4][12]. Hackaday, Philly Mesh and YYCMesh attribute it to FCC compliance [11][13][14] **[2nd]**. Philly Mesh reports "no official word" from developers at the time [13].
- Part 97 may be a different route for licensed operators on 33 cm. **[I]** That needs a legal check. Philly Mesh mentions ham mode at 250 kHz as an option [13] **[2nd]**; Meshtastic's FAQ cites 97.313(j) for 10 W [15].

### Growth and congestion: evidence

| Community        | When              | Reported facts                                                                                                                                                                                                                                                                                                  | Evidence quality                                 |
| ---------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------ |
| UK (issue #3978) | 2024-05           | "Severely congested - almost to the point where it becomes unusable", linked to users setting hops to 7 [16].                                                                                                                                                                                                   | Anecdotal                                        |
| Wellington NZ    | 2024-08           | LongFast channel use peaks "over 65%", mostly NodeInfo, position, telemetry. 150+ nodes moved to ShortFast after 24 h trial [17].                                                                                                                                                                               | Project blog quoting operators                   |
| Bay Area         | to 2025-04        | 150+ nodes "thriving" on MediumSlow after coordinated move [17].                                                                                                                                                                                                                                                | Testimonial                                      |
| Project blog     | 2025-04-22        | Consider moving above ~60 nodes. Default not changed because it would break discovery; new default considered for 3.0 [17].                                                                                                                                                                                     | Project position                                 |
| New York         | 2025 to 2026-02   | 5 tests. MediumFast failed on links. MediumSlow reduced congestion but hurt marginal links. Moving off default slot helped avoid problem nodes. Needs filters, backbone, critical mass [18].                                                                                                                    | Operator tests, field method, no formal stats    |
| Colorado         | [Unverified date] | Moved to MediumFast; bot/nodes left on LongFast redirect newcomers. Search snippet only, page returned 403 [19].                                                                                                                                                                                                | Snippet                                          |
| Austin           | 2026-07           | Metro needs 10+ repeats; "seven hops can be a hard ceiling". Chose to focus on MeshCore. Reports "zombie" nodes and 10-20 s vs ~5 s latency [20].                                                                                                                                                               | Operator opinion; pro-MeshCore                   |
| Philadelphia     | 2026-08/09        | Operators expect ~20 new local users per week to land on LongTurbo (their estimate). LongTurbo "not viable": heavier interference (RFID readers at 9XX.25 and 9XX.75), firmware issues. LF910 better. Decision: MeshCore 500 kHz on infrastructure, dual radios at high sites. Revisit in 6-12 months [13][21]. | Qualitative, SDR captures; no channel statistics |
| Calgary (YYC)    | 2026-09+          | 500 kHz, SF11, CR4/8 on 902.6875; 300+ visible nodes; channel use "approaching 40%" at elevated sites; own TX peak ~4%. Not controlled; authors warn results may not transfer to other cities [14][22].                                                                                                         | Observational; Canadian rules                    |
| Seattle/PNW      | [Unverified date] | Whole mesh moved to MeshCore narrow [23].                                                                                                                                                                                                                                                                       | Advocacy site                                    |

**Responses seen, in the order they appeared [I from dates above]:**

1. Advice to use CLIENT and 3 hops, not ROUTER and 7 hops (2024).
2. Public broker restrictions, zero-hop (Aug 2024).
3. Community moves to faster presets (2024-2026).
4. Next-hop for DMs (2.6, 2025).
5. Telemetry off by default (2.7.13/15, late 2025).
6. Interval scaling above 40 nodes. Introduction date not verified.
7. Traffic management, hop scaling, position opt-in (2.8, 2026).
8. Parallel exits to MeshCore in some metros (2025-2026).

**[I]** Takeaways for fast growth:

- Preset splits are the main side effect of fixing congestion. Local meshes fragment by preset, by frequency and now by protocol. Meshtastic had to build Local Mesh Discovery to help new users find the right preset [2].
- Most congestion was reported as routine metadata and misconfigured nodes, not user text.
- New users arrive with defaults. Old firmware and old roles persist, and they drove the on-air exclusion in 2.8.
- A default change does not reach existing nodes. From the blog's "considering" (2025-04) to an alpha (2026-09) took about 17 months, on a narrower scope than a general default, and it has not reached a stable release.
- MeshCore's narrow settings are the subject of an unresolved FCC question, and its own response at scale has been region scoping and hop caps, set by operators.

### Snapshot counts **[Measured, not comparable]**

| Source                             | Today                                                                                                        | Caveat                                                                               |
| ---------------------------------- | ------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------ |
| meshmap.net nodes.json [8]         | 9,587 nodes; last-heard window appears ~8.5 h **[I]**. By MQTT region: US 3,422, EU_868 3,050, RU 1,406.     | MQTT-visible only. Excludes most LoRa-only nodes.                                    |
| Same, US by preset/topic           | LongFast 1,892, "map" 965, MediumFast 388, LongTurbo 75, MediumSlow 28.                                      | Topic of last-seen gateway, approximate.                                             |
| meshtastic.liamcottle.net API [24] | 29,586 nodes, all within 7 days **[I retention]**. Fields sparse. Licensed flag true: 69.                    | MQTT-visible. Firmware known for ~20%: 2.7.26 most common.                           |
| map.meshcore.io API [9]            | 70,253 entries. Last advert ≤30 days: 37,257 (33,846 repeaters). 910.5 MHz: 11,760. US bounding box: 12,904. | Self-upload and possibly tooling. Duplicates or spam not excluded. Not a user count. |

No time series for US growth was found from a primary source. The Philly figure of ~20 per week is the only rate found, and it is an operator expectation, not a measurement.

Sources:

1. https://github.com/meshtastic/meshtastic/blob/master/docs/blocks/_lora-regions.mdx
2. https://meshtastic.org/docs/getting-started/joining-your-local-mesh/
3. https://api.meshcore.nz/api/v1/config (fetched 2026-11-04)
4. https://github.com/meshtastic/firmware/pull/11637
5. https://github.com/meshcore-dev/MeshCore/issues/945
6. https://github.com/meshtastic/firmware/blob/develop/src/mesh/RadioInterface.cpp
7. https://github.com/meshcore-dev/MeshCore/issues/2079
8. https://meshmap.net/nodes.json (fetched 2026-11-04)
9. https://map.meshcore.io/api/v1/nodes (fetched 2026-11-04)
10. https://www.law.cornell.edu/cfr/text/47/15.247 (eCFR itself refused automated access)
11. https://hackaday.com/2026/09/17/fcc-ism-rules-may-shatter-lora-mesh-communities/
12. https://github.com/meshtastic/firmware/releases/tag/v2.8.1.8e6a88d
13. https://phillymesh.net/2026/09/02/fcc-regulations/ (updated 2026-09-10)
14. https://yycmesh.com/blog/500khz-field-report
15. https://meshtastic.org/docs/faq/
16. https://github.com/meshtastic/firmware/issues/3978
17. https://meshtastic.org/blog/why-your-mesh-should-switch-from-longfast/ (2025-04-22)
18. https://nyme.sh/preset-testing/
19. https://coloradomesh.org/news/mediumfast-changeover (403 on fetch; search snippet only)
20. https://www.austinmesh.org/learn/meshcore-vs-meshtastic
21. https://phillymesh.net/2026/08/12/freq-testing-report/
22. https://meshtastic.org/blog/lets-talk-about-2-8/
23. https://wnymeshcore.org/why-meshcore (search snippet only)
24. https://meshtastic.liamcottle.net/api/v1/nodes (fetched 2026-11-04)

## 10. Community and governance

| Aspect                           | Meshtastic                                                                                                      | MeshCore                                                                                                                                                                                               |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Stars / forks **[Measured]**     | firmware repo 8,529 / 2,890 [1]                                                                                 | 3,992 / 1,545 [2]                                                                                                                                                                                      |
| Contributors **[Measured]**      | ~482 including anonymous [1]                                                                                    | ~212 including anonymous [2]                                                                                                                                                                           |
| Open issues + PRs **[Measured]** | 418 [1]                                                                                                         | 905 [2]                                                                                                                                                                                                |
| Age                              | Repo since 2020-02 [1]                                                                                          | Repo since 2025-01 [2]                                                                                                                                                                                 |
| Licence                          | GPL-3.0 firmware [1]                                                                                            | MIT, copyright Scott Powell / rippleradios.com [3]. T-Deck firmware and native apps are closed [4].                                                                                                    |
| Legal holder                     | Meshtastic LLC holds the registered trademark; grants are two-year and revocable [5].                           | MeshCore Technologies Limited (NZ) formed 2026-04, per the core team [6].                                                                                                                              |
| Governance                       | "100% community driven"; volunteers; Discord, Open Collective, sponsors [7][8]. No formal governance doc found. | Five part-time core members, of whom Scott Powell and Liam Cottle are named [6]. **Split 2026-04-23** with a former member over UK/EU trademark filings; opposition running. Account is one-sided [6]. |
| Funding                          | Open Collective, GitHub sponsors [8].                                                                           | App unlocks, T-Deck keys, Buy Me a Coffee, GitHub Sponsors, legal appeal (~$18k) [4][6].                                                                                                               |
| Usage policy                     | Code of conduct, trademark rules, public MQTT restrictions [5][9][10]. No acceptable-use policy found.          | No acceptable-use policy or code of conduct found (`CODE_OF_CONDUCT.md` 404) [2]. FAQ lists security and law enforcement uses [4].                                                                     |

Sources:

1. https://github.com/meshtastic/firmware
2. https://github.com/meshcore-dev/MeshCore
3. https://github.com/meshcore-dev/MeshCore/blob/main/license.txt
4. https://github.com/meshcore-dev/MeshCore/blob/main/docs/faq.md
5. https://meshtastic.org/docs/legal/licensing-and-trademark/
6. https://blog.meshcore.io/2026/07/04/help-us-save-meshcore (core team's account only)
7. https://meshtastic.org/docs/introduction/
8. https://meshtastic.org/docs/faq/
9. https://meshtastic.org/docs/legal/code_of_conduct/
10. https://meshtastic.org/docs/software/integrations/mqtt/

## 11. Others, briefly

| Project           | Relevance                                                                                                                                                                                             | Source                                                                                                                                   |
| ----------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| Reticulum / RNode | Encryption is the default; plain destinations are local-only and not forwarded multi-hop. Goal is unlicensed use; the page does not mention amateur rules. 7,673 and 734 stars **[Measured]**.        | https://reticulum.network/manual/understanding.html, https://github.com/markqvist/Reticulum, https://github.com/markqvist/RNode_Firmware |
| LoRa APRS         | Plain APRS world, licensed by nature **[I]**. Tracker firmware has digipeater mode and frequency sets; needs iGate for internet. 522 stars for richonguzman (GPL-3.0), active 2026-10 **[Measured]**. | https://github.com/richonguzman/LoRa_APRS_Tracker                                                                                        |

**[I]** Neither changes the Meshtastic/MeshCore comparison for capacity or growth. LoRa APRS is the closest in licensing posture, but it is not a text mesh. I did not research its network size.

## 12. What could not be verified

- **US growth over time:** no primary time series. Map counts today are not comparable and measure different things.
- **MeshCore user count:** the Meshtastic ~40,000 and MeshCore ~3,500 figures appeared only on a blog, so I did not use them.
- **FCC:** no FCC statement found. Applicability of 15.247 is disputed. The Reddit claim about Semtech advisers was not read. eCFR was not reachable directly; I used the Cornell copy.
- **Meshtastic's reason for LongTurbo:** primary sources I read give no FCC rationale; blog body read only via a summarising fetch, so a statement there could be missed.
- **Japan:** ARIB STD-T108 text not read. The figures come from MeshCore issue #2079. Community state in Japan not established.
- **Meshtastic:** formal EOL policy, board-support criteria and governance documents not found, which does not prove absence. Minimum-memory figures not found. Beta date for 2.8 not published in what I read. The introduction date of interval scaling was not traced. No A/B rollback documented.
- **MeshCore:**
  - App delivery-status semantics (closed app).
  - TOFU or key-change handling.
  - S&F for DMs.
  - The "Client repeat" field from firmware v9+ in the companion protocol. This may concern Ultra firmware, and conflicts with "clients do not repeat" unless scoped.
  - Whether the dispatcher does other carrier checks with CAD off.
  - Absence of per-sender forward limit is from targeted reading, not a full audit.
- **Counts:** board-tier counts come from a simple parse of 202 ini files and 123 env entries, so treat as approximate.
- **Fetch method:** many blog and web pages were read through a summarising fetch. Quotes from these (Hackaday, YYCMesh, Philly Mesh, New York, Austin, 2.8 blog, MeshCore blogs) are close but should be re-checked before being cited externally. Meshtastic docs and all GitHub files were read directly.
- **Not fetched:** Colorado (403) and WNY (search snippet only).
