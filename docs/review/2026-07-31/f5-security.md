# F5 — Security: full attack surface of the shipped firmware

**Repo:** `.` @ `v4.35p_prio` (1ba101f4)
**Scope:** whole shipped firmware, not the working diff. Defensive review.

## Index, ranked by (attacker reach × impact)

RF-reachable findings come first: a LoRa attacker needs no network access, no association, no
credential, is unattributable, and works at kilometre range against nodes that have no IP
connectivity at all.

| ID        | Sev          | Position     | One-line                                                                                                                                         |
| --------- | ------------ | ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| **F5-1**  | critical     | **RF**       | `{MCP}` password is a character-set membership test — five spaces pass on any node with a password shorter than 14 chars → remote GPIO actuation |
| **F5-2**  | critical     | **RF**       | `{SET}` writes mesh hop limits with no password and no range check — one broadcast packet silently cuts every node in range out of the mesh      |
| **F5-3**  | critical     | **RF → web** | Stored XSS from received LoRa traffic; web auth is IP-bound, so injected JS is already authenticated → full node takeover from radio proximity   |
| **F5-5**  | high         | **RF**       | `msg_id` is `MAC ‖ counter` and unauthenticated → forged delivery receipts and targeted, silent suppression of a chosen station's traffic        |
| **F5-9**  | medium       | **RF**       | No relay rate limit; TX ring is drop-oldest and shared → an RF flood evicts the operator's own outgoing messages                                 |
| **F5-10** | medium       | **RF**       | `{CET}` sets the clock from an unauthenticated broadcast (also undermines the proposed TOTP feature)                                             |
| **F5-14** | critical     | BLE range    | No pairing ever occurs and the command dispatch has no auth gate → any BLE device runs `--cleanflash` / `--setpwd` / `--ota-update`              |
| **F5-4**  | critical     | WiFi/LAN     | `mcpname` unchecked array index → 16-byte write up to ~4 KB past the settings struct, persisted to flash                                         |
| **F5-13** | critical     | WiFi range   | OTA is unauthenticated, unsigned, plain HTTP, over an open AP that can be _forced_ by deauth                                                     |
| **F5-15** | critical     | network      | `CONF` zero-fill loop overflows a 255-byte stack buffer by up to 251 bytes — one UDP packet (nRF52+Ethernet)                                     |
| **F5-16** | high         | network      | Spoofed UDP transmits arbitrary frames on LoRa under the operator's callsign; one zero-filled packet drops WiFi                                  |
| **F5-6**  | high         | LAN/internet | Empty `node_passwd` — the shipping default — is an unauthenticated root shell on TCP 2323                                                        |
| **F5-7**  | high         | LAN/internet | `--info` prints the console password, web password and WiFi PSK in cleartext                                                                     |
| **F5-8**  | medium       | LAN/internet | Net console lockout: one idle TCP connection blocks the console for ~34 min                                                                      |
| **F5-11** | low (latent) | —            | Parser accepts 340-byte frames into 255-byte buffers; **currently unreachable** — reported as latent                                             |
| **F5-12** | design       | —            | TOTP ADR review: the code authenticates the time, not the command; §8's replay mitigation is incorrect                                           |

**Cheapest high-value fixes, in order:** SEC-02 one-liner (`Serial.printf("%s", temp)`),
BUG-10 one-liner (`if(size < 12) return false;`), F5-15 loop bound, F5-14 dispatch gate,
F5-4 index bound, F5-2 password+clamp, F5-1 real comparison, F5-3 output escaping.

## Framing: what is already an accepted risk, and what is not

`docs/code-audit-20260626.md:264-278` (§7 Authentication & Security) records deliberate
`EXCEPTION` decisions for: open `WiFi.softAP()` (#44, #45), `SECMODE_OPEN` BLE (#46), empty web
password = open access (#47), URL-parameter auth (#48), hardcoded BLE PIN `000000` (#49), and
**OTA without auth and without firmware validation (#50)** — all justified as
_"Amateurfunk-Regulierung"_ (amateur-radio rules forbid encryption).

I do not re-litigate #44–#49; they are informed operator-facing choices. But three of those
rationales do not hold and are called out below:

1. **Amateur-radio rules forbid _obscuring the meaning of transmissions_. They do not forbid
   authenticating a management interface, and they say nothing about firmware signing.** #50
   (unsigned, unauthenticated OTA) is not an encryption question — it is a code-integrity
   question, and the exception rationale is misapplied. See F5-13.
2. The "open by design" decisions were taken assuming the web UI shows only _local_ data. It also
   renders **attacker-supplied bytes received over the air** (F5-3), which converts an accepted
   convenience into an RF-reachable takeover chain.
3. `--info` deliberately reveals credentials the same codebase deliberately redacts elsewhere
   (F5-7) — that is an inconsistency, not a decision.

`docs/code-audit-fixes-20260627.md:36-37` claims D1 (HMAC plaintext-bypass) and D2 (password in
serial log) fixed in `6ba4f3c7`. **Both verified genuinely fixed** — `src/net_console.cpp:163-186`
now goes straight to HMAC with `ct_equal()`, and `:167` logs `s_password:<***>`.

---

## Attack surface map

| Channel            | Authenticated?                                                                                                                                                                                                   | Entry point                                                                                                                                                                              | Input validation present?                                                                                                                                                                                                          | Worst realistic impact                                                                                                                                                                                                      |
| ------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **LoRa RF**        | **No** — no auth of any kind; callsign is a self-asserted string, `FCS` is a plain additive checksum (`aprs_functions.cpp:398-402`)                                                                              | `OnRxDone()` `src/lora_functions.cpp:288`                                                                                                                                                | Partial. `decodeAPRS` bounds path scans to 120 B and checks FCS + callsign regex, but `handleACK` has **no minimum-length gate** (`:202-214`) and the `{MCP}`/`{SET}`/`{CET}` command payloads get **no range or auth validation** | **Remote GPIO actuation with a bypassable password (F5-1); one-packet mesh disable for every node in range (F5-2); stored XSS → full node takeover (F5-3); targeted message suppression + forged delivery receipts (F5-5)** |
| **BLE**            | **No — and no pairing ever occurs.** All `WRITE_ENC`/`WRITE_AUTHEN` flags commented out (`esp32_main.cpp:1619-1630`); `setSecurityAuth(false,false,false)` (`:1588`); nRF52 `SECMODE_OPEN` (`nrf52_ble.cpp:274`) | onWrite `src/esp32/esp32_main.cpp:353`; `readPhoneCommand()` `src/phone_commands.cpp:208`                                                                                                | Write **length** is correctly bounded (`esp32_main.cpp:356-357`). The **frame's own length field is fully trusted** and never compared to bytes received                                                                           | **Unauthenticated arbitrary command execution from BLE range (F5-14)** — `--cleanflash`, `--setpwd`, `--ota-update`, radio reconfiguration                                                                                  |
| **Web UI**         | Only if `node_webpwd` set — **default is empty → fully anonymous** (`esp32_flash.h:119`; `web_functions.cpp:235`, `:295-296`). When set: source-IP allowlist, 4 h TTL, no token                                  | `web_client_html()` / `work_webpage()` `src/web_functions/web_functions.cpp:405-489`; `webSetup_setParam` `web_setup.cpp:18`                                                             | Mostly bounded `snprintf`, **but one attacker-controlled array index is unchecked** (`web_setup.cpp:551-553`) and **no output escaping exists anywhere**                                                                           | **OOB write up to ~4 KB past the settings struct, persisted to flash (F5-4); `manualcommand` = unrestricted CLI; `ACAO: *` + IP-bound auth + all-GET API = drive-by takeover (F5-3b)**                                      |
| **Net console**    | HMAC-SHA256 challenge-response — **but empty `node_passwd` (the default) means no auth at all** (`net_console.cpp:129-132`, `net_console.h:12`, `esp32_flash.h:105`)                                             | `loopNetConsole()` / `authTask()` `src/net_console.cpp:121`, `:377-403`; TCP 2323, `INADDR_ANY`                                                                                          | Response length + hex validated; constant-time compare present                                                                                                                                                                     | **Unauthenticated root shell on a node that is port-forwarded or on a public IP (F5-6)**; then `--info` hands over the web password and WiFi PSK (F5-7)                                                                     |
| **UDP backhaul**   | **No** — zero source-address validation; `grep -rn "remoteIP\|remotePort" src/` returns no hit in any UDP receive path                                                                                           | `getMeshComUDPpacket()` `src/udp_functions.cpp:112` (port 1990); `getExtern()` `src/extudp_functions.cpp:140` (port 1799); `getUDP()` `src/nrf52/nrf_eth.cpp:209`. All bind `INADDR_ANY` | Read length clamped, **but** the NUL-terminator write is off-by-one (`udp_functions.cpp:104`) and the `CONF` zero-fill loop is unbounded (`nrf_eth.cpp:515-518`)                                                                   | **Remote stack smash on nRF52+Ethernet (F5-15); spoofed datagram transmits arbitrary frames on LoRa under the node's callsign and can knock it off WiFi (F5-16)**                                                           |
| **OTA (safeboot)** | **No** — `ElegantOTA.clearAuth()` `src/safeboot/main.cpp:284`                                                                                                                                                    | `/ota/start`, `/ota/upload` `src/safeboot/ElegantOTA.cpp:46,201,225`                                                                                                                     | Only the `0xE9` image magic + an **optional, client-supplied** MD5 (`ElegantOTA.cpp:98-106`)                                                                                                                                       | **Arbitrary persistent firmware flash from WiFi proximity; no signature, no secure boot, no TLS (F5-13)**                                                                                                                   |

**Position legend used below:** _RF_ = anyone within LoRa range (worst: unattributable, no network
access, works against a node with no IP connectivity at all). _WiFi/LAN_ = associated to the node's
AP or on the same L2/L3 network. _Internet_ = reachable if the node is port-forwarded or on a public
IP (the brief states this is common for MeshCom gateways).

---

## Findings

### F5-1: `{MCP}` remote-GPIO password is a character-set membership test — a space always passes

- **File:** `src/loop_functions.cpp:2057-2071` (check), `:2042` (entry), `:2101` (`commandAction`)
- **Severity:** critical
- **Attacker position:** RF range. Unauthenticated, unattributable.
- **Relation to prior art:** extends `docs/code-audit-20260712.md` **SEC-01**, which found only the
  _empty-password_ path (all-`0x00` `cpasswd` → loop body never runs → `bpass` stays `true`).
  **Verified still unfixed.** The mechanism below is new: it defeats a node that _has_ configured a
  password, so SEC-01's proposed fix (`if(!bpass || cpasswd[0]==0x00) return;`) does **not** close it.

The check is not a comparison:

```c
bool bpass=true;
for(int ip=0;ip<5;ip++) {
    if(cpasswd[ip] != 0x00 && bpass) {
        bool bp=false;
        for(int ic=0;ic<15;ic++) {
            if(meshcom_settings.node_passwd[ic] == cpasswd[ip])   // <-- membership, not equality
                bp=true;
        }
        bpass = bp;
    }
}
```

It only asks whether each received character occurs _somewhere_ in `node_passwd`. Order, position
and repetition are all irrelevant. Two consequences:

1. Any single character of the password, repeated five times, passes (`"AAAAA"` against
   `"ABCDEFGHIJKLMN"`).
2. **`node_passwd` is space-padded to exactly 14 characters** by
   `src/command_functions.cpp:3007`:
   `snprintf(meshcom_settings.node_passwd, sizeof(...), "%-14.14s", _owner_c);`
   The `-14` left-justify flag pads with `0x20`. So for **every password shorter than 14
   characters — i.e. essentially every real node — the space character is guaranteed to be a member
   of the set.** Five spaces pass unconditionally.

The second gate is not a gate either (`:2074-2079`): it compares `cset[5]`, `cset[8]`, `cset[11]`
against `clfd`, which is `snprintf("%03i", aprsmsg.msg_id & 0x3FF)` (`:2053`) — derived from the
**attacker's own** `msg_id`, which the attacker writes into bytes 1–4 of the frame.

**Concrete attack.** Transmit one LoRa text frame, broadcast (`>*`), payload:

```
{MCP}1A02ON3<SP><SP><SP><SP><SP>
```

with `msg_id` chosen so `msg_id & 0x3FF == 123`. Field layout is
`{MCP}` `d1` `A` `0` `d2` `ON` `d3` `ppppp` (`:2037`). The five trailing spaces are the password
field. Reachability: `OnRxDone` → the broadcast branch at `src/lora_functions.cpp:844` →
`queueDisplayText` → `sendDisplayText` (`loop_functions.cpp:2034`) → the `{MCP}` handler. **The
`{MCP}` branch is evaluated before, and independently of, the `destination_call == "*"` /
`bNoMSGtoALL` gate**, so it fires regardless of the node's broadcast settings.

**Impact.** `commandAction("--setout A0 on")` (`:2101`) drives the MCP23017 output expander
(`src/io_functions.cpp`). On any node wired for remote switching — repeater PTT, antenna relay, mast
power, the ADR's LED — an RF-range attacker with no credentials actuates it at will.
(Side note: `:2096-2099` inverts the command — `bON` true emits `"off"`. Cosmetic for the attacker.)

**Fix.** Replace the loop with a real comparison against the padded stored value, and require a
non-empty configured password before the feature is active at all:

```c
if(meshcom_settings.node_passwd[0] == 0x00 || meshcom_settings.node_passwd[0] == ' ') return;
if(memcmp(cpasswd, meshcom_settings.node_passwd, 5) != 0) return;   // constant-time preferred
```

Five characters of key material is far too little for an unauthenticated RF surface; consider
requiring the full 14 and adding a monotonic counter or timestamp so the frame is not replayable.
Regression tests: (a) empty password, (b) five spaces, (c) a permutation of the real password,
(d) the correct password.

---

### F5-2: `{SET}` writes mesh routing parameters over the air with no password and no range check

- **File:** `src/loop_functions.cpp:2121-2127`; dispatch at `src/lora_functions.cpp:854`
- **Severity:** critical
- **Attacker position:** RF range. Unauthenticated, unattributable, no network access needed.
- **Not present in any prior-art document.**

```c
if(aprsmsg.msg_payload.startsWith("{SET}") > 0)
{
    char cset[30];
    snprintf(cset, sizeof(cset), "%s", aprsmsg.msg_payload.c_str());
    sscanf(cset+5, "%d;%d;", &meshcom_settings.max_hop_text, &meshcom_settings.max_hop_pos);
    return;
}
```

There is no password field, no sender allowlist, no range check, and no bounds validation — a raw
`sscanf` straight into two persisted settings fields. Unlike `{MCP}`, `{SET}` does not even pretend
to authenticate.

`max_hop_text` / `max_hop_pos` seed `aprsmsg.max_hop` for every message the node _originates_
(`src/aprs_functions.cpp:214-217`), which is encoded into wire byte 5 as
`msg_buffer[5] = aprsmsg.max_hop & 0x0F` (`src/aprs_functions.cpp:~630`). Receivers relay only when
`aprsmsg.max_hop > 0` (`src/lora_functions.cpp:1101`).

**Concrete attack A — one-packet mesh silencing.** Transmit a single broadcast text frame with
payload `{SET}0;0;`. Every node in radio range now originates all future traffic with `max_hop == 0`.
No neighbour will relay any of it. **Every node that heard the packet is silently cut out of the
mesh** — its messages and beacons reach exactly one hop and stop. The operator sees a node that
looks perfectly healthy: it transmits, it receives, its display works. Nothing logs the change.
`{SET}16;16;` is equivalent and less obvious (`16 & 0x0F == 0`).

**Concrete attack B — flood amplification.** `{SET}15;15;` maximises the hop budget for every node
in range, multiplying relay traffic on a shared duty-cycle-limited band.

**Persistence.** The write is to RAM, so it survives until reboot — but `save_settings()` is called
by dozens of unrelated paths (any later config change, any BLE settings write), at which point the
attacker's values are **persisted to flash permanently**.

**Impact.** Unauthenticated, one-frame, mesh-wide denial of service against every node in radio
range simultaneously, with no diagnostic trail. This is the highest damage-per-packet ratio on the
RF surface.

**Fix.** Gate `{SET}` behind the same (repaired, per F5-1) password check as `{MCP}`, clamp both
values to the protocol's legal 0–15 range, log the change, and prefer requiring a directed message
to the node's own callsign rather than accepting broadcasts. If `{SET}` exists only for the
operator's own gateway, restrict it to the UDP/console path and remove it from the RF path entirely.

---

### F5-3: Stored XSS from LoRa into the web UI — RF proximity yields full node takeover

- **Files:** render sites `src/web_functions/web_functions.cpp:834` (RX Log), `:1341`, `:1354`
  (messages), `:935`, `:991`, `:993` (MHeard); data path `src/loop_functions.cpp:2941-2950`
  (`charBuffer_aprs`), `src/aprs_functions.cpp:363-377` (payload decode)
- **Severity:** critical
- **Attacker position:** **RF range only.** The attacker never touches the node's network.
- **Prior art:** audit #20–#22 flag "web input validation" and DE-PRIO them as _"Web offen per
  Amateurfunk-Exception; Operator-Selbstverschulden"_. That rationale covers _operator-entered_
  values. It does not cover **third-party bytes arriving over the air**, and the RF→web chain is not
  identified anywhere in `docs/`.

There is no output escaping in the web layer — grepping `src/web_functions/` for
`htmlescape|escapeHtml|sanitiz|&amp;|&lt;` returns zero hits. Worse, `decodeURLPercentCoding`
actively _re-introduces_ the dangerous characters (`web_functions.cpp:576-577`).

The received payload is never character-filtered. `decodeAPRS` restricts the source/destination
**path** bytes to `0x20..0x7E` (`aprs_functions.cpp:194`, `:308`) but the **payload** loop
(`:363-377`) copies every byte up to the NUL terminator with no filtering at all. It lands verbatim
in the RX-log ring (`loop_functions.cpp:2941-2950`, 60 payload chars) and, untruncated, in
`aprsmsg.msg_payload` for the messages page.

Then:

```c
// web_functions.cpp:834  — RX Log
web_client.printf("<p class=\"font-small no-wrap\"><%i>%s</nobr></td></tr>\n", iRead, ringbufferRAWLoraRX[iRead]);
// web_functions.cpp:1341 — messages
web_client.printf("<p class=\"font-normal\">%s</p>", msgtxt.c_str());
```

Also note the middle elements of `msg_source_path` are **not** regex-checked — `checkRegexCall` is
applied only to `msg_source_call` (first element) and `msg_source_last` (last element)
(`aprs_functions.cpp:249-273`). A path like `OE1AAA,<img src=x onerror=…>,OE1BBB` passes decode and
is rendered into an `href` **and** as element text at `:1337`.

**Concrete attack.**

1. Attacker within LoRa range transmits one broadcast text frame whose payload is
   `<img src=x onerror=fetch('/setparam/?manualcommand=--webpwd%20pwned')>` (fits the 60-char RX-log
   budget in shortened form; the messages page has no such limit).
2. The operator opens the node's web UI. The messages view is **auto-polled every 10 s** by the
   page scaffold (`web_functions.cpp:611`, `:621`), so the payload fires without the operator
   navigating anywhere.
3. The JS runs same-origin on the node.

**Why this escalates to takeover rather than stopping at defacement:** the web session credential
**is the source IP** (`web_functions.cpp:252-257`), not a cookie or token. The injected script runs
in the operator's browser, from the operator's IP, and is therefore _already authenticated_. It can
immediately call `GET /setparam/?manualcommand=--<anything>` (`web_setup.cpp:26-35`), which is
unrestricted CLI access. There is no CSRF token to steal and no SameSite protection to defeat.
End state: attacker sets a web password to lock the operator out, changes the WiFi SSID/PSK to an
attacker AP, or triggers `--ota-update` and chains to F5-13.

**F5-3b (same root, network-side variant):** `web_functions.cpp:1729` sends
`Access-Control-Allow-Origin: *` on **every** response, including 401s. Because the credential is the
requester's IP and not a cookie, the usual "cookies are not sent cross-origin" mitigation does not
apply. **Any web page the operator visits, from anywhere on the internet, can both write to and
read from the node** — including `GET /getparam/?setpwd`, which returns the WiFi PSK in cleartext
(`web_setup.cpp:838-841`). Combined with the all-GET API (documented as such in
`web_functions/Web-API_documentation.txt:10`) this is drive-by node compromise.

**Fix.**

1. HTML-escape at every render site (`&`, `<`, `>`, `"`, `'`). A single 20-line
   `web_escape(const char*)` helper covers all of them; this is the minimal change and it fixes the
   RF vector completely.
2. Filter the decoded payload to printable ASCII in `decodeAPRS` (matching what the path loop
   already does) — defence in depth for the display/serial paths too.
3. Remove `Access-Control-Allow-Origin: *`, or restrict it to same-origin. The comment
   _"tell modern browsers that CORS is okay for us"_ is the defect: it is not okay for a device whose
   only credential is the requester's IP address.
4. Move state-changing endpoints to POST and add a per-session token.

---

### F5-4: `mcpname` — attacker-controlled array index gives a 16-byte write up to ~4 KB past the settings struct, persisted to flash

- **File:** `src/web_functions/web_setup.cpp:539-556`; destination `src/esp32/esp32_flash.h:88`
  (`char node_mcp17t[16][16]`), mirrored at `src/nrf52/WisBlock-API.h:251`
- **Severity:** critical
- **Attacker position:** WiFi/LAN — anonymous when `node_webpwd` is empty (the default), which in
  AP mode means "anyone in WiFi range of an open AP named after the operator's callsign".
- **Not present in any prior-art document.**

```c
if(port.length()!= 2) { setupData->returnCode = WS_RETURNCODE_FAIL; return; }   // length only

uint8_t t_io = (uint8_t)port.charAt(1) - 48;    // ASCII '0' is numerically 48
if(port.charAt(0)=='B') t_io+=8;

snprintf(meshcom_settings.node_mcp17t[t_io], sizeof(meshcom_settings.node_mcp17t[t_io]), "%s", setupData->paramValue.c_str());
...
save_settings();
```

Only the _length_ of `port` is validated — never its content. `charAt(1)` is a raw request byte, so
`t_io` covers the whole `uint8_t` range (bytes below `'0'` wrap to 208–255). The write lands at
`node_mcp17t + t_io*16`, i.e. up to **4080 bytes past a 256-byte array**, and is then committed to
NVS by `save_settings()`.

Everything after `node_mcp17t` in the struct is at a positive offset and therefore reachable —
verified in `src/esp32/esp32_flash.h`: `node_gcb` (`:91`), `node_country`, `node_passwd[15]`
(`:105`), `node_ownip/owngw/ownms`, `node_name`, **`node_webpwd[20]` (`:119`)**,
**`node_ssid[33]` (`:121`)**, **`node_pwd[64]` (`:122`)**.

**Concrete attack.** `GET /setparam/?mcpnameA~=<16 attacker bytes> HTTP/1.1` — `'~'` is 126, so
`t_io = 78` and the write lands 1248 bytes past the array. Sweeping the second character enumerates
a 16-byte-granular write primitive across the entire settings struct and into adjacent globals.
Overwrite `node_ssid`/`node_pwd` to make the node associate with an attacker-controlled AP on next
boot; overwrite `node_webpwd` to lock the operator out; corrupt arbitrary RAM to crash it.

**The equivalent CLI path is correctly guarded** — `src/command_functions.cpp:3820`:
`if(iVar >= 0 && iVar <= 7)`. `webSetup_setParam` bypasses `commandAction()` here and writes the
struct directly, which is exactly why it lost the bound.

**Fix.** `if(t_io > 15) { setupData->returnCode = WS_RETURNCODE_FAIL; return; }` before the
`snprintf`. Same missing check in `mcpio` (`web_setup.cpp:499-502`) and `mcpout` (`:526-529`), where
`1 << t_io` with `t_io > 31` is additionally undefined behaviour (read-only there, so lower
severity). Better: route these through `commandAction()` like every other parameter so the existing
bound applies.

---

### F5-5: `msg_id` is fully predictable and unauthenticated — targeted message suppression and forged delivery receipts

- **Files:** generation `src/loop_functions.cpp:3138` (and `:3736`, `:3820`, `:3884`, `:3962`,
  `:4056`); dedup gate `src/lora_functions.cpp:673`; dedup insert `src/loop_functions.cpp:634-655`;
  ACK handling `src/lora_functions.cpp:220-256`
- **Severity:** high
- **Attacker position:** RF range.
- **Not present in any prior-art document.** `msg_id` is documented as a deduplication mechanism;
  this finding is that the code also relies on it for **delivery semantics** while it carries no
  integrity guarantee whatsoever.

```c
aprsmsg.msg_id = ((_GW_ID & 0x3FFFFF) << 10) | (meshcom_settings.node_msgid & 0x3FF);
```

`_GW_ID` is the node's MAC (`src/esp32/esp32_main.cpp:1016`, `src/nrf52/nrf52_main.cpp:686`) and
`node_msgid` is a sequential counter wrapping at 999 (`loop_functions.cpp:3153-3155`). `msg_id`
travels in cleartext in wire bytes 1–4. There is no MAC, no signature, and no nonce. `FCS` is a plain
additive byte sum (`aprs_functions.cpp:398-402`), trivially recomputed by anyone. **Nothing binds a
frame to the callsign it claims** — full impersonation of any station is a given, and that is
inherent to the protocol. What is _not_ inherent is the following:

**Attack A — forged delivery receipt + retransmission cancellation.** Hear a victim's outgoing text
frame (`msg_id` is in the clear). Immediately transmit an ACK frame carrying that `msg_id`.
`handleACK` (`lora_functions.cpp:221`) calls `checkOwnTx(msg_id)`, matches, and at `:238-251`:
sets `own_msg_id[itxcheck][4] = 0x02` (ACK), pushes an ACK notification to the operator's phone via
`addBLEOutBuffer`, and calls `findAndStopRingSlot(msg_id)` to **stop retransmission**. Result: the
sender's app displays a delivery confirmation for a message that reached nobody, _and_ the firmware
stops trying to deliver it. Attacker needs no prediction — only to hear the frame.

**Attack B — targeted suppression.** `is_new_packet()` (`lora_functions.cpp:673`) gates the _entire_
receive branch: display, phone forwarding, gateway upload, and relay. Pre-inject frames carrying the
victim's _future_ `msg_id`s — trivially computed as `(GWID<<10)|n`, where `GWID` is learned from any
one overheard frame and `n` is the next counter value. Every node in range calls
`addLoraRxBuffer()` (`:737`) and records them. When the victim actually transmits, every neighbour
classifies it as a duplicate and **silently drops it** — no display, no relay, no gateway upload.
The dedup ring holds 60–100 entries (`configuration_global.h:83`, `:90`, `:98`, `:113`), so a burst
of ~50 pre-injected IDs suppresses the victim's next ~50 messages, refreshable indefinitely at
negligible cost. The victim sees successful transmissions and no errors.

**Impact.** Targeted, deniable, remote censorship of a specific amateur station, plus false delivery
confirmations. On an emergency/EmComm mesh, a station that believes its traffic is being delivered
while it is being silently dropped is the worst failure mode available.

**Fix.** This is a protocol-level gap and needs upstream coordination; it cannot be fully fixed in
one node. Minimal, locally useful mitigations:

- Derive `msg_id` from `esp_random()` rather than `MAC || counter`, so future IDs are not
  predictable (fixes Attack B; costs nothing, stays wire-compatible).
- Treat an ACK as authoritative only when the receiving node also has corroborating evidence
  (e.g. require the ACK's source callsign to match the message's destination), so Attack A needs the
  attacker to also forge the destination station's identity in a way the operator can see.
- Long term: an optional signed/HMAC'd frame extension for stations that opt in. Note that message
  _authentication_ does not obscure meaning and is therefore not affected by the encryption
  prohibition that drives the project's other exceptions.

---

### F5-6: Empty `node_passwd` — the shipping default — is an unauthenticated root shell on TCP 2323

- **Files:** `src/net_console.cpp:129-132` (the bypass), `:285-300` (bind to `INADDR_ANY`),
  `src/net_console.h:12` (documented), `src/esp32/esp32_flash.h:105` (`char node_passwd[15] = {0}`)
- **Severity:** high (critical for a port-forwarded gateway)
- **Attacker position:** LAN, or **internet** if the node is port-forwarded or on a public IP.

```c
if (s_password[0] == '\0')
{
    authOk = true;  // no password configured — grant immediately
}
```

`node_passwd` defaults to all zeros and `--passwd none` explicitly clears it
(`command_functions.cpp:2998-3001`). The listener binds `INADDR_ANY` on port 2323 (`:288-292`) with
no interface restriction and no source filtering. Once connected, the client gets the full serial
console — every command `commandAction()` implements, including `--reboot`, `--setout`, `--setcall`,
`--setssid`/`--setpwd`, and `--ota-update`.

**Concrete attack.** `nc <node-ip> 2323` → banner → type `--info`.

**Compounding:** the very first thing an unauthenticated console user can do is `--info`, which
prints (F5-7) the net-console password, the **web password**, and the **WiFi PSK** in cleartext.
So an anonymous TCP connection yields the credentials for every other surface on the device.

**Fix.** Refuse to start the listener when `node_passwd` is empty; log
`"[CON ]...net console requires --passwd; not started"`. If open access is genuinely wanted, make it
an explicit second opt-in (`--netconsole open`) rather than the default state, and bind to the LAN
interface rather than `INADDR_ANY`. Also raise the 14-character ceiling
(`command_functions.cpp:2993`, `:3007`) — 14 characters is a weak key for a network-exposed shell
and forbids a passphrase entirely.

**Related, verified still unfixed:** `docs/code-audit-20260712.md` **CONC-19** —
`stopNetConsole()` (`src/net_console.cpp:274-288`) still does `s_mutex = xSemaphoreCreateMutex();`
without holding the old mutex (leaking it), then calls `teardownClient()`, which does an
unmatched `xSemaphoreGive()`. Also `if(s_listen_fd >= 0) ::close(s_listen_fd); s_listen_fd = -1;`
(`:283`) has a dangling-statement shape — the assignment is unconditional, which happens to be
correct here but reads as a bug.

---

### F5-7: `--info` prints the net-console password, the web password and the WiFi PSK in cleartext

- **Files:** `src/command_functions.cpp:4759-4760` (`node_passwd`), `:4829` (`node_webpwd`),
  `:4851-4852` (`node_pwd`, the WiFi PSK); also `src/udp_functions.cpp:560` (PSK on every WiFi
  connect failure, not behind any debug flag); `:5203` (`nsetdoc["WSPWD"]` — web password over BLE)
- **Severity:** high
- **Attacker position:** anyone who reaches the console (LAN/internet per F5-6) or any BLE peer.

```c
printfdeb("...NOMSGALL %s ...MESH %s ...BUTTON (%i) %s ...SOFTSER %s ... SOFTSERREAD %s\n...PASSWD <%s>\n",
    ..., meshcom_settings.node_passwd);                       // :4760
printfdeb(" / Webpwd <%s>", meshcom_settings.node_webpwd);    // :4829
printfdeb(" / PASSWORD <%s>\n", meshcom_settings.node_pwd);   // :4852
```

`printfdeb` emits via `Serial.printf` (`src/printfdeb_functions.cpp:118`), and `Serial` is
`#define`d to `MSerial` (`src/net_console.h:76`), which mirrors **every byte of serial output** to
the TCP client (`src/net_console.cpp:246-256`). So all three credentials go out over the network
console.

**This directly contradicts the codebase's own stated intent** — `--passwd` with no argument
deliberately refuses to echo the value:

```c
// command_functions.cpp:3020-3022
// --passwd without argument: show current status (never print the actual password)
bool hasPasswd = (meshcom_settings.node_passwd[0] != 0x00 && meshcom_settings.node_passwd[0] != ' ');
printfdeb("...passwd is %s\n", hasPasswd ? "SET" : "EMPTY (open access)");
```

`--wifiset` shows the same good instinct — the PSK lines are deliberately commented out
(`command_functions.cpp:5022`, `:5026`). But the identically-marked `//KBC/KFR` line at `:5203` was
**not** commented out, so `--nodeset` still ships the web password to any BLE peer, and BLE has no
authentication (audit #46).

**Impact.** Chained with F5-6: one anonymous TCP connection to a default-configured node yields the
WiFi PSK (lateral movement onto the operator's home network) and the web password.

**Fix.** Redact all three in `--info` — print `SET`/`EMPTY` as `--passwd` already does. Remove the
PSK from `udp_functions.cpp:560`. Comment out `:5203` to match `:5022`/`:5026`.

---

### F5-8: Net console can be locked out indefinitely by one idle TCP connection

- **File:** `src/net_console.cpp:392-403` (single-handshake gate), `:143-160` (the read loop)
- **Severity:** medium
- **Attacker position:** LAN or internet (whoever can reach port 2323).

```c
if (s_hs_running) { ...  ::close(client_fd);  return; }      // :392-397
s_hs_running = true;
```

Exactly one handshake may be in flight; every other connection is rejected and closed. The handshake
runs in `authTask`, which reads the response **one byte at a time** with `SO_RCVTIMEO` set to 30 s
(`:141`, `:147-155`). The timeout is per-`recv`, not per-session.

**Concrete attack.** Connect, then send one byte every 29 seconds. `respBuf` is 72 bytes
(`:143`), so a single connection holds `s_hs_running` for up to ~34 minutes, during which the
legitimate operator is refused. Reconnect on expiry. Cost to the attacker: one socket.

The `s_hs_running` flag is also a plain `volatile bool` set from the loop task and cleared from
`authTask` on core 1 without synchronisation — if a task-creation failure path is ever hit
concurrently, the console can wedge permanently.

**Fix.** Track handshake start time and abort the auth task after a whole-session deadline
(e.g. 10 s total, not 30 s per byte); use a single `recv` with a deadline rather than a byte loop.
Make `s_hs_running` `std::atomic<bool>` (`docs/codequality-rules.md` §4 already requires this).

---

### F5-9: Relay path has no rate limit — an RF flood evicts the operator's own outgoing messages

- **Files:** `src/loop_functions.cpp` `addRingPointer()` (drop-oldest policy), TX ring
  `src/loop_functions.cpp:383` (`ringBuffer[MAX_RING][…]`, `MAX_RING` = 10/20/30 per board,
  `configuration_global.h:82`, `:89`, `:97`, `:104`, `:112`); relay enqueue
  `src/lora_functions.cpp:1176-1184`
- **Severity:** medium
- **Attacker position:** RF range.
- **Violates the project's own rule** `docs/codequality-rules.md` §16: _"Rate limiting: cap repeated
  responses to prevent amplification."_

Every received frame with a new `msg_id` and `max_hop > 0` is re-encoded and enqueued for
transmission (`lora_functions.cpp:1161-1184`). There is no per-source rate limit, no airtime budget
check, and no distinction between relay traffic and operator-originated traffic — they share one
ring. When the ring fills, `addRingPointer()` **advances the read pointer**, silently discarding the
oldest entry and logging only `RING_OVERFLOW` under `bLORADEBUG`.

**Concrete attack.** Transmit valid, well-formed text frames with random `msg_id`s as fast as the
channel allows. Each is unique, so dedup does not stop it; each is relayed by every node in range;
the 10–30 slot TX ring saturates, and **the operator's own queued messages are dropped before
transmission**. Every node in range simultaneously spends its duty cycle relaying attacker traffic.

**Impact.** Mesh-wide amplified DoS that propagates one hop further than the attacker's own range —
worse than a plain jammer because the victims do the transmitting. Some flooding is inherent to a
flood mesh; the specific defects are (a) no rate limit at all and (b) no priority separation, so the
operator's traffic loses to relay traffic.

**Fix.** Reserve a few TX-ring slots for locally-originated messages so relay traffic cannot starve
them. Add a per-source-callsign relay budget (e.g. N frames per minute) and a global airtime cap;
`ch_util_rx_accum` (`lora_functions.cpp:294-300`) already measures channel utilisation, so the input
for a back-off exists. Promote `RING_OVERFLOW` out of `bLORADEBUG` so operators can see it.

---

### F5-10: `{CET}` sets the node clock from an unauthenticated broadcast

- **File:** `src/loop_functions.cpp:2130-2158`; dispatch `src/lora_functions.cpp:864`
- **Severity:** medium
- **Attacker position:** RF range.

No password, no sender check. Guarded only by `if(!bRTCON && !posinfo_fix && !bNTPDateTimeValid)`
(`:2133`) — i.e. it applies precisely to the cheap nodes with no GPS, no RTC and no NTP, which is a
large share of the fleet. The only content validation is `if(Year > 2023)` (`:2151`); month, day,
hour, minute and second are taken from `substring().toInt()` with no range check, so
`{CET}9999-99-99 99:99:99` reaches `MyClock.setCurrentTime()` with out-of-range fields.

**Impact.** Wrong timestamps on every logged and displayed message, mesh-wide, for every clockless
node in range. Note this becomes a _security_ dependency if the TOTP feature in
`docs/adr-totp-remote-led.md` is implemented — TOTP validity is a pure function of the clock, and
`{CET}` lets an RF attacker move it (see F5-12).

**Fix.** Range-check all six fields before use. Require `{CET}` to be a directed message to the
node's callsign rather than accepting broadcasts, and/or gate it behind the `{MCP}` password once
that is repaired.

---

### F5-11: `decodeAPRS` accepts frames up to 340 bytes but assembles them in 255-byte stack buffers

- **File:** `src/aprs_functions.cpp:9` (`#define MAX_APRS_FRAME_SIZE 340`), `:149` (the accept
  check), `:183-187` (the three `char cConcat[UDP_TX_BUF_SIZE]` = 255-byte buffers), `:363-377`
  (the unbounded payload loop)
- **Severity:** low today, high if the reachability precondition ever changes
- **Attacker position:** would be RF/network. **Currently not reachable — reported as a latent
  defect, not a live vulnerability.**

```c
if (rsize > MAX_APRS_FRAME_SIZE)   // 340
    return 0x00;
...
char cConcat1[UDP_TX_BUF_SIZE];    // 255
...
for(ib=inext; ib < rsize; ib++)    // bounded only by rsize (<=340)
{
    if(RcvBuffer[ib] == 0x00) { ... break; }
    else { cConcat1[iConcat1] = (char)RcvBuffer[ib]; iConcat1++; }   // no bound on iConcat1
}
```

The two path loops are correctly capped at 120 iterations (`:194`, `:308`). The **payload** loop is
not — it is bounded only by `rsize`. With `rsize` in 256..340 and `inext` near its minimum of ~8, up
to ~332 bytes are written into a 255-byte stack array: a **~77-byte stack smash with fully
attacker-controlled contents.**

**I traced every caller and none can currently supply `rsize > 255`:**

| Caller                          | Bound | Why                                                  |
| ------------------------------- | ----- | ---------------------------------------------------- |
| `lora_functions.cpp:455`        | ≤255  | LoRa PHY maximum payload                             |
| `udp_functions.cpp:186`, `:461` | ≤255  | `Udp.read(incomingPacket, UDP_TX_BUF_SIZE)` (`:100`) |
| `extudp_functions.cpp:251`      | ≤255  | `UdpExtern.read(…, UDP_TX_BUF_SIZE)` (`:223`)        |
| `nrf52/nrf_eth.cpp:299`         | ≤255  | same read pattern                                    |
| `web_functions.cpp:1303`        | ≤255  | `blelen` is `uint8_t`                                |

So the 340 limit is currently inert. But `MAX_APRS_FRAME_SIZE = 340` is an explicit, documented
statement that the parser accepts frames larger than its own working buffers — a single future
caller with a larger read buffer turns this into remote stack corruption, and the parser is exactly
the code most likely to be reused for a new transport.

`docs/code-audit-20260712.md` **BUG-13** notes the trailing-field over-read in the same function but not this
buffer/limit mismatch.

**Fix.** Either bound the payload loop (`if(iConcat1 >= (int)sizeof(cConcat1)-1) break;`) or, better,
set `MAX_APRS_FRAME_SIZE` to `UDP_TX_BUF_SIZE` so the accept check and the buffers agree — and add a
`static_assert` tying them together (`docs/codequality-rules.md` §2 requires exactly this).
`docs/codequality-rules.md` §19 boundary tests (0, 1, max, max+1 bytes) would have caught it.

---

### F5-12: `docs/adr-totp-remote-led.md` — design review (feature not yet implemented)

- **File:** `docs/adr-totp-remote-led.md` (status _Proposed_). Confirmed unimplemented — no
  `src/totp_functions.*`, zero `TOTP` references in `src/`.
- **Severity:** n/a (design); would be **high** if implemented as written.

The ADR is a clear improvement over `{MCP}` and the intent is right. Four design defects should be
resolved before implementation:

**(a) The TOTP code authenticates the _time_, not the _message_ — §8's replay mitigation is wrong.**
The ADR states: _"TOTP-Code ist Einmal-Code — Mitlesen bringt nichts, da er nach 30s verfällt."_
That is incorrect on an unencrypted broadcast medium. The wire format `TOTP:<code>:<command>`
computes the code over the time step only; the command is outside the authenticated envelope, and
nothing records consumed codes. An RF-range attacker who overhears `TOTP:482913:ON` can, within the
same window, transmit `TOTP:482913:OFF` — a **valid, different command with a stolen-but-still-valid
code**. §4's ±1-step tolerance widens the window to ~90 s. Fix: bind the command into the HMAC
(compute over `time_step || command`), and keep a small cache of consumed `(code, step)` pairs so a
code is genuinely single-use.

**(b) §4 Phase 1 — "Node reagiert auf alle Textnachrichten (Broadcast, Gruppe 999, DM)" — makes the
NACK an amplification vector.** Every failed attempt produces a DM injected into the mesh (§3
`TOTP NACK`). An attacker broadcasting `TOTP:000000:ON` at channel rate makes the node flood the
mesh with NACKs — the node becomes the DoS source. Go straight to Phase 2 (DMs only), and make
failures **silent** (log locally, never transmit).

**(c) §8's brute-force mitigation is itself a lockout DoS.** _"max. 3 Fehlversuche pro 90s, danach
5 Min Sperre"_ means an RF attacker sending 3 bad codes every 90 s keeps the feature permanently
disabled for the legitimate owner, at negligible cost. Combined with (b) this is a cheap permanent
denial of the whole feature. Rate-limit **per source callsign** rather than globally, and never let a
failed attempt from an unknown sender lock out a known one.

**(d) §5 "TOTP GPIO Pin | Zahl | Default 35" — an unvalidated arbitrary GPIO number.** Writing an
arbitrary pin can drive the LoRa module's NSS/RESET/BUSY or a flash pin and hang or brick the node.
Given F5-4 (unauthenticated web parameter writes) and the empty default web password, this is
remotely settable. Validate against a per-board allowlist of safe output pins.

Two further notes: §6 stores the secret as plaintext in NVS and §5 renders it into the web page —
acceptable only once the web surface is fixed (F5-3, F5-4) and a web password is mandatory; and §9's
dependency on NTP means F5-10 (`{CET}` remote clock set) can invalidate TOTP from RF range, so
`{CET}` must be fixed before this ships.

---

### F5-13: OTA has no code-integrity guarantee, and the "amateur radio" exception does not apply to it

- **Files:** `src/safeboot/main.cpp:284` (`ElegantOTA.clearAuth()`), `:67-68` and `:186-187`
  (open `WiFi.softAP(hostname)`), `:31` (180 s window), `:255-263` (`setBootPartition_APP`);
  `src/safeboot/ElegantOTA.cpp:89` (`Update.begin(UPDATE_SIZE_UNKNOWN, …)`), `:98-106` (optional
  client-supplied MD5), `:238`, `:248`; `platformio.ini:177`, `:215` (`-D HTTPCLIENT_NOSECURE`)
- **Severity:** critical
- **Attacker position:** WiFi range (open AP) or LAN.
- **Prior art:** audit #50 records this and marks it `EXCEPTION — Amateurfunk-Regulierung`.
  **I am flagging the rationale, not re-reporting the fact.** Firmware signing does not obscure the
  meaning of any transmission and is not affected by the encryption prohibition that justifies
  #44–#49. This exception should be re-decided on its merits.

There is no signature verification, no secure boot (zero `SECURE_BOOT` hits in `platformio.ini`), and
no trusted-source hash. The only checks that survive are the ESP-IDF `Update` library's `0xE9` image
magic and — **only if the uploader chose to supply one** (`if (request->hasParam("hash"))`,
`ElegantOTA.cpp:99`) — an MD5 the _uploader itself_ computed client-side (`ota.html:254`). That is a
transport-integrity check against the attacker's own bytes; omitting `&hash=` skips it entirely.

Authentication is explicitly removed (`ElegantOTA.clearAuth()`), so every guard of the form
`if(_authenticate && !request->authenticate(...))` (`ElegantOTA.cpp:21`, `:46`, `:201`, `:225-229`)
is a no-op. The transport is plain HTTP (`ota.html:201-202`, `:261`; server on port 80,
`safeboot/main.cpp:20-21`), advertised by mDNS (`:212`).

**Concrete attack chain, no credentials at any step:**

1. Reach the node — the safeboot AP is created with single-argument `WiFi.softAP(hostname)`, which
   is `WIFI_AUTH_OPEN`. It appears automatically after 15 failed STA connects (`main.cpp:182-191`),
   so **deauthing or jamming the node's WiFi forces the open-AP state.**
2. `GET /callfunction/?otaupdate=` on the app firmware (`web_nodefunctioncalls.cpp:31-33` →
   `command_functions.cpp:606-618`) reboots into safeboot — anonymous when `node_webpwd` is empty.
3. `GET /ota/start?mode=fw` **without** `&hash=`.
4. `POST /ota/upload` with any image beginning `0xE9`. Bytes go to flash via `Update.write()`.
5. `onOTAEnd(true)` → `setBootPartition_APP()` (`main.cpp:262`) → auto-reboot into attacker firmware.
   `mode=fs` (`ElegantOTA.cpp:53-61`) also allows overwriting the filesystem partition.

An active MITM on the LAN can equally swap the body of a _legitimate_ update in flight, since the
connection is unauthenticated HTTP with no end-to-end signature.

`-D HTTPCLIENT_NOSECURE` (`platformio.ini:177`, `:215`, both safeboot envs) compiles `HTTPClient`
without any HTTPS support. It is **not** currently a vulnerability — the safeboot build never
instantiates `HTTPClient` (`build_src_filter` is `+<safeboot/*>`, `platformio.ini:198-201`,
`:237-240`), and no HTTP client exists in `src/` at all. It is a size optimisation. The risk it
creates is forward-looking: **if a pull-based updater is ever added to safeboot, HTTPS will be
structurally unavailable and the code will silently be HTTP-only.** Remove the flag or add a comment
recording why it is safe today.

`tools/download_meshcom.py` fetches from GitHub over HTTPS with Python's default certificate
validation (correct — no `_create_unverified_context`), but performs **no checksum or signature
check** on the downloaded `.bin`/`.uf2` (`:24`, `:184`), and its skip-if-exists logic (`:20-21`,
`:180-181`) means a pre-planted file with the right name is silently trusted forever.

**Fix (ranked by cost/benefit).**

1. **Enable ESP32 Secure Boot v2 and sign releases.** This is the only fix that actually closes it,
   and it is orthogonal to radio regulation.
2. Interim: publish a detached SHA-256 per release, embed the release signing public key in
   safeboot, and verify before `Update.end(true)`. Make the hash parameter **mandatory** and reject
   the upload without it.
3. Password-protect the safeboot AP (`WiFi.softAP(hostname, pw)`) and restore `ElegantOTA` auth using
   `node_webpwd`. Being unable to _encrypt RF traffic_ does not require an open WiFi AP.
4. Publish and verify checksums in `tools/download_meshcom.py`; drop the skip-if-exists shortcut or
   re-verify on skip.

---

### F5-14: Any BLE device in range executes arbitrary firmware commands — no pairing, no PIN, no gate

- **Files:** dispatch `src/esp32/esp32_main.cpp:2780-2791` and `src/nrf52/nrf52_main.cpp:1513-1521`;
  the ungated frame handler `src/phone_commands.cpp:523-546` (case `0xA0`); characteristic
  properties `src/esp32/esp32_main.cpp:1608-1630`; security policy `:1587-1593`
- **Severity:** critical
- **Attacker position:** **BLE range** (~10–30 m). No pairing, no bonding, no credential. Shorter
  reach than the LoRa findings, but the resulting control is total.
- **Prior art:** audit #46 accepts `SECMODE_OPEN` as an EXCEPTION, and #19 DE-PRIOs BLE command
  validation with the rationale _"BLE offen per Entscheidung; Risiko: nur fehlerhafte App"_
  (_"risk: only a buggy app"_). **That threat model is understated by the code.** The finding here
  is not "BLE is open" — that is a decision — it is that the accepted worst case (a misbehaving
  paired app) is not the actual worst case (an unpaired stranger reflashing the node).

Three facts compose:

1. **Pairing never happens.** Every encryption/authentication property on both characteristics is
   commented out, and `NimBLEDevice::setSecurityAuth(false, false, false)` (`:1588`) disables
   bonding, MITM protection and LE Secure Connections. Because no ATT attribute requires encryption,
   NimBLE never initiates SMP. The `PAIRING_PIN "000000"` (`configuration_global.h:200`) is dead code
   on ESP32 — the `setSecurityPasskey` call is commented out (`:1590-1593`).
2. **The app-layer PIN gate is off by default and does not cover the command frame.** `bt_code`
   defaults to `0` (`esp32_flash.h:109`), so `phone_commands.cpp:305-311` takes the
   _"No PIN configured, accepting hello without authentication"_ branch. Crucially, the hash check
   guards only case `0x10` (hello). Case `0xA0` — the text/command frame — has **no auth check at
   all** (`phone_commands.cpp:523-546`); it merely sets `hasMsgFromPhone = true`.
3. **The dispatch has no `isPhoneReady` gate:**
   ```c
   if(memcmp(textbuff_phone, "-", 1) == 0)
       commandAction(textbuff_phone, isPhoneReady, true);
   ```
   `isPhoneReady` is passed only to select the _reply channel_ (`command_functions.cpp:126-133`);
   the command body executes unconditionally.

**Concrete attack.** Scan for the Nordic UART service `6E400001-…`, connect (no pairing prompt),
write one value to characteristic `6E400002-…`:
`[len][0xA0]--cleanflash` — or `--setpwd <attacker-psk>`, or `--ota-update` (chaining to F5-13),
or `--txfreq`/`--txpower` (out-of-band transmission, a licence problem for the operator), or
`--passwd none` (opens the net console, F5-6), or `--btcode 0` (permanently disables the PIN gate).
A `:`-prefixed frame instead reaches `sendMessage()` — **arbitrary message injection into the LoRa
mesh under the operator's callsign**, i.e. an unlicensed party transmitting as a licensed one.

Replies are suppressed for an unauthenticated peer (`ble` is false), so the attacker works blind —
that limits _exfiltration_ but not any of the state changes above.

Related false comment: `src/command_functions.cpp:3031` states _"000000 disables BLE security — only
allowed on an already-authenticated connection"_. **No such check exists in the function.**

**Fix.** Gate the dispatch on authentication — the one-line core fix:

```c
if(isPhoneReady == 1 && memcmp(textbuff_phone, "-", 1) == 0)
    commandAction(textbuff_phone, isPhoneReady, true);
```

(same in `nrf52_main.cpp:1513-1521`), and require the `0x10` hello hash before honouring any
state-changing frame. Then either restore `NIMBLE_PROPERTY::WRITE_ENC` or make `bt_code` mandatory
on first boot. Note that BLE link encryption is a _device-management_ control on a short-range
personal link, not an obscuring of amateur radio transmissions — the encryption prohibition does not
reach it. At minimum, delete the misleading comment at `:3031`.

---

### F5-15: One UDP packet overflows a 255-byte stack buffer by up to 251 bytes (nRF52 + Ethernet)

- **File:** `src/nrf52/nrf_eth.cpp:509-518`; reached from `getUDP()` (`:209`), called at
  `src/nrf52/nrf52_main.cpp:1880`
- **Severity:** critical (on affected builds)
- **Attacker position:** anyone who can send a UDP datagram to port 1990 — LAN, HAMNET, or the
  internet if the node is port-forwarded or on a public IP.
- **Applies to:** nRF52 builds with the Ethernet (W5100S) module active. `nrf_eth.cpp` is a diverged
  copy of `udp_functions.cpp` (`docs/code-audit-20260712.md` DRY-21); **the ESP32 copy does not have this bug**,
  which is exactly the divergence risk DRY-21 predicted.

```c
uint8_t config_buf[UDP_CONF_BUFF_SIZE] = {0};          // 255 bytes, on the stack

if (packetSize <= UDP_CONF_BUFF_SIZE && packetSize >= UDP_MSG_INDICATOR_LEN)
{
  memcpy(config_buf, inc_udp_buffer + UDP_MSG_INDICATOR_LEN, packetSize - UDP_MSG_INDICATOR_LEN);
  // fill rest of buffer with 0
  for (int i = 0; i < UDP_CONF_BUFF_SIZE; i++)
  {
    config_buf[packetSize - UDP_MSG_INDICATOR_LEN + i] = 0x00;   // <-- start offset, full-length loop
  }
```

The `memcpy` is correctly guarded. The zero-fill loop is not: it always runs
`UDP_CONF_BUFF_SIZE` (255) iterations but _starts_ at offset `packetSize - 4`. The intended code is
obviously `for (int i = packetSize - UDP_MSG_INDICATOR_LEN; i < UDP_CONF_BUFF_SIZE; i++)` — the loop
variable was meant to be the index, not an offset added to it.

Overflow = `packetSize - 4` bytes. **Any `CONF` datagram longer than 4 bytes overflows.** A 255-byte
one writes `config_buf[251]` … `config_buf[505]` — 251 bytes of zeros straight through the saved
registers and return address of the calling frame.

**Concrete attack.** Send one UDP datagram to port 1990 beginning with the ASCII bytes `CONF`,
padded to 255 bytes. No authentication is required at any point (F5-16). Effect: deterministic stack
corruption — at minimum a crash loop; on ARM Cortex-M with no stack canary and no MPU-enforced
non-executable stack, a controlled reboot into attacker-chosen state is not out of reach. Note the
write is zeros only, which constrains exploitation to overwriting the return address with `0x00000000`
(hard fault) rather than redirecting it — so the realistic impact is **remote, repeatable,
unauthenticated denial of service** rather than code execution.

Follow-on OOB _reads_ in the same block, from unvalidated length bytes:
`int call_len = config_buf[1];` (`:527`), `short_len = config_buf[2+call_len+1]` (`:540`), then
`memcpy(short_arr, config_buf + (2 + call_len + 2), short_len)` (`:542`) — source index reaches 514
in a 255-byte buffer. `inpos` (`:550`) inherits the same unvalidated arithmetic and indexes
`config_buf` at `:552`, `:560`, `:568`. These write attacker/stale bytes into `_longname` and
`shortname` — the node's advertised identity.

**Fix.** Correct the loop bound (one line), then range-check `call_len` and `short_len` against
`packetSize` before use. Add the boundary tests `codequality-rules.md` §19 requires (0, 1, max,
max+1). Longer term this is the case for DRY-21: unify `nrf_eth.cpp`'s UDP handler with
`udp_functions.cpp` so a fix cannot land on only one side.

---

### F5-16: A spoofed UDP datagram transmits arbitrary frames on LoRa and can knock the node off WiFi

- **Files:** `src/udp_functions.cpp:112` (entry), `:350-355` (LoRa TX injection), `:131` + `:400-404`
  (WiFi disconnect), `:186` (ignored `decodeAPRS` return), `:373-395` (heartbeat spoof);
  `src/extudp_functions.cpp:197` (second injection path)
- **Severity:** high
- **Attacker position:** anyone who can send a UDP datagram to port 1990 (or 1799 with `--extudp on`)
  — LAN, HAMNET (`44.0.0.0/8`, see `udp_functions.cpp:876`), or the internet.

**There is no source-address validation anywhere.** The node knows its server's address
(`node_hostip`, set at `udp_functions.cpp:881-929`) and never compares it against the sender. The
only "validation" is a 4-byte plaintext magic (`"GATE"`/`"BEAT"`/`"CONF"`, `:142-145`) plus the APRS
FCS — a plain additive checksum, forgeable by construction. There is no shared secret, no HMAC, no
nonce and no sequence number. **Server responses are trusted absolutely.**

**Attack A — transmit on the operator's licence.** Send `GATE` + a well-formed APRS frame to
UDP/1990:

```c
ringBuffer[iWrite][0] = size;
ringBuffer[iWrite][1] = 0xFF;                       // no retransmission for UDP relay messages
memcpy(ringBuffer[iWrite] + 2, convBuffer, size);   // :350-352
...
addTxRingEntry("udp_rx");                           // :355
```

The frame is queued for RF transmission, and at `:216-217` the node **stamps its own callsign into
the path** first. An unauthenticated internet host makes a licensed amateur station transmit content
of the attacker's choosing under the operator's callsign. `extudp_functions.cpp:197`
(`sendMessage(val, strlen(val))`) is a second, simpler path to the same outcome on port 1799.

**Attack B — one-packet WiFi drop.** `:121-131` counts consecutive zero bytes; more than `MAX_ZEROS`
routes to `:400-404` → `resetMeshComUDP()` → `Udp.stop(); WiFi.disconnect(true, true);`
(`udp_functions.cpp:1000-1013`). A single datagram of zeros disconnects the gateway from WiFi.
Repeat to keep it offline.

**Attack C — silence the "server lost" watchdog.** Both the `BEAT` branch (`:373-388`) _and_ the
catch-all unknown-indicator branch (`:390-395`) reset `last_upd_timer` and `hb_warn_logged`. Any junk
datagram therefore convinces the node its backhaul is healthy — so an attacker who blackholes the
real server can suppress the operator's only indication that it is gone.

**Contributing defect:** `:186` calls `decodeAPRS(convBuffer, lora_tx_msg_len, aprsmsg);` and
**discards the return value**, so a frame that fails callsign validation or FCS still proceeds with a
default-initialised `aprsmsg`. The nRF52 copy gets this right (`nrf_eth.cpp:299-301` checks
`if(msg_type_b_lora > 0)`) — another DRY-21 divergence, this time with ESP32 on the losing side.

**Fix.**

1. Drop datagrams whose source address is not the configured server / external peer. This is the
   single highest-value change on this surface and it is a few lines at each of the three entry
   points.
2. Check `decodeAPRS`'s return value at `udp_functions.cpp:186` (match `nrf_eth.cpp:301`).
3. Do not let unknown-indicator packets reset the heartbeat timer (`:390-395`).
4. Require more than a zero-run to trigger `resetMeshComUDP()`.
5. Long term: authenticate the backhaul (the node already links mbedtls for the net console's
   HMAC-SHA256 — the same primitive and key material would cover UDP at near-zero Flash cost).

---

## Prior-art findings verified as STILL UNFIXED

Not re-reported in detail — but confirmed live in the current tree, and the RF-reachable ones are
the highest-priority work items alongside F5-1..F5-3.

| ID                                                         | Where                                                             | Verified state                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| ---------------------------------------------------------- | ----------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **SEC-01** `{MCP}` bypass                                  | `loop_functions.cpp:2057`                                         | **Live.** `bool bpass=true;` unchanged. See F5-1 — the proposed fix is insufficient.                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| **SEC-02** `printfdeb` format-string injection             | `printfdeb_functions.cpp:118`                                     | **Live.** Still `Serial.printf(temp);`. RF-reachable: received message text reaches it via `%s` at `lora_functions.cpp:471` and `printBuffer_aprs`. One-line fix (`Serial.printf("%s", temp)`); this should ship first — it is the cheapest high-severity fix in the repo. Note also the `malloc` at `:104` sits in the radio receive path, violating `codequality-rules.md` §1.                                                                                                                                                                            |
| **SEC-05/06** UDP off-by-one OOB write                     | `udp_functions.cpp:67,100,104`; `extudp_functions.cpp:44,223,228` | **Live.** `unsigned char incomingPacket[UDP_TX_BUF_SIZE]` (255) with `read(buf, UDP_TX_BUF_SIZE)` then `incomingPacket[len]=0`. A 255-byte datagram writes index 255.                                                                                                                                                                                                                                                                                                                                                                                       |
| **BUG-10** `handleACK` missing length gate                 | `lora_functions.cpp:202-214`                                      | **Live**, and **worse than reported.** Beyond cancelling an unrelated retransmit, a truncated ACK-type frame also reaches `:261-274`, which enqueues a 12-byte forwarded ACK into the mesh ring. So a **1-byte** RF frame with `payload[0]==0x41` is amplified into a relayed mesh packet built from stale buffer bytes. Note the ACK relay path gates on `(print_buff[5] & 0x7F) > 0` — a **7-bit** hop counter, whereas the data path masks to 4 bits (`aprs_functions.cpp:162`). One-line fix: `if(size < 12) return false;`                             |
| **BUG-12** UDP zero-scan over-read                         | `udp_functions.cpp:121-123`                                       | **Live.**                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| **BUG-13** APRS trailer read past payload                  | `aprs_functions.cpp:394-403`                                      | **Live.** Adds nuance: an attacker can _groom_ the stale bytes by sending a long frame first, then a truncated one, since `RcvBuffer` is not cleared between packets — so the FCS backstop the finding relies on is not unconditional.                                                                                                                                                                                                                                                                                                                      |
| **CONC-19** net-console mutex reassigned without ownership | `net_console.cpp:274-288`                                         | **Live.** See F5-6.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| **SEC-03** BLE `0x55` Wi-Fi config OOB read                | `phone_commands.cpp:549-563`                                      | **Live.** `ssid_len`/`pwd_len` are unvalidated packet bytes; `memcpy(pwd_arr, conf_data+(4+ssid_len), pwd_len)` reads to index 514 in a 300-byte buffer. Adds to the prior report: the over-read bytes land in `meshcom_settings.node_pwd` (`:569`) and are then **rendered into the web setup page** (`web_functions.cpp:1108`) — so adjacent memory is disclosed to anyone who can load that page. On nRF52 `conf_data` is a stack array (`nrf52_ble.cpp:251`), making it adjacent-stack-frame disclosure. Now reachable **without pairing** given F5-14. |
| **SEC-04** URL-decode overflow                             | `loop_functions.cpp:3022`, `:3065`, guard at `:3090`              | **Live.**                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| **BUG-07** BLE `0xA0` length underflow                     | `phone_commands.cpp:526`                                          | **Live.** `txt_msg_len_phone = msg_len - 2` with `msg_len < 2` wraps to 254/255. Confirmed not an OOB write (both buffers are 300 B and zero-filled), so it stays a logic bug — but it is the same frame that F5-14 turns into command execution.                                                                                                                                                                                                                                                                                                           |

**Verified genuinely FIXED (do not re-report):** audit #51 (net-console HMAC plaintext bypass) and
#52 (password in serial log) — `src/net_console.cpp:163-186` now uses `mbedtls_md_hmac` +
`ct_equal()` with no shortcut, and `:167` logs `<***>`. `docs/code-audit-fixes-20260627.md:36-37`
credits `6ba4f3c7`; the claim is accurate.

---

## Non-issues checked

Surfaces examined and found adequately defended — the next pass can skip these.

- **`memcpy(RcvBuffer, payload, size)` — `lora_functions.cpp:407`.** Not an overflow. `RcvBuffer` is
  `UDP_TX_BUF_SIZE * 2` = 510 bytes (`loop_functions.cpp:376`) and `size` is capped at 255 by the
  LoRa PHY. Deliberate over-allocation. (Audit #14 reached the same conclusion.)
- **`decodeAPRS` path parsing — `aprs_functions.cpp:194`, `:308`.** Both path loops are correctly
  double-bounded (`ib < rsize && (ib - 6) < 120`) and filter to printable ASCII. Callsigns are
  regex-validated (`checkRegexCall`). The `<16`-byte minimum-length check (`:136`) and the
  `bSourceEndOk`/`bDestinationEndOk`/`bPayloadEndOk` structural checks are all present and correct.
- **Net-console cryptography — `net_console.cpp:134-186`.** Correct: 16-byte nonce from
  `esp_fill_random()` (hardware TRNG, `:137`), fresh per connection so captured responses do not
  replay; HMAC-SHA256 via mbedtls; response length checked (`strlen(respBuf) == 64`) and hex-decoded
  before comparison; **constant-time comparison via `ct_equal()`** (`:80-85`). The password is never
  transmitted. Mirroring is correctly gated on `s_authenticated` (`:236`, `:250`). The only defects
  are the empty-password bypass (F5-6) and the handshake DoS (F5-8).
- **Web request buffer — `web_functions.cpp:315`, `:346-350`.** `web_header_collect[1024]` is
  correctly bounds-checked while filling.
- **Web pre-auth routing — `web_functions.cpp:384-401`.** When a web password _is_ configured, the
  router genuinely blocks all API routes. I specifically tried the `HTTP/1.0` path (which makes
  `indexOf("HTTP/1.1")` return -1 at `:393`); it falls through to the login page, not the API. No
  bypass found.
- **Path traversal.** None. The only filesystem paths in `src/` are the literals `"/mheard.dat"` and
  `"/mhpath.dat"` (`mheard_functions.cpp:170`, `:202`, `:952`, `:987`). No request parameter reaches
  a filesystem path.
- **Shell / command injection.** There is no shell, so no metacharacter injection. The 18
  `commandAction()` sinks in `web_setup.cpp` interpolate parameters as _arguments_ into fixed command
  strings via bounded `snprintf`. (Caveat, post-auth only: `commandAction` splits its input on every
  `--` occurrence — `command_functions.cpp:143-160` — so a parameter value containing `--` executes
  as an extra command. `manualcommand` already grants strictly more, so this is not an escalation.)
- **`webSetup_setParam` string handling.** No `strcpy`/`sprintf`/unbounded `memcpy` of a request
  parameter anywhere in `web_setup.cpp` or `web_functions.cpp`; the `snprintf`-into-`message_text[200]`
  pattern is bounded throughout. The one defect is the _index_, not the length (F5-4).
- **Credential write paths.** `snprintf`/`strncpy` with explicit `sizeof()` used consistently
  (`command_functions.cpp:2206`, `:3007`, `:3251`; `esp32_main.cpp:953-956`). No overflow found.
  The disclosure problem is F5-7, not memory safety.
- **`--wifiset` / BLE `SW` JSON.** Correctly omits the WiFi PSK (`command_functions.cpp:5022`,
  `:5026`, deliberately commented out).
- **`tools/download_meshcom.py` transport.** HTTPS with Python's default certificate validation; no
  `ssl._create_unverified_context()`. Only the missing checksum is a gap (F5-13).
- **`--setio` CLI bound — `command_functions.cpp:3820`.** `if(iVar >= 0 && iVar <= 7)` is correct.
  It is the web path that lost the check (F5-4).
- **Safeboot OTA exposure window — `safeboot/main.cpp:31`, `:321-327`.** The 180 s auto-reboot back
  to the app partition is a real (if partial) mitigation. It does not close F5-13 because
  `--ota-update` is remotely re-triggerable.
- **Web session expiry — `web_functions.cpp:240-244`.** 4-hour TTL with a bounded 10-entry IP table,
  using the wraparound-safe `(uint32_t)(millis() - t) >= iv` form. The design flaw is that the
  session key is an IP address (F5-3b), not the expiry logic.
- **BLE characteristic write length — `esp32_main.cpp:353-360`.** Correct:
  `if (item.length <= 0 || item.length > MAX_MSG_LEN_PHONE) return;` before `memcpy` into
  `uint8_t data[300]`, and `BleQueueItem item = {}` zero-fills the tail so no stale data leaks
  between writes. The nRF52 equivalent (`nrf52_ble.cpp:251-252`) reads into a sized, zero-initialised
  buffer. **The `MAX_MSG_LEN_PHONE 300` bound in the brief is correctly enforced.** The BLE problem is
  authorisation (F5-14), not length handling.
- **BLE frame length field.** Trusted and never cross-checked against bytes received
  (`phone_commands.cpp:241`, no `len` parameter in the signature at `phone_commands.h:4`) — but
  because both platforms hand `readPhoneCommand` a zero-filled 300-byte buffer and the maximum
  claimable length is 255, **no out-of-bounds write is reachable**. Only case `0x55` reads past the
  end (SEC-03, above).
- **nRF52 settings characteristic — `nrf52_ble.cpp:305-316`.** Rejects wrong-size writes and requires
  struct markers. Correct.
- **BLE PIN (`BPIN`) readback — `command_functions.cpp:4693-4724`.** Correctly gated behind
  `isPhoneReady`, so an unauthenticated peer cannot read it back.
- **UDP → `commandAction()` / reboot / OTA.** **No such path exists.** I looked for one specifically;
  the UDP handlers never reach the command dispatcher. That boundary is correctly held, and it is
  what keeps F5-16 at "high" rather than "critical".
- **Inbound UDP does not set the clock.** `timeClient` (`udp_functions.cpp:63`) is an outbound NTP
  client only. Correctly absent.
- **`getExtern` JSON field validation — `extudp_functions.cpp:176-195`.** Destination 1–9 chars,
  message 1–150 chars, `snprintf(val, 160, …)` into `char val[161]`. Correctly bounded — the missing
  control there is authentication, not length checking.
- **UDP length clamps that ARE correct:** `udp_functions.cpp:151-153` (before the `:174` memcpy into
  the 305-byte `convBuffer`), `:227-228` (before the `:352` memcpy into the 260-byte ring slot),
  `nrf_eth.cpp:224` (`packetSize <= UDP_TX_BUF_SIZE` before the read, into an oversized
  `UDP_TX_BUF_SIZE+5` buffer — which is why nrf_eth avoids the SEC-05 off-by-one),
  `loop_functions.cpp:526-527`, `udp_functions.cpp:1021-1022`, `extudp_functions.cpp:510-516`.
- **`encodeAPRS` output bounds — `aprs_functions.cpp:~615-660`.** The `inext + 10 >= UDP_TX_BUF_SIZE`
  clamp and the final `inext > UDP_TX_BUF_SIZE` clamp hold for every current caller. Worth noting the
  margin is incidental rather than asserted: the clamp permits `6 + 254 = 260` bytes while
  `udp_functions.cpp:306` passes a 255-byte stack buffer, and what actually holds the line is
  `decodeAPRS`'s 120-char path caps plus the ≤251-byte UDP payload. It lands at 254/255 by
  arithmetic coincidence. A `static_assert` and an explicit destination-size parameter would make it
  intentional. No current overflow — do not report as one.

### Minor, noted but not written up as findings

- `printBuffer_ack(char*, uint8_t[], **int8_t** size)` (`loop_functions.h:46`) is called with a
  `uint16_t` (`lora_functions.cpp:211`); `size` 255 becomes -1. Cosmetic today (only affects which
  `printf` branch runs) but violates `codequality-rules.md` §11.
- `checkRegexCall()` uses a single **file-scope** `MatchState regex_call` (`regex_functions.cpp:6`)
  shared by the radio, UDP, web and BLE paths — `codequality-rules.md` §15 requires one parser
  instance per transport. No exploit traced; flagged for the concurrency angle.
- `netConsoleSetPassword()` (`net_console.cpp:262-270`) computes
  `char* end = s_password + strlen(s_password) - 1;` — for an empty password this forms a pointer
  before the array (UB, benign in practice since the loop guard fails immediately). Also, a password
  consisting only of spaces strips to empty and silently enables open access; `--passwd`'s status
  check (`command_functions.cpp:3021`) does detect and report this correctly.
