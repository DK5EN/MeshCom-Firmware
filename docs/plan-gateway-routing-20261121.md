# Plan: gateway routing (P-0381, P-0382) and call sign with country prefix (P-0210)

Date: 2026-11-21. Branch `fork-dev` at `067d29cd`. Problem files `P-0381`, `P-0382`, `P-0210` (RCA at pin
`96aaa67c72`; `git merge-base --is-ancestor 96aaa67c72 HEAD` is true). Backlog ids `GW-GROUP-01`, `GW-DM-01`,
`GW-CALL-01` (section 7). Nothing is implemented in this session; no `pio` command was run, so every "fails today"
below is derived from the code, not from a before-run. The implementer must record the real before-run.

No operator decision on these three tickets exists in `DECISIONS-2026-11-21.md` (grep for the ids: no hit). The
decisions in section 1 are therefore open.

## 1. Decisions needed

| #   | Question                                                                                                                                                                                           | Options and consequence                                                                                                                                                                                                                                                                                                                               | Recommendation                                                                             |
| --- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------ |
| D1  | Third-party DMs and their `:ackNNN` frames on a gateway: what reaches the phone link?                                                                                                              | A) nothing (same as the LoRa path). The phone app, the web GUI (reads the same ring) and AppLink clients lose them. B) keep them behind a new setting ("monitor"): one more setting, more flash, a setting nobody asked for.                                                                                                                          | A. The app has dropped them since app 4.29 (P-0079); the LoRa path never passes them.      |
| D2  | `--nomsgall on` hides `*` text on LoRa (`lora_functions.cpp:2248`). On the UDP path `*` leaves the branch when the flag is on, falls into the relay hand-off and reaches the phone. Close that?    | A) yes, one rule for both paths: `*` with `--nomsgall on` is not handed to the phone. B) leave it: the flag keeps a hole on gateways. A is a behaviour change beyond the two tickets: a gateway user who set the flag stops getting `*` text on the phone.                                                                                            | A. The flag's name and the LoRa behaviour say so; found while reading, not in the tickets. |
| D3  | Node with no group set: today `CheckOwnGroup()` accepts every numeric group (`aprs_functions.cpp:126`), on LoRa. Keep that for the UDP path too?                                                   | A) yes (identical to LoRa; a node without groups is unfiltered, P-0381 bites only nodes that have groups). B) a node without groups shows no group at all: changes LoRa as well, needs a `lora_functions.cpp` edit and an upstream discussion.                                                                                                        | A. Minimal and consistent.                                                                 |
| D4  | Gateway keeps acknowledging group frames from the server for groups it does not hold (`SendAckMessage` in the branch, mirrors the LoRa gateway ack `lora_functions.cpp:2335`, `CheckGroup() > 0`)? | A) keep (this plan: only display and phone copies are gated, the branch is entered as today). B) stop acking foreign groups on the UDP path: changes what the sender sees from the gateway, not requested by any ticket.                                                                                                                              | A.                                                                                         |
| D5  | P-0210: what happens to a call sign with a country prefix (`SV/OE3WFA`)?                                                                                                                           | A) keep the refusal as by design, document it, make the error say why and what to use (no wire change). B) extend the call sign grammar for `/`: every receiver in the field that has not been updated drops the frame (`aprs_functions.cpp:297,307,405`; wire-format doc 1.4), so it needs maintainer agreement and a staged rollout. See section 6. | A now; B only as an upstream proposal after the maintainers agree.                         |

## 2. Shared routing section (P-0381 and P-0382)

### 2.1 Symptom

A gateway node (ESP32 and nRF52 with Ethernet) receives text frames from the server over UDP. Two things reach
the phone link (BLE and, since `ce05f38c`, AppLink) that the same frame would never reach when it arrives over
LoRa:

- P-0381: a numeric group the node has not set, when the node has groups set (also shown on the display).
- P-0382: a text frame addressed to another call sign (third-party DM) and its `:ackNNN` / `:rejNNN` frames.

Both were found by code reading and are not reproduced on hardware.

### 2.2 Root cause at HEAD

The LoRa side decides what the phone gets (`src/lora_functions.cpp`):

- own call: DM branch, ending at `addBLEOutBuffer(RcvBuffer, size)` `:2116` (`rmTryQueue()` first);
- `*` (unless `--nomsgall`) or own group: `if((strcmp(destination_call, "*") == 0 && !bNoMSGtoALL) ||
CheckOwnGroup(destination_call))` at `:2248`, phone copies at `:2270` and `:2276`;
- everything else (a foreign DM, a foreign group, its acks) is relayed or dropped, never handed to the phone. The
  P-0079 statement "the LoRa path passes the phone only DMs for the node" is confirmed by the read of `:2243-2276`.

The UDP side differs in two places, each of them present on ESP32 and nRF52:

1. The branch condition uses the shape test only. ESP32 `src/esp32/udp_frame_esp32.cpp:398`, nRF52
   `src/nrf52/udp_frame_nrf52.cpp:333`: `(strcmp(destination_call, "*") == 0 && !bNoMSGtoALL) ||
strcmp(destination_call, meshcom_settings.node_call) == 0 || CheckGroup(destination_call) > 0`.
   `CheckGroup(const char *)` (`src/aprs_functions.cpp:64-86`) accepts 1..6 digits with value 1..99999 or 100001
   and never reads `node_gcb`. `CheckOwnGroup(String)` (`:88-127`) compares with `node_gcb[0..5]` and returns true
   for any group only when no group is set at all (`:126`). Inside the branch the phone and display calls are
   ESP32 `:462` (ack frame), `:537` (`sendDisplayText`), `:551` (`addBLEOutBuffer(tempRcvBuffer, ...)`); nRF52
   `:413`, `:491` (`sendDisplayText`, only `if(!bGATEWAY)`), `:505`. The branch ends with `bBLELoopOut = false`
   (ESP32 `:553`, nRF52 `:507`), so the frame is not handed to the phone a second time.
2. The relay step hands every frame that did not enter the branch to the phone. `bBLELoopOut` starts `true`
   (ESP32 `:189`, nRF52 `:189`); in the relay block `if(isPhoneReady == 1 && bBLELoopOut)` at ESP32 `:641-643`
   and nRF52 `:618-625` calls `addBLEOutBuffer(convBuffer, size)`. The comment "wird schon vorher abgehandelt" is
   only true for frames that went through the branch. This block is inside `if(bUdpMsgIsNew)` (nRF52
   `is_new_packet()` at `:549`), `icheck < 0`, not an own-source reflection (Finding 7) and `bUDPtoLoraSend`.

Consequences, by frame:

| Frame from the server                                | Enters branch today | Phone gets it today                  | After the fix                                     |
| ---------------------------------------------------- | ------------------- | ------------------------------------ | ------------------------------------------------- |
| DM to own call                                       | yes                 | yes, once                            | unchanged                                         |
| `*`, `--nomsgall off`                                | yes                 | yes, once                            | unchanged                                         |
| `*`, `--nomsgall on`                                 | no                  | yes, through the hand-off (hole, D2) | no (if D2 = A)                                    |
| own group                                            | yes                 | yes, once                            | unchanged                                         |
| foreign group, node has groups (P-0381)              | yes                 | yes, once, plus display              | no, no display; relay to LoRa and ack as today    |
| foreign group, node has no group set                 | yes                 | yes                                  | unchanged (D3 = A)                                |
| DM to a third call (P-0382)                          | no                  | yes, through the hand-off            | no                                                |
| `:ackNNN` / `:rejNNN` text for a third call (P-0382) | no                  | yes, through the hand-off            | no                                                |
| `{SET}` / `{CET}` control text                       | no (own `else if`)  | yes, through the hand-off            | unchanged (explicit exemption)                    |
| position (`0x21`) and HEY (`0x40`)                   | not text            | yes, through the hand-off            | unchanged (mesh traffic on the phone is intended) |

The two tickets are one coupled change. Swapping `CheckGroup(...) > 0` for `CheckOwnGroup(...)` in the branch
condition (the fix direction in P-0381) alone is not enough: a foreign group would leave the branch with
`bBLELoopOut` still `true` and the relay hand-off would give it to the phone anyway. It would also drop the gateway
ack (`SendAckMessage`, ESP32 `:565-582`, nRF52 `:519-536`) for foreign groups, which is D4. The
design below therefore decides once and gates the outputs.

### 2.3 Commits since the pin; APPLINK

`git log --oneline 96aaa67c72..HEAD -- src/esp32/udp_frame_esp32.cpp src/nrf52/udp_frame_nrf52.cpp
src/lora_functions.cpp src/aprs_functions.cpp test/test_udp_frame_twin` is empty. Every line above is the line of the
pin (the RCA numbers hold; P-0382's own `:572`/`:561` were already corrected there).

APPLINK (`ce05f38c`, an ancestor of HEAD, not reverted; `git branch --contains ce05f38c` lists `fork-dev`)
shares the hand-off. `docs/applink-plan-firmware.md` says the phone path (`phoneRing`, the drain) stays the single
path and AppLink is a second sink: `addBLEOutBuffer()` (`src/loop_functions.cpp:1003`) pushes into `phoneRing`,
`sendToPhone()` (`src/phone_commands.cpp:123`, `blePhoneDrainOne(&phoneRing, ...)` `:165`) drains it to the owner of
the link (BLE or AppLink), `isPhoneReady == 1` gates both. The relay hand-off therefore feeds both transports and
a filter at the two call sites covers both; nothing in `applink_*` needs a change. Not to be filtered inside
`addBLEOutBuffer()`: it is the funnel for every phone frame (acks, status, positions).

### 2.4 Platforms

ESP32 (`udp_frame_esp32.cpp`: Heltec V3, T-Beam, T-Deck with WiFi/W5500) and nRF52 (`udp_frame_nrf52.cpp`: RAK4631
with RAK13800) are two copies with the same defect at the same relative places; both are edited. Known per-platform
difference that stays: nRF52 shows the text on the display only `if(!bGATEWAY)` (`:491`), ESP32 always (`:537`). The
helper and the gate are identical on both, so the twin test pins both sides with one case.

### 2.5 Fix design (one shared helper)

Grep result: no existing helper decides "is this server text frame for this node". Existing pieces reused, not
copied: `CheckOwnGroup()` (`aprs_functions.cpp:88`, real in `native_udp_frame_twin`), `bNoMSGtoALL`,
`meshcom_settings.node_call`, `{SET}`/`{CET}` prefixes as the handler already tests them. New header, no Arduino
dependency beyond `aprs_functions.h`: `src/gw_phone_route.h` (name checked: no file `src/gw_phone_route*` exists).

```cpp
// true when a server text frame is for this node: "*" (unless --nomsgall), the own call, or an own group
static inline bool gwUdpTextForNode(const char *dest, const char *ownCall, bool noMsgToAll);

// false only for a text frame (0x3A, not {SET}/{CET}) that is not for this node
static inline bool gwUdpPhoneWants(uint8_t msgType, const char *payload, bool forNode);
```

Both handlers (the same edit twice):

1. Compute `const bool bForNode = gwUdpTextForNode(destination_call, meshcom_settings.node_call, bNoMSGtoALL);`
   once, for `msg_type_b == 0x3A`, before the branch.
2. Leave the branch condition (`:398` / `:333`) as it is. Inside it gate only the user-facing calls on `bForNode`:
   the ack frame to the phone (ESP32 `:462`, nRF52 `:413`), `sendDisplayText` (`:537` / `:491`), and
   `addBLEOutBuffer(tempRcvBuffer, ...)` (`:551` / `:505`). The ack handling, `msgstore`, `SendAckMessage`, the dedup
   and the relay to LoRa stay as they are (D4 = A). `bBLELoopOut = false` stays.
3. In the relay hand-off change the condition to
   `if(isPhoneReady == 1 && bBLELoopOut && gwUdpPhoneWants(msg_type_b, aprsmsg.msg_payload, bForNode))`
   (ESP32 `:641`, nRF52 `:618`) and correct the comment.

Not changed: `lora_functions.cpp` (no host env builds it; the LoRa rule is the reference the helper mirrors, and
`:2248` could call the helper later), the relay to LoRa (`bUDPtoLoraSend`, `gwUdpRxEnqueue`), the gateway ack, EXTUDP,
KISS, msgstore. A gateway keeps bridging every group and DM between server and mesh; only what it shows and gives
to the phone changes.

`CheckOwnGroup(String)` builds a `String` per frame; the LoRa path does the same per frame, a gateway sees few UDP
text frames. A `const char *` overload is optional, not needed for the fix.

### 2.6 Tests (fail before, pass after)

Env `native_udp_frame_twin` (`platformio.ini:1526`), file `test/test_udp_frame_twin/test_udp_frame_twin.cpp`
(`g_ble` records `addBLEOutBuffer`, `g_sendDisplayText_calls`, `txRingDepth()`; `aprs_functions.cpp` is linked for
real, `node_gcb` exists in `test/support/nrf52/WisBlock-API.h:27`, `setUp` zeroes it). Command (implementer, one
`pio` at a time through the lock wrapper): `pio test -e native_udp_frame_twin -f test_udp_frame_twin`. No new env,
no `platformio.ini` change. Each case loops `side` 0 (esp32) and 1 (nrf52), like `test_agreement_*`.

| Test (new)                                                               | Setup and feed                                                                                           | Assertion                                                                                                                     | Why it fails today                                                                                       |
| ------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------- |
| `test_agreement_gw_foreign_group_not_shown_nor_phoned_on_both` (P-0381)  | `node_gcb[0] = 262`; text to group `9` from `DK5EN-2`; control: same to `262`                            | foreign: `g_sendDisplayText_calls == 0`, `g_ble.size() == 0`, TX ring `depth0 + 1` (still relayed); control: display 1, BLE 1 | `CheckGroup("9") > 0` enters the branch: display 1, BLE 1                                                |
| `test_agreement_gw_no_group_set_shows_every_group_on_both` (D3 pin)      | `node_gcb` all 0; text to group `9`                                                                      | display 1, BLE 1                                                                                                              | passes before and after (pins D3 = A)                                                                    |
| `test_agreement_gw_third_party_dm_not_handed_to_phone_on_both` (P-0382)  | text `hallo{12` to `DK5EN-3`, and text `x:ack12` to `DK5EN-3`; control: `hallo{12` to own call `DK5EN-1` | third-party: `g_ble.size() == 0`, TX ring `depth0 + 1`; control: `g_ble.size() == 1` (existing `:1048` shape)                 | not in the branch, `bBLELoopOut` true, hand-off at ESP32 `:643` / nRF52 `:625` gives `g_ble.size() == 1` |
| `test_agreement_gw_star_respects_nomsgall_on_both` (D2, only if D2 = A)  | `bNoMSGtoALL = true`; text to `*`                                                                        | `g_ble.size() == 0`, still relayed                                                                                            | `*` skips the branch with the flag on, hand-off gives BLE 1                                              |
| `test_agreement_gw_position_and_set_cet_still_reach_phone_on_both` (pin) | position `!` to `DK5EN-3`; text `{CET}2026-11-21 12:00:00` to `DK5EN-3`                                  | `g_ble.size() == 1` each                                                                                                      | passes before and after (pins the exemptions)                                                            |

Before the change the first, third and fourth case must be run red and the output kept in the report. Pure helper
cases (table of dest, call, flag, groups) can be added to the same file. Audit before the change: the existing
`g_ble` assertions (`:780`, `:1192`, `:1209`, `:3327-3461`) use the own call or group `9` with no group set and stay
true; any other that feeds a foreign destination and expects a BLE copy is a pinned instance of the bug and is
changed with a note. Gate: `tools/regression.sh --stage 1,2`.

Build check (no host env for the two files outside the twin):

`pio run -e heltec_wifi_lora_32_V3` (ESP32) and `pio run -e wiscore_rak4631` (nRF52, needs Rosetta, see CLAUDE.md) must compile;
`native_udp_frame_twin` compiles both handler files on the host.

### 2.7 Bench step

Boards: DK5EN-1 (Heltec V3, ESP32, WiFi) and DK5EN-90 (RAK4631 + RAK13800, nRF52, serial). Never DK5EN-98. Rules:
group `TEST` only, DMs only between own bench nodes, TX power at most 2 dBm, `identity_guard.py` first, `lsof` on the
port, load `/bench`.

1. `tools/bench/gwrx_bench.py --node DK5EN-1=tcp:dk5en-1.local --node DK5EN-90=serial:<id> --mock-host <mac-ip>`
   points each node at the mock server (`--gateway on`, `--srvip` to the mock, restored in its `finally`). Frames
   from the mock carry flag `0x80`, so no real gateway uploads them. Build the frames with `gwrx_frame.py`
   (`<id> <src> <dest> <text>`), source and destinations are own bench calls.
2. Observe the phone side: DK5EN-1 over AppLink (`python3 tools/bench/applink_client.py <ip> --capture 20 --out
<file>`, build with `--applink on`) or BLE; DK5EN-90 through the BLE capture (`ble_golden.py`) or the console with
   `--bledebug on` (`BLEtoPhone RingBuff added`).
3. Cases, node with group `262` set (`--setgrc`, `command_functions.cpp:7773`): (a) text
   to `TEST` from a bench call, expected: not on the phone, relayed to LoRa (a second bench node hears it); (b) text
   to `262`, expected: on the phone; (c) DM to a third bench call and its `:ack`, expected: not on the phone, relayed
   to the addressee over LoRa and acked there; (d) DM to the gateway's own call, expected: on the phone once, ack
   goes out; (e) `*` is not sent (bench rule); the `--nomsgall` case is covered by the native test only.
4. Before the fix the same run shows (a) and (c) on the phone. Restore groups and `--gateway`; the driver does the
   gateway part.

### 2.8 Upstream

Both defects exist on `upstream/dev` (`5698913a`): `git show upstream/dev:src/esp32/udp_frame_esp32.cpp` has the
`CheckGroup(destination_call) > 0` branch at `:398` and the `bBLELoopOut` hand-off at `:641`; nRF52 `:332` and `:617`;
`aprs_functions.cpp` has `CheckOwnGroup`. `src/gw_phone_route.h` is fork-only. Fork first; port to an upstream PR later
from `upstream/dev` with the helper header and the two call-site edits (German PR text, minimum port, the twin test is
a fork file).

### 2.9 Risks

- Mesh view: none for the relay (`bUDPtoLoraSend` is not touched; foreign groups and third-party DMs still go to LoRa).
  The gateway ack for groups stays (D4).
- Phone view: a gateway with groups set stops showing foreign groups on the phone and display; a gateway with no group
  set sees no change (D3). Every consumer of `phoneRing` loses third-party DMs (the app does not use them since 4.29;
  the web GUI and AppLink clients read the same ring; D1).
- `*` with `--nomsgall on` stops reaching the phone (D2 = A).
- DK5EN-98 (production gateway): behaviour changes only if it has groups set or `--nomsgall on`; the flash is a
  later, explicit step, not in this plan.
- `{SET}`/`{CET}` are exempted so a gateway's server control frames keep their path; whether these should reach the
  phone at all is a separate question (section 8).

## 3. P-0381 (GW-GROUP-01)

Symptom, root cause, platforms and tests as in section 2 (rows 1, 2 of the test table, group part of the
fix). Evidence: uid 37726 names a call sign, not a group, so it does not prove this path; the ticket stays "found on a
code reading". Closing criterion: the two group cases green on both sides and the bench case (a) and (b) in 2.7.

## 4. P-0382 (GW-DM-01)

Symptom, root cause, platforms and tests as in section 2 (row 3 and the hand-off part of the fix). Depends on the
helper header of GW-GROUP-01: implement in the same wave and commit; if split, GW-GROUP-01 lands first.
Closing criterion: third-party DM and `:ack` give no BLE frame on both sides, own DM still gives one, bench case (c)
and (d). Reporter evidence (1053, 1048, 6481, 18409) came from app users and is inferred to match.

## 5. Evidence checklist

| Claim                                                                                                                                                | Checked                                                                                                              |
| ---------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| Every path:line in section 2 is at HEAD `067d29cd`                                                                                                   | yes, read at HEAD (grep and `sed -n`); the files are unchanged since the pin                                         |
| No commit since the pin touched the UDP files, `lora_functions.cpp`, `aprs_functions.cpp`, `regex_functions.cpp`, the twin test or `test_regex_call` | `git log --oneline 96aaa67c72..HEAD -- <each>` empty                                                                 |
| `ce05f38c` (APPLINK-01) and `f4af55b0`, `8b4d509b` are in `fork-dev`, not reverted                                                                   | `git branch --contains ce05f38c` lists `fork-dev`; the three touch `command_functions.cpp`, not the lines cited      |
| ESP32 and nRF52 read separately                                                                                                                      | yes: `:398`/`:333`, `:189`/`:189`, `:641-643`/`:618-625`, phone calls `:462`/`:413`, `:551`/`:505`                   |
| LoRa reference rule                                                                                                                                  | read `lora_functions.cpp:2104-2120`, `:2243-2276`, `:2335`                                                           |
| APPLINK shares the hand-off                                                                                                                          | `addBLEOutBuffer` `loop_functions.cpp:1003`, drain `phone_commands.cpp:165`, `docs/applink-plan-firmware.md`         |
| Upstream state                                                                                                                                       | `git show upstream/dev:<path>` for both UDP files, `lora_functions.cpp`, `aprs_functions.cpp`, `regex_functions.cpp` |
| Tests run                                                                                                                                            | no (no `pio` in this session); before-run is the implementer's job                                                   |

## 6. P-0210: gateway setup rejects a call sign with a country prefix (GW-CALL-01)

### 6.1 Symptom

Setting the gateway call to `SV/OE3WFA` (T-Beam, reporter OE3WFA, uid 37832) is refused. The string "Invalid
callsign" does not exist in the sources; the refusal is `[ERR]..Callsign <SV/OE3WFA> not valid`, and the web setup
shows "Value could not be set." for it.

### 6.2 Root cause at HEAD

- Pattern `src/regex_functions.cpp:9`: `^[0-9A-Z]?[A-Z]?[0-9]+[A-Z][A-Z]?[A-Z]?[%-]?[0-9]?[0-9]?$`, no `/`.
  `checkRegexCall(String)` at `:22`. Unchanged since `fe517514` ("V4.32e mit GRC", an ancestor of HEAD); no commit
  since the pin (`git log --oneline 96aaa67c72..HEAD -- src/regex_functions.cpp test/test_regex_call` empty).
- `--setcall` at `src/command_functions.cpp:4613` (message `:4615`; the pin said `:4527` and `:4428`, moved by
  `ce05f38c` and `f4af55b0`, which add other code to this file). `--setlog` (`:1542`) and `--setudpcall` (`:4660`)
  use the same check.
- Web route: field "Call-Sign" `src/web_functions/web_functions.cpp:3269` (`maxlength` 9), handler
  `src/web_functions/web_setup.cpp:312-324` calls `commandAction("--setcall ...")`, so the same refusal.
- The BLE/phone setter `src/phone_commands.cpp:504` (comment "Bewusst ohne checkRegexCall()") skips the check, then
  `normalizeOwnCall()`. A slash call set that way is stored but not valid for the mesh:
- Receivers drop it: `src/aprs_functions.cpp:297` (`msg_source_last`), `:307` (`msg_source_call`), `:405` (dest),
  UDP CONF `src/esp32/udp_frame_esp32.cpp:729`, `src/nrf52/udp_frame_nrf52.cpp:713`. The wire-format doc
  (`docs/architecture/11-wire-format.md`, 1.4) states frames failing the regex are dropped. Frames from a slash
  call are therefore dropped by every node running today's code, not only by the gateway.
- `node_call` is `char[10]` (`src/meshcom_settings.h:70`): `SV/OE3WFA` is 9 characters, no room for an SSID.
  The frame structs use 20 + 1 (`MC_CALL_LEN_Z`, `aprs_structures.h:57`), so the wire struct is not the limit.

### 6.3 Platforms

Shared code, no per-platform copy: `regex_functions.cpp`, `command_functions.cpp` and `aprs_functions.cpp` build for
ESP32 and nRF52 alike; the UDP CONF check exists once per platform (listed above) and behaves the same. The T-Beam
plays no part.

### 6.4 Fix design

It is a protocol decision (D5), not a local bug.

- Option A (recommended, this plan): no grammar change. (1) Pin the refusal with a test so it is by design, not by
  accident. (2) Make the refusal say why: `[ERR]..Callsign <%s> not valid (letters, digits and an optional -SSID; no
'/' country prefix, use the home call and the SSID)` in the three `printfdeb` lines of the setters that share the
  check (`:1542`, `:4613`, `:4660`) through one helper `callRejectHint()` in `regex_functions.cpp` (the file already
  holds `normalizeOwnCall()` and `checkRegexCall()`), so the text exists once; the web setup keeps "Value could not be
  set." (3) One paragraph in the user docs and in `docs/architecture/11-wire-format.md` 1.4 stating the grammar
  and the consequence for portable operation. Whether the home call may be used abroad is the operator's licensing
  matter, not asserted here.
- Option B (not planned): accept a `/` prefix (for example `^[0-9A-Z]{1,3}/` before the existing pattern) in
  `checkRegexCall()`. Costs: `node_call[10]` needs 9 + SSID, so a settings layout change (NVS and flash settings,
  `meshcom_settings.h`, config JSON); old firmware drops every frame of such a node (a one-sided mesh); the server
  (not in this repo) may validate too: not verified. Needs an upstream proposal first; no code before that.

### 6.5 Test

Env `native` (`platformio.ini:209`, filter includes `test_regex_call`), file
`test/test_regex_call/test_regex_call.cpp` (13 tests in the suite map; it has no `/` case), command
`pio test -e native -f test_regex_call`.

- `test_lehnt_laenderkennung_mit_schraegstrich_ab` (pin, German like the suite): `checkRegexCall("SV/OE3WFA")`,
  `"SV1/OE3WFA"`, `"OE3WFA/P"` all false. Passes before and after: it pins the decision.
- `test_reject_hint_names_the_slash` (new helper): `callRejectHint("SV/OE3WFA")` contains the hint text; a call
  without a slash gets the plain text. Fails today only because the helper does not exist (before-run: compile error,
  recorded as such). The `printfdeb` call sites are not host-testable; the seam is the helper, which is.

### 6.6 Bench

DK5EN-1 over its 2323 console: `--setcall SV/TEST-1` is refused and the new text appears; the node's call is
unchanged (`--info`). Own call signs only (never set a foreign real call; `SV/...` here is a syntactic probe that is
refused, nothing is stored; restore check with `--info`). No on-air step. DK5EN-98 untouched.

### 6.7 Upstream

`upstream/dev:src/regex_functions.cpp:9` has the same pattern. Option A (hint text, docs) can go upstream as a small
PR; Option B must be discussed with the maintainers first.

### 6.8 Risks

Option A changes a console and web error text only; nothing on air, no stored setting. Anything that parses the old
text (`[ERR]..Callsign <...> not valid`) would still find the prefix: keep it as the first part of the line.

## 7. Backlog rows (Implement table, section 3.2)

`grep -rn "<id>" docs/BACKLOG.md docs/architecture/08-defect-catalogue.md docs/archive/` is empty for all three ids
(checked 2026-11-21).

| Id          | Area                | Item                                                                                                                                                                                                                                                                                                                                                                            | Status                                          | Next step                                                                                 |
| ----------- | ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------- | ----------------------------------------------------------------------------------------- |
| GW-GROUP-01 | Gateway / UDP route | The UDP path accepts every numeric group by shape (`CheckGroup()`, `udp_frame_esp32.cpp:398`, `udp_frame_nrf52.cpp:333`), the LoRa path only own groups (`CheckOwnGroup()`, `lora_functions.cpp:2248`): a gateway with groups set shows and hands foreign groups to the phone. Plan [plan-gateway-routing-20261121.md](plan-gateway-routing-20261121.md). Problem file P-0381.  | Open 2026-11-21: plan written, not implemented. | Decisions D2-D4, then helper `gw_phone_route.h` and the twin tests (plan 2.5, 2.6).       |
| GW-DM-01    | Gateway / UDP route | The gateway relay hands every frame outside the UDP branch to the phone link (`bBLELoopOut`, `udp_frame_esp32.cpp:641-643`, `udp_frame_nrf52.cpp:618-625`): third-party DMs and their `:ack` reach the app, web GUI and AppLink. Shares the hand-off with AppLink (`ce05f38c`). Plan [plan-gateway-routing-20261121.md](plan-gateway-routing-20261121.md). Problem file P-0382. | Open 2026-11-21: plan written, not implemented. | Decision D1, implement with GW-GROUP-01 (same helper), twin test and bench (plan 2.7).    |
| GW-CALL-01  | Call sign grammar   | `--setcall` and the web setup refuse a call with a country prefix (`SV/OE3WFA`): no `/` in `regex_functions.cpp:9`, gate `command_functions.cpp:4613`; every receiver drops such frames. Plan [plan-gateway-routing-20261121.md](plan-gateway-routing-20261121.md). Problem file P-0210.                                                                                        | Open 2026-11-21: plan written, not implemented. | Decision D5; Option A is a hint text, a pin test and docs; Option B is a protocol change. |

## 8. Other findings (not fixed here)

- `{SET}`/`{CET}` server control frames reach the phone through the same hand-off (both UDP files); whether the app
  needs them is not established. Exempted in the design, pinned by a test.
- A group message whose text contains `:ack3` or `:rej3` is parsed as an ack in the UDP branch (`iAckPos`, ESP32
  `:409-410`; `*` is excluded explicitly, numeric groups are not) and produces a phone ack frame. Not reproduced.
- The problem files carry stale lines: P-0210 `command_functions.cpp:4527` (now `:4613/:4615`), web field
  `web_functions.cpp:3283` (now `:3269`), `web_setup.cpp:304-315` (now `:312-324`); P-0381/P-0382 numbers are current.
- The gateway ack for a group message is sent for every numeric group (LoRa `:2335`, UDP end of branch): a gateway
  without that group acknowledges a group it does not hold (kept, D4).

## Operator decisions 2026-11-21

- D1 A: third-party DMs and their acks no longer reach the phone link.
- D2 A: `--nomsgall on` also hides `*` on the UDP path.
- D3 A: a node with no group keeps seeing every group. D4 A: the gateway keeps acking foreign groups.
- D5 A: the refusal of `/` in call signs stays; the error text explains it, plus docs.
