# verdict-ux: usability against the user requirement (verifier, cluster UX)

> Layout rules for the Remote page (row grid, cards, counter tiles, queue bars, TX power range) are amended by
> `docs/rm-gui/ux-tidy-plan-20261010.md`; where the two disagree, that file wins.

Code read now: src/remote_cmd.cpp (allowed 130-157, reject 185-201, rmCheck 354-407, rmAccept 410-432),
src/remote_cmd.h:40-45, src/rm_runtime.cpp (whole file), src/lora_functions.cpp:227-256 + 554,
src/loop_functions_extern.h:30-42, src/configuration_default.h:109-110, variants/*/configuration.h,
src/web_functions/web_functions.cpp (rmstatus 712-763, scaffold 1198/1305-1315, CSS 1407-1498,
nav 1568-1599, mheard 1860-1964, RM card 3271-3326), src/nbr_views.h:27-50, tools/remote_cmd.py:55-80.

## Ground truth the tile design must respect

- What the node reports today. `status` reply (rm_runtime.cpp:150-157):
  `ok v=4.40a up=<min> bat=<%> heap=<kB> gw=0|1 mesh=0|1[ led=0|1]`. `led=` appears only where
  REMOTE_LED_PIN exists, so it doubles as an LED capability flag. NOT reported: gps, track, display,
  txpower, MCP output config. Toggle replies carry the new state (`ok gps=on`, 180), txpower
  `ok txpower=N` (195). /rmstatus (712-763) has no parsed state, only `sent[].reply` raw text
  (ring of 5, rm_runtime.cpp:37) plus rep/ver/ago.
- Reply budget: RM_MAX_RESULT 63 (remote_cmd.h:45). Worst-case status today is 56 chars
  (`up=525600 heap=8123`); adding `gps=1 trk=0 dsp=1 pwr=22` gives 81, so it gets cut. Compact form
  needed, e.g. replace `gw= mesh= led=` by `s=GtDwMl p=17/22` (letter = supported, upper = on;
  52 chars). Only consumer of the old fields is one test vector (tools/remote_cmd.py:282).
- Every non-allowlisted or out-of-range command is RM_REJ_BLOCKED, silent and counts toward the
  3-strike lockout (remote_cmd.cpp:376-377 into reject() 185-201). That covers `txpower` above the
  TARGET's TX_POWER_MAX and `led` to a target older than fbba3f46. The sender only checks its own
  TX_POWER_MAX (rm_runtime.cpp:613). Values: 22 by default (configuration_default.h:110),
  Heltec V2 15, TTGO LoRa32 v2.1 20, T-Beam 20. RM floor is 0 (allowed() rejects negative values;
  the console allows TX_POWER_MIN -9).
- Nothing on the allowlist cuts the node's RM receive path. The mesh bit gates relaying only
  (lora_functions.cpp:554), commands are LoRa-only anyway (`msg_server` -> ordinary text, 245-246),
  so `gateway off` does not remove the operator's path, txpower changes TX only, and reboot is
  delayed 8 s then returns (rm_runtime.cpp:27, 417-422).
- RM capability is not advertised: node_rm exists only locally (meshcom_settings.h:203, command
  path). The MHEARD `fw` is one letter a..z (nbr_views.h:49, nbr_views.cpp:243), so 4.35a and
  4.40a look the same. Detecting RM-capable nodes is impossible. Say so in the UI.
- MHEARD offers per row: call with SSID, hardware name (nbrHardwareName), last-heard age, RSSI,
  SNR, distance, fw letter (web_functions.cpp:1904-1951). It is HTML only (no JSON route, 1060) and
  lists direct receptions from the last 3 h only (1895). A managed node behind relays is not in it.

## Candidates

### find-ux-1 Raw-command dropdown stays, requirement not met -> CONFIRMED (high)

web_functions.cpp:3305-3311 emits 19 raw option strings ("gps on", "setout <a0..b7> <on|off>"),
3317-3318 Send / Sync counter. The concept keeps this select, which is console vocabulary inside a
dropdown. Fix sound: data-driven tiles with plain labels; raw list only in a collapsed Advanced fold.

### find-ux-2 Toggles have no current state -> CONFIRMED (high), fix only half sound

Parsing `status` gives gw/mesh/led only (rm_runtime.cpp:150-157). gps/track/display/txpower are
never reported, so the finder's "parse status into switch tiles" shows 3 of 6 switches.
Toggle replies (180) give state only for what this GUI sent, and only while it stays in the 5-entry ring.
Fix: extend `status` with a compact state token (above, fits 63) and parse old and new form
(old targets: those tiles show "unknown"). One switch per function, state "unknown" until a
verified status has arrived, "pending" until the reply.

### find-ux-3 txpower/setout need free text -> CONFIRMED (high), with a correction

rm_args free text (3314, placeholder 1305). Correction: "node limits it" is wrong. A value above the
target's max is a silent BLOCKED reject that counts toward lockout (remote_cmd.cpp:146, 376-377).
setout to a pin not configured as output answers `err not output` (rm_runtime.cpp:205-208), and MCP
presence/config is never reported.
Fix: txpower stepper 0..min(own max, target max) where the target max comes from the status token
`p=cur/max`; without it, cap at 15 (lowest board max) and say so. setout: 16 pin chips + On/Off,
only in Advanced, failure text "this pin is not set as output on that node".

### find-ux-4 Dangerous actions strand the node permanently -> DOWNGRADED (medium)

No allowed command disables the node's RM reception (see ground truth: lora_functions.cpp:554,
245-246; reboot comes back after kRebootDelayMs 8000). Real harm: mesh off or gateway off
disrupts third parties who depend on that site; low txpower shrinks range so the reply may never
arrive although the command ran; reboot drops the node for a while. "Hold 2 s" plus a checkbox is
disproportionate. Fix: one confirm naming node and consequence for reboot, mesh off, gateway off,
and txpower when lowering it. No confirm for status/sendpos/sendtrack/gps/track/display/led.

### find-ux-8 Ad-hoc target needs typing; pick from MHEARD -> CONFIRMED (medium), reduce not remove

MHEARD gives call+SSID, hardware, age, signal (1904-1951) but direct-only, 3 h (1895), no JSON.
RM capability cannot be detected (see ground truth), so the list is "nodes I hear", not "manageable nodes".
Fix: (a) sub_page_remote() prints the nbrMhRows() calls as node chips server-side (no new endpoint);
(b) a "Manage" button per MHEARD card that opens Remote with the call preselected; (c) keep one
validated call field as fallback for nodes behind relays. The slot count (3 vs >=8) is a
DESIGN-DECISION; nothing in the sender limits it (peer hwm table 4, rm_runtime.cpp:55, is only a cache).

### find-ux-9 Password wording / no way to know a saved password is right -> CONFIRMED (medium)

Footnote 3321 is jargon; a write-only slot with a typo fails silently (silent TAG reject, counts
to lockout). Fix sound with caveat: "Test connection" = one `sync`, whose reply is verified with the
key (handleReply 358-378). A wrong password costs 1 of 3 strikes, so test once and never retry
automatically. A sync also starts the 10 s rate window (remote_cmd.cpp:401-404; the sender shows
"busy" 10 s, rm_runtime.cpp:622-627), so the UI needs a visible cooldown after the test.

### find-ux-10 Raw reply text and "UNVERIFIED" jargon -> CONFIRMED (medium)

The result set is small and closed: ok rebooting / ok sent / ok <sw>=on|off / ok txpower=N /
ok <pin>=on|off / err failed / err not output / err unsupported / err storage
(rm_runtime.cpp:144-238), plus the sync form `ok ctr=N v=...` (526) and the unverified raw text (379-380).
Fix sound and cheap: one JS map to plain sentences; ctr columns into Advanced; one Activity list.

### find-ux-11 "Locate me" LED stays on, unsupported boards -> CONFIRMED (medium)

LED is held until `led off` or reboot, no timer (rm_runtime.cpp:225). No LED pin on nRF52, T-Beam
or boards without BOARD_LED (loop_functions_extern.h:31-38): `err unsupported`. An older target
gives a silent BLOCKED reject plus a lockout strike. A capability flag already exists: status prints
`led=` only where supported. Fix: an "Identify light" switch (on/off with state), shown only after a
status reported `led=`. No one-shot "Locate me" without a firmware auto-off; that is a feature request.

### find-ux-12 "needs --passwd" hint, switch before password -> CONFIRMED (low)

web_functions.cpp:3285-3286 shows console syntax in the hint; RM on without password is inert
(command_functions.cpp:4656). Fix: disable the RM switch until a password is set, with the sentence
"set a password first". A 3-step stepper is overkill.

### find-tests-altitude-5 Keep passwords in localStorage / McApp instead of firmware -> DESIGN-DECISION

Valid cost argument (no store, no nRF52 LittleFS logic). Missed caveat: localStorage is per origin
= per URL host. The RAK is reached by DHCP IP only (no mDNS), and an ESP32 by IP or by name, so the
saved data are lost or split when the address changes. It is plaintext in the browser and per device.
Acceptable as stage 1 with an opt-in "remember on this browser"; a firmware store only if the
user wants passwords to follow the node.

### find-tests-altitude-12 Keep the card on Setup, presentation-only fix -> DESIGN-DECISION

A presentation-only fix (tiles + MHEARD pick + plain errors) meets "no typing of commands". Leaving it
as a collapsed card among ~10 Setup cards hurts the "comfortable" half. The cost driver is the
firmware slot store, not the page. One route plus one nav button is about 10 lines (1060-1063 pattern).
Recommend: own page yes, slot store deferred.

### find-webmech-6 No tile/modal components, confirm() blocks -> DESIGN-DECISION (low)

True that there is no tile CSS (1407-1498). But confirm() is the established pattern (1594, 1599, 2941,
3041, 3044, 3164, 3481, 3832), and a blocked poll for a few seconds is harmless. Cheapest sound
variant: confirm() with a full sentence ("Restart DK5EN-12? It is gone for about 30 s"). The
armed-tile two-tap is optional polish. Tile CSS is about 4 rules reusing --mclightred/--mclightgreen.

### find-webmech-7 printf emission cost of many tiles -> DOWNGRADED (low)

Valid guidance, not a concept defect. Render tiles in scaffold JS from one array into an empty
container, which is how the RM card is filled already (rmRender, 1307). Fix as proposed; drop the
SVG-sprite effort. Text labels plus a few tiny inline icons are enough.

### find-webmech-8 Sidebar overflow with one more button -> DOWNGRADED (low)

14 nav buttons, each 60px*wf square + 20px*wf margin (1420-1426, 1568-1599). #nav_layer already
scrolls (overflow:auto, 1420). +80 px makes an existing condition worse, it does not create it.
Fix: insert after Setup; the max-height media rule is optional.

### find-webmech-11 Tile state has no data source -> CONFIRMED (medium), one claim refuted

Confirmed: no remote on/off state except the status text (see ux-2). Refuted part: "pending state is
lost after a page reload" is wrong. The sent ring lives in firmware and /rmstatus returns
dst/cmd/ago/rep/ver (746-757), so pending can be rebuilt after a reload. The last known remote
state (from replies) is lost once 5 newer sends push it out of the ring. Fix: tile = delivery state
plus last verified state with age ("GPS on, 3 min ago"). Conflict with ux-2: they agree once status
is extended.

### find-webmech-12 Command list in three/four places -> CONFIRMED (low)

remote_cmd.cpp:130-157, tools/remote_cmd.py:55-69, web_functions.cpp:3305-3307. Cheapest sound
fix: a tile table in scaffold JS plus one stage-2 test that extracts the cmd ids and compares them
with ALLOWLIST (as test_remote_cmd.py parses vectors). The C table + compile-time check is more work
for the same guarantee.

## Conflicts between finders

- find-ux-7 (auto-sync before the first command, outside this cluster) vs the rate window: a sync
  consumes the target's 10 s window and the sender's "busy" window, so every first command waits
  10 s. Auto-sync is acceptable only with a visible countdown.
- find-ux-4 (hold 2 s + checkbox) vs find-webmech-6 (two-tap armed tile) vs codebase convention
  (confirm()): one confirm is proportionate, see ux-4.

## Missed by all finders (verified)

1. Status reply is near its 63-char cap (remote_cmd.h:45, 56 chars worst case today). Every
   state-aware tile depends on a compact status extension; verbose key=value does not fit.
2. Offering a value or command the TARGET does not accept (txpower > its max; led to pre-fbba3f46)
   is a silent reject that counts toward the 3-strike lockout (remote_cmd.cpp:376-377, 185-201).
   Pickers must be capped by what the target reported, not by the local node's limits.
3. RM capability is undetectable over the air, and MHEARD is direct-only (3 h). The picker must say
   "heard nodes, RM may be off", and the typed fallback stays.

## Recommended smallest design meeting "no typing of commands"

Stage 1 (no firmware store, one target-firmware change: the compact status token):

```
+--------+-------------------------------------------------------------------+
| [Info] |  REMOTE                                                           |
| [Msg]  |                                                                   |
|  ...   |  This node                                                        |
| [Setup]|  RM  [ off | ON ]   password: set   locked: no                   |
| [Remote|  (switch greyed + "set a password first" while none is set)      |
|  ...   |  Password [**********] [Set] [Clear]  also net console + KISS     |
|        |                                                                   |
|        |  Choose a node                                                    |
|        |  +-----------+ +-----------+ +-----------+ +-----------+          |
|        |  | DK5EN-1   | | DK5EN-92  | | OE3XYZ-4  | |  + other  |          |
|        |  | Heltec V3 | | T-Beam    | | RAK4631   | |  node...  |          |
|        |  | 2 min -7dB| | 12 min    | | 40 min    | |           |          |
|        |  +-----------+ +-----------+ +-----------+ +-----------+          |
|        |  heard directly in the last 3 h; RM may be off on any of them     |
|        |  (+ other: one call field, live format check)                     |
|        |                                                                   |
|        |  DK5EN-92            password [*******] [ ] remember here         |
|        |  [ Test connection ]  -> "Connected, v4.40a" / "No answer: ..."   |
|        |  last status: v4.40a, up 2 h 10 min, battery 87 %  (3 min ago)   |
|        |                                                                   |
|        |  INFO                                                             |
|        |  +--------------+ +--------------+ +--------------+               |
|        |  | Refresh      | | Send         | | Send         |               |
|        |  | status       | | position now | | track now    |               |
|        |  +--------------+ +--------------+ +--------------+               |
|        |                                                                   |
|        |  SWITCHES (state from last verified status / reply)              |
|        |  +--------------+ +--------------+ +--------------+               |
|        |  | GPS      ON  | | Track    off | | Display  ON  |               |
|        |  +--------------+ +--------------+ +--------------+               |
|        |  +--------------+ +--------------+ +--------------+               |
|        |  | Light    off | | ! Mesh   ON  | | ! Gateway ?  |               |
|        |  | (only if led | |  relay       | |  unknown     |               |
|        |  |  reported)   | |  (confirm)   | |  (confirm)   |               |
|        |  +--------------+ +--------------+ +--------------+               |
|        |                                                                   |
|        |  TX POWER   [ - ]  17 dBm  [ + ]   max 22   [Apply] (confirm if   |
|        |                                                  lower)           |
|        |  +--------------+                                                 |
|        |  | ! Restart    |   (confirm: "DK5EN-92 is gone for ~30 s")      |
|        |  +--------------+                                                 |
|        |                                                                   |
|        |  [ waiting for answer ... 42 s ]  all tiles locked 10 s after a   |
|        |                                   send (rate limit)               |
|        |                                                                   |
|        |  ACTIVITY                                                         |
|        |  14:02  GPS off -> done, GPS is now off                           |
|        |  14:01  Test connection -> connected, v4.40a                      |
|        |  13:55  Light on -> this board has no light                       |
|        |                                                                   |
|        |  > Advanced (collapsed): outputs A0..B7 + on/off, re-sync,       |
|        |    raw table with counters                                        |
+--------+-------------------------------------------------------------------+
```

Rules behind the sketch:

- Switch tiles flip with one tap. A tile shows "?" until a verified status or reply has set it.
  Light shows only after a status contained `led=`; TX power max comes from `p=cur/max`, else 15.
- One command in flight per node; 10 s cooldown visible; "no answer" after a timeout gives a plain
  checklist and warns "3 failed tries lock the node for 5 min".
- Confirm only on Restart, Mesh off, Gateway off, TX power down.
- Node chips: printed server-side from nbrMhRows(), plus a "Manage" button on each MHEARD card.
- Passwords: typed once per node per session; opt-in "remember on this browser" (localStorage,
  per node address). A firmware slot store is stage 2.

## Top 5 confirmed for the user

1. ux-1 (high): the concept keeps the raw-command select and fails "no typing of commands".
   Replace it with labelled tiles; the raw list goes to Advanced only.
2. ux-2 + missed-1 (high): only gw/mesh/led state is reported. Real switch tiles need a compact status
   extension (gps/track/display/txpower) within the 63-char reply cap; until a status arrives the
   tiles show "unknown".
3. ux-3 + missed-2 (high): picker values the target does not accept (txpower above its max, led to
   older firmware) are silent rejects that count toward the 5-min lockout. Cap pickers by what the
   target reported.
4. ux-8 + missed-3 (medium): node chips from MHEARD remove most call typing. RM capability cannot be
   detected and MHEARD is direct-only for 3 h, so a validated fallback field stays.
5. ux-4 downgraded (medium): no allowed command strands a node's RM path. One plain confirm on
   Restart / Mesh off / Gateway off / TX power down is proportionate. ux-11: the LED is a switch,
   not a one-shot "Locate me", because there is no timer.
