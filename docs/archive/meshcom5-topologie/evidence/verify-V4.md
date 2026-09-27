# Verifier V4: MH-JSON length, BLE limits, MHeard bytes, horizon meta, 6.2 kB

Tree: /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main at 082c2412 (src identical to d9fc5c5a).
ArduinoJson 7.4.3 on every firmware env (7.4.2 on ttgo_tbeam_supreme_l76k only).
Scratch tools: `v4/mhlen.cpp` (MH frame lengths through the firmware's ArduinoJson), `v4/rx.cpp`
(firmware `src/Regexp.cpp`), `v4/hz.c` (xtensa + arm `_Static_assert`).

## Measured MH frame length (bytes incl. 0x44, ArduinoJson 7.4.3)

| Case                                                                           | 13 fields | 20 fields |
| ------------------------------------------------------------------------------ | --------- | --------- |
| Generator values (paper)                                                       | 162       | 225       |
| Realistic, live path (DIST is an unrounded double, 11 chars)                   | 167       | 230       |
| Realistic, list path (DIST float-rounded to %.1f, max 7 chars)                 | 162-163   | 225-226   |
| Proposed encodings at max (SNR/HM -128, NCNT 255, EX/NB 128, DIST 11, AGE 720) | -         | 232       |
| Same, plus AGE 65535 and DIST in exponent form (<1e-5 km)                      | -         | 237       |
| Today live, regex-valid 20-char CALL                                           | 178       | (241)     |
| Today type-max (CALL 20, RSSI -32768, SNR -128, PL 255, DIST exponent)         | 185       | (251)     |

The values in parentheses cannot occur in the proposed frame, because its CALL has at most 9 characters.

How DIST is serialised (ArduinoJson `VariantImpl.hpp:74-95`, `TextFormatter.hpp:67-69`,
`FloatParts.hpp`):

- The library stores a double as Float only if `value == (float)value`. Otherwise it stores a
  Double and prints it with 9 decimal places, reduced by one for each extra integer digit, then
  strips trailing zeros. That gives at most 11 characters for 1e-5 <= x < 1e7, and the exponent form
  outside that range.
- **Live path** (`lora_functions.cpp:1166`, `gps.distanceBetween()/1000.0`): the value is
  practically never float-exact, so it prints 11 characters, e.g. `1234.567891`. Sweeping 2M doubles
  between 0.01 and 20,000 km gives a maximum of 11.
- **List path** (`mheardRoundDist()` -> float record -> double): the value is float-exact, so it is
  stored as Float and printed with a 6-digit budget. Sweeping 0 to 20,100 km in 0.1 km steps gives
  exactly the `%.1f` text for every value (0 mismatches), at most 7 characters.

## Verdicts

**F1 (DIST/SNR/PL underestimate): PARTLY.**

- Confirmed: live-path DIST is an unrounded double and costs +5 B against the generator's `1234.5`.
  The correct realistic worst cases are **167 / 230 B**, not 162 / 225.
- Refuted, SNR: -128 is unreachable. RadioLib `SX126x::getSNR()` returns register/4, which is -32
  to +31.75 (`RadioLib/src/modules/SX126x/SX126x.cpp:712-716`), so at most 3 characters.
- Refuted, PL: in the proposed frame PL is 4 bits (max 15), which is the generator's value.
- Refuted, "~235-245 B, at or over the limit": the realistic figure is 230 B, 15 B under 245.
  Even with every encoding at its maximum plus AGE 65535 and an exponent-form DIST, the frame is
  237 B. The paper's conclusion that the frame fits holds; only its printed numbers are 5 B low.
- Fix: print 167 / 230, or round DIST to 0.1 km before it goes into the JSON (then 162 / 225 holds
  up to 9999.9 km).

**F2 (CALL up to 20): mostly REFUTED for the paper.**

- True for today's live frame. `checkRegexCall` (`[0-9]+` is unbounded) accepts
  `0A123456789012ABC-99`, verified with the firmware's `Regexp.cpp`.
- Today's list path truncates the call to 9 characters (`mheardCalls[..][10]`,
  `mheard_functions.cpp:389-393`).
- The proposed frame takes CALL from the 6-bit uint64 (3 to 9 characters), so it has no effect on
  the 225 B claim.

**F3 (limits per path): PARTLY.**

Confirmed:

- The com (list) path clamps to 245 by truncating bytes (`loop_functions.cpp:716-719`), which
  leaves invalid JSON.
- Both call sites use plain `bleJsonFrame()` (`mheard_functions.cpp:439, 926`).
- The app drops any frame that does not end in `}` and only writes a log line
  (`MessageHandler.ts:795-799`; it reads up to the first 0x00, so the padding bytes are harmless).
- The live path clamps to 255 (`loop_functions.cpp:674-676`; 0x44 is `'D'`).

Refuted: the claim that the live path has "10 B more headroom". Its effective limit is lower:

| Platform | Effective live limit | Cause                                                                                                                                                                                                                                                                                                                                    |
| -------- | -------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ESP32    | 252 B                | `sendToPhone()` does `uint8_t blelen; blelen = blelen + 2` (`phone_commands.cpp:69, 104`, and `esp32_write_ble` takes `uint8_t`). A 254 or 255 B frame therefore sends 0 or 1 byte. NimBLE truncates a notify to MTU-3 = 252 (preferred MTU 255 at `nimconfig.h:224`, truncation at `ble_att_cmd.c:74`), so a 253 B frame loses its `}`. |
| nRF52    | 247 B                | `configPrphConn(250)` at `nrf52_ble.cpp:97`. `BLECharacteristic::notify` splits at MTU-3 = 247, so from 248 B the first packet has no `}`.                                                                                                                                                                                               |

- Real limits: com 245, live 252 on ESP32 and 247 on nRF52. All of them assume the phone
  negotiates the node's maximum MTU.
- The "High" severity is overstated. 230 B fits every path.
- What remains valid: the paper should name `payload_max` = 244 JSON characters and require
  FailSoft at both call sites.
- Open and unverified: a phone that negotiates MTU 185 loses every frame over 182 B. That would hit
  the 20-field frame, but not today's frame of at most 167 B.

**F4 (FailSoft order): CONFIRMED as a note.**

- `ble_json_frame.h:46-58` always drops the last member that is not TYP.
- "nie die alten" also depends on the 13-field frame staying under 244. It does: the type-max is
  185 B.

**A1 (55 vs 54 B): REFUTED as an arithmetic error, PARTLY as a documentation gap.**

- nm on V2: the eight arrays sum to 1,620 B, or 54 B per entry over 30 entries.
- The 55th byte is `static uint8_t mheard_send_idx[MAX_MHEARD]` (`mheard_functions.cpp:802`). nm
  shows 0x1e on V2 and 0x50 on V3 and RAK4631, so 1 B per entry.
- The 6 B of Verwaltung are `mheard_send_cursor` (4) + `mheard_send_n` (1) + `mheardWrite` (1).
- The total of 55 is right, but the field list leaves out the send index.

**A2 citations:**

| Citation                           | Verdict   | Reason                                                                              |
| ---------------------------------- | --------- | ----------------------------------------------------------------------------------- |
| `lora_functions.cpp:1076`          | CONFIRMED | Line 1076 is a comment; the `mcSet` is at 1078.                                     |
| `nbr_matrix.cpp:1109`              | REFUTED   | 1109 is the `return` line that returns 0 for idx >= 32, which is exactly the claim. |
| checkMesh caller at 1777 not cited | CONFIRMED | 1777 is its only caller; the row cites only checkVia callers.                       |

**D1 (hzMeta 3 B): PARTLY.**

- The naive `struct {uint16_t; uint8_t}` is 4 B on both xtensa and arm (compiled).
- `uint8_t hzMeta[H][3]` or two separate arrays are 3 B (compiled).
- The paper never gives a type for `hzMeta` (unlike `nbrExt[X][12]`), so this is an
  underspecification. The cost is at most +40 or +96 B.

**D2 (6.2 kB): CONFIRMED.**

- The figure is a literal in `.body.html:317`; the generator does not compute it.
- Taking "mit den S3-Größen" at its word, `newsize()` gives +7,455 B for the tables alone and
  **+7,935 B including the rings (about 7.9 kB)**.
- 6.2 kB (+6,231) comes out only if R, W and E are scaled while X and H stay at their classic
  sizes.
- The option is rejected either way, so the conclusion stands.
