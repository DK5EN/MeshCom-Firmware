# APRS text limits: implementation plan (P-0044, P-0386, P-0179)

Date: 2026-11-21. Branch `fork-dev` at `067d29cd`. Firmware pin of the problem files: `96aaa67c72`
(ancestor of HEAD, `git merge-base --is-ancestor` rc 0). Nothing is implemented; this file is the plan.
Problem files (read-only): `~/homeautomation/telegram/problems/P-0044-*.md`, `P-0386-*.md`, `P-0179-*.md`,
related by-design `P-0053-*.md`. Operator decisions already taken:
`~/homeautomation/telegram/problems/DECISIONS-2026-11-21.md`.

Step 0 for the executing agent: copy this file to `docs/campaign-aprs-text-limits-20261121.md` (the resume
point), update it per step and `docs/RESUME.md` when a step lands.

## 1. Decisions on record and decisions still open

Taken (do not re-open):

| Source                               | Ruling                                                                                                                                                                                                                                |
| ------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| CHR-02 (FIXED 2026-08-31 `361ac9fe`) | Free text passes as UTF-8, only controls, bidi marks and the separators `{ } : ; , /` are stripped; truncation is UTF-8-safe at 25/16 bytes; the storage stays unfiltered (`docs/archive/backlog-journal-20260818-20260929.md:1908`). |
| P-0053 (decision file, line 14)      | Kept open as tracking. The umlaut passthrough is by design; it does not cover the byte cap, so P-0386 stays a separate question.                                                                                                      |
| P-0179 (decision file, line 25)      | Stays `affected` on both frontmatter lines; the fork keeps it as an open item.                                                                                                                                                        |

Open, each stated again where it bites. Nothing below is decided by this plan.

| Id  | Question                                                                   | Ticket |
| --- | -------------------------------------------------------------------------- | ------ |
| D1  | Is the 16-byte cap in the LoRa-APRS Track frame intentional?               | P-0044 |
| D2  | Does the Track frame carry the station name? (needs the reporter's answer) | P-0044 |
| D3  | Which atxt limit is the contract, and how is it shown to the operator?     | P-0386 |
| D4  | What happens when the tags leave no room for comment and name?             | P-0179 |

## 2. Facts shared by all three tickets

Limits as they stand at HEAD (all verified by reading the lines):

| Place                                                            | Limit                                   | Counts             | Where                                                                                  |
| ---------------------------------------------------------------- | --------------------------------------- | ------------------ | -------------------------------------------------------------------------------------- |
| `node_atxt` storage                                              | 39 usable, `char[40]`                   | bytes              | `src/meshcom_settings.h:102`                                                           |
| `--atxt` / `--aprscomment` setter clip                           | `RM_ATXT_MAX` = 39, `String::substring` | bytes, UTF-8-blind | `src/command_functions.cpp:4576-4593` (clip at :4590), `src/rm_commands.h:81`          |
| Web setup field "APRS Comment"                                   | `maxlength` 25                          | UTF-16 units       | `src/web_functions/web_functions.cpp:3593`                                             |
| Mesh position comment (`PositionToAPRS()`)                       | 25                                      | bytes, UTF-8-safe  | `src/loop_functions.cpp:5471`                                                          |
| LoRa-APRS plain frame `encodeLoRaAPRS()`                         | 25                                      | bytes, UTF-8-safe  | `src/aprs_functions.cpp:1321` (dead: only a commented call, `loop_functions.cpp:5808`) |
| LoRa-APRS Track frame `encodeLoRaAPRScompressed()`               | 16                                      | bytes, UTF-8-safe  | `src/aprs_functions.cpp:1435`                                                          |
| Receiver region (comment + `#` + name), nodes from `16aea1e1`    | 47 bytes (buffer 48)                    | bytes              | `src/aprs_functions.cpp:813-817` (`decodeAPRSPOS()` at :722)                           |
| Tags + comment + name budget                                     | 100, `strconcat[100]`                   | bytes              | `src/loop_functions.cpp:5652`, :5678, :5684                                            |
| `docs/rm1-commands.md:287,644` says `atxt` is "1..39" characters | (documentation)                         | characters         | disagrees with the beacon                                                              |

Reuse found (grep `charset_utf8_safe_truncate`, `mcSet`, `msg_input`): `charset_utf8_safe_truncate()`
(`src/charset_filter.cpp:209`) is the one UTF-8-safe cut and is already used by `src/msg_input_budget.h`
and the web message clamp (`web_functions.cpp:4851`, `substring(0, charset_utf8_safe_truncate(...))`).
No second helper is needed for the cut. The sequence "copy, `charset_filter_apply(...STRIP_SEPARATORS)`,
`charset_utf8_safe_truncate`, NUL" exists three times (`aprs_functions.cpp:1317-1322`, `:1426-1436`,
`loop_functions.cpp:5462-5473`); a named helper for it is the seam in section 4.

Receiver constraint (why the mesh cap stays at 25): `decodeAPRSPOS()` reads the comment and name from one
region of at most 47 bytes, sized for "atxt 25 + `#` 1 + name 19 = 45" (`aprs_functions.cpp:809-811`,
comment in the source). A longer comment on the mesh path pushes the name out of that region on every
node from `16aea1e1` (2026-09-13) on, and `docs/architecture/11-wire-format.md:243` documents 25 bytes as the wire contract for
apps and servers too. Raising the mesh cap is therefore a wire change against old nodes (R4 blocker class).
The 47-byte region holds only from `16aea1e1` on. Older receivers are stricter: they keep at most 25 characters of
the comment and stop at the first space or `/` (`git show 16aea1e1^:src/aprs_functions.cpp`, lines 639-656), so a
longer comment, or one with a space, is cut there as well. That strengthens the case against raising the cap.

Byte budget (REVIEW.md R4). Frame = 6 header + source path (9 + hops x 10 + 1) + destination and type
(2 for `*`) + payload; `encodeAPRS()` clamps at 245 (`aprs_functions.cpp:1141-1142`). Position payload =
19 bytes of coordinates and symbols (`"%07.2lf%c%c%08.2lf%c%c"`, `loop_functions.cpp:5689`) + comment +
name + tags. Position default hop is `MAX_HOP_POS_DEFAULT` 2, the limit is `MAX_HOP_LIMIT` 7
(`src/configuration_global.h:403-404`).

| Hops | Header + path + destination | Payload room (245 minus that) | Comment + name + tags room (minus 19) |
| ---: | --------------------------: | ----------------------------: | ------------------------------------: |
|    2 |                          38 |                           207 |                                   188 |
|    7 |                          88 |                           157 |                                   138 |

Today the sum is capped at 100 (+ buffer limit 99 for the tags alone), so a position payload is at most
119 bytes. Airtime estimate, assumed SF11, BW 250 kHz, CR 4/6, 8 preamble symbols, CRC on (not measured in
this session): about 10 ms per payload byte, 121 vs 151 payload bytes is 1.49 s vs 1.79 s of airtime.
Repo measurement for comparison: `docs/2026-10-24-dk5en98-txp-own-msg-echo-release.md:181` (1.1 s for
100-120 bytes, other radio settings). APRS-IS limits: nothing here comes near the 512-byte line limit; the
comment recommendations of APRS101 (about 40 to 43 bytes for a position comment) were not looked up in
this session and are not used as an argument.

Common to all fixes: the shared code (`aprs_functions.cpp`, `loop_functions.cpp`) is built for ESP32 and
nRF52 alike. No `#if` guards sit around the touched ranges; `[nrf52_base]` (`platformio.ini:105-123`)
excludes only the ESP32, display and T-Deck directories. `sendPosition()` is called from
`src/esp32/esp32_main.cpp:5050,5054` and `src/nrf52/nrf52_main.cpp:2369,2373`. One exception to check: ESP32
board `BOARD_TLORA_OLV216` forces `bSendViaAPRS = false` (`loop_functions.cpp:5778-5780`), so it never sends
the Track frame.

## 3. P-0044: APRS text cut in the LoRa-APRS Track frame (APRS-TXT-01)

### Symptom

DO1TFS (2026-04-25): the APRS text is cut and the name is missing, only with LoRa-APRS or Track on. No
firmware, hardware or viewer named.

### Root cause at HEAD

- Cut, confirmed: `src/aprs_functions.cpp:1435` `iatxt = charset_utf8_safe_truncate(catxt, iatxt, 16);`
  inside `encodeLoRaAPRScompressed()` (:1338); the frame is built at :1440 (`... P[%s`, no `node_name`).
  Caller: `src/loop_functions.cpp:5809` in `sendPosition()` behind `bSendViaAPRS` (set at :5735-5738 for
  `bDisplayTrack && posinfo_fix`, at :5754-5759 for the `--sendtrack` send, `command_functions.cpp:4510-4512`).
  The mesh position of the same node keeps 25 bytes (`loop_functions.cpp:5471`) and appends `#name`
  (:5477-5480). In Track mode the node alternates: LoRa-APRS frames with 16 bytes and no name, and mesh
  frames with 25 bytes and name whenever no LoRa frame was heard for 15 s or the periodic interval is due
  (:5762-5776). That fits "only with LoRa-APRS or Track on".
- Origin: `b1e15f27` (2026-03-27, "v4.35p aprs track proto fixed", first tag `v4.35p_04_05`) added
  `substring(0, 16)`; `2e874921` carries the UTF-8-safe form (CHR-02 `361ac9fe` made it safe, the 16 stayed).
  `git blame` of :1435 shows `2e874921`. Both are ancestors of HEAD and of `upstream/dev`; no revert touches
  the line (`git log -S'utf8_safe_truncate(catxt, iatxt, 16)'` finds only `2e874921`).
- Since the pin: `git log --oneline 96aaa67c72..HEAD -- src/aprs_functions.cpp` is empty. `loop_functions.cpp`
  changed in `ce05f38c` (AppLink) and `9283e6a3` (pong id), neither touches the beacon path
  (`git log 96aaa67c72..HEAD -S'catxt' -- src` and `-S'encodeLoRaAPRScompressed'` are empty).
- No commit message or comment says 16 is deliberate. Not established: why 16.
- Name, undetermined: no version of `encodeLoRaAPRScompressed()` wrote `node_name`; `#name` is MeshCom syntax
  of the mesh position (see `docs/architecture/11-wire-format.md:238-247`). `encodeLoRaAPRSText()`
  (`aprs_functions.cpp:1454-1487`) has no cap and no atxt.

### Platforms

ESP32 and nRF52 run the same `sendPosition()` and encoder (section 2); the report names no board.

### Fix design

1. Decision D1 first. Options:
   - (a) Raise to 25, the cap of every other path (recommended: one number to explain). Cost: up to 9 more
     bytes per Track frame, about 90 ms airtime (estimate), no receiver change on the MeshCom side; LoRa-APRS
     iGates and trackers that read the frame were not reviewed.
   - (b) Keep 16 and document it (`--atxt` help, `docs/rm1-commands.md`): ticket becomes by-design.
   - (c) Raise above 25: no evidence for a limit; not recommended without one.
2. Under (a): add `#define APRS_ATXT_WIRE_MAX 25` next to `MC_PAYLOAD_LEN` in `src/aprs_structures.h:58`
   (included through `aprs_functions.h` by both files) and use it at `aprs_functions.cpp:1321`, `:1435` and
   `loop_functions.cpp:5471`. Behaviour change: :1435 only. Delete the stale comment "same 25-byte cap ...
   (aprs_functions.cpp:632)" in the three places or point it at the constant.
3. D2 (name in the Track frame): not part of this fix. Recommendation: do not add `#name`; on a standard APRS
   viewer it would show as literal comment text and costs up to 20 bytes. Ask DO1TFS or OE1KBC what "name"
   meant (open question 2 of the problem file). Operator decides; if yes, it is a second step with its own
   budget line (payload of the Track frame is then 25 + 1 + 19 bytes at most).

### Test (fails today)

Env `native_aprs_fuzz` (sanitizers on; `platformio.ini:1660-1680`), suite `test/test_lora_aprs_encode/
test_lora_aprs_encode.cpp` (4 cases today, only a 5-byte `node_atxt = "track"`). Add:

- `test_track_beacon_keeps_25_bytes_of_comment`: `node_atxt = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123"` (30 bytes),
  `encodeTrack()`, then `TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJKLMNOPQRSTUVWXY", strstr(tnc2(buf), " P[") + 3)`.
  Today the tail is `ABCDEFGHIJKLMNOP` (16 bytes), so it fails.
- `test_track_beacon_cut_never_splits_utf8`: 13 x `C3 84` (26 bytes); expects 12 whole sequences (24 bytes)
  after ` P[`. Today it yields 8 sequences (16 bytes), fails.
- Keep `test_track_beacon_golden_frame` unchanged (5-byte text).

Command (one `pio` at a time, through the lock wrapper): `pio test -e native_aprs_fuzz -f test_lora_aprs_encode`.
Under (b) no code test; a source-pin is not worth it.

### Bench

Optional. Board: a bench node (DK5EN-1 or -14, own call sign), `--airgap on` first so nothing is radiated
(INSTRUMENT build, `docs/bench-airgap.md`), `--info on`, `--atxt` with 30 bytes, `--sendtrack`. Expect the
console line `[LO-APRS]...` (`loop_functions.cpp:5814-5817`) to end in ` P[` plus 25 bytes. No on-air step is
needed; if one is wanted, TX power at most 2 dBm and the operator's explicit OK (a position frame is not
TEST-group traffic). Run `identity_guard.py` and load `/bench` before.

### Upstream impact

Same code in `upstream/dev` (`5698913a`): `aprs_functions.cpp:1388` `..., 16)`, :1393 the frame. Upstream PR
candidate after D1 (a); the PR body is German per `CLAUDE.md`.

### Risks

On-air bytes: +9 on the Track frame only. LoRa-APRS receivers outside MeshCom were not examined. Changing the
constant touches three sites; the dead `encodeLoRaAPRS()` keeps working.

## 4. P-0386: the comment is cut at 25 bytes, the limits disagree (APRS-TXT-02)

### Symptom

"Meshcom Gro\xC3\x9Fer Inselsberg" is 25 characters and 26 bytes. The web field accepts it, `--atxt` stores
it, the beacon sends 25 bytes and silently loses the final `g`. Stored and transmitted text differ; no message.

### Root cause at HEAD

- Cut: `src/loop_functions.cpp:5471` `charset_utf8_safe_truncate(catxt_src, iatxt, 25)`; the byte cap is
  intentional (comments :5464-5467, `src/charset_filter.h:119-125`) and matches the receiver (section 2).
  The defect is the mismatch of what the operator may enter: web 25 UTF-16 units (:3593), `--atxt` 39 bytes
  (`command_functions.cpp:4590-4593`), documentation "1..39 characters" (`docs/rm1-commands.md:287,644`),
  beacon 25 bytes. Byte arithmetic: 8 + 3 + 2 + 2 + 1 + 10 = 26.
- Related, same setter: `sVar.substring(0, RM_ATXT_MAX)` (`command_functions.cpp:4590-4591`) counts bytes
  but is UTF-8-blind. Text of 25 two-byte characters (50 bytes, accepted by the web field) is clipped at byte 39,
  which is inside a sequence; the stored text ends in a broken byte. The beacon cut (`utf8_safe_truncate` at 25)
  hides it on air, but the stored text, the app (`sendAPRSset()`) and `--info` show it.
  `RM_ATXT_MAX` is pinned at 39 by `test/test_rm_text` (`TEST_ASSERT_EQUAL_UINT(39, RM_ATXT_MAX)`).
- Since the pin: `git log --oneline 96aaa67c72..HEAD -- src/loop_functions.cpp src/aprs_functions.cpp
src/rm_commands.h src/meshcom_settings.h src/charset_filter.cpp` shows only `ce05f38c` and `9283e6a3` (not
  related); `command_functions.cpp` and `web_functions.cpp` changed in `ce05f38c`, `8b4d509b`, `f4af55b0`, none
  in the atxt setter or field (`git log -S'RM_ATXT_MAX'` empty). Line numbers moved: the setter was cited at
  :4502, it is :4590 now.

### Platforms

Setter, web page and beacon are shared code (`nrf52_main.cpp:154` includes `web_functions.h`; whether the RAK
serves the setup page was not checked); BLE, serial and the web field reach the same setter. `ble_settings_v1` carries `node_atxt[40]` (`src/nrf52/ble_settings_v1.h:128`, offset
pinned at 280, `:347`): the buffer must not change.

### Decision D3 (operator)

Which limit is the contract?

| Option | What it means                                                                                                                                                             | Consequence                                                                                                                                                                                                              |
| ------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| A      | Bytes stay the contract (25 on the wire). Keep storing up to 39. Tell the operator at set time how many bytes go on air; fix docs and the UTF-8-blind clip. (recommended) | No wire change, no settings change, no RM contract change. The comment is still cut; now visibly.                                                                                                                        |
| A1     | As A, but clip the stored text at 25 bytes.                                                                                                                               | Changes the RM write contract: `writeText` compares stored with argument (`rm_exec_write.cpp:69-71`), a 30-byte `atxt` would answer `err failed`; `rmLen`, `docs/rm1-commands.md`, `test_rm_text` follow. Larger change. |
| B      | Characters become the contract.                                                                                                                                           | Not workable: 25 characters are up to 100 bytes; receivers read at most 47 bytes from `16aea1e1` on, older ones at most 25 characters up to the first space or `/` (section 2).                                          |
| C      | Raise the byte cap, at most 27 (47 - 1 - 19).                                                                                                                             | Gains 2 bytes; apps and servers parse the 25-byte contract (`11-wire-format.md:243`), unknown limits. Not recommended.                                                                                                   |
| D      | Leave as is, document only ("25 bytes UTF-8" in help and docs).                                                                                                           | Cheapest; the operator still learns it only by reading.                                                                                                                                                                  |

### Fix design (written for A; D reduces it to step 4)

1. Helper in `src/charset_filter.{h,cpp}` (already built by env `native`, `platformio.ini:301`):
   `size_t charset_wire_field(const char *src, char *dst, size_t dst_size, size_t cap, size_t *cut_bytes)`.
   It does copy, `charset_filter_apply(..., CHARSET_FILTER_STRIP_SEPARATORS)`, `charset_utf8_safe_truncate`
   to `min(cap, dst_size - 1)`, NUL, returns the length, and reports in `cut_bytes` how many bytes the cap
   removed (not the separator strip). Replace the three copies (`aprs_functions.cpp:1317-1322`,
   `:1426-1436` after its `trim()`, `loop_functions.cpp:5468-5472`). Cap value: `APRS_ATXT_WIRE_MAX`
   (defined in P-0044 step 2; whichever lands first defines it).
2. Setter, `command_functions.cpp:4590-4591`: replace the clip with
   `sVar = sVar.substring(0, charset_utf8_safe_truncate(sVar.c_str(), sVar.length(), RM_ATXT_MAX))`, the same
   pattern as `web_functions.cpp:4851`. Independent of D3, a bug fix.
3. Notice at set time: after storing, call `charset_wire_field` on the stored text and, when `cut_bytes > 0`,
   `printfdeb("[ATXT] %u bytes, only the first %u go on air\n", ...)`. No on-air change; BLE (`sendAPRSset()`)
   and RM paths stay as they are.
4. Docs: `docs/rm1-commands.md:287,644` ("1..39 characters" becomes "up to 39 bytes stored, the first 25 bytes go
   on air"), `--aprscomment` help text (`command_functions.cpp:1772`), and `docs/architecture/11-wire-format.md`
   lines still citing old line numbers. The web field `maxlength` 25 stays (HTML counts UTF-16 units, R4 red
   flag, cannot be made exact); the setter and the notice are the enforcement.

### Test

Suite `test/test_charset_filter` in env `native` (29 cases today, `docs/test-suite-map.md:201`), command
`pio test -e native -f test_charset_filter`. Add:

- `test_wire_field_26_byte_umlaut_comment`: input `"Meshcom Gro" C3 9F "er Inselsberg"` as a char array (the
  suite already avoids `\x` literal pitfalls, see its header), cap 25: expects length 25, last byte `r`, `cut_bytes`
  = 1. Red only because `charset_wire_field` does not exist yet, so the suite does not build (red = no build). This
  assertion is the one decision A makes observable.
- `test_wire_field_separator_strip_is_not_counted_as_cut`: `"a,b/c"` cap 25: length 3, `cut_bytes` 0.
- `test_wire_field_never_splits_utf8`: 13 x `C3 84`, cap 25: length 24, `cut_bytes` 2.
- Characterization that passes today and must keep passing after the refactor: a 26-byte comment gives exactly
  25 bytes (pins the wire cap so nobody changes it silently).
- Setter (UTF-8-safe clip): the repo's style is a source pin in `test/test_command_setters`
  (env `native_command_setters`, `test_build_src = no`, `command_functions.cpp` is not compiled natively): assert
  that the `atxt` setter block contains `charset_utf8_safe_truncate(` and no bare `substring(0, RM_ATXT_MAX)`.
  Fails today (the block has the bare substring). This setter source pin is the only firmware-red test of P-0386: it
  fails against the unchanged `command_functions.cpp`. The helper tests above are red only because the helper does
  not exist yet; say so in the commit.

### Bench

None required. Optional: `--atxt` with the 26-byte text via the 2323 console, expect the `[ATXT]` notice and a
stored read-back of 26 bytes; `--airgap on`.

### Upstream impact

Same lines upstream (`loop_functions.cpp:5301`, `aprs_functions.cpp:1274`, `meshcom_settings.h:102`,
`web_functions.cpp:3544` maxlength 25). `RM_ATXT_MAX` upstream (`rm_commands.h:76`) is the same 39. So an
upstream PR is possible, but `test/` and `docs/` are fork-only; the PR carries the code only.

### Risks

On-air bytes: none (no cap changes). Helper replaces three code paths that all build the beacon; the golden
frames (`test_lora_aprs_encode`, `test_aprs_*`) must stay byte-identical. Stored-text rule change: texts already
stored with a broken byte stay broken until the operator rewrites them.

## 5. P-0179: position beacon drops comment and name when the tags are long (APRS-TXT-03)

### Symptom

Nodes with many tags (battery, altitude, weather, group calls) show an empty comment and, worse, an empty name
on the dashboard and node list, while the app shows both (OE3BCB, Preine, upstream issue 1120).

### Root cause at HEAD

- `src/loop_functions.cpp:5652` `char strconcat[100]`; tags appended with bounded `strncat` (:5653-5673);
  :5678-5681 `if(strlen(strconcat)+strlen(catxt)+strlen(cname) > 100) catxt[0]=0x00;`, :5684-5687 the same test
  clears `cname[0]`; :5689 builds the frame. The order is comment first, name second, silently.
- History (blame at HEAD): `fbd6b7d9` (2026-07-05, first tag `v4.35p.07.05`) introduced the dropping with 150,
  `73cdcebd` (2026-07-17, first tag `v4.35p.07.17`) lowered the limit and the buffer to 100; its message ("E22
  XML check XML running") gives no reason. `51c3c317` (2026-09-03) made the appends bounded and left the
  dropping. All three are ancestors of HEAD and `upstream/dev`; none reverted. Since the pin:
  `git log 96aaa67c72..HEAD -S'catxt[0]=0x00' -- src` and `-S'strconcat[100]'` are empty;
  `loop_functions.cpp` changed in `ce05f38c` and `9283e6a3` only.
- Side effect of the same lines: when the tag set alone exceeds 99 bytes (`C02` note at :5654-5655: worst case
  about 111), `strncat` cuts the tail mid-tag (`/Y=1`, `/R=...`), so the beacon can carry a half tag.
  `docs/architecture/11-wire-format.md:317-321` states the `/X=` tail "is never truncated"; that is wrong at
  HEAD.
- The 100-byte rule is documented as the wire design (`11-wire-format.md:317-321`).
- Not traced: reports 34763 ("only 2 characters") and the `##` in the raw data (web setter path); the budget
  does not explain a 2-character limit. Not part of this plan.

### Platforms

`PositionToAPRS()` (`loop_functions.cpp:5391`) is shared; ESP32 (the reporter's Heltec V3) and nRF52 run the same
code. Three callers: :5898 (Track echo to app/gateway), :5950 (mesh), :6037 (`sendAPPPosition()`). The tag
sources differ by board (weather sensors, INA226), which decides how often it happens, not whether.

### Decision D4 (operator)

The upstream answer to issue 1120 was `wontfix` ("shorten comment and name"; from the problem file, the issue
was not re-read here). Options:

| Option | What it means                                                                                            | Consequence                                                                                                                                                                                                                                                                                                                                                                                           |
| ------ | -------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| B0     | Keep 100, add a visible notice (serial marker and `--info`) when comment or name is dropped.             | No wire change; the operator learns why. Texts on weather nodes stay blank. (recommended minimum)                                                                                                                                                                                                                                                                                                     |
| B1     | B0 plus raise the budget to 130 and `strconcat` to 112 bytes (worst-case tag set about 111 plus margin). | Payload up to 19 + 130 = 149 bytes; fits R4 at 7 hops (157) and 2 hops (207). Worst-reported node (tags 84-90 + 25 + 20 = 129-135) is just covered. Cost: up to +30 bytes, about +0.3 s airtime (estimate), on those nodes only. Also stops the mid-tag cut. Every receiver reads a longer payload; `decodeAPRSPOS()` handles it (`PayloadBuffer` is `MC_PAYLOAD_LEN + 3`), apps and servers unknown. |
| B2     | Drop telemetry tags before the operator texts.                                                           | Needs a ranking of the 17 keys (`11-wire-format.md` section 1.8.2); dropping `/R=` or `/B=` changes what servers and maps see. Operator's ranking required.                                                                                                                                                                                                                                           |
| B3     | Close as `wontfix` and document "shorten comment and name".                                              | Follows upstream; no code.                                                                                                                                                                                                                                                                                                                                                                            |

### Fix design (B0 always, B1 only on request)

1. Seam: new pure header `src/pos_text_budget.h` (Arduino-free, same pattern as `src/pos_tag_nan.h`):
   `#define POS_TEXT_BUDGET 100` and
   `struct PosTextFit { bool atxt_dropped; bool name_dropped; };`
   `PosTextFit posTextFit(size_t tags_len, char *atxt, char *name, size_t budget)` with exactly the present
   order (comment, then name; strict `> budget`). `PositionToAPRS()` calls it instead of the two blocks at
   :5678-5687 and keeps the buffer sizes.
2. Notice: `size_t posTextDropMarker(char *out, size_t n, const PosTextFit &f, size_t tags_len, size_t budget)`
   formats `[POS];txtdrop;atxt;tags;84;budget;100` (`name`, or `atxt+name`). The call site prints it with raw
   `Serial.printf` like the existing `[POS];shot;suppressed` marker (`loop_functions.cpp:5713`), once per
   change of state, not per beacon (beacons repeat every few minutes; the existing marker pattern is
   rate-limited the same way).
3. B1 only: change `POS_TEXT_BUDGET` to 130 and `strconcat[100]` (:5652) to `[112]`; one place for the number.
4. Docs: `11-wire-format.md:317-321` (budget, drop order, "never truncated" claim), `--info` help if the marker is
   added there.

### Test

New suite `test/test_pos_text_budget/test_pos_text_budget.cpp` in env `native` (add to its `test_filter`,
`platformio.ini` around :240; header-only, `-I src`), command `pio test -e native -f test_pos_text_budget`.
Cases:

- `tags 47, atxt 25, name 20` (92): nothing dropped.
- `tags 60, atxt 25, name 15` (100): nothing dropped (strict `>`); `tags 61`: only atxt dropped, name kept.
- `tags 84, atxt 25, name 20` (129): atxt and name both dropped (the reported weather-node case, pins B0).
- Marker text for each of the three outcomes (exact string).
- B1 only: the same `84/25/20` case with the production `POS_TEXT_BUDGET` expects nothing dropped. This one
  fails today (budget 100 drops both) and is the red test for B1. For B0 alone the red test is the missing
  helper (red = no build); say so in the commit.
- Optional with B1: worst-case tag set (111 bytes) is kept whole in a 112-byte buffer (formatter-level test
  needs the tag builder; if not extractable, skip and rely on the bench).

`PositionToAPRS()` itself is not host-testable (`loop_functions.cpp` is in no native env); the seam is the
point. `test/test_pos_tag_nan` and `test/test_mcp17_bits` do not touch `catxt`/`cname`.

### Bench

Board DK5EN-1 or -14 (own call), `--airgap on`, `--info on`. Make a long tag set with `--grc` (six groups add up
to 49 bytes of `/R=`), battery, altitude, 19-byte `--setname`, 25-byte `--atxt`; `--sendpos`. Expect the
`[POS];txtdrop` marker (B0) or, with B1, the console payload carrying comment and name (the executor confirms
which console line shows the payload). No on-air beacon without the operator's OK (broadcast rule); TX at most
2 dBm if one is allowed; check that a second own bench node lists the name and comment (mheard).

### Upstream impact

Same code upstream (`loop_functions.cpp:5482`, :5508-5516, budget 100; `5698913a`). Issue 1120 was closed
`wontfix`; a PR would be B0 (notice) at most, with B1 argued by the R4 table. Fork first; the operator decides
whether to PR.

### Risks

On-air bytes (B1): payload up to 149 bytes, inside R4 for 7 hops; a deployed node may still be near the 245
limit because the path grows per hop. The receivers' tag parsers scan forward and take any length, the
comment/name region does not move. The notice must not spam the log. Old nodes: no behaviour change under B0.

## 6. Order and independence

1. APRS-TXT-01 (one constant, one test) can land alone after D1.
2. APRS-TXT-02 (helper, setter, notice, docs) can land alone after D3; touches the same three lines as
   APRS-TXT-01 (trivial merge; land 01 first).
3. APRS-TXT-03 (new header, call-site change in `loop_functions.cpp`, docs) is independent of the other two.

Gate per step: `tools/regression.sh --stage 1,2`; no bare `pio test`; board builds for ESP32 (`heltec_v3`,
`ttgo_tbeam`) and nRF52 (`rak4631`) because the touched code is shared (R2). Needs the Rosetta note of
`CLAUDE.md` for nRF52.

## 7. Evidence checklist

| Claim                                                                                                                       | Check done at `067d29cd`                                                                                                                                              |
| --------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Commits `fbd6b7d9`, `73cdcebd`, `b1e15f27`, `2e874921`, `51c3c317`, `361ac9fe`                                              | each `git merge-base --is-ancestor <c> HEAD` rc 0 (and in `upstream/dev` for the first five); not reverted: lines present at HEAD, `git blame` cites the same commits |
| No change since the pin                                                                                                     | `git log 96aaa67c72..HEAD` on the seven source files: only `ce05f38c`, `9283e6a3`, `8b4d509b`, `f4af55b0`, none touches the lines                                     |
| `aprs_functions.cpp:1321`, :1435, :1338, :1440, :1454-1487; `loop_functions.cpp:5471`, :5652, :5678-5687, :5694, :5806-5809 | read at HEAD                                                                                                                                                          |
| Web `maxlength` 25 at `web_functions.cpp:3593` (name field :3592 also 25, `node_name` holds 19; not fixed here)             | read at HEAD; the scout's :3604 is :3593 now                                                                                                                          |
| ESP32 and nRF52 both call `sendPosition()`                                                                                  | `esp32_main.cpp:5050,5054`, `nrf52_main.cpp:2369,2373`; no `#if` in the touched ranges; `[nrf52_base]` filter read                                                    |
| Upstream state                                                                                                              | `git show upstream/dev:` for `aprs_functions.cpp`, `loop_functions.cpp`, `rm_commands.h`, `meshcom_settings.h`, `web_functions.cpp`: same lines                       |
| Airtime figures                                                                                                             | estimate under assumed SF11/BW250/CR4/6, not measured                                                                                                                 |
| Not verified                                                                                                                | upstream issue 1120 text, APRS101 limits, third-party LoRa-APRS receivers, any hardware or `pio` run (none done in this session)                                      |

## 8. Backlog rows (for the Implement table of `docs/BACKLOG.md`, section 3.2; not yet added)

| Id          | Area                   | Item                                                                                                                                                                                                                                                                                                 | Status 2026-11-07                               | Next step                                                                                                                                                      |
| ----------- | ---------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| APRS-TXT-01 | APRS / LoRa-APRS Track | The LoRa-APRS Track beacon cuts the APRS comment at 16 bytes while the mesh position keeps 25 (`aprs_functions.cpp:1435`); DO1TFS 2026-04-25. Plan [plan-aprs-text-limits-20261121.md](plan-aprs-text-limits-20261121.md). Problem file P-0044.                                                      | Open 2026-11-21: plan written, not implemented. | Operator D1 (is 16 intentional?). Then `APRS_ATXT_WIRE_MAX` 25, test in `test_lora_aprs_encode`, optional airgap bench.                                        |
| APRS-TXT-02 | APRS / text limits     | The APRS comment is cut at 25 bytes on air while the web field allows 25 characters and `--atxt` 39 bytes; the setter clip splits UTF-8 (`loop_functions.cpp:5471`, `command_functions.cpp:4590`). Plan [plan-aprs-text-limits-20261121.md](plan-aprs-text-limits-20261121.md). Problem file P-0386. | Open 2026-11-21: plan written, not implemented. | Operator D3 (which limit is the contract; plan recommends A). Then `charset_wire_field`, UTF-8-safe setter clip, notice, docs, tests in `test_charset_filter`. |
| APRS-TXT-03 | APRS / position beacon | The position beacon silently drops the comment, then the name, when tags + texts exceed 100 bytes (`loop_functions.cpp:5678-5687`). Plan [plan-aprs-text-limits-20261121.md](plan-aprs-text-limits-20261121.md). Problem file P-0179.                                                                | Open 2026-11-21: plan written, not implemented. | Operator D4 (notice only, raise to 130, tag ranking or wontfix). Then `pos_text_budget.h`, new suite `test_pos_text_budget`, docs.                             |

## 9. Other findings (not fixed here)

- Web name field `maxlength` 25 (`web_functions.cpp:3592`) against `node_name[20]` / `RM_NAME_MAX` 19; the name
  setter clips with the same UTF-8-blind `substring` (`command_functions.cpp:3815`).
- `docs/architecture/11-wire-format.md:317-321` claims the tail is never truncated; the C02 bounded `strncat`
  truncates it at 99 bytes.
- `encodeLoRaAPRS()` (`aprs_functions.cpp:1278`) has no caller (only a commented call at `loop_functions.cpp:5808`).
- Stale line citations in source comments ("aprs_functions.cpp:632" in `charset_filter.h:119`,
  `loop_functions.cpp:5466`, `aprs_functions.cpp:1313-1316`); the decoder is at :722.

## Operator decisions 2026-11-21

- D1 (a): the Track frame comment cap goes from 16 to 25 bytes (`APRS_ATXT_WIRE_MAX`).
- D2: no `#name` in the Track frame.
- D3 A: bytes stay the contract (25 on air, up to 39 stored), notice at set time, UTF-8-safe setter clip, docs.
- D4 B0: budget stays 100; a visible notice (serial marker and `--info`) when comment or name is dropped.
