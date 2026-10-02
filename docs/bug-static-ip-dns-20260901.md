# NET-01..06 — a node on a fixed IP loses its server, and the web GUI cannot show why

**Status:** Mechanism ESTABLISHED by code reading against the tree at `dc7fcd41`. The specific
field case is **NOT confirmed** — the discriminating command (`--info` on the reporter's node) has
not been run. §7 says exactly what would confirm or kill it.
**Severity:** High for the affected operator — no crash, no mesh impact, but a gateway on a fixed
IP can be permanently disconnected from the MeshCom server while every local check says the node
is fine, and the web GUI is structurally unable to reveal the cause.
**Class:** upstream defect. `git diff upstream/dev HEAD` over both files is **empty** — none of
NET-01..04/06 is ours. NET-05 is fixed on this branch and upstream, but not in any released build.
**Reported:** 2026-09-01, operator chat thread — E22 node, "with a fixed IP I have no connection
to the server, with DHCP it works without problems; in both cases I can reach the node's web
server without problems", and, asked again about the web interface, "yes, that's the odd part, but
it feels sluggish".
**Branch:** `v4.35p_prio` @ `dc7fcd41` · **Upstream merge-base:** `2dac2eac`
**Related:** `TM-34` Wave W (async DNS, `f34fd2ae`), `CS-04` (`/getparam/`, same GUI/API split),
`WEB-01..04` (web UI intake), `CONF-01`, `E22-01`.

> **Scope note for the implementer.** NET-01 is the item that produced this ticket and NET-02 is
> the item that broke the node; they are independent defects that only look like one bug from the
> outside. Fixing NET-02 alone leaves the operator unable to verify the fix. Fixing NET-01 alone
> leaves the resolver wrong.

---

## 1. Verdict in one paragraph

Switching a node from DHCP to a fixed IP silently swaps its DNS resolver. The DHCP branch takes
the resolver the router handed out (`WiFi.dnsIP()`); the static branch takes `node_owndns`, and if
that string is shorter than seven characters it substitutes the hard-coded literal **`8.8.8.8`**.
The MeshCom server is reached by **name** (`meshcom.oevsv.at`), so once that resolver cannot
answer, `node_hostip` stays `0.0.0.0` and the node sends nothing to the server at all — while
everything that needs no DNS (its own web server, the LoRa side) keeps working. The operator
cannot see this from the web GUI, because the four "IP Network Settings" input boxes are
pre-filled with the node's **effective** values (`node_ip`, `node_subnet`, `node_gw`, `node_dns`)
while their buttons write the **stored** settings (`node_ownip`, `node_ownms`, `node_owngw`,
`node_owndns`). Under DHCP the DNS box therefore shows the router's address although
`node_owndns` is empty, and "it says the same in both variants" is not evidence of anything. NTP,
added in the same commit as the DNS field, is the only one of the five that shows the stored value.

## 2. What the reporter saw, and what each observation rules out

| Observation                                        | What it establishes                                                                      |
| -------------------------------------------------- | ---------------------------------------------------------------------------------------- |
| Web server reachable on the fixed IP               | L2/L3 inside the LAN is fine: address, netmask and ARP work. Rules out a typo'd address. |
| No server connection on the fixed IP, fine on DHCP | The failure is above L3 and is a function of the addressing mode, not of the link.       |
| Netmask/gateway/DNS "the same in both variants"    | **Nothing** — see NET-01. The page shows effective values, not stored ones.              |
| Web interface feels sluggish                       | Consistent with a blocked loop task (NET-05) or a duplicate address (H3, §8).            |

The combination "LAN yes / WAN no / no DNS needed for the part that works" is the signature of a
name-resolution failure, and it is the only one of the three hypotheses in §8 that explains all
four rows without an additional assumption.

## 3. The two paths side by side

`startMeshComUDP()`, `src/udp_functions.cpp:1471`:

| Setting           | DHCP branch (`:1530-1537`)       | Static branch (`:1489-1525`)                                                                       |
| ----------------- | -------------------------------- | -------------------------------------------------------------------------------------------------- |
| IP                | `WiFi.localIP()`                 | `node_ownip`                                                                                       |
| Netmask           | `WiFi.subnetMask()`              | `node_ownms`                                                                                       |
| Gateway           | `WiFi.gatewayIP()`               | `node_owngw`                                                                                       |
| **DNS**           | **`WiFi.dnsIP()`** (from router) | **`node_owndns`, else the literal `8.8.8.8`**                                                      |
| NTP               | unset (`node_ntp` untouched)     | `node_ownntp`, else empty                                                                          |
| Gate for the mode | —                                | `strlen(ownip) >= 7 && strlen(owngw) >= 7 && strlen(ownms) >= 7` — **DNS is not part of the gate** |

Two consequences follow directly from the table:

1. `node_owndns` is **only** consulted on a fixed IP. A value stored while the node runs DHCP has
   no effect and gives no feedback, which is exactly when an operator would think to set it.
2. The mode gate does not require a DNS. A node can therefore enter static mode fully configured
   by the firmware's own definition and still have no working resolver.

## 4. Findings

| ID     | Where                                                                                                   | Finding                                                                                                                                                                                                                                                                                                      | Sev.   | Action                                                                                                |
| ------ | ------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------ | ----------------------------------------------------------------------------------------------------- |
| NET-01 | `web_functions.cpp:1432-1435`                                                                           | The "fixed IP / Subnet Mask / Gateway / DNS" boxes are pre-filled from `node_ip` / `node_subnet` / `node_gw` / `node_dns` — the values in use — while their buttons write `node_own*`. `:1436` (NTP) is the only one that shows the stored setting. The page can never show whether a static setting exists. | High   | Pass `node_ownip` / `node_ownms` / `node_owngw` / `node_owndns` as `inputValue`, as NTP already does. |
| NET-02 | `udp_functions.cpp:1494-1497`, `:1508-1511`                                                             | Empty `node_owndns` on a fixed IP silently falls back to `8.8.8.8`. Switching DHCP → static swaps the resolver from "the router" to "Google" without a word in the log.                                                                                                                                      | High   | Fall back to `node_owngw`, which is already configured, validated and correct on any home LAN.        |
| NET-03 | `udp_functions.cpp:1522-1525`, `:1548`, `:1555-1562`                                                    | On a fixed IP the node reports itself online from its **settings string**: `WiFi.config()` failure is printed and ignored, `node_ip` never comes back from `WiFi.localIP()`. `--info` says `hasIpAddress: yes` and KEEP goes to `0.0.0.0`.                                                                   | Medium | Read `WiFi.localIP()` back after `WiFi.config()`; treat a `false` return as a bring-up failure.       |
| NET-04 | `command_functions.cpp:3921/3926`, `:3961`, `:3989/3994`; `udp_functions.cpp:1494-1497` vs `:1508-1511` | Two `--setowndns` handlers. The live one (`:3926`, offset `+12`) is correct; the dead duplicate (`:3994`, offset `+11`) would have cut the first character off the address. `:3961` (`setownms`) is missing its `else`. The DNS assignment block inside the static branch is likewise duplicated.            | Low    | Delete the dead handler and the duplicated block, restore the `else`.                                 |
| NET-05 | released builds only — pre-`f34fd2ae` `udp_functions.cpp`                                               | `startMeshComUDP()` resolved server **and** NTP host with blocking `WiFi.hostByName()` (two calls, up to 31 s each, in the loop task), and `sendMeshComHeartbeat()` had no retry — one failed lookup was permanent until reboot.                                                                             | Medium | **Already fixed here and upstream** (`f34fd2ae`, TM-34 Wave W). Nothing to do but ship a release.     |
| NET-06 | `udp_functions.cpp:1215-1246`                                                                           | The static address is applied only from the `got_ip` path, i.e. **after** a DHCP lease has been obtained. On a network without a DHCP server a statically configured node never applies its configuration at all.                                                                                            | Low    | Apply `WiFi.config()` before `WiFi.begin()` when a static configuration is stored.                    |

### NET-01 in detail

`_create_setup_textinput_element(id, label, inputValue, placeholder, parameterName, …)` —
`web_functions.h:46`, definition at `web_functions.cpp:2119`; the third argument is what the box
shows, the fifth is the command the button sends.

```
:1432  ownip   "fixed IP"     node_ip        -> setownip   -> node_ownip
:1433  ownsn   "Subnet Mask"  node_subnet    -> setownms   -> node_ownms
:1434  owngw   "Gateway"      node_gw        -> setowngw   -> node_owngw
:1435  owndns  "DNS"          node_dns       -> setowndns  -> node_owndns
:1436  ownntp  "NTP"          node_ownntp    -> setownntp  -> node_ownntp   <- the only correct one
```

The REST API is right where the page is wrong: `/getparam/` returns the stored `node_own*`
(`web_setup.cpp:880-901`), `/setparam/` writes them (`:440-476`). Page and API therefore disagree,
and the page does not use the API — our own CS-04 comment at `web_functions.cpp:2333-2335` records
why that went unnoticed for so long: "die Web-GUI faellt es nicht auf, weil sie ihre Werte aus den
gerenderten Seiten nimmt, nicht ueber `/getparam/`".

Blame: `:1432`/`:1434` Kurt 2025-06-07 (`3fb07e00`), `:1433` Kurt 2025-05-22 (`8ab2770d`),
`:1435`/`:1436` Luca Cireddu 2026-06-09 (`4f09bfc5`) — the same commit added the DNS box reading
the effective value and the NTP box reading the stored one.

### NET-02 in detail, and the HAMNET corollary

`8.8.8.8` fails whenever outbound port 53 to a public resolver is not available: no internet at
that moment, a router that blocks or hijacks external DNS, a guest/IoT VLAN, a Pi-hole with an
IP-range ACL — and, categorically, on a HAMNET-only site.

The HAMNET case is the sharpest, because the server names are chosen by the node's own address
(`udp_functions.cpp:1581-1631`):

| `node_gwsrv` | Server target on 44.x    | Survives a broken resolver? |
| ------------ | ------------------------ | --------------------------- |
| OE (default) | `44.143.8.143` (literal) | yes                         |
| `DL`         | `meshcom.hamnet.cloud`   | no                          |
| `IT`         | `meshcom.dig-italia.it`  | no                          |

So a DL or IT HAMNET gateway on a fixed IP with no `OWNDNS` is handed a resolver that is
unreachable from HAMNET by definition, and both of its server names are unresolvable. The OE
default works only because it is a literal.

### NET-03 in detail

```
:1522   if (!WiFi.config(node_ip, node_gw, node_ms, node_dns))
:1524     printlndeb("[Error] STA Failed to configure");     // printed, then execution continues
:1548   s_node_ip = node_ip.toString();                      // node_ip came from settings, :1502
:1555   if(strcmp(s_node_ip.c_str(), "0.0.0.0") == 0) hasIPaddress=false; else hasIPaddress=true;
```

`hasIPaddress` is therefore a statement about the **configuration file**, not about the interface.
Anything gated on it — `sendMeshComHeartbeat()` at `:1680`, the `--info` line at
`command_functions.cpp:5919` — inherits that. In the DHCP branch the same variable is honest,
because `node_ip` is `WiFi.localIP()`.

## 5. Why the reported symptoms follow

- **Web server reachable.** It listens on the node's own address and needs no resolver. Unaffected
  by any of NET-01..03.
- **No server connection.** `wifiDnsStart()` (`:1655`) is the only producer of `node_hostip`; with
  no answer it stays `0.0.0.0`, and `sendMeshComHeartbeat()` (`:1683-1689`) returns before
  `sendKEEP()`. Not one datagram is emitted. On this branch the heartbeat re-arms the lookup; in a
  released build (NET-05) there is no second attempt at all.
- **Sluggish web interface.** In a pre-`f34fd2ae` build the two `WiFi.hostByName()` calls block the
  loop task for up to 31 s each, on every bring-up, and the web server is served from that loop.
  This is the least certain link in the chain: it is consistent with the report but not proven,
  and H3 in §8 explains it equally well.

## 6. Fix proposal

Smallest change that closes the operator-visible defect, in this order:

1. **NET-01** — five characters per line, `web_functions.cpp:1432-1435`: show `node_own*`. This is
   the fix that makes every other one verifiable, so it goes first.
2. **NET-02** — replace the `8.8.8.8` fallback with `node_owngw`. On a home LAN the gateway is the
   DNS forwarder in the overwhelming majority of cases; on HAMNET it is at least routable. Log the
   substitution.
3. **NET-03** — read `WiFi.localIP()` back after `WiFi.config()` and derive `hasIPaddress` from it.
4. **NET-04** — delete the dead handler, the duplicated DNS block, restore the missing `else`.
5. **NET-06** — separate item, needs bench proof on a DHCP-less network before it is worth
   proposing.

**Regression tests owed** (`env:native_config` is the right home for 1, 2 and 4):

- a static configuration with empty `node_owndns` resolves to the gateway, not to `8.8.8.8`;
- `--setowndns 1.2.3.4` stores `1.2.3.4`, not `.2.3.4` (pins the offset the dead handler got wrong);
- the rendered setup page contains the stored `node_own*` values, not the effective ones.

**Bench proof** for 2 and 3 (`DK5EN-93`, Heltec V3, per the bench fleet table): configure a fixed
IP with no OWNDNS, block port 53 to `8.8.8.8` at the AP, confirm `[WIFI];dns;meshcom.oevsv.at;ip;0.0.0.0`
and no KEEP; apply the fix, confirm resolution via the gateway and KEEP resuming.

## 7. What would confirm or kill this, on the reporter's node

Two commands, in this order:

1. `--info` — the block at `command_functions.cpp:5902-5908` prints `...OWNDNS address:`. **Empty
   confirms the mechanism.** Note the block is only printed when IP, mask and gateway are all set,
   which is itself the case under test.
2. Boot log after `[WIFI]...Internet UDP-DEST meshcom.oevsv.at`: on this branch the next line is
   `[WIFI];dns;meshcom.oevsv.at;ip;<addr>;ms;<t>`. `0.0.0.0` proves the failure is resolution and
   nothing else; a valid address kills the whole hypothesis and sends the search to §8.

Workaround for the operator, independent of any firmware change: `--setowndns <router IP>` followed
by `--reboot`, or the same value in the GUI's DNS box **with the button pressed**, even when the box
already shows the right address. The lower-risk option is to leave the node on DHCP and pin its
address by MAC in the router.

## 8. Alternative hypotheses, kept open

- **H2 — the stored resolver is right but unreachable from the static address.** Routers with
  per-client access profiles (Fritz!Box parental controls), guest VLANs, or a Pi-hole ACL keyed on
  the DHCP range will answer the DHCP-assigned address and drop the static one. Same symptom, same
  §7 evidence, different fix. Discriminator: the `--info` OWNDNS field is populated.
- **H3 — duplicate address.** A fixed IP inside the router's DHCP pool that is already leased
  produces exactly the "reachable but sluggish" web interface, and NAT state on the router for the
  other holder explains the missing server traffic. Discriminator: `ping` the address with the node
  powered off; anything that answers proves it.
- **H1 (this document) is preferred** because it is the only one of the three that needs no
  assumption about the operator's router, and because the firmware provides the mechanism outright.

## 9. Not ours

`git diff upstream/dev HEAD -- src/web_functions/web_functions.cpp src/udp_functions.cpp` is empty.
Blame on every line named in NET-01..04 and NET-06 is Kurt (2025-01-15 through 2026-06-09) or Luca
Cireddu (2026-06-09). NET-05 is the one item this branch has already fixed — `f34fd2ae`, TM-34
Wave W, async `dns_gethostbyname()` with a per-boot cache and retry from the heartbeat — merged
upstream, and absent from every released 4.35p build.

## 10. Upstream

NET-01 and NET-02 are good PR candidates: two files, a handful of lines, no behaviour change for
any node that is not statically addressed, and they fix a class of report ("my gateway fell off the
map after I gave it a fixed IP") rather than one node. NET-03 changes what `hasIpAddress` means and
should be proposed with the reasoning, not just the diff. NET-04 is a cleanup to fold into either.
NET-06 must not be offered before it has bench evidence.
