# WiFi findings — TM-34 (a) … (g)

Date 2026-08-29. Branch `tdeck-partial-refresh-trace`, HEAD `79cc356e`. Backlog item: `BACKLOG.md`
§3.8f **TM-34**. Absorbs **TM-24** (no roaming) and the **TD-01 / TM-11** confirmation run; owns the
upstream fate of **TM-20** (non-blocking `startNetwork()`).

This is the desk half of TM-34: every question answered from the driver source, the shipped
`sdkconfig` and the prebuilt libraries, with the exact file and line so the next reader does not
have to re-derive it. The bench half — the arms, the runner and the acceptance thresholds — is
§9/§10 and has **not been run yet**. Nothing here is a shipped change; §9 is the fix plan.

---

## 1. Summary — one recommendation per question

| #   | Question            | Finding (short)                                                                                                                 | Recommendation                                                                                                | Evidence |
| --- | ------------------- | ------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------- | -------- |
| a   | Driver capabilities | ESP-IDF **v4.4.7** / arduino-esp32 **2.0.17**. 802.11k/v **compiled out**, 802.11r **does not exist** in this IDF. WPA3-SAE in. | Do not plan on 11k/v/r. An IDF 5.3 upgrade buys 11k/v but **loses WPA3-SAE** — not a free trade.              | §2       |
| b   | Scan and selection  | Our hand-rolled scan + best-RSSI + **BSSID pin** re-implements `WIFI_ALL_CHANNEL_SCAN` + sort-by-signal, worse and only once.   | **Delete the hand-rolled selection.** Set `ALL_CHANNEL_SCAN` + `CONNECT_AP_BY_SIGNAL`, `begin(ssid, pwd)`.    | §3       |
| c   | Stall log           | The worst blocker left on `loopTask` is **not** WiFi bring-up (TM-20 fixed that) — it is `hostByName()`: **up to 31 s**.        | Bound/queue DNS off `loopTask`; add a `[WIFI];stall;<site>;ms;<n>` scoped timer at the five known call sites. | §4       |
| d   | SSID-only join      | The pin is the whole reason a failed join cannot fall back to the second Orbi; 8/12 vs 4/12 already measured.                   | Ship SSID-only. Keep our scan **for logging only**, never as an input to `begin()`.                           | §5       |
| e   | Re-connecting       | **`setAutoReconnect` is already `true`** (Arduino default) — TM-24's premise is wrong. We have **three** competing owners.      | One owner. Driver reconnects; our watchdog only escalates after a long grace; harvest `got_ip` by event.      | §6       |
| f   | Roaming             | Without 11k/v/r the only lever is `esp_wifi_set_rssi_threshold()` + `WIFI_EVENT_STA_BSS_RSSI_LOW` → re-`begin()`.               | Phase 2, not now. Fix selection and ownership first; a correct re-`begin()` is already 80 % of roaming.       | §7       |
| g   | Band / AP steering  | All boards are **2.4 GHz only**. We can be pushed off 2.4 but can never follow to 5 GHz. Orbi ch 3 sits under 1/5/6 neighbours. | Never pin a BSSID. Treat first-join `AUTH_EXPIRE` as expected steering behaviour and retry patiently.         | §8       |

**Verdict for TM-20:** the non-blocking `startNetwork()` is not the problem, and reverting it is not
the fix. The problem is that it kept the _old_ selection policy (scan once, pick one BSSID, pin it).
Fix the policy — TM-20 then ships together with it, as one change, per §9.

---

## 2. (a) Driver capabilities

### Versions actually in the build

| Environment                                    | platform                        | framework package | Arduino core | ESP-IDF   |
| ---------------------------------------------- | ------------------------------- | ----------------- | ------------ | --------- |
| all normal ESP32 envs (`[esp32]`, `extends`)   | `espressif32@^6.13.0`           | `3.20017.241212`  | 2.0.17       | **4.4.7** |
| `t_deck_pro`, `t5_epaper`                      | `espressif32@6.5.0`             | `3.20014.231204`  | 2.0.14       | 4.4.6     |
| `vision-master-e290`, `-e213`                  | `espressif32@^6.6.0`            | (same 2.0.x line) | 2.0.x        | 4.4.x     |
| `esp32-safeboot`, `esp32-S3-safeboot` **only** | tasmota/pioarduino `2026.02.30` | `3.3.7`           | 3.3.7        | 5.3       |

`platformio.ini:336` (`[esp32] platform = espressif32@^6.13.0`), `variants/t_deck/platformio.ini:2`
(`extends = esp32`), `variants/t_deck_pro/platformio.ini:2`, `variants/t5_epaper/platformio.ini:2`,
`variants/vision-master-e290/platformio.ini:2`. Version source:
`~/.platformio/packages/framework-arduinoespressif32/tools/sdk/versions.txt` → `esp-idf: v4.4.7`.

The IDF 5.3 framework is used **only** for the two safeboot recovery images
(`platformio.ini:383`, `:423`), never for the running firmware.

### What the WiFi stack can and cannot do

| Capability                      | Status in our build              | Evidence                                                                                                                              |
| ------------------------------- | -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| 802.11k (RRM, neighbour report) | **compiled out** (see below)     | `tools/sdk/{esp32,esp32s3,esp32c3}/sdkconfig`: `# CONFIG_WPA_11KV_SUPPORT is not set` (esp32:1767, s3:2008, c3:1825)                  |
| 802.11v (BTM/WNM)               | **compiled out**                 | same option; `esp_wnm_send_bss_transition_mgmt_query` is defined in **no** library of the 4.4.7 set, and `esp_wnm.h` does not exist   |
| 802.11r (FT)                    | **not present in IDF 4.4**       | no `ft_enabled` field in `wifi_sta_config_t` (`esp_wifi_types.h:273-292`); no `wpa_ft_*` symbol in the library                        |
| MBO                             | off                              | `# CONFIG_WPA_MBO_SUPPORT is not set`                                                                                                 |
| WPA3-SAE (station)              | **on**                           | `CONFIG_ESP32_WIFI_ENABLE_WPA3_SAE=y` (esp32:1039, s3:1248); `sae_prepare_commit`, `sae_derive_pt` are **T** in `libwpa_supplicant.a` |
| PMF                             | capable, not required            | `WiFiSTA.cpp:92-93` — `pmf_cfg.capable = true; pmf_cfg.required = false`                                                              |
| `rm_enabled` / `btm_enabled`    | never set by us — **do not set** | fields exist (`esp_wifi_types.h:284-285`) and are **not** inert: they advertise a capability we cannot honour. See "the wrong turn"   |
| Scan method at `begin()`        | **`WIFI_FAST_SCAN`**             | `WiFiSTA.cpp:119` — `_scanMethod = WIFI_FAST_SCAN`, and we never call `setScanMethod()`                                               |
| Sort method                     | `WIFI_CONNECT_AP_BY_SIGNAL`      | `WiFiSTA.cpp:120` — but **inert**: sorting only applies under `ALL_CHANNEL_SCAN` (`esp_wifi_types.h:212-215`)                         |
| `threshold.rssi`                | `-127` (no floor)                | `WiFiSTA.cpp:91`                                                                                                                      |
| `threshold.authmode`            | `WPA2_PSK` when a key is set     | `WiFiSTA.cpp:96-103`, `_minSecurity` default `WIFI_AUTH_WPA2_PSK` (`WiFiSTA.cpp:118`)                                                 |
| `failure_retry_cnt`             | 0                                | `memset` in `WiFiSTA.cpp:242`; only honoured under `ALL_CHANNEL_SCAN` (`esp_wifi_types.h:290`)                                        |
| Auto-reconnect                  | **`true`** (default)             | `WiFiSTA.cpp:116` — see §6                                                                                                            |
| Config persistence              | **NVS/flash**                    | `WiFiGeneric.cpp:763` `_persistent = true`; we never call `WiFi.persistent(false)`                                                    |
| Modem sleep                     | **`WIFI_PS_MIN_MODEM`**          | `WiFiGeneric.cpp:769`, applied on `STA_START` (`WiFiGeneric.cpp:1046`); we never call `setSleep()`                                    |
| Regulatory default              | `CN`, channels 1-13, 802.11d on  | `esp_wifi.h:638`; `esp_wifi_set_country_code()` available at `esp_wifi.h:1325`                                                        |
| Band                            | **2.4 GHz only, all boards**     | every `board =` in `variants/*/platformio.ini` is ESP32 or ESP32-S3 (no C6, no dual-band part)                                        |

**We set none of these.** `grep -rn "setScanMethod\|setSortMethod\|setMinSecurity\|WiFi.persistent\|setAutoReconnect\|esp_wifi_set_config\|esp_wifi_set_country\|setSleep" src/` returns nothing. Every
value above is an arduino-esp32 default that arrived by accident, not by decision.

### What "compiled out" means here — and the one wrong turn

"Compiled out" above is not "disabled, flip a flag". **PlatformIO does not build ESP-IDF from
source.** It links _prebuilt_ static libraries that Espressif shipped inside the framework package;
the link line in `tools/platformio-build-esp32s3.py:320-327` reads
`-lwpa_supplicant -lnet80211 -lesp_wifi …` out of `tools/sdk/esp32s3/lib/`.

So `tools/sdk/<target>/sdkconfig` is a **record of how Espressif compiled those `.a` files**, not an
input to our build. Adding `-D CONFIG_WPA_11KV_SUPPORT=1` to `platformio.ini` defines a macro for
_our_ `.cpp` files only; the machine code in `libwpa_supplicant.a` is byte-identical afterwards. At
best nothing happens; at worst a struct or header assumption diverges from the binary.

The functionality is genuinely absent, not merely switched off:

- `esp_rrm_send_neighbor_rep_request`, `esp_rrm_is_rrm_supported_connection` and
  `esp_wnm_send_bss_transition_mgmt_query` are defined in **no** library of the 4.4.7 set — verified
  by sweeping every `.a` in `tools/sdk/esp32s3/lib/`, not just the supplicant.
- `esp_rrm.h` and `esp_wnm.h` do not exist in `tools/sdk/esp32s3/include/esp_wifi/include/`. A call
  would not even compile.

**The wrong turn: `rm_enabled` / `btm_enabled`.** These two bits _do_ have live code behind them —
but in `libnet80211.a` (the driver), not the supplicant: `esp_wifi_is_rm_enabled_internal` and
`esp_wifi_is_btm_enabled_internal` are defined there and nowhere else. All they do is read our
config bits and **advertise the capability in the association request**. Setting them therefore
makes the station claim 11k/v to the AP while having no code to receive a neighbour report or act on
a BSS-transition request. On a steering mesh that is actively worse than staying silent: the Orbi
starts sending BTM requests the node drops on the floor. TM-24's "optionally enable 11k/v via
`esp_wifi_set_config` (`rm_enabled`, `btm_enabled`)" is exactly this trap — **do not do it.**

Three real paths exist to get 11k/v, none of them a build flag:

| Path                                                      | Cost                                                                                                                                    |
| --------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| Move the framework to pioarduino/IDF 5.3                  | Its libs **do** define `esp_rrm_*`/`esp_wnm_*` — but that set has no SAE symbol at all, so WPA3 goes. Framework-wide. See next section. |
| Rebuild the Arduino libs with `esp32-arduino-lib-builder` | Works, but every contributor then needs a custom framework package — against the minimal-change/upstream-PR model in `CLAUDE.md`.       |
| Convert to an ESP-IDF component project                   | Not realistic for this codebase.                                                                                                        |

None is on the critical path: with F1/F2 (§9) the driver re-selects the strongest AP on every
reconnect, which covers the case that actually bites. 11k/v only adds "move while still
well-connected" — see §7 for why that is phase 2 at best.

### Would an IDF 5.x upgrade help?

Partly, and it costs something. In the pioarduino/tasmota IDF 5.3 prebuilt libraries
(`tools/esp32-arduino-libs/esp32s3/lib/libwpa_supplicant.a`):

- `esp_rrm_send_neighbor_rep_request`, `esp_rrm_is_rrm_supported_connection`,
  `esp_wnm_send_bss_transition_mgmt_query` are **defined (T)** → 802.11k/v **is** available.
- **No SAE symbol anywhere** in that lib set (`sae_prepare_commit` absent from every `.a`), matching
  `# CONFIG_ESP_WIFI_ENABLE_WPA3_SAE is not set` → **WPA3-SAE is gone**.
- No `wpa_ft_*` → still no 802.11r.

So the upgrade trades WPA3 for 11k/v. Against a WPA2/WPA3-transition Orbi that is arguably a _win_
(it forces the plain WPA2-PSK path), but it is a framework-wide change affecting every board and
every library pin, and it is **not** a prerequisite for anything in §9. Park it as a separate item.

> Corrections to TM-24. "The SDK is built without 802.11k/v … no 11r" is right. Two other parts are
> not: "never calls `setAutoReconnect()`", read as reconnect being off — it is on by default (§6);
> and "optionally enable 11k/v via `esp_wifi_set_config` (`rm_enabled`, `btm_enabled`) once the SDK
> supports it" — those bits only advertise the capability, they do not add it, so setting them makes
> the node claim 11k/v it cannot honour ("the wrong turn", above).

---

## 3. (b) Scan and selection

### What the code does now

`startNetwork()` (`src/udp_functions.cpp:561-670`) resets the radio and fires an async scan:

```
udp_functions.cpp:656   WiFi.disconnect(true, true);     // eraseap → wipes the NVS STA config
udp_functions.cpp:657   WiFi.mode(WIFI_OFF);
udp_functions.cpp:658   WiFi.mode(WIFI_STA);
udp_functions.cpp:663   WiFi.scanNetworks(true);         // async
```

`doWiFiConnect()` (`:754-806`) polls `scanComplete()` at the 1 s cadence of the web timer
(`esp32_main.cpp:3761`), gives up after 10 polls (`:761`) and hands the result to
`wifiBeginFromScan()` (`:675-752`), which walks the scan list, keeps the strongest matching SSID
(`:693-697`) and calls:

```
udp_functions.cpp:745/747   WiFi.begin(ssid, pwd, WiFi.channel(best_idx), WiFi.BSSID(best_idx), true);
```

### Why that is the wrong shape

1. **It is a re-implementation of a driver feature, minus the good part.**
   `WIFI_ALL_CHANNEL_SCAN` + `WIFI_CONNECT_AP_BY_SIGNAL` (`esp_wifi_types.h:212-219`) is exactly
   "scan every channel, sort the SSID matches by RSSI, join the best". The driver does it _inside_
   `esp_wifi_connect()`, on the WiFi task, without touching `loopTask` — and, decisively, it does it
   **again on every reconnect**. Ours runs once and freezes the answer.

2. **The pin defeats the driver's own scan.** Passing `channel` + `bssid` sets `bssid_set = 1`
   (`WiFiSTA.cpp:105-107`); with the default `WIFI_FAST_SCAN` the driver then probes one channel for
   one BSSID. If that AP does not answer, there is no second candidate — the very fallback TD-01
   needs (`BACKLOG.md` §3.8 "BSSID pinning removes the fallback").

3. **`failure_retry_cnt` is unreachable.** The driver's own "try the next AP after N failures"
   (`esp_wifi_types.h:290`) only works under `ALL_CHANNEL_SCAN`. We are on `FAST_SCAN`, so it can
   never fire, whatever value we set.

4. **The scan we do run is short and active-only.** `WiFi.scanNetworks(true)` uses the Arduino
   defaults `show_hidden=false, passive=false, max_ms_per_chan=300` (`WiFiScan.h:34`), i.e. an active
   scan with `scan_time.active = {min: 100, max: 300}` (`WiFiScan.cpp:79-81`). An AP that is slow to
   answer a probe, or that ignores probes from an unknown client (§8), is simply absent from the list.
   The 2026-08-29 boot log shows the consequence: only two ORBI63 BSSIDs found, both on channel 3, at
   **-80 and -85 dBm** (`tools/bench/runs/tdeck_run_20260829-120944.log:85-87`), on a desk where a
   Heltec V3 sees the same SSID at **-47 dBm** (`BACKLOG.md` §3.8, TD-01 update).

5. **Arduino bug worth knowing:** `WiFiScan.cpp:70` declares `wifi_scan_config_t config;` and never
   memsets it, then assigns six of seven fields — `home_chan_dwell_time` (`esp_wifi_types.h:161`) is
   passed to the driver **uninitialised**. Harmless while disconnected; it is not harmless if we ever
   scan while associated (§7). Reason enough to call `esp_wifi_scan_start()` directly there.

### Recommendation (b)

- Set `WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN)` and `WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL)`
  once, before the first `begin()`, and call `WiFi.begin(ssid, pwd)` — **no channel, no BSSID**.
  Selection moves into the driver, off `loopTask`, and is redone on every reconnect.
- Keep `WiFi.scanNetworks(true)` only as a **diagnostic**: log the AP list once per bring-up
  (`[WIFI]...SSID: … CHAN: … RSSI: … BSSID: …` is already the right line) and never let its result
  reach `begin()`. That preserves the field-diagnosis value without the coupling.
- Do **not** lengthen the dwell to "hear every AP". That was the intuitive fix behind reverting
  TM-20 and it is the wrong one: it re-introduces the wait, and the driver's own scan already covers
  all 13 channels. Longer dwell is only worth testing (arm A2b, §10) if A2 does not clear the bar.
- Leave `threshold.rssi` at `-127`. A floor here means "refuse to associate at all", which for a
  remote gateway is worse than a weak link.

---

## 4. (c) Stall log — where the node actually blocks

TM-20 removed ~7 s of `delay()` and the synchronous scan from `startNetwork()` (measured: no loop gap

> 0.7 s at boot, loop max 26 ms). What it did **not** remove is the largest blocking call on the WiFi
> path, which sits one step later.

### The five blocking sites, worst first

| #   | Site                                 | Worst case             | Why                                                                                                        |
| --- | ------------------------------------ | ---------------------- | ---------------------------------------------------------------------------------------------------------- |
| 1   | `WiFi.hostByName()` × 2 per bring-up | **~31 s each**         | `WiFiGeneric.cpp:1572` waits 16 s for `WIFI_DNS_IDLE_BIT`, then `:1578` waits 15 s for `WIFI_DNS_DONE_BIT` |
| 2   | `WiFi.mode(WIFI_OFF)` / `WIFI_STA`   | tens of ms             | `esp_wifi_stop()` / `esp_wifi_start()` via `WiFiGeneric.cpp:1249-1292`                                     |
| 3   | `WiFi.disconnect(true, true)`        | tens of ms + NVS write | `WiFiSTA.cpp:345-367`; `eraseap` writes an empty config to flash                                           |
| 4   | `WiFi.begin(...)`                    | ms + NVS write         | `esp_wifi_set_config` under `WIFI_STORAGE_FLASH` (`WiFiGeneric.cpp:763`)                                   |
| 5   | `timeClient.begin()` / `.update()`   | NTP socket timeouts    | `udp_functions.cpp:798-800`, `:853-878`                                                                    |

Site 1 is the finding. `startMeshComUDP()` runs on `loopTask` and calls `hostByName()` twice on
every successful join — server name and NTP name:

```
udp_functions.cpp:1001, 1009   meshcom.dig-italia.it / meshcom.hamnet.cloud
udp_functions.cpp:1026, 1065   node_ntp
udp_functions.cpp:1051, 1057   meshcom.dig-italia.it / meshcom.oevsv.at
udp_functions.cpp:1072         pool.ntp.org
```

Reached from `doWiFiConnect()` → `startMeshComUDP()` (`udp_functions.cpp:803`) and from
`esp32_main.cpp:1759`. On a WLAN that associates but has no working DNS — captive portal, HAMNET
without a resolver, a router still booting — `loopTask` stops for up to a minute. **LoRa RX is not
serviced during that window**, which is precisely the failure mode TM-34 (c) asks us to make visible.
It is also invisible today: no log line, and the loop instrument
(`src/instrument.cpp:50-67`) only reports an aggregate `max_us`, never the call site.

### Recommendation (c)

1. **Instrument.** A scoped timer that names the site, on all five call sites:
   `[WIFI];stall;<site>;ms;<n>;task;<name>` emitted when the call exceeds a threshold (50 ms
   proposed). Same `;` convention as `[WIFI];event;…` (`udp_functions.cpp:534-553`) so
   `--debug csv` parses it and a human reads it. Cheap, log-only, default-on — a field report then
   names the blocker instead of saying "the node hangs".
2. **Correlate.** Extend `instrument_note_loop_tick()` with a "current section" label so a gap over
   threshold prints `[INSTR-LOOP];gap;ms;N;in;<section>`. That closes the loop between a stall and
   the WiFi event that preceded it.
3. **Fix site 1.** Resolve DNS off `loopTask` or bound it: cache the resolved address in settings,
   resolve at most once per bring-up, skip resolution entirely when the configured target is already
   a literal IP (`hostByName` short-circuits on `IPAddress::fromString`, `WiFiGeneric.cpp:1567` — but
   only for the _configured_ names, and three of the seven sites pass string literals).
4. Log the reason code on every disconnect — already done (`udp_functions.cpp:539`) and it is what
   made TM-11 conclusive. Keep it, and add the BSSID we were on at the time.

---

## 5. (d) SSID-only association

Already measured on DK5EN-14, 12 boots per arm, same hour (`BACKLOG.md` §3.8f, TM-11):

| Arm                                           | First joins | Note                                    |
| --------------------------------------------- | ----------- | --------------------------------------- |
| A baseline (BSSID pinned)                     | 4/12        |                                         |
| B BLE advertising deferred                    | 3/12        | BLE-coexistence hypothesis **refuted**  |
| C join by SSID only (`BENCH_WIFI_NO_BSSID=1`) | **8/12**    | the flag at `udp_functions.cpp:701-706` |

Every failure logs `[WIFI];event;disconnected;reason;2` = `WIFI_REASON_AUTH_EXPIRE`, once 202
`AUTH_FAIL`.

**What changes when the pin goes.** With `bssid_set = 0` and `channel = 0` the driver runs its own
scan inside `esp_wifi_connect()`. Under the _current_ `WIFI_FAST_SCAN` that means "first SSID match
wins" (`esp_wifi_types.h:213`) — better than the pin, because a refusing AP no longer blocks the
second one, but the choice is arbitrary rather than strongest-first. That is exactly the operator's
2026-08-29 concern ("can associate with the weakest BSSID of the mesh"), and it is why (d) must ship
**with** (b): `ALL_CHANNEL_SCAN` + `CONNECT_AP_BY_SIGNAL` restores strongest-first, in the driver,
on every attempt.

`AUTH_EXPIRE` at -47 dBm (Heltec, `BACKLOG.md` §3.8 TD-01 update) is not a link-budget failure. Two
candidate mechanisms remain, and they are **not** distinguished by any measurement so far:

- **Steering** — the Orbi ignores the first auth from an unknown client to see whether it appears on
  5 GHz (§8). Predicts: first join fails, later joins succeed, independent of security mode.
- **WPA2/WPA3 transition** — the AP is WPA2/WPA3 Personal (operator, 2026-08-28); with
  `CONFIG_ESP32_WIFI_ENABLE_WPA3_SAE=y` the station attempts SAE, whose commit/confirm exchange
  timing out presents exactly as `AUTH_EXPIRE`. `sae_pwe_h2e` is left `WPA3_SAE_PWE_UNSPECIFIED` by
  the Arduino `memset` (`WiFiSTA.cpp:242`, field at `esp_wifi_types.h:289`).

Arm A5 in §10 separates them: a second SSID on the same Orbi set to **WPA2-only** costs one router
setting and decides it. Worth doing before any SAE tuning.

### Recommendation (d)

Ship SSID-only, together with (b). Retire `BENCH_WIFI_NO_BSSID` once it is the default. Keep the
scan log so a field report still shows which APs were audible.

---

## 6. (e) Re-connecting — who owns it

### The premise in TM-24 is wrong

`WiFiSTAClass::_autoReconnect = true` (`WiFiSTA.cpp:116`) is the arduino-esp32 default and we never
change it. On `STA_DISCONNECTED` the core runs (`WiFiGeneric.cpp:1076-1094`):

```
if (reason == ASSOC_LEAVE)            -> no reconnect
else if (first_connect)               -> reconnect once, for any reason   (WiFiGeneric.cpp:1079)
else if (autoReconnect && reconnectable(reason)) -> reconnect             (WiFiGeneric.cpp:1084)
   ...
if (DoReconnect) { WiFi.disconnect(); WiFi.begin(); }                     (WiFiGeneric.cpp:1091-1094)
```

and `AUTH_EXPIRE` **is** in the reconnectable set (`WiFiGeneric.cpp:1182`). So the driver has been
retrying all along — with the stored config, which is our pinned BSSID and channel. That is the
concrete mechanism behind "no roaming": not a missing reconnect, but a reconnect that is contractually
forbidden from choosing a different AP.

### Three owners, and how they fight

| Owner                    | Trigger                                                           | Action                                                                                                                 |
| ------------------------ | ----------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| arduino-esp32 core       | any reconnectable `STA_DISCONNECTED`                              | `WiFi.disconnect(); WiFi.begin();` — same pinned config                                                                |
| `checkWifiPing()`        | `WiFi.status() != WL_CONNECTED`, 30 s cadence then 5 s, 5 strikes | `Udp.stop()`, `WiFi.disconnect(true, true)` (`udp_functions.cpp:829`) → then `startNetwork()` at `esp32_main.cpp:2791` |
| the 5-minute `web_timer` | `!node_hasIPaddress && iWlanWait == 0`, every `30 s × 10 = 5 min` | `stopWebserver()` + `startNetwork()` (`esp32_main.cpp:3767-3775`)                                                      |

(A fourth path, `sendMeshComHeartbeat()` → `startNetwork()` at `udp_functions.cpp:1103`, is
unreachable for the `!hasIPaddress` case: its only live caller is gated on `hasIPaddress` being true,
`esp32_main.cpp:3691` and `udp_functions.cpp:1078`. Dead code for this purpose — worth deleting.)

**The concrete defect.** After the boot retry gives up, `esp32_main.cpp:3783` sets `iWlanWait = 0`
and `bAllStarted = true`. `doWiFiConnect()` is only ever called while `iWlanWait > 0`
(`esp32_main.cpp:3777-3779`). So from that moment:

- the driver keeps retrying and **may associate and obtain an IP**;
- nobody harvests it — in STA mode `startMeshComUDP()` has exactly one live caller,
  `doWiFiConnect()` (`udp_functions.cpp:803`), which no longer runs, so `hasIPaddress` stays
  `false`. (The other two call sites are `:624`, the AP-mode branch, and `:1106`, the dead branch
  above.);
- five minutes later `esp32_main.cpp:3775` calls `startNetwork()`, which does
  `WiFi.disconnect(true, true); WiFi.mode(WIFI_OFF)` and **destroys a working connection** to start
  another scan.

That is a plausible mechanism for "the node is absent for five minutes after every power-on" that is
independent of, and additive to, the association failure itself.

**Second-order:** with `_persistent = true` (`WiFiGeneric.cpp:763`), every one of those cycles writes
NVS twice — an empty config on `disconnect(true, true)` and the new one on `begin()`. The pinned
BSSID changes whenever the mesh moves us, so the `sta_config_equal` guard (`WiFiSTA.cpp:251`) does not
suppress it. Unnecessary flash traffic; the SSID lives in our own settings anyway.

### Recommendation (e)

1. **The driver owns reconnect.** Call `WiFi.setAutoReconnect(true)` explicitly — not to change
   behaviour but to make the ownership a decision in our source rather than a default we inherited.
2. **Harvest by event, not by poll.** Move `startMeshComUDP()` onto `ARDUINO_EVENT_WIFI_STA_GOT_IP`
   in `wifiEventLog()` (`udp_functions.cpp:544`) — set a flag there, act on it in the loop. A
   driver-side reconnect then becomes visible instead of being ignored. This also removes the
   `iWlanWait > 0` precondition that creates the blind window.
3. **The watchdog escalates late, and gently.** `checkWifiPing()` must not tear the radio down while
   the driver is still retrying. Give it a grace of ≥ 3 min of continuous `!WL_CONNECTED`, then
   escalate to `WiFi.disconnect(false, false); WiFi.begin();` — keep the config, keep the radio up.
   Full `startNetwork()` (radio off/on + rescan) stays as the last resort after a second grace.
4. **`WiFi.persistent(false)`** once, before the first `WiFi.mode()` call — `wifiLowLevelInit()` reads
   it exactly once (`WiFiGeneric.cpp:693-695`, guarded by `lowLevelInitDone`), so the call site
   matters.
5. Delete the dead `startNetwork()` branch in `sendMeshComHeartbeat()`.

---

## 7. (f) Roaming without 11k/v/r

There is no fast transition and no network-assisted steering to be had (§2). What the station _can_
do, all of it available in IDF 4.4.7:

| Mechanism                 | API                                                                                                                 | Cost                                                                                        |
| ------------------------- | ------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| React to a weakening link | `esp_wifi_set_rssi_threshold(int32_t)` (`esp_wifi.h:1197`) → `WIFI_EVENT_STA_BSS_RSSI_LOW` (`esp_wifi_types.h:655`) | one-shot; must be re-armed after every event (`esp_wifi.h:1186`)                            |
| Re-select the best AP     | `WiFi.begin(ssid, pwd)` under `ALL_CHANNEL_SCAN` + sort-by-signal                                                   | one full-channel scan inside the driver (~1-2 s off-channel)                                |
| Survey while associated   | `esp_wifi_scan_start()` with `home_chan_dwell_time` set                                                             | brief off-channel gaps; **do not** use `WiFi.scanNetworks()` here (§3, uninitialised field) |
| Read the current AP       | `esp_wifi_sta_get_ap_info()` (`esp_wifi.h:505`)                                                                     | free; gives BSSID + RSSI for logging                                                        |

Arduino 2.0.17 does **not** surface the RSSI-low event — `ARDUINO_EVENT_WIFI_STA_*`
(`WiFiGeneric.h:39-46`) has no entry for it. It needs a raw
`esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_BSS_RSSI_LOW, …)`, which coexists fine with
`WiFi.onEvent()`.

The shape that follows: arm the threshold at, say, -75 dBm on `got_ip`; on the event, log
`[WIFI];rssi_low;…` and call `WiFi.begin(ssid, pwd)` again. Because selection now lives in the driver
and sorts by signal, that single call _is_ the roam. If the current AP is still the strongest, the
driver re-selects it and nothing moves.

### Recommendation (f)

**Phase 2, deliberately after §9.** Once (b), (d) and (e) are in, a reconnect already re-selects the
best AP — which covers the case that matters (the AP drops us, or we lose beacons). RSSI-triggered
roaming only adds the "we are still connected but a better AP appeared" case, and it carries a real
risk of ping-ponging on a two-AP mesh where both are within a few dB. Do not ship it without
hysteresis and a rate limit, and do not ship it in the same PR.

---

## 8. (g) Band and AP steering

### Facts

- **Every board in this firmware is 2.4 GHz only.** ESP32 and ESP32-S3 exclusively
  (`variants/*/platformio.ini`); no C6, no dual-band part. Steering is therefore one-directional: the
  AP can push us off 2.4 GHz, and we can never follow to 5 GHz. Any 5 GHz-preferring policy on the AP
  is, from our side, indistinguishable from a broken AP.
- **The bench WLAN is a steering mesh.** ORBI63 = router + satellite, single SSID across 2.4 and
  5 GHz ("Smart Connect"), WPA2/WPA3 Personal, 80 MHz (operator, 2026-08-28). The Mac on the same
  desk is associated on **channel 40 (5 GHz, 80 MHz)**, WPA2/WPA3 Personal
  (`system_profiler SPAirPortDataType`, 2026-08-29) — the same SSID the ESP32 can only reach on 2.4.
- **Both Orbi 2.4 GHz BSSIDs sit on channel 3**, co-channel with each other
  (`BACKLOG.md` §3.8, TD-01 table), under neighbouring networks on channels 1, 5 and 6
  (`system_profiler`, same run). Channel 3 overlaps all three.
- **802.11v BTM cannot reach us.** With the supplicant's WNM compiled out (§2), a BSS-transition
  request is not answered and not acted on. An AP that expects BTM to move a client has exactly one
  remaining lever against us: deauthenticate, or ignore.

### Reading of the failure

`AUTH_EXPIRE` on the _first_ association at -47 dBm, succeeding on a later retry, on two different
boards, is the signature of an AP that **declines the first attempt on purpose**. Two mechanisms fit
and are not yet separated (§5): band steering (ignore 2.4 GHz auth from an unknown client, see if it
shows up on 5 GHz) and WPA2/WPA3 transition (SAE exchange timing out). Both are AP-side; neither is
fixed by anything we do to the scan.

What _is_ ours to fix: the response. Today a refused first attempt costs five minutes, because the
pin removes the alternative AP and the give-up path stops harvesting (§6). With SSID-only join,
driver-owned reconnect and event-driven `got_ip`, a refused first attempt costs one retry.

### Recommendation (g)

- **Never pin a BSSID.** There is no case in a multi-AP WLAN where pinning is right, and in a
  single-AP WLAN it buys nothing the SSID does not already give. The question TM-34 asks ("is
  pinning the mesh's 2.4 GHz BSSID ever the right call?") answers itself once the driver selects by
  signal on every attempt.
- **Treat a refused first join as normal.** Retry patiently rather than escalating to a radio reset;
  the "6 × connection error → full radio reset" pattern (`BACKLOG.md` §3.8) is the node punishing
  itself for the AP's policy.
- **Do not add a `--wifiband` or channel-preference setting.** With 2.4-only hardware there is
  nothing to prefer.
- **Document the AP-side workaround for operators**: on a Netgear Orbi, disabling Smart Connect (or
  giving 2.4 GHz its own SSID) removes the steering entirely. That is a note for the field, not a
  firmware change — but it belongs in the release notes with this fix.

---

## 9. Fix plan

One PR, firmware-only, against upstream `DEV`, **separate from the T-Deck PR** (`BACKLOG.md` §4.1),
with a German per-file description per `CLAUDE.md`. TM-20's non-blocking `startNetwork()` travels
with it — it is the same change once the policy is right.

| ID  | Change                                                                                                                                                                                           | Files                                           | Risk   | Ships with          |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------- | ------ | ------------------- |
| F1  | `WiFi.persistent(false)`, `setAutoReconnect(true)`, `setScanMethod(WIFI_ALL_CHANNEL_SCAN)`, `setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL)`, `setSleep(false)` — once, before the first `WiFi.mode()` | `udp_functions.cpp` `startNetwork()`            | low    | PR                  |
| F2  | `WiFi.begin(ssid, pwd)` — drop channel and BSSID; scan becomes log-only; delete `BENCH_WIFI_NO_BSSID`                                                                                            | `udp_functions.cpp` `wifiBeginFromScan()`       | low    | PR                  |
| F3  | `startMeshComUDP()` driven by `STA_GOT_IP` instead of the `iWlanWait` poll; remove the blind window                                                                                              | `udp_functions.cpp`, `esp32_main.cpp:3759-3812` | medium | PR                  |
| F4  | Watchdog grace: `checkWifiPing()` escalates only after ≥ 3 min, and to `disconnect(false,false)+begin()`, not a radio cycle                                                                      | `udp_functions.cpp`, `esp32_main.cpp:2779-2796` | medium | PR                  |
| F5  | `[WIFI];stall;<site>;ms;<n>` scoped timer on the five blocking sites; loop-gap section label                                                                                                     | `udp_functions.cpp`, `instrument.{h,cpp}`       | low    | PR (log-only)       |
| F6  | Bound DNS: literal-IP short circuit, resolve once per bring-up, cache in settings                                                                                                                | `udp_functions.cpp` `startMeshComUDP()`         | medium | PR                  |
| F7  | Delete the dead `startNetwork()` branch in `sendMeshComHeartbeat()`                                                                                                                              | `udp_functions.cpp:1096-1117`                   | low    | PR                  |
| F8  | RSSI-threshold roaming (`esp_wifi_set_rssi_threshold` + raw event handler + hysteresis)                                                                                                          | `udp_functions.cpp`                             | high   | **later**           |
| F9  | Framework move to IDF 5.x for 11k/v (costs WPA3-SAE)                                                                                                                                             | `platformio.ini`, all variants                  | high   | **later, own item** |

Order: F1+F2 first (they are the measurement), then F3+F4, then F5+F6, then F7. F8/F9 are separate
backlog items, not this PR.

**Do not** revert TM-20. **Do not** add dwell-time tuning, an RSSI floor, or a band preference —
§3, §2 and §8 respectively. **Do not** set `rm_enabled` / `btm_enabled` hoping to enable 11k/v: the
bits advertise the capability without adding it (§2, "the wrong turn"). There is no build flag that
brings 11k/v back — the libraries are prebuilt, so it takes F9 or a custom lib-builder package.

---

## 10. Bench protocol

Runner: `tools/bench/experiments/bootloop.py` (written with this doc). Reset by opening the USB
port — every ESP32 node reboots on port open (`RESUME.md`, "Harnesses"), so no relay is needed.
Node: **DK5EN-14**, T-Deck Plus, `/dev/cu.usbmodem1101`. 75 s per boot, 24 boots per arm ≈ 30 min.
Run all arms in the same session; the AP's behaviour varies by hour, which is why the TM-11 arms
were deliberately run within one hour.

### Arms

| Arm | Build                                                               | Question                                                  |
| --- | ------------------------------------------------------------------- | --------------------------------------------------------- |
| A0  | HEAD, unmodified                                                    | baseline                                                  |
| A1  | `-D BENCH_WIFI_NO_BSSID=1`                                          | (d) — 24-boot confirmation of the 8/12 result             |
| A2  | A1 + F1 (`ALL_CHANNEL_SCAN` + sort-by-signal + `persistent(false)`) | (b)                                                       |
| A3  | A2 + `WiFi.setSleep(false)`                                         | (a) modem sleep as a factor                               |
| A4  | A3 + F3/F4 (event-driven `got_ip`, watchdog grace)                  | (e)                                                       |
| A5  | A0 against a **WPA2-only** test SSID on the same Orbi               | (d)/(g) — separates SAE from steering; one router setting |
| A2b | A2 with `scan_time.active.max = 1000`                               | only if A2 misses the bar (§3)                            |

### Metrics, per boot

Parsed from the serial log; all markers already exist except `[WIFI];stall`.

- first join succeeded within 20 s of `[WIFI]...Wait connect` — yes/no (the headline number)
- `[WIFI];event;connected;ms;N` and `[WIFI];event;got_ip;ms;N`
- `[BOOT];ready;ms;N;ip;X`
- every `[WIFI];event;disconnected;reason;R` with R, counted per boot
- chosen BSSID and the full scan list (`[WIFI]...SSID: … BSSID: …`)
- `[INSTR-LOOP];…;max_us` at ready
- `[WIFI];stall;…` lines (A4 onward)

### Acceptance

| Criterion                              | Bar                       |
| -------------------------------------- | ------------------------- |
| first join within 20 s                 | **≥ 22/24** (A0 was 4/12) |
| median time to `got_ip`                | ≤ 6 s                     |
| loop max at ready                      | no worse than A0          |
| boots where the node is offline > 60 s | 0                         |
| `[WIFI];stall` over 500 ms             | 0 on a healthy WLAN       |

### Cross-board regression before the PR

Heltec V3 (DK5EN-93), T-Beam v1.2 (DK5EN-92): 6 boots each on the winning arm, `--info` answers,
LoRa TX/RX both directions, net console on 2323 reachable. The RAK4631 (DK5EN-90) has no WiFi and is
unaffected — but build it, because `udp_functions.cpp` is shared.

---

## 11. What this doc does not settle

- **No bench measurement in this document is new.** §5's 12-boot table is TM-11's, re-used. Every
  arm in §10 is unrun. The findings above are source-and-library evidence; that is strong for (a),
  (b), (c) and (e), and it is _not_ a substitute for the boot statistics that decide (d), (f), (g).
- **`AUTH_EXPIRE`: steering or SAE?** Not decided. Arm A5 decides it, and it should run first — it is
  the cheapest arm and it changes what F1-F4 have to survive.
- **The T-Deck's -80 dBm vs the Heltec's -47 dBm at the same desk** (`BACKLOG.md` §3.8) is a 30 dB
  hardware difference that nothing here explains, and it is not an association problem. Separate item.
- **IDF 5.x** (F9): the 11k/v-for-WPA3 trade is documented in §2 but not evaluated against the rest
  of the firmware (NimBLE, LVGL, library pins).
