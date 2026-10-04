# ADR: Authenticated remote management over LoRa (RM1, HMAC-SHA256)

**Status:** Accepted, implemented (issue #1189, waves RM W1/W2, fork-dev `e0075017`, `1cab9347`)
**Date:** 2026-10-05
**Supersedes:** [`archive/adr-totp-remote-led.md`](archive/adr-totp-remote-led.md) (TOTP, never implemented)
**Design source:** `docs/concept-open-issues-20261004.md` sections 3.2a, 6 and 7
**Wire format:** `docs/architecture/11-wire-format.md`, section "RM1 command DM"

## Context

No command that arrives over LoRa is authenticated today. Any node in range can send a text frame,
and the firmware acts on a small set of control payloads by shape alone (`{SET}`, `{CET}`, `{ping}`).
The one remote-switching precedent, `{MCP}` (finding N-01, WONTFIX), carries a password in the clear
inside the frame and a 3-digit check derived from the frame's own message id. Anyone who hears a
single frame can replay it. It is a weak precedent and is not extended.

Operators of unattended nodes (mountain sites, solar relays) need a way to reboot, query and toggle
a node over the mesh when the internet path and the BLE/WiFi surfaces are not available. The
requirements:

- authenticated: only a holder of a shared secret can cause an effect,
- replay-safe without a clock (a RAK without Ethernet and a Heltec without GPS or RTC may have none),
- a closed, small command set that cannot reach credentials, flash wipe or update paths,
- runs on ESP32 and nRF52, in the loop task only (no crypto, no printf and no flash write in RX context
  on nRF52),
- legal as amateur radio traffic: the frames stay readable.

The earlier proposal (TOTP, RFC 6238, `archive/adr-totp-remote-led.md`) never reached `src/`. See
"Alternatives rejected".

## Decision

RM1 is a direct message (DM) to the exact own call with a plaintext command and a 64-bit
authentication tag.

- **Tag:** the first 8 bytes of HMAC-SHA256(K, canonical string), sent as 16 lower-case hex
  characters.
- **Key:** `K = SHA-256(node_passwd with trailing spaces stripped)`. `node_passwd` is `char[15]`, at
  most 14 characters, space padded.
- **Counter:** a decimal counter 1..4294967295 in every command, strictly greater than the node's
  persisted high-water mark. Counter 0 is reserved for `sync`.
- **Allowlist:** closed and compiled in (below). Everything else is rejected, whatever the tag.
- **Plaintext plus tag:** command and reply are readable on the air. The tag authenticates, it does
  not encrypt. This is deliberate and follows amateur-radio law: no encryption, no obscured meaning.
  The PR description must state this position.
- **Where accepted:** LoRa only (`!msg_server`), DM to the exact own call, never group or broadcast.
  The gateway ingress twins are not wired. `RM1 ` DMs are excluded from store-node custody
  (`mboxClassify`), so they never occupy a mailbox slot.
- **Enable:** `--rm on|off` (default off) and a non-empty `node_passwd`. Otherwise an `RM1 ` DM is
  ordinary text, shown and forwarded like any other DM.
- **Execution:** the receive hook (`rmTryQueue` in `lora_functions.cpp`) only copies the text into a
  2-slot queue (`rm_queue.h`, cross-task lock, drop newest). `rmDrain()` in the loop task parses,
  verifies, executes through a fixed table of literal console strings (never the received text), and
  sends a tagged reply DM. Replies (`RM1 <ctr> ok|err ...`) are recognised by `rmIsReply()` and shown
  to the operator, never queued as commands.

### Wire format

```
command  RM1 <ctr> <cmd>[ <args>] <tag>
reply    RM1 <ctr> ok <status> <rtag>   |   RM1 <ctr> err <reason> <rtag>

canonical (command) = "RM1|"  dst "|" src "|" ctr "|" cmd [" " args]
canonical (reply)   = "RM1R|" dst "|" src "|" ctr "|" result
```

`dst` is the managed node's full call as configured, `src` is the sender call from the frame. Binding
`dst` stops replay against a sibling node that shares the password; binding `src` ties the reply
address. The reply tag lets the operator's client verify that the node answered. Examples and test
vectors are in `docs/architecture/11-wire-format.md` and `tools/tests/remote_cmd_vectors.json`
(21 shared vectors, reproduced by the C++ host test).

### Allowlist

`reboot`, `status`, `sendpos`, `sendtrack`, `sync` (no arguments); `gps`, `track`, `display`,
`gateway`, `mesh` (`on|off`); `txpower <n>` (0 to the board maximum); `setout <a0..a7|b0..b7>
<on|off>`. Never reachable, whatever the tag: `cleanflash`, `ota-update`, `dfu`, `deepsleep`,
`setcall`, `passwd`, `webpwd`, `btcode`, `setssid`, `setpwd`, `wifiset`, `updrepo`, `updchan`,
`autoupdate`, `rm`, `stor`, and any argument containing `--`, `;`, `{` or `%`.

### Verdicts and markers

`rmCheck()` returns one verdict per command. All rejects are silent on the air (no oracle, no reply
storm); the node prints a serial marker `[RM];reject;<verdict>` and counts it for `--info`
(`RM: on ok= rej=`).

| Verdict    | Meaning                                                                             | Reply        | Counts for lockout    |
| ---------- | ----------------------------------------------------------------------------------- | ------------ | --------------------- |
| `ok`       | valid tag, counter above the mark, allowlisted, rate ok: execute                    | tagged reply | no                    |
| `cached`   | same counter and same tag as the last accepted command within 10 min                | cached reply | no                    |
| `sync`     | valid `sync`: `ok ctr=<hwm> v=<version>`, never moves the mark                      | tagged reply | no                    |
| `format`   | syntax error: a parse failure in `rmDrain()` (statistic only) or inside `rmCheck()` | none         | parse: no, check: yes |
| `tag`      | HMAC mismatch                                                                       | none         | yes                   |
| `replay`   | valid tag, counter at or below the mark and not the cached command                  | none         | yes                   |
| `blocked`  | not on the allowlist, bad arguments or forbidden characters                         | none         | yes                   |
| `rate`     | valid tag, but less than 10 s since the last accepted command or `sync`             | none         | no                    |
| `lockout`  | RM1 locked for 5 min                                                                | none         | no                    |
| `disabled` | empty `node_passwd` (the hook already drops `RM1` DMs when `--rm` is off)           | none         | no                    |

Other markers: `[RM];ok;ctr;<n>` and `[RM];fail;ctr;<n>` (command accepted, execution succeeded or
not), `[RM];cached;ctr;<n>[;suppressed]`, `[RM];sync;ctr;<hwm>`, `[RM];reboot`,
`[RM];init;hwm;<n>`, `[RM];hwm_save;failed`.

### Rate limit and lockout

- At most one accepted command per 10 s (`RM_RATE_MS`); `sync` stamps the same limiter.
- After 3 counted rejects within 90 s the node ignores RM1 for 5 min (`RM_REJ_LIMIT`,
  `RM_REJ_WINDOW_MS`, `RM_LOCKOUT_MS`; the numbers come from the TOTP ADR).
- **Rate rejects do not count.** A rate reject needs a valid tag, so it is not an attack signal.
- **Parse failures do not count.** A text that does not parse never reaches `rmCheck()`; `rmDrain()`
  only bumps a statistic.
- **Accepted DoS surface:** every other reject (`tag`, `replay`, `blocked`) is reachable without the
  key. Three junk DMs per 5 min, to the exact own call and with a well-formed `RM1` syntax, keep RM
  unavailable for the legitimate operator. RM fails closed, never open. The alternative (not
  counting unauthenticated rejects) would remove the brute-force limit on the 64-bit tag, which is
  the worse trade. An operator who needs RM during a lockout waits it out or uses another surface
  (console, web, BLE).

### Replay handling

- The counter must exceed the persisted high-water mark. A captured frame is useless after execution.
- **Lost-reply recovery:** the same counter with the same valid tag within 10 min (`RM_CACHE_MS`)
  re-sends the cached reply and does not execute again. The cache is RAM only. At most one cached
  reply per 10 s, so a sniffed valid frame re-injected inside the window cannot turn the node into a
  reply amplifier (suppressed replies print `[RM];cached;ctr;<n>;suppressed`).
- **Mesh relay copies:** the cache and the dedup ring are RAM. A relayed copy of an already executed
  command that arrives after a reboot (typically after a `reboot` command) finds the counter at or
  below the mark and is a `replay` reject. It counts toward the lockout like any other counted
  reject. One such copy is harmless; three within 90 s lock RM for 5 min.
- **Counter loss on the sender:** `sync` (counter 0) returns the node's mark; the operator's client
  stores the counter per target.

### Counter persistence

The mark is written to flash **before** the command executes, so a command that crashes or reboots
the node cannot stay replayable. It lives outside the settings record and outside the config JSON:
a settings restore, a config import or a BLE settings write can never rewind it.

- **ESP32:** Preferences namespace `Counters`, key `rm_hwm` (UInt), next to `node_msgid`. The
  namespace is deliberately not cleared by `clear_flash()`.
- **nRF52:** two slot files `/rm_hwm.a` and `/rm_hwm.b` in InternalFS, decimal plus LF. Each save
  removes and rewrites the slot that holds the smaller value, so the other slot always keeps the
  previous mark; load takes the maximum of the readable slots. **No rename:** the shared
  temp-then-rename helper (`writeFileAtomic`) failed its rename onto an existing small file on every
  save on the RAK4631 (bench 2026-10-05, `rename_failed_twice`), while an in-place write with
  read-back verification is reliable. A torn write lacks the trailing LF and is ignored on load, so
  it loses at most the newest mark, and the command whose mark did not reach flash is not executed.
- **Failed save fails closed:** the node replies `err storage` and does not execute. The sender
  retries with a higher counter.
- **After a flash wipe change `node_passwd`.** A chip erase or an nRF52 filesystem format resets the
  mark to 0; frames captured earlier would be valid again under the old key. A new password
  invalidates them.

## Consequences

- **RM-D1: `node_passwd` is the key.** It is shared with the net console (TCP 2323) and the KISS
  challenge-response, and it is exported in clear in the config JSON (operator decision 2026-08-30).
  Whoever reads a config export can send RM1 commands. This is accepted and goes into the release
  notes. A separate RM key would need a new setting and a separate provisioning path.
- The tag is 64 bits and the counter 32. Online guessing at LoRa rates, with a lockout after 3
  rejects per 90 s, is not feasible; offline guessing of a weak `node_passwd` from one captured
  frame is. The password should be long and random. There is no key stretching: the key is a single
  SHA-256 of the password.
- The sender's `src` is spoofable. The tag binds it, so a spoofed `src` needs the key anyway; who may
  send is "any call holding the key" (RM-D3).
- Same-key replies are unencrypted and contain the node status (`status` shows version, uptime,
  battery, heap, gateway and mesh flags).
- Crypto is the portable `src/hmac_sha256.h` (no mbedtls, no allocation, FIPS 180-4 and RFC 4231
  tests) on both platforms. The three existing mbedtls sites (console, KISS, external radio glue)
  are untouched.
- The lockout DoS above and the relay-copy replay count are documented behaviour, not defects.
- Not covered: a gateway/internet path (RM-D6), reply retries beyond the normal PN ladder (RM-D4),
  commands that change credentials or update the firmware.

## Alternatives rejected

- **TOTP (RFC 6238, the earlier ADR).** Needs a trustworthy clock; the target nodes may have none
  (no GPS, no RTC, no NTP), and a wrong clock silently locks the operator out. A 6-digit code is
  guessable inside a 30 s window; a shared secret in Base32 needs a separate provisioning path
  (QR, web field, NVS keys). No sender state means no replay protection inside the window. Also
  never implemented. A counter plus HMAC needs no clock and binds the code to the exact command.
- **Extending `{MCP}`.** Password in the clear in the frame, check derived from the frame's own id,
  replayable. WONTFIX (N-01).
- **Encrypted commands.** Not permitted in amateur radio.
- **Not counting unauthenticated rejects.** Removes the DoS surface and the brute-force limit
  together. Rejected.

## Bench evidence (2026-10-05, RM W2, 2 dBm, DM only, own calls)

DK5EN-1 (Heltec V3) to DK5EN-90 (RAK4631) and the reverse direction:

- `status`, `display off`, `display on`, `sync` and `reboot` executed and answered; the reply tag was
  verified by `tools/remote_cmd.py`.
- The high-water mark survived a reboot.
- A replayed frame was rejected (`[RM];reject;replay`), bad tags were rejected, and the lockout
  was observed.
- Fixed on the bench: the nRF52 rename failure on the mark file (now two slot files without rename),
  and replies parsed as commands at the sender (now `rmIsReply()`).
