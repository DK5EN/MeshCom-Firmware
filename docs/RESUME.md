# RESUME -- pick up here

**Stand:** 2026-10-17, branch `fork-dev`, last release `v4.40a.10.17` (CI-built, Latest).

## 2026-10-17 -- AU bench run to v4.40a.10.17: 3 of 4 ESP32 nodes pass, AU-14 and AU-15 opened

- Dev-channel Auto Update (`--update check/install/apply`) to the published v4.40a.10.17: DK5EN-14 (T-Deck
  Plus), DK5EN-1 (Heltec V3) and DK5EN-92 (T-Beam) now run it, channel back on `prod`, mode `off`.
- **AU-14:** DK5EN-92 rebooted on the first try, 13 s into the download, `TASK_WDT` with the loop in
  `lora_rx`; the retry passed. Single event, open.
- **AU-15:** DK5EN-98 staged 10.17 but has no AU Safeboot (`none (old)`), so `apply` fell back to the old app.
  It needs one web-flasher USB flash, or an app OTA with `tools/webflash.py` (operator).
- DK5EN-90 (RAK4631, no AU) flashed with the release DFU zip over USB.
- Summary: `tools/bench/runs/harness/au1017_20261017-093623/au1017.summary`.

## 2026-10-17 -- release v4.40a.10.17 published (CI), web flasher

- Release [v4.40a.10.17](https://github.com/DK5EN/MeshCom-Firmware/releases/tag/v4.40a.10.17) (tag on
  `1a4a5c09`, CI run 39010666034): Latest, 66 assets (names as 10.16), 27 `.autoupdate` with `78 da`,
  re-inflate ok and byte-identical to a local regeneration, tag string in the image, T-Deck field commands
  `mute=2 stat=1 udplog=1 injectraw=0`, `tlsalloc` in the T-Deck Plus image. `FLASH_VERSION` 20261017.
- Content since 10.16: AU-13 (`c9e64344`), VIA-01 (`be7c3c7f`, `9b96d884`), TD-21 (`0146fa0b`), web firmware
  column (`c7fe948c`), `6d833289`. Gate before the tag: 72 envs, 2440/2440, golden 48, pytest 1080.
- Web flasher: only v4.40a.10.17 (30 boards, HTTPS check 0 mismatches); v4.40a.10.16 folder pruned (release
  object kept). Presentation index card and PR-history lead name 10.17 (pages-sync `b0ad04e7`).
- Not flashed to the fleet (operator choice). DK5EN-14, -92, -1 still hold a staged `v4.40a.10.16` AU record
  (mode `off`).
- **Still open:** VIA-01 bench test; AU-13 field confirmation (OE5HWN-16) and upstream PR; T-Beam Supreme
  PSRAM; PR history page does not list #1214 yet.

## 2026-10-17 -- AU-13 fixed on fork-dev: auto-update TLS on PSRAM boards

- **AU-13**: the auto-update check failed on T-Deck Plus (field OE5HWN-16 `[AU];tls;-32512`; bench
  DK5EN-14 `tls -1` right after boot, internal min 2.7 KB). The prebuilt mbedTLS allocates internal RAM
  only; PSRAM was never used. `src/esp32/fw_update_net.cpp` now installs a PSRAM-first mbedTLS allocator
  on `BOARD_HAS_PSRAM` builds and gates/logs on internal RAM (`[AU];heap;..;ifree;..;imin;..;iblk;..`).
  Bench DK5EN-14: 10/10 checks after reboot, internal min 55.7 KB; one download staged (1.57 MB, 22 s).
- **Left on DK5EN-14:** a staged `v4.40a.10.16` record (mode `off`, so no handover). Setting `--autoupdate
auto` would install that release over the fix build.
- **Control runs 2026-10-17 on c9e64344:** DK5EN-92 (T-Beam classic) and DK5EN-1 (Heltec V3) pass check and
  download/stage; T-Beam internal min 11.6 KB during the download (tight, as designed). Both now AU `off`/`prod`
  with a staged `v4.40a.10.16` record, like DK5EN-14.
- **Open:** upstream PR, T-Beam Supreme PSRAM (released in v4.40a.10.17)
  never initialised (no `BOARD_HAS_PSRAM`). Campaign: [`campaign-au-psram-tls-20261016.md`](campaign-au-psram-tls-20261016.md).

## 2026-10-16 (evening) -- VIA-01 fixed on fork-dev, RX-01 closed, REBOOT-98 opened

- **VIA-01** (`be7c3c7f`, `9b96d884`): `--via` was stored unchecked; field case DK8VW-44 set `DK8VW-P`, so
  nothing was meshed. `checkViaList()` now rejects non-numeric or out-of-range SSIDs, a leading zero, empty
  tokens, `*`/`H`/`HG` and lists over 39 chars; `NONE` and empty clear. Web `setvia` no longer reports FAIL for
  lowercase or `none`. Host gate green; **bench test and the upstream PR are open** (checks in the BACKLOG row).
  Won't fix (operator): a bad value already in flash survives the update; JSON import stays unvalidated.
- **RX-01 closed** (`7f4818c4`, `docs/rx01-soak-evaluation-20261005.md`): the SX1262 soak shows no evidence of a
  leak in the RX-timeout defer logic (a leak below ~1 % per day is beyond this data). The SX127x T-Beam would
  need its own run with ground-truth TX times; not scheduled.
- **REBOOT-98** (new, watch): DK5EN-98 rebooted once on 2026-10-04 20:38:59 with no panic text. Single event, cause
  unknown, compare BL-04. If it repeats: read the reset reason after boot.

## 2026-10-16 -- T-Deck DM reply (TD-21) + web firmware column: upstream PR #1214 open

- Upstream PR [#1214](https://github.com/icssw-org/MeshCom-Firmware/pull/1214) against `dev`:
  branch `pr/tdeck-dm-reply-web-fwversion` (worktree `../MeshCom-PR-tdeck`, `7010c12f` + `2a7842a6`,
  13 files, +259/-32), firmware only. Ports `c7fe948c` (web firmware column) and `0146fa0b` (TD-21).
  The duplicated `if(aprsmsg.msg_source_fw_on_air)` in `lora_functions.cpp` is fixed on both sides
  (PR copy and `fork-dev`), so the files match. After the merge: `/sync-upstream` (merge), then remove worktree and branch.
- Both on hardware: DK5EN-14 (T-Deck Plus, `0146fa0b`, operator hand test) and DK5EN-98 (`0146fa0b`,
  OTA 18:13; it needed more than webflash's 120 s to come back).

## 2026-10-16 -- release v4.40a.10.16 published (CI), web flasher, fleet flashed

- Release [v4.40a.10.16](https://github.com/DK5EN/MeshCom-Firmware/releases/tag/v4.40a.10.16) (tag on
  `27ce9d3c`, CI run 38879957594): Latest, 66 assets, 27 `.autoupdate` re-inflate ok, tag string in the
  image, T-Deck field commands `mute=2 stat=1 udplog=1 injectraw=0`. Base: official `v4.40a.hbserver`.
- Web flasher: only v4.40a.10.16 (30 boards, HTTPS check 0 mismatches), v4.40a.10.11 folder pruned
  (release object kept); presentation index card updated (PR history page: BACKLOG PAGES-01).
- Root `safeboot.bin`/`safeboot-s3.bin` replaced by the release assets: the Safeboot env compiles
  `esp32_flash.cpp`, which changed with the network mode.
- Fleet on v4.40a.10.16: DK5EN-1 (USB, gateway OE, BEAT), DK5EN-92 (USB 460800, gateway OE, BEAT),
  DK5EN-90 (DFU, DHCP .62), DK5EN-98 (OTA webflash, gateway OE, server connected).
- Gate for the release: Unity 72 envs 2419/2419, golden 48, pytest 1080.

## 2026-10-16 -- gw_servers: upstream PR #1213 open (ready for review); bench matrix on real nodes

- Upstream PR [#1213](https://github.com/icssw-org/MeshCom-Firmware/pull/1213) against `dev`:
  branch `pr/gw-servers` (worktree `../MeshCom-PR-next`, `cd911c6e` + `87f2f583`, 13 files, +774/-357).
  Opened as draft, marked ready for review on 2026-10-16. Kurt reviews and merges. After the merge: `/sync-upstream` (merge, not rebase), then remove the
  worktree and the branch.
- Bench driver `tools/bench/gw_net_matrix.py` (oracle = `tools/gw_servers_dump.cpp`), 45 cases per node.
  Results and open items: `docs/campaign-gw-net-matrix.md`. Server choice right in all 90 cases.
- N3 (gateway rpizero) Internet cases fail by design: rpizero NATs only to ppp0 (Hamnet). Only N3
  Hamnet counts. Never capture on rpizero (it died from promiscuous tcpdump on 10-16).
- `tools/bench/fleet.json` hosts refreshed; hosts go stale with the short DHCP leases.

## 2026-10-15 -- gateway server table (gw_servers): implemented on fork-dev, upstream PR next

Upstream #1209-#1212 (Kurt) moved DL/HC/HB around for five days; the fork temp fix `389ace0d` is reverted and
`upstream/dev` is merged (`39642f0a`). Concept, reviewed by an independent fable pass:
`docs/gw-servers-unification.md` (one table `GW_SERVERS[]` in `src/gw_servers.{h,cpp}`, per-side modes
DIRECT / OTHER_NET / VIA, `--setnet auto|inet|hamnet`, value 2 = auto and factory default, FR/US rows commented
out). Decisions are in section 7, the upstream history of DL/HC/HB in Appendix A. Plan: PR 1 = table + ETH-05 fix
(static nRF52 must stay static, section 3.7) + `node_ownntp` on DHCP nodes; PR 2 = auto mode and `--setnet`;
PR 3 = FR/US when the operators name servers. Open: FR/US addresses; the sysadmin wish (DL Internet only,
Hamnet via HC) is second-hand via Kurt. Backlog row ETH-05.
**Implemented** (commits `29c09ac8`, `cf66ad18` and the bench follow-up): `src/gw_servers.{h,cpp}` +
`native_gw_servers`, ESP32 / nRF52 callers on `gwResolveNode`, legacy golden
(`test/golden/gw_servers_legacy.json`, produced by running the old code) with a named-delta diff test, ETH-05 fix
(bench-verified on DK5EN-90, see BACKLOG), network mode 0/1/2/3 (`--setnet auto|inet|hamnet`, no flash migration,
factory default 2 only for new/wiped nodes). Gate: `tools/regression.sh --stage 1,2` PASS (Unity 2419, pytest 1062).
Left: upstream PR (draft, German description, port onto `upstream/dev`), regenerate the stale
`test/golden/corpus/commands` separately, FR/US rows when addresses exist.

## 2026-10-13 -- tools/loganalyse.sh campaign done (W0-W2b)

`tools/loganalyse.sh` reads firmware 4.40a logs again (colon-less tags, `--setlog` `[LOG]` lines,
path-head collision logic, pre-f2d6532f billing warning, reboot-aware dedup balance, n/a for
captures without line timestamps); 127 tests in `tools/mock/test_loganalyse.py`. Campaign doc:
`docs/campaign-loganalyse-20261012.md`. Left: logauswertung skill references (Out of scope there),
the doc errors listed under Open leads. Net-console corpus pulled to `~/meshlog/rpizero/`; fresh
dk5en-98 capture on rpizero `~/meshlog/dk5en-98/2026-10-1{2,3}.log` (ends 2026-10-13 08:00).

RCA for the DUP counter gap and slow OnRxDone, brief in
`docs/rca-brief-dup-onrxdone-20261013.md` (DK5EN-1 Heltec V3 is the flashable bench node).
**Wave R0 done (read-only, advisor-gated):** findings in `docs/rca-dup-r0-findings-20261013.md` and
`docs/rca-onrxdone-r0-findings-20261013.md`. Issue A: the 43-line gap is 39 GWI echoes the firmware does not count
(own-msg_id branch, `lora_functions.cpp:1552`) plus 4 lost-STAT lines; operator decision script fix vs firmware fix
pending. Issue B: handler time tracks bytes printed over UART (R2 0.985), reception loss not shown; R1 flags matrix
on DK5EN-1 next. Resume from the brief's "R0 status" section.
**Wave R1 built and gated** (INSTRUMENT firmware + `tools/bench/onrx_matrix.py`); bench run done (print time explains the slow handler). **Wave R2 built and gated:** `loganalyse.sh` DUP fix, console TX ring `CONSOLE_UART_TXBUF`; DK5EN-1 benched; T-Beam bench, DK5EN-98 byte-model nights and the upstream PR are open, see the brief's "R2 status". **DK5EN-98 runs `fork-dev` 8a1b2564 since 2026-10-13 19:53 (OTA); see "R3 status".**

## 2026-10-11 -- v4.40a.10.11 released (first CI-built fork release)

**Shipped:** tag `v4.40a.10.11` on `fork-dev` (`77e12f4e`), built and published by the re-enabled `MeshCom CI`
workflow (tags `v*` publish as Latest, `ci-proof-*` as draft); 66 assets (39 + 27 `.autoupdate`), all
`.autoupdate` re-inflated byte-exact. Web flasher serves the release's app images (`pages_flasher.py
--from-release`), live check 30 boards 0 mismatches, `v4.40a.10.06` pruned. Content: upstream fold-in, RM
`led flash` / `chgled`, charge-LED fixes CHG-01..04, AU classic TLS heap gate (AU-D13), harness logs moved to
`tools/bench/runs/harness/`. Campaign doc: `docs/campaign-upstream-fold-20261011.md`.

**Verified after publishing:** DK5EN-92 (T-Beam classic) and DK5EN-1 (Heltec V3) installed the release through Auto
Update on the dev channel (both nodes now `--updchan dev`, DK5EN-1 in mode `auto`). Classic download heap minimum
13,952 B: only ~2.4 KB margin under the 76 KB gate -- watch for `lowheap` refusals on busier classic nodes.
DK5EN-98 (production gateway) OTA'd to the release app with `tools/webflash.py` (back on the server); its
Safeboot is still the old one, so AU stays locked there until one USB flash with the web flasher.
DK5EN-90 (RAK4631) flashed over USB DFU with the release `wiscore_rak4631.zip`; identity guard ok.

**Accepted by the operator 2026-10-11:** SNF-GW/STOR (#1188), NMTU leftovers (#1190), BLC B1 carry-over (#1191).
**Still open:** RM items (sync limiter/RM-DUP on hardware, late echo after "No answer", RM1 uploads to the public
server, RM-GWRELAY against a real server), Heltec V4 remote LED on hardware, upstream PRs (none opened; charge-LED
fixes are the first candidate).

## 2026-10-07 -- RM extended commands campaign: where it stands, how to continue

**Done (fork-dev, not pushed, not released):** nine new RM commands (radio, name, atxt, pos, sens, mh, txq, mbox, maxhop)
with page cards; sender budget 2 / 10 with the proof rule (RM-PROOF); commands accepted over the server path (RM-GWRELAY,
`rm_rx_gate.h`); reject counter and lockout per sender callsign-SSID, never per path; RAK `radio` unit fix; Remote page
buttons no longer wrap (`37a464fa`); duplicate ring for the second copy of one frame (RM-DUP, `661aca73`); gateway bench
toolbox `tools/bench/gwbench/` (`8b24d0ac`). Plan, decisions, both RF bench rounds and the gateway round:
`docs/rm-gui/extended-commands-impl-plan.md` sections 6, 9, 10; evidence `docs/rm-gui/gateway-bench-20261007.md`.
Host gate green: 67 envs, 2203 Unity cases, 823 pytest, 240 page checks.

**Gateway bench 2026-10-07 (both bench nodes as gateways against a mock on the rpizero):** the server path works, the
server copy was first in 40 of 40 commands, nothing ran twice, a command round trip is about 2 s against 10 to 110 s
over RF alone. Finding RM-DUP (RF copy of a rejected frame counted again) is fixed in `661aca73` but **not yet benched**.

**State of the bench when work stopped:**

- DK5EN-1 Heltec (now `192.168.68.71`, `dk5en-1.local`) and DK5EN-90 RAK (now `192.168.68.73`, no mDNS) run the INSTRUMENT
  image of `8b24d0ac` WITHOUT the RM-DUP fix; gateway off and confirmed on both, RM on at DK5EN-1 only. Addresses are DHCP
  and moved overnight once: resolve before every run (`tools/bench/fleet.json` still lists old hosts).
- The RAK's name and APRS text were cleared again after a driver bug wrote a literal `-` (restore was done by hand).
- DK5EN-92 T-Beam (`192.168.68.57`) was not part of the gateway round. Bench passwords:
  `~/MeshCom-bench-backups/bench-node-passwords-20261006.txt`. DK5EN-98 (`192.168.68.62`) is production, never a target.
- The permission classifier denies OTA flashing of bench nodes ("Production Deploy") and name/text changes; the operator
  runs those commands with `!` or grants them with `/permissions`.

**Next steps, in this order:**

1. Flash the instrument image of `661aca73` onto both nodes: Heltec
   `python3 tools/webflash.py --env heltec_wifi_lora_32_V3 --bin .pio/build/heltec_wifi_lora_32_V3/firmware.bin <heltec ip>`,
   RAK `PLATFORMIO_BUILD_FLAGS="-DINSTRUMENT_ENABLED=1" pio run -e wiscore_rak4631 --target upload --upload-port <usb port>`
   (rebuild both first with the same flag, no space after `-D`; check `strings firmware.elf | grep -c 'SRVIP\];err'` is 1).
2. Fix three driver gaps in `tools/bench/gwbench/gwrun.py`: never write back the `-` placeholder of an empty name or text;
   treat a refused send (TX backpressure `QRT NOT SENT` at the sender, `/rmsend` refusal) as a failed case, not as `sent`;
   wait for a drained Heltec TX ring before the negative phase.
3. Rerun `gwrun.py` (README in the toolbox) and check: one junk frame gives rej +1 (not +2), the lock comes at the third,
   a valid frame while locked gets no reply, the Heltec to RAK plain DM and group message arrive once with ACK.
4. Open question from the run: the Heltec TX ring waits minutes in gateway mode (mean 203 s, ring full 19/19) while the RAK
   waits seconds. Find out whether that is the production mesh traffic it relays or a defect before blaming RM.
5. Put the stock images back on both nodes (build without the instrument flag), then look at the Remote page in a
   browser: the "Run again" button must stay on one line.
6. Decisions that stay with the operator: push `fork-dev`, release, upstream PRs (none opened for the RM campaign).

**Hardware needed on the MacBook for that:**

| Item                            | Why                                                                                             |
| ------------------------------- | ----------------------------------------------------------------------------------------------- |
| RAK4631 DK5EN-90 on USB         | serial log with DTR (`/dev/cu.usbmodem1101`), USB upload; also on the wired LAN                 |
| Heltec V3 DK5EN-1 on power/WiFi | OTA and net console 2323, no USB needed; its GPS stays connected                                |
| rpizero reachable by ssh        | hosts the mock MeshCom server (`~/gwmock`); a Wi-Fi mock on the Mac was unreachable for the RAK |
| MacBook on the same LAN         | web API, ssh, loggers; Chrome or Safari for the page check                                      |
| T-Beam DK5EN-92 (optional)      | RF-only checks and a third callsign for the per-sender lock proof                               |
| T-Echo, T114                    | not available: stay untested                                                                    |

**Tests still to run:** items 1 to 3 above (RM-DUP on hardware, plain traffic, negative phase over both paths); an RF-only
smoke of the RM matrix on the stock images after the reflash; the page check; later, when a gateway against the real
MeshCom server is acceptable, one RM command through the real server (does the server forward DMs without a message
number? unknown); the forced attempt after a re-key; hostile name/APRS text on a node is not tested by design. Then the full
`tools/regression.sh --stage all` once before any release.

## 2026-10-05 -- web GUI for #1187/#1189/#1190, release v4.40a.10.06

- Shipped (fork-dev `e1801d9f`, `839e952b`): Auto Update as one web selector off/prod/dev, MTU card,
  remote-management card + send panel (`POST /rmsend`, `GET /rmstatus`), header "Meshcom 4.40a";
  console `--rm` renamed to `--remotemgmt` (`node_rm` and web key `rm` unchanged).
- Release `v4.40a.10.06` from `573b6444`: 66 assets, flasher keep 1 (10.05 pruned, 30/30 verified),
  Safeboot unchanged from 10.05. Dated ahead because the AU tag parser rejects a `.2` suffix: never cut
  a same-day `.N` release while AU exists; date it the next day instead.
- AU dev channel on DK5EN-1: check found 10.06, GitHub download 1.11 MB in 10.8 s, Safeboot apply
  1.74 MB in 10.6 s, boots `tag=v4.40a.10.06`.
- Bench nodes: DK5EN-1 runs v4.40a.10.06 (updchan dev); DK5EN-90 runs the 839e952b dev build, RM off.

## 2026-10-05 -- campaign #1187-#1191 done, release v4.40a.10.05

- Shipped (fork-dev, `c16a9d5f`..`bfe5310c`): MTU default 1280 on every board and Safeboot (#1190),
  BLE reconnect hardening (#1191), S&F for PMs from the server + `--stor` (off, #1188), RM1 remote
  management with HMAC (off, #1189), Auto Update (off, #1187: `.autoupdate` staging in the end of
  `ota_0`, five GitHub roots, Safeboot ROM-tinfl apply, classic ESP32 pauses BLE, old-Safeboot
  detection). Plan and decisions: `docs/concept-open-issues-20261004.md` (status table, section 9).
- Release `v4.40a.10.05`: 66 assets (39 + 27 `.autoupdate`), web flasher (keep 1, 30 boards verified),
  new Safeboot images. Dev-channel AU test on DK5EN-1 against this release: check, real GitHub
  download (1.10 MB in 10.8 s), Safeboot apply (1.73 MB in 10.5 s), boots `v4.40a.10.05`.
- Open: STOR needs the server operator's approval; SNF M1-M4 against the real server; prod AU needs
  icssw-org to publish `.autoupdate` assets; upstream PRs for the five features not opened; BLE backlog B1
  (bleQueue carry-over); classic Safeboot headroom 1.5 KB (drop ESPmDNS next time it grows).
- AU-12 (Safeboot capability version): Safeboot carries `MCSB;ver;002`, the app scans the partition and
  locks auto update (command, boot demotion, handover, greyed web dropdown + footnote) below
  `AU_SAFEBOOT_MIN`. Contract: `docs/safeboot-ota-contract.md` "Capability version". Needs a web-flasher
  update on every node whose Safeboot predates 2026-10-05; not yet released. Bench 2026-10-05 passed:
  DK5EN-92 with the pre-AU Safeboot (git 12e35ad0, v0): `safeboot;old;sbver;0`, `--autoupdate auto` refused,
  web `aumode=dev` fails with the channel untouched, dropdown disabled + footnote, a config-import auto mode
  demoted to notify at boot; v1 (strings) and v2 (marker) read correctly on DK5EN-92/-1; DK5EN-1 full USB
  flash gives `sbver;2` and `[SAFEBOOT];ver;2`. DK5EN-92 restored to the current Safeboot (v2), AU off. The
  nocap-reflash path (F1) is host-tested only.
- Bench nodes: DK5EN-1 runs v4.40a.10.05 (new Safeboot); DK5EN-90 normal build, `--remotemgmt off`;
  DK5EN-92 runs an INSTRUMENT v4.40a.10.05 with the OLD Safeboot (flagged `safeboot;old`, a staged
  test record remains, AU off). node_passwd on DK5EN-1/-90 set to bench values (rmbench1/rmbench90).

## Branch model

- `fork-dev` is the only working branch: `upstream/dev` (icssw-org/MeshCom-Firmware) plus the fork's docs,
  tests, tools, GitHub Pages **and fork-only firmware** (`src/`, `platformio.ini`, `variants/`) that is not
  upstream yet. New firmware lands on `fork-dev` first; upstream PRs are cut from `upstream/dev` later.
- Retired 2026-10-02: `fork-main`, `fork-neo`, `fork-neo-test`, `master` (tag
  `archive/fork-neo-test-20261002`). Nothing is developed there any more.
- Upstream sync: cherry-pick, never merge the fork history in. After icssw-org squash-merges a PR
  of ours, **merge** `upstream/dev` (not rebase) to re-sync.
- All PRs target upstream `dev`; PR text is German and lists files, functions and the why.
  Minimal, targeted changes only. DK5EN never merges upstream himself (Kurt reviews and merges).
- Open upstream PRs by dk5en (checked via `gh` 2026-10-16): #1213 (`gw_servers`, ready for review) and
  #1214 (T-Deck DM reply TD-21 + web firmware column). Merged since 4.40a: #1200, #1201, #1207, #1208.
  Fixes of the 10-04 campaign (N-36, WEB-04, TD-09/11/15, T5-01) are in `upstream/dev` too.
- The temporary OE Internet server fix `389ace0d` (lost in upstream #1209) is reverted (`a748948d`,
  2026-10-15); `upstream/dev` was merged with Kurt's #1210-#1212. Nothing temporary remains in `src/`.
- Worktrees for the open PRs: `../MeshCom-PR-next` (#1213), `../MeshCom-PR-tdeck` (#1214). Remove them
  and their branches after the merge and `/sync-upstream`.

## Last release and flasher policy

- `v4.40a.10.16` (tag on `27ce9d3c`, CI run 38879957594), base official `v4.40a.hbserver`; 66 assets.
- CI builds and publishes since `v4.40a.10.11`: `MeshCom CI` (`.github/workflows/meshcom-ci.yml`) fires on
  `v*` tags (release, Latest) and `ci-proof-*` tags (draft; delete draft and tag afterwards).
  `ci-build.yml` stays disabled. Recipe: `/release-firmware`.
- The web flasher on gh-pages offers only the latest release (`--keep 1`); root `safeboot*.bin` are the
  release assets.
- Never cut a same-day `.N` release (the AU tag parser rejects it): date it the next day.
- Release history: [docs/release-journal.md](release-journal.md); user-facing delta:
  [release-notes.md](../release-notes.md).

## How to verify the tree

- Host: `tools/regression.sh --stage 1,2`. With the bench fleet attached: `--stage all`. The
  `/full-regression` skill wraps it (local only, `.claude/commands/` is gitignored).
- Stage 1: every `[env:native*]` (72 envs, 2437 Unity cases on 2026-10-16) plus `test/golden/selftest.sh`.
- Stage 2: ruff lint gate, pytest over `tools/bench`, `tools/tests`, `tools/mock` (1080 cases on 2026-10-16), the
  `test/test_nbrlog` scripts, node tests, the jsdom safeboot page test and four `--self-test`
  tools.
- Stage 3: `tools/bench/bench_suite.py` (nodes matched by USB serial, identity guard as the gate,
  `prepare <node>`, per-board harness, OTA with its own image build, web GUI badge, mesh exchange;
  `--flash`, `--extudp`, `--deepsleep` are opt-in, `--instrument` is the default). Green on RAK,
  T-Beam and T-Deck on 2026-10-04 (run 9, `docs/bench-20261003-stage3.md`). A bench run flips the
  PlatformIO checksum (`PLATFORMIO_BUILD_FLAGS`), so the next stage 1 builds cold.
- **Never run bare `pio test`**: it walks the board envs and flashes attached hardware.
- **One `pio` process at a time**: shared build cache; `selftest.sh` itself calls
  `pio project config`.
- Inventory of every suite: [docs/test-suite-map.md](test-suite-map.md). Runner details:
  [docs/automation-runner-runbook.md](automation-runner-runbook.md).
- Not verified until lint, typecheck, format-check and the real suite have run.

## Bench fleet

State 2026-10-16. All four nodes run `v4.40a.10.16` except DK5EN-14 (T-Deck Plus) and the production
DK5EN-98, which run `fork-dev` `0146fa0b` (TD-21). Registry: `tools/bench/fleet.json`.

| Node     | Board           | USB serial              | Host (2026-10-16)    | Role / state                             |
| -------- | --------------- | ----------------------- | -------------------- | ---------------------------------------- |
| DK5EN-90 | RAK4631 (nRF52) | 230D6EBB3266D20E        | 192.168.68.62 (DHCP) | DFU-flashed, no 2323 console, no mDNS    |
| DK5EN-14 | T-Deck Plus     | E0:72:A1:AD:65:E0 (MAC) | `dk5en-14.local`     | `0146fa0b`, TD-21 hand-tested            |
| DK5EN-92 | T-Beam v1.2     | 573C000584              | 192.168.68.70        | USB 460800, gateway OE                   |
| DK5EN-1  | Heltec V3       | 0001                    | 192.168.68.71        | USB, gateway OE, net console off default |

DK5EN-98 is the production Heltec V3 (gateway, logger on rpizero), not a bench node; it is flashed over
the air with `tools/webflash.py` (allow more than its 120 s wait for the node to return).

Handling:

- Ports move and DHCP moves the IPs between sessions (short leases: on 10-16 a listed address had become
  DK5EN-98). Confirm the callsign on the start page or via `identity_guard.py --port` before any flash or
  test. Resolve ports by USB serial (ioreg or pyserial), never by name.
- Harness `--node` takes the fleet key (`t-deck-14`), not the callsign.
- Heltec V3 on CP2102: DTR is the PRG button, a held DTR is a long press (deep sleep). The guard
  asserts DTR only on native-USB ports.
- Opening a port reboots every ESP32. The RAK needs DTR. The T-Beam flashes at 460800 (921600
  fails).
- The RAK web GUI has no mDNS (the Ethernet path ships no responder): reach it by IP from the
  boot log (`Ethernet.localIP():`).
- rpizero NATs only Hamnet: Internet cases on a gateway behind it fail by design. Never capture on it.

Hard bench rules:

- Test traffic only to group `TEST` or as a direct message to an own node. Never broadcast.
- Never a foreign callsign (never OE1XAR, never XX0XXX).
- Bench TX power at most 2 dBm.
- Run `tools/bench/identity_guard.py` before any test.
- No USB port open while `serial_capture.py` holds it (check `lsof`).
- `pio run -t upload` needs an explicit `--upload-port`; autodetect can land on the T-Deck.

## Where things are

- Open work: [docs/BACKLOG.md](BACKLOG.md) is the single list. Do not copy items here.
- Tests: [docs/test-suite-map.md](test-suite-map.md); runner:
  [docs/automation-runner-runbook.md](automation-runner-runbook.md).
- Releases: [docs/release-journal.md](release-journal.md), [release-notes.md](../release-notes.md).
- History: [docs/archive/](archive/README.md). The session journal 2026-09-03 .. 2026-10-01 that
  used to live in this file is
  [docs/archive/resume-journal-20260903-20261001.md](archive/resume-journal-20260903-20261001.md).

## Hottest first (details in BACKLOG)

- Upstream: #1213 and #1214 wait for Kurt's review. Next PR candidate: VIA-01 after its bench test.
- VIA-01 bench test (`--via dk8vw-p` rejected, `none`/empty clear, `on`/`off` still toggle, web `setvia`).
- GWRX-01: the gateway re-transmits server-injected frames after hearing another node relay them (39 times in
  one night on DK5EN-98). Operator decision open, with the DUP script-vs-firmware choice and whether the
  OnRxDone/console-TX-ring fixes go upstream (`docs/rca-brief-dup-onrxdone-20261013.md`, "Open questions").
- RM items: sync limiter/RM-DUP on hardware, late echo after "No answer", RM1 uploads to the public server,
  RM-GWRELAY against a real server; command unification parked (Q1-Q5).
- Gateway server table follow-ups: FR/US rows when operators name servers; regenerate the stale
  `test/golden/corpus/commands`.
- `--mesh off` still transmits on two RF paths (gateway DM-ACK, DM-store custody; BL-01): operator decision.
- GPS: altitude Kalman re-seed length (BL-03) and baro fusion tau (GPS-05b), both measured, neither changed.
- Stability, both single events: TASK_WDT on DK5EN-1 (2026-09-28, BL-04) and REBOOT-98 (2026-10-04).

## Last three sessions

- **2026-10-16:** release `v4.40a.10.16` (CI) and fleet flash; gw_servers bench matrix on real nodes and
  upstream PR #1213; TD-21 plus web firmware column as PR #1214; VIA-01 fix; RX-01 soak evaluated and closed,
  REBOOT-98 opened; presentation/PR-history pages refreshed (PAGES-01 closed).
- **2026-10-15:** gateway server table `gw_servers` (concept, implementation, ETH-05 fix, network mode
  auto/inet/hamnet); `389ace0d` reverted and `upstream/dev` merged (#1210-#1212).
- **2026-10-13:** DUP counter gap and slow OnRxDone RCA (R0-R3: console TX ring `CONSOLE_UART_TXBUF` 2048,
  `onrx_*` in the STAT line, DK5EN-98 OTA); `tools/loganalyse.sh` campaign W0-W2b done.
