# Campaign RM1 `net` / `upd` / `rs=` (McApp handover 2026-10-17)

Request: `docs/mcapp-rm1-net-update-handover-20261017.md`. Fork-only (RM1 and AU are not upstream), no
upstream PR. Base `fork-dev` at `fcbc15b8`.

## Final reply grammar (sent back to McApp)

| Command        | Reply                                                                                                                                                                           |
| -------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `net`          | `ok mtu=<1280..1500> srv=<CC> net=<auto\|inet\|hamnet> eff=<inet\|hamnet\|->`                                                                                                   |
| `upd`          | `ok m=<off\|notify\|auto> c=<prod\|dev> n=<0\|1> a=<tag\|-> st=<tag\|-> b=<0\|1> g=<0\|1> ck=<min\|-> e=<err\|->`                                                               |
| `upd check`    | `ok checking`, `err aubusy`, `err nowifi`, `err unsupported`                                                                                                                    |
| `upd go <tag>` | `ok installing <tag>`, `ok applying <tag>`, `err stale <avail\|->`, `err current`, `err noasset`, `err notready`, `err aubusy`, `err nowifi`, `err safeboot`, `err unsupported` |
| `status`       | unchanged plus trailing `rs=<0\|1>` (node_rmstrict)                                                                                                                             |

Argument shape `UPD`: empty, `check`, `go <tag>`; tag `[A-Za-z0-9._-]{1,23}` (AU buffers are char[24]).

Deviations from the draft, and why:

- `err tag` and `err busy` already exist with other meanings (HMAC mismatch, sender 10 s spacing), and
  error tokens are one word: renamed to `stale`, `aubusy`, `nowifi`, `current`, `noasset`.
- Staging records the tag as installed, so after staging a check reports `n=0`. `upd go` accepts the
  staged tag too; McApp enables "Update now" for `n=1` **or** `st=<tag>`, and not while `b=1` or `g=1`.
- `g=` (remote go pending) added: a go waits for its download and for the handover checks.
- `ck=` is possible after all (`FwNetStatus.lastCheckEpoch`).
- Boards without AU (nRF52/RAK4631) answer every `upd` with `err unsupported`.
- Rate limit: option (a), `rs=` in `status`. Option (b) (defer in the queue) not taken.
- `err notready` (advisor round 1): a go that needs a download is refused when the auto path would not
  download either (no valid clock for TLS, a phone connected, or battery below the AU-D17 floor). `err safeboot` also covers a handover
  already attempted for that tag this boot.

## Operator decisions 2026-10-17

- Remote `upd go` allowed regardless of `node_autoupd`.
- Before the handover: all auto-path checks except the update window (no phone, battery floor
  AU-D17, Safeboot capable); not before 8 s after the reply.
- Firmware Remote web page: full parity with McApp (cards plus Check and Update now with confirm).

## Waves

| Wave | Content                                                                                         | State |
| ---- | ----------------------------------------------------------------------------------------------- | ----- |
| 1    | A1 wire + `rs=`, A2 executors, B AU bridge, C web page (contract `src/rm_au.h` by orchestrator) | done  |
| 2    | Gate: regression stage 1,2; clean builds heltec_v3, ttgo_tbeam, wiscore_rak4631; advisor        | done  |
| 3    | Bench DK5EN-1: RM1 over LoRa (own DM, <= 2 dBm), real `upd go` from an older-labelled build     | done  |
| 4    | Docs: RM1 spec, RESUME, BACKLOG; reply to McApp                                                 | open  |

### Wave 1 file ownership

| Agent | Files                                                                                                                                                                                                                     |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A1    | `src/rm_commands.h`, `src/remote_cmd.cpp`, `src/rm_sender_policy.h`, `src/rm_runtime.cpp` (status call), `tools/remote_cmd.py`, `tools/tests/` (vectors + py tests), `test/test_remote_cmd`, `test/test_rm_sender_policy` |
| A2    | `src/rm_exec_ext.h`, `src/rm_exec_read.cpp`, `src/rm_exec_write.cpp`, `test/test_rm_format`                                                                                                                               |
| B     | `src/esp32/rm_au_esp32.cpp` (new), `src/rm_au_none.cpp` (new), `src/fw_update.h`, `src/esp32/esp32_main.cpp` (auTick), `test/test_fw_update`                                                                              |
| C     | `src/web_functions/web_rm_page.cpp`, `tools/webgui_rm_test.js`, `test/test_rm_web_parse` (if needed)                                                                                                                      |
| Orch. | `src/rm_au.h` (contract), `platformio.ini` if needed, docs                                                                                                                                                                |

### Wave 1 status (2026-10-17 evening)

- All four writers returned; gate run 1: ESP32 builds failed (missing include in `rm_au_esp32.cpp`),
  `native_command_setters` 3 pinned source scans red (command count 23 -> 25, handover sequence moved
  into `auHandoverRecord`). RAK4631 build green, everything else green.
- Advisor (Opus): NOT APPROVED, six items, all verified: go armed forever on a tag mismatch, silent
  no-op after an already attempted handover, FWNET_STUB not `unsupported`, remote download without the
  clock/phone checks, two wrong token sentences, no host tests for the go decisions.
- Orchestrator fixed the token side (`notready`, sentences). A fresh Sonnet-high implementer owns the
  rest (R1-R7: include, nostage/attempted disarm, stub guards, NOTREADY, pure go helpers + tests,
  command_setters scans). Then: gate again, advisor on the rework diff, commit.

### Wave 1 closed (2026-10-17)

- Rework done (fresh Sonnet-high implementer R1-R7, orchestrator: `notready` token, battery floor in
  NOTREADY, sentences). Advisor pass 2: approved after the `notready` sentence named the battery.
- Gate run 2: Unity 72 envs, 2520/2520; stage 2 PASS; clean builds heltec_wifi_lora_32_V3 (RAM 38.5 %,
  flash 54.4 %), ttgo_tbeam, wiscore_rak4631 green. Image scan: `remote;fail;nostage`, `err notready`,
  `mtu=%`, `upd go` present in the Heltec V3 image.
- Next: wave 3 bench on DK5EN-1 (sender DK5EN-92, needs the new build for the web page; ask before
  flashing DK5EN-92).

### Wave 3 bench (2026-10-17 22:00-22:20)

- Operator: OTA flash DK5EN-98 (production gateway, sender) and DK5EN-1 (target) with `5c1d12b5`;
  test over LoRa between the two. RM1 driven through DK5EN-98's `POST /rmsend` (dst + pw form).
- Verified over LoRa: `status` `rs=0` / `rs=1`, `net`, `upd`, `upd check`, `upd go` stale and current.
- Real remote install: INSTRUMENT build on DK5EN-1, `--update stagelan` of the v4.40a.10.17 release
  asset from the Mac, `upd go v4.40a.10.17` -> `ok applying` -> Safeboot -> release build 07:00:19
  up 31 s after the reply. DK5EN-1's reinstall guard (`last` = v4.40a.10.17) blocked `n=1` on a
  normal check, so the `installing` (download) path is host-tested only.
- Bench finding fixed: `ck=-` whenever the system clock is not set (DK5EN-1 has a GPS clock;
  `time()` counts uptime). `fwCheckAgeMin` in `fw_update.h` keeps both stamps in one clock domain;
  test fails before, passes after; on air `ck=0` after a check.
- Not a firmware bug: `/rmsend` decodes `%20` only, not `+` (Python `urlencode` default). The page
  and McApp send `%20`.
- End state: DK5EN-98 on `5c1d12b5` (21:51:17); DK5EN-1 on the dev build with the `ck` fix
  (22:14:44), `--updchan prod`, `--rmstrictsecurity off`, autoupdate off as before.
- Reply to McApp: `docs/mcapp-rm1-net-update-reply-20261017.md`.
