# AppLink: implementation plan, firmware

**Status:** draft, 2026-11-21. Contract: `docs/applink-api-contract.md`. App side:
`docs/applink-plan-app.md`.

## Goal

A WebSocket server on port 8002 that carries the BLE phone frames, on ESP32 (WiFi and W5500) and on
the RAK4631 with RAK13800, behind an HMAC login, off by default. The phone path (`phoneRing`,
`phoneComRing`, the drain, `readPhoneCommand()`) stays the single path; AppLink is a second sink
and a second source for it.

## What exists and is reused

| Piece                         | Where                                                                                         | Use in AppLink                         |
| ----------------------------- | --------------------------------------------------------------------------------------------- | -------------------------------------- |
| Drain with pluggable sink     | `src/ble_phone_drain.h` `blePhoneDrainOne()`                                                  | second sink, `mtu` 0 = no cap          |
| Frame shape                   | `src/ble_phone_frame.h`                                                                       | unchanged, including the `+2` pad      |
| Inbound queue to the loop     | `bleQueue` (`esp32_main.cpp`, `nrf52_main.cpp`)                                               | AppLink frames enter the same queue    |
| Hello, config burst           | `readPhoneCommand()` case `0x10`                                                              | PIN check skipped for AppLink          |
| HMAC-SHA256, portable         | `src/hmac_sha256.{h,cpp}` (RM1)                                                               | login MAC on both platforms            |
| Nonblocking raw-socket server | `src/kiss_functions.cpp`, `src/net_console.cpp`                                               | pattern for the ESP32 server           |
| W5100S socket handling        | `src/web_functions/web_functions.cpp` (`webApplyEthMss`), `src/extudp_functions.cpp` (EXT-02) | socket census, failure handling on RAK |

New: SHA-1 and base64 for the WebSocket handshake (portable, about 120 lines; ESP32 may use
mbedtls SHA-1 behind the same API, as `hmac_sha256` does).

## Design rules

1. **Phone link owner.** A new `phone_link` state (`NONE`, `BLE`, `NET`) replaces the meaning
   "BLE connected" wherever the code means "a phone is connected". The radio-level BLE state
   (`g_ble_uart_is_connected`) stays for BLE-only uses (advertising, BLE pause, notify).
2. **Newest authenticated session wins** (contract section 6). The switch resets `isPhoneReady`,
   `config_to_phone_prepare`, `conffin_sent` the way `ble_session.h` does today; the rings are kept.
3. **Loop task only.** Accept, read, parse, login, drain all run in the loop, nonblocking, like KISS.
   No new FreeRTOS task, no crypto in a callback, no `printf` above 64 bytes on hot paths.
4. **RAK socket budget.** One listener on 8002, never a second one: while a session is up the
   listener is not reopened. AppLink and ext-UDP exclude each other on nRF52.
5. **Arduino-free cores.** Handshake parser, WebSocket framing, login, lockout and session state
   machine live in host-testable headers with time injected; the platform files only move bytes.

## Waves

### W0: audit (read-only)

- Classify the 27 uses of `g_ble_uart_is_connected` and every `isPhoneReady` site (8 files) as
  "phone session" or "BLE radio". Output: a table in this doc.
- Find a free persisted bit for the setting (`node_sset4` or the next free set), checked against
  `ble_settings_v1` on nRF52 (layout must not move).
- RAK socket census on the bench: which W5100S sockets are in use with web GUI idle, during a web
  request, during DHCP renew, with ext-UDP on and off. Confirms the budget in contract section 1.
- Measure free DRAM and flash on classic ESP32 (Heltec V2 class) and S3 before the change
  (`/ram-snapshot`).

### W1: host cores and tests (no hardware)

| File                      | Content                                                                                                                                                         |
| ------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/sha1_base64.{h,cpp}` | SHA-1, base64, `wsAcceptKey()`                                                                                                                                  |
| `src/applink_ws.h`        | HTTP upgrade parser (path, key, subprotocol, size caps), frame encode and decode (mask, 7/16-bit length, continuation, close/ping/pong), limits 300 in, 257 out |
| `src/applink_auth.h`      | challenge JSON, MAC check with `MCAPPLINK1` prefix, constant-time compare, lockout (5 in 60 s, 60 s)                                                            |
| `src/applink_session.h`   | states `IDLE`, `HTTP`, `CHAL`, `AUTHED`, `CLOSING`; timers (10 s login, 15 s heartbeat, 45 s silence); close codes                                              |
| `src/phone_link.h`        | owner arbitration `NONE`/`BLE`/`NET`, takeover, reset set                                                                                                       |

Tests: new envs `native_applink_ws`, `native_applink_session`, `native_phone_link`; RFC 6455 test
vectors (accept key from RFC section 1.3), malformed headers, oversize frames, fragmented client
messages, lockout timing, takeover in both directions. Extend `native_ble_phone_harness` with a NET
sink (no MTU cap, BUSY on a full TCP buffer). Register the envs in `tools/regression.sh` and
`docs/test-suite-map.md`.

### W2: shared firmware integration

- `phone_commands.cpp`: drain picks the sink by owner; the gate becomes `phoneLinkUp()`; a source
  argument for `readPhoneCommand()` skips the PIN hash on `NET` (contract 4.3).
- Inbound: the platform glue pushes complete AppLink frames into the existing `bleQueue` with a
  source tag (queue item grows by one byte).
- `command_functions.cpp`: `--applink on|off`, status line, toggle-table row, help text; refuse
  without `--passwd`; `--passwd` cleared stops AppLink; on nRF52 mutual exclusion with `--extudp`.
- `--info`: AppLink counters (sessions, auth failures, frames in and out, takeovers).
- Every site W0 marked "phone session" moves to `phoneLinkUp()`.

### W3: ESP32 glue

- `src/esp32/applink_esp32.cpp`: raw lwIP socket, nonblocking, one listener, one session plus one
  pending login (for the takeover); `send()` EAGAIN maps to `BLE_SEND_BUSY`, so the drain's retry
  logic applies unchanged.
- Start and stop follow the network: WiFi STA up, AP mode, W5500 Ethernet up; network loss closes
  with 1001.
- Build guard `DISABLE_APPLINK` (as `DISABLE_KISS_TCP`), so a tight board can opt out.
- Re-measure DRAM and flash against W0.

### W4: nRF52 glue

- `src/nrf52/applink_nrf52.cpp` under `HAS_NRF_ETH`: one listening socket via the W5100S library;
  on accept the socket becomes the session and no new listener opens until it closes (rule 4).
- Failure to get a socket is reported once (rate-limited, as EXT-02 does) and retried, never latched.
- MSS handling as `webApplyEthMss()` does for port 80.

### W5: web GUI and docs

- Web GUI, Network page: AppLink toggle next to ext-UDP; on RAK the two are shown as a choice.
- Docs: `11-wire-format.md` section 4 points to the contract; `RESUME.md`, `BACKLOG.md` row,
  `test-suite-map.md`.

### W6: bench verification

- `tools/bench/applink_client.py` (Python, `uv`, `websockets`): login, hello, captures the config
  burst. Compare it byte for byte with `tools/bench/ble_golden.py` on the same node.
- Cases: wrong password, lockout, login timeout, heartbeat loss (pull the cable / WiFi off),
  takeover BLE to AppLink and back, `--applink off` with a session up, reboot with a session up.
- RAK: AppLink session plus web GUI request plus DHCP renew at the same time; ext-UDP exclusion
  both ways.
- Message traffic only as DM to an own node or group TEST, TX power at most 2 dBm, identity guard
  first. 24 h soak on one ESP32 and the RAK with an app session up.

## Acceptance

- Host gate green (`tools/regression.sh --stage 1,2`) including the new envs.
- Config burst over AppLink byte-identical to BLE on ESP32 and RAK.
- No regression in BLE behaviour with AppLink off (bench BLE suite).
- RAK: no failed web request and no lost server UDP with an AppLink session up.
- DRAM and flash cost recorded; classic ESP32 still boots with WiFi, BLE and web GUI on.

## Upstream

- Firmware PR first, ESP32 and RAK in one PR (R1: complete across platforms), German body, against
  upstream `dev`. The app PR follows once the firmware is merged.
- Size target: W1 cores about 600 lines plus tests, W2 to W4 about 500 lines of glue.

## Risks

| Risk                                           | Mitigation                                                          |
| ---------------------------------------------- | ------------------------------------------------------------------- |
| DRAM on classic ESP32 (TCP buffers)            | one session, small RX buffer, measure in W0 and W3, opt-out flag    |
| Missed "phone connected" site after the rename | W0 table, host harness covers the drain, bench BLE suite            |
| RAK socket starvation                          | rule 4, W0 census, W6 combined load test                            |
| Android background drops the WebSocket         | app side (foreground service); firmware just sees a 45 s timeout    |
| Upstream reluctance (ext-UDP already exists)   | issue #1276 discussion first; the PR stays off by default and small |
