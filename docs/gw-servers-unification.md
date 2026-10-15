# Gateway server selection: one central table

Status: concept, revised after an independent review (2026-10-15), nothing implemented. Decisions in section 7; one decision (DL on the Hamnet) is open with a recommended default. Baseline: `fork-dev` at `8d232bde` (upstream/dev merged up to
`f2b3a349`, #1212).

## BLUF

The choice of the MeshCom server (and the time server that belongs to it) is hand-coded three times:
`startMeshComUDP()` (ESP32), `NrfETH::startUDP()` (nRF52, DHCP) and `NrfETH::startFIXUDP()` (nRF52,
static IP). Each path has its own `if/else` chain, its own Hamnet test and its own NTP rule, and they
have drifted apart (section 2). Upstream #1209 lost the OE default in one of them; #1210-#1212 fixed
ESP32 and extended nRF52 but not the static path.

Proposal: one table `GW_SERVERS[]` in `src/gw_servers.{h,cpp}`, one network decision function, one
resolver. Every path calls the resolver and only does the transport (DNS lookup on ESP32, literal IP on
nRF52). A new country is one row. France and USA are already in the table as commented-out rows (no server addresses known yet).

Three PRs against upstream `dev`, in this order: (1) the table, behavior-neutral except the deltas listed in 5.1,
(2) the `--setinet auto` mode, (3) enable the FR and US rows once the operators name their hosts.

## 1. Current behavior (matrix)

`node_gwsrv` is two characters: `OE` (factory default and fallback), `DL`, `IT`, `HC`, `HB`. `HC` is
Hamcloud: reachable from the Hamnet only. Its Internet entry is a compromise: it uses the DL Internet
server, so DL is "Internet" and HC is "Hamnet" for the same operator.

**Sysadmin requirement (relayed by Kurt, 2026-10-15): the German server (DL) is to be reachable over the
Internet only.** Hamnet access to the German operator's server is HC's job. The code does not follow
this yet: all three paths below still give DL a Hamnet target (`meshcom.hamnet.cloud` /
`44.148.230.197`), which is the same host HC uses. This is finding D9 in section 2 and is resolved by
the `GWS_INET_ONLY` flag on DL in section 3.

`node_hamnet_only` today: `--sethamnet on` / `--setinet off` set 1, `--sethamnet off` / `--setinet on`
set 0.

### 1.1 When is the Hamnet side taken

| Path                       | Condition for "Hamnet"                                             | Effect                                                                                 |
| -------------------------- | ------------------------------------------------------------------ | -------------------------------------------------------------------------------------- |
| ESP32 `startMeshComUDP`    | `(node_ip[0]==44 && hamnet_only==1) \|\| hamnet_only==1`           | The 44er term is dead: only `hamnet_only==1` counts. A 44er address alone gives Inet.  |
| nRF52 dynamic `startUDP`   | `(local_addr[0]==44 && hamnet_only==1) \|\| hamnet_only==1`        | Same as ESP32 since #1212.                                                             |
| nRF52 static `startFIXUDP` | `node_ip` string starts with "44" **or** `hamnet_only` is non-zero | A static 44er address always goes to the Hamnet. `--setinet on` cannot force the Inet. |
| nRF52 dynamic, no address  | `local_addr[0]==0`                                                 | No server, no NTP (`btimeClient=false`).                                               |
| ESP32, no address / AP     | `node_ip == 0.0.0.0` or `bWIFIAP`                                  | No server selection.                                                                   |

### 1.2 ESP32 (`src/udp_functions.cpp`, `startMeshComUDP`)

Targets are host names (async lwIP lookup with cache, retried from the heartbeat after a failure; no literal fallback) or a literal IP. `--gateway srv` accepts OE, DL, IT,
HC, HB.

| `node_gwsrv` | Hamnet server             | Internet server        |
| ------------ | ------------------------- | ---------------------- |
| OE (default) | 44.143.8.143              | meshcom.oevsv.at       |
| DL           | meshcom.hamnet.cloud      | meshcom.hamnet.network |
| IT           | meshcom.dig-italia.it (*) | meshcom.dig-italia.it  |
| HC           | 44.148.230.197            | meshcom.hamnet.network |
| HB           | 44.142.42.59              | meshcom.emcomm.ch      |

(*) Italy has no Hamnet server: the Internet server is used even when Hamnet is forced
(`srv_path = "inet"`).

Time server (independent of `node_gwsrv`):

| Case                                                                                                                                   | NTP                                                   |
| -------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------- |
| `node_ntp` set (>= 7 chars, own NTP; **filled only when the node has a static IP**, from `node_ownntp`, `udp_functions.cpp:1044-1047`) | `node_ntp`, both networks. A DHCP node never gets it. |
| Hamnet, `node_ip[1] == 143`                                                                                                            | 44.143.0.9                                            |
| Hamnet, `node_ip[1] == 148`                                                                                                            | 44.148.224.123                                        |
| Hamnet, any other second octet                                                                                                         | 44.143.0.9                                            |
| Internet                                                                                                                               | `pool.ntp.org` (DNS)                                  |

### 1.3 nRF52 dynamic (`src/nrf52/nrf_eth.cpp`, `startUDP`)

No DNS resolver: every target is a literal IP. The NTP address is chosen **per server**, not per own
address. `node_ntp` is not read anywhere in `nrf_eth.cpp`.

| `node_gwsrv` | Hamnet server / NTP                   | Internet server / NTP          |
| ------------ | ------------------------------------- | ------------------------------ |
| OE (default) | 44.143.8.143 / 44.143.0.9             | 89.185.97.38 / 162.159.200.1   |
| DL           | 44.148.230.197 / 44.148.224.123       | 192.68.17.26 / 162.159.200.1   |
| IT           | 145.239.75.155 (inet) / 162.159.200.1 | 145.239.75.155 / 162.159.200.1 |
| HC           | 44.148.230.197 / 44.148.224.123       | 192.68.17.26 / 162.159.200.1   |
| HB           | 44.142.42.59 / 44.142.163.2           | 157.143.4.44 / 162.159.200.1   |

### 1.4 nRF52 static IP (`startFIXUDP`)

Only OE, DL and IT exist. HC and HB fall into the OE default. The Internet branch does not set
`srv_path` (it keeps the initial `"inet"`). A static node can leave the static path after a recovery; that is a defect (ETH-05) and is fixed together with the table (section 3.7).

| `node_gwsrv` | Hamnet server / NTP                   | Internet server / NTP          |
| ------------ | ------------------------------------- | ------------------------------ |
| OE (default) | 44.143.8.143 / 44.143.0.9             | 89.185.97.38 / 162.159.200.1   |
| DL           | 44.148.230.197 / 44.148.224.123       | 192.68.17.26 / 162.159.200.1   |
| IT           | 145.239.75.155 (inet) / 162.159.200.1 | 145.239.75.155 / 162.159.200.1 |
| HC           | like OE (missing)                     | like OE (missing)              |
| HB           | like OE (missing)                     | like OE (missing)              |

## 2. Where the three paths disagree

| #   | Finding                                                                                                                                                                                                                                                                                              | Consequence                                                                                                                                                                      |
| --- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| D1  | nRF52 static has no HC and no HB (1.4).                                                                                                                                                                                                                                                              | An HB or HC node with a fixed IP silently uses the OE server.                                                                                                                    |
| D2  | nRF52 static sends every 44er address to the Hamnet, whatever `hamnet_only` says (1.1).                                                                                                                                                                                                              | A static 44er node cannot choose the Internet. Dynamic and ESP32 can.                                                                                                            |
| D3  | The 44er term in the ESP32 and nRF52 dynamic conditions is dead (`&& ==1` inside `\|\| ==1`).                                                                                                                                                                                                        | The code reads as if a 44er address mattered. It does not.                                                                                                                       |
| D4  | Hamnet NTP: ESP32 picks it by the node's own second octet, nRF52 by the server (HB gets 44.142.163.2 only on nRF52).                                                                                                                                                                                 | The same node/server pair gets different time servers per platform.                                                                                                              |
| D5  | Own NTP: ESP32 applies `node_ownntp` with a static IP only (DHCP never); nRF52 never reads it. `meshcom_settings.h:41-44` records the operator decision of 2026-09-13 that `node_ntp` stays ESP32-only.                                                                                              | The same setting gives different time servers per platform.                                                                                                                      |
| D6  | IT in forced-Hamnet: nRF52 uses the Internet NTP, ESP32 a Hamnet NTP, both with the Internet server.                                                                                                                                                                                                 | ESP32 asks a Hamnet NTP from a node that may have no Hamnet route.                                                                                                               |
| D7  | nRF52 uses fixed IPs for every row (no resolver). For HB the name `meshcom.emcomm.ch` is a CNAME of a dyndns name (checked with `dig`), so the literal 157.143.4.44 can go stale; the split DNS (different answer inside and outside the Hamnet) is the operator's statement, not visible in `src/`. | A server move needs a firmware release on nRF52.                                                                                                                                 |
| D8  | The valid-code list lives in `command_functions.cpp` (check, error text, help text) apart from the chains.                                                                                                                                                                                           | Adding a code means editing at least five places. The help text still says `OE/DL/IT`.                                                                                           |
| D9  | DL has a Hamnet target in all three paths (44.148.230.197 / `meshcom.hamnet.cloud`), although the sysadmin wants DL reachable over the Internet only.                                                                                                                                                | A DL node with Hamnet forced reaches the Hamnet-only server meant for HC. The nRF52 dynamic path got this from commit `8d232bde` (decided before the sysadmin's wish was known). |

## 3. Design

### 3.1 The table and its flags

The routing information lives **per network side**, not per row. Every row has two sides, Internet and
Hamnet, and each side has exactly one mode. These modes are the "flags" of the table.

```c
// src/gw_servers.h -- pure C++, no Arduino types, builds in the native Unity envs.
enum GwNet  : uint8_t { GW_NET_INET = 0, GW_NET_HAMNET = 1 };
enum GwMode : uint8_t {
    GWM_DIRECT,      // this side has its own server and NTP endpoints
    GWM_OTHER_NET,   // use the OTHER side of the same row; path reports the network really used
    GWM_VIA          // use the same-network side of another row (code in `via`)
};

struct GwEndpoint {
    const char *host;    // DNS name or nullptr. Used where a resolver exists (ESP32).
    uint8_t     ip[4];   // literal. Used where none exists (nRF52). Never a DNS-failure fallback.
};

struct GwSide {
    GwMode      mode;
    char        via[3];  // GWM_VIA only
    GwEndpoint  srv;     // GWM_DIRECT only
    GwEndpoint  ntp;     // GWM_DIRECT only
};

struct GwServer {
    char    code[3];     // "OE", "DL", ...   (node_gwsrv is char[3])
    const char *label;   // "MeshCom OE (Austria)", for the web page and --help
    GwSide  side[2];     // [GW_NET_INET], [GW_NET_HAMNET]
};
```

One array `GW_SERVERS[]` in flash. Row 0 is OE and the default for every unknown or empty code. The
table is the only place with server names, IPs and NTP addresses.

**The three modes:**

| Mode            | Meaning                                                                    | Used by                              |
| --------------- | -------------------------------------------------------------------------- | ------------------------------------ |
| `GWM_DIRECT`    | The side has its own server and NTP endpoints.                             | Every real server side.              |
| `GWM_OTHER_NET` | The side has no server of its own: use the row's other side, `path` = used | IT and later FR/US on the Hamnet.    |
| `GWM_VIA`       | The side is served by the same-network side of another row.                | HC on the Internet; DL on the Hamnet |

The rows:

| Code | Internet side                                      | Hamnet side                                                     |
| ---- | -------------------------------------------------- | --------------------------------------------------------------- |
| OE   | DIRECT `meshcom.oevsv.at` / 89.185.97.38           | DIRECT 44.143.8.143, NTP 44.143.0.9                             |
| DL   | DIRECT `meshcom.hamnet.network` / 192.68.17.26     | **VIA HC** (recommended default) or OTHER_NET (strict), see 3.6 |
| IT   | DIRECT `meshcom.dig-italia.it` / 145.239.75.155    | OTHER_NET (Italy has no Hamnet server)                          |
| HC   | **VIA DL** (HC is Hamnet only, compromise with DL) | DIRECT 44.148.230.197, NTP 44.148.224.123                       |
| HB   | DIRECT `meshcom.emcomm.ch` / 157.143.4.44          | DIRECT 44.142.42.59, NTP 44.142.163.2                           |
| FR   | _commented out, no addresses known_                | _OTHER_NET (assumed)_                                           |
| US   | _commented out, no addresses known_                | _OTHER_NET (assumed)_                                           |

Every Internet NTP is `{"pool.ntp.org", 162.159.200.1}`: ESP32 keeps the DNS name it uses today, nRF52
the literal it uses today.

`DL` carries a comment in the table: _"Internet server by wish of the German sysadmin (relayed by Kurt,
2026-10-15). Hamnet users select HC."_ The same note sits in the code comments of the three legacy `DL`
branches until the table replaces them (section 5), so the information survives even if PR 1 is delayed.

### 3.2 Resolver

```c
GwNet           gwSelectNet(uint8_t mode, const uint8_t ownIp[4]);   // section 4
const GwServer *gwFind(const char *code);                            // unknown/empty -> row 0
struct GwTarget { const char *srv_host; uint8_t srv_ip[4];
                  const char *ntp_host; uint8_t ntp_ip[4];
                  const char *path; /* "inet" | "hamnet": the network really used (TM-39 [GW];srv) */ };
bool            gwResolve(const char *code, GwNet net, const char *ownNtp, bool hasResolver, GwTarget *out);
bool            gwResolveNode(const char *code, uint8_t mode, const uint8_t ownIp[4],
                              const char *ownNtp, bool hasResolver, GwTarget *out);  // = gwResolve(gwSelectNet(mode, ownIp), ...)
int             gwCodes(char out[][3], int max);    // validation, --help, web page, BLE list
```

Rules of `gwResolve`:

- At most **one** redirection step (`OTHER_NET` or `VIA`), and the target side must be `DIRECT`. A test
  enforces both, so cycles cannot exist.
- `path` is the network actually used: IT on a forced Hamnet reports `"inet"`; DL via HC on the Hamnet
  reports `"hamnet"`.
- `ownNtp` (the node's own NTP, section 3.3) replaces the NTP endpoint when it parses as an IPv4 address
  on a platform without a resolver, or as an address or host name on ESP32. An unusable value is ignored
  and logged; the table NTP is used.

Callers:

| Path          | What it does with `GwTarget`                                                                     |
| ------------- | ------------------------------------------------------------------------------------------------ |
| ESP32         | `wifiDnsStart(&s_dnsSrv, srv_host ?: literal)`; NTP likewise. `[GW];srv` log from `path`.        |
| nRF52 dynamic | `udp_dest_addr = IPAddress(srv_ip)`, `timeClient.setPoolServerIP(ntp_ip)`.                       |
| nRF52 static  | Same as dynamic. The static chain disappears, and with it D1.                                    |
| Web page      | `getEffectiveNtpServer` (`web_functions.cpp:1720-1734`) calls `gwResolve`: the fourth copy goes. |

The `--srvip` bench override, `gwLinkSetDest` and the `[GW];srv` line keep working: they take the
resolved `GwTarget`. The static nRF52 path gains `[GW];srv` and `--srvip`, which is harmless.

### 3.3 Time server rule

One rule for all paths, replacing D4 to D6:

1. **Own NTP** (`node_ownntp`, set with `--setownntp`) wins, on every platform and with DHCP as well as
   with a static IP (decided 2026-10-15). Today this holds for a static ESP32 only: `node_ntp` is filled
   only in the static-IP branch (`udp_functions.cpp:1044-1047`) and the nRF52 never reads it. After PR 1
   a DHCP node that has `node_ownntp` set starts to use it: a listed behavior change (5.1).
   `meshcom_settings.h:41-44` (decision of 2026-09-13) concerns the runtime field `node_ntp`, which stays
   ESP32-only; the nRF52 reads `node_ownntp`, which exists on both platforms.
2. Otherwise the NTP of the resolved side (per server, as the nRF52 does today).

On nRF52 the value must be an IPv4 literal (no resolver: `NtpAsync` takes an `IPAddress`). `--setownntp`
accepts free text today; a host name on nRF52 is ignored with a log line and the table NTP is used.

ESP32 Hamnet NTP changes (octet rule goes away). All of these are listed in 5.1:

| Case                                   | Today (ESP32)             | After                        |
| -------------------------------------- | ------------------------- | ---------------------------- |
| OE on a 44.148 node                    | 44.148.224.123            | 44.143.0.9                   |
| HC / DL on a 44.143 node               | 44.143.0.9                | 44.148.224.123               |
| HB on 44.143 / 44.148 / other node     | 44.143.0.9 / .148.224.123 | 44.142.163.2                 |
| Hamnet forced, own address not in 44/8 | 44.143.0.9                | the server's Hamnet NTP      |
| IT with Hamnet forced                  | Hamnet NTP by octet       | 162.159.200.1 (Internet NTP) |
| Internet, any server                   | `pool.ntp.org`            | `pool.ntp.org` (same)        |

All Hamnet NTP servers are reachable inside the Hamnet.

### 3.4 Expansion: France and USA

A country is a row; nothing else changes. `node_gwsrv` is `char[3]`, so the two-letter codes fit, and
the `s_ble_settings_v1` offsets are untouched (`node_gwsrv` at 1188, `node_hamnet_only` at 200).

Both rows are created in `GW_SERVERS[]` now, **commented out**, because no server address is known. No
host name or IP is invented:

```c
// { "FR", "MeshCom France",
//   { /*INET*/   { GWM_DIRECT, "", { /*srv*/ nullptr, {0,0,0,0} }, { /*ntp*/ "pool.ntp.org", {162,159,200,1} } },
//     /*HAMNET*/ { GWM_OTHER_NET, "", {}, {} } } },
// { "US", "MeshCom USA",  ... same shape ... },
```

To enable a row: the operator's server (host and/or IP), a reachable time server (default `pool.ntp.org` /
`162.159.200.1` like the other rows, or a national NTP), and whether a Hamnet-side server exists (then the
Hamnet side becomes `DIRECT`). `OTHER_NET` on the Hamnet is an assumption, not a fact.

What a new row costs: one entry. The validation message, `--help` text, web dropdown and BLE list come
from `gwCodes()`. `--gateway srv` accepts exactly `gwCodes()`; today `command_functions.cpp:2534` hard-codes
the list and would reject FR/US until PR 1 replaces it.

### 3.5 Tests

All native. Wiring (checked in `platformio.ini`): board envs need nothing (`+<*>`; safeboot excludes the
file). The native env needs `test/test_gw_servers/`, `test_gw_servers` in the `[env:native]` `test_filter`
and `+<gw_servers.cpp>` in its `build_src_filter` (precedent: `gw_link_status`).

**Unity (`test/test_gw_servers/`):**

- For every row and both networks: `gwResolve` returns a non-empty server and a non-empty NTP after at
  most one redirection step, and the redirection target is `DIRECT`; `path` matches the network used.
- `OTHER_NET` sides resolve to the Internet endpoints with `path = "inet"`.
- Unknown and empty code give OE; codes are unique and exactly two characters.
- `ownNtp` overrides the table NTP; a host name is ignored where no resolver exists; an unusable value
  falls back to the table.
- `gwSelectNet`: modes 0, 1, 2 and an unknown value (treated as auto, section 4) against 44er and other
  addresses.

**Golden test, not circular:** pytest cannot call `gwResolve`, and a golden matrix read off the same code
passes by construction. Instead:

1. Before PR 1 lands, a script executes the legacy chains (extracted from the pre-PR-1 commit) once over
   code x own IP x `hamnet_only` x platform (ESP32, nRF52 dynamic, nRF52 static) x own NTP, and commits the
   result as JSON (`test/golden/gw_servers_legacy.json`). It is never regenerated from new code.
2. A native Unity helper dumps `gwResolve` over the same grid as JSON.
3. A pytest diffs the two. `EXPECTED_DELTAS` lists every difference by name (5.1, with old and new
   values). The test fails on any unlisted difference **and** on any listed delta that no longer differs.

**Existing test `tools/tests/test_gwsrv_select.py`:** it pins source text and will go red when the chains
go. It is deleted in PR 1. Until then note that `test_nrf52_dl_hc_share_hamnet_server...` currently
requires D9 (DL on the Hamnet server) and must change in the same commit that moves DL, and that the
nRF52 static function is not scanned by it at all.

### 3.6 DL on the Hamnet: the open decision

Fact: `meshcom.hamnet.cloud` (DL on the Hamnet, ESP32 and nRF52 static, nRF52 dynamic since fork commit
`8d232bde`) resolves to `44.148.230.197`, the literal HC uses. `node_gwsrv` is never sent to the server, so
"DL on the Hamnet" and "HC" send the same packets to the same host. The wish of the German sysadmin is
second-hand (relayed by Kurt).

| Option              | DL Hamnet side | Effect                                                                                       |
| ------------------- | -------------- | -------------------------------------------------------------------------------------------- |
| (a) via HC, default | `VIA HC`       | Nothing changes on air. The table records DL = Internet server, HC = Hamnet access.          |
| (b) strict          | `OTHER_NET`    | A Hamnet-only DL node loses server and time. Needs a migration DL + Hamnet -> HC beforehand. |

Decided direction: (a) for PR 1 (it is what the operator described for the Hamnet side), and (b) only after the operator confirms the wish first-hand and a
migration exists. Switching later is one table cell plus the migration.

### 3.7 A static nRF52 node must stay static (ETH-05)

A node with `node_ownip`, `node_ownms` and `node_owngw` configured (all longer than 6 characters) is a
static node. The check is spelled out in five places today (`nrf52_main.cpp:1231`, `:2276`, `:2317`,
`gateway_service_nrf52.cpp:142`, `nrf_eth.cpp:641` `in.fixed_ip`),
and two recovery paths forget it:

| Trigger                                                             | Path                                           | Result for a static node                                                                       |
| ------------------------------------------------------------------- | ---------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| UDP TX error streak (`MAX_ERR_UDP_TX`, `udp_drain_nrf52.cpp:91-95`) | `hasIPaddress=false`, then `resetDHCP()`       | `resetDHCP` -> `startETH()` runs a real DHCP exchange; on success `startUDP()` (dynamic chain) |
| `--ethdrop` (`ethDrop()`, `nrf_eth.cpp:240`)                        | same two lines                                 | same                                                                                           |
| Gateway watchdog stage 2 (`gateway_service_nrf52.cpp:138-156`)      | checks the static case, calls `initethfixIP()` | correct: stays static                                                                          |

In `resetDHCP()` the rule "fixed IP has no lease" (`eth_recovery.h:56`, `FullDhcp` means "legacy path") is
only effective when `hasIPaddress` is still true, because then `startETH()` returns 0 at once. Both
triggers above clear it first, so `startETH()` does a DHCP exchange. Consequences: a static node takes a
leased address (or, if no DHCP server answers, ends up with no address until the gateway watchdog or a
reboot), and it selects its server with the dynamic chain instead of `startFIXUDP()`.

Fix, part of PR 1 (a separate commit, so it can be cherry-picked as a bug fix on its own):

1. One predicate, the free inline function `ethIsStaticConfigured()` in `nrf_eth.h`, replaces the five spellings.
2. `resetDHCP()` for a static node never starts a DHCP exchange. `eth_recovery::decideStatic()` chooses:
   **Reopen** (link up and the chip still holds the configured address: only the UDP socket is reopened, no
   hardware reset), **Reapply** (link up, address differs or the reopen got no UDP RX: `Ethernet.begin` with the
   static parameters, no hardware reset, same link wait as `startETH`), or **HwReset** (link down, or a stalled
   reopen with a matching address: the full `initethfixIP()` including the W5100S hardware reset). A hardware
   reset is allowed at most once per 5 minutes (`hwResetAllowed`, shared bookkeeping with the DHCP F2 reset);
   inside the gap HwReset degrades to Reapply and, if that fails, the call returns 1 without a reset. Log
   lines: `[ETH];event;reapply_static;mode;reopen|begin|hw_reset;...` (with `stalled` and `gap` fields). The
   boot path and the gateway watchdog stage 2 keep calling `initethfixIP()` unchanged.
3. The two UDP start calls go through one function `startServerLink()` which picks the path by
   `isStaticConfigured()`. Every other remaining difference between `startUDP` and `startFIXUDP`
   disappears with the table (section 3.2), because both call `gwResolve`.
4. Regression test, written and run before the fix: a host test of the decision (`eth_recovery`-style pure
   function: static + `hasIPaddress=false` -> `ReapplyStatic`, never `FullDhcp`). A bench case on the RAK
   with a static address, a forced `--ethdrop` and the check that `[ETH]` shows no DHCP exchange and the
   address is unchanged (extends the ETH-04 bench).

## 4. Network mode: `--setnet auto|inet|hamnet`

`node_hamnet_only` (JSON `node_honly`) becomes a four-valued **network mode**. The field stays an `int`
(offset 200 in the BLE v1 struct, no layout change). The encoding keeps every stored value meaning what it
meant, so **no flash migration is needed**:

| Value | Meaning                                                              | Typed                                                |
| ----- | -------------------------------------------------------------------- | ---------------------------------------------------- |
| 0     | not set / legacy: behaves exactly as before on each path (see below) | (default of nodes that never set it)                 |
| 1     | Hamnet forced                                                        | `--setnet hamnet`, `--sethamnet on`, `--setinet off` |
| 2     | auto: own address 44.x -> Hamnet, otherwise Internet                 | `--setnet auto`, `--setinet auto`                    |
| 3     | Internet forced (explicit)                                           | `--setnet inet`, `--setinet on`, `--sethamnet off`   |

Any other stored value (corrupt flash) is treated as auto. **Value 2 is the factory default**, set only for
nodes with no stored settings (see "Factory default" below).

Legacy meaning of 0, per path (this is what makes a migration unnecessary):

| Path                        | Value 0                                                   |
| --------------------------- | --------------------------------------------------------- |
| ESP32, nRF52 dynamic (DHCP) | Internet (as before)                                      |
| nRF52 static IP             | Hamnet when the configured address is 44.x, else Internet |

Value 3 is new and fixes finding D2: before, a static nRF52 node on a 44er address could not choose the
Internet at all (`--setinet on` wrote 0, which the static path read as Hamnet). The old aliases
`--setinet on` and `--sethamnet off` now write 3 instead of 0 for that reason.

`gwSelectNet(mode, ownIp)`: 1 -> Hamnet; 0 and 3 -> Internet; 2 or any other value -> Hamnet when
`ownIp[0] == 44`, otherwise Internet. The nRF52 static caller applies the legacy rule for value 0 itself
before calling (`mode = (honly == 0) ? (ownIp[0] == 44 ? 1 : 0) : honly`); ESP32 and nRF52 DHCP pass the
stored value.

### 4.1 One command instead of four spellings

Before, two commands with two polarities wrote the same field (`--sethamnet on` = `--setinet off` = Hamnet;
`--setinet on` = `--sethamnet off` = Internet). There is no contradictory state (one `int`, the last command
wins), but "both off" reads like "neither" and there was no spelling for auto.

| Typed                             | Stores | Notes                                             |
| --------------------------------- | ------ | ------------------------------------------------- |
| `--setnet auto`                   | 2      | new                                               |
| `--setnet inet`                   | 3      | new                                               |
| `--setnet hamnet`                 | 1      | new                                               |
| `--setnet`                        | -      | prints the mode and the network in use            |
| `--setnet <anything else>`        | -      | prints the usage line `--setnet auto/inet/hamnet` |
| `--setinet on`, `--sethamnet off` | 3      | deprecated aliases (explicit Internet)            |
| `--sethamnet on`, `--setinet off` | 1      | deprecated aliases                                |
| `--setinet auto`                  | 2      | alias for Kurt's wording                          |

The old spellings stay (BLE apps, scripts). `--help` shows `--setnet` and marks them deprecated. The info
line prints `NET MODE: <mode> (in use: <hamnet|inet>)`. `--gateway srv` validation and its help text come
from `gwCodes()`.

### 4.2 Factory default 2 for new or wiped nodes only

The struct initializer stays 0, so a node whose key is missing keeps its old behaviour:

- ESP32 (`esp32_flash.cpp`, before the load): if neither `node_honly` nor `node_call` is stored in NVS, the
  value is seeded to 2. Every installed node has `node_call`, so an OTA never changes it.
- nRF52 (`nrf52_flash.cpp`): `flash_reset()` writes 2 into the default settings. It runs only for real wipes
  (missing legacy file, bad marker or size, the explicit clear). A loaded keyed store or a legacy blob is
  never touched. A plain flash layout/version bump keeps the loaded mode (the RAM copy is written back); ESP32
  behaves the same.

Native tests pin all of this (`test_esp32_settings_nvs`, `test_nrf52_settings_paths`).

### 4.3 What a user sees, and what to know when downgrading

- Existing nodes: no change on update (0 keeps its legacy meaning on every path).
- `--setnet auto` is the way for a node to switch to automatic routing; new nodes start in auto.
- Downgrade: an older firmware that knows only the range 0..1 may clamp a stored 2 or 3 to 1 (the nRF52
  keyed store clamps on load: `settings_store.cpp`), which means Hamnet, on DHCP nodes too. An old nRF52
  static reads any non-zero value as Hamnet. Whether upstream's store clamps the same way is unverified.
  So `--setnet auto` and `--setnet inet` should not be set on a node that may be downgraded.
- Config export/import: `config_json.cpp` rejects the whole import when `node_honly` is out of range
  (now 0..3). A config exported with 2 or 3 cannot be restored on older firmware.
- The golden command corpus (`test/golden/corpus/commands`) was already stale against HEAD before this
  change; it is not regenerated here (a regeneration would mix 380 unrelated lines into this change).
  A separate regeneration is a follow-up.

## 5. Plan

| PR  | Content                                                                                                                                                                        | Behavior change         |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------- |
| 1   | `gw_servers.{h,cpp}`, three callers plus the web NTP display, `gwCodes()` for validation and help, native tests, legacy golden, ETH-05 fix (3.7) as its own commit.            | Only the deltas of 5.1. |
| 2   | `gwSelectNet` modes, `--setnet auto\|inet\|hamnet` (old spellings as aliases), info/help/`--gateway srv` from `gwCodes()`, config range 0..3, factory default 2 for new nodes. | Section 4.              |
| 3   | Enable the FR and US rows when the operators supply server and NTP.                                                                                                            | New codes only.         |

Gate per PR: build every nRF52 and ESP32 board env (nRF52 `wiscore_rak4631` flash is the tightest, 82.5 %
at baseline), compare flash deltas, `tools/regression.sh --stage 1,2`.

### 5.1 Delta list (before / after)

D7 (literal can go stale) and D8 (code list in two places) are not behavior changes. The behavior deltas
are below; the golden test (3.5) names every one of them and fails on anything unlisted. Existing stored
values 0 and 1 keep their behaviour on every path except as listed.

| #   | Path                    | Case                                      | Before                                  | After                                           |
| --- | ----------------------- | ----------------------------------------- | --------------------------------------- | ----------------------------------------------- |
| D1  | nRF52 static            | HC, HB (Hamnet and Internet)              | OE server (44.143.8.143 / 89.185.97.38) | the row's own server and NTP                    |
| D9  | all                     | DL on the Hamnet                          | 44.148.230.197 (name on ESP32)          | same host via HC; ESP32 name becomes literal    |
| D4  | ESP32                   | Hamnet NTP                                | by own second octet                     | by server, table in 3.3                         |
| D5  | nRF52 (static, dynamic) | node_ownntp                               | ignored                                 | used when it is an IPv4 literal                 |
| D5  | ESP32 DHCP              | node_ownntp                               | ignored (only filled with a static IP)  | used                                            |
| D6  | ESP32                   | IT with Hamnet forced, NTP                | Hamnet NTP by octet                     | `pool.ntp.org` (the Internet NTP)               |
| D3  | ESP32, nRF52 dynamic    | stored value 2 or any value > 3           | Internet (only `== 1` meant Hamnet)     | auto by own address                             |
| D3  | nRF52 static            | stored value 2 or any value > 3           | Hamnet (any non-zero)                   | auto by own address                             |
| D2  | all                     | value 3 (new), explicit Internet          | not expressible on a static 44er node   | Internet on every path                          |
| --  | all                     | stored 0 and 1                            | -                                       | unchanged (0 keeps its per-path legacy meaning) |
| --  | nRF52 static            | UDP TX error streak, `--ethdrop`          | leaves the static path (DHCP exchange)  | stays static (ETH-05, 3.7)                      |
| --  | nRF52 static            | `--srvip` bench override, `[GW];srv` line | not applied / not printed               | applied / printed (shared tail)                 |
| --  | all new/wiped nodes     | factory default of the mode               | 0                                       | 2 (auto)                                        |

## 6. Out of scope

- A DNS resolver on nRF52.
- Changing the server addresses themselves.
- A fallback to a literal IP when an ESP32 DNS lookup fails (today the KEEP is skipped and the heartbeat
  retries; the table does not change that).
- The nRF52 UDP recovery that can turn a static node into a DHCP node (1.4 note).

## 7. Decisions

Decided 2026-10-15:

1. Hamnet NTP is per server; the own NTP (`node_ownntp`) overrides everywhere, DHCP nodes included (3.3).
2. Auto is the factory default (4: value 2 for new or wiped nodes; stored values keep their meaning).
3. FR and US are created commented out; no addresses are known (3.4).
4. IT with Hamnet forced stays on the Internet server; Italy has no Hamnet server (`OTHER_NET`).
5. DL is the Internet server (`meshcom.hamnet.network` / 192.68.17.26) by wish of the German sysadmin
   (relayed by Kurt). DL on the Hamnet goes via HC, i.e. to the 44er address of the German Hamcloud: no
   change on air (3.6, option a). The idea of DL pointing to OE was the state before v4.35v (Appendix A)
   and is withdrawn.
6. No flash migration: the mode keeps 0 = legacy per path and gets new values 2 (auto) and 3 (Internet forced) (4). Replaces the earlier plan to migrate static 44er nodes.
7. `--setnet auto|inet|hamnet` replaces the four `--setinet` / `--sethamnet` spellings; the old ones stay
   as deprecated aliases (4.1).
8. A static nRF52 node stays static: ETH-05 is fixed with the table, as its own commit (3.7).

Open:

- FR and US: hosts, time servers, and whether a Hamnet-side server exists.
- That "DL on the Hamnet via HC" satisfies the sysadmin's wish is second-hand (relayed by Kurt).

## Appendix A: how upstream moved DL, HC and HB (2026-09-29 to 2026-10-13)

All commits by Kurt (`icssw-org/oe1kbc_v4.40a`) unless noted. "inet" = Internet side, "ham" = Hamnet side.

| Date       | Commit     | PR     | Platform | Change                                                                                                                                                                                                                                                                                                                                                                                 |
| ---------- | ---------- | ------ | -------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 2025-06-24 | `3cecd6a0` | -      | both     | v4.35a: DL on the Hamnet exists (ESP32 `meshcom.hamnet.cloud`, nRF52 `44.148.230.197`). DL on the Internet does not: it falls through to OE.                                                                                                                                                                                                                                           |
| 2026-09-29 | `aac2e59a` | -      | ESP32    | v4.35v: DL inet = `meshcom.hamnet.network` (first time DL is an Internet server).                                                                                                                                                                                                                                                                                                      |
| 2026-09-30 | `f860e934` | -      | nRF52    | v4.35v RAK: DL inet = 192.68.17.26 on the dynamic path; DL ham NTP becomes 44.148.224.123.                                                                                                                                                                                                                                                                                             |
| 2026-10-01 | `6966c527` | (fork) | nRF52    | fork delta: the same DL branch also in the static path `startFIXUDP`.                                                                                                                                                                                                                                                                                                                  |
| 2026-10-12 | `4d6c562a` | #1209  | ESP32    | HC and HB added. HC ham 44.148.230.197, **HC inet 44.148.230.197 (unreachable from the Internet)**, HB ham `meshcom.emcomm.ch`, **HB inet `meshcom.oevsv.at` (OE host)**. The HB inet block takes the place of the closing `else`: `OE` (factory default) leaves `srv_host` NULL, gateway without Hamnet has no server link. Condition becomes `(44er && hamnet_only != 0) \|\| == 1`. |
| 2026-10-12 | `542262b8` | #1210  | nRF52    | DL ham branch replaced by HC (DL ham falls to the OE default 44.143.8.143). HC inet = 44.148.230.197 (unreachable). **HB ham = 157.143.4.44 (an Internet address in the Hamnet branch)**, HB inet = 157.143.4.44. Condition `(44er && hamnet_only == 1) \|\| == 1` on nRF52 and now ESP32 too.                                                                                         |
| 2026-10-12 | `d1c58278` | #1211  | ESP32    | HB ham `meshcom.emcomm.ch` -> `44.142.42.59`. HC inet `44.148.230.197` -> `meshcom.hamnet.network`. HB inet `meshcom.oevsv.at` -> `meshcom.emcomm.ch` and the OE default `else` is back (fix of the #1209 hole).                                                                                                                                                                       |
| 2026-10-13 | `2be1dfdc` | #1212  | nRF52    | HB ham 157.143.4.44 -> 44.142.42.59, NTP 162.159.200.1 -> 44.142.163.2. Comment added: "local 44er only routes to the Hamnet if `hamnet_only` is set".                                                                                                                                                                                                                                 |

Readings:

- **DL on the Internet pointing to OE** was true until v4.35v (ESP32 09-29, nRF52 09-30); since then DL inet
  is `meshcom.hamnet.network`. No commit between 09-29 and 10-13 touches DL inet. What flipped is HB and HC.
- **What flipped in five days:** HB inet (OE host -> emcomm.ch), HB ham (emcomm.ch -> literal, and on nRF52
  an Internet address -> literal), HC inet (Hamnet literal -> DL host on ESP32; still the Hamnet literal on
  nRF52), the 44er condition (`||` -> `!= 0` -> `== 1`), and the OE default (lost in #1209, back in #1211).
- **Still different today:** HC inet (ESP32 `meshcom.hamnet.network`, nRF52 44.148.230.197, fixed in this
  repo by commit `8d232bde`), DL ham on nRF52 (dropped upstream in #1210, restored in this repo), HB and HC
  absent in the nRF52 static path, Hamnet NTP rule (octet versus server).
- The pattern is the reason for the table: each change touched two to four places by hand, and five
  commits in two days did not leave the platforms equal.
