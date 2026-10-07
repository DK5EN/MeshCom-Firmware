# Remote management extended commands - implementation plan

Status: APPROVED (2026-10-06), in progress; wave status in section 7. Builds on `extended-commands-concept.md` (draft 2)
and `extended-commands-verdict.md`. The operator decisions are in section 6 (`[Dn]`).

## 1. Flash overflow: cause and fix

### 1.1 Measured (2026-10-06, HEAD 91d378cb, trial edits reverted)

| Env           | As configured (`-Ofast`)                | With `-Os`        | Free with `-Os` |
| ------------- | --------------------------------------- | ----------------- | --------------- |
| `t_echo`      | link fails, 9 344 B over 815 104 B      | 594 904 B, 73.0 % | 220.2 kB        |
| `heltec_t114` | 799 780 B, 98.1 %                       | 571 084 B, 70.1 % | 244.0 kB        |
| RAK4631       | (already `-Os` since 2026-09-29) 77.3 % | -                 | about 185 kB    |

Cause: the Adafruit nRF52 framework adds `-Ofast` to every build. The RAK4631 env overrides it
(`build_unflags = -Ofast`, `build_flags = -Os`, `variants/wiscore_rak4631/platformio.ini:25-37`,
upstream since 4.40a). `t_echo` and `heltec_t114` never got that override and were at 96 % and
98 % before the RM page commits; those commits only pushed the T-Echo over the edge.

### 1.2 Options

| Option                                          | Gain (measured or attributed)                                                             | Cost and risk                                                                                                            |
| ----------------------------------------------- | ----------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| A: `-Os` on both envs (recommended)             | 204 kB on T-Echo, 229 kB on T114 (measured)                                               | 3 lines per env, the RAK precedent. No T-Echo or T114 on the bench: display and timing are not hardware-verified by us   |
| B: do not build the web layer on web-less nRF52 | about 140 kB in an `-Os` image (web code about 40 kB, page strings about 98 kB, Ethernet) | New macro, guards in `nrf52_main.cpp` and every caller, a second code shape to keep compiling; also untested on hardware |
| C: A now, B later as a hygiene item             | A, then B on top                                                                          | B is no longer needed for space; it stays a backlog row                                                                  |

Attribution in the T-Echo `-Os` image (linked symbols per object plus string literals):
`web_functions.cpp` 30.1 kB code and about 70 kB strings, `web_rm_page.cpp` 0.9 kB code and 26.4 kB
strings (the scaffold JS), `web_setup.cpp` 6.2 kB, `web_rm_handlers.cpp` 2.0 kB, `nrf_eth.cpp` 5.0 kB.
The web server can never start on these boards: `startWebserver()` is only reached behind
`neth.hasETHHardware` (`src/nrf52/nrf52_main.cpp:1184-1197`, `2571-2594`), a runtime flag.

Recommendation: option C. `-Os` restores the link with one known mechanism, ends `-ffast-math` on
both boards (the NaN checks that `-Ofast` folds away work again) and leaves room for the whole
campaign. Option B becomes a backlog row.

### 1.3 Gate gap

`tools/regression.sh` compiles no board firmware at all (stage 1 walks `[env:native*]` only); the
RM GUI board compiles were manual and skipped these two envs. W-1 adds a board link step with a
flash ceiling so a full or failing image turns the gate red.

## 2. Waves

Model tiers: `scout` (Haiku) for recon, `implementer` (Sonnet) for writer briefs, orchestrator for
hotspot integration and gates, `/fable-review` advisor pass before every behavioural commit.
One `pio` process at a time: writers run only their own native env; board builds belong to the gate.

Orchestrator-owned hotspots (no writer edits them inside a parallel wave): `src/rm_runtime.cpp`,
`src/remote_cmd.cpp`, `src/command_functions.cpp`, `src/web_functions/web_functions.cpp`,
`platformio.ini`, `tools/regression.sh`.

| Wave | Content                                                                                                                                                                                                        | Writers                          | Gate additions                                                          |
| ---- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------- | ----------------------------------------------------------------------- |
| W-1  | `-Os` on `t_echo` and `heltec_t114`; board link step in `tools/regression.sh` (t_echo, heltec_t114, wiscore_rak4631, flash ceiling); concept 2.5 corrected; backlog row B                                      | orchestrator                     | both envs link, flash below ceiling `[D2]`                              |
| W0a  | `sync` replay fix: own limiter 60 s, never blocks commands; regression test red before, green after                                                                                                            | 1 implementer (serial, hotspots) | `native_remote_cmd`, pytest twin                                        |
| W0b  | Buffers and `static_assert`s (concept 2.2), `RM_MAX_RESULT` 108, table allowlist, `rmSanitizeResult()`, `rm=2` token in the `sync` reply, Python twin and vectors incl. McApp ask 3, jsdom suite into the gate | 1 implementer (serial, hotspots) | `lockActive` byte-compare test, `strlen(wire) <= 140`, jsdom in stage 2 |
| W1   | Four parallel writers on disjoint new files, hotspot edits applied by the orchestrator at the gate (section 3)                                                                                                 | 4 implementers                   | each writer's own native env or jsdom; whole gate after                 |
| W2   | `execute()` dispatch glue (orchestrator), page cards P7, P8, P10-P12 (1 implementer on `web_rm_page.cpp` and the jsdom test)                                                                                   | orchestrator + 1 implementer     | full gate, board link step                                              |
| W3   | Full gate `--stage 1,2`, size build on all RM boards incl. E22_XML-DevKitC, advisor on the campaign diff, flash and bench DK5EN-1 and DK5EN-92, RAK compile, docs, backlog                                     | orchestrator                     | bench list of concept section 7                                         |

Commit per wave on `fork-dev`, explicit paths, after the gate and the advisor pass. No push, no
release, no upstream PR without a separate request.

## 3. W1 ownership (parallel, disjoint; as dispatched 2026-10-06)

| Writer | Exclusive files                                                                                                                                                                                                                              | Verification slot                                                |
| ------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------- |
| W1a    | new `src/rm_format.h` (pure formatters, `rmFmtCount`, input structs, reply contract table), new `test/test_rm_format/`                                                                                                                       | `native_rm_format`                                               |
| W1b    | new `src/rm_text.h` (text allowlist, pos parse, argument shapes, `rmHtmlEscape`), new `test/test_rm_text/`, `web_functions.cpp` (escape at the sinks only), `msgstore.cpp` / `msgstore_api.h` (`msgstoreBytes()`), `test_web_escape_lint.py` | `native_rm_text`, pytest, 2 board builds                         |
| W1c    | `rm_sender_policy.h`, new `rm_sent_book.h`, `rm_runtime.cpp/.h` (sender half), `web_rm_handlers.cpp/.h`, `web_rm_parse.h`, `command_functions.cpp` (`nodePasswdApply` only), `test_rm_sender_policy/`, new `test_rm_policy_rx/`              | `native_rm_sender_policy`, `native_rm_policy_rx`, 3 board builds |
| W1d    | `web_rm_page.cpp`, `tools/webgui_rm_test.js` (P1-P6, P9)                                                                                                                                                                                     | `node tools/webgui_rm_test.js`                                   |

The three native envs (`native_rm_format`, `native_rm_text`, `native_rm_policy_rx`) were added to
`platformio.ini` by the orchestrator before dispatch. Since no other W1 writer touches
`rm_runtime.cpp`, W1c owns it for this wave; the `execute()` dispatch for the new commands (the
`rmGather*` helpers and the allowlist rows) is W2. The "try once more anyway" one-shot (D10) is in
W1c on the server side (`force=1` on `/rmsend`, `canForce` in the JSON); its button is W2.

## 4. Verification rules for every wave

- A fix ships with a test that fails before and passes after; the report shows both outputs.
- Positive existence check in the artifact for guarded code (symbol or string scan of the ELF).
- Size figures only from a clean, sequential build at the gate.
- Bench rules of `CLAUDE.md` apply (group TEST or own DM, own callsigns, at most 2 dBm, identity
  guard, `lsof` before a port).
- The docs (`extended-commands-concept.md` status, this plan's wave table, `BACKLOG`) are updated
  after each wave, before the next dispatch.

## 5. Risks

| Risk                                                                     | Handling                                                                                           |
| ------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------- |
| `-Os` on T-Echo and T114 without hardware on the bench                   | Same SoC, SoftDevice and SX1262 path as the RAK, which runs `-Os` since 09-29; field tester `[D3]` |
| The RM core is not upstream (`upstream/dev` has no `src/remote_cmd.cpp`) | The `sync` fix cannot be a stand-alone upstream PR; it rides with the RM upstream PR `[D4]`        |
| Version skew between old and new RM firmware                             | `rm=2` capability token; existing replies stay at 63 characters or less                            |
| Hotspot conflicts in W1                                                  | Writers own new files only; orchestrator integrates                                                |
| E22_XML DRAM headroom (about 19 kB, estimate)                            | Measured in W0b and W3 with release flags                                                          |

## 6. Decisions (operator, 2026-10-06)

| #   | Decision                                        | Outcome                                                                                  |
| --- | ----------------------------------------------- | ---------------------------------------------------------------------------------------- |
| D1  | Flash fix                                       | `-Os` on both envs now; dropping the web layer on web-less nRF52 boards is a backlog row |
| D2  | Flash ceiling in the gate for nRF52 board links | fail above 95 %                                                                          |
| D3  | T-Echo and T114 hardware verification of `-Os`  | ship, no follow-up                                                                       |
| D4  | `sync` fix                                      | plain commit on `fork-dev`; goes upstream inside the later RM PR                         |
| D5  | New commands on T-Echo and T114                 | yes, same code on all boards                                                             |
| D6  | Radio parameters (SF, CR, BW, frequency)        | read-only                                                                                |
| D7  | `maxhop`                                        | read-only                                                                                |
| D8  | `pos` read on the public server                 | answers only when the node already beacons its position                                  |
| D9  | Sender limit                                    | 2 unanswered while the key is unproven, 10 once proven; spacing 10 s                     |
| D10 | "Try once more anyway" after a re-key           | yes, one extra attempt, without a warning dialog                                         |
| D11 | MHEARD window for `mh`                          | 3 h, like the web page                                                                   |
| D12 | Scope of the go                                 | whole campaign through the bench; stop before release, no push                           |

## 7. McApp asks (2026-10-06)

Source: `~/Desktop/2026-10-06_mcapp-rm1-firmware-asks.md` (McApp is a second RM1 sender over BLE and
Extern-UDP). All three are accepted.

| Ask | Content                                                                                                                                                                                                                        | Wave                                                       |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------- |
| 1   | `rm=2` in the `sync` reply only (`ok ctr=<hwm> v=<ver> rm=2`); `status` never carries a capability token; rule stated in concept 2.1                                                                                           | W0b                                                        |
| 2   | No `{NNN` suffix and no retry ladder for any DM whose payload starts with `RM1 `, from the Remote page, BLE or Extern-UDP; native test                                                                                         | W0c (new, `loop_functions.cpp`)                            |
| 3   | Vectors: compact `status` replies (6-letter with LED, 5-letter without, 62-character worst case) and the `sync` reply with the token, each with the fields `rmStatusParse` produces; native check that parses every JSON reply | W0b (items 1-4), W2 (one worst-case reply per new command) |
| ref | Names for the `..` counters of `txq` and `mbox` are fixed by the W1a formatters; `busy` stays a refusal of the firmware's own sender only                                                                                      | W1a, concept                                               |

## 8. Wave status

| Wave | Status                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| W-1  | done 2026-10-06: gate 64 envs / 2115 cases green, selftest 48 green, board links 73.0 / 70.1 / 77.3 %; no advisor pass (build flags and gate script only)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| W0a  | done 2026-10-06: `sync` has its own 60 s limiter (`RM_SYNC_RATE_MS`), 3 new native tests red before / green after, advisor APPROVED. Residual, accepted: a manual re-sync inside 60 s is dropped silently; the limiter is per target, not per sender                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| W0b  | done 2026-10-06: `RM_MAX_RESULT` 108, args 39, all buffers tied by `static_assert` (cmd buffers 56, not 48: 15 + 1 + 39 + NUL), table allowlist, `rmSanitizeResult()` before cache and send, `rm=2` in the `sync` reply, `rmCapLevel()`, 4 new reply vectors, jsdom Remote suite in stage 2. Lock-state test red with the old 64-byte buffer. Gate 64 envs / 2128 cases, advisor: no finding in W0b                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| W0c  | done 2026-10-06: `dmTextIsRm1Frame()` / `dmTextNoRetransmit(text, isDM)` in `dm_text_escape.h`; an `RM1 ` DM gets no `{NNN` and ring status 0xFF from any origin; group texts unaffected (advisor rework). On-air proof is a bench item in W3: `[NEW-TXT]` line without a trailing `{NNN`, no ACK frame at the receiver                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| W1   | done 2026-10-06: formatters (`rm_format.h`), text validation and XSS escaping incl. over-the-air sinks (`rm_text.h`, `web_functions.cpp`, lint), sender policy (window 150 s, budget 2 / 10 with the `rm=2` capability gate, 12-slot book, per-target forget, one forced attempt after a re-key, receiver unlock, password clear switches RM off), receiver no longer counts authenticated replays, page fixes P1-P6 and P9 with Run again. Two advisor passes with rework. Gate 67 envs / 2170 cases, 785 pytest, 142 page checks. Carried into W2: `/rmstatus` lists only 4 targets and 5 sent entries (P10); Run again by slot should also post the call so a slot changed in another tab is refused; BLE settings write can leave an empty password with RM on (harmless, backlog)                                                                                                                                                                                          |
| W2   | done 2026-10-06: 22 command names in the allowlist (args keep their case), read executors `rm_exec_read.cpp` and write executors `rm_exec_write.cpp` behind `rm_exec_ext.h`, Python twin and vectors (32 commands, 25 replies, longest 108 / 140), `/rmstatus` streamed (12 sent entries, all targets), slot guard `call=`, page cards (Radio, Name, APRS text, Position, Sensors, TX queue, Mailbox, Max hop, heard list with driver and details), capability gate, forced attempt. Advisor: firmware side approved, page side reworked (forced attempt and slot sends bound to their node, run stops on page leave). Gate 67 envs / 2176 cases, 816 pytest, 238 page checks. Notes: `pos` answers `err hidden` exactly when no position is set (the firmware has no other beacon switch); `txq` reports `rt` and `u` as 0 (no counter exists) and `tx`/`dr` per statistics interval; routed `mh` never carries `rc`; no host test env for the two executor files (bench only) |
| W3   | done 2026-10-06, second bench round in section 9 (two findings open): gates green on 3ffa916f (67 envs / 2176 cases, 816 pytest, 238 page checks; flash t_echo 76.4 %, heltec_t114 73.5 %, RAK4631 80.7 %, Heltec V3 53.3 %, T-Beam 56.6 %, E22_XML 56.6 % with RAM 20.2 %); both nodes OTA-flashed and benched DK5EN-92 to DK5EN-1 (section 9)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |

## 9. Bench result (2026-10-06) and what is still open

Setup: DK5EN-1 Heltec V3 (192.168.68.56) and DK5EN-92 T-Beam (192.168.68.57), both OTA-flashed with
the 3ffa916f build (`tools/webflash.py`), identity guard passed before and after (own calls,
2 dBm). Sender DK5EN-92 through its web API (`/rmsend` with the saved slot of DK5EN-1 and
`call=DK5EN-1`), target DK5EN-1, DMs on air. 192.168.68.62 is the production node DK5EN-98: never
a target; `tools/webflash.py` without a host argument targets dk5en-98.local.

| Check                        | Result                                                                                                                                                             |
| ---------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `sync`                       | `ok ctr=... v=4.40a rm=2`; the sender then reports `cap` 2 for the target                                                                                          |
| `status`                     | `ok v=4.40a up=1 bat=100 heap=118 s=gtdMwl p=2/22 led=0` (54 characters, legacy form intact)                                                                       |
| `radio`                      | `ok f=433.175 sf=11 cr=6 bw=250 p=2/22`                                                                                                                            |
| `name`, `atxt`, `pos` (read) | `ok n=Martin`, `ok a=MeshCom Freising`, `ok 48.40760 11.73840 485 set`                                                                                             |
| `sens`                       | `err unsupported` (no sensor on the Heltec)                                                                                                                        |
| `maxhop`, `txq`, `mbox`      | `ok t=4 p=2`; `ok q=2/20 bp=quiet tx=1 rt=0 dr=0 u=0`; `ok m=off u=0/50 b=0 a=0/20 st=0 dl=0 ak=0 dr=0 bl=0 nt=0`                                                  |
| `mh 0`                       | `ok 3 - DK5EN-92 0 DK5EN-98 1 DL2JA-2 6`; same three calls as the target's own `/rmheard`                                                                          |
| `mh <call>`                  | direct: `ok d g=0 m=1 r=-41 s=6 la=- lo=- di=- a=- n=3 x=0 h=2 t=0`; lower-case argument accepted; unknown call `err unknown`; `mh 5` `err end`                    |
| Writes                       | `name Bench Node 1`, `atxt Bench Test (W3)`, `pos 48.40812 11.73812 492`: each answered with the stored value, read back, restored; `pos 91 0 0` `err range`       |
| Persistence                  | `name Bench Node 2`, remote `reboot`, `name` after the reboot still `Bench Node 2`; restored to `Martin`                                                           |
| `sync` limiter               | a second `sync` inside 60 s stayed unanswered                                                                                                                      |
| Wrong password               | first wrong-key frame sent, second refused locally with `limit` and `canForce` 1 (an unanswered `sync` already counted); one wrong-key frame = one reject; no lock |
| Right password afterwards    | works after the window; the target's capability is learned again by the next `sync`                                                                                |
| `/rmstatus` depth            | 12 sent entries listed                                                                                                                                             |
| Loss                         | 3 of about 40 frames stayed unanswered (2 dBm next to the 22 dBm production node); with no retry ladder a lost frame shows as "no answer" and is repeated by hand  |
| Reply latency                | 9 to 39 s per command                                                                                                                                              |

### Second bench round (2026-10-06 evening)

Setup: fresh passwords on all three bench nodes (outside the repo, `~/MeshCom-bench-backups/`), net
console logs of DK5EN-1 and DK5EN-92 for the whole run, RAK4631 DK5EN-90 flashed over USB, identity
guard on all three before and after. Remote management is back to its state from before the run
(DK5EN-1 on, DK5EN-92 and DK5EN-90 off).

| Check                        | Result                                                                                                                                                                                                             |
| ---------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Wire, no message number      | 334 logged `RM1 ` frames in both directions, none with `{NNN`; an ordinary DM in the same run carried `{522` and was answered with `:ack522`, no ACK frame for any `RM1 ` frame                                    |
| Password clear               | `/rmpasswd` clear: `[RM];off;passwd_cleared`, `/rmstatus` `on` 0 and `pw` 0, a command afterwards stays unanswered; a new password alone leaves it off, the switch brings it back                                  |
| Replay                       | accepted frame sent again with its counter equal to the mark: `[RM];cached`, the stored reply once per 10 s; with the counter below the mark: three times `[RM];reject;replay`, no lockout, the next command works |
| `pos` with GPS on            | write `err gps`, read `ok 48.40760 11.73840 480 gps`; GPS off and the position restored                                                                                                                            |
| Forced attempt               | after the slot was re-saved with the new password: plain send refused `limit` with `canForce` 1, `force=1` goes out once, `canForce` back to 0 (the frame itself met the lockout of finding 1)                     |
| Reverse, DK5EN-1 to DK5EN-92 | `sync` `rm=2`, `status`, `radio` (`p=2/20`), `name`, `atxt`, `pos` (`gps`), `maxhop`, `txq`, `mh 0`, `mh DK5EN-1`; `mbox` and `sens` `err unsupported` on the T-Beam                                               |
| RAK4631 as target            | `sync` `rm=2`, `status`, `name` and `atxt` empty as `-`, `pos`, `maxhop`, `txq`, `mbox` (`m=list u=0/50 ...`), `mh 0`, `mh <call>`, `sens` `err unsupported`, `atxt` write with read-back, restored                |
| RAK4631 `radio`              | bug: `f=999.999 ... cr=2` (the nRF52 stores Hz and a coding-rate index); fixed (`rm_radio_in.h`, regression test in `test_radio_units`), reflashed: `ok f=433.175 sf=11 cr=6 bw=250 p=2/22`                        |
| Remote page in Chrome        | DK5EN-1 managing DK5EN-92: cards filled from the sent book, a card read, Run again with its two-tap confirm, heard-list reader ("Read 4 of 4"), Details of one row; no console error                               |
| Escaping of received text    | a DM `<b id=...><img src=x onerror=...>&amp;` shows literally on the Messages page of the receiver; no element created, no handler run                                                                             |

Findings of this round, both decided by the operator and fixed the same evening:

1. RM-PROOF: a sender whose key was proven kept the budget of 10 after the TARGET got a new
   password. DK5EN-92 sent five old-key frames in 4 minutes and DK5EN-1 locked itself for 5 minutes
   (three `reject;tag` inside 90 s). Fix: two sends in a row without a verified reply put the
   target back on the budget of 2 until a reply verifies again (`rmPolicyLimit()`); native
   simulation of the bench case in `test_rm_policy_rx`.
2. RM-GWRELAY: a command whose first copy arrived through a gateway node's relay was dropped
   without a marker, because the gateway sets the server flag on what it relays and the receiver
   took the flag as "delivered by the server" (RM-D6). Decision: remote management also works over
   the internet path. Fix: one receive hook `rm_rx_gate.h` without the server-flag rule, called
   from `OnRxDone` and from the server ingress of both platforms (a gateway node now takes commands
   and replies straight from the server); twin test and source lint.

Hardware check of the two fixes (all three nodes on 0e938a49): DK5EN-92 proven for DK5EN-1 with
`rm=2`, DK5EN-1 re-keyed, five tries with the old key: two frames sent, three refused with `limit`,
the target counted two rejects and did not lock. The server path is not benched: in 23 commands no
first copy came through the gateway relay, and no bench node is a gateway (that needs the mock
server or the operator's go for the real one); it rests on the twin tests.

Third change of the evening (operator decision): the reject counter and the lockout are per sender
callsign-SSID, independent of the path (`RmRejSrc` in `remote_cmd.h`, 6 senders). Junk under other
calls can no longer lock the operator out, which was the open availability point of the server
path. `/rmstatus` `lock` means "at least one sender is locked".

Hardware check of the per-sender lockout (all three nodes on a262b304): three wrong-tag `RM1` DMs
under DK5EN-90's call locked that call at DK5EN-1 (`/rmstatus` `lock` 1), and a command from
DK5EN-92 sent during the lock was executed and answered.

Also seen: the receiver's reject window is a fixed 90 s window that starts with the first reject,
not a sliding one (four rejects spread over 158 s did not lock).

Still open:

1. Hostile node NAME or APRS text on the pages: not benched, both travel in the position beacon,
   which is a broadcast; covered by the host lint and tests only.
2. T-Echo and T114: no hardware. The `radio` fix applies to both.
3. `/rmstatus` and the page on the RAK4631 as SENDER (it was target only).

## 10. Gateway bench, server path (2026-10-07)

Both bench nodes (DK5EN-1 Heltec V3, DK5EN-90 RAK4631) as gateways against a mock server on the rpizero; toolbox
`tools/bench/gwbench/`, full evidence in `docs/rm-gui/gateway-bench-20261007.md`.

| Result                                                                                                                  | Evidence                                                  |
| ----------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------- |
| Commands and replies travel over the server path; the server copy was first in 40 of 40 commands and 37 of 37 replies   | RM-GWRELAY works end to end                               |
| Latency of an RM command about 2.2 s (driver poll floor), RF copy needed a median 12.5 s (RAK sender) to 113 s (Heltec) | Heltec TX queue wait dominates                            |
| No counter executed twice on either node; the second copy is a ring duplicate, `cached`, `replay` or `rate`             | 34 / 7 / 23 / 2 copies                                    |
| Plain DM accepted once (RF copy suppressed by `[DMDUP]`), group message once, server ACK at the sender after 1 s        | RAK to Heltec only; Heltec to RAK not sent (backpressure) |

Findings:

- **RM-DUP (fixed 2026-10-07):** a server-ingress DM to the own call creates no dedup entry and RM frames carry no
  `{NNN`, so the RF copy of a rejected frame was counted again. One junk frame cost two strikes; the lock came
  after the second junk frame, not the third. Valid frames were not affected. Fix: a duplicate ring in
  `rm_queue.h` (`rmqSeenBefore`: source call, message id and a hash of the text, 8 slots, 60 s) used by
  `rm_rx_gate.h`; the second copy of the same frame is consumed before the queue. The text hash is part of the key
  because message ids are predictable: a spoofed frame with the next id must not swallow the real command. Tests:
  `test_regression_same_rm_frame_twice_is_queued_once_on_both` (red before), five ring and gate cases in
  `test_remote_cmd`. Not yet benched on hardware.
- **Bench driver holes:** the Heltec TX backpressure refused the three junk frames and the Heltec plain DM and group
  message in the 1 to 90 direction, so those cases tested nothing (the analyzer shows it). Rerun needed with a
  drained TX ring.
- **Write-back of an empty value:** an empty name or text reads as `-`, the write validator accepts `-`, so the
  driver stored a literal `-` on the RAK (driver artefact). Restore with `--setname none` and `--atxt none`.
- **Lost reply** when the TX ring is full (`RING_DROP_NEW`); the sender sees `noanswer`. The `rate` limit counts
  from the end of a flash-saving write.
- Not established: why the Heltec TX ring waits minutes while the RAK waits seconds in gateway mode; why `txq`
  reports `q=30/20`.
