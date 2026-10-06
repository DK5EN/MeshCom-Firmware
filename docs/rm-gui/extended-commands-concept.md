# Remote management: extended commands and page fixes - concept

Status: IMPLEMENTED on fork-dev (cb7267bb..3ffa916f, 2026-10-06), host gates green, bench open
(`extended-commands-impl-plan.md` sections 8 and 9). Sections 5 and 9 carry the final rules.
Review record: `extended-commands-verdict.md` (findings, refuted claims). Evidence per claim:
the six verifier reports named there.

Scope: (A) page fixes from the 2026-10-06 review of the Remote page, (B) new remote read/write
commands, (C) sender rate-limit relief, (D) the 140-character reply budget, (E) two fixes the
review found in the already flashed code. Builds on `docs/rm-gui/` and `docs/adr-remote-hmac.md`.

## 1. Requirements (from the operator) and how they are met

| #   | Requirement                                                          | Outcome in this plan                                                                                                         |
| --- | -------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| 1   | "not locked" is unclear (there is no password lock)                  | Label only while the node really blocks commands, plain wording (P1)                                                         |
| 2   | Clearing the password switches remote management off                 | `nodePasswdApply(nullptr)` sets rm off and persists (P2)                                                                     |
| 3   | Toggle hidden until a password is set                                | Toggle hidden while no password and rm off (P3)                                                                              |
| 4   | Sent and received as one message list (time, command, state, reply)  | New Messages card under the node chooser; replaces Activity, status line and the "Waiting" line (P4)                         |
| 5   | TX power card removed                                                | Removed; power moves into the Radio card (P5)                                                                                |
| 6   | Advanced open on load                                                | `open` attribute (P6)                                                                                                        |
| 7   | New read/write commands for the list in section 3                    | 10 command names (section 3). Deviation: SF/CR/BW/frequency and max_hop pos are read-only (4.1), `maxhop` is read-only (4.2) |
| 8   | Sender limits reset on new password, and lifted ("10 tries is fine") | Per-target forget on password change, 10 tries once the key is proven (section 5). Deviation: 10 s spacing stays             |
| 9   | Reply fits 140 7-bit safe characters                                 | Whole reply DM text <= 140, body <= 105 (section 2.1)                                                                        |
| 10  | No console command needed                                            | Every command has a button or field on the page (section 6)                                                                  |

## 2. Facts that shape the design

| Fact                                                                                                       | Where                                  |
| ---------------------------------------------------------------------------------------------------------- | -------------------------------------- |
| Reply wire form `RM1 <ctr> <result> <16-hex tag>`; result starts with `ok ` or `err `                      | `remote_cmd.cpp:434-467`               |
| Exactly one verified reply per request ctr; no multi-part                                                  | `rm_runtime.cpp:385-426`               |
| The DM cap is `sendMessage`'s 160 including the `{CALL}` wrapper: the RM wire is at most 149 characters    | `loop_functions.cpp:4197`              |
| Receiver: 10 s rate limit (silent, not counted), 3 rejects in 90 s lock the target for 5 min               | `remote_cmd.cpp:396-399`               |
| `static_assert(RM_POLICY_COOLDOWN_MS == RM_RATE_MS)` ties sender spacing to the receiver limit             | `rm_runtime.cpp:20`                    |
| `execute()` runs on the loop task; nbr readers take their own lock, msgstore readers are unlocked          | `nbr_views.h:8-10`, `msgstore_api.h`   |
| Boot calls `lora_setcountry()`: every node with a country preset (all but country 7) reloads freq/SF/CR/BW | `lora_setchip` boot path               |
| Unauthenticated `{SET}` frames already set and save `max_hop_text` (N-02, accepted WONTFIX)                | server `{SET}` handler                 |
| All RM1 commands and replies are on the public server and in the mcmap archive                             | `loop_functions.cpp:2209`, ADR:161-164 |

### 2.1 The reply budget

The reply DM text (`RM1 <ctr> <result> <tag>`) must be at most 140 characters:

    result <= 140 - 4 - 10 - 1 - 1 - 16 = 108, and the result includes "ok " or "err "

So `RM_MAX_RESULT = 108` and a formatter body gets 105 characters. The firmware ceiling is 149
(result 117): 9 characters of margin. Tests assert `strlen(wire) <= 140` on the real wire with a
10-digit counter and worst-case values.

Version skew: replies of the existing commands stay at 63 characters or less, because older
operator firmware rejects a longer reply as unverified. New commands are only sent to targets that
report support (capability token `rm=2` in the `sync` reply only, see below); the page keeps the new cards disabled
until then. An unknown command is a counted reject even with the right password.

Capability token rule (McApp ask 1, 2026-10-06): `rm=<n>` is carried by the `sync` reply only,
`ok ctr=<hwm> v=<ver> rm=2`. `status` never carries it: its worst case is already 62 characters and
the token would push it past the 63 that older firmware and older McApp accept. Every future
capability token follows the same rule (one carrier, `sync`). A consumer treats the token as sticky
and resets it on a version change in `v=` or a verified `reboot`.

### 2.2 Buffers that must change together (W0, with `static_assert`s)

| Buffer                                           | Change                          |
| ------------------------------------------------ | ------------------------------- |
| `RmState.lastReply`                              | 64 -> `RM_MAX_RESULT + 1` (109) |
| `RmCmd.args`, `PendingCmd.args`                  | 24 -> 40                        |
| `RmSent.cmd` / `reply`                           | 40/72 -> 48/109                 |
| `RmLogEntry.cmd` / `result`                      | 40/64 -> 48/109                 |
| `sendReply` `wire`                               | 128 -> 144                      |
| `runConsole` `buf`                               | 32 -> 64                        |
| `RM_FORM_ARGS_MAX` (web parse)                   | 23 -> 39                        |
| `bookAndSend` `wire[96]`, `RM_FORM_BODY_MAX` 200 | stay                            |
| Python `ARGS_MAX` / `RESULT_MAX`                 | new, same values                |

Why this is wave 0 and not detail: with `lastReply[64]` left in place, a reply of 81 characters or
more overwrites `lockActive` and locks remote management for 6-25 days of uptime (reproduced in a
host harness on Xtensa and Cortex-M4 code; host clang -O1 hides it, so the regression test compares
`lockActive` as a byte). A missed `wire[128]` makes every 96-108 character result execute, get
cached and never be answered.

### 2.3 Reply grammar

- Single spaces; `key=value` tokens; `-` means absent; free text (names) is the last field.
- Calls in replies are upper case; the request stays lower case except as below.
- Allowed reply bytes: space, letters, digits, `- . / = + _ @ ? ( ) , * #`. Everything else becomes
  `?`, in one `rmSanitizeResult()` before the result is cached or sent. `:` is excluded because
  `:ack` anywhere in a DM is swallowed as an acknowledgement. `path`-style chains use `,` not `>`.
- Decimals are rounded, not truncated (a truncated float echo shows the frequency 1 kHz low on
  2-13 % of channels). Float `%f` is available on nRF52 in this build.
- Counters use `rmFmtCount()`: at most 5 characters (`99999`, then `NNNNk`, `NNNNM`).

### 2.4 Case

Names and comments need capitals. Decision: command names stay lower case; **arguments may
contain A-Z** (the HMAC covers the exact text; the page no longer lower-cases arguments). The risk
(an upper-case `ON` from a foreign sender becomes a counted reject instead of an uncounted drop) is
accepted. Free text is validated with a refuse-never-alter allowlist (4.3). One bench check:
`name Martin` from DK5EN-1 to DK5EN-92.

### 2.5 Resource budget (measured 2026-10-06 unless marked estimate)

| Env class                | State at HEAD (456e2138)                                                                                                                          | Verdict for this plan |
| ------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------- |
| `t_echo` (nRF52, -Ofast) | **Link fails: flash overflows by 9 344 B.** Built at 02314ce2 (before the RM page waves): 782 536 B, 96.0 %. The RM GUI waves added about 41.9 kB | **NO-GO until W-1**   |
| `heltec_t114`            | Links, 799 780 of 815 104 B (98.1 %), 15.3 kB free; the plan would add about 18-26 kB (estimate)                                                  | NO-GO as specified    |
| `wiscore_rak4631`        | 77.3 % (-Os since 2026-09-29, about 185 kB free); the older 96 % figure was the -Ofast build                                                      | GO                    |
| ESP32-S3, classic ESP32  | headroom large; E22_XML DRAM headroom about 19 kB (estimate, was 24.9 kB on 10-04); the buffer widening costs +772 B static RAM                   | GO, measure           |

W-1 outcome (2026-10-06): the cause was the framework's `-Ofast`, which only the RAK env overrode.
With `-Os` `t_echo` links at 594 904 B (73.0 %) and `heltec_t114` at 571 084 B (70.1 %), so both
boards are GO and the new commands are built on them. `tools/regression.sh` stage 1 now links the
three nRF52 board envs and fails above 95 % flash. Not linking the web layer on boards that cannot
start the web server (about 140 kB) is a backlog row, no longer needed for space.

Airtime (corrects the finder's figures, about 2x too low): at the default profile (preamble 32,
CR 4/6) one `mh` page costs 4.65 s of channel time, 24.4 s at the slow profile. An unanswered
request is sent four times through the DM retry ladder, which section 5 item 4 removes. The 1 %
duty-cycle premise is wrong (433 MHz has no limit, 869.525 MHz allows 10 %). A full MHEARD read of
20 nodes needs about 3 pages, 128 nodes about 22.

## 3. Command set (10 names)

All requests lower-case command names. "R" read, "W" write (same password, no roles). Reply
examples without the `ok ` prefix; lengths are worst-case results and full-wire lengths from
`verify_D_len.py` (budget 108 / 140).

| Cmd       | Args                       | R/W | Reply                                                                               | Worst result / wire | Notes                                                                                                 |
| --------- | -------------------------- | --- | ----------------------------------------------------------------------------------- | ------------------- | ----------------------------------------------------------------------------------------------------- |
| `radio`   | none                       | R   | `f=433.175 sf=11 cr=5 bw=250 p=10/22`                                               | 49 / 81             | Read only (4.1)                                                                                       |
| `txpower` | `<dBm>`                    | W   | `txpower=10` (existing reply kept)                                                  | -                   | Existing; page moves it into the Radio card; bound by the target's verified `p=<cur>/<max>`           |
| `name`    | none / `<text 1..19>`      | R/W | `n=Martin`                                                                          | 24 / 56             | Refuses `# : = / , { } ; %`, `none`; err `text`                                                       |
| `atxt`    | none / `<text 1..39>`      | R/W | `a=MeshCom Garten`                                                                  | 44 / 76             | Same allowlist; APRS comment also refuses `,` `/`                                                     |
| `pos`     | none / `<lat> <lon> <alt>` | R/W | `48.40812 11.73812 492 gps` (src `gps`, `nofix`, `set`)                             | 35 / 67             | Signed lat/lon from `node_lat_c`/`_c`; write range +-90 / +-180 / 0..40000, `err gps` while GPS is on |
| `sens`    | none                       | R   | `t=21.4 h=45 p=1013.2 t2=-`                                                         | 34 / 66             | Presence from the sensor flags, not from 0.0; `err unsupported` without a sensor                      |
| `mh`      | `<row>` (digits)           | R   | `23 41 DL2JA-2 0 DK5EN-98 12 ...` (total, next row or `-`, then call/minute pairs)  | 108 / 140 (7 rows)  | Paged by explicit requests; ages are minutes since last direct reception                              |
| `mh`      | `<call>`                   | R   | direct: `d g=1 m=1 r=-95 s=8 la=48.4231 lo=11.7871 di=4.0 a=499 n=15 x=14 h=18 t=0` | 93 / 125            | `x` only-it-hears, `h` it-hears, `n` its neighbour count, `t` minutes since heard; `-` for sentinels  |
| (same)    |                            |     | route: `r h=3 k=2 g=0 m=- rc=17.8@DL2JA-2 t=3 v=DK5EN-98,DL2JA-2`                   | 72 / 104            | Shortest route, `k` = number of routes; err `unknown`                                                 |
| `txq`     | none                       | R   | `q=3/20 bp=quiet tx=.. rt=.. dr=.. u=12`                                            | 52 / 84             | Counts only, never calls                                                                              |
| `mbox`    | none                       | R   | `m=heard u=12/50 b=1834 a=3/20 st=.. dl=.. ak=.. dr=.. bl=.. nt=..`                 | 87 / 119            | New `msgstoreBytes()`; `err unsupported` on classic ESP32 (T-Beam)                                    |
| `maxhop`  | none                       | R   | `t=4 p=2`                                                                           | 10 / 42             | Read only (4.2)                                                                                       |

Existing commands unchanged: `reboot status sendpos sendtrack sync gps track display led gateway
mesh setout txpower`. The allowlist grows from 13 to 22 names.

Data sources (all outside the web handler, extraction needed only for the gather helpers):
`nbrMhRows/nbrRowGet/nbrEdgeGet/nbrMhGet/nbrFind/nbrDistKm` (signed own position, the web handler
uses an unsigned one: do not copy), `nbrRouteGet` scan for routes, `msgstoreUsed/Counters` plus the
new `msgstoreBytes()`, sensor flags, `txRingPrioCounts/bpCurrentState`. Helpers `rmGather*` live in
`rm_runtime.cpp`; formatters are pure (4.5).

## 4. Design decisions

### 4.1 Radio parameters (SF, CR, BW, frequency): read-only in this campaign

The concept's RAM trial was reviewed and does not hold:

- every boot reloads the country preset on all nodes except country 7, so a "kept" change lasts
  only until the next restart (both bench nodes are EU8). The existing CLI `--txfreq/--txsf`
  already behave that way;
- the whole settings struct is saved by about 12 paths, so a RAM-only trial leaks to flash;
- the radio is reconfigured from the loop task with no check for a running TX/RX/CAD
  (SX1262 re-init on nRF52, SX127x forced standby on the T-Beam); this is already true of today's
  `--txpower` and must become a backlog item;
- a single node on other parameters is cut off from the mesh.

Decision (recommended, operator to confirm): `radio` is a read. Writes of SF/CR/BW/frequency are a
follow-up with the minimal safe design written down in `verify-F.md` (Option 3R: preset nodes only,
one combined command, switch only after the reply left the ring and the ACK or a 20 s grace, apply at
the ESP32 quiescent point, keep means "until restart"). IP-reachable ESP32 nodes keep the password-
gated console, RF-only nodes are changed on site. SF6 must never be offered (the SX1278 goes to
implicit header).

### 4.2 `maxhop` is read-only

Unauthenticated `{SET}` frames already set and save `max_hop_text` (known N-02, accepted WONTFIX), so
a remote write adds no protection boundary and raising it on a gateway multiplies airtime. `max_hop_pos`
is runtime-only. Read-only; write is an explicit operator choice.

### 4.3 Remote text is hostile until proven otherwise

`setname` and `setatxt` reach the node's own web pages unescaped today (stored XSS; the script runs
same-origin and can read `config.json`). Therefore, before these two writes ship:

- HTML-escape name and atxt at the page sinks (3 sites) with a source-lint test;
- one allowlist for free text (refuse, never alter) shared by the allowlist check and the sender;
- `rmSanitizeResult()` on every reply (2.3);
- the Messages and cards render with `textContent` only.

### 4.4 Allowlist and writes

An in-file table in `remote_cmd.cpp` (command, argument shape, class) replaces the hand-written
switch; the Python twin and the golden vectors are the drift guard (no code generation, no new setter
refactor). Non-radio writes go through `runConsole("--setname ...")` with read-back, pre-validated by
the allowlist shape. The allowlist checks syntax only; ranges and charset live in `execute()` as
tagged `err range` / `err text`, which do not count toward the lockout.

### 4.5 Pure formatters

Every reply is built by a pure function over a plain input struct into a bounded buffer, in a header
next to `rmFormatStatus`, tested in `native_rm_sender_policy`. Worst cases are domain-bounded
(9-character calls, 5-character counts, negative RSSI/coordinates); one distinct sentinel per field
catches field-position errors.

## 5. Sender policy (replaces the draft "3 s / 10 per 90 s")

The draft does not compile (3 s spacing breaks the `static_assert` on the receiver's 10 s limit) and,
with the assert removed, locks the target with the right password in 44 % of simulated 3-hop runs (the
older of two reordered frames counts as a replay strike). Final design:

1. Spacing stays 10 s = `RM_RATE_MS`; the assert stays.
2. Window 150 s = the receiver's 90 s reject window + 30 s path delay + 30 s margin for TX-queue
   latency (an assert keeps it at or above the receiver window + 60 s). The first version of this
   wave used 120 s; the advisor showed zero margin at a 30 s path delay.
3. Per target, over all keys: 2 unanswered while the current key is unproven; 10 once a reply under
   that key's fingerprint verified (fingerprint and flag in RAM only).
4. `RM1 ` frames go out without the `{NNN` suffix (no DM ACK, no retry ladder), from any origin:
   the DM send path treats a payload starting with `RM1 ` like `{CET}`/`{MCP}`/`{SET}`
   (`user_msg_status = 0xFF`), whether it comes from the Remote page, BLE (0xA0) or Extern-UDP
   (McApp ask 2: McApp is a second RM1 sender and cannot see late retry keyings). This is the
   precondition for 3: a retried stale frame is otherwise a replay strike.
5. Sent book 12 slots, never evicts a counted entry, and refuses with `busy` before a counter is used.
6. "Reset on new password" becomes: per-target forget when a saved node slot is written or forgotten
   (drops pending chain, chain error, proof; wipes keys on delete). Never touches the persisted send
   counter, learnt counter marks or any receiver state. Own-password change calls
   `rmReceiverUnlock()` (lock flag and reject count only).
7. Rate rejects never count toward the lockout; the page shows the countdown, not "locked".
8. Receiver (advisor rework, W1): a frame with a valid tag and a stale counter (a replayed or
   overtaken frame) stays rejected but is no longer counted toward the lockout. The lockout throttles
   key guessing, and a valid tag is not a guess. Before this, a sender with the right password and
   reordered frames (path delays alternating 30 s and 0 s) locked the target within 70 s, and anyone
   could lock a node by replaying one sniffed frame three times. Rejects before the tag check and
   wrong tags are counted as before.
9. The budget of 10 applies only to a target that reported the new receiver: `rm=2` in a verified
   `sync` reply under the current key (RAM only, cleared by a key change or a forget). Every other
   target keeps 2, which is safe against the old receiver that still counts replays. A cap on
   "overtaken" sends was tried first and dropped: simulation showed it does not protect an old
   receiver (frames in flight are not counted) and refuses a healthy operator in 25-40 % of attempts
   at 20 % reply loss.
10. After a re-key the page offers one "try once more anyway" beyond the unproven limit, without a
    warning dialog (operator decision; a wrong password then locks the target for 5 min).

## 6. Page changes

| #   | Change                                                                                                                                                                                                                                                                                                                                                             |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| P1  | Lock label only while the node really blocks commands: "Remote commands blocked for N s after wrong attempts"                                                                                                                                                                                                                                                      |
| P2  | Clear password -> rm off, persisted; confirm and result texts                                                                                                                                                                                                                                                                                                      |
| P3  | Toggle and label hidden while no password and rm off; hint stays                                                                                                                                                                                                                                                                                                   |
| P4  | Messages card under "Choose a node": time, command, state badge (sent / verified / no answer), reply, Run again. Rows update in place. Activity, "Last status" and the "Waiting" line are removed (three lists would otherwise coexist)                                                                                                                            |
| P5  | TX power card removed; power in the Radio card                                                                                                                                                                                                                                                                                                                     |
| P6  | Advanced `open` on load                                                                                                                                                                                                                                                                                                                                            |
| P7  | One `rmDefs` table (label, write shape, parse, ok text, fields), generic key=value parser, one card renderer. Cards: Radio (read, power stepper), Identity (19/39 counters, forbidden-character hint before send), Position, Sensors, Network (MHeard driver with progress and Stop, Details per row, call field for non-direct nodes; TX queue; Mailbox; Max hop) |
| P8  | Sentences for the new error tokens: `range text unknown unsupported end busy`                                                                                                                                                                                                                                                                                      |
| P9  | Retry countdown from the target, no fixed 10 s in sentences                                                                                                                                                                                                                                                                                                        |
| P10 | Messages depth: stream `/rmstatus` entries (no 12-slot struct on the stack); list of targets beyond 4; deeper history client-side                                                                                                                                                                                                                                  |
| P11 | Capability gate: new cards disabled until the target reports `rm=2`; "stored as typed" case note                                                                                                                                                                                                                                                                   |
| P12 | `tools/webgui_rm_test.js` in `tools/regression.sh`, id regex `[a-z0-9_]+`, size budget, fake server mirrors G2, worst-case 108-character reply row                                                                                                                                                                                                                 |

## 7. Test plan

- Native: formatters and budget (`strlen(wire) <= 140`, byte-compare `lockActive`), sanitiser and text
  allowlist vectors (`<>"'&`, `a:ack5`), pure sender book (extracted header-only so a native env can
  compile it), real policy against the real receiver in virtual time with wrong key at the fastest
  allowed rate and path delay 0..30 s (red today, regression for the 90 s edge), per-target forget,
  `sync` limiter.
- pytest: allowlist and vectors for every new command, reply grammar, Python/C++ twin parity.
- jsdom: G1-G6 regressions (each fails before), Messages state transitions, capability gate, 108
  character row.
- Bench (DK5EN-1 Heltec V3, DK5EN-92 T-Beam; group TEST or own DM only, <= 2 dBm, identity guard):
  every read, every write, `mbox` on DK5EN-1 (T-Beam answers `err unsupported`), `mh` paging against
  live data, `name Martin` capitals, wrong password at the fastest allowed rate (assert two tag
  rejects and no lockout), then the right password. Every read is compared with `--mheard`, `--info`,
  `--path` of the same node as an oracle. No radio write is exercised.

## 8. Waves (proposal)

| Wave | Content                                                                                                                                                                                       |
| ---- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| W-1  | Restore the T-Echo link (fails at HEAD by 9 344 B) and give heltec_t114 headroom: do not link the RM web layer on boards that cannot serve it, or trim; measure both with release flags (2.5) |
| W0a  | `sync` replay fix (own limiter 60 s, never blocks commands) + test; separate commit, separate upstream PR                                                                                     |
| W0b  | Buffers and `static_assert`s, `RM_MAX_RESULT 108`, table allowlist, reply sanitiser, `rm=2` token, Python twin, vectors, gate the jsdom suite                                                 |
| W1a  | Pure formatters and `rmGather*` helpers with native tests (new files)                                                                                                                         |
| W1b  | XSS escaping at the three sinks, text allowlist, `pos`/`name`/`atxt` write validation, `msgstoreBytes()`                                                                                      |
| W1c  | Sender policy (book 12, proof, 120 s, no `{NNN`, per-target forget, receiver unlock) with the real-policy-vs-receiver test                                                                    |
| W1d  | Page fixes P1-P6, P9 with jsdom regressions                                                                                                                                                   |
| W2   | `execute()` dispatch glue, page cards P7, P8, P10-P12                                                                                                                                         |
| W3   | Gate, release-flags size build, advisor, flash and bench both nodes, RAK compile                                                                                                              |

Hotspots stay orchestrator-owned: `rm_runtime.cpp`, `remote_cmd.cpp`, `command_functions.cpp`,
`web_functions.cpp`, `platformio.ini`. One `pio` process at a time through `scratchpad/pio_locked.sh`.

## 9. Operator decisions (2026-10-06)

1. Radio SF/CR/BW/frequency: read-only.
2. `maxhop`: read-only.
3. Public replies: accepted; the `pos` read answers only when the node already beacons its position.
4. Sender limit: 10 s spacing; 2 unanswered while the key is unproven, 10 once it is proven.
5. "Try once more anyway" after a re-key: yes, one extra attempt, no warning dialog.
6. MHEARD window for the `mh` list: 3 h.
7. Flash (2.5): `-Os` on `t_echo` and `heltec_t114`; the new commands are built on all boards.

Implementation plan and wave status: `extended-commands-impl-plan.md`.
