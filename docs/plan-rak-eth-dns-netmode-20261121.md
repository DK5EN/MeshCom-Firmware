# RAK Ethernet: own DNS ignored (P-0154) and `--info` network mode (P-0163), fix plan

Date: 2026-11-21. Branch `fork-dev` at `067d29cd`. Problem files (read-only, outside this repo):
`telegram/problems/P-0154-rak-ethernet-ignores-own-dns-and-ntp.md` and
`telegram/problems/P-0163-info-network-mode-wifi-on-rak-lan.md`; their RCA cites path:line at pin
`96aaa67c72`, every line below was re-read at HEAD. Nothing is implemented; this is the plan.

Two independent fixes, two backlog rows, two host test envs. Both are nRF52 only (RAK4631 with
RAK13800/W5100S) and both have the same code upstream.

## Evidence checklist

- Commits cited are ancestors of `fork-dev` HEAD (`git merge-base --is-ancestor`, all rc=0):
  `cf66ad18` and `29c09ac8` (NTP table and own-NTP literal), `3cdd04ed` (v4.35g, `--setowndns`),
  `82627dcf` (`HAS_NRF_ETH` per env), `ce05f38c`, `e8188e3c`, `8b4d509b` (APPLINK-01, ETH-06, bench
  hook). `git log --grep=revert` over `src/nrf52/nrf_eth.cpp` and `src/command_functions.cpp` shows
  `12e35ad0` (fork equals upstream again) and `72b6b1bf`; neither touches the DNS block, the NTP
  call or the netmode print, and `cf66ad18` is still in the tree (the `node_ownntp` argument is at
  `nrf_eth.cpp:1005`).
- Commits since the pin that touched `src/nrf52/nrf_eth.cpp`: `e8188e3c` (ETH-06 main-loop hang
  after a W5100S reset), `ce05f38c` (AppLink), `8b4d509b` (`--ethdrop` hook). `git show` of each,
  filtered for `dns` and `ntp`: only `ce05f38c` adds a line, `#include "ntp_async.h"`. None changed
  the DNS blocks. `git log 96aaa67c72..HEAD -- src/nrf52/nrf_eth.cpp src/nrf52/nrf_eth.h` is empty
  (those three commits are older than the pin); no commit in that range touches `node_netmode` or
  `node_owndns` in `src/`.
- Every path:line below was read at HEAD `067d29cd`. Upstream lines are `upstream/dev` at
  `5698913a` (2026-11-21).
- Platform claims are per platform in each section (nRF52, ESP32 classic and S3, ESP32 boards with
  Ethernet).

## Ticket P-0154: own DNS ignored on a static IP

### Symptom

On a RAK4631 with LAN board and a static IP, `--setowndns <ip>` is accepted, stored and triggers
the auto reboot, but the node keeps using `44.143.0.10` (own IP starts with "44") or `8.8.8.8`.
`--info` (`...DNS address`), the web info page and the web setup field show the hard-coded value.
A Hamnet node cannot be pointed at another resolver (DD9QP, 7752; DG3FBL, 328 point 3).

### Which cases are still open

| Case                                          | Verdict now  | Basis at HEAD                                                                                                                                            |
| --------------------------------------------- | ------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1. Own DNS ignored on a static IP             | open         | this plan                                                                                                                                                |
| 2. Own NTP given as an IPv4 literal           | fixed-code   | `cf66ad18`, `29c09ac8` (ancestors); `nrf_eth.cpp:1004-1005` passes `node_ownntp` to `gwResolveNode()`; tests exist                                       |
| 3. Own NTP given as a host name               | by design    | `false /* nRF52 has no DNS resolver */` (`nrf_eth.cpp:1005`); the debug line says "(own NTP ignored)" (`nrf_eth.cpp:1025`, web `web_functions.cpp:1794`) |
| 4. NTP not synced on v4.35k (4651, no config) | undetermined | not shown to be this defect; receive stop in the same report is P-0009                                                                                   |

Cases 2 and 3 need no work. The plan covers case 1 only.

### Root cause (HEAD)

- `NrfETH::initethfixIP()` (`src/nrf52/nrf_eth.cpp:273`) writes `node_dns` from a fixed rule at
  `:285-289` (`memcmp(node_ownip, "44", 2) == 0` gives `44.143.0.10`, else `8.8.8.8`), then
  `dns.fromString(node_dns)` at `:299` and `Ethernet.begin(macaddr, ip, dns, gw, subnet)` at `:307`.
- `nrfStaticReapplyNoReset()` (`:656`, the ETH-05 static re-apply) is a second copy of the same
  rule at `:659-663`, `dns.fromString` at `:671`, `Ethernet.begin` at `:674`.
- `node_owndns` is not read anywhere in `src/nrf52/`; the only hits are the flash sanitiser
  (`nrf52_flash.cpp:444`) and the settings copies in `ble_settings_v1.*`. Grep:
  `grep -n "node_owndns" src/nrf52/*.cpp src/nrf52/*.h`.
- The DHCP path overwrites `node_dns` with the lease value (`nrf_eth.cpp:941`) and is correct as is.
- `--setowndns` (`command_functions.cpp:5009-5026`) stores into `node_owndns` (`char[20]`,
  `meshcom_settings.h:159`) and calls `cmdOwnNetRebootCheck()` (`:1065`), so the value is applied at
  the next boot, which is when the DNS is read. No commit since the pin changed any of this.
- Effect: on nRF52 there is no resolver in the firmware (`src/` has no DNS client outside ESP32,
  `extudp_functions.cpp:122` is `lwip`), and every server and NTP address is a literal from the
  table. The wrong value therefore changes no packet today; it is what the node reports and what it
  hands to the W5100S library. That is why the defect is cosmetic-plus (wrong display, silently
  ignored command, no way to prepare a resolver) and why 7752 could not be satisfied.

### Platforms

| Platform                                    | Own DNS honoured? | Evidence                                                                              |
| ------------------------------------------- | ----------------- | ------------------------------------------------------------------------------------- |
| nRF52 RAK4631 + RAK13800 (static IP)        | no (this defect)  | `nrf_eth.cpp:285-289`, `:659-663`                                                     |
| nRF52 RAK4631 (DHCP)                        | not applicable    | lease DNS (`nrf_eth.cpp:941`); ESP32 DHCP ignores it as well (`esp32_eth.cpp:90,96`)  |
| nRF52 T114, T-Echo (`HAS_NRF_ETH=0`)        | no network        | `--setowndns` answers "no ethernet in this build" (`command_functions.cpp:4979-4989`) |
| ESP32 WiFi, static IP                       | yes               | `udp_functions.cpp:1257-1260` and `:1271-1274` (length >= 7, else 8.8.8.8)            |
| ESP32 Ethernet (T-ETH-Elite, T-Connect-Pro) | yes               | `esp32_eth.cpp:45,54` (see "Other bugs": no length guard there)                       |

### Fix design (minimal)

1. New header-only helper `src/eth_dns.h`, no Arduino types, only `<cstring>` and the existing
   `extudp_parse_ipv4()` from `src/extudp_target.h` (strict dotted quad, already header-only and
   used by `extudp_functions.cpp` and `command_functions.cpp`; do not write a second parser):

   ```cpp
   // Static-IP DNS of the nRF52 W5100S path. Own DNS when it is an IPv4 literal (nRF52 has no
   // resolver, a host name or a short string is unusable), else the old rule: Hamnet resolver for
   // an own IP starting with "44", else 8.8.8.8.
   inline const char *ethStaticDns(const char *ownDns, const char *ownIp);
   ```

   Returned pointer is `ownDns` itself or a string literal, so callers `snprintf("%s", ...)` it.
   The `"44"` prefix test is kept byte for byte (`memcmp(ownIp, "44", 2)`); no behaviour change for
   nodes without own DNS.

2. `nrf_eth.cpp:285-289` and `:659-663` become one call each:
   `snprintf(node_dns, sizeof(node_dns), "%s", ethStaticDns(node_owndns, node_ownip));`. Everything
   after (`dns.fromString`, `Ethernet.begin`) stays. Include the header next to the other includes.
3. No change to `--setowndns`, the DHCP path, ESP32, the web pages or the settings layout. DHCP plus
   own DNS stays ignored on both platforms (that matches ESP32 and `--info`, which prints OWNDNS only
   for a static configuration, `command_functions.cpp:8523-8529`).
4. Rejecting `--setowndns` on nRF52 (the other option in the problem file) is not recommended: the
   value is shared through config import, BLE settings and the web setup row, so a reject would
   break those flows for a no-op gain. Honouring it costs one helper. Operator confirmation is in
   the open questions.

### Test (fails before, passes after)

- Unity env `native_eth_dns` (new, modelled on `native_netif_mtu`, `platformio.ini:839`:
  `test_filter = test_eth_dns`, `-I src`, `test_build_src = no`), file
  `test/test_eth_dns/test_main.cpp`. Cases: own DNS `10.0.0.53` on own IP `44.1.2.3` returns
  `10.0.0.53`; same on `192.168.1.5`; empty own DNS with `44.1.2.3` returns `44.143.0.10`; empty
  with `192.168.1.5` returns `8.8.8.8`; empty with an empty own IP returns `8.8.8.8`; `nullptr`
  inputs fall back; unusable own DNS falls back (`1.1.1`, `dns.example.org`, `300.1.1.1`,
  `8.8.8.8 ` with trailing blank). Before the fix the header does not exist, so this suite does not
  compile; it pins the rule, not the wiring.
- The wiring is not host-testable (`nrf_eth.cpp` needs the W5100S stack), so the seam is a
  structural pin in the style of `tools/tests/test_mesh_policy_sites.py`:
  `tools/tests/test_nrf_eth_dns_sites.py` reads `src/nrf52/nrf_eth.cpp` (root overridable by an
  environment variable for the mutation proof) and asserts that both static blocks call
  `ethStaticDns(` and that the literals `"8.8.8.8"` and `"44.143.0.10"` no longer appear in that
  file. Today it fails: the literals are at `:287`, `:289`, `:661`, `:663` and the call count is 0.
  After the fix it passes. Run: `uv run pytest tools/tests/test_nrf_eth_dns_sites.py`.
- Registration: a row in `docs/test-suite-map.md` and the env in `platformio.ini` (the host gate
  `tools/regression.sh --stage 1,2` picks up `native*` envs and `tools/tests`).
- Env to run, one `pio` process at a time: `pio test -e native_eth_dns`.

### Bench (DK5EN-90, RAK4631 + RAK13800)

Load `/bench` first; `tools/bench/identity_guard.py`; `lsof <port>`; RAK needs DTR. Own call sign
only (DK5EN-90), TEST group only, TX power at most 2 dBm; no radio traffic is needed for this
check. DK5EN-98 is production and is never touched. Preconditions: gateway or web server on
(`--info` prints the DNS line only with `bGATEWAY || bWEBSERVER`, `command_functions.cpp:8558`),
settings exported before the change.

1. Old build, `--setownip`, `--setownms`, `--setowngw` with bench LAN values (reboot after the
   third), then `--setowndns <bench resolver>` (reboot). `--info` shows `...DNS address  : 8.8.8.8`
   (own IP is not 44.x). This is the failing observation.
2. New build, same sequence. Expected: `...DNS address  : <bench resolver>` and the boot line
   `DNS address: <bench resolver>`; the web info page shows the same.
3. `--setowndns ` with an empty argument (open question 1 of the problem file: whether an empty
   argument survives the parser) or an invalid value: expected fallback `8.8.8.8`.
4. Restore DHCP (clear the three own addresses) and the exported settings; check `--info` shows the
   lease DNS. Whether the empty arguments work is exactly open question 1; if not, restore from the
   export and file it.

### Upstream

Yes. `upstream/dev:src/nrf52/nrf_eth.cpp` has the same blocks (`:274-276`, `:286`, `:644-646`,
`:654`) and `node_owndns` is not read in `src/nrf52/` there either; `esp32_eth.cpp:45,54` honours it
upstream; `upstream/dev` has `src/extudp_target.h`. Upstream PR cut from `upstream/dev` (German
body), carrying `eth_dns.h`, the two call sites and the Unity test; the pytest pin and the
`platformio.ini` env follow whatever the PR carries for tests.

### Risks

- A node whose flash holds an old `node_owndns` typed while the command was ignored switches to
  that value after the update. Functional impact is nil today (no resolver on nRF52); it only
  changes what `--info` shows.
- Truncation: the command accepts a longer string but the field is 20 bytes; a truncated value
  fails the literal check and falls back, which is the safe direction.
- The strict literal check is stricter than ESP32 (`>= 7` only). Intentional, documented in the
  helper comment; ESP32 sites are not touched in this change.
- No LoRa byte budget, no settings layout, no new flash key, no new command text.

## Ticket P-0163: `--info` shows "NETWORK Mode:WiFi" on a RAK on LAN

### Symptom

On a RAK4631 with LAN board `--info` prints `...NETWORK Mode:WiFi` although the node is on Ethernet.
Cosmetic; no behaviour depends on it.

### Root cause (HEAD)

- `src/command_functions.cpp:8533-8538`: `printdeb("...NETWORK Mode:")`, then `WiFi` when
  `node_netmode == 0`, `ETH` when `== 1`. Nothing reads the actual interface.
- `node_netmode` defaults to 0 (`src/meshcom_settings.h:167`). The only command setter,
  `netmode wifi|eth` (`command_functions.cpp:4871`, `:4884`), sits under `#if defined(HAS_ETHERNET)`
  (`:4867`), which only `variants/LilyGo_T_Connect_Pro/configuration.h:18` and
  `variants/T-ETH-ELITE_1262/configuration.h:10` define. Other writers: settings import
  (`config_json.h:351`, range 0..1) and the BLE settings copy (`ble_settings_v1.cpp:176,381`);
  nothing in `src/nrf52/` sets it. This answers the open question of the problem file: no other
  path sets it on nRF52.
- The RAK4631 sets `-D HAS_NRF_ETH=1` (`variants/wiscore_rak4631/platformio.ini:38`); T114 and
  T-Echo set `=0` (`variants/heltec_t114/platformio.ini:87`, `variants/t_echo/platformio.ini:51`).
  `command_functions.cpp:97-102` already includes `nrf_eth.h` for `NRF52_SERIES`, and `nrf_eth.h:10`
  makes an unset `HAS_NRF_ETH` a hard `#error`, so the macro is always defined there.
- No commit since the pin changed this (`git log -S'node_netmode' 96aaa67c72..HEAD -- src` is
  empty).
- Correction to the problem file: the web info "Netmode" row (`web_functions.cpp:4280`) is inside
  `#if defined(HAS_ETHERNET)` (`:4279`) and is not compiled for the RAK, so only `--info` is wrong
  there. The other web readers of `node_netmode` (`web_functions.cpp:387,425,522`) are
  `HAS_ETHERNET` blocks as well, so none runs on the RAK.

### Platforms

| Platform                                         | Print today                                                                 | Right?                                  |
| ------------------------------------------------ | --------------------------------------------------------------------------- | --------------------------------------- |
| nRF52 RAK4631 + RAK13800                         | `WiFi` (netmode 0)                                                          | no, uplink is Ethernet                  |
| nRF52 T114 (`HAS_NRF_ETH=0`)                     | `WiFi`                                                                      | no, no IP network at all                |
| nRF52 T-Echo                                     | no NETWORK line (`#ifndef BOARD_T_ECHO`, `command_functions.cpp:8483-8601`) | not affected                            |
| ESP32 without Ethernet (Heltec, T-Beam, T-Deck)  | `WiFi` (0)                                                                  | yes                                     |
| ESP32 with Ethernet (T-ETH-Elite, T-Connect-Pro) | `WiFi` or `ETH` from `netmode eth`                                          | yes, the stored switch is the real mode |

### Fix design (minimal)

1. New header-only helper `src/netmode_label.h` (no Arduino types):

   ```cpp
   inline const char *infoNetModeLabel(int netmode, bool nrf52, bool nrfEth);
   ```

   `nrf52 && nrfEth` returns `"ETH"`; `nrf52 && !nrfEth` returns `"none"`; otherwise `netmode == 1`
   returns `"ETH"` and everything else `"WiFi"` (netmode is range-checked 0..1 at
   `config_json.h:351`, so no third value exists; the old code printed nothing for one).

2. `command_functions.cpp:8533-8538` becomes
   `printlndeb(infoNetModeLabel(meshcom_settings.node_netmode, NRF52_SERIES?, HAS_NRF_ETH?))`, the two
   flags as compile-time constants from the existing macros (`#if defined(NRF52_SERIES)` with
   `HAS_NRF_ETH`), no new macro. The following `hasIpAddress` and IP lines stay.
3. `node_netmode` is not touched: setting it on nRF52 would change the BLE/config export and the
   web setup row for no gain.
4. Optional, not required: refresh `tools/bench/testdata/identity/rak_ok_info.txt` (it holds a
   captured `NETWORK Mode:WiFi` of a RAK). No parser reads that line
   (`grep -n "NETWORK" tools/bench/identity_guard.py` is empty), so leaving it is safe.

### Test (fails before, passes after)

- Unity env `native_netmode_label` (new, same shape as `native_eth_dns`), file
  `test/test_netmode_label/test_main.cpp`. Cases: `(0, false, false)` `WiFi`; `(1, false, false)`
  `ETH`; `(0, true, true)` `ETH` (the reported RAK case, netmode 0); `(1, true, true)` `ETH`;
  `(0, true, false)` and `(1, true, false)` `none`; `(5, false, false)` `WiFi`.
- The call site is not host-testable (`command_functions.cpp` is compiled by no native env). Seam:
  `tools/tests/test_info_netmode_site.py` reads the `NETWORK Mode:` block and asserts it calls
  `infoNetModeLabel(` and no longer contains a `node_netmode == 0` branch printing `"WiFi"`. Today
  it fails (the block at `:8533-8538` has that branch and no call); after the fix it passes. Run:
  `uv run pytest tools/tests/test_info_netmode_site.py`.
- Env to run: `pio test -e native_netmode_label`. Register both in `docs/test-suite-map.md`.

### Bench (DK5EN-90)

Same rules as above (no radio traffic needed; own call sign, TEST group only, at most 2 dBm,
DK5EN-98 never). Old build, `--info`: `...NETWORK Mode:WiFi`. New build, `--info`:
`...NETWORK Mode:ETH`, with `...hasIpAddress: yes` and the IP block unchanged. Negative control on
an ESP32 WiFi bench node (DK5EN-1): `--info` still prints `WiFi`. T114 `none` is covered
by the host test only unless such a board is on the desk.

### Upstream

Yes, same defect: `upstream/dev:src/command_functions.cpp:8047-8053` has the identical print and
`:4578` the `HAS_ETHERNET` setter. The upstream PR cannot use `HAS_NRF_ETH` (it is fork-only,
`82627dcf`; `git show upstream/dev:src/command_functions.cpp | grep -c HAS_NRF_ETH` is 0), so the
upstream form is `nrf52 = defined(NRF52_SERIES)` with `nrfEth = true`, printing `ETH` for every
nRF52 target; upstream still builds `heltec_t114` and `t_echo`, which would then print `ETH`
without hardware, so the PR needs the maintainers' view on that or stays at the `none` fork form.
Treat as upstream-able with a variant; the fork carries the full version first.

### Risks

- Anything that parses `--info` text: none in the repo (see above); apps reading the console line
  are not known. The word `WiFi` changes to `ETH` only on RAK.
- `none` on the T114 is a new word on a line that never said it; if the operator prefers to
  keep `WiFi` there, drop the branch (one case in the helper and the test).
- Display only; no flash, command, protocol or LoRa change.

## Backlog rows (Implement table, `docs/BACKLOG.md` section 3.2; not edited here)

Ids checked: `grep -rn "RAK-ETH-DNS-01\|RAK-NETMODE-01" docs/BACKLOG.md docs/architecture/08-defect-catalogue.md docs/archive/`
is empty (2026-11-21).

| Id             | Area             | Item                                                                                                                                                                                                                                                                                                                                                                                                         | Status                                          | Next step                                                                                                          |
| -------------- | ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| RAK-ETH-DNS-01 | Ethernet / nRF52 | RAK4631 static IP ignores `--setowndns`: `initethfixIP()` and `nrfStaticReapplyNoReset()` hard-code `44.143.0.10` or `8.8.8.8` and never read `node_owndns`; `--info` and web show the fixed value. ESP32 honours it. Own NTP cases (literal fixed by `cf66ad18`, host name by design) need nothing. Plan [plan-rak-eth-dns-netmode-20261121.md](plan-rak-eth-dns-netmode-20261121.md). Problem file P-0154. | Open 2026-11-21: plan written, not implemented. | Implement `src/eth_dns.h`, `native_eth_dns`, site pin; bench DK5EN-90 static IP; upstream PR.                      |
| RAK-NETMODE-01 | Ethernet / nRF52 | `--info` prints `NETWORK Mode:WiFi` on a RAK4631 on LAN (and on the T114; the T-Echo prints no NETWORK line): the line reads `node_netmode`, which nothing sets on nRF52. Print the uplink from `HAS_NRF_ETH` through a pure helper. Plan [plan-rak-eth-dns-netmode-20261121.md](plan-rak-eth-dns-netmode-20261121.md). Problem file P-0163.                                                                 | Open 2026-11-21: plan written, not implemented. | Implement `src/netmode_label.h`, `native_netmode_label`, site pin; bench DK5EN-90 `--info`; upstream PR (variant). |

## Open questions for the operator

1. P-0154: honour `--setowndns` on nRF52 (recommended, plan above) or reject it there? Rejecting
   breaks config import and web setup symmetry.
2. P-0163: print `none` on the T114 (recommended) or leave `WiFi`? (The T-Echo never prints the line.)
3. P-0154 DHCP way back from a static IP (open question 1 of the problem file) is verified only
   on the bench, step 3 and 4; no code change is planned for it.

## Other bugs seen (not fixed, not in this plan)

- `src/esp32/esp32_eth.cpp:45,54`: the static branch uses `node_owndns` unguarded; an empty value
  gives DNS `0.0.0.0` and an empty `node_dns`, while the WiFi path falls back to `8.8.8.8` below 7
  characters (`udp_functions.cpp:1257`). The static branch is also selected by `node_ownip` alone,
  not by the ip/mask/gateway triple used everywhere else.
- `src/web_functions/web_functions.cpp:3538`: the web setup "DNS" field is prefilled from `node_dns`
  (the active value), not `node_owndns`; on a DHCP node it shows the lease DNS as if it were the
  own setting.
- `src/gw_servers.cpp:71` keeps a private `parseIpv4()` next to `extudp_parse_ipv4()`
  (`src/extudp_target.h:30`); two strict IPv4 parsers.
- Problem file P-0163 cites `web_functions.cpp:4288` as another surface for the same defect; that
  row is `HAS_ETHERNET`-only and absent on the RAK (see the correction above).

## Operator decisions 2026-11-21

- P-0154: nRF52 honours `--setowndns` like ESP32.
- P-0163: the T114 prints `none`.
