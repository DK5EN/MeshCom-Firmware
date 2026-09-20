---
name: Bug report
about: Create a report to help us improve
title: ''
labels: ''
assignees: ''
---

<!--
Please attach a debug log. Almost every LoRa/mesh report is unanalysable without one.
The section "Debug log" below explains how to produce one in a few minutes.
-->

**Describe the bug**
A clear and concise description of what the bug is.

**To Reproduce**
Steps to reproduce the behavior. Please describe as clear as possible:

**Expected behavior**
A clear and concise description of what you expected to happen.

---

## Node information (`--info`)

Paste the full output of `--info` here. It contains the firmware version, board type,
country/frequency setting and which debug flags are active.

```
paste --info output here
```

> **Redact before posting.** `--info` prints your WiFi `PASSWORD <...>`, the net-console
> `PASSWD <...>`, `Webpwd <...>` and `BTCODE`. Replace them with `xxx`.

---

## Debug log

### 1. Where do I type the commands?

All `--` commands work the same on every channel — pick whichever is easiest for you:

| Channel                          | Available on      | Notes                                                       |
| -------------------------------- | ----------------- | ----------------------------------------------------------- |
| Serial USB console               | all boards        | always works, also without WiFi — see 3a                    |
| Net console, TCP port 2323       | ESP32 boards only | no cable needed, node must be on WiFi/LAN — see 3b          |
| MeshCom phone app (BLE)          | all boards        | type the command into the message input like a chat message |
| Web interface (`--webserver on`) | ESP32 boards only | switches for the debug flags under Setup                    |

Type the command, press Enter — the node answers on the same channel and stores the
setting permanently (it survives a reboot).

### 2. Which debug do I turn on?

For LoRa / mesh / routing / ACK / "message did not arrive" problems:

```
--loradebug on
```

This logs LoRa RX and TX, the APRS decode of every received frame, RSSI/SNR, mesh
forwarding decisions, ACK/retransmit handling, PING/PONG and MHeard updates.

Other flags, only add them if your problem is in that area:

```
--debug on           general node debug
--gpsdebug on        GPS / position
--wxdebug on         weather sensors
--bledebug on        Bluetooth / phone app
--softserdebug on    software serial
```

Useful extras:

```
--debug en           debug texts in English (--debug de = German)
--debug csv          machine-readable output (--debug man = human-readable, default)
--mheard             list of currently heard neighbours
--lora               show the active LoRa parameters
```

Turn it off again when you are done — the flags are stored permanently, and the extra
output costs CPU time and console bandwidth on the node:

```
--loradebug off
```

### 3a. Connecting via serial USB console

Settings: **115200 baud, 8N1**, no flow control.

Port names:

- Linux: `/dev/ttyUSB0` (CP2102/CH340 ESP32) or `/dev/ttyACM0` (RAK4631, T-Echo)
- macOS: `/dev/cu.usbserial-*` (ESP32) or `/dev/cu.usbmodem*` (RAK4631, T-Echo)
- Windows: `COM3`, `COM4`, ... (see Device Manager)

Examples:

```bash
# PlatformIO (any OS)
pio device monitor -b 115200

# Linux / macOS
screen /dev/ttyUSB0 115200            # quit with Ctrl-A then K
minicom -D /dev/ttyUSB0 -b 115200

# log straight to a file (Linux / macOS)
python3 -m serial.tools.miniterm /dev/ttyUSB0 115200 | tee meshcom.log
```

Windows: PuTTY or TeraTerm, connection type *Serial*, speed 115200. In PuTTY enable
*Session -> Logging -> All session output* so you get a file to attach.

> On many ESP32 boards, opening the port with DTR/RTS asserted **resets the node**, so
> you lose the state you wanted to capture. Most terminals have a "no DTR/RTS" option.
> This repository ships `tools/serial_monitor.py`, which does that for you and writes a
> timestamped log file:
>
> ```bash
> python3 tools/serial_monitor.py --port /dev/ttyUSB0 --no-dtr   # ESP32
> python3 tools/serial_monitor.py --port /dev/ttyACM0            # RAK4631 / nRF52
> ```

### 3b. Connecting via the net console (high port 2323)

**ESP32 boards only** (Heltec V3, T-Beam, T-Deck, E22 DevKit, ...). The nRF52 boards
(RAK4631, T-Echo, Heltec T114) have no net console — use the serial console there.

One-time setup on the node (over serial or the phone app):

```
--setssid MyWLAN         WiFi credentials, if not set yet
--setpwd  MyWlanPassword
--netconsole on          enable the console on TCP port 2323
--passwd MySecret        optional: HMAC password, max. 14 characters
--info                   read the node's IP address from the output
```

`--netconsole` on its own prints the current status and the IP/port. `--passwd none`
clears the password again (open access). The password is never sent over the network —
the node sends a random nonce and the client answers with `HMAC-SHA256(password, nonce)`.

Connect (client scripts are in this repository under `tools/`):

```bash
# with password (all platforms, no dependencies, Python 3.6+)
python3 tools/hmac_connect.py 192.168.1.100 MySecret

# without password set
nc 192.168.1.100 2323                 # Linux / macOS
ncat.exe 192.168.1.100 2323           # Windows, from the Nmap package
```

```powershell
# Windows PowerShell, with password
.\tools\hmac_connect.ps1 192.168.1.100 MySecret
```

Once connected you get exactly the same output as on the USB console, and you can type
the same `--` commands. Only **one** client at a time can be connected. To capture it:

```bash
python3 tools/hmac_connect.py 192.168.1.100 MySecret | tee meshcom.log
```

### 4. What to send us

Please attach the log as a **`.txt` / `.log` file** (drag & drop into the issue) rather
than as a screenshot — we need to search and grep it.

A useful log contains:

1. `--info` output at the start (redacted, see above)
2. `--loradebug on`
3. at least a few minutes of traffic **including the moment the problem happens**
4. a note telling us the local time / log line at which it went wrong
5. `--mheard` at the end, if the problem is about a specific neighbour node

If the node crashes or reboots, please include the lines **before** the reboot and the
complete boot banner after it.

```
paste a short excerpt here (full log as attachment)
```

---

**Screenshots**
If applicable, add screenshots to help explain your problem.

**Smartphone (please complete the following information):**

- Device: [e.g. iPhone 15]
- OS: [e.g. iOS 18.1]
- App Version: [e.g. 4.3.2]

**Additional context**
Add any other context about the problem here.
