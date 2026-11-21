# AppLink: API contract (v1, draft)

**Status:** draft, 2026-11-21. Working name "AppLink"; renaming is a find-and-replace.
**Origin:** upstream issue icssw-org/MeshCom-Firmware#1276 (WiFi transport between app and node).
**Payload spec:** `docs/architecture/11-wire-format.md` section 4 (BLE phone protocol). This contract
does not repeat it; it defines only how those frames travel over IP.

## 1. Scope

AppLink carries the existing BLE phone protocol over a WebSocket on the LAN. The app speaks the
same frames it speaks over BLE; only the transport and the login change.

| Platform                                      | AppLink             | Network        | Note                                                       |
| --------------------------------------------- | ------------------- | -------------- | ---------------------------------------------------------- |
| ESP32 (all WiFi boards)                       | yes                 | WiFi STA or AP | 16 lwIP sockets, coexists with ext-UDP, KISS, console      |
| ESP32 with W5500 (T-ETH-Elite, T-Connect Pro) | yes                 | Ethernet       | 8 hardware sockets, coexists                               |
| RAK4631 + RAK13800 (W5100S)                   | yes, **or** ext-UDP | Ethernet       | 4 hardware sockets: AppLink and ext-UDP exclude each other |
| RAK4631 without Ethernet, T114, T-Echo        | no                  | none           | BLE only                                                   |

## 2. Transport

- WebSocket, RFC 6455, unencrypted: `ws://<host>:8002/applink`.
- `<host>` is the node IP. On ESP32 `<CALL>.local` also works where the phone resolves mDNS (the
  node registers `node_call` as its mDNS name). The RAK has no mDNS: IP only.
- Subprotocol: the client offers `Sec-WebSocket-Protocol: meshcom-applink.1`. The node answers with
  the same value or refuses the upgrade with HTTP 400. A future v2 gets a new subprotocol string.
- No extensions (no `permessage-deflate`). The node ignores `Sec-WebSocket-Extensions`.
- Messages are never fragmented by the node; the node accepts fragmented client messages up to the
  size limits in section 4 and drops the session above them.
- **Text messages** carry control JSON (section 3). **Binary messages** carry phone frames (section 4).
- One session at a time per node (section 6).

Why a WebSocket and not raw TCP or UDP: the app runs in a Capacitor web view, which has WebSocket but
no raw sockets. UDP would need acknowledgements and ordering on top: the node's phone queue only
drops a frame once the transport has taken it, and TCP gives that for free.

## 3. Control messages (text, JSON)

Every control message is one JSON object with a type field `t`. Unknown fields are ignored; an
unknown `t` is ignored by both sides (forward compatibility).

### 3.1 Login (node speaks first)

```
node -> app   {"t":"chal","v":1,"nonce":"<32 hex>","call":"DK5EN-1","fw":"4.35a.11.21","hw":"HELTEC_V3"}
app  -> node  {"t":"auth","mac":"<64 hex>"}
node -> app   {"t":"ok","mtu":257}
           or {"t":"err","code":"auth"}      followed by close 4401
```

- `nonce`: 16 random bytes from the hardware RNG, fresh per connection, lower-case hex.
- `mac` = HMAC-SHA256(key = node password, message = `"MCAPPLINK1"` (ASCII, 10 bytes) followed by
  the 16 raw nonce bytes), lower-case hex.
- Node password = `--passwd` (`node_passwd`, at most 14 characters, trailing spaces stripped). The
  same secret protects the net console (2323) and KISS (8001). The prefix `MCAPPLINK1` keeps an
  AppLink answer from being usable as a console or KISS answer, and the other way round.
- The node compares in constant time.
- `call`, `fw`, `hw` before login let the app show which node it reached. They reveal nothing the
  node does not already announce on air.
- `mtu` in `ok`: the largest binary message the node will send (section 4).

### 3.2 Heartbeat

```
node -> app   {"t":"hb"}        every 15 s after "ok"
app  -> node  {"t":"hb"}        answer to each node heartbeat
```

Either side closes the session after 45 s without any message from the peer. Browser WebSocket
APIs cannot send ping frames, so the heartbeat is application level. The node still answers RFC
ping frames with pong.

### 3.3 Session end

```
node -> app   {"t":"bye","reason":"<text>"}    then close with a code from section 7
```

`bye` is informational; the close code is authoritative.

## 4. Phone frames (binary)

One binary message = one phone frame. The bytes are exactly those of one BLE write or one BLE
notify, as specified in `11-wire-format.md` section 4.

### 4.1 App to node

- Same frames as the BLE write: byte 0 = length, byte 1 = message id (`0x10` hello, `0x20` time,
  `0xA0` text or `--` command, `0x95` APRS symbol, `0xF0` save, ...).
- Maximum 300 bytes (`MAX_MSG_LEN_PHONE`). Longer messages close the session (1009).
- Before `ok` any binary message closes the session (4401).

### 4.2 Node to app

- Same bytes as the BLE notify, **including** the two trailing zero bytes the node appends today
  (`ble_phone_frame.h`, send length `blelen + 2`). The app parser expects that shape; AppLink keeps it.
- Maximum 257 bytes. The BLE limit of 244 bytes (MTU 247) does not apply: frames that BLE cuts
  arrive whole over AppLink.
- First byte as on BLE: `0x40` text, position, ack (`0x41` second byte); `0x44` JSON (`DI`, `DG`,
  `DW`, config burst, `MH` heard list); `0x91` legacy, not produced anymore.

### 4.3 Hello and PIN

After `ok` the app sends the same hello as over BLE (`0x10 0x20 0x30`). The node starts the config
burst exactly as after a BLE hello (`11-wire-format.md` 4.2).

The BLE PIN (`--btcode`) does not apply to AppLink: the HMAC login is stronger, and a static PIN
hash would be replayable on a LAN. The node accepts the 4-byte hello and the 36-byte hello with PIN
hash alike and ignores the hash on AppLink.

## 5. Enabling

| Command         | Effect                                                                          |
| --------------- | ------------------------------------------------------------------------------- |
| `--applink on`  | start the server on port 8002, persist. Refused when `--passwd` is empty.       |
| `--applink off` | stop the server, close a session with 1001, persist.                            |
| `--applink`     | status: on/off, port, session peer IP, uptime, frames in and out, auth failures |

- Default: off.
- **RAK4631:** `--applink on` is refused while `--extudp` is on, and `--extudp on` is refused while
  AppLink is on, each with a one-line hint naming the other command.
- Clearing `--passwd` while AppLink is on stops AppLink and says so.
- The web GUI shows the setting next to ext-UDP (Network page) with the same rules.

## 6. Sessions and the BLE phone

- The node has one phone session. Its owner is either BLE or AppLink, never both: the outgoing
  phone queue is single-consumer.
- **Newest authenticated session wins.** A successful AppLink login closes an active BLE phone
  link (BLE disconnect), and a successful BLE hello closes an active AppLink session (4409). An
  attempt that fails its login changes nothing.
- Queued frames are not lost at a handover: the new owner receives them after its hello.
- **ESP32:** while a session is up the node keeps listening; a second client gets the challenge,
  and only after a successful login replaces the first.
- **RAK4631:** while a session is up the node does **not** listen on 8002 (each listener costs a
  hardware socket). A second client gets connection refused until the first session ends or its
  heartbeat times out (at most 45 s).

## 7. Close codes

| Code | Meaning                                                   | App reaction                         |
| ---- | --------------------------------------------------------- | ------------------------------------ |
| 1000 | normal close (app disconnects)                            | none                                 |
| 1001 | node going away (reboot, `--applink off`, network change) | auto reconnect                       |
| 1009 | message too big                                           | log, reconnect                       |
| 4401 | login failed or binary before login                       | ask for the password, no auto retry  |
| 4403 | AppLink disabled or no password set                       | show hint, no auto retry             |
| 4408 | login timeout (10 s after `chal`) or heartbeat timeout    | auto reconnect                       |
| 4409 | session taken over by a newer login (BLE or AppLink)      | show "taken over", no auto reconnect |
| 4429 | locked out after repeated login failures                  | show wait time, retry after 60 s     |

## 8. Security

- Login: HMAC challenge-response, the password never crosses the wire.
- Lockout: after 5 failed logins within 60 s the node refuses logins for 60 s (4429); failures are
  counted per node, not per IP (a LAN attacker can change IPs).
- No transport encryption. Traffic on the LAN is readable, the same as ext-UDP, KISS and the
  console today, and the payload is amateur radio traffic that is readable on air anyway.
- An attacker on the same LAN who can inject into the TCP stream can hijack an authenticated
  session. Accepted for v1, same as console and KISS. A per-frame MAC would close this and is a v2
  candidate.
- AppLink must not be exposed to the internet. The status output and the web GUI say so.

## 9. Versioning

- The subprotocol string carries the major version. `chal.v` carries the minor version; the app
  accepts any `v >= 1` with the same major.
- Phone frame changes are not AppLink changes: they follow `11-wire-format.md` and apply to BLE
  and AppLink alike.
