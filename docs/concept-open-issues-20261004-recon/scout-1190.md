<!-- Recon report by a read-only Sonnet scout, 2026-10-04, fork-dev a9e64cf6. Line references are a snapshot; re-verify before editing. -->

# scout-1190: MTU/MSS for MeshCom nodes + Safeboot (read-only recon, fork-dev, HEAD 5899e12b)

BLUF: The RAK4631/W5100S web-server MSS cap already exists and is upstream (node_ethmtu / --ethmtu / webApplyEthMss, #1183, 50fdfe65,
in upstream/dev too). Nothing equivalent exists for ESP32 or Safeboot. On ESP32 TCP_MSS is compile-time (1436, prebuilt lwIP);
the only runtime lever is lwIP netif->mtu (effect on advertised MSS unverified, needs a SYN-ACK capture). UDP paths are all
< 730 B, so no fragmentation at any tunnel MTU >= 1280. App firmware has no HTTP client and no TCP client except the optional external radio.

## Findings

### 1. TCP/IP consumers (app firmware)

| Consumer | Platform | Proto | Library / symbol | Ref |
| Gateway to server (port 1990) | ESP32 WiFi | UDP | WiFiUDP Udp | src/udp_functions.cpp:77 |
| Gateway to server | ESP32 ETH (T-ETH-ELITE, T-Connect-Pro) | UDP | ETHClass2 + EthernetUdp types (lwIP underneath, unverified) | src/udp_functions.cpp:35, src/esp32/esp32_eth.h:6,26 |
| Gateway to server | nRF52 RAK4631 | UDP | RAK13800_W5100S EthernetUDP Udp (HW socket) | src/nrf52/nrf_eth.cpp:31,909,1045 |
| NTP (port 123, local 1337) | both | UDP | own ntp_async on the gateway UDP socket (no NTPClient) | src/ntp_async.h:20-24, src/ntp_async.cpp:58 |
| Extern UDP telemetry (EXTERN_PORT) | ESP32 / nRF52 | UDP | WiFiUDP / EthernetUDP UdpExtern | src/extudp_functions.cpp:58-65 |
| Web GUI (port 80) | ESP32 | TCP | WiFiServer/WiFiClient (Arduino WiFi), sync-polled | src/web_functions/web_commonServer.h:14-25, web_functions.cpp:48 |
| Web GUI (port 80) | nRF52 RAK | TCP | EthernetServer/EthernetClient (W5100S) | web_commonServer.h:31-40 |
| mDNS (ESP32 only) | ESP32 | UDP mc | ESPmDNS | web_functions.cpp:226 area, web_commonServer.h:12 |
| Net console 2323 (HMAC) | ESP32 only | TCP | raw lwIP BSD sockets, listen backlog 1 | src/net_console.cpp:368-384, net_console.h:32 |
| KISS TCP 8001 | ESP32 only | TCP | raw BSD sockets | src/kiss_functions.cpp:601-618, configuration_global.h:270 |
| External radio bridge (client) | ESP32 only (esp32-external-radio env) | TCP | BSD ::connect to build-time host:port | src/esp32/external_radio_glue.cpp:74-84 |
| DNS / DHCP | ESP32 lwIP; nRF52 W5100S lib | UDP | WiFi.hostByName async (udp_functions.cpp:485-569); Ethernet.begin (nrf_eth.cpp:270) | |
| esp_sntp | t5-epaper | - | header include only, no sntp_* call found | src/t5-epaper/t5epaper_main.h:12 |

- Not found in src/: HTTPClient, esp_http_client, WiFiClientSecure, mcmap/aprs.fi uploads (server-side only), ArduinoOTA, Update.h (outside src/safeboot), any TCP client on nRF52.
- App OTA = reboot into Safeboot (no Update.h in app).
- W5100S has 4 hardware sockets (web_functions.cpp:95-103): gateway UDP, extern UDP, web LISTEN, 1 accepted client.

### 2. UDP sizes vs a ~1400 tunnel MTU

- UDP_TX_BUF_SIZE 255 (configuration_global.h:272); UDP_CONF_BUFF_SIZE 255 (:273); UDP_PORT 1990 (:266).
- Gateway RX buffers: ESP32 incomingPacket[255], read capped 254 (udp_functions.cpp:81,158); nRF52 inc_udp_buffer[255+5], read 255 (nrf_eth.cpp:49,476); handleUdpFrame_esp32 takes buffer[500] (udp_functions.h:47). A server datagram > 255 B is truncated, not fragmented-related.
- Gateway TX: hb/dt buffers UDP_TX_BUF_SIZE+50 = 305 B max (udp_functions.cpp:83); KEEP/DATA/HB built at udp_functions.cpp:1264-1324. IP packet <= 333 B.
- Extern UDP TX: EXTERN_MSG_JSON_BUF 700 (extern_msg_json.h:23; worst frame ~640 B, extudp_functions.cpp:755-762), c_tjson[500] (:578), c_json[400] (:1009), c_json[160] (:900). Max ~728 B IP packet. RX buffer incomingExtPacket[255], read 254 (extudp_functions.cpp:68,479).
- Verdict: nothing exceeds 730 B; a 1400 (or even 1280) tunnel MTU never fragments node UDP. Only MTU < ~760 would touch extern JSON.
- ESP32 lwIP has CONFIG_LWIP_IP4_FRAG=y but "CONFIG_LWIP_IP4_REASSEMBLY is not set" (both frameworks): inbound fragmented datagrams would be dropped; irrelevant at these sizes.

### 3. ESP32 runtime knobs vs compile-time

- platformio.ini:1111 `platform = espressif32@^6.13.0` -> installed packages/framework-arduinoespressif32 = 3.20017.241212 (arduino-esp32 2.0.x, ESP-IDF v4.4.7; tools/sdk/versions.txt). `[env:esp32-safeboot]` pins tasmota platform 2026.02.30 (platformio.ini:1203) -> framework package.json 3.3.7+sha.b3b492ff, libs built on ESP-IDF 5.3.4 (tools/esp32-arduino-libs/esp32/dio_qspi/include/sdkconfig.h header).
- COMPILE-TIME ONLY (prebuilt liblwip.a, sdkconfig in the framework): CONFIG_LWIP_TCP_MSS=1436 (4.4.7: tools/sdk/esp32/sdkconfig:1298, esp32s3:1509; 5.3.4: esp32-arduino-libs/esp32/sdkconfig:1806), TCP_MSL, TCP_WND_DEFAULT 5760 / SND_BUF 5744-5760, MAXRTX 12. lwipopts.h:439 `#define TCP_MSS CONFIG_LWIP_TCP_MSS`. A `-D CONFIG_LWIP_TCP_MSS` in platformio.ini would be shadowed by sdkconfig.h (memory build-flag-shadowed-by-sdkconfig) and lwIP is not recompiled anyway. Needs a custom IDF lib build: out of scope.
- RUNTIME candidates:
  a) lwIP `netif->mtu` (lwip/netif.h:354 `u16_t mtu`): reachable via tcpip_adapter_get_netif() (4.4.7: tcpip_adapter.h:95) / esp_netif_get_netif_impl() (esp_netif_net_stack.h:49, both IDFs). lwIP caps the effective send MSS at netif mtu - 40 (tcp_eff_send_mss_netif). Must be written under the tcpip core lock. Whether the SYN/SYN-ACK ADVERTISED MSS (what matters for inbound browser POST/OTA upload) follows the netif MTU is NOT verifiable here: lwIP .c sources are not in the tree (headers + liblwip.a only). Must be proven on bench with a SYN-ACK pcap.
  b) No esp_netif_set_mtu / netif_set_mtu symbol in either framework's headers (grep empty). No TCP_MAXSEG in lwip headers. WiFiClient exposes only setNoDelay/setOption (WiFiClient.h:91-94); WiFiServer setNoDelay (WiFiServer.h:52). AsyncTCP 3.2.14 has read-only getMss() (AsyncTCP.h:130) and a protected pcb() (:192); no setter.
  c) lwIP has no path-MTU discovery; DHCP option 26 (interface MTU) is not consumed by lwIP DHCP.
- Existing ESP32 hook points: WiFi event handler wifiEventLog (udp_functions.cpp:365-395, GOT_IP at :380), WiFi init wifiInitOnce (:403-412), ETH start esp32_eth.cpp:14-122.

### 4. nRF52 / W5100S

- Library: `https://github.com/icssw-org/RAK13800-W5100S` UNPINNED (platformio.ini:84), copy at .pio/libdeps/wiscore_rak4631/RAK13800-W5100S/src. T114/T-Echo envs carry the same lib dir; no board uses it other than via the RAK path in the app.
- Library has NO MSS API of its own: socket.cpp:63-107 socketBegin() writes MR, IR, PORT, then Sock_OPEN; never touches Sn_MSSR. Register accessor only: w5100.h:282 `__SOCKET_REGISTER16(SnMSSR, 0x0012)` (generates readSnMSSR/writeSnMSSR). Per-socket only, no global MSS.
- App-side fix (already in HEAD and upstream/dev): src/web_functions/web_functions.cpp:86-144 webApplyEthMss() (RAK only, `#if defined(BOARD_RAK4630)` :63). MSS = clamp(node_ethmtu,1280..1500) - 40 (:88-93). Pass A pre-writes MSSR on every CLOSED socket and closes a port-80 LISTEN socket holding a stale MSSR (:107-124); pass B re-writes then web_server.begin() (:127-143). Called after begin (:265, `server_port[1]==0` guard :261) and after every available() (:323). Default MTU 1500 -> MSS 1460 = previous chip default.
- Commit 50fdfe65 (2026-10-01) touched: docs/d1-04..., docs/settings-registers.md, command_functions.cpp, config_json.h, meshcom_settings.h, web_functions.cpp, web_setup.cpp, test/golden/settings_schema_lint.py, test_command_setters, test_config_json (10 files, 297+/10-).
- Latch behaviour: Sn_MSSR latched at Sock_OPEN (datasheet 3.2.9; memory w5100s-mssr-latches-at-open: write-after-LISTEN left first two connections at 1460). UDP sockets open before the web socket (nrf_eth.cpp:909/1045) and may inherit MSSR if they get a CLOSED slot pre-written; harmless, UDP ignores it.
- Reporter-side caveat: chip ping responder corrupts echo byte 119 (memory), so ping is no MTU instrument; the reporter's MTU claim was unconfirmed.

### 5. Safeboot

- Env: esp32-safeboot and esp32-S3-safeboot, Tasmota platform 2026.02.30, ESPAsyncWebServer 3.3.23 + AsyncTCP 3.2.14 (platformio.ini:1200-1260); build_src_filter = src/safeboot/*, esp32/esp32_flash.{h,cpp}, settings_schema.cpp (:1235-1239).
- Code: WiFi.h + esp_wifi.h + ESPAsyncWebServer + ESPmDNS (src/safeboot/main.cpp:6-12); AsyncWebServer webServer(80) (:43); local ElegantOTA copy in async mode (src/safeboot/ElegantOTA.cpp:26,249-301, upload handler = browser POST, INBOUND-heavy; flagged ELEGANTOTA_USE_ASYNC_WEBSERVER=1 platformio.ini:1220); Preferences (:22).
- Settings: wifiConnect() calls init_flash() (main.cpp:217), the SAME function as the app, reading NVS namespace "Credentials" through the shared settings_schema (esp32_flash.cpp:319,461; walk via settings_schema.cpp). It already uses node_ssid, node_pwd, node_sset/sset2, node_call, node_ownip/gw/ms/dns (main.cpp:219-299). => ANY new schema field (e.g. node_ethmtu, 1500 default) is loaded into meshcom_settings in Safeboot with NO extra plumbing; Safeboot just must read the field.
- Apply point: in wifiConnect() after WiFi.mode(WIFI_STA)/WiFi.begin (main.cpp:274-329) and again on GOT_IP (safebootWifiEventLog, main.cpp:194, registered :240) and AP start (:249-253), before webServer.begin() in setup() (:467+). AsyncTCP gives no MSS setter; only the netif->mtu route (3a) applies; S3 and classic ESP32 images are separate binaries (safeboot.bin / safeboot-s3.bin in repo root, rebuilt by tools/safeboot.py post script).
- Note: safeboot-s3.bin/safeboot.bin are committed binaries; boards flash them from the repo root (platformio.ini:1198). A Safeboot code change requires rebuilding both images (memory safeboot-bin-rebuild-before-v3-flash).

### 6. Settings plumbing template (use node_ethmtu, 50fdfe65; TZ-01 bcf8f083 is the string-valued, heavier template)

Files for a new int setting `node_xxx` with range:

1. src/meshcom_settings.h:174: `M(int, node_ethmtu, 1500)` row (X-macro struct body; name = NVS key, max 15 chars).
2. src/config_json.h:349: `X("node_ethmtu", CFG_INT, node_ethmtu, 1280.0, 1500.0, CFG_NOESC)`: one row gives JSON export AND persistence (settings_schema.cpp:133 expands CFG_FIELD_LIST); no change in settings_schema.h/.cpp needed. ESP32 NVS via esp32_flash.cpp (namespace "Credentials", I32 dispatch :109-112); nRF52 keyed file /MeshCom-Settings-Store on InternalFS (settings_store_nrf52.cpp:65).
3. src/command_functions.cpp: parser `commandCheck(msg_text+2,"ethmtu ")` :3574-3600 using cmdStoreInt(...,min,max,&iVar) (CMD_SET_NAN / CMD_SET_RANGE reject, save_settings()); --help line :1226; --info line :6481.
4. src/web_functions/web_setup.cpp: setparam handler :138-148 (calls commandAction "--ethmtu %s"), getparam :836; web_functions.cpp:2889 `_create_setup_textinput_element("ethmtu",...)`.
5. test/golden/settings_schema_lint.py:246 (classification row + expected-count text :362), docs/d1-04-settings-field-triage-20260912.md:122, docs/settings-registers.md:239; tests: test/test_command_setters, test/test_config_json.

- BLE settings v1 on nRF52 is a FROZEN image (src/nrf52/ble_settings_v1.h static_asserts; ble_settings_v1.cpp translates member by member): node_ethmtu is NOT in it and was not added. ESP32 BLE needs nothing for a CFG row. node_ethmtu is also absent from BLE SN/SN1 registers (grep: no hit in src/ble*).
- Gap: --ethmtu help/parse/web are RAK-only (#if BOARD_RAK4630 on help :1225, info :6480, web set :138-146) but the setting itself is stored on all boards ("auf ESP32 ohne Wirkung", settings-registers.md:239). Generalizing = drop the #if in those three places (and web_functions.cpp:2888-2890, the web field is #if BOARD_RAK4630) and add the ESP32 apply hook.

### 7. Risks

- Throughput: MSS 1460 -> 1240 (MTU 1280) is -15% payload per segment, header overhead 2.7% -> 3.2%; ESP32 TCP_WND 5760 = 4 x 1436 or ~4.6 x 1240, still >= 4 segments in flight; WiFi/flash-write limits (Safeboot Update.write, web GUI page chunks) dominate. Expect single-digit-% change; measure on bench (OTA upload of a ~1.2 MB image before/after).
- Direction matters: node TX MSS = min(peer MSS option, node cap) (outbound pages: web GUI). Inbound segment size (browser->node OTA POST, big forms) is set by the node's ADVERTISED MSS. W5100S: MSSR sets the advertised value (bench-measured 1240/1260/1360). ESP32: advertised value stays 1436 unless netif mtu affects it (unverified, 3a). Peers that clamp MSS at the tunnel (typical VPN/PPPoE) make the setting unnecessary; it matters when MSS clamping and ICMP frag-needed are both absent.
- lwIP sets no DF on its TCP segments (unverified; no source), IP4_FRAG=y lets ESP32 fragment oversize TX, but ESP32 cannot reassemble inbound fragments, so TX is more forgiving than RX on ESP32; the reverse of the W5100S (DF set, no PMTUD, per #1183 commit text).
- W5100S latch timing: MSSR must be written while the socket is CLOSED, before Sock_OPEN; setting changes only apply to the next OPEN of that index; a live change closes/re-opens the port-80 LISTEN socket (drops pending clients for ms). Boot order: Ethernet.begin + Udp.begin run before startWebserver(); the pre-write covers indexes still CLOSED. Settings are loaded at boot before any socket, so first-connection-after-boot is correct (bench-proven 60/60).
- ESP32: the netif MTU must be applied before the first TCP socket accepts (web server begin, net console/KISS listen, external radio connect) AND again on every re-association (netif recreation on WiFi mode change/AP<->STA, udp_functions.cpp:643-688 WiFi.mode(OFF)/(STA) cycles reset it). Apply in the GOT_IP/ETH_GOT_IP event, not once at boot. Listening PCBs created earlier keep their cached pcb->mss (lwIP computes at listen/accept; accepted PCB derives from the netif at SYN time, unverified).
- Lowering netif MTU also lowers the UDP fragmentation threshold on the node: with 1280 min no node datagram (<=728 B) fragments. DNS/NTP/mDNS fine.
- Safeboot: 4 KB AsyncTCP stack/Oz build: the change is a few lines; but Safeboot images are committed binaries and must be rebuilt and verified on OTA bench (safeboot campaign state machine tests).
- Env hazards for implementers: never run `pio test` bare; one `pio` process at a time; the tree is shared with a concurrent session.

## Open questions for the operator

1. Scope: is "MTU on all nodes" meant as TCP MSS only (web GUI, Safeboot OTA) or also the node UDP datagram size? (Latter is already safe, see 2.)
2. Reuse `node_ethmtu` (name says "eth", exists, persisted, default 1500) for all platforms, or add a neutral `node_mtu` and keep `--ethmtu` as alias? Reusing avoids a schema/lint/doc churn and the field is already read by Safeboot via init_flash().
3. Default for ESP32: 1500 (= no change, MSS stays 1436 effective) or something lower out of the box? Value range 1280..1500 enough for PPPoE (1492) / WireGuard (1420) / HAMNET?
4. Is a bench ESP32 (DK5EN-1 / DK5EN-92 / DK5EN-14) plus a tunnel-like path (e.g. `pfctl`/Linux `ip link set mtu`) available to capture SYN-ACK MSS, to decide whether netif->mtu changes the advertised MSS? Without that the ESP32 solution is unproven.
5. Fallback if netif->mtu does not move the advertised MSS: custom-built IDF libs (out of scope?), or a raw pcb hack (`tcpip_callback` + pcb->mss) on the AsyncTCP listen pcb for Safeboot only?
6. Is the unpinned RAK13800-W5100S fork stable enough, or pin to a commit (platformio.ini:84) before relying on w5100.h private accessors (readSnMSSR) in more places?
7. Does upstream want a Safeboot binary refresh in the same PR (safeboot.bin / safeboot-s3.bin are committed at repo root)?

## Suggested file ownership (implementation), grouped by concern

A. Setting generalization (settings/commands/web)

- src/command_functions.cpp (drop RAK-only #if on --ethmtu :1225,:6480; help text; optional alias)
- src/web_functions/web_setup.cpp (:138-146 drop #if RAK) ; src/web_functions/web_functions.cpp (:2889 field; NOTE this file is also in C)
- src/meshcom_settings.h / src/config_json.h ONLY if renaming to node_mtu (else untouched)
  B. ESP32 apply hook (app firmware)
- src/udp_functions.cpp (wifiEventLog GOT_IP :380 + WiFi mode cycles) ; src/esp32/esp32_eth.cpp (ETH got-ip) ; new header e.g. src/esp32/esp32_mtu.h (netif lookup + mtu write under tcpip lock)
  C. RAK/W5100S
- src/web_functions/web_functions.cpp (webApplyEthMss :86-144, already done; only if range/renaming changes) -- OVERLAP with A
  D. Safeboot
- src/safeboot/main.cpp (read meshcom_settings field after init_flash :217; apply on GOT_IP and AP start) ; rebuilt safeboot.bin, safeboot-s3.bin (+ tools/safeboot.py untouched)
  E. Tests/docs
- test/golden/settings_schema_lint.py (:246, :362) ; docs/settings-registers.md:239 ; docs/d1-04-settings-field-triage-20260912.md:122 ; test/test_command_setters/test_command_setters.cpp ; test/test_config_json/test_config_json.cpp ; new native test for the MTU clamp helper (new env in platformio.ini -> also test/golden/native/variant-ini-effective.json) ; docs/BACKLOG.md
  Overlaps: src/web_functions/web_functions.cpp (A and C), platformio.ini + variant-ini-effective.json (E new native env), src/command_functions.cpp is single-owner (A). B and D share a new helper header if one is created (assign to B, D includes it; check safeboot build_src_filter :1235 lists only esp32_flash/settings_schema, so a shared header must be header-only and added deliberately).
