# AppLink: implementation plan, mobile app

**Status:** draft, 2026-11-21. Contract: `docs/applink-api-contract.md`. Firmware side:
`docs/applink-plan-firmware.md`. App repo: `Meshcom-MobileApp` (Ionic React, Capacitor 8,
`@capacitor-community/bluetooth-le` 8.3.0, tests with vitest).

## Goal

A second connection type "LAN" in the Connect tab: IP or `<CALL>.local`, password, saved nodes.
After login the app runs its existing frame parser and command builders unchanged. BLE stays the
default and keeps working as today.

## What exists

| Piece                                                            | Where                                                                            | Transport-bound?               |
| ---------------------------------------------------------------- | -------------------------------------------------------------------------------- | ------------------------------ |
| Frame parser `parseMsg`                                          | `src/hooks/MessageHandler.ts`                                                    | no, takes a `DataView`         |
| Send helpers `sendDV`, `sendTxtCmdNode`, `syncTimestampToDevice` | `src/hooks/BleHandler.ts`                                                        | yes, 3 `BleClient.write` calls |
| Connect, notify, hello, reconnect                                | `src/pages/Connect.tsx` (1735 lines)                                             | yes, 22 `BleClient` calls      |
| Hello builder with PIN hash                                      | `Connect.tsx` `buildHello()`                                                     | frame only, reused             |
| Auto reconnect                                                   | `src/utils/AutoReconnectManager.ts`                                              | BLE scan driven                |
| Connection state                                                 | `BLEconnStore`, `DevIDStore`, used in Chat, Map, Info, Settings, `BleDiscoAlert` | state only                     |
| Node password check                                              | `src/utils/NetPassword.ts`                                                       | reused for the login form      |
| Config watchdog                                                  | `src/utils/ConfigSyncWatchdog.ts`                                                | no                             |

The callers outside Connect (Chat, Map, Info, Settings, `PhoneGps.ts`, `ConfigObject.ts`) send
through `BleHandler` with a device id from `ConfigObject.getBleDevId()`. That is the seam.

## Design

- **`NodeTransport` interface** (`src/transport/NodeTransport.ts`):
  `connect()`, `disconnect()`, `send(frame: DataView)`, `onFrame(cb)`, `onClose(cb)`,
  `kind: "ble" | "lan"`, `label` (device name or host).
- **`BleTransport`** wraps today's `BleClient` calls without changing behaviour (bonding, MTU log,
  retry on connect, Android quirks stay where they are).
- **`LanTransport`** (`src/transport/LanTransport.ts`): browser `WebSocket`, `binaryType =
"arraybuffer"`, subprotocol `meshcom-applink.1`, login (HMAC via `crypto.subtle`), heartbeat,
  close-code handling as in contract section 7.
- **`TransportStore`** holds the active transport. `BleHandler` send helpers call
  `TransportStore.active.send()`; the `devID` argument stays for compatibility and is ignored for LAN.
- The setup sequence after "connected" (update stores, clear MHeard, config watchdog, send hello)
  moves out of the BLE callback chain into one `onLinkUp(transport)` function used by both.
- Store names stay (`BLEconnStore.ble_connected` means "node link up"); a rename is a later cleanup,
  not part of this change.

## Waves

### A0: spikes (one day, decide before building)

- **Mixed content.** The app origin is `https://localhost` on Android (`androidScheme: 'https'`)
  and `capacitor://localhost` on iOS. Check that a `ws://` connection to a LAN IP opens from the
  web view on both. Expected fixes: Android `android.allowMixedContent: true` in
  `capacitor.config.ts` plus a `network_security_config.xml` that allows cleartext to private
  ranges; iOS `NSAppTransportSecurity` / `NSAllowsLocalNetworking` and
  `NSLocalNetworkUsageDescription` in `Info.plist` (none of these keys exist today).
- **mDNS.** Does `<CALL>.local` resolve from the web view on Android 12+ and iOS? If not, IP only in
  v1 and a discovery plugin later.
- **Background.** Does an open WebSocket survive 10 minutes with the app in the background
  (Android with and without a foreground service, iOS)? This decides whether the issue's
  "background" argument holds. Result goes into this doc.
- **Node IP from the config burst.** Check whether the BLE config burst carries the node IP, so a
  BLE-connected node can be saved as a LAN node with one tap.

### A1: transport seam, no feature change

- Add `NodeTransport`, `BleTransport`, `TransportStore`; route `BleHandler` and `Connect.tsx`
  through them. Behaviour over BLE must be identical.
- Unit tests for `TransportStore` and the send path with a fake transport.
- Gate: vitest, lint, typecheck, Cypress e2e as today; manual BLE connect, reconnect, PIN, chat
  on Android and iOS.

### A2: `LanTransport`

- Login: wait for `chal`, compute `HMAC-SHA256(pwd, "MCAPPLINK1" + nonce)` with `crypto.subtle`,
  send `auth`, wait for `ok`; 10 s timeout.
- Binary messages to `onFrame` as `DataView`; text messages as control JSON.
- Heartbeat: answer each `hb`; close after 45 s silence.
- Close codes map to: reconnect (1001, 1009, 4408), ask for password (4401), hint (4403), "taken
  over" (4409), wait (4429).
- Unit tests: HMAC against the vectors the firmware host tests use (shared JSON fixture), a mock
  WebSocket for login, timeout, heartbeat and every close code.

### A3: Connect tab UI

- Segment "Bluetooth | LAN" at the top of the Connect tab; BLE is the default.
- LAN view: host field (IP or `<CALL>.local`), password field (checked with `checkNetPassword`),
  "remember" switch, list of saved nodes with last-seen time, connect and disconnect buttons.
- After `chal` show the node's call and firmware before login, so the user sees where they are.
- Saved LAN nodes in SQLite (`DataBaseService.ts`): host, call, password. Password in the
  platform keystore if a secure-storage plugin is acceptable, otherwise stored as the BLE PIN is
  stored today (decide in A0).
- From a BLE-connected node: "Use over LAN" action that sets `--passwd` if missing (existing
  Settings flow), sends `--applink on` and saves the node with its IP.

### A4: reconnect and lifecycle

- `AutoReconnectManager` gets a LAN path: no scan, retry the host with backoff (2, 5, 10, 30 s),
  stop on 4401, 4403, 4409.
- App to foreground: reconnect the LAN node if the link dropped while in background.
- Taken over by BLE at the node (4409): show it, do not fight back.

### A5: release

- Release notes in English (house style), screenshots of the LAN view.
- Store listing note on local-network permission (iOS shows a system prompt on first use).

## Acceptance

- BLE behaviour unchanged (A1 gate, manual check list).
- LAN: connect, config burst, chat, position, MHeard, settings commands on an ESP32 node and on the
  RAK4631 with Ethernet.
- Wrong password, lockout, node reboot, WiFi loss, takeover in both directions behave as in the
  contract's close-code table.
- Frames over 244 bytes arrive whole over LAN.

## Risks

| Risk                                          | Mitigation                                                               |
| --------------------------------------------- | ------------------------------------------------------------------------ |
| `ws://` blocked as mixed content or cleartext | A0 spike before any code; platform config only, no plugin                |
| Regression in the BLE path from the refactor  | A1 is behaviour-neutral and gated on its own                             |
| `Connect.tsx` size (BLE state mixed into UI)  | extract only the setup sequence; leave scan and bonding code where it is |
| Android kills the socket in background        | A0 measurement; foreground service only if A0 shows the need             |
| Password storage on the phone                 | decision in A0, same protection level as the BLE PIN at minimum          |
