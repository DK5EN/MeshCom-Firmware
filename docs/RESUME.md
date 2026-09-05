# RESUME — pick up here

## 2026-09-05 (evening): v4.35s.09.05 published, 39 assets, tag a60905d6

Release object at <https://github.com/DK5EN/MeshCom-Firmware/releases/tag/v4.35s.09.05>, marked
latest, 39 assets diff-identical in name to 09.03. **Tag scheme changed: no `-stability` suffix
from this tag on** (the release-firmware skill and the changelog say so). Shipped: changelog items
179-191 -- BP-11 echo guard (publication hold lifted by the operator on 2026-09-05, still no
upstream PR requested), RAK4631 TX power after flash reset (#1132, bench DK5EN-90), T-Deck TD-12/
TD-13/TD-14 and CDC-01 (bench DK5EN-14), TM-49 fail-closed safeboot gate, TLM-04, ADC-01, WQ-01
queue panel, web messages page (ring scan, browser history, group tabs), MEM-03 tool. Gates: 591
native cases / 12 envs, 32 envs built, tracked safeboot bins unchanged by the build.
`FLASH_VERSION 20260905`, struct version untouched. The 09.03 release stays published; nothing was
deleted. Deliberately still open and stated in the notes: BP-11 ring-flood bench case, TM-49 bench
arm on a 4 MB board, web GUI changes without a measured bench arm, TD-15, MEM-04 risk, GPS-07 and
the carry-overs from 09.03. Bench fleet and gateway not yet flashed with the release build.

## 2026-09-05: TLM-04 fixed, ADC pin-99 guard

Both on `fork-main`, pushed. TLM-04: the Extern-UDP `lora` `tele` datagram now puts the `/P=`
station pressure under `qfe` and the `/F=` pressure altitude under a new key `pressure_alt`;
builder extracted to `src/extern_tele_json.h`, native test `test_extern_tele_json` registered in
`[env:native]`, doc note `ext_udp_telemetry.md` §6, write-up §8. Ruled a bug fix, not a contract
change. ADC (`a7208537` + feedback wave, see ADC-01 in BACKLOG): `loop_ADCFunctions()` skips
sampling while `node_analog_pin` is the unset default 99, and `initAnalogPin()`, `--info` and the
WebGUI status now say so instead of silently substituting the board default. **Attribution
corrected the same day:** the DG2NPE-5 field case (a 4.35e E22 broadcasting
`__analogReadRaw(): Pin 99 is not ADC pin!` lines as messages via a serial loopback) is NOT
explained by this -- on 4.35e `initAnalogPin()` already replaced 99 by GPIO 32 at boot, the core
emits that line from any `analogRead(99)`, and current E22 variants have no `ANALOG_PIN` at all
(upstream dropped it in 4.35p). The message source on that node is unresolved; the loopback
mechanism (parser needs `::`, own echo recirculates the flood) stands. Open questions to the
operator: `--analogset` APN value, what hangs on the serial port, does `--analog check off` stop
it. Neither fix is upstream yet: two PR candidates against DEV.

## 2026-09-04 morning: TLM-04 filed (superseded, fixed 2026-09-05) — Extern-UDP `tele` `qfe` is an altitude (BLOCKED, Low)

Operator report: a dashboard fed by a gateway's Extern-UDP shows "190 hPa" for `DM3KS-13`
(BME680). RCA by code reading, no fix: the relayed-node `tele` datagram copies the APRS `/F=`
field, which carries `node_press_alt` in metres; the real `/P=` pressure is parsed but never
emitted. Upstream-identical, MCProxy already works around it. Write-up
`docs/bug-extudp-tele-qfe-20260904.md`, BACKLOG §3.8y. **Blocked on the operator's decision**
whether a one-line firmware fix (contract change for `src_type:"lora"`) goes upstream or the
consumer side carries it. Nothing to do until then.

## 2026-09-04 morning: BP-11 echo guard — implemented, bench-proven, NOT published

The node refuses to re-transmit its own back-pressure wording (`bpIsOwnWording()` in
`src/backpressure.h`, wired into `sendMessage()` after the `{ZIEL}` parse; strict block, no
receipt for an echo). Field case IZ5CND-1/-10, still looping during the bench. Plan, decision
(Option A over strip-and-resend) and evidence: `docs/node-msg.md`; BACKLOG row `BP-11`;
changelog item 179 in its own "Unreleased" section.

**State:** commit `b3ecaa68` + this docs commit on `fork-main`, **ahead of origin, unpushed**.
Operator decision the same morning: no upstream PR, no push, no release — the PR worktree was
removed unpushed. A `/release-firmware` or `/submit-pr` run must stop before carrying it.
Installed on `DK5EN-98` (OTA) and `DK5EN-14` (USB), both build Sep 4.

**Still owed:** bench case 5 on `DK5EN-14` (flood the ring into the QRT band, feed the nack
back over the same transport, expect one `[BP];echo;` and no second `[BP];nack;`); identify
the re-injecting client on IZ5CND's side (node-to-node EXTUDP bridge is the first hypothesis);
decide whether the commit moves to a private branch so `fork-main` stays pushable.

## 2026-09-03 evening: Backlog grooming — twelve items closed, TM-49 fixed

One pass over the open rows from an operator list. Detail and evidence per item:
BACKLOG §3.8x.

**Closed:** `TD-04` (Europe map installed and rendering), `TD-05` (partial
refresh shipped and working), `GPS-01..04` + `GPS-06` (the 8 h overnight
`DK5EN-14` capture — 9791 evaluations, 0 rejects, 0 corrupt samples — is the
bench proof those rows were waiting for; the planned TRACK-mode A/B/C arms are
superseded), `TM-35` (gate met since the async-NTP build, field-proven since),
`TM-39` (both leftovers resolved by `b624bd33` and `CTY-01`/`a1191eaa`),
`TM-44` (accepted, no fix), `CQ-01` (BLE PIN on the console is intended, risk
accepted), `TOOL-06` (re-verified on disk, nothing to change), and decision
`G12` (all at once).

**Fixed:** `TM-49` — the OTA completion path is now fail-closed, which is what
the operator proposed: `ElegantOTAClass::_ota_image_valid` is false from
`/ota/start` and set true in exactly one place, after `Update.end(true)`
(length + client MD5) _and_ `Update.isFinished()`. Both completion handlers
gate the reboot, the partition switch and the HTTP 200 on it, and abort with
`incomplete_upload` otherwise. `abortActiveUpdate()` clears it only while
`Update.isRunning()`, so a late AsyncTCP disconnect cannot retract an earned
verdict. Builds green (`esp32-safeboot`, `esp32-S3-safeboot`), native 568/568.

**Still owed for TM-49:** the bench arm on a **4 MB** board (Heltec or T-Beam,
not the 16 MB T-Deck whose bootloader masks the defect) — kill an upload
mid-transfer, assert HTTP 400, `abort;reason;incomplete_upload`, no
`verify;result;ok`, `end;result;error`, recovery via the 180 s fallback.
Procedure written up in `docs/bench-ota-regression.md`.

**Two stale statuses corrected on a read-through:** `TD-01` (WLAN association) had been sitting on
`open` in §3.8b since 2026-08-30, when TM-34 Wave W closed it with 24/24 first joins — the row was
stale, not the work. `TD-06` (automated test harness) sat on `in progress` while
`tools/bench/tdeck_harness.py` (20 scenarios) and its sibling rigs were in daily use and HL-01
(`--wifi on/off`) had closed the last gap; net-console observation is `meshlogger.py`, separate by
design. The CI limit from §3.8e still stands and is tracked elsewhere: both fork workflows are
disabled, so every green claim is a manual run.

**Still open, corrected:** `MH-01` is **not** fixed — `lora_functions.cpp:705`
still stamps `| 0xF0` over the country nibble. While checking it, a duplicate
backlog ID surfaced: `MH-02` named two unrelated defects. The one that shipped
in `a1191eaa` (dead MHeard eviction, §3.8p) keeps the ID; §3.8d's
`decodeAPRS` defaulting defect is renamed **`MH-03`** and is also still open.

**Memory guard was blind — fixed (`MEM-03`), and it is why we missed the PR #1114 break.**
`resource_watch.py` watched `dram0_0_seg` only, and its baseline stored PlatformIO's `RAM:`/`Flash:`
summary, which on a PSRAM board measures against the PSRAM-inclusive total. So `ttgo_tbeam` sitting
at **99.98 % of `iram0_0_seg`** printed `RAM 8.7 %` and sailed through the 4 kB DRAM gate with
10 kB to spare. Counter-proof on the same map: the old guard emits `::notice` and exits 0; the new
one emits `::error ... iram0_0_seg 131052/131072, headroom 20 bytes` and exits 1 under `--strict`.
The tool now parses every region the linker can overflow (`REGIONS`: `dram0_0_seg` via `_bss_end`,
`iram0_0_seg` via `_iram_end`), names the right consequence per region, records both in the
baseline, and carries the two real maps as self-test fixtures — the `ttgo_tbeam` shape asserts
_passes a DRAM-only gate, fails on IRAM_. `regions` is the new preferred subcommand; `dram` still
works. Baseline refreshed for seven ESP32 envs; a full 32-env re-baseline is not claimed.

**`MEM-04` filed, open — four envs are one small feature from a link failure** (clean tree,
2026-09-03): `ttgo_tbeam` / `_SX1262` / `_SX1268` at **20 bytes** of IRAM free, `E22_XML-DevKitC`
at **920 bytes** of DRAM (and 3 456 B IRAM). For contrast the two envs our PR review _did_ build —
`ttgo-lora32-v21` and `E22-DevKitC` — have ~11 kB DRAM and ~5 kB IRAM: they are the boards `MEM-01`
relieved, not the tight ones. Pre-existing upstream state, no headroom campaign proposed; the point
is that the gate now says so before a PR goes out.

**Upstream:** PR #1114 (KISS/TCP interface) was merged (`9d885b1a`) and
reverted (`674413ce`, PR #1128) on 2026-09-03 — **on a build break, not a design objection**: CI
job `100674766799` on the merge commit failed `ttgo_tbeam`/`_SX1262`/`_SX1268` with
`IRAM0 overflowed by 104 bytes` and `E22_XML-DevKitC` with `DRAM0 overflowed by 304 bytes`. `upstream/dev` does not carry
it and neither do we. Our review had all 15 findings fixed and re-verified by
the author and recorded the semantic conflict with BP-09's `sendMessage()` return type as the
blocker — but its build table covered four envs and reported "builds green" without ever touching
three of the four that failed. **No repairs are carried for it.** That PR is also
what answered `APRS-01`: it settled protocol choice (KISS-over-TCP + AX.25 UI),
where the serialiser lives, per-env cost and which platforms cannot carry it.
The one question it never touched is the one `APRS-01` said to ask first —
node-side or a bridge in MCProxy — and after the revert that is the only live
option. aprs.fi's app stays out of reach either way: it is an APRS-IS viewer,
not a KISS client.

## 2026-09-03: Release v4.35s.09.03-stability published, fork branch renamed to `fork-main`

`v4.35s.09.03-stability` is live on GitHub (39 assets, `--latest`), tagged at
`74810123` on `fork-main`, `FLASH_VERSION 20260903`. Gates: 32 envs clean,
native 568/568 in 12 host envs, 250 tool tests. `v4.35p.09.02-stability` was
kept -- this is a new release, not a replacement.

**It is a maintenance cut.** The whole firmware delta is upstream's own 4.35s
(`c908a4dd`, #1126): the `--postime` `else` branch removed, and the version
letter. No hardware ran against this tree, and both release documents say so;
the 09.02 bench and field results carry over because the source outside those
two hunks is unchanged. Changelog items 177/178.

The fork carries firmware **4.35s**. `upstream/dev` `4e649eae` merged cleanly
(`c908a4dd`: `SOURCE_VERSION_SUB`/`_WEB_SUB` `p` -> `s`, and the `--postime`
`else node_postime = 0` branch removed, which had reset every value >= 300 s
back to the default). 0 behind upstream. Gates on the merge: native 230/230,
`wiscore_rak4631` builds (RAM 35.8 %, Flash 80.4 %).

The branch `v4.35p_prio` was named after the upstream version letter and went
stale on every upstream cut, so it is retired: renamed to the version-neutral
`fork-main`, the fork's GitHub default branch moved with it, the old remote
branch deleted behind tag `archive/v4.35p_prio-20260903` (last 4.35p state
`1d0bd23a`). Updated to match: `.claude/commands/{rebase-upstream,submit-pr,
code-audit,ram-snapshot}.md`, the release tag scheme in
`release-firmware.md` (now `v<VER>.MM.DD-stability`, `<VER>` read from
`configuration_global.h` instead of hard-coded `4.35p`), and BACKLOG §4.1 plus
the new §4.2 table. Historic reports keep the old branch name on purpose.

Inherited from upstream, not fixed here: `--postime 0` no longer disables
position beacons (0 clamps to 300 s). Needs its own upstream PR if wanted.

## 2026-09-02 night: Release v4.35p.09.02-stability published — GPS/QNH path, --setlog lines, T-Deck key repeat

`v4.35p.09.02-stability` is live on GitHub (39 assets, `--latest`), tagged at
`53ecc696` on `v4.35p_prio`, `FLASH_VERSION 20260902`. Three worktree branches
were merged in order setlog (`c98f1abc`) → GPS (`de522912`) → keyboard
(`bda28bae`) after an upstream `dev` sync (`b4f3b400`, #1124); changelog items
173/174/175 renumbered at merge, 176 is the upstream sync. Gates: 32 envs
clean, native 568/568 in 12 host envs, tool tests 102. The four worktrees
(`mc-gps`, `mc-keyrepeat`, `mc-setlog`, `mc-helmut`) and their branches are
deleted; `mc-pr1114-kiss` stays.

Deliberately still open (all in release-notes.md "Known gaps"):

1. **`--setlog` bench** (plan Welle 3: RAK-90 + Heltec-93, 30 min) — no
   hardware run yet.
2. **GPS two-hour arms A/B/C** on DK5EN-14 — only 6.5 min bench + two
   one-hour OE5HWN field logs exist (`gps-nmea-impl-plan-20260902.md` §6).
3. **GPS-06 T-Deck pin fallback** unverified on hardware; OE5HWN has the
   build (`firmware_t_deck_gps-kbd-test_20260902c.bin`, pre-merge tree).
4. **PR cuts to upstream**: `docs/pr-gps-draft-20260902.md`,
   `docs/pr-tdeck-keyrepeat-draft-20260902.md`; setlog has no draft yet.
   Include the T-Deck pin note for Kurt ("Modul-TX an GPIO44").
5. `tools/resource_baseline.json` is stale (deltas of +4 kB on unchanged
   trees); refresh it from this release's build logs.
6. OE5HWN's T-Deck Plus keyboard controller predates raw mode — nothing to
   fix, `--info` now says `KBD raw-mode unknown` on such units.

## 2026-09-02: Code Quality 2.0 written, scanner extended, §3.8t filed — next is the four-wave audit (CQ-12)

`docs/code-quality-2.0.md` (`e108fa98`) is the field-derived layer on top of
`codequality-rules.md`: 28 code patterns C01..C28, 16 process patterns
P01..P16, Part D review checklist, Part E gap table. `tools/code_audit_scan.py`
carries the twelve grep-stable detectors as `CQ2-*`; `/code-audit` walks
Part D. Four sites the checks found were fixed in the same commit (TM-51
reset banner, `PositionToAPRS()` `strncat` bound, map-timer `millis()`,
build skill parallelism); gate was three boards clean + 489/489 native.

Backlog §3.8t (CQ-01..CQ-12) holds what the checks found and did not fix plus
the enablement work. Order for the next session:

1. **CQ-01** (`bt_code` in clear on serial and 2323) — one-line masking, do
   first.
2. **CQ-12** audit wave 1 (parsers and protocol) with `/code-audit --full
--delta`; write `docs/code-audit-20260902.md`, triage into §3.8u.
3. **CQ-06** native seam for `PositionToAPRS()` / `sendMessage()` before the
   next fix touches either file again.

Unreleased changelog items 170-172 are written; next release cut takes them
as they stand.

## 2026-09-01 night: Release v4.35p.09.01.2-stability published

`v4.35p.09.01.2-stability` is live on GitHub (39 assets, `--latest`), tagged at
`d0d33824` on `v4.35p_prio`, `FLASH_VERSION` unchanged at `20260901` (same-day
second cut, 08.27.2 precedent). The `v4.35p.09.01-stability` release object is
deleted (tag kept as marker); GitHub now carries 09.01.2 + 08.28. Gate:
**489/489 native test cases across all 12 host environments**; all 32 release
envs built, safeboot bins deterministic (no diff against the tracked files).

What shipped on top of 09.01 (changelog items 167-169):

- **QRS_MIN_USER_MSGS (item 167).** Operator field report: "QRS - slow down"
  far too sensitive even at buffer 5, fires on the first message. Root cause:
  `txRingDepth()` counts relay/ACK/beacon traffic, so a gateway idling at
  depth 4 warned the sender on their first own message. `onSend()` runs once
  per locally typed message, so the state machine counts its own calls: QRS
  needs three own messages in a row on a ring at/above `qrsThreshold()`; a
  sighting below the line in `onSend()` or `poll()` restarts the count. QRT,
  QTA, latch, hysteresis and QRV hold untouched. Eight lines in
  `src/backpressure.h` (`341a25bd`), four new host cases, T3 in
  `test_bp_regression` now expects QRS at depth 7 (`ab3c5b65`).
- **Wireless Stick V3 ADC_MULTIPLIER 4.9245 -> 4.13** (upstream #1119,
  OE3LCR, item 168) and the **upstream `dev` sync** with our PR #1118
  (esptool upload protocol, `-Wall -Wextra` out of the nRF52 core build) and
  OE1KBC's `v4.35p compile` (item 169).

Hardware for this cut: only dk5en-98 (live gateway, Heltec V3) runs the
build, OTA-flashed 21:52; the web GUI reports the new build string, nothing
more was checked. No burst was driven against the three-messages rule on
hardware. `tools/webflash.py` printed "node did not come back within 120s"
right after "node up" on the final poll -- cosmetic race in the tool, worth
a look.

Deliberately still open: TM-49, L5, DJ8MEH incident not re-provoked on
hardware, T-Deck partial-refresh targeted re-arm, `t5_epaper` not in the
release. Not part of this release and left untouched in the working tree: an
uncommitted edit to `docs/BACKLOG.md` and a new
`docs/bug-GPS-uart-overflow-20260901.md` that appeared during the release
run from outside this session.

## 2026-09-01 evening: Release v4.35p.09.01-stability published

`v4.35p.09.01-stability` is live on GitHub (39 assets, `--latest`), tagged at
`b3cc03d5` on `tdeck-partial-refresh-trace`, `FLASH_VERSION 20260901`. The
`v4.35p.08.31.4-stability` release object is deleted (tag kept as marker);
GitHub now carries 09.01 + 08.28. Gate: **485/485 native test cases across
all 12 host environments** — the "530 across 11" figure in the entry below
was wrong and is corrected in all release docs. Bench (after the entry
below was written): the BP-07..10 receipt build ran on all four bench boards
— T-Deck Plus DK5EN-14, Heltec V3 dk5en-98 (live gateway) and dk5en-93,
T-Beam dk5en-92, RAK4631 dk5en-90.

Two decisions shaped this cut (operator, 2026-09-01):

- **Final state only, no intermediate steps.** The changelog sections for
  08.31.2/.3/.4 plus the unreleased section were collapsed into one
  `## New in v4.35p.09.01-stability` section (items 153-166); the
  back-pressure system is described as it stands, BP-10's corrections folded
  in silently. release-notes.md carries no cut-by-cut history any more.
  release.md keeps its journal chronology by design.
- **No repairs for features upstream reverted.** Upstream reverted the
  extended Mheard JSON (PP #1105, SRC/GW #1106, serializeJson guard #1107)
  and removed the string `FWDATE` entirely (the `I` register carries `FWVER`
  alone — both trees). Our tree followed the reverts via the merge already;
  the one orphaned repair left, the PP/DIST fail-soft in `updateMheard()`
  (item 105), is removed as dead code (`c2adc18d`). Kept, because they
  repair live code: the HEY ingress bound (item 106), `bleJsonFrame`,
  NC-01/02. The `--mheard`-vs-live two-schema known gap is gone with the
  reverts and no longer listed.

Deliberately still open: TM-49 (safeboot OTA completion guard — 4-MB boards
flash via USB on marginal links), L5 (machine-readable back-pressure marker,
needs protocol change), the DJ8MEH incident not re-provoked end-to-end on
hardware, T-Deck partial-refresh targeted re-arm as follow-up, `t5_epaper`
not in the release (pre-existing include-path breakage).

## 2026-09-01 afternoon: BP-07/08/09/10 shipped, closes L1-L4

Four commits on `tdeck-partial-refresh-trace`, each advisor-gated (Fable),
implementing `docs/bp-l1-l4-impl-plan.md` (now archived, see below): a
refused or dropped message finally gets an app-visible receipt with its own
text (`QRT NOT SENT - <text>` / `QTA NOT SENT - <text>`), and the operator's
typed text survives a refusal on all three GUIs instead of vanishing. **BP-07**
(`77b43d4b`): root-caused L1 — `onRefuse()` was dead code, provably always
`BP_NOTICE_NONE`, because `refusing()` implies the latch already sits at QRT
or higher — and closed L2 (a dropped-after-accept message got no notice at
all) with a new, never-latched `BpNack` vocabulary; refuse-check moved past
`{ZIEL}` parsing so the receipt carries the real text, `bpPeekDst()` deleted.
**BP-08** (`e8b82a02`): closed L3 — the app echo used to fire before the ring
was even asked, so a dropped message looked sent; echo moved behind the ring
write, and (operator decision E4, bigger than L3) a dropped frame no longer
reaches a gateway's UDP uplink either — a message enters the network whole or
not at all. **BP-09** (`4f97f7f0`): closed L4 — `sendMessage()` returns
`BpSendResult` instead of `void`; T-Deck, T-Deck Pro and the web API (plus,
found beyond the plan, the web GUI's own JS) now keep the typed text on a
refusal instead of clearing it unconditionally. **BP-10** (`3aecc90f`): an
independent Fable advisor round over the whole `458af2b1..4f97f7f0` diff (7
finders) found and fixed 3 real regressions the three waves above had caused
or amplified — most seriously, BP-09's `BP_SEND_OK` gating had hidden the
BP-07 receipt entirely on both T-Decks (worse than before BP-07 shipped) —
plus 5 medium fixes; full detail in `docs/BACKLOG.md` (BP-07..BP-10 rows) and
`docs/CHANGELOG-stability.md` (items 163-166).

**L5 (a machine-readable marker instead of the text-prefix convention) is
explicitly out of scope** and stays open — it needs a protocol change and
app-side coordination (`docs/backpressure-protocol.md` chapter 5).

**Verified:** 530 native test cases across 11 host environments, and clean
builds of all six affected boards (heltec_v3, wiscore_rak4631, ttgo_tbeam,
t_deck, t_deck_plus, t_deck_pro).

**NOT verified — no bench run yet, and one gap no bench run can close:**

- `loop_functions.cpp`, where nearly all of this lives, is in no native
  `build_src_filter`. Findings H2/H3/M1/M4/M5/M7 from the BP-10 advisor round
  are therefore not natively testable at all; the planned bench run (DK5EN-93
  flood + neighbour LoRa-receive proof + DK5EN-14 input-field check) is the
  only end-to-end proof for the whole feature, and it has not run yet.
- **E4 (a dropped frame no longer reaches a gateway's UDP uplink) has no bench
  coverage, structurally.** Proving it needs an active gateway and an mcmap
  comparison, but the planned bench runs require Gateway OFF on every
  participating node (otherwise a message could reach neighbours over the
  central server instead of over LoRa, and the LoRa-receive proof would be
  worthless). E4 is covered only by code review and the native ring-write
  ordering test; a later gateway-on bench run stays open.

Archived: `docs/archive/bp-l1-l4-impl-plan-20260901.md` (the implementation
plan) and `docs/archive/bp-advisor-verdict-20260901.md` (the BP-10 advisor
verdict).

## 2026-09-01 morning: overnight soak 98/90/93 PASSED, documented

Three-channel overnight soak (22:00-07:58, build v4.35p.08.31.4) with a REAL
router reboot at 05:35 — full write-up appended to
`docs/wifi-soak-report-20260831.md` (Nachtrag). Headlines: 93 (WLAN, gapless
serial view) reconnected in 96 s onto a different BSSID/channel with NTP
resync 3 s after got_ip; RAK-90 (W5100S) 3 link flaps, DHCP renew with new
IP, 0 resets; gateway 98 hub-side 100 % uptime/0 events (96 s < the 3-min
silence threshold). Zero reboots/crashes/watchdogs on all three. BP: 0
`[BP]` markers (no user sends overnight — passive evidence only), depth
peaked at 10/20 without a false warning, and **BP-03 field-proven: 13x
RING_DROP_STALE at age_s 180-181** (noted on the BACKLOG BP-03 row). Two
tool findings: **TM-50 filed** (meshlogger zombie TCP after target WLAN
loss, 2.4-h gap, flags not restored — restored manually in the morning,
loradebug off everywhere again), and the CP2102 correction (port-open DOES
reboot the Heltec-93; the rst banner just precedes the reader attach —
`--no-dtr` does not prevent it). Still open: BP-05/06 McApp sight check
(burst -> QRS at depth 5 in the right chat), DJ8MEH burst re-provocation,
TM-49, TM-50.

## 2026-08-31 night, cut 4: BP-05/06 notice policy, release v4.35p.08.31.4-stability

The operator-triggered follow-up to the .3 fixes: the first real message
through the new build showed QRS+QRV on a single send. A 5.5-min meshlogger
capture on DK5EN-98 proved the gateway baseline sits at depth 1-4 (mode 2,
three false QRS/QRV pairs in normal traffic). **BP-05** (`ebceef90`): QRS at
depth >= 5, flat on every board (clamped below QRT), band 2-4 silent, QRV
only after QRT/QTA — baseline capture pinned as regression test. **BP-06**
(`687bb50b`): notices addressed to the triggering conversation (group chat /
DM thread, sender-only, never on air) via bp_origin_dst/bp_episode_dst and a
pure `bpPeekDst` for the parse-before-refuse path; advisor REWORK (3 edge
cases) incorporated. Both waves Fable-gated. **Release
[v4.35p.08.31.4-stability](https://github.com/DK5EN/MeshCom-Firmware/releases/tag/v4.35p.08.31.4-stability)
published** (items 161-162, 39 assets, gate 480/480 in 12 envs, 32/32 build
envs); **v4.35p.08.31.3-stability stays** (no delete order). Doc-count
lesson: the 477 in the first docs commit was stale (3 advisor tests landed
after that run) — count from the FINAL gate output, tag moved once before
publish. Fleet flash to .4 follows immediately after this entry. Still open:
re-measure DK5EN-98 baseline on .4 (expect zero [BP] markers), re-provoke
the DJ8MEH burst, TM-49.

## 2026-08-31 latest: release v4.35p.08.31.3-stability published, fleet on it

**Release [v4.35p.08.31.3-stability](https://github.com/DK5EN/MeshCom-Firmware/releases/tag/v4.35p.08.31.3-stability)
published** (third cut of the day, items 157-160: BP-02/03/04 +
test_bp_regression; 39 assets name-identical to the previous cuts, fresh
32-env rebuild — the earlier "reuse the gate build" shortcut failed its own
freshness check, 7 of 27 app images and both safeboot outputs had vanished
from `.pio`, so the skill's full-rebuild step stays mandatory). Release
object v4.35p.08.31.2-stability **deleted, tag kept** (same precedent as the
morning). Whole fleet (14/93/92/90 + gateway 98, OTA build stamp 19:58:58)
runs the BP-fix build. FLASH_VERSION stays 20260831; gate 459/459 in 12
envs, 32/32 release envs. Still open: re-provoke the DJ8MEH burst on
hardware, TM-49, McApp display confirmation of the .2 notice framing.

## 2026-08-31 late night: BP-02/03/04 shipped (backpressure RCA fixes)

The three DJ8MEH-RCA fixes, implemented as advisor-gated waves (plan + both
verdicts archived in `docs/archive/bp-rca-fixes-*.md`): **BP-02** (`e501e63c`)
`txRingDepth()` counts occupied slots, all report sites unified, `RING_STATUS`
carries `dist=`, both RING_ZOMBIE detectors ported (queued fallback for old
logs); **BP-03** (`180917a1`) 2-s main-loop sweep `txRingAgeBackground()` drops
BACKGROUND entries >180 s (`RING_DROP_STALE`), atomically under the nRF52 lock,
deliberately NOT in `getNextTxSlot()` (runs on the timer task — the plan
advisor's critical catch); **BP-04** (`fdda7f2a`) QRV closes in the water band
(depth 1) after a 10-s hold, depth 0 immediate, time injected, anti-flap
pinned. 11 new native cases, all fails-before-verified; every wave got a
fresh Fable advisor gate (wave 1: one real rework, RING_TX_READ still printed
the old distance). On top: **`test_bp_regression`** (native_aprs), the
integrated DJ8MEH end-to-end replay over the real ring + real state machine —
burst→QRT→drain-with-pinned-blocker→QRV in 10 s (field: 8 min), blocker
age-out path, and an over-correction guard (QRT still fires at 80 %); each of
the three fixes individually mutation-verified to turn the suite red. The
full BP regression family runs as two commands (documented in the suite
header; a single cross-env `pio test -f` call ERRORs on foreign suites
instead of skipping them). NOT yet done: bench replay of the DJ8MEH burst on
the new build, fleet not reflashed (still runs the 08.31.2 release build
without these fixes), next release must mention the `queued`/`dist`
log-semantics change and the stale-HEY on-air effect.

## 2026-08-31 night: BP-01 notice reframing, release v4.35p.08.31.2-stability

**DJ8MEH RCA** (console log, 3x `[BP];refuse` invisible to the sender): messages 1-7 went
out on UDP+HF, 8-10 were refused by BP-01 — correctly at first, but the QRT episode then
ran 8 minutes on **phantom depth**: `txRingDepth()` is `iWrite-iRead` and counts freed
holes behind a starved prio-5 entry pinning iRead (RING_STATUS showed queued=19 with 3-4
real entries); QRV only closes at depth 0. Three fixes filed, none shipped yet: count
occupied slots, age/promote starved background entries, QRV low-water band. **BP-01
notice reframing shipped instead** (items 153-156 + TM-46/47/48 = second release cut):
notices now arrive as a normal message from the node's own callsign — BLE/web frame via
`bp_notice_frame.h` (was pseudo-sender "response" -> McApp spam class 9999), EXTUDP as a
LoRa-shaped `type:"msg"` JSON via `extern_notice_json.h` (was `type:"notice"`, rendered
by nobody). Two new native suites pin both framings (fails-before verified); gate
445/445 in 12 envs. **Release
[v4.35p.08.31.2-stability](https://github.com/DK5EN/MeshCom-Firmware/releases/tag/v4.35p.08.31.2-stability)
published** (39 assets, same recipe as the morning cut; safeboot bins byte-identical to
the tracked ones — the 5286c294 bins were already built from the e5882a0f source state);
the morning release object v4.35p.08.31-stability is **deleted, its tag kept** as a
marker on a40b812e. Whole fleet (14/93/92/90/98) runs the BP-fix build from 8bdd5c23
(same source as the release, docs-only delta). Still unconfirmed: McApp display of the
new sender framing (the afternoon test ran against the old firmware). TM-49 remains open
and is now stated in the release's Known gaps.

## 2026-08-31 late evening: TM-46/47/48 fixed and bench-proven, TM-49 filed

The OTA tooling wave (orchestrate-waves, one safeboot writer + one tool writer, plus two
orchestrator follow-ups driven by bench evidence): **TM-47** webflash keeps the `+` in
hardware strings, expects `TDECK+`, safeboot-resume path, `--self-test`; **TM-48** the
safeboot WLAN join now uses the production TM-34 pattern (driver-picked AP, PMF-off,
`[SAFEBOOT];wifi;pmf_off;rc;0` proven, ~170 kB/s at the -74 dBm desk link); **TM-46**
aborted-upload recovery — central `abortActiveUpdate()`, onDisconnect hook with a session
generation counter, explicit `fallback_armed_at`, and the decisive bench find: a
cross-task unsigned-underflow race let the stall watchdog abort every healthy upload
(fixed with signed deltas + volatile). Abort-retry proven twice on DK5EN-14 (kill 5 s into
the upload -> immediate retry completes, no `--force`). **TM-49 filed (open, Medium)**: the
ElegantOTA completion handler can see `hasError==false` after a disconnect (final frame
never arrived) and switch boot partitions after a partial write — benign on the 16-MB
T-Deck (slot validation), dangerous on 4-MB single-slot boards; needs a guard + bench
proof before the next release. Safeboot serial on native-USB boards is visible only with
the opt-in `ARDUINO_USB_CDC_ON_BOOT` flag (commented in the safeboot env, documented).
Note: the fleet's OLD safeboots (93/92/98/90) predate these fixes — they update at the
next full-layout USB flash, not via OTA.

## 2026-08-31 evening: Release v4.35p.08.31-stability published, fleet flashed

**Release published**: [v4.35p.08.31-stability](https://github.com/DK5EN/MeshCom-Firmware/releases/tag/v4.35p.08.31-stability)
— 39 assets (list identical to 08.28), release-notes.md as body, CHANGELOG items 107-152
(condensed from and linked to `pr-draft-20260831.md`), FLASH_VERSION 20260831
(STRUCT unchanged). Full field instrumentation ships enabled; exception E22_XML
(`MC_CAPTURE=0`, `MC_INJECT_HOOKS=0` — the campaign had pushed that link 648 B over
`dram0_0_seg`). Five envs had been broken since 08.28 (never in a gate) and were fixed for
the release: E22_XML DRAM, both safeboot envs (`MC_SAFEBOOT` guard around the TM-32
sanitize path), t114/t_echo (`oledStat()` board guard). Gate: all 32 envs, 438/438 native.

**Whole fleet runs the release build** (webflash OTA: 93, 92, 98; USB: 14 after two OTA
failures, RAK-90 via DFU serial; each verified by build stamp). The T-Deck OTA failure is
root-caused and filed: **TM-46** (safeboot leaves stale session state after an aborted
upload -> HTTP 400 on retry; abort itself was correct — 2.17 MB image at the -74 dBm desk
margin) and **TM-47** (`webflash.py` expects `TDECK_PLUS`, firmware reports `TDECK` —
needless `--force`).

## 2026-08-31 afternoon: GW-01 + TM-06/07/14/19 campaign (both waves done)

Wave 1 (three parallel writers, gated 438/438 native + 8 targets): TM-06 (a) `--injectraw`
(real RX path via OnRxDone recursion) + (b) `--loratx` burst; TM-07 `--spitrace` per-flush
GPSPI2 register/user trace; TM-19 `--touch tap/down/up` through the LVGL indev path; harness
scenarios `gps_experiment`, `flush_lora_correlation`, `touch_inject`. GW-01 **measured**
(dual-node run + mcmap interlink): server stream has no HEY msg_id dedup, all copies arrive;
the only gateway-on difference is the bare `gw:1` self-upload arriving first — consumer-side
loss, fix (a) scheduled. Wave 2 done: **GW-01 fix (a)** shipped (`sendHey()` self-upload removed) and proven —
interlink shows only enriched `gw:0` records with gateway ON, parity with off. **TM-07
register named: GPSPI2.clock, exclusively** (pre-transfer `[SPITRACE];clobber` snapshot +
direct SD counter in tdeck_sdmap; LoRa changes nothing — the NOP can become a clock re-arm).
**TM-14 measured**: GPS is not the loop-tail culprit (102/119 ms vs 101/677 ms with the
outlier in the GPS-OFF window). **TM-19/TM-06 bench-proven** on DK5EN-14 (touch_inject PASS,
injectraw res;58 via fixture frame, full standard harness PASS; --injectraw lines > ~256
chars need paced serial writes). Bench restored (93/92 gateway+debug off, DK5EN-14/93 run
the wave-2 build, 92/90 unchanged). Details: BACKLOG §3.8f TM-06/07/14/19, §3.8i GW-01.

Last session: 2026-08-31, morning through afternoon. The full 3.8o/3.8p intake campaign was
implemented via `/orchestrate-waves` (Waves 1+2: WEB-01..04, NC-01/02, MH-02, TM-45, TM-39-CONF,
BAT-01/02, CHR-01/02, JSN-01, TD-07/08, ETH-01, CONF-01, CTY-01, PM-01, NTP-01, DOC-02 text,
PRES-01/02, DOC-01/03/04), gated (438 native cases / 12 envs, 7 standard targets build), bench-
proven where reachable ([NTP];ok rtt 37-89, NCNT 0->1 on DK5EN-90, "USB (no battery)" on
DK5EN-93/-14, web NTP/BSSID rows live), and pushed. The bench regression caught and fixed one
real gap (`--ntpsync` was a silent no-op with a GPS fix). **PR draft for the upstream
follow-up lives in `docs/pr-draft-20260831.md`** (firmware-only, full per-change granularity,
open cut questions in its Teil E). Last code commit `a3ae913f` (FLASH_VERSION comment),
everything pushed to `origin/tdeck-partial-refresh-trace`, working tree clean. All four bench
nodes run the final build.

## Pick up here — for the next agent

Read BACKLOG §0 (re-entry procedure) first; then, in the operator's priority order:

1. **Upstream-PR vorbereiten** — `docs/pr-draft-20260831.md` Teil E abarbeiten (Entwurf
   am 2026-08-31 abends um GW-01/4.9, C.3-Kommandos und das benannte TM-07-Register
   aktualisiert): (E1) das
   `full_refresh=0`-EXPERIMENT auf dem T-Deck zurückdrehen oder deklarieren — dank TM-07
   kann der Flushfix-NOP jetzt durch ein gezieltes `GPSPI2.clock`-Re-Arm ersetzt
   werden (Entscheidung offen), (E2) die 11
   nativen Test-Envs aus dem PR-Schnitt halten, (E3) die "nicht für Upstream"-Kopfkommentare
   in `instrument.*`/`test_inject.*`/`tdeck_debug.*` anpassen, (E5) Bedienungs-Anhang im Stil
   von `command-changes-pr1102-1103.md` für die neuen Kommandos. DK5EN merged nie selbst
   ([[upstream-no-self-merge]]).
2. **TD-09 Tile-Cache** (§3.8p follow-ups) — flüssiges Karten-Pan; ohne Cache 0,33-0,79 s
   pro Schritt. TD-07-Handtest auf DK5EN-14 steht ebenfalls aus (Skript im Wave-Report).
3. **E22-01** (§3.8p, High) — Frame-Integrität unter Versorgungsspitzen, Konzept für
   Operator-Review.
4. **TLM-03** (§3.8i) — Soft-Serial-Telemetrie-Review (blockiert TLM-01/02). GW-01 ist
   seit 2026-08-31 gefixt und belegt (fix a, siehe oben).
5. **MEM-02 risk assessment** (§3.8m, geparkt) und **UDP-01 reporter questions** (§3.8l) —
   unverändert aus der letzten Übergabe.
6. **TM-28** — E290 Wireless Paper Hardware kommt in der Woche ab 2026-09-01.
7. Kleinvieh, alles gefiled: `ntpsync.py`-Live-Lauf; PM-01 boot-gecachtes Global (optional);
   CONF-Koordinaten anwenden (eigenes Ticket); WEB-03 (c)-(e); WF-01 Sites 1+2; TM-44
   (deferred); CDC-01 GEFIXT (S3 HWCDC-Prints blockierten die Loop ohne USB-Host, RCA
   `tdeck-cursor-stall-rca-20260905.md`, Unplug-Nachweis `tdeck_cdc_unplug.py`); TD-14 Karten-Doppelaufbau beim Tabwechsel ueber die Leiste (RCA
   `tdeck-cursor-stall-rca-20260905.md`, Instrument `msg_roll`); TD-12/TD-13 (T-Deck Tab-Animation-Stall, Trackball-Doppelclick — Code
   gefixt 2026-09-05, `3f6a35d5`/`7368539e`, Bench-Nachweis auf DK5EN-14 offen, siehe
   BACKLOG §3.8p); TD-10 Backspace-Auto-Repeat (zurückgestellt, Konzept fertig:
   [`tdeck-backspace-autorepeat-20260831.md`](tdeck-backspace-autorepeat-20260831.md)),
   TM-29, TM-23 (von TD-09 abgelöst) — TM-06/07/14/19 sind seit 2026-08-31
   erledigt (siehe oben); BAT-02-Grenze dokumentiert
   (stabil in-band floatender Teiler ist von einer vollen Zelle nicht unterscheidbar —
   RAK-90 liest stabile 4,22 V, Erkennung greift dort nicht).

**Bench state at handover** (all four on USB, LoRa TX 2 dBm; DK5EN-93 and DK5EN-14 run the
2026-08-31 wave-2 build, 92/90 the previous one): Heltec `DK5EN-93` — gateway off, GPS on
(fix), DEBUG on; T-Beam `DK5EN-92` — webserver on, gateway off, debug/udplog off; T-Deck
`DK5EN-14` — web UI live at dk5en-14.local; RAK `DK5EN-90` — bench gateway, EXTUDP on, time
source NTP (first time ever valid without GPS). No background runs
alive. Production node `DK5EN-98`: gateway off, mesh off, 2 dBm.

---

## Frühere Übergaben (Historie)

## 2026-08-31 morning: TM-36 soak evaluated (PASS), TM-45 found, GPS restored

- **TM-36 done — the 9.1-h WiFi night soak passed every bar** (2026-08-30 22:42 →
  2026-08-31 07:48, T-Deck/Heltec/T-Beam, `--wifidrop` every 600 s, GPS off, gateway off):
  55/55 drops recovered per board (medians 4459/4016/4032 ms, worst max 5329 ms), 0
  unsolicited disconnects, 0 watchdog actions, 0 `[WIFI];stall`, 0 unexpected resets (the one
  "reset" each on T-Deck/T-Beam is the port-open reboot at t=0), `same_ip` 55/55/55, every
  join WPA2-PSK, BSSID re-picks between the two Orbi radios after drops (14/6/13) as
  designed. Consistent with the Wave-W 51-min fragment (3989/4123/4123). Report:
  `docs/wifi-soak-report-20260831.md`; summary + events CSVs checked in under
  `tools/bench/runs/wifisoak_night_20260830-224246/` (raw `.log`s stay untracked, 6 MB).
- **TM-45 filed (Medium, both platforms)** — the soak was the first run with NTP as the only
  clock, and NTP got **0 replies in 9.1 h** (545–548 timeouts per board, 60-s cadence):
  since TM-35 the reply is harvested only by `getMeshComUDP()`/`neth.getUDP()`, and both sit
  inside `if(bGATEWAY)` (`esp32_main.cpp:3708`, `nrf52_main.cpp:1963`) while the send path
  is un-gated (`esp32_main.cpp:2628`). A gateway-off, GPS-off node never gets a valid wall
  clock (feeds NC-01) and sends one NTP datagram per minute forever. Full analysis in the
  soak report and the BACKLOG row.
- **GPS back on** on T-Deck, Heltec, T-Beam (`--gps on`, persisted), verified with `--pos`:
  all three have a fix (Heltec sat:7, T-Beam sat:8). Bench state otherwise unchanged.
- Housekeeping: the 2026-08-30 late intake §3.8o (WEB-01/02/03, NC-01) was still uncommitted
  and went in as its own commit; the docs-only CI `paths-ignore` edit to
  `.github/workflows/ci-build.yml` is still uncommitted, operator decision pending.

## 2026-08-30 night: TM-43, UDP-01 answered, UDP-02 found+fixed, all 8 parser findings fixed, MEM-01

- **TM-43 done** -- `extudp` scenario (`rak_harness.py`) + `tools/bench/extudp_peer.py`; RAK
  DK5EN-90 PASS (601 s soak, 197/197 datagrams, no reset); Heltec as ESP32 control. Not in
  `--scenario all` (reconfigures the node); run by name. `docs/bench-extudp-regression.md`.
- **UDP-01 answered** with the `[EXT];rx/tx;stack_hwm` instrument: EXTUDP inbound leaves 424 B of
  the 4 kB nRF52 loop stack, the gateway UDP->LoRa path 276 B (run minimum) -- thin, not zero.
  Reporter questions still open; see the §3.8l list before touching code.
- **UDP-02 found by the control run, fixed:** one 255-byte datagram wedged ESP32 EXTUDP receive
  permanently (`WiFiUdp` unread-buffer semantics); `flush()` drain + marker; Heltec before/after
  0/40 -> 40/40 picked up.
- **PT-01: all eight parser findings fixed**, every pinned `TEST_IGNORE` now a real assertion
  (`native_parsers` 24, `native_extern` 32, `native_xml` 11, 0 skips). Notables: mheard date is
  10 chars (`YYYY-MM-DD`), timezone `-03:30` converts to -3.5, NUL in `msg`/`dst` is rejected,
  `val` sized 163 so max dst+msg ships complete, `"none"` is a legal message text again.
- **MEM-01 done** (commit `861f2967`): `resource_watch.py dram` + hard CI gate at 4 kB;
  classic-ESP32 rings 30/25 -> 20/20; headroom E22 11,896 B, T-Beam 10,712 B. **MEM-02**
  (rings -> boot-time heap, ~28 kB) is parked in §3.8m pending the risk assessment.

## 2026-08-30 late: Wave 2 (BP-01, CS-03, TM-39, TM-40, TM-41)

All verified on the bench; all four nodes run this build (`--gateway off` everywhere, Heltec
`srv OE`, `--udplog off`).

- **BP-01 / TM-37** -- `src/backpressure.h` (pure state machine, 18 cases), origin tag set by
  every user-message caller (serial `::`, BLE, web GUI, EXTUDP, T-Deck GUI), notices back on that
  transport only, `[BP];notice;<Q>;depth;N;max;M` / `[BP];refuse` raw lines. Texts are
  "Q-code - meaning" (operator note during the gate). QRV closes at depth 0, not 1 -- depth 1
  flapped QRS/QRV/QRS 400 ms apart on the bench. DK5EN-93: 22-message burst -> QRS at 2, QRT at
  16/20, five refusals, QRV after the drain (16 s for 2 frames).
- **CS-03** -- `GET /config.json` (Content-Disposition, 107 fields on the Heltec, 2.1 kB) and
  `POST /config` (bounded body reader, layout/value/crc checks, reboot after save). Excluded from
  the file: msg-id counters and live sensor readings (would rewind dedup / never byte-stable).
  GPS lat/lon/alt still drift on a GPS node, so "second export byte-identical" holds only with
  GPS off. Findings: E22-DevKitC DRAM headroom ~1.7 kB (a 6 kB static buffer failed to link, the
  config buffer is heap for one request); with an empty `node_webpwd` the file (WLAN password
  included) is one unauthenticated GET away -- same class as the setup page, now concentrated.
- **TM-39** -- OE and DL are the same server today (`meshcom.oevsv.at`); IT is
  `meshcom.dig-italia.it` (DNS 1.7 s vs 61 ms). Every KEEP answered by a 20-byte BEAT on all
  three. `[GW];rx;type;DATA` is gated behind `--udplog` (per relayed frame otherwise). The nRF52
  has `[UDP];rx/tx` + `--udplog` now (TM-38 follow-up closed).
- **TM-40** -- live OTA PASS on DK5EN-92 (`tools/bench/runs/ota_20260830-201649/`). The tool
  matches `TBEAM_AXP2101` against env `ttgo_tbeam` by prefix.
- **TM-41** -- 516/516 frame CRCs on the T-Deck at stride 1 (12.2 fps); the loop-task watchdog
  must be fed per frame.
- Open follow-ups filed in BACKLOG: nRF52 `CONF` indicator vs ESP32, nRF52 internet path without
  per-country case, the 9 scratch scripts still on group 9999, PT-01's 8 parser findings.
- **TM-38 real run is still yours** (AP power cycle, see `docs/bench-ap-reboot.md`; set
  `--gateway on` on the three ESP32 nodes first).

## 2026-08-30 evening: Wave 1 of the intake campaign (`/orchestrate-waves`)

Eight writer briefs on disjoint file sets, one gate, one commit. Everything below is verified on
the bench unless marked otherwise; all four nodes run this build.

- **RX-01 / TX-01** -- `isUnconfiguredCall()` (`configuration_global.h`); RX drop after
  `decodeAPRS()` in `OnRxDone` and on the GATE-in path; TX refused in `addTxRingEntry()` and
  `doTX()`. Markers `[RX];drop;unconfigured` / `[TX];refuse;unconfigured`, one line per 10 s.
  Bench: `--setcall XX0XXX-00` + `--sendpos` on DK5EN-93 -> `[TX];refuse;unconfigured;refused;1`.
  RX-01 can no longer be provoked from a bench node (TX-01 stops the sender) -- native predicate
  tests only. `test_txring`/`test_txring_flood` fixtures now set a real callsign.
- **FL-02** -- `sendHeyShot()` (own timestamp, `--sendhey` only; trickle untouched).
  `[HEY];shot;suppressed;since_ms;1635;min_ms;30000` on Heltec, 3021 ms on the RAK.
- **CS-01 / CS-02** -- `--maxhop <1..6>` persisted (NVS `max_hop_text`, nRF52 struct + sanitize
  clamp), `[MAXHOP];text;N;pos;N`, web `<select>` 4/3/2 + current, `/setparam/?maxhop=`,
  `/getparam/?maxhop`. Heltec kept 2 across a port-open reset; 7/0/9 rejected; RAK set 3.
  Both restored to 4. `{SET}` still changes the value at runtime only (operator decision).
- **PT-01** -- `native_parsers` (decodeAPRSPOS, decodeMHeard, checkVia; needs
  `lib_ldf_mode = chain+`), `native_extern` (getExtern/handleExternTelemetry), `native_xml`
  (decodeTinyXML, tinyxml2 lib). 58 cases, 8 skipped = real parser findings, listed in
  BACKLOG §3.8j "PT-01 findings". `mheard_functions.cpp`, `extudp_functions.cpp`,
  `tinyxml_functions.cpp` carry `#ifndef NATIVE_BUILD` guards only.
- **TM-42** -- group `TEST` is the default of the injector doc and of `tdeck_harness.py` /
  `oled_harness.py`; proof: `::{TEST}bench proof TM-42 191514` left DK5EN-93 with `--gateway on`
  (`[UDP];tx` len 107 at 17:15:14Z) and is absent from mcmap, while 99099 traffic of the same
  minute (17:15:06Z, 17:16:06Z) is there. Gateway switched back off. Nine scratch scripts in
  `tools/bench/experiments/` still say `9999` (listed in the runbook §2.6, cosmetic).
- **TM-38** -- `tools/bench/experiments/apreboot.py` (`start|status|stop|report`, detached via
  double fork, all four ports, macOS notification + `say`, `--strict-udp`, `--require-ntp`),
  14 unit tests, hardware smoke on all four nodes (verdict FAIL "no outage detected", as
  expected). **The real run is yours:** `--gateway on` on T-Deck/Heltec/T-Beam first (they emit
  no `[UDP];tx` with gateway off), then the start line in `docs/bench-ap-reboot.md`, cycle the
  APs when the Mac says so, `report` afterwards. The RAK has no per-datagram UDP marker (its
  `udp_rx` is derived from the `[ETH];link` heartbeat `rx_n` delta) -- a firmware marker in
  `nrf_eth.cpp` is the open follow-up.
- Gate: native 346 cases / 9 envs green, four boards built (Heltec flash +2.2 kB, RAK +0.9 kB)
  and flashed.

Wave 2 (not started): BP-01/TM-37 (Q-code back-pressure), CS-03 (config JSON export/import --
note the web server reads no POST body today, `web_functions.cpp:~350`, so the upload needs a
body reader first), TM-39 (country-server probe), TM-40 (OTA regression), TM-41 (T-Deck
colour/geometry test on the flush path).

## Where things stand

| Branch                        | Contents                                                                                                                                                       | Device state                            |
| ----------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------- |
| `tdeck-partial-refresh-trace` | Working branch. `v4.35p_prio` + upstream `dev` `2cb6bb4d` merged + all of today's fixes and instrumentation. 0 behind `upstream/dev` as of 2026-08-29.         | **All four bench nodes run this build** |
| `v4.35p_prio`                 | Fork main (docs, tools, tests, debug code). Behind the trace branch; merge the trace branch back into it when the T-Deck work is declared done.                | —                                       |
| archive tags                  | `archive/pre-rebase-20260827{,-2}`, `archive/tdeck-partial-refresh-wip-20260828`, `archive/claude-flood-network-20260822` — the deleted branches, on `origin`. |                                         |

Bench node settings (decided 2026-08-29): Heltec `--debug off`, GPS on, BME280 on; T-Beam track
off, GPS on; T-Deck as is. Mind that the firmware's triple click (`--btn triple`, OLED harness
`track` scenario) switches GPS off together with the track page -- the harness restores the GPS
state it found since the wrap-up commit; check `--pos` after any manual triple click.

Bench fleet (all on USB, port names can move — match by USB serial; table in `BACKLOG.md` §3.8f):
T-Deck Plus `DK5EN-14` `/dev/cu.usbmodem1101`, Heltec V3 `DK5EN-93` `/dev/cu.usbserial-0001`,
T-Beam v1.2 `DK5EN-92` `/dev/cu.usbserial-573C0005841`, RAK4631 `DK5EN-90` `/dev/cu.usbmodem201301`.

## Fixed today, verified on hardware

| Item         | What                                                                                                | Measured                                                                                           |
| ------------ | --------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------- |
| UP-01        | `serializeJson` bound = buffer size, not JSON length (`bleJsonFrame()` + native test)               | native 50/50                                                                                       |
| TM-01..04    | Audio task + queue; loopTask never blocks on audio; player SD reads under the bus mutex             | `audio_stall` 1 552 ms -> 23 ms                                                                    |
| TM-15        | Boot messages without the 2 s pump                                                                  | `CLIENT STARTED` 17.8 -> 4.6 s                                                                     |
| TM-18        | Trackball edges counted in an ISR (old level compare lost ~75 % of a fast roll)                     | edges == steps, 0 backlog, repaint p50 8 ms; operator: "feels good now"                            |
| TM-20        | `startNetwork()` non-blocking (async scan, no delays, no `delay(1500)` retry)                       | no loop gap > 0.7 s at boot, loop max 26 ms; was ~7 s frozen, also every 5 min while unconnected   |
| TM-09        | Heltec V3/V4/Stick OLED on `Wire1` hardware I2C, 400 kHz (`-D U8X8_HAVE_2ND_HW_I2C=1` is mandatory) | frame push 579 ms -> 34.5 ms, loop max 645 -> 39 ms                                                |
| TM-08        | T-Deck header labels/colours written only on change                                                 | idle invalidations 36.9/s -> 7.0/s                                                                 |
| TM-21        | `[WEB]...no ip set` once per state                                                                  | was ~125 lines/s with debug on                                                                     |
| TM-12        | Loop/heap instrument on the RAK4631                                                                 | loop avg 99.7 / max 104 ms (paced), heap 111 832 free                                              |
| TM-05        | Closed by analysis: all SPI2 users except the (mutex-guarded) audio task run on loopTask            | —                                                                                                  |
| ready marker | `[BOOT];ready;ms;N;ip;X` (raw `Serial.printf`; `printfdeb` strips `;` outside `--debug csv`)        | ready 9-11 s on all ESP32 boards with WiFi joined                                                  |
| TD-03 / H1   | Active-tab message view trimmed like the model (`msg_list_trim_view()`, 50 bubbles)                 | harness `trim`: 60 -> 50 children; saturated view -584 B PSRAM / 20 msgs (was 2 760 B/msg)         |
| UP-02        | Last `delay()` out of `add_map_point()` (LoRa RX path, 30x in `refresh_map()`)                      | harness `map --map-stations 40`: recycle branch wraps, 40/40, no crash; loop max unchanged (noise) |
| TM-22/10/27  | SSD1306 full-buffer, frame CRC, unchanged frames skipped (`oledFrameUnchanged()`)                   | OLED harness 8/8 Heltec V3 + T-Beam; blank CRC `0xefb5af2e` both; repeat page -> skip              |
| TM-33 (a)    | GT911 `begin()` retried 5x / 100 ms on the T-Deck (upstream #64 `touch: failed`)                    | boot scenario: `touch;1;touch_tries;1`; failure path not reproducible on the bench                 |
| TM-33 (b)    | `--display on/off` drives the T-Deck TFT (`tft_on()`/`tft_off()`, upstream #690)                    | harness `displaycmd`: before 0/4, fixed 4/4; `sleep` scenario unaffected                           |

Cross-board regression after all of it: Heltec V3, T-Beam, RAK4631 boot clean, LoRa TX/RX in every
direction between the four nodes, `--info` OK (BACKLOG §3.8f "Cross-board regression").

## Measured, not fixed — decisions pending

- **TD-01 / TM-11 / TM-24 — CLOSED 2026-08-30** (24/24 first joins, `got_ip` median 10.4 s, 0
  disconnects; see the late-session section below). Kept here only as the record of how it was
  found. 12 boots per arm on DK5EN-14, same hour:
  baseline 4/12, BLE advertising deferred 3/12 (**BLE hypothesis refuted**), join by SSID only
  8/12. Every failure logs `[WIFI];event;disconnected;reason;2` (= `AUTH_EXPIRE`). Root cause named
  in the evening (TM-24): **the firmware has no roaming** — no 802.11k/v/r in the SDK build, no
  auto-reconnect, channel+BSSID pinned at join — while the bench WLAN is a steering mesh (router +
  satellite). Fix direction decided: join by SSID only, let the driver roam,
  `WiFi.setAutoReconnect(true)`, keep the ping watchdog as last resort. Confirm with a 24-boot run
  (`BENCH_WIFI_NO_BSSID=1` build, `tools/bench/experiments/bootloop.py`, 75 s per boot) before
  shipping. _Revised 2026-08-29 by TM-34: "no auto-reconnect" is wrong — it is on by default; the
  pin is what stops the driver choosing another AP. See `wifi-findings-20260829.md` §6._
- **Tile format on the SD card** — parked as low priority by decision (2026-08-29): stability and
  functionality first, optimisation later. Options and the `[SDMAP]` read/decode split stay in
  `tdeck-findings-20260828.md`.
- **OLED (TM-22/TM-10/TM-27) fixed 2026-08-29:** SSD1306 full-buffer on the `#else` ladder (no
  bench node for that half), frame-buffer CRC per push, identical frames skipped. OLED harness 8/8
  on Heltec V3 and T-Beam (`display` CRC round trip, `dirty`). Frame push stays ~32/36 ms — that
  is 1 KB at 400 kHz; further savings need partial updates, not a faster bus.

## Harnesses (run from `tools/bench/runs/` so the raw logs land there)

- `python3 tools/bench/tdeck_harness.py --list` — 14 scenarios (boot, idle, tabs, drawer, inject,
  audio, audio_stall, sleep, screen, map, nav, input, heap, trim). All 14 green on DK5EN-14.
- `python3 tools/bench/oled_harness.py --list [--port …]` — 8 scenarios (boot, pos, inject, pages,
  display, track, timing, dirty) for every U8g2 board. 8/8 on Heltec V3 and T-Beam.
- `tools/bench/experiments/rolltest.py` — operator trackball roll test (edge vs level mode).
- Both harnesses wait for `[BOOT];ready`, accept `--scenario a,b --skip c`, and switch the panel on.
- Opening a port reboots every ESP32 node; the RAK needs `dtr=True`; the T-Deck ignores serial for
  ~11 s after `CLIENT STARTED`; the Heltec prints `;`-less lines unless `--debug csv`.

## Firmware bench hooks added today (fork-only, all default-off or log-only)

`--btn click|double|triple`, `--oledstat`, `--oledlog on/off`, `--injectpos` on OLED boards,
`--scroll <tab> <dy>`, `--key <text>`, `--ball <dir> <n>`, `--balledge on/off`, `--balledges
[reset]`, `[TFT];on/off`, `[KEY]`, `[BALL]`, `[OLED];frame`, `[WIFI];event;…`, `[BOOT];ready`.
Build-flag experiments: `BENCH_BLE_ADV_LATE`, `BENCH_WIFI_NO_BSSID`.

## Priorities as set by the operator (2026-08-29 evening)

1. T-Deck: UI performance, mouse pointer, stability, code quality (incl. handover §4.2 review
   flaws and TD-03 heap: `msg_list` never trimmed, H1/C2 in `tdeck-gui-verdict.md`).
2. OLED redraw on Heltec V3 and T-Beam (TM-22, TM-10).
3. Boot time on all four platforms: T-Beam, T-Deck, RAK nRF52, Heltec V3 (TM-16; RAK not yet
   profiled).
4. Automated integration/regression harnesses over USB serial on all four platforms (RAK has no
   harness yet; T-Deck 13 scenarios, OLED 7).

- **WiFi (revised 2026-08-29 late, reverses the evening decision):** TM-20 (non-blocking
  `startNetwork()`, all boot delays removed) **stays OUT of the PR.** It feels good on the bench,
  but without the waits the station no longer hears the beacons of every nearby AP and can
  associate with the weakest BSSID of a mesh WLAN — shipped as is it would break nodes behind
  multi-AP WLANs. The 7 s freeze it removes is real, so it goes upstream only together with a
  scan/selection policy. That policy is **TM-34**, a separate research track (BACKLOG §3.8f):
  (a) driver capabilities, (b) scan/wait/select-best-AP options, (c) log output for the
  WiFi-stalls-the-node case (blocks even LoRa RX), (d) SSID-only association, (e) re-connecting,
  (f) roaming, (g) band/AP steering. TM-34 absorbs TM-24 and the TD-01/TM-11 confirmation run;
  findings doc first, then its own bench protocol and its own PR.
  **Desk half delivered 2026-08-29:** [`wifi-findings-20260829.md`](wifi-findings-20260829.md)
  answers (a)-(g) from the driver source and `sdkconfig`, with fix plan F1-F9 (§9) and bench arms
  A0-A5 (§10); runner `tools/bench/experiments/bootloop.py`. **No bench arm has been run yet.**
  Four things it changes: the pin — not the missing wait — is the defect (full-channel scan plus
  sort-by-signal is the driver doing our job better, and on every reconnect); `setAutoReconnect`
  was already `true`, so TM-24's premise is wrong and the real problem is three restart owners
  fighting; the largest `loopTask` blocker left is `hostByName()` at up to 31 s, not WiFi bring-up;
  and **TM-20 should not be reverted** — it ships together with the selection fix, in one PR.
- **TD-03 heap defect: fixed 2026-08-29, goes into the PR.** RAK boot profile + harness needed for platform
  parity (TM-25/26). Screen CRC on Heltec and T-Beam (TM-27; T-Deck has no readback). TM-06/07
  lower priority (cumbersome, no longer highest). Full automation low priority — the manual
  procedure is `automation-runner-runbook.md` (TM-29).
- **E290 Wireless Paper hardware arrives the week of 2026-09-01** (TM-28, delayed until it is
  here) — e-paper redraw is the next display target (WP_DISP code paths, `PAGE_MAX 10`, browse loop in `singleClick()`); needs its
  own harness scenarios and a frame instrument.
- Tile format (TM-23) parked; upstream sync stays merge + net-diff review each time.

## 2026-08-29 late session (TM-30/31/32, TM-13, TM-25/26, A5)

- **TM-32 fixed** (settings plausibility at load, native 10/10, bench-verified with `--flashpoke`),
  **TM-13 done** (`INSTR_SECTION`, `[INSTR-SECT]`, gap attribution), **TM-25/26 done** (RAK ready
  marker, boot profile, `rak_harness.py`). **TM-35 new**: RAK gateway `getUDP()` blocks the loop
  1.6–3.3 s every ~20 s, `sendHey()` 0.7–1.4 s.
- **TM-30 not reproduced** (35 min `uptime` run on DK5EN-14: latency, loop max and heap flat, no
  crash) — the scenario stays as the regression gate for #1083.
- **TM-31** instrument built (`gwflood.py`, `--srvip` hook); appeared blocked because UDP from the
  Heltec never seemed to reach the Mac. _Superseded 2026-08-30: no tcpdump and no Orbi change were
  needed — the LAN was fine and the verdict came from a blind instrument. See the late-session
  section below._
- **TM-34 arms**: A0/A5 first attempt **void** — `bootloop.py` opened the port with DTR/RTS asserted,
  which does not reset the T-Deck Plus (24/24 "no reset marker"); fixed (dtr/rts False). A5 (24
  boots on `ORBI63_Guest`, WPA2) re-run: **24/24 first joins, got_ip median 9.6 s, 0 disconnects**
  (`tools/bench/runs/bootloop_A5_20260829-230435/summary.txt`) vs 4/12 on WPA2/WPA3 `ORBI63` —
  the first-join failure is WPA3-SAE, not steering; the WiFi fix must force WPA2-PSK.
  **A0 run 2026-08-30 on WPA2/WPA3 `ORBI63`: 0/24 first joins, got_ip median 55.8 s / max 56.5 s,
  240 disconnects (216× AUTH_EXPIRE, 24× AUTH_FAIL)** — every boot pins the router BSSID
  `5A:…:8B` (-68…-75 dBm, same radio as A5's guest VAP at the same RSSI), fails 9 attempts and the
  driver's auto-reconnect lands the 10th at ~55 s (`tools/bench/runs/bootloop_A0_20260830-084907/`).
  Baseline for F1–F4 is therefore 0/24, not TM-11's 4/12.
- Firmware bench hooks added (fork-only): `--flashpoke <field> <value>`, `--srvip <ip>`,
  `[BOOT];ready` on nRF52, `[INSTR-SECT]`/`[INSTR-GAPS]`/`[INSTR-LOOP];gap`.

## 2026-08-30 Wave W (TM-34 WiFi fix, TM-35 parity instrumentation)

- **WiFi bring-up rewritten on all ESP32 boards** (`udp_functions.cpp`, `esp32_main.cpp`): driver
  owns AP selection (`ALL_CHANNEL_SCAN` + sort-by-signal, `persistent(false)`), SSID-only
  `begin()`, **WPA2-PSK forced on WPA2/WPA3 APs** via `esp_wifi_disable_pmf_config()` (SAE needs
  PMF; `WIFI_SAE_POLICY` 1 = default), `got_ip` harvested by event (no blind window, no boot radio
  reset), watchdog 180 s grace -> reconnect -> 360 s radio reset, async DNS off `loopTask`,
  `[WIFI];stall/assoc/link`, `--wifistat`, `--wifidrop`, `--wifi on/off` (HL-01), HL-02 default.
- **Arm A4p1 on WPA2/WPA3 `ORBI63`: T-Deck 24/24 first joins (`got_ip` median 14.2 s uptime, 0
  disconnects, 0 stalls), Heltec 12/12 (11.2 s), T-Beam 12/12 (10.6 s)** — baseline A0 was 0/24
  at 55.8 s. `[WIFI];assoc … pmf;0;wpa3;0` on every join = WPA2-PSK negotiated. `--wifidrop`
  reconnect 4.0 s. Runs in `tools/bench/runs/bootloop_A4p1_*`.
- `got_ip` median <= 6 s from bring-up is **not** met as measured from power-on: bring-up starts at
  ~5 s (T-Deck) and our diagnostic scan costs ~5 s before `begin()`; the driver join itself is
  ~4 s. The scan is log-only now (dwell 300 ms x 13 channels) — shortening or dropping it is the
  lever if 14 s matters; not done, keep the field diagnosis unless the operator decides.
- **RAK parity (TM-35)**: `[ETH];stall;<site>` on every W5100S socket/DHCP/link call,
  `[ETH];event;link|got_ip|dhcp|reset`, `[ETH];link` heartbeat, `--ethstat`, `--ethdrop`
  (= `resetDHCP()`, 126 ms on the bench). The 1.6–3.3 s `getUDP()` stall did not show in a 2-min
  window (`udp_rx` max 1 ms) — the marker names it when it recurs. Fix still open.
- Soak runner `tools/bench/experiments/wifisoak.py` (held-open sessions, `--wifidrop` every
  10 min, per-event CSV, reconnect distribution, `--parse-only`). Reducer verified on a smoke log.
- Gates: native 60/60, `test_tdeck_parse` 56/56, all four boards build, OLED harness Heltec 8/8,
  RAK harness boot/info/instr/mheard PASS (`lora` needs `--peer-port`, as before). T-Beam OLED
  `dirty` failed 3/3 **while the other nodes were boot-looping** (LoRa boot beacons flip the
  page) — re-run with the bench quiet before reading it as a regression.
- **A4p0 (SAE kept): 24/24 addressed by 19.7 s, all SAE, but every second boot +4.5 s (silent SAE
  retry) — the A0 0/24 was pin + SAE together. Policy 1 (WPA2-PSK) stays default.**
- **Hook stall found and fixed:** `WiFi.getMode()` in the 60-s link heartbeat, called every loop
  pass, blocked `loopTask` 2.7–2.9 s per boot while the driver scanned (`[INSTR-LOOP];gap … in;lvgl`
  in every A4p1/A4p0 boot, absent in A0/A5). Loop hooks now use STA_START/STOP events only. Rule:
  never call `esp_wifi_*` / `WiFi.getMode()` from the loop unless connected. A4p1b re-measures.
- **A4p1b (hook fix): T-Deck 24/24, `got_ip` median 14.1 s, 0 disconnects, 0 stalls.** The one
  remaining 2.6–3.5 s loop gap per boot (~8.5 s uptime, `section_ms` 32 = outside every section,
  right after GPS/UBLOX init) is pre-existing: the A0/A5 build predates the gap reporter and A0
  shows the same single scan-poll in that window. TM-16 lead, not WiFi.
- **TM-35 with the `[ETH]` instrument, 600 s:** loop max 314 ms = the 15-min NTP round trip
  (`[ETH];stall;ntp;ms;213`), 122 UDP RX with `udp_rx` max 5 ms; the 1.6–3.3 s `getUDP()` stall
  did not reproduce. Bounding NTP needs an async client on the shared gateway socket — operator
  decision (BACKLOG TM-35).
- **Gates after the hook fix:** T-Deck harness 15/15 PASS (`tdeck_w_20260830.json`), OLED
  Heltec 8/8. T-Beam OLED `all` on a quiet bench: 6/8 — `display` and `dirty` fail on harness
  sensitivities, not on firmware: the panel had already auto-switched off when `display` began
  (first `--display off` draws 0 frames, `offwait_ms` 12952), and `dirty` landed on page 5 whose
  content changes every draw (three `--display on` pairs, all `frame`, CRCs differ 250 ms apart;
  yesterday's green run was on page 3). Standalone `dirty` is invalid by construction (starts at
  5 s uptime, first frame inside the window). Harness fix: pin the page and wake the panel first.
- **Overnight soak started 2026-08-30 11:26** (`tools/bench/runs/wifisoak_W_20260830-112600/`,
  14 h, `--wifidrop` every 10 min on T-Deck/Heltec/T-Beam, detached `nohup`; interim
  `summary.txt` every 10 min). Reduce with `wifisoak.py --parse-only wifisoak_W_*/{tdeck,heltec,tbeam}.log`.
- Open: overnight `wifisoak.py` on all
  three boards; WPA3-only APs cannot associate with policy 1 (no PMF) — documented trade, an
  adaptive fallback (SAE first, PMF off after 2x `AUTH_EXPIRE`) would cost ~9 s on every boot.

## 2026-08-30 afternoon: TM-35 async NTP, TM-31 unblocked + gateway defect

- **TM-35 done.** `src/ntp_async.{h,cpp}` (`NtpAsync`) replaces `NTPClient` on both platforms: the
  48-byte request goes out on the shared gateway socket and the call returns at once; the reply is
  harvested by the normal receive path (`getUDP()` / `getMeshComUDP()` offer every datagram to
  `tryConsume()` before parsing it as a MeshCom frame). 2.5 s timeout, 5 s/60 s backoff, mode-4 /
  stratum / epoch validation, `[NTP];ok|timeout|txfail|kod` markers. It also removes a second
  defect: `NTPClient::forceUpdate()` flushed _every_ queued datagram off the shared socket before
  sending, so each refresh could eat pending GATE/CONF frames.
  **Gate met:** RAK 600 s steady state `rak_instr600_ntpasync_b_20260830.json` — **loop max 145 ms,
  0 gaps > 250 ms** (was 314 ms / 1 gap), `eth_state` max 2.9 ms (was carrying the 213 ms NTP
  stall); the 15-min refresh fell inside the window at `[NTP];ok;epoch;1788084488;rtt;106`, i.e.
  106 ms on the wire at zero loop cost. Native `test_ntp_async` 10/10.
- **TM-31 unblocked — the LAN was never the problem.** The old "no datagram reaches the Mac"
  verdict came from a blind instrument: the whole ESP32 UDP receive path logs only through
  `DEBUG_MSG()` (compiled away, `DO_DEBUG 0`) and `printfdeb()` behind `--debug on`. New fork-only
  instrument: `--udplog on/off` (`[UDP];rx`/`[UDP];tx` per datagram) and `--udpstat`
  (`[UDPSTAT];bind;..;rx;..;tx;..;tx_fail;..`), counters always kept. Both directions measured
  good: Mac -> Heltec 3/3, Heltec -> Mac 6/6 with `--srvip`. **No sudo tcpdump, no Orbi change.**
- **TM-31 found a real gateway defect (ESP32 only), fixed.** `getMeshComUDPpacket()` evaluated the
  dedup gate `is_new_packet()` _after_ the `msg_type_b == 0x21` branch had already inserted the
  msg_id via `addLoraRxBuffer()` — so every UDP position frame deduplicated against the entry it
  had just written itself (`RX_DEDUP_ADD slot N`, 17 ms later `RX_DEDUP_DUP slot N`) and an ESP32
  gateway **never relayed a UDP position frame to LoRa**. Baseline `gwflood_instr_20260830.json`:
  30 ingress, 36 `[UDP];rx` seen, **0 queued, 0 TX, 0 observer RX** at every inter-arrival from 8 s
  to 0.5 s. Fix: read the dedup gate before that branch (`is_new_packet()` has no side effects),
  and skip the now-redundant second insert on the queue path so the ring insert rate is unchanged.
  The nRF52 gateway path never had the early insert. After the fix the injected frames radiate
  (`TX-LoRa ... x02F9FE00/01/02`) and the T-Beam observer hears them (`RX-LoRa2 ... x02F9FE02`).
- `gwflood.py` now enables `--udplog`, reports ingress / `[UDP];rx` seen / `RING_WRITE src=udp_rx`
  queued / LoRa TX / observer RX so a loss is attributable to a stage, and falls back to a forced
  `--reboot` for a node that does not reset when the port is opened (T-Beam v1.2).
- **TM-31 result -- upstream #568 answered, with the mechanism named.** Definitive run
  `gwflood_fixed_settle300_20260830.json` (30 frames, gaps 8/4/2/1/0.5 s, 300 s settle so the queue
  can drain):

  | gap  | in  | queued | tx  | rx  | ingress->air median | max   |
  | ---- | --- | ------ | --- | --- | ------------------- | ----- |
  | 8 s  | 6   | 6      | 6   | 3   | 78 s                | 226 s |
  | 4 s  | 6   | 6      | 6   | 4   | 162 s               | 180 s |
  | 2 s  | 6   | 6      | 6   | 5   | 241 s               | 291 s |
  | 1 s  | 6   | 6      | 3   | 1   | 310 s               | 321 s |
  | .5 s | 6   | 6      | 0   | 0   | --                  | --    |

  **Nothing is lost at ingress** -- 30/30 datagrams reach the socket and 30/30 are queued. The
  radio drains at roughly one frame per 20 s under bench channel load, so the ingress-to-air
  latency climbs from 78 s to over 5 minutes. At 1 s and below the **20-slot TX ring saturates**
  (`queued=19/20`) and the firmware then discards the arriving frame:
  6x `RING_DROP_NEW ... prio=4 type=21 (queue full, no lower prio to evict)` (msg_ids
  `...16/18/19/1A/1B/1C`, all in the 1 s and 0.5 s groups) plus 1x `RING_DROP_PRIO` that evicted the
  node's **own** HEY (`type=40`) to make room. So a gateway fed faster than it can radiate does not
  drop at the network edge -- it delays by minutes and then drops at the TX ring, and it starves
  its own traffic while doing so. That matches #568 (the lost packet was the one on the shortest
  inter-arrival) and our DG0OPK TX-queue-latency finding. From a user's point of view a message
  radiated 4 minutes late is indistinguishable from lost, and downstream dedup windows will discard
  it anyway.

- **TM-31 text vs position, measured** (`gwflood_mixed_20260830.json`, 15 text + 15 position frames
  alternating, gaps 8/4/2/1/0.5 s, 300 s settle): **text 15/15 radiated, position 9/15** -- and the
  ring-drop lines say why: 5x `RING_DROP_PRIO ... prio=4 type=21 ... replaced_by_prio=3`, i.e. the
  arriving text frames evicted queued positions, plus 2x the node's own HEY evicted by positions and
  2x HEY tail-dropped. So the priority ladder does what it should under a real flood: text survives,
  positions are sacrificed, and the node's own background traffic goes first. `--assert-relay`
  passed (31/30 queued). Note the class: a UDP-relayed text is enqueued with status `0xFF` =
  `RING_STATUS_DONE`, so `getMessagePriority()` reads it as a _relay_ -> `MSG_PRIO_NORMAL` (3), not
  the path-based broadcast `MSG_PRIO_HIGH` (2). NORMAL still beats position (4), so the conclusion
  holds -- but whoever changes the relay path's status byte moves the priority of all UDP traffic.
- **Regression tests added** (`pio test -e native_aprs`): `test_txring_flood` (8) pins the queue-full
  policy -- equal priority is tail-drop, higher priority is head-drop, text evicts positions, a
  position evicts HEY, ACK always gets in, a 30-frame flood accepts exactly `MAX_RING-1`, and
  `MAX_RING == 20` is pinned so changing the buffering horizon is deliberate. `test_gwflood_frames`
  (6) runs the real `decodeAPRS()` over the injector's checked-in fixture
  (`test/support/gwflood_frames.txt`, regenerate with `gwflood.py --frame mixed --dump-frames`), so
  a malformed injector can no longer masquerade as a gateway that drops everything.
- Open on TM-31 (policy, not defects): tail-drop vs head-drop on a full queue and whether
  `MAX_RING = 20` (~6 min of buffered airtime at the bench drain rate) is right for a gateway. Both
  are now pinned by tests, so a change is a reviewable diff rather than a silent behaviour shift.
  The former open point -- "the sweep only injects position beacons" -- is closed: `--frame
pos|text|mixed` covers the 0x3A case #568 reports, and the measurement above is its answer.

## 2026-08-30 late: TM-16 boot time, TM-11/TD-01 closed, HL-03/HL-04, TM-37 filed

- **TM-16 done.** Two remaining boot costs measured on DK5EN-14 and cut:
  1. `SetupUBLOX()` ended with `WaitPause()` + `sendUBX_MON_VER()` + `readUBXbin()`. The version
     string feeds only a `[GPS_VER]` debug line (`ver` is file-local, no other reader),
     `readUBXbin()` always runs into its 500 ms timeout because it only retriggers and never
     returns early, and `WaitPause()` waits up to 1000 ms for the next character block. Now gated
     on `iGPSDEBUG >= 2`: **SetupUBLOX 1 933 -> 899 ms**, GPS init total 3 503 -> 2 001 ms, module
     configuration untouched.
  2. `startNetwork()` ran a full `WiFi.scanNetworks()` before every `begin()` (3-5 s), but since
     Wave W the driver picks the AP itself (`WiFi.begin(ssid, pwd, 0, NULL, false)`) and
     `wifiLogScan()` only prints and discards. The scan is now skipped on the **first** bring-up
     after reset (`[WIFI];scan;skipped;first_bringup`) and kept on every later one -- radio
     restart, watchdog, i.e. after something went wrong -- so the field diagnosis survives exactly
     where it is needed. Verified with `--wifi off` / `--wifi on`, which prints the AP list again.
     `--wifistat` reports `bringups;N`.

  **Measured over 24 boots** (`bootloop_TD01_close_20260830_20260830-134157/`): `got_ip` median
  **10 394 ms** / max 10 437, ready median **10 949 ms** / max 11 399 -- against Wave W's 24-boot
  baseline of `got_ip` median 14.1 s. Heltec ready 8.2 s, T-Beam 7.5 s, RAK boot/info PASS; GPS
  still detects UBLOX and gets a fix on all of them. T-Deck harness 15/15.

- **TM-11 / TD-01 closed.** The same 24-boot run is the confirmation: **24/24 first joins, 0
  disconnects, 0 connection-error bursts, 0 `[WIFI];stall`.** Chain: 4/12 (TM-11 baseline) -> 0/24
  at 55.8 s (arm A0, BSSID pin + SAE) -> 24/24 at 14.1 s (Wave W) -> 24/24 at 10.4 s (after TM-16).

- **HL-03 done -- and it was the opposite of what the row said.** `--mute on/off` already existed;
  the gap ran the other way: `btn_soundon` called `audio_set_mute()` and then `save_settings()`
  **without touching** `meshcom_settings.node_mute`, so the GUI wrote the OLD value to flash, the
  toggle did not survive a reset, and button and command disagreed about the state. The button now
  goes through `commandAction("--mute on/off")` like every other switch on that page, and the
  command itself now calls `save_settings()` -- it never persisted either.

- **HL-04 done.** `--persistflash on/off`, `--persistsd on/off`, `--immediatesave on/off`, each
  writing the field and saving, with `[PERSIST];flash|sd|immediate;<0/1>`. `--persistsd` also calls
  `loadPosPersistence()` exactly as the GUI switch does -- without it the node keeps working from
  the old store until the next reset. New `--persiststat` prints all four flags in one line.
  Verified on hardware including survival across a reset.

- **TM-37 filed** (operator question): every outgoing frame, the user's own included, goes through
  the same 20-slot TX ring and is radiated one at a time, so messages typed in quick succession are
  spooled. A user text is CRITICAL (DM) or HIGH (broadcast) and normally evicts something lower --
  but when the ring holds only equal-or-higher priority entries, `addTxRingEntry()` returns -1 and
  `sendMessage()` **ignores the return value**: the message is gone and the sender is never told.
  Wanted: act on the return value, and warn the user before the loss when the queue fills.

## Next session, in order

Done 2026-08-29/30: Wave W (TM-34 F1–F7 + WPA2-PSK, TM-17, HL-01/02, TM-33 (c)), TD-03, UP-02,
TM-22/10/27, TM-33 (a)/(b), TM-32, TM-13, TM-25/26, TM-30 (not reproduced), **TM-35, TM-31, TM-16,
TM-11/TD-01, HL-03/HL-04**.

0. **New intake 2026-08-30 — the operator's list of 11 points is filed, not started.** Read
   `BACKLOG.md` §3.8h (`CS-01`..`CS-03`: max-hop over serial + NVRAM, web drop-down, config
   download/upload), §3.8i (`GW-01` HEY parity with `--gateway on`; `TLM-01`/`TLM-02` telemetry
   definitions **parked** behind `TLM-03`, the soft-serial review) and §3.8f `TM-38`..`TM-42`
   (AP-reboot recovery test, country servers, OTA in the regression, T-Deck colour/geometry
   display test, group `TEST`). **Two decisions are waiting on the operator:** whether a server
   `{SET}` may overwrite a persisted max-hop value (CS-01), and what the config-export hash
   protects against plus whether secrets travel in the file (CS-03).

0b. **FL-01 fixed this session (2026-08-30): a node could be driven to beacon at loop rate.**
`sendPosition()`'s shot path (`--sendpos`, user button, and the unauthenticated EXTUDP
telemetry injection) had no rate limit at all; the field evidence is 25 146 position frames in
21 minutes from one station. `src/beacon_rate.h` + a 30 s floor in `sendPosition()`, native
`test_beacon_rate` 6/6, `pio test -e native` 76/76, ESP32 + nRF52 build, **and proven on
DK5EN-93**: the second `--sendpos` prints `[POS];shot;suppressed;since_ms;89;min_ms;30000`, and
after a 34 s pause the next one prints `[POS];shot;resumed;suppressed;1` and goes out. DK5EN-93
runs this build now, the other three bench nodes do not. `sendHey()` has the same missing floor
and is still open. Detail and
the two corrections to the mcmap finding: `BACKLOG.md` §3.8j.

0c. **Third intake 2026-08-30 (§3.8k), plus one more fix.** New: `RX-01` (discard frames whose
source callsign is still `XX0XXX` — seen relayed over four hops), `TX-01` (such a node must not
transmit at all — the other half of RX-01, guard in `doTX()` plus `addTxRingEntry()`), `BP-01` (TX back-pressure to
the sender as Q-code notices QRS/QRT/QTA plus QRV once the queue clears (only if a warning went out before), 80 % refusal for locally originated messages only —
this is the design for `TM-37`, and it needs bench regressions on all four boards), `FL-02`
(`sendHey()` needs the same 30 s floor as `FL-01`; 17 of 18 field events were `hey`).
**`CS-04` fixed:** `getparam()` searched for `"/setparam/?"` and took the substring from the `=`
instead of up to it — the whole read half of the Web-API was dead. Verified on DK5EN-93.
The mcmap finding now carries the corrections in its §10.

1. **TM-36 — restart the WiFi soak.** The 14-h run died at ~12:17 when the TM-31 work took the
   Heltec and T-Beam ports; ~51 min survived (checked in: 5 drops, reconnect median 4.0 s, 0
   unsolicited disconnects, 0 stalls). It needs all three USB ports exclusively, so start it when
   nothing else will touch the bench:
   `cd tools/bench/runs && nohup python3 ../experiments/wifisoak.py … &`, reduce with
   `wifisoak.py --parse-only`.
2. **TM-37 — a dropped outgoing message is silent.** Filed this session on the operator's question.
   `sendMessage()` ignores the `addTxRingEntry()` return value, so a message lost to a full TX ring
   never reaches the air and the sender is never told. Minimum: act on the return value and push a
   failure notice to phone + display. Wanted: a back-pressure warning at a high-water mark so the
   person stops typing. `test_txring_flood` already pins the ring policy, so the change is testable
   natively; bench check is `gwflood.py` with a parallel send burst.
3. **TM-31 leftovers — policy, not defects.** Tail-drop vs head-drop on a full queue, and whether
   `MAX_RING = 20` (~6 min of buffered airtime at bench drain rate) is right for a gateway. Both are
   pinned by tests, so either change is a reviewable diff. Same file as TM-37.
4. **TM-28** E290 Wireless Paper when the hardware is here (week of 2026-09-01): frame instrument,
   OLED-harness scenarios, `[OLED];crc` for e-paper.
5. **TD-05** GUI latency on the T-Deck — cause still unidentified; leads G05/G06 in §3.8f (SD + PNG
   decode with an ~870 ms `delay()` in the LVGL `read_cb`, `addMessage()` blocking 2 s / 8 s at
   boot). **TD-04** Europe map tiles on SD. **TD-06** full serial+net test rig.
6. **Upstream sync**: UP-03 (ours wins the merge, two hunks merged silently — check by hand),
   UP-04 (keep his hunk, drop ours from the PR), UP-05 (watch), UP-06 (trace consumers, small PR +
   test).
7. Lower: TM-06/07 (LoRa raw injection + SPI trace), TM-14, TM-19, TM-23 (tile format, parked),
   TM-29, Wave 0.6 (native suite), Wave 2 on nRF52 (CONC-15..18, N-14..16).
8. Then the PRs (operator decision, not now): T-Deck PR, OLED PR, WiFi PR — per BACKLOG §4.1.
   Note TM-20 ships only together with the WiFi selection policy, and Kurt owns review/merge
   upstream.

## Gates that were green when we stopped

`pio test -e native` 70/70 · `pio test -e native_aprs` 50/50 · `test_tdeck_parse` 56/56 ·
T-Deck harness 15/15 · RAK harness boot/info/instr PASS · OLED harness Heltec 8/8 · builds:
t_deck_plus, t_deck_pro, heltec_wifi_lora_32_V3, ttgo_tbeam, wiscore_rak4631, T-ETH-ELITE_1262.
