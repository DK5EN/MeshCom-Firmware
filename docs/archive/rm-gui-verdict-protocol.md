# Verdict: PROTOCOL / COUNTER / LOCKOUT / FEEDBACK cluster

> **ARCHIVED 2026-10-07.** closed verdict of the shipped RM GUI campaign, folded into `../rm-gui/impl-plan.md`. Body unchanged.

Repo fork-dev 02314ce2, read now. R = src/rm_runtime.cpp, C = src/remote_cmd.cpp, H = src/remote_cmd.h,
W = src/web_functions/web_functions.cpp, L = src/lora_functions.cpp, CF = src/command_functions.cpp,
ADR = docs/adr-remote-hmac.md.

## Settled key questions (code evidence)

1. **Wrong key / stale counter count toward the TARGET lockout: yes.** rmCheck: tag mismatch ->
   `reject(RM_REJ_TAG)` C:383-385; valid tag but ctr <= hwm -> `reject(RM_REJ_REPLAY)` C:392-393;
   reject() increments rejCount, 3 within 90 s -> lockActive 5 min C:185-202, H:42-44. A `sync` with a
   wrong key is ALSO a counted tag reject (tag check C:383 runs before the sync branch C:401). Only
   rate (C:398-399), lockout (C:360-363) and disabled are uncounted. While locked every frame,
   including a correct one or a sync, is dropped silently (C:360-363).
   **Sender observes nothing:** rejects are silent on air (R:532-535); the sent entry stays `rep=0`;
   W:1310 renders "waiting" with no timeout; after RM_CACHE_MS the key is wiped (R:259-266) and
   rmqReplyWanted goes false (R:250-257), so a later reply is never matched, the row still says
   "waiting". Lock state is only visible in the TARGET's own /rmstatus (`lock`,`lockS`, W:728-730).
2. **Counter today: one global sender counter**, `s_lastSent` persisted as rm_snd (R:46, R:406,
   saved before TX R:669-677). ctr = max(s_lastSent+1, unix time if clockUnix(), peerHwm(dst)+1)
   (R:629-653). peerHwm is RAM-only, 4 entries round robin (R:49-56, 285-310), learnt only from
   verified replies (R:373-377, sync result parsed R:374-375). No per-target counter, none needed.
   **Sync is only needed when the sender has no trustworthy clock** (clockUnix false: no NTP/RTC/GPS
   fix, R:313-322) and no peerHwm for that dst (after sender reboot or 5th target), or when another
   sender's clock runs ahead (skew). With a valid clock ctr = now >= any earlier honest ctr. So sync
   can be automatic and conditional; a human button is not needed. Cost: sync stamps the target's
   rate limiter (C:401-405) and books the dst busy for 10 s on the sender (R:622-627), so
   sync -> command takes >= 10 s plus two round trips. /rmstatus exposes neither "sender clock
   valid" nor "peer hwm known" today, so the GUI cannot decide alone: add one flag or let
   rmSendCommand return err `sync` in that case.
3. **sent / executed / reply today:** /rmstatus `sent[]` = dst, ctr, cmd, ago, rep, ver, reply
   (W:744-759). "sent" = sendMessage() returned BP_SEND_OK = queued to the TX ring, not on air
   (R:681-690). No delivery ACK: the DM is `:{DST}RM1 ...` without `{NNN` (R:680), and only a PN ending
   in `{NNN` gets an id/retry/ACK (loop_functions.cpp ~4321-4323 comment). "executed" = ver=1 with
   reply `ok ...`; `err ...` = target accepted but execution failed or storage failed (R:473-481).
   rep=1, ver=0 = raw unauthenticated text (R:379-380). A cached (lost-reply) answer is
   indistinguishable from the first one, which is correct. No timeout state exists.
4. **Resending the same (ctr,tag) is safe from double execution, but not free.** RM_CACHED needs
   haveLast && ctr == lastCtr && same tag && <= 10 min (C:389-391), returned before the replay check,
   uncounted, re-sends lastReply, nothing executed (R:502-521; one cached reply per 10 s). But only the
   LAST accepted command is cached: if anything else was accepted since, or the target rebooted
   (RAM cache gone; always the case after `reboot`), the resend is a counted `replay` reject. If the
   original never arrived, ctr > hwm and it executes once. rmSendCommand can't do it today (always a
   new ctr, R:629-653; busy gate R:622-627).
5. **Reply tag binding:** target signs with its own call as dst and the received source call as src
   (R:121, rmReply C:434-466); sender verifies with e.pub.dst and its CURRENT node_call (R:366).
   Bound correctly; only an own-call rename in flight breaks it, and then the reply DM is addressed to
   the old call anyway.
6. **Password normalisation:** console line is trimmed as a whole (`sVar.trim()`, CF:641-642), then
   `--passwd` takes msg_text+9, truncates to 14 BYTES silently, `none` clears, stores `%-14.14s`
   (CF:3138-3166). Key = SHA-256 with trailing spaces stripped (C:78-86, 317-326); console/KISS strip
   trailing spaces too (net_console.cpp:304-311, kiss_functions.cpp:576-582). Sender strips trailing
   spaces for the empty test but rejects raw strlen > 14 with err `passwd` (R:603-611), JS sends pw
   untrimmed (W:1314). Mismatch cases produce a VISIBLE `passwd` error, not a silent tag reject. One
   real inconsistency found (missed item M-1 below): leading space.

## Candidates

### protocol-2 Wrong/stale key or counter silent and counts toward lockout -> CONFIRMED (HIGH)

C:383-385, 392-393, 185-202 count tag/replay; wrong-key sync counts too (C:383 before C:401). Sender
gate allows a send per dst every 10 s (R:622-627): 3 tries in ~20 s lock the target 5 min. Sender sees
`rep=0` "waiting" forever (W:1310). Stale ctr is realistic only for a clockless sender without peerHwm
or with clock skew between two senders.
Fix (sound, cheapest): GUI-side per-target budget: timeout ~60 s per command, max 2 unanswered sends per
target per 90 s, then disable Send with "possible lockout, wait 5 min"; conditional auto-sync (Q2). Drop
the finder's "Test key on save" as a separate action: it is just a sync and costs one count when wrong;
fold it into "Check connection".

### protocol-3 No retry-with-same-counter; Send again re-executes -> DOWNGRADED (LOW)

True that rmSendCommand never reuses a ctr (R:629-653). But every allowlisted command except reboot is
idempotent or harmless twice (toggles, led, txpower, setout, status; sendpos/sendtrack = one extra
beacon). For reboot a same-ctr resend cannot help: the target's cache is RAM and gone after the reboot,
so the resend is a counted replay (C:392-393). Resend only helps inside the 8 s pre-reboot window.
Fix: no rmResend path. After `reboot` never resend; confirm with `status` (reply carries `up=<min>`,
R:150-152). Plain Send of other commands is fine.

### protocol-4 GUI cannot learn delivered/executed as specified -> CONFIRMED (MEDIUM)

No `{NNN` -> no ACK (R:680); rep/ver/reply only (W:744-759); ver=0 raw text (R:379-380, W:1310 shows it,
marked "UNVERIFIED"). A forged reply can't override a verified one (R:361 skips verified). Keys expire at
10 min and later replies are dropped (R:259-266, 250-257).
Fix sound: states queued / waiting (countdown) / no answer (timeout ~60-90 s, client-side from `ago`) /
unverified (never green) / done ok / done err <reason>. Parse the `ok`/`err` prefix of a verified reply.

### protocol-5 Reply verify binds current own call -> REFUTED

R:366 uses current node_call, but the target addresses the reply DM to the src it received (R:128), i.e.
the OLD call; a renamed sender no longer receives it as its own DM, so storing src in SentSlot fixes
nothing. Window is a deliberate own-call rename during a pending command. The "target renamed" half is a
slot-management matter (see protocol-8 / last-verified marker).
Fix: none beyond protocol-8's "last verified" marker.

### protocol-6 Key-cache sizing / wipe semantics with a key-based entry -> DOWNGRADED (LOW)

5-slot book, oldest evicted with its key (R:692-697). Eviction of a still-pending reply needs 5 sends
within its reply latency; same-dst sends are >= 10 s apart (R:622-627). Not a regression of the concept.
CONFLICT with storage-11: finder proposes verifying from the store by slot index; that breaks when a slot
is edited/deleted while a reply is pending. storage-11's variant (copy the 32-B key into s_sent exactly as
today) is cheaper and sound. Keep rmqReplyWanted tied to s_sent (unchanged).

### protocol-7 Password normalisation mismatch -> DOWNGRADED (LOW), but see M-1

Finder's lockout scenarios do not hold: a >14-byte password (incl. UTF-8) is refused by the sender with a
visible `passwd` error (R:607-611), never signed with a different key. Trailing spaces: stripped
everywhere (C:78-86, net_console.cpp:309-310, kiss_functions.cpp:580-581); console trims the line
(CF:641-642). `none` = clear on console only (CF:3144).
Finder's fix "do NOT trim" is WRONG for leading spaces (M-1). Correct fix: one shared validator for the new
Set field, slot save and sender: 1..14 bytes, printable ASCII 0x21-0x7e first char (no leading space),
no trailing space, reject literal `none`; reject, never truncate.

### protocol-8 Slot editor call validation and duplicates -> CONFIRMED (LOW-MEDIUM)

Sender rule R:578-601 ([A-Za-z0-9-], <= 9, upper-folded, != own call, error token `dst`). busy gate and
peerHwm keyed by dst string (R:622-627, 285-310), so two slots with the same call share them.
Fix sound: extract the dst check from rmSendCommand into one function, use it at slot save; reject
duplicate calls; store "last verified reply" time per slot.

### protocol-9 Bad clock poisons the target -> DOWNGRADED (LOW)

clockUnix has no upper bound (R:313-322), true. But a poisoned hwm is recoverable: after a sync the sender
uses peerHwm+1 (R:642-652) and works; `ctr` error only at hwm 0xFFFFFFFF, which a uint32 unix clock
reaches only in 2106. Every clockless/clocked sender then needs one sync per sender boot (peerHwm RAM).
Fix: cheap upper bound in clockUnix (e.g. reject > build time + 20 y); no per-slot counter store
(agree with finder (c)); optional, not required for the GUI.

### protocol-10 Reboot vs rate limit vs boot time -> CONFIRMED (LOW)

Reply first, reboot after 8 s (R:27, 493-499); sender busy 10 s (R:622-627). ADR: relayed copies of the
reboot command arriving after the reboot are counted replay rejects (ADR "Mesh relay copies"), which eats
lockout budget right when the user checks the node. Fix: after reboot grey the node ~30-60 s, then one
"Check" (status, shows up=0/1 min). Use 12 s sender spacing is optional.

### protocol-11 Two in flight / correlation -> CONFIRMED (LOW)

(dst,ctr) unique for commands (global ctr), but every sync has ctr 0: a late reply to an older sync
matches the newest unverified sync slot of that dst (R:358-362, newest first), the older stays "waiting"
forever. Fix: GUI keys rows by (dst,ctr,ago) and treats ctr 0 rows as superseded by a newer one; nothing
in firmware.

### protocol-12 Concept vs ADR -> DESIGN-DECISION (MEDIUM)

Clear = `--passwd none` semantics opens console/KISS (CF:3144-3153, ESP32). node_passwd in config export is
an accepted ADR decision (ADR RM-D1). Saved derived keys are a new secret class not in the ADR. Commands
are LoRa-only (L:244-246; ADR RM-D6), so "Known nodes" must say "radio range only".
Fix: ADR addendum (saved keys, at-rest, Clear side effects); label Clear accordingly.

### security-9 GUI retries + stale key = self-inflicted lockout -> CONFIRMED (HIGH), duplicate of protocol-2

Same evidence as protocol-2. Sub-claim "pushes the target hwm up to 0xFFFFFFFF -> ctr error forever" is
REFUTED (uint32 unix clock, see protocol-9). Sub-fix "refuse unix before 2026" redundant (R:315-318 already
2024). Sub-fix "disable Send 90 s after every unanswered send" is too blunt: replies get lost on air
(ADR bench note); use the 2-unanswered budget from protocol-2.

### security-10 Slot index instead of identity: wrong-target race -> DOWNGRADED (LOW)

Valid only with two tabs/operators editing slots concurrently. Fix sound and cheap: POST slot=N&dst=CALL,
node rejects when slots[N].call != dst. Skip the generation counter.

### ux-5 Silent rejects look like "waiting" forever -> DOWNGRADED (HIGH, not critical)

W:1310 "waiting" without timeout; W:1315 shows raw err tokens (`busy`,`passwd`,`dst`,`store`,`send`,
`ctr`,`size`,`short`,`form`). Fix sound; correct the detail: there is no "retry ladder" for RM1 (no `{NNN`),
so the countdown is a fixed timeout (~60-90 s). Map each token to a sentence.

### ux-6 10 s rate limit and LoRa latency not designed for -> DOWNGRADED (MEDIUM-LOW)

Busy gate R:622-627 and poll only while setup card open (W:1312) confirmed. "A reply that arrives later is
missed" is REFUTED: replies are booked node-side in s_sent (R:367-380) and show on the next poll after the
user returns (within 10 min). Fix: per-target 10 s cooldown on the buttons, poll while the Remote page is
open; no sidebar badge needed.

### ux-7 "Sync counter" must not be a human concept -> CONFIRMED (MEDIUM), finder fix partly wrong

Agree: hide it (Q2). Wrong in the finder's fix: "sync never counts" (false with a wrong key, C:383-385) and
"again when a reply says counter too low" (no such reply; replay is silent, C:392-393). Sound variant:
auto-sync only when sender has no valid clock and no peerHwm for that dst (needs one /rmstatus flag or an
err `sync` from rmSendCommand); expose the same frame as a visible "Check connection" button (returns
`v=<version>`, proves key + reach) after a timeout. Never auto-repeat it.

### storage-11 Key-based entry point: second key copy / one wipe discipline -> CONFIRMED (LOW-MEDIUM)

rmSendCommand derives into a stack key and wipes on every exit (R:655-709). Fix is sound and the cheapest:
rmSendCommandKey(dst, key32, cmd, args, ...) holding today's body, password variant = derive, call, wipe;
store holds no counter. Wins over protocol-6's slot-index verify.

## Missed by all finders (verified)

### M-1 Leading-space password: GUI says "set", RM silently dead -> MEDIUM

Receive hook treats node_passwd[0]==' ' as "no password" and never queues RM1 (L:250-252); same test in
`--remotemgmt` (CF:4656) and the setup hint (W:3277). rm_runtime's passwdEmpty() treats only all-spaces as
empty (R:102-114) and feeds /rmstatus `pw` (R:547, W:729). `--passwd  x` (two spaces) stores " x", so
status shows "password set: yes", RM on, every RM1 DM shown as plain text. Fix: the new Set field and
console reject a leading space (or align the hook to strippedLen); one shared validator (protocol-7).

### M-2 Lockout and remote reject reasons are unobservable from the sender

No sender-side signal exists for "target locked" (rejects silent, R:532-535; lock only in target's own
/rmstatus). Therefore the GUI budget in protocol-2 is the ONLY protection; it must be per target and
survive page reloads (keep it node-side in s_sent ages, not in browser memory: the current rmKeep is
tab memory, W:1302). LOW-MEDIUM.

## Top 5 confirmed for the user

1. protocol-2 / security-9 (HIGH): 3 unanswered tries in 90 s (wrong key, stale counter, even a wrong-key
   sync) lock the target 5 min, invisibly. GUI needs a per-target budget (max 2 unanswered per 90 s) and
   a timeout state.
2. ux-5 + protocol-4 (HIGH/MEDIUM): "waiting" never ends; no delivery ACK exists. Define queued / waiting /
   no answer (60-90 s) / unverified / done ok / done err; translate err tokens.
3. ux-7 (MEDIUM): drop the Sync button; sync is only needed for a clockless sender without a learnt
   target mark. Auto-sync in that case, offer "Check connection" after a timeout, never auto-repeat.
4. M-1 (MEDIUM): leading-space passwords are "set" in the status but disable RM silently; the new Set field
   needs one validator (1..14 bytes printable ASCII, no leading/trailing space, not `none`, reject not
   truncate).
5. storage-11 (LOW-MEDIUM): implement the key path as rmSendCommandKey() with the password path as a
   wrapper; keep the 5-slot key copy as today (not protocol-6's verify-from-store). No resend-same-ctr
   path needed (protocol-3): after reboot confirm with `status`.
