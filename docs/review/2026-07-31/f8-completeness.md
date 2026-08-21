# F8 — Completeness critic: what the architecture concept is missing entirely

Angle: not "is 01–07 wrong", but "what is not in the set at all, or is so thin nobody could act on
it". Baseline reviewed: `docs/architecture/README.md` + `01`–`07`, branch `v4.35p_prio`, commit
`1ba101f4`.

Method note: I ran a keyword sweep over all eight documents. These terms return **zero hits across
the entire set**: `FLASH_VERSION` migration, `deepsleep`, `license`/`GPL`, `CVE`, `duty cycle`,
`regulatory`, `country`, `issue template`. `safeboot` appears only as a toolchain fact,
`meshcom_settings` only as "flash-persisted config struct" in a mermaid node.

## Blind spots

### F8-1: The wire format is named as the highest-risk contract and then never specified

**What is missing.** Every document points at the wire format as the thing that must not break —
01 calls `aprs_functions.cpp` "the interop contract with the live network", 05 §1 says "there is no
specification to port against", 06 makes `decodeAPRS()` test target #1, 07 scenario 1–2 is
"decode every captured frame correctly". Nowhere in the set (or anywhere else in the repo) is the
frame layout written down. The concept correctly _diagnoses_ the absence and then reproduces it.

That is the single largest hole, because it is the one artefact that:

- gates test Layer 2 (you cannot write `test/vectors/rx_<n>.json` expectations without knowing what
  the fields mean),
- gates the RadioLib and Arduino-3.x upgrades (03's "verification needed" column is "on-air"
  precisely because there is no offline oracle),
- is the only thing a second implementation (the MeshCom server, the phone app,
  `extras/decode_meshcom.py`) has to agree with.

**Evidence that the spec is derivable today — i.e. this is a writing task, not a research task.**
From `src/aprs_functions.cpp` (`decodeAPRS` :122–:481, `encodeAPRS` :1047–:1105):

| Offset   | Field                   | Notes                                                                                                                                    |
| -------- | ----------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| `[0]`    | `payload_type`          | `0x21 '!'` position · `0x3A ':'` message · `0x40 '@'` HEY · `0x41 'A'` ACK (early-returns at :129) · `0x3C '<'` LoRa-APRS, rejected :132 |
| `[1..4]` | `msg_id`                | little-endian uint32                                                                                                                     |
| `[5]`    | flags + hop budget      | `&0x0F` = `max_hop`; `0x80` server, `0x40` track, `0x20` app_offline, `0x10` mesh                                                        |
| `[6..]`  | source path             | ASCII `0x20–0x7E`, comma-separated, terminated by `>`; hop count = comma count + 1 (:213)                                                |
| —        | destination path        | comma-separated, terminated by a repeat of `payload_type` (:294)                                                                         |
| —        | payload                 | terminated by `0x00`                                                                                                                     |
| +1       | `msg_source_hw`         | numeric board registry, `configuration_global.h:8–41` (38 entries, `TLORA_V2 1` … `ESP32_LORAPRS_RA01 60`)                               |
| +1       | `msg_source_mod`        | `(getMOD() & 0x0F) \| (node_country << 4)` — modulation in low nibble, **country in the high nibble** (`aprs_functions.cpp:113`)         |
| +2       | `FCS`                   | big-endian; plain **sum of all preceding bytes**, not a CRC (:392–:397, :1073–:1082)                                                     |
| +1       | `msg_source_fw_version` | `shortVERSION()` = digits 3–4 of `SOURCE_VERSION`                                                                                        |
| +1       | `msg_last_hw`           | `0x80 \| BOARD_HARDWARE` — high bit = "last heard"                                                                                       |
| +1       | fw sub-version          | `0x7E` or `0x00` → normalised to `'#'`                                                                                                   |
| +1       | `0x7E`                  | trailer                                                                                                                                  |

Frame ≤ `MAX_APRS_FRAME_SIZE 340`, minimum 16 bytes. Callsigns validated by
`checkRegexCall()`; destination may instead be a numeric **group** 1–99999 or `100001`
(`CheckGroup()` :28).

**The interop cliff nobody has written down:** `aprs_functions.cpp:472` silently discards any frame
whose `msg_source_fw_version` is `> 0 && < 35`. Firmware older than 4.35 is _cut off the network by
this build_. That is a fleet-wide compatibility decision encoded in one `if` and documented nowhere.

**Remedy — new document, and it is the highest-value one in the set.**
`08 — Wire Format Specification (on-air protocol v4.35)`. Questions it must answer:

1. Byte-exact frame layout per payload type, including the ACK (`0x41`) short form that
   `decodeAPRS` early-returns on before any parsing.
2. What is the position payload grammar? `decodeAPRSPOS()` (`aprs_functions.cpp:531`, 466 LOC,
   CC 75) parses `/B=`, `/A=`, `/P=`, `/H=`, `/T=`, `/Q=`, `/G` — this is a second, nested format
   with no written grammar at all.
3. FCS semantics: it is a byte sum, so it detects corruption but not reordering, and it is
   computed over a prefix that _excludes_ the trailing version bytes. Say so, and say what a
   receiver must do on mismatch (currently: drop, unless the sender is yourself — :400).
4. Which malformed frames are _tolerated_ vs rejected. 05 explicitly calls this out as unwritten
   behaviour that a rewrite would lose. Enumerate the `return 0x00` paths.
5. Version negotiation: `msg_source_hw` / `MOD` / `FW` / sub-version, the `< 35` cutoff, and the
   rule for adding a new hardware ID.
6. Path/hop mechanism: how `max_hop` is decremented, who appends to `msg_source_path`, and the
   relationship to `max_hop_text` (default 4) vs `max_hop_pos` (default 2).
7. Country in the MOD high nibble — 16 values (`getCountry()` `lora_setchip.cpp:60`), so this is
   also the regulatory field. See F8-9.
8. The reserved/unused bits and what a receiver must do with them (forward-compatibility rule).

Cross-reference, do not duplicate: `extras/decode_meshcom.py` (136 LOC) is an **independent Python
decoder** of the same format reading UDP port 1798. It is a free conformance oracle and the concept
never mentions it.

---

### F8-2: Settings persistence and flash-version migration — the fleet-upgrade risk nobody has written down

**What is missing.** The concept treats `meshcom_settings` as a leaf detail (one mermaid node in 01,
one `--save`/`--cleanflash` mention in 07 §1.2). But `FLASH_VERSION 20260712`
(`configuration_global.h:5`) exists precisely because the persisted layout has a version, which
implies it has changed, which implies there is — or is not — a migration path. The concept never
asks the question. I had it investigated, and the answer is worse than "undocumented".

**What is actually true (verified against the source, with sizes cross-compiled for both targets):**

- There is **no single `meshcom_settings`**. There are two structurally different structs with the
  same name: `src/esp32/esp32_flash.h:8` (**2008 B**) and `src/nrf52/WisBlock-API.h:174` (**1968 B**),
  plus a legacy `s_meshcomcompat_settings` (`WisBlock-API.h:402`, 1632 B). They are **not
  layout-compatible** — `node_ossid` sits at offset 66 on ESP32 and 112 on nRF52; ESP32 has
  `node_disp_rot`, nRF52 has `send_repeat_time`/`auto_join` and neither has the other's.
- ESP32 persists **field-by-field into NVS** (`Preferences`, namespace `"Credentials"`,
  ~150 hand-written keys) via **two independent hand-maintained lists** — `init_flash()`
  (`esp32_flash.cpp:14–264`) reads, `save_settings()` (`:280–533`) writes, with no shared schema.
  Keys are renamed and cross-wired (NVS `node_ssid` ⇒ struct `node_ossid`). Any divergence between
  the two lists is a silent data-loss bug with no test.
- nRF52 persists a **raw `memcpy` of the whole struct** to a LittleFS file (`nrf52_flash.cpp:257`,
  `:312`). Integrity protection is **two marker bytes** (`0xAA`/`0x55`) — no CRC, no length, no
  struct version. A mid-struct field insertion shifts every following field, is not detected, and is
  then **written back**. `node_via` was in fact added mid-struct historically
  (`docs/code-audit-20260517.md:360`). The struct also contains uninitialised padding (7 bytes before
  `node_lon`, 3 before `node_alt`, …) which is written to flash verbatim.
- **`FLASH_VERSION` neither migrates nor resets.** Both platforms do
  `init_flash(); if (node_fversion != FLASH_VERSION || bClear) { clear_flash(); } ...; save_settings();`
  (`esp32_main.cpp:725–746`, `nrf52_main.cpp:499–518`). On a pure version bump `init_flash()` is
  **not re-run**, so the pre-clear RAM copy is written straight back. The log line
  `[INIT]...FLASH cleared new version` claims the opposite of what happens. On nRF52 the garbage read
  has already occurred before the check, so `FLASH_VERSION` is decorative there.
- **`--cleanflash` is a no-op on nRF52.** `nrf52_flash.cpp:38` early-returns on `init_flash_done`,
  which was already set during the first call, so the `if (bClear) init_flash();` at
  `nrf52_main.cpp:511` returns immediately and the old values are restored. It works on ESP32.
- **ESP32 first boot silently blanks 18 string fields.** Those `getString()` calls supply no default
  (`esp32_flash.cpp:20, 23, 62, 84–116, 137, 146–156, 228, 235, 240, 255, 260`), so the in-class
  defaults are overwritten with `""` — including `node_call`, which becomes `""` on ESP32 and stays
  `"XX0XXX-00"` on nRF52.

**Why it matters.** This is the failure mode that turns a firmware upgrade into a fleet-wide support
incident, and unlike a wire-format bug it is invisible until users report "my node lost its
callsign". It is also the direct blocker on 05 Phase 5 (retiring the global bus) and on any
`meshcom_settings` restructuring: today, adding a field to the middle of the struct corrupts every
nRF52 node in the field, and nothing in the repository says so.

**Remedy — new document.** `09 — Configuration, Persistence and Fleet Upgrade`. Must answer:

1. Both struct layouts, their sizes, and why there are two. Which fields are persisted vs derived
   (the nRF52 "nicht im Flash" comment at `WisBlock-API.h:343–379` is **false** — that tail is
   persisted).
2. What `FLASH_VERSION` actually does, versus what it appears to do. Then: what it _should_ do.
3. **The rule for adding or moving a field** — currently the single most dangerous edit in the
   codebase, with an ESP32 path (add two keys, in two lists) and an nRF52 path (append only, never
   insert) that differ completely.
4. What survives `--cleanflash` on each platform, and the fact that it does not work on nRF52.
5. What else persists: NVS `meshcom_time`, the T-Deck-only SPIFFS `/messages.json` and SD `/pos.dat`,
   the `t5_epaper` private NVS namespace `"system"`, and the fact that `data/*.mp3` is never
   uploaded by any environment (no `board_build.filesystem`, no `uploadfs`) — i.e. dead payload.
6. Flash-wear: nRF52's `memcmp` dirty-check spans volatile fields (`node_date_*`, `node_temp`), so
   most of the **199** `save_settings()` call sites trigger a full erase+rewrite.
7. The tests that make this safe. 07 scenario 19 is "settings survive reboot" — a round-trip, which
   passes today. The two missing ones are **"settings written by version N are readable by N+1"**
   (a golden-image test) and **"every field written by `save_settings()` is read by `init_flash()`"**
   (a schema test that would have caught the whole class of ESP32 key-list drift). Both are cheap and
   belong in 06 as on-device tests.

---

### F8-3: Safeboot / OTA — how a node in the field recovers from a bad update

**What is missing.** 03 uses safeboot to make its best point (two ESP32 toolchains in one repo) and
02 lists two safeboot environments. Neither describes what safeboot _is for_. The repository ships
two prebuilt binaries at the root (`safeboot.bin` 688 KB, `safeboot-s3.bin` 645 KB), a partition CSV
pair, `otadata.bin`, `tools/safeboot.py`, `tools/ensure_safeboot.py`, `tools/dump_otadata.sh`, an
`src/safeboot/` app (~3,220 LOC, `ota.h` alone is 2,030 lines), and a web-UI button that reboots the
node into the OTA updater (`web_functions.cpp:1092`, `web_nodefunctioncalls.cpp:31` →
`--ota-update`).

**Why it matters for the stated goal.** "Move to modern tested firmware" means shipping new firmware
to nodes that are on masts and in other people's flats. The recovery path is the thing that decides
whether a bad release is an inconvenience or a truck roll. It is also the piece most likely to break
in the Arduino-3.x migration 03 recommends — 03 itself flags "bootloader format differs → safeboot
interplay" in one table cell and then never returns to it.

**What is actually true (verified).** The partition table has a `factory` (safeboot, 704 K at
`0x10000`) and **exactly one** `ota_0` (app at `0xC0000`) — so A/B rollback is impossible by
construction. `--ota-update` (`command_functions.cpp:604–628`) points otadata at `factory` and
reboots; safeboot brings up WiFi (or a soft-AP) and serves **plain HTTP ElegantOTA with
`clearAuth()`** (`src/safeboot/main.cpp:284`) — anyone on the LAN or the open AP can flash arbitrary
firmware during the window. Integrity is a **client-supplied MD5** computed in the browser
(`ota.html:342–355`). There is no rollback, no `esp_ota_mark_app_valid_cancel_rollback()` anywhere in
the tree, and no crash-loop or boot-failure detection. A valid-MD5 image that boots and panics is a
serial-recovery brick, because the app's `--ota-update` is the only door into safeboot.

Three further facts that belong in writing and are in none:

- **Safeboot cannot be field-updated.** `esp_ota_get_next_update_partition()` resolves to `ota_0`;
  nothing in the tree writes the factory partition. Every bug in the two committed blobs — including
  the `updateInProgress` flag that is set at `main.cpp:243` and never cleared on failure, which
  permanently disables the 180 s auto-return — is permanent for every deployed unit.
- **Two ESP32 boards silently have no safeboot at all.** `variants/t_deck_pro/platformio.ini:9,47–49`
  and `variants/t5_epaper/platformio.ini:9` have `upload_command`, `board_build.partitions` and
  `upload_protocol` commented out (the t5 line also has a typo: `partition`, not `partitions`). On
  those boards `--ota-update` hits the `else { return; }` dead branch and does nothing.
- **nRF52 has zero remote-update capability.** `--ota-update` is behind `#ifdef ESP32`. The three
  nRF52 boards are UF2/`nrfutil` only — a physical-access-only update path for a third of the
  supported MCU families. 03 lists nRF52 as "comparatively healthy"; on this axis it is not.

**Remedy — new document.** `10 — Boot, Partitions, Safeboot and OTA`. Must answer:

1. Flash map for 4 MB and 16 MB, both app slots, otadata, and why there is only one OTA slot.
2. How a node enters safeboot (one command, no automatic path) and how it leaves (success, or the
   180 s uptime timer).
3. What safeboot serves, over what transport, with what authentication — and the decision record for
   why `clearAuth()` is acceptable, or the issue to fix it. `node_webpwd` already exists in NVS and
   is unused.
4. **Recovery matrix**: interrupted upload / corrupt image / valid image that panics / bad safeboot.
   State plainly which of these require physical access. Today three of four do.
5. Per-board coverage table: which of the 30 boards actually have safeboot wired up. Two ESP32 boards
   and all three nRF52 boards do not, and nothing anywhere says so.
6. Provenance of the two committed blobs: how they are rebuilt, how anyone verifies the binary in git
   matches `src/safeboot/`, and why there is no version stamp the app can query.
   (`tools/ensure_safeboot.py` is dead code — not referenced by any environment, and it looks for a
   filename the build never produces.)
7. The two-toolchain constraint from 03, and what must hold for it to survive the Arduino-3.x
   migration.

---

### F8-4: Licence and provenance of vendored code — the repo currently ships GPL-3.0 code under an MIT LICENSE

**What is missing.** 03 inventories `lib/` by _version_ and never by _licence_. The word `license`
does not appear in the concept.

**Evidence.** Root `LICENSE` is MIT ("Copyright (c) 2024 icssw.org"). Vendored:

| Library                                                    | Licence                                                          | Statically linked into                                                                                                                       |
| ---------------------------------------------------------- | ---------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| `lib/GxEPD2` 1.5.5                                         | **GPL-3.0**                                                      | every e-paper board — referenced from `src/loop_functions.cpp`, `src/Displays/E0213A367/`, `t-deck-pro`, and ~10 `variants/*/platformio.ini` |
| `lib/ESP32-audioI2S` 2.1.0                                 | **GPL-3.0**                                                      | `src/esp32/esp32_audio.cpp`, T-Deck / T-Connect-Pro variants                                                                                 |
| `lib/epdiy` 2.0.0                                          | LGPL-3.0                                                         | T5 e-paper                                                                                                                                   |
| `lib/TinyGSM` 0.11.7                                       | LGPL-3.0                                                         | T-Connect-Pro modem                                                                                                                          |
| `lib/lvgl`, `XPowersLib`, `SensorLibTDECkpro`, `AceButton` | MIT                                                              | —                                                                                                                                            |
| `lib/Adafruit TCA8418`                                     | BSD                                                              | —                                                                                                                                            |
| `lib/TFT_eSPI`                                             | mixed, MIT-derived + per-file terms                              | T-Deck                                                                                                                                       |
| `lib/es7210`                                               | **none** — no LICENSE file, no `license` field in `library.json` | T-Deck audio codec                                                                                                                           |
| `lib/Timeout`                                              | **none** — two files, no metadata at all                         | —                                                                                                                                            |

Statically linking GPL-3.0 into a distributed firmware binary makes the combined work GPL-3.0,
which the root MIT statement contradicts. GPL-3.0 §6 "Installation Information" also interacts
directly with F8-3 (a bootloader that restricts what the user can install). Two libraries have no
identifiable licence at all.

**Vendored-library divergence, with a concrete instance.** Commit `99817243` — author
`orbisai0security`, "Automated security fix generated by OrbisAI Security" — edited
`lib/TinyGSM/src/TinyGsmClientSequansMonarch.h` (3 insertions, 4 deletions) and left
`test/test_invariant_TinyGsmClientSequansMonarch.h` behind as the only inhabitant of `test/` besides
`compress_functions`. So: a third-party bot has modified a vendored library in place, the change is
invisible to `lib_deps`, and any future TinyGSM refresh silently reverts it. 06 mentions that file
only as "a source file parked in the test directory".

**Remedy — a section in 03, plus one new short document.** Add `03 §Licensing` with the table above,
and create `11 — Vendored Code: Provenance, Divergence and Licence Compliance`. Must answer:

1. Per-library: upstream URL, exact upstream tag, licence, which boards link it.
2. For each: **is the vendored copy byte-identical to upstream?** If not, what is the local delta and
   why. (Establishing this once is the whole value; after that a CI job can hold it.)
3. What licence the distributed binaries are actually under, per board, and what the release
   artefacts must carry (licence texts, written offer, corresponding source).
4. Policy: when may a library be vendored rather than pinned via `lib_deps`? Who may patch a
   vendored copy, and where is the patch recorded?

This is not padding. It is the only finding in my sweep that can stop distribution outright, and it
costs one afternoon to resolve.

---

### F8-5: There is no threat model — and an open, verified security backlog already exists in the repo root, unreferenced

**This is the finding I would fix first, because unlike the others it is not "write a document" — it
is "the document exists and the concept did not read it".**

`docs/code-audit-20260712.md` (40 KB) is dated **2026-07-12**, reviews branch **`v4.35p_prio`**
— the same branch the concept baselines on, eighteen days earlier — and contains a severity-ranked,
individually-verified backlog of 39 findings with stable IDs, file:line evidence, failure scenarios
and proposed actions. Its security half:

| ID         | Severity     | Verdict   | Reachable from                                                                                                              |
| ---------- | ------------ | --------- | --------------------------------------------------------------------------------------------------------------------------- |
| SEC-01     | **Critical** | CONFIRMED | a crafted LoRa frame (`{MCP}` remote-command password check is bypassable; ends in `commandAction()` toggling GPIO outputs) |
| SEC-02     | High         | CONFIRMED | received message text (`printfdeb()` re-parses already-substituted payload as a printf format string)                       |
| SEC-03     | High         | CONFIRMED | BLE `0x55` — attacker-controlled `ssid_len`/`pwd_len` drive a `memcpy` past a 300-byte buffer                               |
| SEC-04     | High         | CONFIRMED | a received message — URL-decode loop writes past `msg_text_checked[200]`; the length guard runs after the loop              |
| SEC-05/06  | Med-High     | CONFIRMED | a 255-byte UDP datagram — off-by-one OOB write, both the normal and external UDP paths                                      |
| CONC-14…19 | High…Med     | CONFIRMED | six cross-task/ISR concurrency defects, incl. nRF52 running the phone-command handler inline in the BLE callback            |

**I spot-checked the two most severe against the current tree. Both are still open:**

- SEC-02 — `src/printfdeb_functions.cpp:118` is still `Serial.printf(temp);`. The one-line fix
  (`Serial.printf("%s", temp);`) has not been applied.
- SEC-01 — `src/loop_functions.cpp:2055–2101` still has `bool bpass=true;` with the clearing branch
  guarded by `cpasswd[ip] != 0x00`, and still reaches `commandAction(cBefehl, false)`.

**Why this is a completeness failure and not just an oversight.** The concept independently
_re-derived_ the structural half of this document — 06's "there are no automated tests" is TEST-36/37,
02's B-07 "CI builds on tags only" is TEST-38, 04's `commandAction()` analysis is SIMP-26, 04's
`USE_NEW_BATT` section is DRY-20, 04's `nrf_eth.cpp`↔`udp_functions.cpp` clone is DRY-21 — while
missing its security and concurrency halves entirely. That is the exact signature of a document set
built without an inventory of what already exists (see F8-15). It also means the concept's
prioritisation is wrong: 01 §1 notes that only 12 of 423 globals are atomic and calls concurrency
"manual"; CONC-14…19 name the six specific places where that has already gone wrong.

**The remaining surface, none of it described in the set:**

- `OnRxDone()` — 968 LOC, nesting 13, parses **attacker-controlled bytes from an open RF channel**,
  with `String` concatenation, fixed `char[UDP_TX_BUF_SIZE]` scratch buffers, and hand-rolled index
  arithmetic. 04 correctly names it "the highest-consequence code in the firmware" — on the
  _reliability_ axis. Nobody has framed it as the parser of untrusted input that it is.
- Web UI: authentication is a password passed as a **GET query string** (`/?nodepassword=`,
  `web_functions.cpp:364–376`) over plain HTTP, with a 4-hour IP-keyed session table of 10 slots
  (`web_ip_passwd_time[10]`, :40, :240).
- Net console: TCP/2323, HMAC-SHA256 challenge-response, but **open access when `node_passwd` is
  empty** (`src/net_console.h` header comment), password max 14 chars.

**The surface, none of it described in the set:**

- `OnRxDone()` — 968 LOC, nesting 13, parses **attacker-controlled bytes from an open RF channel**,
  with `String` concatenation, fixed `char[UDP_TX_BUF_SIZE]` scratch buffers, and hand-rolled index
  arithmetic. 04 correctly names it "the highest-consequence code in the firmware" — on the
  _reliability_ axis. Nobody has framed it as the parser of untrusted input that it is.
- Web UI: authentication is a password passed as a **GET query string** (`/?nodepassword=`,
  `web_functions.cpp:364–376`) over plain HTTP, with a 4-hour IP-keyed session table of 10 slots
  (`web_ip_passwd_time[10]`, :40, :240).
- Net console: TCP/2323, HMAC-SHA256 challenge-response, but **open access when `node_passwd` is
  empty** (`src/net_console.h` header comment), password max 14 chars.
- BLE: pairing PIN is the compile-time constant `PAIRING_PIN "000000"`; the phone can push the WiFi
  SSID _and password_ over BLE (`phone_commands.cpp` case `0x55`).
- **OTA: unauthenticated plain-HTTP arbitrary firmware flash** during the safeboot window
  (`ElegantOTA.clearAuth()`, `src/safeboot/main.cpp:284`), with a client-supplied MD5 as the only
  integrity check and no image signature. This is the highest-severity item on the list and it is in
  neither the concept nor any other document. See F8-3.
- The MeshCom server backhaul: F8-10.

**Remedy — two things, in this order.**

1. **Triage `docs/code-audit-20260712.md` now, before writing anything.** Mark each of the 39 findings
   open/fixed/won't-fix against the current tree, and land SEC-02 (one line) immediately. SEC-01
   should be handled as a coordinated disclosure with the upstream maintainers — the document itself
   says so, and it affects deployed nodes, not just this branch. Then link the triaged document from
   06 as the real regression backlog (06 currently nominates `docs/code-audit-*.md` for that role,
   which is the weaker artefact).
2. **New document `12 — Threat Model and Untrusted Input`.** Must answer: what is trusted (nothing on
   RF; the local LAN?), what each channel authenticates, what a hostile frame can reach, which
   parsers are the trust boundary, what the disclosure process is for a firmware with a deployed
   fleet, and what the negative-test obligation is. It should feed directly into 06 — the
   highest-value native test after the golden vectors is a **fuzzer over `decodeAPRS()`**, which the
   concept does not propose at all (07 scenario 3 is a single hand-written malformed-frame case).
   SEC-02 and SEC-04 are both findings a five-minute fuzz run would have produced.

---

### F8-6: The BLE phone contract is a binary protocol with an external client and zero documentation

**What is missing.** 01 shows `phone app → readPhoneCommand()` as one arrow. 03 spends a page on
NimBLE _versions_. Nobody describes the protocol the phone app speaks — and the phone app is a
separate codebase, maintained by other people, that this firmware cannot break unilaterally.

**Evidence, derived in ten minutes from `src/phone_commands.cpp`:** Nordic UART Service
(`6E400001-…`, `esp32_main.cpp:1595–1597`), MTU 247 → `MAX_MSG_LEN_PHONE 300` with
`BLEtoPhoneBuff[MAX_RING][305]`. Node→phone types: `0x91` mheard, `0x44` JSON data, `0x40` text.
Phone→node opcodes (`readPhoneCommand()` switch, :267–:600): `0x10` handshake, `0x20` time,
`0x50` callsign, `0x55` WiFi SSID+password, `0x70` latitude, `0x80` longitude, `0x90` altitude,
`0x95` APRS symbol, `0xA0` text message, `0xF0` save settings; `0x70/0x80/0x90` additionally carry a
save flag at `conf_data[6]` (`0x0A` save / `0x0B` don't).

That is a wire protocol. It is exactly as much of an interop contract as the on-air format, and it
has an _external_ consumer, which the on-air format at least shares with implementations in this
repo.

**Remedy — a section of the new `08`, or a sibling `08b`.** Title: `Phone/BLE Protocol`. Must
answer: service/characteristic UUIDs, framing, the full opcode table with payload layouts, the
handshake/`isPhoneReady` state machine, MTU and fragmentation rules for >255-byte messages (the
NimBLE 2.4.0 truncation bug in 03 is exactly this), and the versioning rule — how does the firmware
tell the app what it supports?

---

### F8-7: Routing and mesh behaviour — the two best documents in the repo are orphaned

**What is missing.** The concept's link graph reaches only five external documents:
`docs/code-audit-*.md`, `docs/report-ble-tx-latency.md`, `docs/ram-comparison-*.md`,
`docs/codequality-rules.md`, `docs/loradebug-serial-output.md`. It never links
`docs/adr-nc-importance-backoff.md` (38 KB), `docs/prio-talk-flood-networking.md` (26 KB) or
`docs/hey-supp.md` — the three documents that describe _why the mesh behaves as it does_.

**There is in fact an ADR series, and nobody would find it.** Its numbering and statuses are exactly
the missing answer to "implemented or proposed":

- **ADR-001 — "Nachrichtenpriorität, Slot-Negotiation und Logging-Verbesserungen"**, status
  _"Phase 1 implementiert (v4.35n_prio_v20260315)"_. It lives in a file called
  **`docs/README_LORA_TRX.md`** — the filename gives no hint that it is an ADR, which is presumably
  why the concept never opened it. It is the source of the priority matrix that 07 §2.1 re-derives,
  and its §4.4 _"Koexistenz mit alter Firmware"_ and §5.3 _"Slot-Byte und alte Firmware"_ are the
  only written analysis anywhere of **wire-format backward compatibility** — i.e. the F8-1 material,
  already half-written and unlinked.
- **ADR 02 — "Netzwichtigkeits-basierter Relay-Backoff (NC-Importance)"**
  (`docs/adr-nc-importance-backoff.md`), status **Draft**. So it is a proposal, not the implemented
  design — which is precisely what a reader needs to know and cannot currently learn.
- `docs/prio-talk-flood-networking.md` is a supporting _analysis_ (flood-network behaviour,
  star-node priority, topology scenarios), not a decision.
- `docs/adr-totp-remote-led.md` — an ADR for a feature with zero presence in `src/` (see F8-15).

So the project has an ADR practice with inconsistent naming, no index, and no status convention, and
the architecture set neither uses it nor mentions it.

Meanwhile 07 §2.1 reproduces the priority/backoff table (CRITICAL 3000 ms … BACKGROUND 5500 ms) as
if it were a fresh finding, and the trickle mechanism — `TRICKLE_IMIN_S 30`, `TRICKLE_IMAX_S 900`,
`TRICKLE_K 2`, "RFC 6206 adaptiert" (`configuration_global.h:181–184`,
`esp32_main.cpp:3084`) — appears nowhere in the set at all, despite being the thing that governs how
much HEY traffic the network carries.

**Why it matters.** A contributor reading 01–07 learns that routing exists and that it is risky.
They do not learn the rules: when is a packet relayed (`checkMesh()` / `checkVia()`,
`src/via_functions.cpp` — only 136 LOC and it carries a substantial doc-comment at :11–:47 that
includes an unresolved `// TODO` describing the _intended_ NCT-based VIA rule), what a gateway does
differently, what the hop budgets are, and how trickle suppresses beacons.

**Remedy — new document that mostly _links_, plus a rename.** `13 — Routing, Relaying and Channel
Access`. A short spine, not a rewrite: hop budgets (`MAX_HOP_TEXT_DEFAULT 4`, `MAX_HOP_POS_DEFAULT
2`), the relay decision (`via_functions.cpp`), loop prevention, the priority class → CSMA parameter
mapping, trickle-HEY, gateway/`bGATEWAY` behaviour — each a one-line statement of the rule plus a
link into the ADR that argues for it. Must also answer: **is the `// TODO` at
`via_functions.cpp:29–35` (the NCT-based VIA rule) implemented, and is it the same thing ADR 02
proposes?** A new contributor cannot currently tell.

And rename `docs/README_LORA_TRX.md` → `docs/adr-001-message-priority-slots.md`, add a
`docs/adr-index.md` with ID/title/status, and adopt one status vocabulary. That is fifteen minutes'
work and it converts four orphaned files into a navigable decision record.

---

### F8-8: The RAM/flash budget constrains every other decision and is documented in stale, unlinked reports

**What is missing.** 03 makes DRAM the central argument against the Arduino-3.x migration; 01
explains `String`-in-packet-path via DRAM; 04 explains the per-board ring sizing via DRAM. The
budget itself has no document, and the two that exist are stale and unlinked.

**Concrete staleness, verified:**

- `docs/ram-opti.md` (2026-05-14) states the ESP32-S3 constants as
  `MAX_MHEARD 120 / MAX_MHPATH 150 / MAX_RING 30 / MAX_LOG 20 / MAX_RING_UDP 30` "aus
  `configuration_global.h:82-87`". Current values at that branch are
  `MAX_MHEARD 80 / MAX_MHPATH 100 / MAX_RING 20 / MAX_DEDUP_RING 100 / MAX_LOG 10 /
MAX_RING_UDP 20` (`configuration_global.h:94–100`). The recommendations were applied; the document
  still presents the pre-change numbers as current.
- **Two of the five sizing branches in `configuration_global.h` are dead.** `ENABLE_TBEAM` and
  `ENABLE_SBUFFER` are defined nowhere in `variants/`, `src/` or `platformio.ini` (only
  `ENABLE_XML`, in `variants/E22_XML-DevKitC/configuration.h:9`, is live). 03 quotes the dead
  `ENABLE_TBEAM` block — `MAX_MHEARD 10 // was 20, limited by DRAM` — as the live "tight boards"
  configuration. The real ESP32-classic branch is `MAX_MHEARD 30`.
- 03 step 0b says "RAM baseline across all 32 envs (`tools/ram_snapshot.py`)". The tool has a
  hardcoded 7-target list (`tools/ram_snapshot.py:24–30`). The prerequisite the whole dependency
  sequence rests on cannot currently be executed as written.

**Remedy — new document.** `14 — Memory Budget and Per-Board Sizing`. Must answer: what the DRAM
ceiling is per MCU class; the static-consumer ranking (the `nm`-derived table in `ram-opti.md` is
good work — carry it forward, corrected); which `#define`s are the tuning knobs and what each costs
per unit; which branch applies to which board; what the headroom is per board today; and what a
contributor must measure before merging anything that adds a buffer. Fold `ram-opti.md` and the two
`ram-comparison-*.md` into it as dated appendices, and fix `ram_snapshot.py` to cover all
environments so 03 step 0b becomes real.

---

### F8-9: Regulatory / regional operation is invisible in the concept

**What is missing.** `country`, `duty`, `regulatory` — zero hits across 01–07. The firmware carries
16 country profiles (`getCountry()`, `lora_setchip.cpp:60–79`), transmits the country in the wire
format's MOD high nibble, defaults to 433.9250 MHz or 869.525 MHz by country
(`lora_setchip.cpp:242, :262`), validates TX frequency against two bands
(`430.0+BW/200 … 439.0−BW/200` and `869.4+… … 869.65−…`, `command_functions.cpp:4000`), gates PA
behaviour on `node_country == 10` (`lora_setchip.cpp:558, :610`), and carries a bare comment
"duty cycle MUST NOT exceed 1%" (`lora_setchip.cpp:681`, `esp32_main.cpp:1353`) with no enforcement
anywhere.

**Why it matters for maintainability specifically.** Changing CSMA timing, the trickle interval, or
beacon defaults changes airtime, and 869 MHz SRD operation is duty-cycle limited by regulation while
433 MHz amateur operation is not. A contributor optimising throughput has no way to know which of
the two regimes a board is in, or that a limit exists. 07 §4.2 correctly warns about frequency
choice on the bench — that is the only regulatory sentence in the set, and it is about test hygiene,
not about the product.

**Remedy — a section, not a document.** Add `§Regional operation` to the new `13`, or to `08` since
the country lives in the wire format. Must answer: the 16 country profiles and what each sets; which
band each board defaults to; the amateur (433) vs SRD (869) split and which duty-cycle regime
applies; where the 1% limit is (not) enforced and what the project's position is; and who is
responsible for a new country entry.

---

### F8-10: The gateway role and the server backhaul protocol

**What is missing.** 01's layer map has one row: "Transport (backhaul) — WiFi/Ethernet uplink to the
MeshCom server." That is the entire treatment of a second network protocol with a second external
counterparty.

**Evidence.** UDP port 1990 to the server, 1799/1798 external (`configuration_global.h:63–67`).
Frame markers `GATE` (`0x47415445`) and `CONF` (`0x434F4E46`) (`udp_functions.cpp:135–144`), a
`KEEP` heartbeat with a fixed layout `KEEP%08X%-9.9s%-4.4s%-1.1s%s` carrying gateway ID, callsign,
version, sub-version and group IDs (`udp_functions.cpp:1057–1058`), a 30 s heartbeat interval and a
65 s server-timeout (`HEARTBEAT_INTERVAL`, `MAX_HB_RX_TIME`). And this whole protocol is
**implemented twice** — `src/udp_functions.cpp` (1,110 LOC) and `src/nrf52/nrf_eth.cpp` (1,019 LOC),
19 cloned windows, which 04 lists as a duplication finding without noting that the duplicated thing
is an external protocol.

**Remedy — a section of `08`** (it shares the frame encoding) titled `Server backhaul (GATE / CONF /
KEEP)`. Must answer: the port map, the frame types, the heartbeat contract and its timeouts, what a
gateway node does differently on the LoRa side (`bGATEWAY`, `bGATEWAY_NOPOS`), and what happens when
the server is unreachable.

---

### F8-11: The social structure — the concept plans work for a team that does not exist in that shape

**What is missing.** 05 §4 says "935 commits in the last 12 months, 24 contributors" and uses it to
argue a rewrite is unmergeable. Correct conclusion, wrong picture of the project.

Actual distribution over the last 12 months: **Kurt 721, dk5en 75, Rainer Fritz 31, Christian Raith
28, Ralf Altenbrand 16, karamo 16**, then a tail of eleven contributors with ≤11 commits each. One
person is 79% of the commits. There is no `CONTRIBUTING.md`, no `CODEOWNERS`, and `.github/`
contains exactly two files: one bug-report template and one CI workflow.

**Why it matters.** Every remediation in 04 and every phase in 05 is sized in "sessions" and assumes
someone will review and merge it. Under the project's own rule (`CLAUDE.md`: sync upstream first,
cherry-pick the minimum, PR against DEV in German), the entire plan is bottlenecked on one
maintainer's review capacity. A plan that proposes ~3,200 lines of scheduler unification without
naming who reviews it is not actionable. Equally, "who owns board X" determines whether the
hardware-dependent items (04 item 6, the 17-board battery migration; 03 steps 7–9) can happen at
all — nobody has 30 boards, so those steps need _named owners per board_, which no artefact records.

**Remedy — new document.** `15 — Contributing, Ownership and Change Flow`. Must answer: the upstream
PR workflow (currently only in `CLAUDE.md`, i.e. invisible to humans browsing the repo); the German
PR-description convention and why; how a change is proposed vs merged; who reviews what; a board
ownership table (board → who has the hardware → who verifies); how a hardware-dependent change gets
verified when the author lacks the board; and the issue triage path. The bug-report template already
asks for `--info` output — that is the seed of a good triage protocol and should be developed, not
left as a form field.

---

### F8-12: There is no runbook for the single most common contribution — adding a board

**What is missing.** 02 explains the _mechanism_ (`extra_configs`, `-D BOARD_X`, `-I variants/…`)
excellently. It does not tell you what to do. Adding a board is the most frequent structural change
this project makes — the duplication pattern 04 identifies ("new board arrived, copy the closest
existing board's files and edit") _is_ the current procedure, and it is the direct cause of the
`peri_gps.cpp` / `scr_mrg` / `power_controls.cpp` clone families.

**The actual surface, measured on the most recent addition (`BOARD_HELTEC_V4`):** a new
`variants/<board>/{configuration.h, platformio.ini}`, possibly a `boards/<board>.json`
(11 checked in), a numeric hardware ID in `configuration_global.h` (**which goes on air** — F8-1),
plus edits to `src/batt_functions.cpp`, `src/batt_function_old.cpp` (which one? — 04's unfinished
migration), `src/lora_functions.cpp`, `src/loop_functions.cpp`, `src/onebutton_functions.cpp`,
`src/esp32/esp32_main.cpp`, `src/esp32/pa_control.{cpp,h}`. Nine source files, none of them obvious.

**Remedy — new document.** `16 — Adding a Board: Runbook`. Must answer: the ordered checklist; how to
choose the closest existing board and what _not_ to copy; the hardware-ID allocation rule and its
on-air consequence; `USE_NEW_BATT` — which path a new board must use (answer: the new one, always,
per 04); display stack selection across the three overlapping mechanisms (`build_src_filter` /
`lib_ignore` / `#ifdef`) that 02 identifies; the RAM branch it will land in (F8-8); the minimum
verification before the PR; and who signs off. This document is also the _specification_ for 05
Phase 4 — it is the list of things a board descriptor would have to carry.

---

### F8-13: Power management and sleep — one word in the whole set

**What is missing.** `deepsleep` appears nowhere; `sleep` appears once, in 06 Layer 4's scope list.
`--deepsleep` is a real command (`command_functions.cpp:815`, `esp_deep_sleep_start()` at :883),
reachable from the user button on six board families (`onebutton_functions.cpp:243–271`) and
**entered automatically on low battery** (`batt_functions.cpp:246–282`). `src/esp32/esp32_pmu.cpp`
is 13.5 KB of XPowersLib PMU handling and 03 flags an XPowersLib upgrade as needing hardware.

**Assessment.** This does not need its own document. It needs to stop being invisible: a
battery-triggered deep sleep is an _automatic, irreversible-looking_ state transition that a user
experiences as "my node died", and it interacts with the half-finished `USE_NEW_BATT` migration that
04 documents. Two boards' worth of battery-curve divergence plus an auto-sleep threshold is a
support-load generator.

**Remedy — a section in 01 (a "power states" subsection of the layer map) and a row in 07's scenario
catalogue.** Must answer: what states exist (run / display-off / deep sleep / auto-reboot), what
triggers each, what wakes the node, what is lost, and which boards support which. And the missing
test: "node enters deep sleep at the configured threshold and not before" — currently unverifiable
because the threshold logic lives in whichever of the two battery files the board uses.

---

### F8-14: Release, versioning and the compatibility policy

**What is missing.** The set has no notion of a release. `SOURCE_VERSION 4.35` /
`SOURCE_VERSION_SUB "p"` / `FLASH_VERSION 20260712` are quoted in README.md as a baseline label,
never as _things that mean something_. But: the sub-version goes on air (F8-1), the FW version gates
interop at `< 35` (F8-1), the flash version gates persistence (F8-2), and CI triggers on tags only
(02 B-07) — so a tag is simultaneously the release mechanism and the only build gate.
`release.md` (11 KB) and `release_lora_trx.md` (36 KB) exist at the root and are not referenced.

**Remedy — a section in 02** (`§Versioning and release`) rather than a document. Must answer: what
each of the three version constants controls; when each must be bumped and by whom; the
compatibility policy (what N−1 firmware must still interoperate with, given the `< 35` cutoff);
and how tags, CI and the published artefacts relate. Once 02 B-07 is fixed and CI builds on PR, the
tag stops being a build gate and becomes a release gate — that distinction should be written down at
the same time.

---

### F8-15: Orphaned artefacts the concept neither uses nor dismisses

Everything below exists, is relevant, and appears in none of the eight documents. Each needs one
line and a link — collectively they are the difference between "documentation set" and "index of the
project".

| Artefact                                                                                                       | What it is                                                                                                                                                             | Action                                                                                     |
| -------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------ |
| `src/web_functions/Web-API_documentation.txt`                                                                  | 134 lines of REST API docs (`/setparam/?…`, ~40 parameters). Self-described as "not yet finished or stable". The only written external-interface contract in the repo. | Link from the new `08`; convert to Markdown; state its stability.                          |
| `extras/decode_meshcom.py`                                                                                     | Independent 136-LOC Python decoder of the on-air format over UDP 1798                                                                                                  | Conformance oracle for `08` and for 06 Layer 2                                             |
| `extras/SpectrumScan.py`                                                                                       | Host-side companion to `--spectrum` / `src/spectral_scan.cpp`                                                                                                          | One line in 07 §1.6                                                                        |
| *(`docs/code-audit-20260712.md`, `v435p_updates/`, `src/code_review/` and `adr-totp-remote-led.md` are covered |
| in the table in the next section — see F8-5 and F8-16 for the two that matter most.)*                          |
| `data/*.mp3` (12 files)                                                                                        | Audio assets for T-Deck — but **no environment uploads them**: no `board_build.filesystem`, no `uploadfs` step, and the 4 MB table has no SPIFFS partition at all      | Dead payload. Either wire up the filesystem upload or delete; say which in the new `10`    |
| `tools/ensure_safeboot.py`                                                                                     | **Dead code** — referenced by no environment, and looks for `safeboot.bin` in the S3 build dir where `custom_filename` produces `safeboot-s3`                          | Delete or wire up; note in the new `10`                                                    |
| `config/lv_conf.h`                                                                                             | A third LVGL config alongside `src/t-deck/lv_conf.h` and `variants/*/lv_conf.h`                                                                                        | Name which one is authoritative in 02                                                      |
| `tools/meshcom_monitor/` (17 logs)                                                                             | 06 uses it well; 07 uses it well                                                                                                                                       | **Already covered** — no action                                                            |
| `tools/loganalyse.sh` (85 KB!)                                                                                 | Referenced in 07 §1.5 as one table row. It is the largest tool in the repo by a factor of two.                                                                         | Deserves a sentence on what it can assert                                                  |
| `boards/*.json` (11 board definitions)                                                                         | PlatformIO board definitions checked into the repo; CI downloads a 12th at build time from a third-party GitHub raw URL (`.github/workflows/meshcom-ci.yml`)           | Name in the new `16`; the CI download is an unpinned external dependency and belongs in 03 |
| `test/compress_functions.cpp`                                                                                  | 06 and 05 both list it as a Phase-1 extraction target. Its only call site (`command_functions.cpp:244–255`) is commented out with `/* TEST`. **It is dead code.**      | Correct 05/06: it is not a test target, it is a deletion candidate                         |

**Dismissed in one line, deliberately:** `include/` (contains only a stock `README`); `.vscode/`;
`build_output.txt` (stray 175-byte artefact); `otadata.bin` (covered by F8-3); the `Fonts/`,
`GFX_Root/`, `t5-epaper/firasans_*.h` asset blobs (README.md already scopes them out correctly).

---

### F8-16: There is no defect index — four unmapped ID namespaces, and 06 nominates the weakest one as "the backlog"

**What is missing.** 06 says: _"Grow the corpus deliberately: add a vector for every bug ever fixed
in the decoder (`docs/code-audit-*.md` is the backlog)"_. That instruction cannot be executed,
because no artefact in the repository records which findings are open.

**The actual state.** Four disjoint ID namespaces exist, with nothing mapping between them:

| Namespace                                           | Source                              | Count / form                 |
| --------------------------------------------------- | ----------------------------------- | ---------------------------- |
| `MEM-` `BND-` `RACE-` `STAB-` `COMP-` `STK-`        | `docs/codequality-rules.md`         | rule IDs, cited as normative |
| bare ordinals ("Nr 1…20")                           | nine `docs/code-audit-*.md`         | per-audit, not stable        |
| `A1`–`D5`                                           | `docs/code-audit-fixes-20260627.md` | work-items                   |
| `SEC-` `BUG-` `CONC-` `DRY-` `SIMP-` `ALT-` `TEST-` | `docs/code-audit-20260712.md`       | 39, stable, with verdicts    |

The same live defect therefore appears under three different names — non-atomic ring indices is
`RACE-04` _and_ `C1` _and_ `CONC-15/16`. Nobody can answer "how many open issues does this codebase
have" without reading nine documents and diffing them against the tree by hand.

And the audits' own verdicts have been overtaken without being updated: the 2026-05-08 audit's
headline is _"Fixed since 2026-04-17: **None fully closed**"_, whereas roughly half of its twenty
CRITICALs are now closed (task watchdog, the 8× `while(true)`, `sprintf`, millis wraparound,
`charBuffer_aprs` String-by-value, atomic ring indices via commit `8009aa19`). Live CRITICALs that
remain include three open `WiFi.softAP()` sites, FCS-checked-after-parse, an ISR `printf` at
`io_extend.c:26`, and `spectral_scan.cpp:108`.

**Why it matters for the stated goal.** 06's whole regression-discipline section ("every bug fix gets
a test that fails before and passes after") depends on knowing what the bugs are. Layer 2's corpus is
supposed to grow one vector per historical decoder bug. Neither is possible against nine
point-in-time reports with incompatible IDs and stale verdicts.

**Remedy — one generated artefact, not another report.** `docs/defect-index.md`: one row per finding,
columns _stable ID · aliases in the other namespaces · severity · current status (open/fixed/won't-fix)
· fixing commit · regression test_. Seed it from `docs/code-audit-20260712.md`, which is the only namespace that
is already stable and verdict-bearing, and back-fill the audits into it. Then 06 can legitimately say
"the backlog is `defect-index.md`", and the "test per fixed bug" rule becomes checkable — the empty
`regression test` column _is_ the work queue for Layer 2.

**Cross-cutting caveat that belongs in the concept's README.** Line-number rot is universal across
this corpus: nearly every `file.cpp:NNN` citation in the older documents has drifted by 20–700 lines
(`csma_compute_timeout` is now at `lora_functions.cpp:2064`, the retransmit loop at `:1863`). Any
architecture document that links these must state **"line references are indicative; IDs and function
names are the stable handles."** The concept's own 01–07 will rot the same way and should adopt the
same convention now.

---

### F8-17: The 218-command surface is the operator contract, and its reference lives on a website

**What is missing.** `commandAction()` is the single funnel for serial, BLE and web configuration
(01's layer map, 04's worst-offender analysis, 07 §1.2's "test API"). It is therefore the primary
human interface to the product. There is no command reference in the repository. The bug-report
template points contributors at **`https://icssw.org/en/meshcom-kommandos-cl-gw/`** — an external
website, outside version control, with no mechanism keeping it in sync with the 218 arms.

**Why this is a completeness problem and not a nitpick.** 07 §1.2 is the concept's best practical
section, and it builds the entire bench harness on this surface. But it groups the commands by
purpose inferred from their names rather than from the source, and two entries in its
**destructive-commands warning** are wrong:

- `--format` is listed as destructive alongside `--cleanflash`. It is not a flash operation at all —
  `command_functions.cpp:4404–4415` sets `meshcom_settings.node_format`, the APRS telemetry format
  string, max 50 chars.
- `--spiffs reset` is listed as available. It is behind `#ifdef HEAP_TEST` **and**
  `#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)` (`command_functions.cpp:936–950`) — i.e.
  unreachable in every shipping build.

A harness author following 07 would avoid a harmless command and depend on one that does not exist.
This is exactly the failure mode 04 predicts for a 4,916-line if-chain with no reference: nobody,
including the documentation, can enumerate it accurately by reading.

**Remedy — a generated artefact, not a prose document.** `17 — Command Reference` should be
**generated from the source** (a small script over the `commandCheck(msg_text+2, "…")` sites, which
are mechanically greppable), listing for each command: literal string, argument offset, guarding
`#ifdef`s and therefore which boards have it, whether it writes `meshcom_settings`, whether it calls
`save_settings()`, and whether it is reachable in a release build. That artefact then also becomes
the input to 05 Phase 2 (the dispatch table) — the table _is_ this document in code form — and the
oracle that reconciles the icssw.org page with reality. Until then, 07 §1.2 should carry a warning
that its groupings are name-derived and unverified.

---

### F8-18: No glossary — and the project is bilingual

**What is missing.** The set is written in English against a codebase whose comments, ADRs and PR
convention are German, and whose domain vocabulary is amateur radio. A new contributor meets
`mheard`, `HEY`, `NCT`, `GRC`, `VIA`, `SSID` (in the callsign sense, not the WiFi sense — both
appear), `payload_type`, `max_hop`, `FCS`, `KEEP`, `GATE`, `CONF`, `trickle`, `CAD`, `CSMA`,
`shortpath`, `symid`/`symcd` with no definitions anywhere. 07 §1.2 lists 218 commands by _purpose_,
which is genuinely useful, and is the closest thing to a glossary that exists.

**Remedy — `00 — Glossary and Conventions`.** Cheap, and it is the document that makes the other
fifteen readable by someone who is not already inside the project. Must also state the
language convention (English docs / German PR descriptions per `CLAUDE.md`) so nobody has to guess.

---

## Existing docs that should be linked or superseded

Linked-from-concept status verified by grepping all eight documents for each filename.

Currency verified against the source, not against the documents' own claims.

| Doc                                           | Currently                               | Still accurate?                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                | Action                                                                                                           |
| --------------------------------------------- | --------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| `docs/README_LORA_TRX.md` (38 KB)             | **not linked**                          | It is **ADR-001** (message priority / slot negotiation), not a README. Status: "Phase 1 implementiert". §4.4/§5.3 are the only written wire-format back-compat analysis.                                                                                                                                                                                                                                                                                                                                                                       | **Rename** to `adr-001-…`; harvest §4.4/§5.3 into `08`; link from `13`                                           |
| `docs/adr-nc-importance-backoff.md`           | **not linked**                          | **ADR 02, Status: Draft, dated 2026-03-16** (the 2026-07-12 mtime is misleading). A _proposal_, not the shipped design.                                                                                                                                                                                                                                                                                                                                                                                                                        | Link from `13` with the status stated in the link text                                                           |
| `docs/prio-talk-flood-networking.md`          | **not linked**                          | **Upgrade — this is a real protocol contract**: the only written record of the 9 relay-stop conditions, hop-decrement-before-relay, which types relay (`0x3A`/`0x21`/`0x40`, never ACK), fire-and-forget `RING_STATUS_DONE`, originator-only retransmit (`MAX_RETRANSMIT 3`, ~40 s). Constants verified exact. **Breaks:** its "dedup wraps after 60–100" is now false (ladder is 100/60/60/70/10); all `lora_functions.cpp:NNN` refs stale; §Szenario 3 contains inline _retracted_ reasoning, so stopping early yields the wrong conclusion. | **Correction pass, then promote** — it is the backbone of `13`                                                   |
| `docs/hey-supp.md`                            | **not linked**                          | trickle constants live at `configuration_global.h:181–184`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     | Link from `13`                                                                                                   |
| `docs/adr-totp-remote-led.md`                 | **not linked**                          | **No.** `totp` has zero occurrences in `src/` — describes code that does not exist                                                                                                                                                                                                                                                                                                                                                                                                                                                             | Mark rejected/reverted, or delete                                                                                |
| `docs/code-audit-20260712.md` (40 KB)         | **not linked**                          | **Live.** 2026-07-12, same branch. SEC-01 (Critical) and SEC-02 (High) **verified still open** in the current tree                                                                                                                                                                                                                                                                                                                                                                                                                             | **Triage now**; land SEC-02 (one line); coordinate SEC-01 with upstream; then make it the backlog 06 refers to   |
| `docs/code-review.md`                         | **not linked**                          | **Actively misleading.** Contains a _reversed_ decision presented as settled: "bewusst `volatile int` statt `std::atomic`" was overturned by commit `8009aa19`. Following it re-introduces the bug. Also one wrong finding (MEM-01 calls a file-scope global a stack array).                                                                                                                                                                                                                                                                   | Add a superseded banner or delete; fold live items into the defect index                                         |
| `docs/codequality-rules.md`                   | **linked (01 §4)** — cited as normative | **No date, no version, no branch**, yet nine audits cite it. Generic ESP32 ruleset: `portMUX_TYPE`/`IRAM_ATTR`/`esp_task_wdt`/NVS stated unconditionally with **no nRF52 equivalent**, on a project with three nRF52 targets. Rules dead or permanently violated: `-Werror` (0 hits), Rule 17's `#ifdef NATIVE_BUILD` (0 hits), Rule 20 prescribes an `sdkconfig` that does not exist, "no `^` pinning" vs 10+ `@^` deps.                                                                                                                      | Version-stamp it, add an nRF52 applicability column, retire the dead rules — **before** 01 cites it as normative |
| `docs/code-audit-*.md` (9 files)              | linked as a glob                        | Stale verdicts — the 2026-05-08 headline "None fully closed" is wrong; ~half its CRITICALs are closed                                                                                                                                                                                                                                                                                                                                                                                                                                          | Supersede with the defect index (F8-16)                                                                          |
| `docs/code-audit-fixes-20260627.md`           | **not linked**                          | its own `A1`–`D5` namespace, unmapped to the others                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | Fold into the defect index                                                                                       |
| `docs/ram-opti.md`                            | **not linked**                          | **No.** Quotes superseded `MAX_*` values as current; 6 of 16 proposals landed, 9 did not, and it still reads as an open plan. **V5 is half-applied and worse for it** — `web_header_collect[1024]` exists but `String web_header;` survives at `web_functions.cpp:31`, two mechanisms for one job. Its own safety gate ("telemetry zwingend before reducing `MAX_RING`") was bypassed: the reduction shipped, the ring-peak counter does not exist.                                                                                            | Supersede into `14`; keep as dated appendix; **implement the missing ring-peak counter**                         |
| `docs/ram-comparison-2026051{4,7}.md`         | linked as a glob (07)                   | point-in-time, fine as history                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 | Appendices of `14`                                                                                               |
| `docs/loradebug-serial-output.md`             | linked (07 §1.1)                        | current                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        | **Keep — this is the model the other docs should follow**                                                        |
| `docs/report-ble-tx-latency.md`               | linked (03)                             | —                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              | Also link from the BLE-protocol section of `08`                                                                  |
| `docs/NimBLE.md`                              | **not linked**                          | —                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              | Link from 03 §NimBLE                                                                                             |
| `v435p_updates/tls_console_and_updates.md`    | **not linked**                          | All five items verified shipped. **But it never states the TCP port** — a client author cannot connect from it alone (`NET_CONSOLE_PORT 2323`). Leaves the orphaned `DISABLE_TLS_CONSOLE` guard alive at `command_functions.cpp:4832`.                                                                                                                                                                                                                                                                                                         | Migrate the console protocol + port into `07 §1.3` or `08`, then archive                                         |
| `src/code_review/code-audit-20260508.md`      | **not linked**                          | Byte-identical to `docs/` copy modulo 26 `<mark>` tags. **Concrete harm:** pollutes `grep -rn … src/` with false hits                                                                                                                                                                                                                                                                                                                                                                                                                          | Delete                                                                                                           |
| `release_lora_trx.md` / `release.md` (root)   | **not linked**                          | changelog-shaped                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               | Link from `02 §Versioning`; move under `docs/`                                                                   |
| `README.md` (root)                            | **not linked**                          | user-facing entry point                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        | The concept should be linked _from_ it                                                                           |
| `src/web_functions/Web-API_documentation.txt` | **not linked**                          | self-declared "not yet finished or stable"                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     | Convert to Markdown under `docs/`, link from `08`                                                                |
| `CLAUDE.md`                                   | cited once (05 §4)                      | the only record of the PR workflow                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             | Its human-relevant content belongs in `15`                                                                       |

---

## Tasks a new contributor still could not do

After reading all eight documents cover to cover:

1. **Add support for a new board.** They know the mechanism (02) and that `#ifdef` is the wrong
   answer (01, 05 Phase 4). They do not know the nine files to touch, that a hardware ID must be
   allocated in `configuration_global.h` _and that it is transmitted on air_, which of the two
   battery implementations to use, which of the three display-selection mechanisms applies, or which
   RAM branch their board lands in. → F8-12, F8-8, F8-1.

2. **Change or extend a wire-format field.** The set tells them this is the highest-risk thing in the
   codebase and that tests should exist. It gives them no field layout, no forward-compatibility
   rule, no statement of who else parses these bytes (the server, the phone app,
   `extras/decode_meshcom.py`), and no mention of the `FW < 35` cutoff that already partitions the
   fleet. → F8-1, F8-6, F8-10.

3. **Add a configuration field and ship it to deployed nodes.** They would add it to
   `meshcom_settings` — and there are two different structs of that name — add a `--command` arm, add
   a web-setup field, and have no way to learn that inserting rather than appending corrupts every
   nRF52 node in the field, that the ESP32 path needs the key added to _two_ hand-maintained lists,
   or that bumping `FLASH_VERSION` does not migrate or reset anything despite the log line saying so.
   → F8-2.

4. **Diagnose "board X drops packets" from a field report.** 07 gives them an excellent event-marker
   vocabulary — but no decision tree. Is it CAD? Dedup ring wraparound (board-size dependent, and the
   one document that analyses it states a range that is now wrong — F8-8)? A hop budget? A relay rule
   (F8-7)? A duty-cycle/region difference (F8-9)? An FCS mismatch from a version-skewed peer, or the
   `FW < 35` cutoff silently discarding them (F8-1)? The bug-report template asks for `--info`;
   nothing says what to do with the answer. → F8-1, F8-7, F8-8, F8-9, F8-15 (`loganalyse.sh`).

5. **Ship a release — or recover from a bad one.** They cannot answer: which boards can even be
   updated remotely (five ESP32/nRF52 boards cannot), what happens when an OTA fails, that there is
   no rollback and no crash-loop detection, which version constants to bump, what licence the binary
   is under, or who approves the merge. → F8-3, F8-4, F8-14, F8-11.

A sixth, worth naming because the concept's own plan depends on it: **execute 03 step 0b** — "RAM
baseline across all 32 envs" — is not possible with the tool as it is written (7 targets hardcoded).

---

## Proposed final document set

Existing 01–07 stay as they are; they are strong on structure, dependencies and verification. The
gap is that they describe the code and not the _contracts_, the _fleet_, or the _project_.

| #      | Document                                           | Status  | Answers                                                                                                                                                                                             |
| ------ | -------------------------------------------------- | ------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **00** | **Glossary and Conventions**                       | **new** | What do `mheard`, `HEY`, `NCT`, `GRC`, `VIA`, `FCS`, `KEEP` mean? Which language where?                                                                                                             |
| 01     | System Overview                                    | keep    | + a `§Power states` subsection (F8-13)                                                                                                                                                              |
| 02     | Build System & Variants                            | keep    | + `§Versioning and release` (F8-14); name the authoritative `lv_conf.h`                                                                                                                             |
| 03     | Dependencies                                       | keep    | + `§Licensing` (F8-4); fix the dead-`ENABLE_TBEAM` quote and the ram_snapshot claim (F8-8)                                                                                                          |
| 04     | Complexity & Duplication                           | keep    | + note that the UDP clone is an _external protocol_ forked (F8-10)                                                                                                                                  |
| 05     | Rewrite vs. Refactor                               | keep    | + correct: `compress_functions` is dead code, not a Phase-1 target (F8-15)                                                                                                                          |
| 06     | Test Strategy                                      | keep    | + a fuzz obligation for `decodeAPRS()` (F8-5); + a cross-version settings test (F8-2)                                                                                                               |
| 07     | Verification Infrastructure                        | keep    | + cite `prio-talk` rather than restate its table (F8-7)                                                                                                                                             |
| **08** | **Wire Format Specification (on-air v4.35)**       | **new** | Frame layout, payload grammars, FCS, hops, version negotiation, `FW < 35` cutoff. Sub-sections: **Phone/BLE protocol** (F8-6), **Server backhaul GATE/CONF/KEEP** (F8-10), **Web REST API** (F8-15) |
| **09** | **Configuration, Persistence and Fleet Upgrade**   | **new** | `meshcom_settings`, `FLASH_VERSION`, migration, ESP32 vs nRF52, factory reset                                                                                                                       |
| **10** | **Boot, Partitions, Safeboot and OTA**             | **new** | Flash map, update flow, recovery, the two-toolchain constraint                                                                                                                                      |
| **11** | **Vendored Code: Provenance, Divergence, Licence** | **new** | Upstream tags, local deltas, GPL/LGPL exposure, distribution obligations                                                                                                                            |
| **12** | **Threat Model and Untrusted Input**               | **new** | What is trusted where; RF parser as trust boundary; web/BLE/console/OTA auth                                                                                                                        |
| **13** | **Routing, Relaying and Channel Access**           | **new** | Hop budgets, relay rules, priorities, trickle-HEY, gateway role, **regional operation** (F8-9). Mostly links to the two existing ADRs                                                               |
| **14** | **Memory Budget and Per-Board Sizing**             | **new** | DRAM ceilings, static-consumer ranking, tuning knobs, per-board headroom                                                                                                                            |
| **15** | **Contributing, Ownership and Change Flow**        | **new** | Upstream PR workflow, review capacity, board ownership table, triage                                                                                                                                |
| **16** | **Adding a Board: Runbook**                        | **new** | The ordered checklist; also the spec for 05 Phase 4                                                                                                                                                 |
| **17** | **Command Reference** (generated)                  | **new** | All 218 verbs: literal, offset, board guards, persistence effect, reachability                                                                                                                      |
| **18** | **Defect Index** (`docs/defect-index.md`)          | **new** | One row per finding across all four ID namespaces: status, fixing commit, regression test                                                                                                           |

Eighteen documents is more than eight, so the ordering matters.

**Before any of them: triage `docs/code-audit-20260712.md` and land SEC-02.** It is one line, the finding is
verified, and the code path is reachable from received traffic. Writing architecture documentation
above an open Critical is the wrong order of work.

If only four documents are then written:

1. **08** — it unblocks 06 Layer 2, which unblocks everything in 03 and 05. Nothing else in the plan
   moves until the interop contract is written down. Note that ADR-001 §4.4/§5.3 already contains a
   chunk of it.
2. **09** — the only unmitigated fleet-wide data-loss risk. Today, one mid-struct field insertion
   corrupts every nRF52 node in the field and no artefact warns anyone.
3. **10** — five of thirty boards cannot be updated remotely at all, there is no rollback, and the
   update endpoint is unauthenticated. All three facts are currently discoverable only by reading
   `src/safeboot/`.
4. **18** — cheap, generated, and it is the prerequisite for 06's regression discipline being real
   rather than aspirational.

`11` is an afternoon and can stop distribution, so it should not wait long. `16` and `15` are the two
that most directly serve "x-ray vision for a new contributor", and both are cheap. `17` is nearly
free because it should be generated. `12` should be written before, not after, the first fuzz
campaign — and the fuzz campaign should exist, which the concept does not currently propose, despite
two of `docs/code-audit-20260712.md`'s High findings being ones a short fuzz run would have produced.

## Meta-observation on the set as it stands

01–07 are an excellent _code_ review: structure, dependency state, complexity, and a genuinely strong
verification build-out. What they systematically omit is the **contracts, the fleet, and the
paperwork** — everything that is true about MeshCom rather than about this source tree. All eighteen
findings fall into five buckets:

- **Contracts with something outside this repo** (F8-1 on-air, F8-6 phone, F8-10 server, F8-17
  operator, F8-15 web API) — five interfaces, zero specifications.
- **The installed base** (F8-2 persistence, F8-3 OTA, F8-14 versioning) — the concept plans changes
  without describing how they reach nodes or what they do to nodes on arrival.
- **Constraints that are not code** (F8-4 licence, F8-9 regulatory, F8-5 threat model, F8-8 memory
  budget) — the boundaries the design must respect.
- **The project rather than the program** (F8-11 ownership, F8-12 board runbook, F8-18 glossary).
- **What was already written down** (F8-5, F8-7, F8-16) — the set was produced without an inventory
  of existing documentation, so it re-derived the structural half of `docs/code-audit-20260712.md`, missed its
  security half, and never found the ADR series hiding behind a file called `README_LORA_TRX.md`.

That last bucket is the cheapest to fix and the most embarrassing to leave: roughly 180 KB of
relevant, largely accurate German-language design documentation already exists in this repository and
the architecture set links none of it. **The first action is not to write document 08. It is to spend
an hour reading `docs/` and `docs/code-audit-20260712.md`, then decide what still needs writing.** My estimate
after doing exactly that: `08`, `09`, `10`, `11` and `18` genuinely do not exist in any form; `13`,
`14` and `12` are substantially pre-written and need curation, correction and a link; `15`, `16` and
`17` are new but small.

The underlying pattern is one blind spot, not eighteen: the set answers _"what is this code like?"_
thoroughly, and _"what is this system obliged to do, to whom, and what has already been said about
it?"_ not at all. For the stated goal — x-ray vision on the way to modern, tested firmware — the
second question is the one that decides whether a change can ship.
