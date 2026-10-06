# Remote management extended commands - Fable verdict

Date 2026-10-06. Subject: `extended-commands-concept.md` (draft 1). Method: eight independent
finders (protocol, security, concurrency, DRY/altitude, efficiency, test audit, UX, requirement
traceability), then six adversarial verifiers on the session model, one per cluster. Verifier
reports (working files, session scratchpad): verify-A wire and buffers, verify-B security,
verify-C sender policy, verify-D page/tests/data, verify-E resources, verify-F radio trial.
Counts: A 39 confirmed / 3 refuted / 3 plausible of 45; B 17 / 0 / 3 of 20 plus 3 new; C 27 / 18 /
3 of 48 plus 5 new; D 33+11 / 3 (+8 partly) / 9 of 64 plus 19 new; F 14 of 14 confirmed or
corrected plus 5 new.

## Findings that changed the concept

| #   | Sev      | Finding                                                                                                                                                              | Outcome in draft 2                               |
| --- | -------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------ |
| 1   | Critical | `RM_MAX_RESULT` 108 with `RmState.lastReply[64]` overwrites `lockActive`: a reply of 81+ chars locks RM for 6-25 days (host harness, Xtensa and Cortex-M4)           | W0b buffers + `static_assert`, byte-compare test |
| 2   | High     | A captured `RM1 0 sync` frame replays forever on the already flashed code, blocks commands for 10 s each time (100 % denial in harness, 360 replies/h)               | W0a separate fix and PR                          |
| 3   | High     | Draft section 5 (3 s spacing) breaks `static_assert` at `rm_runtime.cpp:20`; without it 44 % of 3-hop runs lock the target with the right password                   | Section 5 rewritten                              |
| 4   | High     | "10 unanswered per 90 s" can never trigger (5-slot shared book, 8-entry policy cap, 10 s spacing); 90 s window edge already locks 357/1000 wrong-password runs today | Window 120 s, book 12, proof-based cap 10        |
| 5   | High     | `rkeep` cannot work: every boot reloads the country preset on all nodes but country 7; trial leaks via ~12 `save_settings()` paths; radio re-init has no TX/RX check | Radio read-only, follow-up spec kept             |
| 6   | High     | Stored XSS: remote name/atxt reach own web pages unescaped, same-origin script reads `config.json`                                                                   | 4.3, W1b                                         |
| 7   | High     | Names with capitals need a protocol decision; any escape scheme breaks the 39-char comment budget; page lower-cases args                                             | A-Z allowed in args                              |
| 8   | High     | `mbox` example 169 chars (full counters) exceeds the budget; T-Beam has no mailbox                                                                                   | `rmFmtCount`, `err unsupported`                  |
| 9   | High     | Version skew: replies > 63 chars are rejected by older operator firmware; unknown command is a counted reject                                                        | `rm=2` capability gate                           |
| 10  | High     | `runConsole` buf[32] silently cuts `--atxt` (39)                                                                                                                     | buf 64                                           |
| 11  | High     | `jsdom` suite is not in `tools/regression.sh`: page regressions never gate                                                                                           | P12                                              |
| 12  | Medium   | Reply charset: `:ack` swallowed, `{` truncates, relay calls unvalidated                                                                                              | `rmSanitizeResult()`                             |
| 13  | Medium   | Remote `maxhop` write is no authority boundary (unauthenticated `{SET}` already writes it)                                                                           | Read-only                                        |
| 14  | Medium   | `setpos` has no lat/lon range check; GPS overwrites                                                                                                                  | Range + `err gps`                                |
| 15  | Medium   | All RM traffic is on the public server and mcmap; replies are readable, frames replayable                                                                            | Operator questions, ADR line                     |
| 16  | Medium   | Mailbox "stored" bytes not exposed; msgstore not built on classic ESP32                                                                                              | `msgstoreBytes()`                                |
| 17  | Medium   | Distance uses an unsigned own position in three places (wrong in S/W hemispheres)                                                                                    | Formatter uses signed; backlog                   |
| 18  | Medium   | SX1278 SF6 switches to implicit header; `lora_setchip` return value ignored; track-mode reconfigure while on air                                                     | Backlog items                                    |
| 19  | Low      | Concept fact "DM cap 160" wrong: 160 includes `{CALL}`, wire <= 149                                                                                                  | Fixed in 2                                       |

## Finding 20 (verify-E, then measured by the orchestrator)

- **High, in already committed code:** `t_echo` fails to link at HEAD (456e2138): flash overflows by
  9 344 B. At 02314ce2 (before the RM page waves) it linked at 782 536 B (96.0 %), so
  967d118d/e6f6c35b added about 41.9 kB. `heltec_t114` links at 98.1 %. The gate never built either
  board. Fix is wave W-1 of the plan, before any new feature work.
- RAK 77.3 % is the real figure (-Os since 09-29); the 96 % figure was the old -Ofast build.
- Airtime numbers of the efficiency finder were about 2x too low; see concept 2.5.

## Refuted claims (do not re-investigate)

- "nRF52 loop task stack is 4 KB": it is an 8 kB task (`src/main.cpp:17-19`).
- "Rate rejects feed the lockout" (T-14, UX-13): they are silent and uncounted (`remote_cmd.cpp:396-399`).
- "3 s spacing is fine" (T9, C9): incompatible, see finding 3.
- "Do not raise `kSentN`": needed, 12 slots ~2.8 kB.
- "Messages list is empty after reload" (T18c).
- "`rmSenderReset()` on own-password change": the sender never reads the node's own password; reset is per target.
- `/rmsend` body over 200 bytes (E8): stays <= 194 for the planned commands.
- Names with spaces break parsing (T-06): they parse fine.
- "Distance is web-handler-only": `nbrDistKm` exists and is pure.
- "Float printf missing on nRF52" (concept premise): float printf is linked.
- "`wire[96]` is one byte short": 95 + NUL fits exactly.

## Declined suggestions

- Shared setter refactor (DRY finder): declined as unnecessary, RM and the web page already share the
  setters through `runConsole` plus read-back; only a radio trial would need a function.
- Generated allowlist header: declined, vectors are the drift guard.
- Folding every read/write under fewer names: adopted where it does not mix classes (10 names).

## Open items outside this campaign

`sync` limiter (own PR); radio re-init from the loop task (`--txpower` today); `lora_setchip` return
value; track-mode reconfigure while on air; unsigned own position in distance; RM1 uploaded by
gateways to the public server; N-02 `{SET}` max_hop.
