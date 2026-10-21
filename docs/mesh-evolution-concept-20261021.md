# MeshCom Mesh Evolution: Review, Ideas and Roadmap (Part B)

| Item          | Value                                                                                                                                                  |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Type          | Concept paper, discussion draft; nothing decided                                                                                                       |
| Date          | 2026-10-21                                                                                                                                             |
| Part A        | [`tx-pool-dedup-concept-20261021.md`](tx-pool-dedup-concept-20261021.md): TX pool, TTL, dedup, hop model (10.0), hop cap, `{CET}`, HG, DM/ACK priority |
| Code baseline | `fork-dev` at `7fe790f`; "since v4.40a" means since `f2b3a349` (#1212, `RESUME.md`; the tag `v4.40a.hbserver` is not in the clone), 65 commits         |
| Presentation  | [`presentation/mesh-roadmap.html`](presentation/mesh-roadmap.html) (GitHub Pages, German, interactive)                                                 |
| Evidence rule | Every number is read from the code, cited from a doc or a web source, or marked COMPUTED / INFERRED / NO SOURCE                                        |

## 1. BLUF

Part A fixes how one node holds and ages frames. Part B asks which frames should be on
the channel at all, at which rate, from whom, and how far. Five findings shape the
roadmap:

1. **The October QRT measures were shipped without on-air proof.** Both waves are in
   `v4.40a.10.19`; the planned night capture between them did not happen. Several
   measures help hilltop nodes and can hurt dense city clusters (section 3). They need a
   measurement wave before anything is built on top.
2. **The channel is the bottleneck, not RAM.** At SF11/BW250 an 80-byte position frame
   takes 1.1 s (COMPUTED). A tracker at the 30 s floor with two relay rings of three
   nodes each fills 26 % of the channel alone (section 4.4). Rate, relay count and
   reach decide utilisation; queue tuning only decides who loses.
3. **Personal messages lose their priority at the first relay** (Part A 10.10). An
   absolute PM priority is cheap in code; making it safe against abuse is the hard part.
4. **Multi-SF is the only lever that multiplies capacity.** SF9 carries 3.4 times SF11
   for 5 dB less margin (section 4.5). Every chip in the shipped envs supports SF7 to
   SF12; the constraint is that a single-radio node listens on one SF at a time.
5. **EMCOMM needs a priority flag and sender authentication together.** A priority flag
   without authentication is an invitation to flood the channel. RM1's HMAC is
   symmetric and does not scale to "many receivers verify one sender"; a signature with
   pinned keys does, at 64 bytes per frame (section 4.9).

Order of the roadmap (section 6): measurement, TX buffer, buffer parity, hop clamp
cleanup, PM priority, end of life for old firmware, EMCOMM authentication, tag
groups, multi-SF; then the speculative block (link stretches, cells, source routing).

## 2. Fact base (delta to Part A 10.0)

### 2.1 Radio parameters

- Default EU profile: 433.175 MHz, BW 250 kHz, SF11, CR 4/6, preamble 32
  (`src/configuration_default.h`; icssw.org installation guide,
  <https://icssw.org/en/meshcom-4-0-installation/>). Belgium and UK profiles use
  BW 125 / SF10 (`src/country_profile.cpp:84-97`). LoRa-APRS is a separate service on
  433.775 MHz, BW 125, SF12, CR 4/5 (<https://icssw.org/en/2026/06/10/meshcom-lora-aprs/>).
- One node-wide SF/BW/CR; no per-packet SF (`src/lora_setchip.cpp:528-546`). The mod
  code is sent in `msg_source_mod` (`getMOD()`, `src/lora_setchip.cpp:181-203`).
- `LORA_SNR_STABLE_MIN_DB` -16 dB is already the fleet constant for "stable link" in the
  neighbour matrix (`src/configuration_default.h`, `src/nbr_matrix.h:110`). It matches
  the field experience "links work well down to -16 dB".

### 2.2 Chips per env (`variants/*/platformio.ini`, `src/esp32/esp32_main.cpp:781-870`)

| Chip   | Envs                                                                                                                                                                                                      | SF range (source)                                                                                     |
| ------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| SX1262 | Heltec V3/V4, Wireless Stick/Tracker, T-Deck (all), T-Beam SX1262/Supreme/1W, T3-S3, T-Connect Pro, T-ETH-Elite, Vision Master, Wireless Paper, T5 e-paper, E22 1262 (classic, S3), RAK4631, T-Echo, T114 | SF5-SF12, BW 7.8-500 kHz (Wio-SX1262 module datasheet via R3)                                         |
| SX1268 | E22-DevKitC, E22_XML, E22 1268 S3, esp32-loraprs-e22, T-Beam SX1268                                                                                                                                       | SF5-SF12, 410-810 MHz variant (no per-SF source found)                                                |
| SX1276 | Heltec V2, TTGO T-Beam (SX127x), esp32-loraprs-ra01                                                                                                                                                       | SF6-SF12 (NO SOURCE in this run; SF6 needs implicit header)                                           |
| LLCC68 | none shipped (`BOARD_E220` path exists, no env)                                                                                                                                                           | SF5-SF9 @125, SF5-SF10 @250, SF5-SF11 @500; **cannot run SF11/BW250** (Semtech forum, Zephyr binding) |

Every shipped env can run SF7 to SF12 at BW 250. Only concentrators (SX1302/SX1303)
demodulate several SFs at once (8 to 16 parallel packets, Semtech product page); a
single SX126x/SX127x listens on one configured SF/BW. CAD is configured per SF
(INFERRED from the RadioLib API; no datasheet text read).

### 2.3 Frame and protocol facts used below

- Message types on air: 0x3A text, 0x21 position, 0x40 HEY, 0x41 binary ACK, 0x3C
  LoRa-APRS/track (`src/configuration_global.h:419-422`). The valid-type set is
  {0x21, 0x3A, 0x40, 0x41} (`src/loop_functions.cpp:3533`). Free type bytes exist
  (INFERRED by grep; external-radio framing to check before reuse).
- Byte 5 has no free bit: nibble plus 0x10/0x20/0x40/0x80 (Part A 10.0 item 1).
- Firmware trailer after the FCS: fw version byte, last-hardware byte, sub-version
  char, 0x7E (`src/aprs_functions.cpp:1117-1134`). **A version filter already exists:**
  decode drops frames with `0 < fw_version < 35` (`:533-537`, "4.35a").
- Groups: numeric 1..99999, at most 6 per node (`node_gcb[6]`,
  `src/aprs_functions.cpp:39-100`); no group-specific relay rule. A `#TAG` user story
  exists as a discussion draft for 4.36 (`docs/userstory-hashtag-filter.md`).
- Track: `--track on` switches SmartBeaconing; moving rates 20/15/12/10 s by speed,
  stationary 1800 s, a 30 s floor between sends (`src/loop_functions.cpp:6193-6341`,
  `src/esp32/esp32_main.cpp:4579`, `beacon_rate.h`). The 0x40 track bit marks a
  non-default cadence (`src/loop_functions.cpp:5350`, `:5408`).
- RM1: HMAC-SHA256 truncated to 8 bytes (16 hex), key SHA-256 of the console password,
  counter-based replay protection, closed command allowlist, plaintext on air
  (`docs/adr-remote-hmac.md`, `src/remote_cmd.cpp:232-250`, `:515-523`).
- No multi-part text: a LoRa frame is at most 255 bytes (`UDP_TX_BUF_SIZE`); longer text
  is truncated or rejected (INFERRED).
- No EMCOMM code in `src/` (only the `meshcom.emcomm.ch` server entry,
  `src/gw_servers.cpp:28`).

### 2.4 SF table (BW 250 kHz, CR 4/6, preamble 32)

SNR limits are the Semtech typical values (SX127x modem design guide,
<https://www.mouser.com/pdfdocs/semtech-lora-modem-design.pdf>); SF5/SF6 are
extrapolated. Bit rate, time on air and sensitivity (NF 6 dB) are COMPUTED.

| SF  | SNR limit (dB) | Bit rate (kbit/s) | ToA 60 B (ms) | ToA 80 B (ms) | ToA 120 B (ms) | Sensitivity (dBm) | Throughput vs SF11 | Margin vs SF11 |
| --- | -------------- | ----------------- | ------------- | ------------- | -------------- | ----------------- | ------------------ | -------------- |
| 7   | -7.5           | 9.1               | 78            | 96            | 130            | -121.5            | 11.6x              | -10 dB         |
| 8   | -10            | 5.2               | 144           | 174           | 236            | -124.0            | 6.3x               | -7.5 dB        |
| 9   | -12.5          | 2.9               | 263           | 312           | 422            | -126.5            | 3.4x               | -5 dB          |
| 10  | -15            | 1.6               | 501           | 599           | 796            | -129.0            | 1.8x               | -2.5 dB        |
| 11  | -17.5          | 0.90              | 903           | 1100          | 1444           | -131.5            | 1.0x               | 0              |
| 12  | -20            | 0.49              | 1905          | 2298          | 3084           | -134.0            | 0.47x              | +2.5 dB        |

**The -24 dB observation.** -24 dB at SF11 is 6.5 dB below the typical demodulation
limit; the PER transition is a few dB wide (Simulink study, error-free to about -18 dB
at SF12, <https://journals.vilniustech.lt/index.php/NTCS/article/view/25705>). A true
decode at -24 dB is unlikely. More likely the reported SNR is biased or quantised near
the floor, or a fading peak carried the packet (INFERRED). A bench test with a
calibrated attenuator decides it; until then the reported SNR below about -18 dB should
be treated as "at the edge", not as a measured margin.

## 3. Adversarial review of the measures since v4.40a

The QRT campaign (`docs/campaign-lora-qrt-20261018.md`) was written and shipped in two
days under pressure from two field incidents: DB0TAW-14 (hilltop, 24 direct neighbours,
41 to 43 % RX, 16 to 19 relays queued, 0.0 % TX) and DK5EN-98 (gateway, 3.8 copies per
server message, 6.4 per own message). The review asks for each measure: why it was
right, why it may be wrong, and what it does to **hilltop nodes** (wide coverage, many
neighbours heard, often the only bridge) and to **dense city clusters** (many neighbours
in short range, all meshing, little coverage gain per transmission).

**Process finding first.** The plan recommended release after wave 1, one night capture,
then wave 2. Both waves are in `v4.40a.10.19`; no post-fix on-air result exists
(`release-journal.md` 10.19 "offen", `RESUME.md` "Open: DK5EN-98 night capture").
Evidence for the fixes is pre-fix captures and replays. That is the main risk of the
whole block.

| Measure                                                                                                            | Why it was right                                                                                                                    | Why it may be wrong                                                                                                                                                                                                                                                | Hilltop                                                                                                                                             | City cluster                                                                                                                                                         |
| ------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `--nbrrelay on` forced in production (`prod_lock.h:177`)                                                           | 3.8 to 6.4 copies per message measured; coverage-based cancel is the largest airtime lever; case A gives a sole bridge a short path | decisions rest on a 12 h matrix and a symmetry assumption (`nbrsym`); classic boards hold 64 rows (Part A 10.7); the bench day still showed only `CANCEL?` candidates; no post-fix proof; locked, so a bad field result cannot be switched off without a new build | good: case A (alone covers someone) gets the 150 ms escape after 8 s                                                                                | risk: every node cancels because "the others cover"; with overlapping masks several can cancel at once (mutual cancel) and a frame dies in the cluster's shadow side |
| CSMA age floor (Brief G: 20 s stage 1, 60 s stage 2 at 100 ms + jitter, prio 1-3)                                  | send starvation measured (4.0 sends/min, 316 interruptions); replay 11.2 sends/min                                                  | compile-time, on by default, replay only; stage 2 removes CSMA's spreading for aged frames: nodes that aged together escalate together (synchronisation); boards without a radio object compute airtime 0 and use the 1500 ms cap                                  | helps the hilltop get out of a full queue; but its aggressive TX collides at distant receivers with valley traffic it cannot hear (hidden terminal) | in a congested cluster many frames age at once; stage 2 turns that into near-simultaneous bursts, more collisions, more retries                                      |
| Echo releases own queued group text; retry stops if already heard                                                  | 20 ignored echoes and 5 messages with 3 retries measured                                                                            | an echo from a neighbour proves only that the neighbour has it, not that the far side does                                                                                                                                                                         | neutral to good: the hilltop's echo usually comes from a far relay                                                                                  | risk: an echo from a node 50 m away stops the retry although the message never left the cluster                                                                      |
| No retry while QRT                                                                                                 | 7 evictions, 4 server texts lost to own retries                                                                                     | DM reliability under load drops exactly when it matters; the user sees no retry                                                                                                                                                                                    | rarely in QRT unless backlog                                                                                                                        | often in QRT under load: DMs lose their ladder                                                                                                                       |
| Own message reflected by the server not queued                                                                     | own message re-sent 311 s later                                                                                                     | none found                                                                                                                                                                                                                                                         | good                                                                                                                                                | good                                                                                                                                                                 |
| Ring full returns -2, QTA at any depth; QRT release below 16, 60 s repeat                                          | 11 of 19 drops at 11 or fewer occupied slots; 6 min 57 s refused with 15 free slots                                                 | treats the symptom of the ring's gaps (Part A problem A); the thresholds were tuned on one replay                                                                                                                                                                  | good                                                                                                                                                | good                                                                                                                                                                 |
| shortpath forced off (`prod_lock.h:176`)                                                                           | the full path feeds the neighbour matrix, loop check and gwcap                                                                      | longer frames: 40 to 80 B costs 756 to 1100 ms at SF11 (+344 ms, COMPUTED) per transmission and relay; gwcap trips on long injected paths (Part A 10.1)                                                                                                            | more airtime per relay on the busiest node                                                                                                          | more airtime per copy, multiplied by many copies                                                                                                                     |
| HN report forced `auto`, `nbrsym` forced on, pingtime/pingmax locks, postime clamp 300 s                           | stops misconfiguration (pingtime without target silenced HEY/HN)                                                                    | locks remove field tuning; postime 300 s does not limit track mode                                                                                                                                                                                                 | neutral                                                                                                                                             | neutral                                                                                                                                                              |
| Not done: Finding 3 (gateway cancels already-heard server frames), Finding 6 (90 s server max age), Finding 9 rest | -                                                                                                                                   | Finding 3 is the gateway-side analogue of cancel-on-overhear and attacks the 3.8 copies directly                                                                                                                                                                   | -                                                                                                                                                   | -                                                                                                                                                                    |

**Assessment (proposal, not decided).** Keep the measures that remove pure waste (server
reflection, echo release for the own node, ring -2). Put the three with a plausible
negative effect on dense clusters under measurement first: forced `--nbrrelay on`
(mutual cancel), age floor stage 2 (synchronised bursts) and the echo-based retry stop
(local echo). For each, wave 0 needs a metric: frames that died in a cluster (heard by
nobody beyond it), burst collisions after a stage-2 escalation (`aged2` counter plus
CRC errors in the following second), retries stopped by an echo with RSSI above, say,
-80 dBm. Reconsider making `--nbrrelay` switchable by RM1 instead of compile-locked, so
a bad field result can be reverted without a release.

## 4. Ideas, one by one

Each idea: problem, options with pro and contra, assessment. All open.

### 4.1 What onion routing teaches a ham mesh

Layered encryption is not allowed on amateur bands (content must stay readable; 47 CFR
97.113 for the US; DL/OE equivalent not checked, NO SOURCE) and anonymity is not a goal,
since stations identify. What transfers is the infrastructure around it
(<https://spec.torproject.org/proposals/207-directory-guards.txt>):

| Tor concept                       | Mesh analogue                                                                                         | Use in this roadmap                         |
| --------------------------------- | ----------------------------------------------------------------------------------------------------- | ------------------------------------------- |
| Directory authorities, consensus  | the server (or several) publishes node list, roles, firmware state, public keys; signed               | EOL (4.10), trust store (4.9)               |
| Relay flags (Stable, Fast, Guard) | flags from measured uptime, SNR, ACK success: "high site", "stable", "gateway"                        | relay election (4.15), source routing (4.7) |
| Guard nodes                       | each node pins one or two preferred first-hop relays instead of flooding through whoever is loudest   | source routing, city clusters               |
| Circuit reuse                     | a discovered path is cached and reused until it fails                                                 | source routing (4.7)                        |
| Bandwidth-weighted path selection | prefer relays with spare airtime, not only good SNR                                                   | relay election, multi-SF                    |
| Rendezvous points                 | a well-known gateway or store node as meeting point (store-and-forward exists, `snf-gateway-concept`) | PM reliability (4.3)                        |
| Key pinning                       | pin a sender's public key on first verified contact or from the directory                             | trust store (4.9)                           |

Assessment: the directory, flags and pinning are the useful part. They give the mesh a
shared view of who is who, which is what the neighbour matrix does locally today.

### 4.2 Tag groups (`#TAG`) instead of numbers

Problem: numeric groups are meaningless, centrally assigned and limited to 6 per node
(`docs/userstory-hashtag-filter.md`, sections 1 to 2).

| Option                                                              | Pro                                                                           | Contra                                                                                                                    |
| ------------------------------------------------------------------- | ----------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| T0 status quo                                                       | compact (up to 5 digits), works with every firmware and app                   | 6 groups, no meaning, no hierarchy                                                                                        |
| T1 tag in the destination field (`>#OE1-SOTA`)                      | clear addressing; filter by prefix gives hierarchy                            | old firmware treats it as a callsign or drops it (`checkRegexCall`); destination length limit; server and app must follow |
| T2 tag in the payload, destination `*`                              | old firmware shows it as broadcast text, nothing breaks; filter is local only | every node receives and relays every tagged message: no airtime gain, only display filtering                              |
| T3 tag hash in the destination (`>G1A2B3`), tag text in the payload | short on air; old nodes see an unknown group and relay as text                | collisions of short hashes; needs a mapping in app and server                                                             |

Airtime view: groups today do not limit relaying (no group-specific relay rule), so tags
change usability, not channel load. Only a relay rule "relay a group only towards nodes
that subscribed" would save airtime, and that needs subscription data in the HEY or HN
report (large change).

Assessment: T2 as a compatible first step (display filter, unlimited subscriptions, no
protocol change), T1 for 4.36-class firmware once server and app support it. Relay
scoping by subscription is a later, separate idea.

### 4.3 Absolute priority and reliability for personal messages

Problem: own DM and own `:ackNNN` are priority 1; relayed they fall to 3 (Part A 10.10,
3.1). Under QRT own retries stop. The ladder (3 x 40 s) re-sends the whole DM when the
small ACK is late.

| Option                                                     | Pro                                                                                            | Contra                                                                                                         |
| ---------------------------------------------------------- | ---------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------- |
| P-A0 status quo                                            | simple; foreign traffic cannot starve own traffic                                              | DMs and ACKs queue behind group relays after hop 1                                                             |
| P-A1 relayed DM and `:ackNNN` to 1 (absolute)              | fastest DM path; ACK stops the ladder early                                                    | a DM flood (bug or abuse) gets the top band on every relay; competes with own ACKs (verdict finding 8 caution) |
| P-A2 relayed DM and ACK to 2, own stay 1                   | ahead of group relays, behind own critical traffic                                             | parsing of `DONE` text; group text from the own node shares band 2                                             |
| P-A3 P-A2 plus a per-source rate limit for band 1/2 relays | bounds the abuse case (for example 1 DM per source per 10 s in the high band, rest falls to 3) | state per source (fits in the neighbour matrix row); tuning                                                    |
| P-A4 ACK-first: relayed `:ackNNN` and 0x41 always 1        | the smallest frames that save the most airtime                                                 | DM itself unchanged                                                                                            |
| P-A5 retries allowed in QRT for DMs only                   | DM reliability exactly under load                                                              | undoes part of the QRT fix (7 evictions); needs the pool (Part A stage 2) so a retry does not evict blindly    |

Speed beyond priority: the DM's CSMA base is 3000 ms own and 4500 ms relayed; with the
age floor stage 1 aged DMs escalate at 20 s. Source routing (4.7) is the structural
speed lever: fewer copies, so less channel contention for everyone.

Assessment: P-A4 plus P-A3 as the first wave step (ACKs first, DMs in band 2 with a
per-source limit), P-A5 after the slot pool. P-A1 only with authentication (EMCOMM
class, 4.8).

### 4.4 Multi-line APRS and track mode without flooding the channel

**Open input:** the "new multi-line APRS format" was not found in a web search (APRS
1.2, aprs.org, aprs.fi, icssw.org; NO SOURCE). The format must come from the operator.
What follows holds for any multi-line form.

Airtime arithmetic (COMPUTED, SF11/BW250/CR4/6): one 120-byte frame costs 1444 ms, two
60-byte frames 1806 ms, because each frame pays preamble and header (297 ms). Splitting
costs about 25 % more per line pair; one long frame is cheaper but has a higher loss
probability per frame.

Track mode is the bigger load. An 80-byte frame (a position with path; size INFERRED) takes 1100 ms. At the 30 s floor a
tracker transmits 3.7 % of the time. With `max_hop_pos` 2 and three relays in each of
two rings, every position goes out 7 times: 26 % of the channel for one moving station;
at the 10 s SmartBeaconing rate (if the floor were lifted) 77 %.

| Option                                                                                   | Pro                                                            | Contra                                                                                                     |
| ---------------------------------------------------------------------------------------- | -------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| K0 status quo                                                                            | full track resolution at every node within 2 hops              | one tracker can take a quarter of the channel                                                              |
| K1 track positions with hop 0 or 1 (local only), every Nth with full hop                 | map near the station stays live; far nodes get a coarse track  | needs a per-frame hop for positions (today `max_hop_pos` fixed 2); old relays obey the lower nibble anyway |
| K2 track via gateway and server only (hop 0 on RF, gateway uploads)                      | RF cost one transmission; the internet map has full resolution | no RF track for nodes without internet                                                                     |
| K3 airtime budget per source (for example 5 % per tracker in a cell), enforced by relays | bounds the worst case; same rule for every type                | relays need per-source airtime state; tuning                                                               |
| K4 compact track frames (delta position, no path, short trailer)                         | 30 to 40 B instead of 80 B halves the cost (INFERRED)          | new frame type; app and server must decode                                                                 |
| K5 multi-line as one frame with line separators, length capped                           | cheapest per line                                              | one loss loses all lines; display support needed                                                           |

Assessment: K1 plus K3 address "track on overruns the frequency" directly and need no new
frame type. K4 is the larger, later win. For multi-line: K5 once the format is known.

### 4.5 Multi-SF: more capacity, compatible like Wi-Fi rate adaptation

Idea (operator): a faster mode that does not make the slowest node decide; links upgrade
where they can, everything stays compatible.

The Wi-Fi pattern: beacons and broadcast at the base rate, unicast per link at the
fastest rate the link margin allows, fallback on missed ACKs. LoRaWAN ADR does the same
centrally (best SNR of 20 uplinks minus required SNR minus 15 dB margin,
<https://www.thethingsnetwork.org/docs/lorawan/adaptive-data-rate/>). Meshtastic has no
per-link adaptation; all nodes share one preset (<https://meshtastic.org/docs/overview/radio-settings/>).

The constraint: a single SX126x/SX127x receives one SF at a time. A node at SF9 is deaf
to SF11 floods while it listens at SF9.

| Option                                                                   | Pro                                                                                                                                                                                     | Contra                                                                                                                                       |
| ------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| M0 status quo, one SF for all                                            | simple, everyone hears everything                                                                                                                                                       | capacity fixed at SF11                                                                                                                       |
| M1 per-link upgrade for unicast with rendezvous                          | base SF for floods, HEY, positions; a DM between two neighbours with margin uses SF9 or SF10 after an SF11 "switch" handshake; both listen at the fast SF for one exchange, then return | both nodes are deaf to the base channel during the exchange (missed floods); needs a handshake frame and timing; only for DM, not for floods |
| M2 CAD scanning: listen on SF11, CAD for SF9 between                     | a node can detect a fast-rate preamble                                                                                                                                                  | CAD per SF is sequential, short preambles may be missed; more RX power; PER on the base channel rises                                        |
| M3 second channel for a fast backbone (different frequency, SF9)         | no deafness on the base channel; hilltop and gateway nodes form a fast layer                                                                                                            | needs a second radio (dual-radio boards are rare) or a dedicated node per site; frequency plan (4.14)                                        |
| M4 regional SF choice (SF10 in dense city cells, SF11/12 in the country) | city capacity x1.8 at 2.5 dB less margin, where margins are large                                                                                                                       | cells must agree; a node at the border between SF regions cannot hear both; bridges needed                                                   |
| M5 concentrator gateways (SX1302/1303)                                   | a gateway hears 8 to 16 packets on several SFs at once                                                                                                                                  | gateway hardware only; nodes still send on one SF                                                                                            |

SNR threshold (operator's question). The fleet already defines -16 dB as "stable"
(`LORA_SNR_STABLE_MIN_DB`). An upgrade to SF9 needs the link SNR at least 5 dB above
the SF9 limit plus fading margin: SF9 limit -12.5 dB, so links at -7 dB or better
(COMPUTED, margin 5.5 dB assumed). A deliberate floor for relay and routing decisions
(not for reception) could be set at -16 dB at SF11: frames from below are still
delivered locally, but a node does not count such a neighbour as a coverage edge and
does not route DMs over it. Pro: fewer paths over links that drop half the packets.
Contra: in sparse rural areas the -16 to -20 dB links are the only ones; a hard floor
would cut them. Assessment: use the floor for path choice and coverage, never for
reception, and measure PER per SNR bin from the bench fleet before choosing the value.

Assessment for multi-SF: M4 (regional SF) and M3 (fast backbone layer) are the realistic
first steps; M1 is elegant but the deafness problem makes it a research item.

### 4.6 Hop clamp cleanup for circling messages (masking bug)

Part A 10.0 item 9 and 10.1 H1: builds without the fix wrap the nibble 0 to 15. The fork
fixed its own relay path; the field still carries wrapped frames from 4.35x. A nibble cap
on relay (`min(nibble - 1, L)` or `min(nibble - 1, MAX_HOP_LIMIT)`) stops them at the
first fixed relay, 0 B. Combined with EOL (4.10) the wrapping firmware leaves the mesh.

### 4.7 Source routing for personal messages

Problem: every DM floods the whole hop radius, and so does its ACK.

The path is already in the frame: with shortpath off (production) the source path lists
every relay. The recipient therefore knows a working path at the moment it sends the
`:ackNNN`.

| Option                                                  | Discovery                                                      | Routing                                                                                                                           | Pro                                                                                                                                | Contra                                                                                      |
| ------------------------------------------------------- | -------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| S0 status quo                                           | none                                                           | flood                                                                                                                             | robust, stateless                                                                                                                  | N copies per DM and per ACK                                                                 |
| S1 next-hop learning (Meshtastic 2.6 style)             | the first DM floods; the reply shows which relay carried it    | each node remembers "for destination X, relay Y worked"; a DM is relayed only by the remembered next hop, flood on the last retry | backward compatible; state per destination, not per path; proven elsewhere (<https://meshtastic.org/blog/meshtastic-2-6-preview/>) | needs a "next hop" field in the frame (no free bit in byte 5; trailer or payload)           |
| S2 full source route (MeshCore style)                   | flood with path record (exists), the ACK carries the path back | the DM lists the relays; only listed relays forward; flood again on missing ACK (<https://wiki.eastmesh.au/meshcore/routing/>)    | fewest copies; deterministic                                                                                                       | header grows per hop (about 10 B per call); old firmware ignores the list and floods anyway |
| S3 hybrid: S2 with a hop-limited flood around the route | as S2                                                          | listed relays plus their direct neighbours may forward                                                                            | survives one moved node                                                                                                            | more copies than S2                                                                         |

Where to put the route: a payload prefix (`{R:CALL1,CALL2}`) is readable and old nodes
treat it as text; a new message type hex ID is cleaner but invisible to old firmware.
Interop: as long as old firmware floods, the copies do not drop until most relays run the
new firmware; the saving grows with the fleet share.

Assessment: S1 first (smallest change, compatible, no route header), S2 later for
store-and-forward nodes and fixed sites.

### 4.8 EMCOMM priority flag: where to put it

Byte 5 has no free bit. Options:

| Place                                             | Pro                                               | Contra                                                                   |
| ------------------------------------------------- | ------------------------------------------------- | ------------------------------------------------------------------------ |
| E1 payload prefix (`{EMC}` or `#EMCOMM` tag)      | old firmware shows it as text; no protocol change | trivial to fake; parse cost                                              |
| E2 new message type (hex ID)                      | unambiguous; room for a signature field           | invisible to old firmware (not shown, not relayed)                       |
| E3 trailer extension (byte after the sub-version) | out of the text                                   | the trailer format is fixed (3 bytes + 0x7E); old decoders may reject it |
| E4 destination convention (`>EMCOMM`, group)      | uses the existing group mechanism                 | no priority semantics in old firmware; fakeable                          |

Any of these is fakeable without authentication. Assessment: E1 or E4 for visibility in
the old fleet, and priority only when the frame also carries a valid signature (4.9);
an unsigned EMCOMM frame is shown, relayed normally, not prioritised.

### 4.9 Sender authentication for EMCOMM

RM1's HMAC authenticates a command to one node with that node's password. For EMCOMM many
receivers must verify one sender. Legal frame: authentication without encryption keeps
content readable and is argued to be allowed (<https://rietta.com/blog/authentication-without-encryption-for/>);
no ruling for DL/OE found (NO SOURCE): **regulatory check needed (DARC/BNetzA, ÖVSV)**.

| Option                                                    | Size per frame                         | Trust                                    | Pro                                                            | Contra                                                                                       |
| --------------------------------------------------------- | -------------------------------------- | ---------------------------------------- | -------------------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| A1 RM1 HMAC with a shared EMCOMM net key                  | 8 B (16 hex)                           | everyone with the key                    | code exists; small                                             | anyone with the key can sign as anyone; one leak breaks all; key distribution out of band    |
| A2 per-sender HMAC keys in a receiver trust store         | 8 B                                    | receiver holds each sender's secret      | small frames                                                   | every receiver holds every secret; a compromised receiver can forge; does not scale          |
| A3 Ed25519 signature, public keys pinned in a trust store | 64 B (about +500 ms at SF11, COMPUTED) | pinned public keys (32 B each)           | forging needs the private key; receivers hold only public keys | 64 B per frame; signature verify about ms on ESP32 (INFERRED); key management in the web GUI |
| A4 A3 with keys from a server directory                   | 64 B                                   | the directory's signing key, pinned once | onboarding without manual pinning; revocation                  | needs internet once; who runs the directory (DARC? ÖVSV? icssw?)                             |
| A5 key announce on air (new hex ID), TOFU plus manual pin | 64 B + announce                        | first contact, then pinned               | works offline                                                  | first contact can be spoofed; announces cost airtime                                         |

Trust store (operator): per entry callsign (10 B) + public key (32 B) + flags (1 B), about
43 B; 100 entries 4.3 kB, fits flash (LittleFS), RAM only for a cache. "Who do you trust":
a pinned hash per callsign in the web GUI (show the key fingerprint, operator confirms),
optionally a pinned directory key for A4. The web GUI checkmark "sign EMCOMM messages"
sets the flag per message or per session.

Assessment: A3 with manual pinning plus A4 as optional directory is the scalable design;
A1 is a quick bench prototype with known weaknesses. 64 B per frame restricts it to
EMCOMM traffic, not every message.

### 4.10 End of life for old firmware

A filter exists: frames from firmware below 4.35 are dropped at decode
(`src/aprs_functions.cpp:533-537`).

| Option                                         | Pro                                                                           | Contra                                                                                                    |
| ---------------------------------------------- | ----------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| L0 status quo (drop < 4.35)                    | exists                                                                        | 4.35x wrapping nodes still relayed                                                                        |
| L1 do not relay frames whose last hop runs < N | the mesh stops amplifying old relays' behaviour; their own frames still shown | the last hop is `msg_last_hw`/trailer of the relay, not the origin: check which fields describe the relay |
| L2 do not relay frames originated by < N       | stops old originators' load                                                   | users who did not update lose reach without notice                                                        |
| L3 drop entirely (as today for < 4.35)         | simplest                                                                      | splits the mesh; frames without trailer (very old) are invisible                                          |
| L4 server-announced minimum version            | one switch for all; can be raised step by step                                | needs the directory (4.1) or `{SET}`-like frame; unauthenticated today                                    |

Assessment: L1 for the nibble-wrapping builds (targets the masking bug's carriers), with a
notice frame to the affected node (DM "please update") before the block, and a published
date. L4 once a signed directory exists.

### 4.11 Buffer parity and consolidation

Part A gives the TX pool and dedup by time. The frame-holding queues beyond that (display
queue, BLE out, phone rings, LOG ring, msgstore; inventory in
`docs/architecture/10-buffer-inventory.md`) each reserve full frame slots. Consolidation
option: one byte pool with descriptors for all frame-holding queues (Part A O5); a frame
received once is referenced by the display, BLE and relay queues instead of copied three
times. Pro: the parity lever for classic boards. Contra: every consumer changes to handles,
lifetime rules per consumer. Needs an inventory pass with sizes per board class first.

### 4.12 Link stretches

Dedicated point-to-point links between hilltop sites (like packet-radio backbones): a
second frequency or SF, directional antennas, no flooding on the link. Pro: carries
inter-region traffic without touching local channels; today the server fan-out does this
over the internet (Part A 10.0 item 8). Contra: sites, licences, hardware. Fits M3 (4.5).

### 4.13 Cell architecture and colouring

Idea: hexagonal cells, neighbouring cells on different frequencies, a bridge node per
cell border. Classic reuse needs 3 colours for adjacent cells on a hexagonal lattice, 7
for a reuse distance of two cells (graph colouring,
<https://arxiv.org/pdf/1711.03096>). Channels: at 70 cm in practice 433.175 (MeshCom),
433.775 (LoRa-APRS, taken) and 433.900 MHz; the IARU R1 plan for LoRa segments was not
found (NO SOURCE); the 433.05 to 434.79 MHz range is shared with ISM/LPD. With at most 2
to 3 free 250 kHz channels a frequency colouring is thin.

Other "colours":

| Colour dimension | Pro                                         | Contra                                                                               |
| ---------------- | ------------------------------------------- | ------------------------------------------------------------------------------------ |
| Frequency        | true separation                             | few channels; bridges need two radios or time sharing                                |
| SF               | same frequency; SFs are quasi-orthogonal    | inter-SF interference is real, worst at SF10-12 (<https://arxiv.org/pdf/1808.01761>) |
| Time (slots)     | one radio; TDMA colouring of neighbour sets | needs synchronised clocks (GPS/NTP/`{CET}`, see Part A 10.8) and a slot plan         |
| Role             | not every node relays (4.15)                | election logic                                                                       |

Assessment: frequency cells are speculative at 70 cm; role colouring (who relays) and SF
regions (M4) are the reachable forms of the idea.

### 4.14 City clusters: elect relays instead of everyone meshing

Not yet named: the OLSR idea of multipoint relays. Each node picks the smallest set of
direct neighbours that covers all its two-hop neighbours; only those relay its floods.
The neighbour matrix and HN reports hold the two-hop data already. The case A/B logic is a
reactive form of this (decide per frame); an MPR set is proactive (decide per topology
change) and deterministic, which avoids the mutual-cancel risk of section 3. Pro: the
structural fix for "many neighbours, little coverage". Contra: depends on matrix quality
(Part A 10.7), larger change, needs a fallback when the MPR set is stale.

### 4.15 Further candidates

- Adaptive TX power for city nodes (less power, smaller interference footprint, same
  coverage when neighbours are close). Bench rule stays 2 dBm.
- Role flags announced in HEY (gateway, high site, mobile, client-only) as in Meshtastic's
  ROUTER/CLIENT_MUTE roles; a client-only node never relays.
- Finding 3 from the QRT verdict (gateway cancels already-heard server frames).
- Airtime accounting per source in the neighbour matrix row (basis for K3 and P-A3).

## 5. Priority matrices per message type

Today (code, Part A 3.1) and two discussion variants. Lower is earlier. CSMA base in
brackets (ms).

| Frame kind                         | Today own  | Today relayed | Variant V1 (PM first) | Variant V2 (EMCOMM + PM)          |
| ---------------------------------- | ---------- | ------------- | --------------------- | --------------------------------- |
| EMCOMM, signed                     | -          | -             | -                     | 0 own and relayed (new band)      |
| Binary ACK 0x41                    | 1 (3000)   | 1 (3000)      | 1                     | 1                                 |
| DM ACK `:ackNNN`                   | 1 (3000)   | 3 (4500)      | 1                     | 1                                 |
| DM                                 | 1 (3000)   | 3 (4500)      | 1 own, 2 relayed      | 1 own, 2 relayed, rate-limited    |
| Group/broadcast text               | 2 (3000)   | 3 (4500)      | 2 own, 3 relayed      | 2 own, 3 relayed                  |
| `{CET}`, `{SET}`, server injection | 1/2 (once) | 3 (4500)      | 3                     | 3, `{CET}` with TTL (Part A 10.8) |
| EMCOMM, unsigned                   | -          | -             | -                     | as text, shown with a warning     |
| Position                           | 4 (5500)   | 4 (5500)      | 4                     | 4, track budget (4.4 K3)          |
| HEY (`H`, `HG`)                    | 5 (5500)   | 5 (5500)      | 5                     | 5                                 |

The presentation shows these matrices interactively per scenario.

## 6. Roadmap and wave plan

Same loop as the v4.40a campaign: plan, implement, test (host gate, golden sets), bench,
field capture, evaluate, learn, adjust. Each wave ends with an upstream PR cut from
`upstream/dev` (German), as in `CLAUDE.md`.

Estimates are calendar time for one operator with agent support, based on the 4.40a and
QRT campaigns (QRT: 15 to 25 min per brief, about 60 min gate and bench per wave,
`docs/campaign-lora-qrt-20261018.md`), plus the field nights the QRT campaign skipped.
They are estimates, not commitments.

| Wave | Content                                                                                                                                           | Gate and golden sets                                                                                                                        | Field proof                                                                      | Estimate     |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------- | ------------ |
| W0   | Measurement: Part A stage 0 plus section 3 metrics (cluster death, stage-2 bursts, echo-stopped retries), hop/`{SET}`/`{CET}`/HG/DM-band counters | host gate; new golden: hop-nibble corpus, CET-age replay                                                                                    | 24 to 48 h DK5EN-98 (explicit OK) plus one bench night                           | 1 week       |
| W1   | TX buffer: TTL per priority, slot pool, supersede (Part A stages 1, 2, 4)                                                                         | `test_txring`, `test_txring_flood`, `test_txprio_replay`, `test_nbr_replay`                                                                 | DK5EN-1/-92/-90 gwflood replay, 1 night                                          | 2 weeks      |
| W2   | Buffer parity and consolidation: time-aged dedup, neighbour coverage window, buffer inventory, byte-pool study                                    | `test_dedup_replay` with clock (8813 verdicts, two overflows become DUP), `test_topo_shadow`, RAM baseline (`tools/resource_baseline.json`) | DK5EN-98 soak: no `RX_DEDUP_NEW` for ids older than 39 min                       | 2 weeks      |
| W3   | Hop clamp cleanup: nibble cap on relay (H1), gwcap review                                                                                         | new nibble corpus with wrapped frames; `test_aprs_corpus`                                                                                   | count of nibble > L before/after                                                 | 3 to 5 days  |
| W4   | PM priority, reliability, speed: ACK first, DM band 2 with per-source limit, DM retries in QRT after the pool; S1 next-hop study                  | `test_ack_replay`, new DM-latency replay (golden: DG0OPK DM round trips, 1 min quiet / 4 min loaded)                                        | DM round-trip median and p95 on the bench fleet, group `TEST` and own nodes only | 2 weeks      |
| W5   | End of life: relay filter for wrapping builds, notice DM, published date                                                                          | trailer corpus per firmware version                                                                                                         | count of blocked frames, no loss of current-firmware frames                      | 1 week       |
| W6   | EMCOMM: priority flag (E1/E4), Ed25519 signatures (A3), trust store with pinning in the web GUI; regulatory check first                           | signature vectors (golden), trust-store persistence lint                                                                                    | bench: signed frame verified on all four boards, unsigned shown with warning     | 3 to 4 weeks |
| W7   | Tag groups: T2 display filter, then T1 with app and server                                                                                        | settings schema lint, group corpus                                                                                                          | bench and one user group                                                         | 1 to 2 weeks |
| W8   | Multi-SF: regional SF (M4) and backbone layer (M3) as experiments; PER per SNR bin first                                                          | PER/SNR measurement set (golden for the threshold)                                                                                          | attenuator bench, then one city cell                                             | 4 to 6 weeks |
| W9+  | Speculative: source routing S2, link stretches, cell/role colouring, MPR election (4.14), adaptive power                                          | per item                                                                                                                                    | per item                                                                         | open         |

Dependencies: W0 before everything that changes relay behaviour; W1 before W4 (retries
need the pool); W2 before W5 and W6 (RAM for trust store); W6 before any absolute priority
for foreign traffic; W8 after W0's PER data.

Recurring verifications (already in use): host gate `tools/regression.sh --stage 1,2`,
golden selftest (48), dedup replay, nbr replay, gwflood replay, APRS corpus and fuzz,
bench suite stage 3, DK5EN-98 soaks. Each wave adds its golden set to that list.

## 7. Open questions for the operator

1. Source of the "new multi-line APRS format" (4.4).
2. Whether forced `--nbrrelay on` should become RM1-switchable (section 3).
3. EMCOMM trust root: manual pinning only, or a directory, and who runs it (4.9).
4. EOL threshold and date (4.10).
5. Regulatory check of signatures on 70 cm in DL and OE (4.9).
6. Whether W0 may run on DK5EN-98 (production gateway).
