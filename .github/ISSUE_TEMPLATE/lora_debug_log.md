---
name: LoRa debug log
about: Submit a LoRa/mesh debug log for analysis (lost messages, no ACK, bad routing, RX/TX problems)
title: '[LORA-LOG] '
labels: ''
assignees: ''
---

<!--
Use this template when the problem is about LoRa itself: messages that never arrive,
missing ACKs, wrong mesh path, nothing received, node not heard by others, high channel
utilisation, retransmit storms.

Without a log this cannot be analysed. The steps below take about five minutes.
-->

## What is the problem?

One or two sentences, plus the callsign(s) involved.

- Own callsign:
- Peer / neighbour callsign (if applicable):
- Approximate time (with time zone) the problem occurred:

## Node

- Firmware version (first line of `--info`):
- Board (e.g. Heltec V3, T-Beam, RAK4631, T-Echo, T-Deck):
- Country/region setting (`--setctry`) and the frequency shown by `--lora`:
- Gateway mode on/off, mesh on/off:

---

## How to produce the log

### Step 1 — open a console

**Serial USB console** (works on every board), **115200 baud, 8N1**:

```bash
pio device monitor -b 115200                      # any OS, PlatformIO
screen /dev/ttyUSB0 115200                        # Linux, ESP32   (Ctrl-A K to quit)
screen /dev/cu.usbmodemXXXX 115200                # macOS, RAK4631 / nRF52 (ls /dev/cu.*)
python3 tools/serial_monitor.py --port /dev/ttyUSB0 --no-dtr   # this repo: logs to a file, no reset
```

Windows: PuTTY or TeraTerm, *Serial*, 115200 baud. Enable
*Session -> Logging -> All session output* in PuTTY so you end up with a file.

**Net console on high port 2323** (ESP32 boards only — nRF52 boards such as RAK4631,
T-Echo and Heltec T114 do not have it). Enable it once on the node:

```
--netconsole on        start the console on TCP 2323
--passwd MySecret      optional, max. 14 characters; --passwd none = open access
--info                 the output contains the node's IP address
```

Then connect from your computer:

```bash
python3 tools/hmac_connect.py <node-ip> MySecret   # with password, any OS
nc <node-ip> 2323                                  # no password set, Linux/macOS
ncat.exe <node-ip> 2323                            # no password set, Windows (Nmap)
```

```powershell
.\tools\hmac_connect.ps1 <node-ip> MySecret        # Windows PowerShell
```

The net console carries exactly the same output as the USB console and accepts the same
commands. Only one client can be connected at a time. The password itself never travels
over the network: the node sends a random nonce, the client replies with
`HMAC-SHA256(password, nonce)`.

You can also enter all `--` commands in the MeshCom phone app (type them into the
message input like a chat message), but the app does **not** show the debug output —
for the log you need the serial or net console.

### Step 2 — enable the LoRa debug and capture

Type into the console:

```
--info
--loradebug on
```

Now let it run until the problem shows up (a few minutes is usually enough), then:

```
--mheard
--loradebug off
```

Redirect the whole session into a file, for example:

```bash
python3 tools/hmac_connect.py <node-ip> MySecret | tee meshcom-debug.log
```

### Step 3 — attach it here

Attach the file (drag & drop) as `.txt` or `.log`. Please do not send screenshots of the
log — we need to search it.

> **Redact first.** `--info` prints your WiFi `PASSWORD <...>`, the net-console
> `PASSWD <...>`, `Webpwd <...>` and `BTCODE`. Replace those values with `xxx`.

---

## Log excerpt

Paste the few lines around the problem here; the full log goes in as an attachment.

```
paste log excerpt here
```

## Additional context

Antenna, location, distance to the peer, anything that changed recently, other nodes
showing the same behaviour.
