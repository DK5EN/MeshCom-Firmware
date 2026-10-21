# MeshCom Mesh Evolution: Review, Ideas and Roadmap (Part B)

| Item          | Value                                                                                                                                                  |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Type          | Concept paper, discussion draft; nothing decided                                                                                                       |
| Date          | 2026-10-21                                                                                                                                             |
| Part A        | [`tx-pool-dedup-concept-20261021.md`](tx-pool-dedup-concept-20261021.md): TX pool, TTL, dedup, hop model (10.0), hop cap, `{CET}`, HG, DM/ACK priority |
| Code baseline | `fork-dev` at `7fe790f`; "since v4.40a" means since `f2b3a349` (#1212, `RESUME.md`; the tag `v4.40a.hbserver` is not in the clone), 65 commits         |
| Presentation  | [`presentation/mesh-roadmap.html`](presentation/mesh-roadmap.html) (GitHub Pages, German, interactive)                                                 |
| Evidence rule | Every number is read from the code, cited from a doc or a web source, or marked COMPUTED / INFERRED / NO SOURCE                                        |
| Review        | Adversarial review (28 findings) and verification pass applied 2026-10-21                                                                              |

## 1. BLUF

Part A fixes how one node holds and ages frames. Part B asks which frames should be on
the channel at all, at which rate, from whom, and how far. Five findings shape the
roadmap:

1. **The October QRT measures were shipped without on-air proof.** Both waves are in
   `v4.40a.10.19`; the planned night capture between them and the bench run did not
   happen. Most measures remove waste; three have a plausible downside in dense clusters
   or for priority order (section 3). DK5EN-98 has run the fixed build since 10-18, so
   its logger may already hold the first post-fix data.
2. **The channel is the bottleneck, not RAM.** At SF11/BW250 an 80-byte frame takes
   1.1 s (COMPUTED). A frame every 30 s, relayed by two rings of three nodes, fills 26 %
   of the channel. Rate, relay count and reach decide utilisation; queue tuning only
   decides who loses. The cheapest single lever is the preamble: 32 symbols in the EU
   default, 8 in most other country profiles; 8 saves 197 ms per frame (COMPUTED).
3. **Personal messages lose their priority at the first relay** (Part A 10.10). An
   absolute PM priority is cheap in code; making it safe against abuse is the hard part.
4. **Multi-SF is the only lever that multiplies capacity.** SF9 carries 3.4 times SF11
   for 5 dB less margin (section 4.5). Every chip in the shipped envs supports SF7 to
   SF12; a single-radio node listens on one SF at a time. A per-packet profile switch
   already ships (track mode to the LoRa-APRS QRG), so its deafness cost can be measured.
5. **EMCOMM needs a priority flag and sender authentication together.** A priority flag
   without authentication invites flooding. RM1's HMAC is symmetric and does not scale
   to "many receivers verify one sender"; a signature with pinned keys does, at 88 bytes
   Base64 in a text payload (section 4.9).

Order of the roadmap (section 6): measurement, TX buffer, buffer parity, hop clamp
cleanup, PM priority, end of life for old firmware, EMCOMM authentication, tag
groups, multi-SF; then the speculative block (link stretches, cells, source routing).

## 2. Fact base (delta to Part A 10.0)

### 2.1 Radio parameters

- Default EU profile: 433.175 MHz, BW 250 kHz, SF11, CR 4/6, preamble 32
  (`src/configuration_default.h`; <https://icssw.org/en/meshcom-4-0-installation/>).
  UK, ON, LA and UK8 use BW 125 / SF10; every country row in the table uses preamble 8,
  only the default row (codes 0, 3, 16) uses `LORA_PREAMBLE_LENGTH` (32)
  (`src/country_profile.cpp:84-109`). LoRa-APRS is a separate service on 433.775 MHz,
  BW 125, SF12, CR 4/5 (<https://icssw.org/en/2026/06/10/meshcom-lora-aprs/>).
- One SF/BW/CR for MeshCom traffic, but **a per-packet profile switch exists**: with
  `--track on` and a GPS fix the position goes out as a LoRa-APRS `<` frame
  (`src/loop_functions.cpp:5191-5201`, `:5272`); `lora_setchip_aprs()` switches to
  433.775/SF12/BW125 for that frame and `lora_setchip_meshcom()` back
  (`src/lora_functions.cpp:2998-3094`, `src/esp32/esp32_main.cpp:3663-3668`; nRF52 branch
  `src/lora_setchip.cpp:387`; not on `BOARD_TLORA_OLV216`). While switched the node does
  not relay (`rly_reason "aprs"`, `src/lora_functions.cpp:2425-2431`); the deaf time is
  about the SF12 airtime plus re-init (unmeasured). The MeshCom QRG gets a copy every
  5 min (`track_to_meshcom_timer`, `:3005`).
- `LORA_SNR_STABLE_MIN_DB` -16 dB is the fleet constant for "stable link" in the
  neighbour matrix (`src/nbr_matrix.h:110`). It matches the field experience "links work
  well down to -16 dB".

### 2.2 Chips per env (`variants/*/platformio.ini`, `src/esp32/esp32_main.cpp:781-870`)

| Chip   | Envs                                                                                                                                                                                                      | SF range (source)                                                                                     |
| ------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| SX1262 | Heltec V3/V4, Wireless Stick/Tracker, T-Deck (all), T-Beam SX1262/Supreme/1W, T3-S3, T-Connect Pro, T-ETH-Elite, Vision Master, Wireless Paper, T5 e-paper, E22 1262 (classic, S3), RAK4631, T-Echo, T114 | SF5-SF12, BW 7.8-500 kHz (Wio-SX1262 module datasheet via R3)                                         |
| SX1268 | E22-DevKitC, E22_XML, E22 1268 S3, esp32-loraprs-e22, T-Beam SX1268                                                                                                                                       | SF5-SF12, 410-810 MHz variant (no per-SF source found)                                                |
| SX127x | Heltec V2, TTGO T-Beam (433 MHz: SX1278), esp32-loraprs-ra01 (SX1278), ttgo-lora32-v21 (no track/APRS switching)                                                                                          | SF6-SF12 (NO SOURCE in this run; SF6 needs implicit header)                                           |
| LLCC68 | none shipped (`BOARD_E220` path exists, no env)                                                                                                                                                           | SF5-SF9 @125, SF5-SF10 @250, SF5-SF11 @500; **cannot run SF11/BW250** (Semtech forum, Zephyr binding) |

Every shipped env can run SF7 to SF12 at BW 250. Only concentrators (SX1302/SX1303)
demodulate several SFs at once (8 to 16 parallel packets, Semtech product page); a
single SX126x/SX127x listens on one configured SF/BW. CAD is configured per SF
(INFERRED from the RadioLib API; no datasheet text read).

### 2.3 Frame and protocol facts used below

- Message types on air: 0x3A text, 0x21 position, 0x40 HEY, 0x41 binary ACK
  (`src/configuration_global.h:419-422`); 0x3C `<` is the LoRa-APRS frame
  (`src/aprs_functions.cpp:154`). Free type bytes exist (INFERRED by grep;
  external-radio framing to check before reuse).
- Byte 5 has no free bit: nibble plus 0x10/0x20/0x40/0x80 (Part A 10.0 item 1).
- Firmware trailer after the FCS, **not covered by it**: fw version byte, last-hardware
  byte, sub-version char, 0x7E (`src/aprs_functions.cpp:1110-1134`). The fw byte and
  sub-version are the **originator's**; a relay rewrites only `msg_last_hw` and
  `msg_source_mod` (`src/lora_functions.cpp:2463-2464`). Trailer parsing is optional:
  trailerless frames pass with `fw_on_air=false`, bytes after the sub-version are
  ignored (`src/aprs_functions.cpp:492-531`, `src/aprs_structures.h:84-87`). **A version
  filter exists:** decode drops frames whose originator runs `0 < fw_version < 35`
  (`:533-537`, "4.35a").
- Text payload: ends at 0x00 and passes a charset filter (C0/C1 controls, invalid UTF-8,
  bidi and zero-width characters stripped, `src/aprs_functions.cpp:405-425`). Binary
  data cannot ride in a text payload.
- Groups: numeric 1..99999, at most 6 per node (`node_gcb[6]`,
  `src/aprs_functions.cpp:39-100`); no group-specific relay rule. A `#TAG` user story
  exists as a discussion draft for 4.36 (`docs/userstory-hashtag-filter.md`).
- Track: SmartBeaconing; moving rates 20/15/12/10 s by speed, stationary 1800 s, a 30 s
  floor between sends (`src/loop_functions.cpp:6193-6341`, `src/esp32/esp32_main.cpp:4578`,
  `beacon_rate.h`); sent on the LoRa-APRS QRG (2.1).
- RM1: HMAC-SHA256 truncated to 8 bytes, sent as 16 hex characters, key SHA-256 of the
  console password, counter-based replay protection, closed allowlist, plaintext on air
  (`docs/adr-remote-hmac.md`, `src/remote_cmd.cpp:232-250`, `:515-523`).
- Via field: the destination path `VIA,DEST` lets only the named via relay
  (`checkMesh()`, `src/via_functions.cpp:88-140`); each relay resets the destination path
  unless it has its own `--via` (`src/lora_functions.cpp:2486`), so it acts for one hop.
- HN reports: at most 8 entries ('+' when truncated), links at or above the stable SNR,
  fresh within 60 min, every 15 min (`src/nbr_matrix.h:192-211`, `:628-632`); under the
  default `auto` only nodes with mesh and gateway off send them
  (`src/loop_functions.cpp:5815-5835`).
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

**Preamble.** The table uses preamble 32 (EU default). With preamble 8 every frame is
197 ms shorter at SF11 (24 symbols of 8.192 ms, COMPUTED): 80 B take 903 ms instead of
1100 ms, about 18 % less. Most country rows already use 8 (2.1); the QRT stage-1 figures
0.71 s / 1.25 s are preamble-8 airtimes (`docs/campaign-lora-qrt-20261018.md:419-420`).
A shorter preamble lowers the detection margin for receivers that wake late or run CAD
(Contra), and nodes with different preamble settings still decode each other only if the
receiver's preamble setting is not longer than the sender's (INFERRED, to verify on the
bench).

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

**Process findings first.**

- The plan recommended release after wave 1, one night capture, then wave 2. Both waves
  are in `v4.40a.10.19`; no post-fix on-air result and no bench run exist
  (`release-journal.md` 10.19 "offen", including "LoRa-QRT auf der Bench"). Evidence for
  the fixes is pre-fix captures and replays. DK5EN-98 runs the fixed build since 10-18
  (`RESUME.md`), so its logger may already hold post-fix data for W0.
- The production locks have two origins: campaign W4 (`e4edffc`) locked `--nbrrelay`
  **off**, `--nbrsym` on, `--nbrreport` auto, `--shortpath` off, pingtime, pingmax and
  spec\*; QRT wave 1 (`fe3f01e`) flipped `--nbrrelay` to locked **on**.
- Every measure here is fork-only (`docs/fork-dev-vs-upstream-20261018.md`). In a mixed
  cluster upstream nodes keep flooding, so every cluster effect below scales with the
  fork's share of the nodes.

| Measure                                                                                                            | Why it was right                                                                                                                      | Why it may be wrong                                                                                                                                                                                                                                                                                                                                                                                                                                                          | Hilltop                                                                                                            | City cluster                                                                                                                                                                                                                     |
| ------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `--nbrrelay on` forced in production (`prod_lock.h:177`)                                                           | 3.8 to 6.4 copies per message measured; coverage-based cancel is the largest airtime lever; case A (sole provider) is never cancelled | a cancel follows an overheard foreign repetition whose coverage the matrix believes to remove the remaining need (`src/lora_functions.cpp:1299-1386`); that belief rests on a 12 h matrix, `nbrsym`-inferred bits (logged as `removed_inferred`) and 64 rows on classic boards (Part A 10.7); locked, so a bad field result needs a new build                                                                                                                                | good: case A gets 3500 ms, then 150 ms after 8 s                                                                   | at least one node always sends (an uncancelled relay goes out, `:2553`), but a **false cover** (stale or asymmetric edge) can cancel the copy that would have reached a shadow node behind the cluster                           |
| CSMA age floor (Brief G: stage 1 at 20 s, stage 2 at 60 s: 100 to 275 ms, prio 1-3)                                | send starvation measured (4.0 sends/min, 316 interruptions); replay 11.2 sends/min; never applied after own TX, so no channel capture | compile-time, on by default, replay only. Not a new synchronisation: same-priority nodes already shared base 4500 + 11 slot choices, and rapid fire at 100 ms existed after 3 CAD attempts (`src/lora_functions.cpp:3938`); stage 2 cuts the choices to 6 and merges prio 1-3. **Priority inversion across nodes:** an aged prio-3 relay at 100 to 275 ms beats another node's fresh prio-1 DM or ACK waiting 3000 ms (within one node the lower priority number still wins) | helps the hilltop out of a full queue; as a receiver it suffers when mutually hidden valley nodes escalate at once | stage 2 fires only after stage 1 failed for 40 s, i.e. in saturation; then many aged frames compete in a 175 ms window; CAD separates nodes in mutual range, hidden pairs collide. The campaign doc names this risk (`:413-416`) |
| Echo release of own group text (SENT on first echo: older; READY release: QRT `fe3f01e`)                           | 20 ignored echoes and 5 messages with 3 retries measured                                                                              | an echo from a neighbour proves only that the neighbour has it. On a gateway the first echo is usually another gateway's server copy, so the gateway may never put its own group message on its own RF island (`docs/campaign-lora-qrt-20261018.md:181`). DMs are exempt (`pnFrameIsReleasableGroupText`, `src/pn_retry.h:362-378`)                                                                                                                                          | neutral to good: the hilltop's echo usually comes from a far relay                                                 | an echo from a node 50 m away releases the group text although it never left the cluster                                                                                                                                         |
| No retry while QRT                                                                                                 | 7 evictions, 4 server texts lost to own retries                                                                                       | the retry is deferred, not dropped: re-checked every tick, fired after the QRT release below 16/20 (`src/lora_functions.cpp:3446-3459`, `backpressure.h:256-262`); a slot already at the retry cap still gives up during QRT                                                                                                                                                                                                                                                 | rarely in QRT unless backlog                                                                                       | DMs wait out the QRT, latency rises, delivery stays                                                                                                                                                                              |
| Own message reflected by the server not queued                                                                     | own message re-sent 311 s later                                                                                                       | deviation C as shipped skips own-source server frames of every type; the phone copy is lost for positions and for texts to groups the node does not follow (`docs/campaign-lora-qrt-20261018.md:32-34`)                                                                                                                                                                                                                                                                      | good                                                                                                               | good                                                                                                                                                                                                                             |
| Ring full returns -2, QTA at any depth; QRT release below 16, 60 s repeat                                          | 11 of 19 drops at 11 or fewer occupied slots; 6 min 57 s refused with 15 free slots                                                   | treats a symptom of the ring's gaps (Part A problem A); thresholds tuned on one replay                                                                                                                                                                                                                                                                                                                                                                                       | good                                                                                                               | good                                                                                                                                                                                                                             |
| shortpath locked off (`prod_lock.h:176`)                                                                           | the full path feeds the neighbour matrix, loop check and gwcap                                                                        | `bSHORTPATH` defaulted to false already (`src/loop_functions.cpp:421`), so the lock changes nothing for default nodes. The full path costs about 10 B, +98 ms at SF11, per extra hop (COMPUTED); gwcap trips on long injected paths (Part A 10.1)                                                                                                                                                                                                                            | more airtime per relay on the busiest node                                                                         | more airtime per copy, multiplied by many copies                                                                                                                                                                                 |
| HN report locked `auto`, `nbrsym` on, pingtime/pingmax locks, postime clamp 300 s                                  | stops misconfiguration (pingtime without target silenced HEY/HN)                                                                      | locks remove field tuning; under `auto` relays and gateways send no HN report (2.3), so the two-hop picture comes only from end nodes                                                                                                                                                                                                                                                                                                                                        | the hilltop's own view stays one-sided                                                                             | neutral                                                                                                                                                                                                                          |
| Not done: Finding 3 (gateway cancels already-heard server frames), Finding 6 (90 s server max age), Finding 9 rest | -                                                                                                                                     | Finding 3 is the gateway-side analogue of cancel-on-overhear and attacks the 3.8 copies directly                                                                                                                                                                                                                                                                                                                                                                             | -                                                                                                                  | -                                                                                                                                                                                                                                |

**Assessment (proposal, not decided).** Keep the measures that remove pure waste
(server reflection, with the phone-copy gap fixed; ring -2; deferred retry). Put three
under measurement first: forced `--nbrrelay on` (false cover), age floor stage 2
(priority inversion and hidden-pair collisions), and the READY echo release on gateways
(own RF island never served). W0 metrics: cancels whose `removed_inferred` was
non-empty; frames from inside the cluster heard by an observer outside it; prio-1
frames from other nodes delayed by a stage-2 burst (`aged2` counter against prio-1 wait);
gateway group texts released by a server copy before any own RF transmission. Frames
that "died in a cluster" need observer nodes outside the cluster; a 2 dBm bench in one
room cannot show it. Consider making `--nbrrelay` switchable by RM1 instead of
compile-locked, so a bad field result can be reverted without a release.

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

### 4.4 Multi-line position (APRS weather companion) and track mode

#### 4.4.1 Multi-line POS: upstream #1219, reverted by #1223

**What it was.** Upstream PR #1219 (Mateusz-Krajewski, 2026-10-19,
<https://github.com/icssw-org/MeshCom-Firmware/pull/1219>) added `--aprswx on|off`
(`node_sset2` 0x2000, default off): after every own MeshCom position a **second `!`
frame** with an APRS 1.0.1 weather block (`/_` symbol,
`.../...g...tTTTr...p...P...hHHbBBBBB`), so that aprs.fi shows the node as a weather
station. MeshCom carries WX today as MeshCom-specific tags in the position comment,
which aprs.fi does not treat as a weather station. Code: `PositionToAprsWx()` and
`sendAprsWxCompanion()` in `src/loop_functions.cpp` (diff `06a4fd9..05b842a`); the frame
is relayed like any position (`max_hop_pos`, `DONE`, priority 4) and uploaded by gateways.

**Why it was reverted.** oe1kbc merged the revert #1223 on 2026-10-20 ("please check
notes from DK5EN .. in the moment we will revert"). The review points in #1219: every own
position becomes two frames, both relayed ("will overflow the LoRa network"); the `/_`
symbol overwrites the station symbol in clients that keep the latest one; MeshCom
decoders do not read `t/h/b`, so the block lands in the comment; bit 0x2000 was
`--checkmesh` until June, so old settings could switch it on by themselves; format edge
cases (a fake `t032` without temperature, `temp2`-only nodes without a frame, minutes
rounding to `xx60.00`); `sendAprsWxCompanion()` duplicates part of `sendPosition()` and
skips some of its steps. No measurement or rebuttal is in the thread.

**Airtime of the companion (COMPUTED, SF11/BW250/CR4/6).** The WX payload is 55
characters; with header, path and trailer the frame is about 75 to 85 B: 1.05 to 1.15 s
with preamble 32, 0.85 to 0.95 s with preamble 8. At the default 30 min interval
(`POSINFO_INTERVAL`) that is 2 extra frames per hour per WX node, each relayed; with two
rings of three relays 14 extra transmissions, about 15 s per hour, 0.4 % of the channel
per WX node. It scales with the number of WX nodes in range (50 WX nodes: about 20 %),
and it doubles whatever the position rate is.

| Option                                                                                                     | Pro                                                           | Contra                                                                                                                       |
| ---------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| X0 status quo (WX tags in the MeshCom position comment)                                                    | one frame; MeshCom clients decode it                          | aprs.fi shows no weather station                                                                                             |
| X1 #1219 as it was (second `!` frame on RF, relayed)                                                       | aprs.fi weather station without a server change               | doubles position airtime; symbol overwrite; settings-bit collision; reverted                                                 |
| X2 conversion at the gateway or the server: MeshCom position with WX tags in, APRS WX frame out to APRS-IS | **no extra RF airtime**; one place; no symbol overwrite on RF | only stations heard by a gateway; the gateway (fork firmware) or the server (outside the fork) must know the MeshCom WX tags |
| X3 one frame in APRS WX position format instead of the normal position, for WX nodes only                  | no extra frame; standard APRS on RF                           | MeshCom decoders must learn `t/h/b` (old ones show it as comment); the weather symbol replaces the node symbol               |
| X4 companion with hop 0 at a lower rate (for example every 6th position), gateway uploads                  | RF cost one transmission per few hours                        | only gateways in direct range see it; still a second frame                                                                   |
| X5 companion only on gateways for their own sensor                                                         | gateways send it straight to APRS-IS without RF               | covers only gateway-attached sensors                                                                                         |

Assessment (proposal, not decided): X2 meets the goal (aprs.fi weather station) without
RF cost and cannot overflow the channel; X3 if a standard APRS frame on RF is wanted.
Both need a native test for the WX formatter (the edge cases above) and a settings bit
that was never used before.

#### 4.4.2 Track mode and the QRG switch

**What track mode does today** (2.1): with `--track on` and a GPS fix each position
goes out as a LoRa-APRS frame on 433.775 MHz, SF12/BW125; the node switches its radio
for that frame and back. During the switch it does not relay MeshCom traffic. The
MeshCom QRG gets one copy every 5 minutes. At the 30 s floor a 50-byte LoRa-APRS frame
costs about 2.3 s every 30 s on the APRS QRG (about 7.7 %, COMPUTED), plus WIDE1-1
digipeaters there.

**Why the switch exists (INFERRED):** a track on the MeshCom QRG would be expensive.
An 80-byte frame takes 1100 ms at SF11/BW250 (preamble 32); every 30 s with
`max_hop_pos` 2 and three relays in each of two rings it goes out 7 times: 26 % of the
channel for one moving station.

**Multi-part text, for comparison.** Airtime for any multi-part form (COMPUTED, SF11/BW250/CR4/6/preamble 32): one 120-byte frame costs
1444 ms, two 60-byte frames 1806 ms; each frame pays preamble plus fixed header symbols
(362 ms). One long frame is cheaper but loses all lines on one error.

| Option                                                                | Pro                                                             | Contra                                                                                               |
| --------------------------------------------------------------------- | --------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| K0 status quo (APRS QRG, MeshCom copy every 5 min)                    | MeshCom channel stays free; APRS-IS and APRS maps get the track | node deaf on MeshCom during each switch; track invisible on MeshCom between copies; SF12 costs 2.3 s |
| K1 track on the MeshCom QRG, hop 0 or 1, every Nth with full hop      | no switch, no deafness; near nodes see the live track           | MeshCom airtime per tracker 3.7 % (own) plus relays; needs a per-frame hop for positions             |
| K2 track on MeshCom with hop 0, gateway uploads to APRS-IS            | one RF transmission per position; maps fed by the gateway       | no RF track beyond one hop; needs a gateway in range                                                 |
| K3 airtime budget per source on MeshCom (for example 5 % per tracker) | bounds the worst case whichever QRG is used                     | per-source airtime state in relays; tuning                                                           |
| K4 compact track frames (delta position, no path, short trailer)      | 30 to 40 B instead of 80 B, about half the airtime (INFERRED)   | new frame type; app and server must decode                                                           |
| K5 text with line separators as one MeshCom frame, length capped      | cheapest per line; no QRG switch                                | one loss loses all lines; display support needed                                                     |
| K6 a second radio for APRS (dual-radio boards)                        | no deafness, both QRGs live                                     | hardware                                                                                             |

Assessment (proposal, not decided): if the goal is "APRS content without leaving the
MeshCom QRG", K2 plus K4 keep the RF cost low and let gateways feed APRS-IS; K1 with K3
if a live RF track is wanted. Measure the
deafness of K0 first (missed relays during switches), since it is the cost K1 to K5 are
compared against.

### 4.5 Multi-SF: more capacity, compatible like Wi-Fi rate adaptation

Idea (operator): a faster mode that does not make the slowest node decide; links upgrade
where they can, everything stays compatible.

The Wi-Fi pattern: beacons and broadcast at the base rate, unicast per link at the
fastest rate the link margin allows, fallback on missed ACKs. LoRaWAN ADR does the same
centrally (best SNR of the last 20 uplinks minus the required SNR minus a safety margin,
<https://www.thethingsnetwork.org/docs/lorawan/adaptive-data-rate/>). Meshtastic has no
per-link adaptation; all nodes share one preset (<https://meshtastic.org/docs/overview/radio-settings/>).

The constraint: a single SX126x/SX127x receives one SF at a time. A node at SF9 is deaf
to SF11 floods while it listens at SF9.

| Option                                                                                                                                                     | Pro                                                                                                                                                                                     | Contra                                                                                                                                       |
| ---------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| M0 status quo, one SF for all                                                                                                                              | simple, everyone hears everything                                                                                                                                                       | capacity fixed at SF11                                                                                                                       |
| M1 per-link upgrade for unicast with rendezvous                                                                                                            | base SF for floods, HEY, positions; a DM between two neighbours with margin uses SF9 or SF10 after an SF11 "switch" handshake; both listen at the fast SF for one exchange, then return | both nodes are deaf to the base channel during the exchange (missed floods); needs a handshake frame and timing; only for DM, not for floods |
| M2 CAD scanning: listen on SF11, CAD for SF9 between; or the reverse: listen on SF9 and CAD for the long SF11 preamble (32 symbols, 262 ms, easy to catch) | a node can detect the other rate's preamble; the reverse form is the easier one                                                                                                         | CAD per SF is sequential; short preambles may be missed; more RX power; PER on the listened channel rises                                    |
| M3 second channel for a fast backbone (different frequency, SF9)                                                                                           | no deafness on the base channel; hilltop and gateway nodes form a fast layer                                                                                                            | needs a second radio (dual-radio boards are rare) or a dedicated node per site; frequency plan (4.14)                                        |
| M4 regional SF choice (SF10 in dense city cells, SF11/12 in the country)                                                                                   | city capacity x1.8 at 2.5 dB less margin, where margins are large                                                                                                                       | cells must agree; a node at the border between SF regions cannot hear both; bridges needed                                                   |
| M5 concentrator gateways (SX1302/1303)                                                                                                                     | a gateway hears 8 to 16 packets on several SFs at once                                                                                                                                  | gateway hardware only; nodes still send on one SF                                                                                            |

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
first steps; M1 is elegant but the deafness problem makes it a research item. The track
mode's per-packet switch (2.1) is a working rendezvous today: measuring its deafness is
the cheapest first experiment for M1.

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

| Option                                                  | Discovery                                                      | Routing                                                                                                                           | Pro                                                                                                                                                                      | Contra                                                                                                                                                 |
| ------------------------------------------------------- | -------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| S0 status quo                                           | none                                                           | flood                                                                                                                             | robust, stateless                                                                                                                                                        | N copies per DM and per ACK                                                                                                                            |
| S1 next-hop learning (Meshtastic 2.6 style)             | the first DM floods; the reply shows which relay carried it    | each node remembers "for destination X, relay Y worked"; a DM is relayed only by the remembered next hop, flood on the last retry | backward compatible; state per destination, not per path; proven elsewhere (<https://meshtastic.org/blog/meshtastic-2-6-preview/>); a via field exists (`VIA,DEST`, 2.3) | the via field acts for one hop today (each relay resets it); old relays without a matching via drop a frame addressed via someone else (`checkMesh()`) |
| S2 full source route (MeshCore style)                   | flood with path record (exists), the ACK carries the path back | the DM lists the relays; only listed relays forward; flood again on missing ACK (<https://wiki.eastmesh.au/meshcore/routing/>)    | fewest copies; deterministic                                                                                                                                             | header grows per hop (about 10 B per call); old firmware ignores the list and floods anyway                                                            |
| S3 hybrid: S2 with a hop-limited flood around the route | as S2                                                          | listed relays plus their direct neighbours may forward                                                                            | survives one moved node                                                                                                                                                  | more copies than S2                                                                                                                                    |

Where to put the route: a payload prefix (`{R:CALL1,CALL2}`) is readable and old nodes
treat it as text; a new message type hex ID is cleaner but invisible to old firmware.
Interop: as long as old firmware floods, the copies do not drop until most relays run the
new firmware; the saving grows with the fleet share.

Assessment: S1 first (smallest change; it can build on the existing via field if relays
rewrite the via to their own learnt next hop instead of resetting it), S2 later for
store-and-forward nodes and fixed sites. Check how old relays treat a via they are not
named in before choosing the field.

### 4.8 EMCOMM priority flag: where to put it

Byte 5 has no free bit. Options:

| Place                                             | Pro                                               | Contra                                               |
| ------------------------------------------------- | ------------------------------------------------- | ---------------------------------------------------- |
| E1 payload prefix (`{EMC}` or `#EMCOMM` tag)      | old firmware shows it as text; no protocol change | trivial to fake; parse cost                          |
| E2 new message type (hex ID)                      | unambiguous; room for a signature field           | invisible to old firmware (not shown, not relayed)   |
| E3 trailer extension (byte after the sub-version) | out of the text; old decoders ignore extra bytes  | every relay re-encodes and drops it; outside the FCS |
| E4 destination convention (`>EMCOMM`, group)      | uses the existing group mechanism                 | no priority semantics in old firmware; fakeable      |

Any of these is fakeable without authentication. Assessment: E1 or E4 for visibility in
the old fleet, and priority only when the frame also carries a valid signature (4.9);
an unsigned EMCOMM frame is shown, relayed normally, not prioritised.

### 4.9 Sender authentication for EMCOMM

RM1's HMAC authenticates a command to one node with that node's password. For EMCOMM many
receivers must verify one sender. Sizes on air: the payload is filtered text (2.3), so a
tag or signature travels as text: RM1 tag 16 hex characters (about +147 ms at SF11),
Ed25519 64 B as Base64 88 characters (about +786 ms), as hex 128 (about +1130 ms); all
COMPUTED. Raw binary would need a new frame type. Legal frame: authentication without encryption keeps
content readable and is argued to be allowed (<https://rietta.com/blog/authentication-without-encryption-for/>);
no ruling for DL/OE found (NO SOURCE): **regulatory check needed (DARC/BNetzA, ÖVSV)**.

| Option                                                    | Size per frame                 | Trust                                    | Pro                                                            | Contra                                                                                      |
| --------------------------------------------------------- | ------------------------------ | ---------------------------------------- | -------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| A1 RM1 HMAC with a shared EMCOMM net key                  | 16 characters (+147 ms)        | everyone with the key                    | code exists; small                                             | anyone with the key can sign as anyone; one leak breaks all; key distribution out of band   |
| A2 per-sender HMAC keys in a receiver trust store         | 16 characters                  | receiver holds each sender's secret      | small frames                                                   | every receiver holds every secret; a compromised receiver can forge; does not scale         |
| A3 Ed25519 signature, public keys pinned in a trust store | 88 characters Base64 (+786 ms) | pinned public keys (32 B each)           | forging needs the private key; receivers hold only public keys | 64 B per frame; verify cost on ESP32 and nRF52840 to measure; key management in the web GUI |
| A4 A3 with keys from a server directory                   | 88 characters                  | the directory's signing key, pinned once | onboarding without manual pinning; revocation                  | needs internet once; who runs the directory (DARC? ÖVSV? icssw?)                            |
| A5 key announce on air (new hex ID), TOFU plus manual pin | 88 characters + announce       | first contact, then pinned               | works offline                                                  | first contact can be spoofed; announces cost airtime                                        |

Trust store (operator): per entry callsign (10 B) + public key (32 B) + flags (1 B), about
43 B; 100 entries 4.3 kB, fits flash (LittleFS), RAM only for a cache. "Who do you trust":
a pinned hash per callsign in the web GUI (show the key fingerprint, operator confirms),
optionally a pinned directory key for A4. The web GUI checkmark "sign EMCOMM messages"
sets the flag per message or per session.

Assessment: A3 with manual pinning plus A4 as optional directory is the scalable design;
A1 is a quick bench prototype with known weaknesses. About 0.8 s extra per frame
restricts signatures to EMCOMM traffic, not every message.

### 4.10 End of life for old firmware

A filter exists: frames **originated** by firmware below 4.35 are dropped at decode
(`src/aprs_functions.cpp:533-537`). The trailer carries the originator's version; a
relay rewrites only the hardware byte (2.3), so no last-hop firmware is on air. The
neighbour matrix learns a call's firmware from frames that call originated
(`nbrNoteFw`, `src/lora_functions.cpp:1233-1235`).

| Option                                         | Pro                                                  | Contra                                                                                                               |
| ---------------------------------------------- | ---------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| L0 status quo (drop originators < 4.35)        | exists                                               | 4.35x wrapping relays still relay; trailerless frames pass unchecked                                                 |
| L1 do not relay frames whose last hop runs < N | targets the masking bug's carriers (wrapping relays) | the last hop's firmware must be looked up in the matrix by its call; unknown when the relay never originated a frame |
| L2 do not relay frames originated by < N       | stops old originators' load; the field exists today  | users who did not update lose reach without notice                                                                   |
| L3 drop entirely (as today for < 4.35)         | simplest                                             | splits the mesh                                                                                                      |
| L4 server-announced minimum version            | one switch for all; can be raised step by step       | needs the directory (4.1) or a `{SET}`-like frame; unauthenticated today                                             |

Assessment: L1 via the matrix lookup for the wrapping builds, together with the hop cap
(4.6) which stops their wrapped frames anyway; a notice DM ("please update") and a
published date before any block. L4 once a signed directory exists.

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
cell border. Three colours suffice for adjacent hexagonal cells to differ; classic
cellular reuse patterns use clusters of N = 3, 4 or 7 cells (textbook cellular
planning; no LoRa-specific study found). Channels: at 70 cm in practice 433.175 (MeshCom),
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
The case A/B logic is a reactive form of this (decide per frame on an overheard copy);
an MPR set is proactive (decide per topology change) and deterministic. Pro: the
structural fix for "many neighbours, little coverage", and the decision no longer
depends on which copy a node happens to overhear. Contra: the two-hop data is thin today:
HN reports list at most 8 links, and under `auto` relays and gateways send none (2.3);
the matrix has the classic row limit (Part A 10.7); a stale MPR set needs a flood
fallback. MPR would need relays to report their neighbours, which costs HN airtime.

### 4.15 Further candidates

- Preamble 32 to 8 for the EU default (2.4): 197 ms per frame, no protocol change; needs a
  compatibility test across the fleet (mixed preamble settings).
- Adaptive TX power for city nodes (less power, smaller interference footprint, same
  coverage when neighbours are close). Bench rule stays 2 dBm.
- Role flags announced in HEY (gateway, high site, mobile, client-only) as in Meshtastic's
  ROUTER/CLIENT_MUTE roles; a client-only node never relays.
- Finding 3 from the QRT verdict (gateway cancels already-heard server frames).
- Airtime accounting per source in the neighbour matrix row (basis for K3 and P-A3).

## 5. Priority matrices per message type

Today (code, Part A 3.1) and two discussion variants. Lower is earlier. CSMA base in
brackets (ms).

| Frame kind                                                       | Today own                        | Today relayed | Variant V1 (PM first) | Variant V2 (EMCOMM + PM)          |
| ---------------------------------------------------------------- | -------------------------------- | ------------- | --------------------- | --------------------------------- |
| EMCOMM, signed                                                   | -                                | -             | -                     | 0 own and relayed (new band)      |
| Binary ACK 0x41                                                  | 1 (3000)                         | 1 (3000)      | 1                     | 1                                 |
| DM ACK `:ackNNN`                                                 | 1 (3000)                         | 3 (4500)      | 1                     | 1                                 |
| DM                                                               | 1 (3000)                         | 3 (4500)      | 1 own, 2 relayed      | 1 own, 2 relayed, rate-limited    |
| Group/broadcast text                                             | 2 (3000)                         | 3 (4500)      | 2 own, 3 relayed      | 2 own, 3 relayed                  |
| Server injection (`{CET}`, `{SET}`, foreign text) at the gateway | 3 (`DONE`)                       | 3 (4500)      | 3                     | 3, `{CET}` with TTL (Part A 10.8) |
| Locally typed `{SET}`, `{ping}`                                  | 1 to a call, 2 to a group (once) | 3             | as today              | as today                          |
| EMCOMM, unsigned                                                 | -                                | -             | -                     | as text, shown with a warning     |
| Position                                                         | 4 (5500)                         | 4 (5500)      | 4                     | 4, track budget (4.4 K3)          |
| HEY (`H`, `HG`)                                                  | 5 (5500)                         | 5 (5500)      | 5                     | 5                                 |

Relayed CSMA in practice: with `--nbrrelay` forced on, relay slots of case A start at
3500 ms and drop to 150 ms after 8 s; case B positions and HEY take a one-time 20 s hold,
then slots 7 to 9 and a short search after 60 s; case B text keeps the normal base
(`src/configuration_global.h:548-554`, `src/txring_functions.cpp:331-380`). On top, the
age floor (section 3) applies to priorities 1 to 3.

The presentation shows these matrices interactively per scenario.

## 6. Roadmap and wave plan

Same loop as the v4.40a campaign: plan, implement, test (host gate, golden sets), bench,
field capture, evaluate, learn, adjust. Each wave ends with an upstream PR cut from
`upstream/dev` (German), as in `CLAUDE.md`.

Estimates are calendar time for one operator with agent support, based on the 4.40a and
QRT campaigns (QRT: 15 to 25 min per brief, about 60 min gate and bench per wave,
`docs/campaign-lora-qrt-20261018.md`), plus the field nights the QRT campaign skipped.
They are estimates, not commitments. The QRT pace (briefs of 15 to 25 min) is a weak
basis on its own: it produced the untested release criticised in section 3, so the
estimates below add field nights explicitly. W6 (regulatory check) and W8 (other operators
changing SF) depend on outside parties, and every wave waits for upstream review; neither
is in the estimates.

| Wave | Content                                                                                                                                                                                                          | Gate and golden sets                                                                                                                        | Field proof                                                                                       | Estimate     |
| ---- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------- | ------------ |
| W0   | Measurement: Part A stage 0 plus section 3 metrics (false cover via `removed_inferred`, stage-2 priority inversion, gateway echo release), hop/`{SET}`/`{CET}`/HG/DM-band counters, K0 deafness, PER per SNR bin | host gate; new golden: hop-nibble corpus, CET-age replay                                                                                    | DK5EN-98 logger data since 10-18 (explicit OK), observer nodes outside a cluster, one bench night | 1 to 2 weeks |
| W1   | TX buffer: TTL per priority, slot pool, supersede (Part A stages 1, 2, 4)                                                                                                                                        | `test_txring`, `test_txring_flood`, `test_txprio_replay`, `test_nbr_replay`                                                                 | DK5EN-1/-92/-90 gwflood replay, 1 night                                                           | 2 weeks      |
| W2   | Buffer parity and consolidation: time-aged dedup, neighbour coverage window, buffer inventory, byte-pool study                                                                                                   | `test_dedup_replay` with clock (8813 verdicts, two overflows become DUP), `test_topo_shadow`, RAM baseline (`tools/resource_baseline.json`) | DK5EN-98 soak: no `RX_DEDUP_NEW` for ids older than 39 min                                        | 2 weeks      |
| W3   | Hop clamp cleanup: nibble cap on relay (H1), gwcap review                                                                                                                                                        | new nibble corpus with wrapped frames; `test_aprs_corpus`                                                                                   | count of nibble > L before/after                                                                  | 3 to 5 days  |
| W4   | PM priority, reliability, speed: ACK first, DM band 2 with per-source limit, DM retries in QRT after the pool; S1 next-hop study                                                                                 | `test_ack_replay`, new DM-latency replay (golden: DG0OPK DM round trips, 1 min quiet / 4 min loaded)                                        | DM round-trip median and p95 on the bench fleet, group `TEST` and own nodes only                  | 2 weeks      |
| W5   | End of life: relay filter for wrapping builds, notice DM, published date                                                                                                                                         | trailer corpus per firmware version                                                                                                         | count of blocked frames, no loss of current-firmware frames                                       | 1 week       |
| W6   | EMCOMM: priority flag (E1/E4), Ed25519 signatures (A3), trust store with pinning in the web GUI; regulatory check first                                                                                          | signature vectors (golden), trust-store persistence lint                                                                                    | bench: signed frame verified on all four boards, unsigned shown with warning                      | 3 to 4 weeks |
| W7   | Tag groups: T2 display filter, then T1 with app and server                                                                                                                                                       | settings schema lint, group corpus                                                                                                          | bench and one user group                                                                          | 1 to 2 weeks |
| W8   | Multi-SF: regional SF (M4) and backbone layer (M3) as experiments; PER per SNR bin first                                                                                                                         | PER/SNR measurement set (golden for the threshold)                                                                                          | attenuator bench, then one city cell                                                              | 4 to 6 weeks |
| W9+  | Speculative: source routing S2, link stretches, cell/role colouring, MPR election (4.14), adaptive power                                                                                                         | per item                                                                                                                                    | per item                                                                                          | open         |

Dependencies: W0 before everything that changes relay behaviour; W1 before W4 (retries
need the pool); W3 before W5 (the hop cap already neutralises wrapped frames); W6 before
any absolute priority for foreign traffic; W8 after W0's PER data.

Recurring verifications (already in use): host gate `tools/regression.sh --stage 1,2`,
golden selftest (48), dedup replay, nbr replay, gwflood replay, APRS corpus and fuzz,
bench suite stage 3, DK5EN-98 soaks. Each wave adds its golden set to that list.

## 7. Open questions for the operator

1. Multi-line POS (4.4.1): X2 (gateway or server conversion, no RF cost) or X3 (one APRS
   WX frame instead of the position); who implements the conversion if it is the server.
2. Whether forced `--nbrrelay on` should become RM1-switchable (section 3).
3. EMCOMM trust root: manual pinning only, or a directory, and who runs it (4.9).
4. EOL threshold and date (4.10).
5. Regulatory check of signatures on 70 cm in DL and OE (4.9).
6. Whether W0 may run on DK5EN-98 (production gateway).
