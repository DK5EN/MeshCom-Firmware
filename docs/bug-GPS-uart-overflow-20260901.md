# GPS-01..04 — the NMEA link is structurally lossy, and nothing downstream checks the result

**Status:** Root cause ESTABLISHED by code reading plus two field logs from the same node; the
arithmetic is verified against the module configuration the node itself prints. **Not yet
reproduced on the bench, not yet fixed.**
**Severity:** Medium-high — no crash, no data loss on the mesh, but every position the node
transmits is drawn from a stream that loses ~40 % of its bytes, and roughly once per 20 minutes a
spliced sentence passes the NMEA checksum and is accepted as a valid fix.
**Class:** upstream defect, present since at least 2023. **Not introduced on this branch** — see §9.
**Reported:** 2026-09-01 by OE5HWN (`OE5HWN-14`, T-Beam Supreme, firmware 4.35p), as
"the altitude on the WX dashboard is wrong".
**Branch:** `v4.35p_prio` @ `ab3c5b65` · **Upstream merge-base:** `2dac2eac`
**Related:** [`N-25`](bug-N25-gps-baud-scan-watchdog.md) (same board, GPS baud scan),
§3.8f timing campaign (loop stalls), `MEM-01`/`MEM-02` (why the obvious fix is the wrong one).

> **Scope note for the implementer.** Every file:line here was read against the tree at `ab3c5b65`.
> Read §7 before touching code: the one-line fix that suggests itself (`setRxBufferSize`) is
> explicitly rejected, and §7.1 says why.

---

## 1. Verdict in one paragraph

`WZ_GPS_Loop()` is the only code that drains the GPS UART. On ESP32 it is called **once every
three seconds**. The L76K sends GGA + RMC at 1 Hz — about 140 bytes per second. The Arduino
default receive ring is **256 bytes**. Three seconds of NMEA is ~420 bytes into a 256-byte ring,
so roughly 165 bytes are discarded by the UART ISR in **every single cycle**, permanently, on
every stationary ESP32 node. The cut always lands mid-sentence; the parser resynchronises at the
next `$` and the truncated sentence gets glued to the tail of a later one. Almost always the
8-bit NMEA checksum catches that and TinyGPS++ discards it silently. About **one in 256** spliced
sentences passes the checksum by chance and is committed as a real fix — with whichever garbage
happened to land in the latitude, longitude, altitude and date slots. Nothing downstream checks
the result: `gpsData.valid` is computed and never read, and the fix gate only looks at satellite
count and HDOP, which come from a different sentence and are therefore unaffected.

---

## 2. What the reporter saw

`OE5HWN-14` is a T-Beam Supreme running fork firmware 4.35p, stationary at 48.2479 N / 14.2576 E.
The MeshCom WX dashboard showed the altitude wandering between **179 m and 308 m** over 24 hours
while the node never moved, and suggested `--setalt 272` as a workaround.

Two facts frame the report:

- The site elevation is **269.9 m** (EU-DEM 25 m, queried at 48.247949 / 14.257572). The node's
  _median_ is therefore correct; the reporter's own figure of 220 m is not the site elevation, it
  is one of the low outliers the node transmitted.
- The barometric pressure series over the same 24 hours is perfectly smooth (983.6 → 990.1 →
  988.8 hPa), which independently confirms the node did not move. The altitude scatter is
  measurement noise, not movement.

So the report is real, but the headline is wrong: the defect is not "the altitude is off by 50 m",
it is "the altitude is a single unfiltered sample taken from a corrupted stream".

## 3. Evidence

Two logs from the reporter, both 2026-09-01, both `~/Downloads` (not in the repo — see §10.3):

|                             | `gpsdebug.txt`                      | `gpsdebug1.txt`             |
| --------------------------- | ----------------------------------- | --------------------------- |
| Window (local)              | 20:26:48 – 20:48:06                 | 21:06:39 – 21:23:44         |
| Starts at                   | 128 s uptime, capture attached late | power-on, full boot banner  |
| GPS time prints             | 407                                 | 305                         |
| Position samples            | 382                                 | 305                         |
| Satellites                  | 0–8, 13 fix losses                  | 7–8, no fix loss            |
| HDOP min / median / max     | 2.1 / 2.7 / 3.3                     | **1.4 / 1.4 / 2.1**         |
| Altitude min / median / max | 249.3 / **274.7** / 284.8 m         | 270.6 / **279.3** / 290.2 m |
| Altitude σ                  | 4.3 m                               | 4.1 m                       |
| Corrupt sentences accepted  | **1**                               | 0                           |
| Loop stalls > 500 ms        | 30                                  | 39                          |
| Reboots                     | **1** (~20:31:58)                   | 0                           |

### 3.1 The corrupted sample (GPS-01 + GPS-02)

`gpsdebug.txt`, lines 663–677 verbatim:

```
[GPS ]...Time <UTC>: 18:34:23 / Date: 2026.09.01

[GPS ]...position  : lat:48.247870 lon:14.257689 alt:278.1
20:34:25 [POSINFO]...Stationary -> Suppressing drift (Rate: 1800s)
[GPS ]...fix:yes sat:7 hdop:2.7
[GPS ]...Time <UTC>: 18:34:26 / Date: 2015.14.00

[GPS ]...position  : lat:48.247871 lon:0.000000 alt:278.0
20:34:29 [POSINFO]...Stationary -> Suppressing drift (Rate: 1800s)
[GPS ]...fix:yes sat:7 hdop:2.7
[GPS ]...Time <UTC>: 18:34:29 / Date: 2026.09.01

[GPS ]...position  : lat:48.247872 lon:14.257685 alt:277.9
20:34:32 [POSINFO]...Stationary -> Suppressing drift (Rate: 1800s)
[GPS ]...fix:yes sat:7 hdop:2.7
```

Three fields are wrong in one commit — longitude `0.000000`, month `14`, day `00`, year `2015` —
while latitude and altitude stay plausible and the sample before and after are clean. That
pattern is a **splice signature**: the leading terms came from one sentence and the trailing terms
from another. A single glitch inside the GNSS module would not select exactly the fields that sit
on opposite sides of a cut and spare the one in between.

Note what the node believed at that moment: `fix:yes sat:7 hdop:2.7`. Satellite count and HDOP are
parsed from GGA/GSA and were untouched by the damaged RMC, so the fix gate saw nothing wrong.

**There is no loop stall anywhere near this event.** The nearest `[INSTR-LOOP]` line is 100 lines
and about five minutes later. This is not stall-induced; it is the steady-state behaviour.

### 3.2 The altitude scatter (GPS-03)

`gpsdebug.txt` around 18:35 UTC, node stationary, 29 m of vertical travel in 30 seconds:

```
[GPS ]...position  : lat:48.247914 lon:14.257719 alt:278.6
[GPS ]...fix:yes sat:6 hdop:3.3
[GPS ]...position  : lat:48.248010 lon:14.257699 alt:273.6
[GPS ]...position  : lat:48.248027 lon:14.257561 alt:268.5
[GPS ]...position  : lat:48.248028 lon:14.257511 alt:267.2
[GPS ]...position  : lat:48.248038 lon:14.257369 alt:263.4
[GPS ]...position  : lat:48.248044 lon:14.257335 alt:261.8
[GPS ]...position  : lat:48.248044 lon:14.257316 alt:261.2
[GPS ]...position  : lat:48.248041 lon:14.257224 alt:257.7
```

The dashboard's 179–308 m range is 48 samples of this, one taken every 30 minutes. The second log
is the control case: with near-ideal geometry (HDOP 1.4, 8 satellites, zero fix losses) the spread
is still 270.6–290.2 m, and the **median moves 4.6 m between the two sessions** at the same
antenna twenty minutes apart. A single raw sample is not a usable altitude even under good
conditions.

### 3.3 The reboot (TM-51)

Not what was reported, found while reading. `gpsdebug.txt` lines 466–479 verbatim:

```
[GPS ]...Time <UTC>: 18:31:54 / Date: 2026.09.01

[GPS ]...position  : lat:48.247960 lon:14.257661 alt:276.6
20:31:57 [POSINFO]...Stationary -> Suppressing drift (Rate: 1800s)
[GPS ]...fix:yes sat:7 hdop:2.7
[GPS ]...Time <UTC>: 18:31:57 / Date: 2026.09.01

[GPS ]...position  : lat:48.247959 lon:14.257661 alt:276.3
p:2.7
[GPS ]...Time <UTC>: 18:32:29 / Date: 2026.09.01
```

`p:2.7` is the tail of `[GPS ]...fix:yes sat:7 hdop:2.7`. The capture channel was cut mid-line and
resumed 32 seconds later, which is where the ~70-line boot banner went.

Three independent proofs that this was a reset, not a capture dropout:

```
gpsdebug.txt  [WIFI];link;up;...;age_s;360;got_ip_n;1;ip;1;ms;368309     <- before
gpsdebug.txt  [WIFI];link;up;...;age_s;58 ;got_ip_n;1;ip;1;ms;66149      <- after
```

`ms` is `millis()` (`udp_functions.cpp:1328`), which cannot decrease without a reset.

```
gpsdebug1.txt  [HEAP] 21:07:44 132092 125888 122868 (mon)   <- fresh boot, 65 419 ms uptime
gpsdebug.txt   [HEAP] 20:33:04 132280 125968 122868 (mon)   <- after the seam, 66 149 ms uptime
gpsdebug.txt   [HEAP] 20:30:40 132132 121692 122868 (mon)   <- before the seam
```

Column 3 is `ESP.getMinFreeHeap()` (`esp32_main.cpp:3523`), a watermark that only ever falls
between boots. It rose from 121 692 to 125 968 — and 125 968 matches the known fresh-boot value of
125 888 at the same uptime to within 0.1 %.

**It was an ESP32-only reset, not a power cycle.** The GPS reports `fix:yes sat:7` immediately on
both sides of the seam — it kept power on its AXP2101 rail and hot-started. Compare the very
beginning of `gpsdebug.txt`, which shows 25 samples of `fix:no sat:0 hdop:25.5` (~75 s): that one
_was_ a cold power-on.

**We cannot say why it reset**, and we could not have said even with the boot banner in hand:

```
$ grep -rn "esp_reset_reason\|rtc_get_reset_reason" src/
(no output)
```

The firmware never prints a reset reason. That is filed as **TM-51**.

### 3.4 The loop stalls (TM-52)

39 stalls in 17 minutes in `gpsdebug1.txt`, tightly clustered at ~575 ms with a second cluster
at ~655 ms:

```
[INSTR-LOOP] gap ms 586 in display_tick section_ms 577 sections_ms 577
[INSTR-LOOP] gap ms 576 in display_tick section_ms 567 sections_ms 568
[INSTR-LOOP] gap ms 579 in display_rx   section_ms 570 sections_ms 570
[INSTR-LOOP] gap ms 651 in display_rx   section_ms 569 sections_ms 642
[INSTR-LOOP] gap ms 574 in udp          section_ms 0   sections_ms 0
```

The first two fire at **7.5 s uptime**, immediately after `[BOOT];ready;ms;7442` — that is the
`iInitDisplay` ramp in `mainStartTimeLoop()`, before any mesh traffic could be involved. Per
`instrument.h`, `section_ms` is the _measured duration_ of the section, not an attribution, so the
display path really does cost ~570 ms per update on this board. This kills the earlier working
hypothesis that the stalls were blocking LoRa airtime.

What is established: `u8g2->setBusClock(400000)` in `esp32_functions.cpp:214` is guarded by
`#if defined(BOARD_HELTEC_V3) || BOARD_HELTEC_V4 || BOARD_STICK_V3`, so the T-Beam Supreme drives
its OLED at the Arduino default of 100 kHz. What is **not** established: a full-buffer flush of
1024 bytes at 100 kHz is ~92 ms, plus `clearDisplay()` ~92 ms — a factor of three short of 570 ms.
This needs a direct measurement, not more reading. Filed as **TM-52**.

The stalls are _not_ the cause of GPS-01 (see §3.1 and §4.3), but they make it marginally worse by
moving the cut point.

---

## 4. Root cause

### 4.1 The arithmetic

| Quantity              | Value                           | Source                                                                         |
| --------------------- | ------------------------------- | ------------------------------------------------------------------------------ |
| Sentences enabled     | GGA + RMC only                  | `$PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0` — `gpsdebug1.txt:91`                       |
| Update rate           | 1 Hz                            | `$PCAS02,1000` — `gpsdebug1.txt:89`                                            |
| Line rate             | 38400 baud                      | `[GPS ]...found with 38400 baud` — `gpsdebug1.txt:79`                          |
| Actual data rate      | **~140 B/s**                    | GGA ≈ 72 B + RMC ≈ 70 B per second                                             |
| RX ring size          | **256 B**                       | `HardwareSerial.cpp:72` (`_rxBufferSize(256)`), never raised                   |
| Drained only by       | `while (GPSSerial.available())` | `gps_functions.cpp:889`, inside `WZ_GPS_Loop()`                                |
| Drain cadence (ESP32) | **every 3 s**                   | `GPS_REFRESH_INTERVAL 3`, `configuration_global.h:178` → `esp32_main.cpp:3079` |
| Drain cadence (nRF52) | **every 1 s**                   | `nrf52_main.cpp:1691`, comment `// gps refresh every sec`                      |

```
3 s x 140 B/s = 420 B  ->  256 B ring  ->  ~165 B discarded, every cycle
```

The overflow threshold is 85 B/s at a 3-second cadence. Even a minimal GGA+RMC pair is ~130 B/s,
so this is not a marginal call: the ring overflows on every cycle on every ESP32 node, and has
done since the cadence was 10 s, then 5 s, now 3 s.

The nRF52 path in the same tree already polls at 1 s (140 B into 256 B, no overflow). The two
platforms share `WZ_GPS_Loop()` and disagree only on the cadence.

**TRACK mode is exempt.** `esp32_main.cpp:3076` sets `gps_refresh_intervall = 1.0` when
`bDisplayTrack` is set. A node in TRACK must not show these artefacts — that is the cheapest
available falsification test (§10.1).

### 4.2 Why the checksum does not save us

The NMEA checksum is an 8-bit XOR. TinyGPS++ discards a sentence whose checksum term does not
match, so most spliced sentences die silently. **1 in 256 passes by chance.**

Expected rate versus observed:

|                          | Cycles | Splices | Expected false commits | Observed |
| ------------------------ | ------ | ------- | ---------------------- | -------- |
| `gpsdebug.txt` (22 min)  | 440    | 440     | ~1.7                   | **1**    |
| `gpsdebug1.txt` (17 min) | 340    | 340     | ~1.3                   | **0**    |

One splice per cycle, because the discard is one contiguous run: everything before the cut was
already consumed, the bytes in the gap were never seen at all, and the parser resynchronises at
the first `$` after the gap. So exactly one sentence straddles each cut. `~1.7` is an upper bound
— many spliced sentences never reach a checksum term in a parseable shape — and the observed 1
and 0 sit comfortably under it.

**Empirical check that the loss is partial, not total:** across both logs, the printed GPS time
never repeats (`delta == 0` count is 0 out of 712 prints; the delta histogram is
`{2: 27, 3: 623, 4: 33, 5: 26, 32: 1}`). Every cycle still yields at least one intact RMC. That is
consistent with the ring keeping the _first_ ~256 bytes of each cycle and the ISR dropping the
rest — the node gets a valid fix and one damaged sentence per cycle, which is exactly the observed
behaviour.

### 4.3 Why this is not the loop stalls

Retracting an earlier reading: at 38400 baud the ring would fill in 67 ms, and a 580 ms stall
would discard ~2 kB. That used the **line rate** instead of the data rate. At the actual 140 B/s
the ring holds 1.8 s, so a 580 ms stall alone does not overflow it. Confirmed by §3.1: there is no
stall within five minutes of the corrupted sample. The stalls are a separate defect (TM-52).

### 4.4 Nothing downstream checks the result (GPS-02)

`gps_functions.cpp:919-924` copies the parsed values out unconditionally:

```c
gpsData.valid      = gps.location.isValid();   // written here, never read anywhere
gpsData.latitude   = gps.location.lat();
gpsData.longitude  = gps.location.lng();
gpsData.altitude   = gps.altitude.meters();
```

`gpsData.valid` is dead. `grep -rn 'gpsData.valid' src/` returns the assignment and nothing else;
it has been dead since it was introduced on 2026-03-12.

The fix gate at `gps_functions.cpp:959` is:

```c
if ((fposinfo_hdop < 6.0) && (posinfo_satcount > 5))
```

Satellite count and HDOP come from GGA/GSA. A damaged RMC leaves both intact, so the gate returns
`has_gnss_location = true` for a sample whose longitude is zero. The values then go straight into
persistent settings at `gps_functions.cpp:1009-1024`, with the only sanity check in the whole path
being `if (node_alt < 0) node_alt = 0`.

**Blast radius.** `bDisplayTrack` is off on this node, so `setSMartBeaconing()` returns at its
first branch and the corrupt sample does not trigger an immediate beacon. But
`meshcom_settings.node_lat/lon/alt` were already overwritten before that call. With
`POSINFO_INTERVAL` at 1800 s and a ~3 s window, a corrupt sample is transmitted with probability
~0.17 % per beacon — for `OE5HWN-14`, roughly once every five days, as a position at
48.25 N / 0.00 E (the Atlantic west of Le Mans). mcmap's RF-neighbour cross-validation
(`positionWithheld`) is what currently absorbs this.

### 4.5 The altitude is a single raw sample (GPS-03)

`gps_functions.cpp:1022`:

```c
meshcom_settings.node_alt = (int)gpsData.altitude;
```

No median, no mean, no hysteresis, no rejection of physically impossible vertical rates. Whatever
the last fix said is what gets beaconed 30 minutes later.

Two consequences the dashboard hint does not account for:

- **`--setalt` cannot work while GPS is on.** `command_functions.cpp:4131` writes `node_alt` and
  calls `save_settings()`, and the next fix overwrites it at `gps_functions.cpp:1022`. There is no
  guard. The dashboard's advice `--setalt 272` is therefore inert on this node.
- **QNH is latched to the first fix after boot (GPS-04).** `bmx280.cpp:326`:

  ```c
  if(fBaseAltidude == 0)
      fBaseAltidude = (float)current_alt;
  ```

  Set once, never corrected. This is why the reported QNH series is smooth (back-computing from
  the telemetry gives a constant ~260 m reference) while `alt` swings by 130 m. The cost is that a
  reboot whose first fix happens to be an outlier poisons QNH for the whole session: at this site
  the observed altitude range corresponds to about **±7 hPa** of QNH error. Given §3.3 — two
  reboots inside seven minutes — that is not hypothetical.

---

## 5. What these logs do and do not prove

**Proven:**

- The drain cadence, ring size, sentence set and update rate, all read from source or from the
  node's own boot output.
- One accepted corrupt sample, with a splice signature, in the absence of any nearby stall.
- One ESP32 reset, by three independent signals.
- The display path costs ~570 ms per update on this board.

**Inferred, consistent, not directly measured:**

- That the specific corrupt sample of §3.1 was produced by _this_ mechanism rather than by a
  module-level glitch. The rate match (§4.2) and the field pattern support it; a bench repro
  would settle it.
- The ~140 B/s figure rests on typical GGA/RMC lengths, not on a byte count from this module. The
  overflow conclusion tolerates a large error here (threshold 85 B/s).

**Not investigated:**

- Why the node reset (TM-51 — no instrument exists).
- Why the display costs 570 ms (TM-52).
- Whether the same overflow damages the T-Deck-Pro path, which uses `SerialGPS` and its own
  drain sites in `t-deck-pro/peri_gps.cpp`.

---

## 6. Findings table

| ID      | Where                                                                        | Finding                                                                                                                                                                                                                                        | Sev.   |
| ------- | ---------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------ |
| GPS-01  | `esp32_main.cpp:3079`, `configuration_global.h:178`, `gps_functions.cpp:889` | GPS UART drained only every 3 s; 420 B of NMEA into a 256 B ring → ~165 B discarded per cycle, permanently, on every stationary ESP32 node. nRF52 already polls at 1 s.                                                                        | High   |
| GPS-02  | `gps_functions.cpp:919`, `:959`, `:1009-1024`                                | No plausibility gate. `gpsData.valid` written and never read; the fix gate only checks sat/HDOP, which a damaged RMC leaves intact. A spliced sentence that passes the checksum is committed to persistent settings verbatim.                  | High   |
| GPS-03  | `gps_functions.cpp:1022`, `command_functions.cpp:4131`                       | `node_alt` is one raw GPS sample, unfiltered; beaconed every 30 min. `--setalt` is silently overwritten by the next fix.                                                                                                                       | Medium |
| GPS-04  | `bmx280.cpp:326` (and `bme680.cpp`, `bmp390.cpp`)                            | QNH reference altitude latched to the first fix after boot and never corrected; an outlier at boot costs ~±7 hPa for the whole session.                                                                                                        | Medium |
| TM-51   | boot path, both platforms                                                    | No reset reason is ever printed. A field reboot cannot be diagnosed even with a complete log.                                                                                                                                                  | Medium |
| TM-52   | `esp32_main.cpp:1844`, `:3365`                                               | Display section measures ~570 ms per update on the T-Beam Supreme; `setBusClock(400000)` is Heltec-only. Cause not yet explained (arithmetic is 3x short).                                                                                     | Medium |
| GPS-05a | design item, no single site                                                  | Moving-node altitude on boards **without** a pressure sensor: innovation gate + vertical-rate limit + speed-adaptive `q`. Runs on every board, needs no sensor. The stationary case needs none of this — §7.4 is already GPS-only. See §7.6.2. | Low    |
| GPS-05b | design item, no single site                                                  | Moving-node altitude on boards **with** a pressure sensor: baro/GPS complementary filter, mirroring mcmap's server-side `altSmoothed`. An upgrade, never a prerequisite. See §7.6.                                                             | Low    |

---

## 7. Fix

### 7.1 Rejected: raising the RX buffer

`GPSSerial.setRxBufferSize(1024)` would mask GPS-01 for one static RAM kilobyte per GPS-bearing
board. **Rejected on operator decision, 2026-09-01: the RAM budget is already the binding
constraint on this tree** (see `MEM-01`, the static-DRAM guard, and `MEM-02`, parked; the
E22-DevKitC has ~1.7 kB of DRAM headroom left, per the `CS-03` finding). Buying our way out of a
polling bug with static RAM is the wrong trade, and it would not fix GPS-02 either — a larger
buffer still delivers spliced sentences whenever the loop stalls long enough.

### 7.2 GPS-01 — split feed from evaluation

`WZ_GPS_Loop()` currently does two unrelated things: it drains the UART into `gps.encode()`, and
it evaluates the accumulated fix. Only the second belongs on a 3-second timer.

- Add a cheap `WZ_GPS_Feed()` that contains only the drain loop (`gps_functions.cpp:885-909`) and
  sets `updateGPSdata`. Call it every loop iteration, outside the `gps_refresh_timer` branch, on
  both platforms.
- Leave the evaluation block (`gps_functions.cpp:911-1053`) where it is, consuming
  `updateGPSdata`.
- Zero additional RAM. The drain is a `while (available())` over an empty ring in the common case.

Two details for the implementer:

- The `iGPSDEBUG > 2` NMEA echo collects into `msg_text` and prints per drain call. With a
  per-iteration drain it will fire far more often with far fewer bytes each; it needs to
  accumulate until `\r\n` or be moved into the evaluation half.
- The nRF52 path at `nrf52_main.cpp:1691` should get the same split for symmetry even though its
  1 s cadence does not currently overflow — it is one stall away from doing so.

### 7.3 GPS-02 — gate on validity, not just geometry

- Use `gpsData.valid`, or delete it. Preferably require `gps.location.isValid()` **and**
  `gps.location.age()` below a bound before touching `meshcom_settings`.
- Reject a sample whose latitude _or_ longitude is exactly 0.0 — the null island is never a legal
  MeshCom position and this alone would have caught §3.1.
- Reject a date the parser cannot have produced legitimately (`month > 12`, `day == 0`); the
  existing `gpsData.year > 2023` check at `:972` already establishes the pattern.

### 7.4 GPS-03 / GPS-04 — make the altitude an estimate, not a sample

> **Corrected 2026-09-01 after §7.6 was measured.** An earlier draft of this section proposed a
> median over N = 5 fixes (15 s). That is worth 2–5 % and is not worth shipping — the vertical
> error is still ~80 % correlated at 15 s (§7.6, table C). The window has to be minutes, not
> seconds.

Replace `node_alt = (int)gpsData.altitude` with a **scalar Kalman filter with constant `R`** —
which is a real Kalman filter, just not an HDOP-weighted one (§7.6 rejects the weighting, not the
filter):

```c
P += q;  K = P / (P + R);  x += K * (a - x);  P = (1 - K) * P;
```

- **State: two floats — 8 bytes.** No array, no matrices, no library.
- **In steady state this _is_ an exponential moving average**, so the measured EMA results carry
  over unchanged. The fixed point of the recursion is `p = (−q + √(q² + 4Rq)) / 2`, giving a
  constant gain `K_ss = (p+q)/(p+q+R)`. With `R = (4 m · 1.7 · 2.0)² = 185 m²`:

  | q        | K_ss       | equivalent EMA τ |
  | -------- | ---------- | ---------------- |
  | 0.001    | 0.0023     | 1292 s           |
  | **0.01** | **0.0073** | **410 s**        |
  | 0.1      | 0.0230     | 131 s            |

  Measured performance at that operating point: scatter from 4.36 / 4.08 m RMS down to
  **1.52 / 1.62 m**, worst single sample from 25.4 m to 2.6 m.

- **What the variance recursion buys over a fixed α is the cold start**, and it is worth having.
  Starting from `P₀ = 400` the gain begins at 0.68 and tightens to `K_ss`, so the estimate
  converges before it stiffens. Time to reach and hold ±2 m of the session median:

  |                 | fixed EMA τ = 300 s | KF `P₀ = 400`, `q = 0.01` |
  | --------------- | ------------------- | ------------------------- |
  | `gpsdebug1.txt` | 474 s               | **282 s**                 |
  | `gpsdebug.txt`  | 1116 s              | 1026 s                    |

  (The first log's figure is dominated by the §3.2 excursion, not by the cold start; the second is
  the clean comparison. The criterion is deliberately harsh — must reach ±2 m _and stay_.)

  **This is exactly what GPS-04 needs.** The QNH reference is latched after boot; today on the
  very first fix. Latching it on `P < threshold` instead gives a principled "the estimate has
  converged" signal that a fixed-α EMA cannot provide.

- Do **not** stretch `q` smaller than ~0.005. τ = 600 s is better on one log (0.94 m) and clearly
  worse on the other (2.61 m): past the decorrelation time the filter lags genuine drift instead
  of averaging noise.
- **Bypass or re-tune in TRACK mode.** A 400 s time constant on a moving node is a defect, not a
  feature. `bDisplayTrack` already switches the GPS cadence at `esp32_main.cpp:3076`; the
  estimator must switch with it. See GPS-05a in §7.6 for what to do there.
- Feed it through the GPS-02 innovation gate (reject `|a − x| > 15 m`). On the first log this
  rejects 5 samples and improves RMS to 1.43 m; on the second it rejects none. The gate's real
  job is GPS-02, the altitude gain is a bonus. Note that `P` gives the gate a principled
  threshold too (`|a − x| > 3·√(P + R)`) once the filter has converged.
- Make `--setalt` sticky: a non-zero user-set altitude wins over GPS, or at minimum the GPS write
  is skipped while a manual value is present. Decide explicitly and document it in `--help`;
  today the command is simply a no-op on any node with a GPS fix.
- Re-latch `fBaseAltidude` when `--setalt` runs, and when the filter has converged (`P` below a
  threshold) — **not** on the very first fix after boot, which is what GPS-04 does today.

### 7.5 TM-51 — print the reset reason

Three lines in the boot banner: `esp_reset_reason()` on ESP32 (and the nRF52 equivalent from
`NRF_POWER->RESETREAS`). This is the highest value-per-line item in the whole document — it is
what turns the next field reboot into a diagnosable event instead of another forensic exercise.

### 7.6 GPS-05a / GPS-05b — verdict on an HDOP-weighted Kalman filter: **rejected as specified**

Asked for during review, 2026-09-01. Measured against both field logs rather than argued, because
the proposal is plausible enough that reasoning alone would not have settled it.

**A. The premise is weak: HDOP is a poor predictor of the vertical error.**

Correlation of HDOP against the absolute altitude error, within each session:

| Log             | n   | σ(alt) | r(HDOP, \|err\|) |
| --------------- | --- | ------ | ---------------- |
| `gpsdebug.txt`  | 381 | 4.30 m | **+0.51**        |
| `gpsdebug1.txt` | 305 | 4.08 m | **+0.20**        |

So HDOP explains ~26 % of the error variance in one session and ~4 % in the other. It is not
nothing, but it is not a measurement-noise model. Binned by HDOP value the relation is not even
monotone — the _best_ geometry in the corpus (HDOP 1.4, n = 216) carries σ = 3.36 m, while HDOP
2.4 and 3.0 carry σ = 0.17 m and 0.28 m. (Caveat: HDOP is constant for long runs, so a bin is a
contiguous time block and the binning confounds geometry with epoch. The within-session
correlation above is the sounder number.)

Two structural problems compound this. **HDOP is the wrong DOP** — vertical error scales with
VDOP, typically 1.5–2× HDOP and not derivable from it. And **VDOP is never received**: the node's
own `$PCAS03,1,0,0,0,1,...` disables GSA, the only sentence that carries it. Re-enabling GSA adds
~70 B/s, which pushes the 3-second accumulation from 420 B to ~630 B — i.e. it makes GPS-01
materially worse. **GPS-01 must land before GPS-05b is even measurable.**

**B. Measured: the HDOP term contributes nothing.** Identical scalar Kalman filter, run over both
logs, once with `R = (UERE · 1.7 · HDOP)²` and once with `R` fixed at the session median. RMS
against the session median, metres:

| q     | HDOP-weighted R | constant R     | HDOP-weighted R | constant R      |
| ----- | --------------- | -------------- | --------------- | --------------- |
|       | `gpsdebug.txt`  | `gpsdebug.txt` | `gpsdebug1.txt` | `gpsdebug1.txt` |
| 0.001 | 1.59            | **1.57**       | 1.95            | 1.97            |
| 0.01  | 1.65            | 1.66           | 1.78            | 1.79            |
| 0.1   | 2.10            | 2.21           | 1.76            | 1.82            |

Third-digit differences, and the constant-`R` filter is marginally _better_ at the best setting.
Within a session HDOP spans only 1.4–2.1 or 2.1–3.3, so `R` moves by a factor of ~2.5 while the
gain is dominated by `q/R` and barely notices. The extra machinery buys nothing that is
measurable in the delivered product.

**C. The dominant error is time-correlated, which is what actually defeats the filter.**
Autocorrelation of the altitude deviation:

| lag             | 3 s   | 6 s   | 15 s  | 30 s  | 60 s  | 120 s |
| --------------- | ----- | ----- | ----- | ----- | ----- | ----- |
| `gpsdebug.txt`  | +0.98 | +0.93 | +0.76 | +0.48 | +0.24 | +0.06 |
| `gpsdebug1.txt` | +0.97 | +0.93 | +0.81 | +0.61 | +0.25 | −0.02 |

White noise would sit at ~0.00 from lag 1. Consecutive samples are 97–98 % correlated and the
error only decorrelates after 60–120 s — the §3.2 excursion is a smooth monotone 21 m ramp across
eight consecutive samples, which is a wandering bias, not noise. A Kalman filter whose `R` assumes
independent measurements will drive `P` down as if it had 305 independent samples when it has
perhaps 8–17, and then report high confidence while being metres wrong for minutes. That is the
classic overconfident-GNSS-KF failure, and no amount of HDOP weighting addresses it.

**D. A one-line EMA matches or beats the filter, for 8 bytes.** RMS against the session median /
worst single sample, metres:

| Estimator                       | `gpsdebug.txt` | `gpsdebug1.txt` | RAM     |
| ------------------------------- | -------------- | --------------- | ------- |
| raw (what ships today)          | 4.36 / 25.4    | 4.08 / 10.9     | 0 B     |
| median N=5 (15 s)               | 4.26 / 19.9    | 3.90 / 10.5     | 20 B    |
| median N=20 (60 s)              | 3.63 / 8.9     | 3.26 / 10.2     | 80 B    |
| median N=200 (600 s)            | 1.27 / 2.3     | 0.41 / 0.8      | 800 B   |
| Kalman, HDOP-weighted, best q   | 1.59 / 3.3     | 1.76 / 4.7      | ~24 B   |
| **EMA τ = 300 s**               | **1.52 / 2.6** | **1.62 / 3.4**  | **8 B** |
| EMA τ = 300 s + innovation gate | 1.43 / 2.4     | 1.62 / 3.4      | 8 B     |

**E. And all of it sits under the accuracy floor.** The two sessions are the same antenna twenty
minutes apart, and their medians differ by **4.6 m** (274.7 vs 279.3). That session-to-session bias
is not reducible by any within-session estimator. Driving the scatter from 1.6 m to 1.5 m is
invisible in a quantity whose bias is 4.6 m.

> **Note on what these numbers measure.** RMS is computed against each session's own median,
> because no ground truth exists for this site at metre resolution. It therefore measures _scatter
> reduction_, not accuracy, and it structurally flatters long windows (a 200-sample median of 305
> samples is nearly the session median by construction). The EMA-vs-Kalman comparison is unaffected
> — both are judged against the same reference — but the absolute figures should not be read as
> accuracy claims. Point E is the honest bound.

**Verdict.** Do not build an HDOP-weighted Kalman filter. It is measurably indistinguishable from
a constant-`R` filter on this data, it is beaten by an 8-byte EMA, it needs a DOP the node does not
receive, obtaining that DOP would worsen GPS-01, and it still requires the GPS-02 outlier gate in
front of it — so it saves no work anywhere. **Ship §7.4 (EMA τ ≈ 300 s + innovation gate) instead.**

**What stays in scope as GPS-05b**, deliberately narrow: an altitude estimator for the **moving**
case, where §7.4's time constant is wrong by construction. The right shape there is not a
GPS-only Kalman but a **complementary filter fusing the BME280 against GPS** — the barometer gives
sub-metre short-term relative altitude and the GPS supplies the absolute reference that stops baro
drift. mcmap already computes exactly this server-side (`altSmoothed`, "a complementary filter
fusing `alt` with the station's own `press`"), so the pairing is proven useful in this ecosystem
before we spend firmware on it. **Gate:** do not start until GPS-01 has landed, TRACK-mode field
logs with pressure exist, and the mcmap `altSmoothed` series has been checked against them. If a
Kalman filter is still wanted after that, it needs VDOP, adaptive `q` for the correlated error, and
a stationary/moving mode switch — three things this proposal did not include.

#### 7.6.1 "So no Kalman filter, then?" — no, you get one

The rejection in §7.6 is of the **HDOP weighting**, not of the filter. §7.4 ships a scalar Kalman
filter: prediction, innovation, gain, covariance update, eight bytes. What it does not do is
pretend that HDOP tells it something about vertical error, because measurement says it does not.

Two things follow that are worth stating plainly, because they were not obvious before the numbers
came in:

- The EMA and the Kalman filter are **not alternatives** here. A scalar KF with constant `R`
  converges to a constant gain, and that gain _is_ the EMA coefficient (§7.4 table). Choosing
  "EMA" over "Kalman" for this problem is choosing whether to keep the covariance around, not
  whether to filter.
- Keeping the covariance is worth its four bytes: it halves cold-start time (282 s vs 474 s) and
  it hands GPS-04 and the GPS-02 gate a convergence signal they otherwise have to fake.

#### 7.6.2 Nodes with no pressure sensor — GPS-05a

**Every number in §7.6 and §7.4 was computed from GPS altitude alone.** No barometer was used
anywhere in the analysis; both field logs are GPS-only altitude series. A node without a pressure
sensor therefore loses **nothing** from the stationary case — the 1.52 / 1.62 m result is already
the GPS-only result. GPS-05b (baro fusion) is an upgrade for boards that have a sensor, never a
prerequisite.

The firmware already knows at runtime which case it is in: `bBMPON`, `bBMEON`, `bBME680ON`
(`loop_functions_extern.h:90-95`).

What remains genuinely harder is a **moving node with no barometer**. Filed as **GPS-05a**, and
unlike GPS-05b it runs on every board. Three mechanisms, none needing a sensor:

1. **Innovation gate** (already GPS-02) — works regardless of motion.
2. **Vertical-rate limit.** Reject a sample implying an implausible climb rate. This is a physical
   bound, not a statistical one, so it survives motion: a handheld or vehicle node does not climb
   at 10 m/s. Cheap, and it is the only one of the three that would have caught the §3.2
   excursion's steepest step (5.2 m in 2 s = 2.6 m/s) without also rejecting real terrain.
3. **Speed-adaptive time constant.** `gpsData.speed_kmh` is already parsed
   (`gps_functions.cpp:925`) from RMC, which the node's own `$PCAS03` enables — so this is free.
   Long `q` when stopped, short when moving. **Caveat before anyone builds it:** GPS speed at rest
   is noisy and non-zero, so this needs a threshold with hysteresis and field validation.
   `bDisplayTrack` is the zero-risk discriminator that already exists and should be used first;
   speed-adaptation is a refinement on top, not the starting point.

**Honest limit.** For a genuinely moving GPS-only node, raw altitude is close to the best
available. A correlated error cannot be averaged out while the true value is also changing —
that is not a filter-design failure, it is the information that is present. Outlier rejection and
a rate bound are the honest deliverables there; anything promising metre-level altitude on a
moving GPS-only node is overselling.

---

## 8. Verification owed

Per the working rules, no fix ships without a test that fails before and passes after.

1. **Native, GPS-01:** feed a recorded NMEA stream through the parser with an injected 165-byte
   gap every 420 bytes; assert the resulting fix stream contains a committed sample with
   `lon == 0.0`. Fails on the current splice handling, passes with §7.3.
2. **Native, GPS-02:** hand the position-commit path a sample with `lon == 0.0` / `month == 14`
   and assert `meshcom_settings` is unchanged.
3. **Native, GPS-03:** feed the §3.2 altitude sequence through the EMA and assert the filtered
   output stays within ±3 m of the median (measured: worst sample 25.4 m raw → 2.6 m filtered).
   A second case must assert the estimator is bypassed when `bDisplayTrack` is set.
4. **Bench, GPS-01:** `DK5EN-14` (T-Deck Plus) or `DK5EN-92` (T-Beam) with `--gpsdebug 1` for two
   hours, before and after. Assert zero samples with `lon == 0.0` and no `Date:` outside the
   current month. Expected before: ~5 corrupt samples in 2 h; after: 0.
5. **Bench, falsification:** the same node with TRACK on (1 s cadence) must show the artefacts at
   a much lower rate even _before_ the fix. If it does not, §4.1 is wrong.

---

## 9. Provenance — is this ours?

**No.** Verified line by line.

| Construct                                       | Author                                      | Date         |
| ----------------------------------------------- | ------------------------------------------- | ------------ |
| `node_alt = (int)gpsData.altitude`, unfiltered  | Kurt, `50a326c6`                            | 2023-03-05   |
| `--setalt` writing `node_alt` with no GPS guard | Kurt, `371666fb2`                           | 2023-05-20   |
| `fBaseAltidude` latched on first fix            | Kurt, `af156f52b`                           | 2023-06-21   |
| `gpsData.valid` written and never read          | Kurt, `f8e95686`                            | 2026-03-12   |
| Fix gate `hdop < 6.0 && sat > 5`                | Kurt, `979630f7`                            | 2026-03-20   |
| `GPS_REFRESH_INTERVAL` 10 → 5 → **3**           | Kurt, `3c264dd49` (5→3)                     | 2026-04-23   |
| Drain only inside the refresh branch            | predates the `WZ_GPS` rewrite (`62cfc0a6b`) | ≤ 2026-04-06 |

`git diff upstream/dev HEAD -- src/gps_functions.cpp src/gps_functions.h src/bmx280.cpp
src/bme680.cpp src/bmp390.cpp src/command_functions.cpp src/loop_functions.cpp
src/esp32/esp32_main.cpp` is **empty**: the fork carries no delta anywhere in this path.

The fork's only commit ever touching a line named here is `ba2a8d740` (SIMP-30, 2026-08-18),
which changed the gate from the truncating `int posinfo_hdop` to `float fposinfo_hdop`. For
positive HDOP, `(int)h < 6` and `h < 6.0` are the same predicate — behaviour-neutral, and the
commit's actual purpose was to remove a duplicated variable that made the web UI and the display
disagree after a fix loss.

Note that upstream has been moving in the right direction: the cadence went 10 s → 5 s → 3 s, and
the sat/HDOP gate of 2026-03-20 replaced a bare `if(GPS_HasFix())`. GPS-01 is a bug upstream has
been unknowingly walking away from for two years without arriving.

**Upstream-worthiness:** GPS-01 and GPS-02 are good PR candidates — small, self-contained,
platform-symmetric, and they fix a defect that affects every ESP32 node in the network, not just
ours. GPS-03/GPS-04 change user-visible behaviour (`--setalt` semantics) and should be proposed as
a plan first, per §3.5 of the backlog.

---

## 10. Next actions

### 10.1 Cheapest falsification first

Before writing any code: put a bench node in TRACK mode with `--gpsdebug 1` for two hours and
count corrupt samples. §4.1 predicts near-zero at a 1 s cadence versus ~5 at 3 s. If TRACK shows
the same rate, the model in §4 is wrong and the module is the suspect instead.

### 10.2 Ask the reporter for

- A serial capture from power-on the next time the node reboots — with TM-51 in place first,
  otherwise it is worth nothing.
- Confirmation of how he captured `gpsdebug.txt` (net console on 2323, web GUI, or USB). This
  decides whether the missing boot banner in §3.3 is fully explained.

### 10.3 Log provenance

Both raw logs are `~/Downloads/gpsdebug.txt` (87 763 B, 2047 lines, md5
`9ca3b78500e995a8eb755e0df4b3a33e`) and `~/Downloads/gpsdebug1.txt` (72 932 B, 1705 lines, md5
`e8d1f335cd61b4001cdbe6f806209a7b`) on the maintainer's machine, and are **not in the
repository**.

**This is not theoretical: during the 2026-09-01 session both files disappeared from `~/Downloads`
mid-analysis and came back a few minutes later.** In that window the §7.6 measurements could not
have been produced — every number in §7.6 exists only because the files returned. Every claim in
this document quotes the lines it rests on, but §7.6's statistics cannot be re-derived from the
quotes alone. Copy both files into the field corpus.

---

## 11. Reply to the reporter (German, for forwarding)

> Deine Höhe ist im Median korrekt: das Geländemodell gibt für deine Koordinaten 269,9 m, dein
> Knoten meldet im Mittel 274,7 m bzw. 279,3 m. Die 220 m sind kein realer Standort, sondern
> einer der Ausreißer nach unten.
>
> Der eigentliche Fehler liegt tiefer. Die Firmware liest die GPS-Schnittstelle nur alle drei
> Sekunden aus, der Empfangspuffer fasst aber nur zwei Sekunden Daten. Es geht also in jedem
> Zyklus ein Stück NMEA verloren, und was der Knoten dann sendet, ist eine einzelne ungefilterte
> Rohmessung aus einem lückenhaften Datenstrom — ohne jede Plausibilitätsprüfung. Deshalb
> schwankt die Höhe, und deshalb steht in deinem Log an einer Stelle eine Position mit
> Längengrad 0,000000 und dem Datum 2015.14.00.
>
> `--setalt` hilft dir aktuell nicht: der Wert wird beim nächsten GPS-Fix wieder überschrieben.
> Das ist Teil desselben Befundes und wird mitbehoben.
>
> Zusätzlich hat dein Knoten am 01.09. um 20:26 und um 20:32 zweimal neu gestartet. Warum, können
> wir noch nicht sagen — die Firmware protokolliert den Reset-Grund bisher nicht. Das bauen wir
> als Erstes ein; danach wäre ein Mitschnitt ab dem Einschalten sehr hilfreich.
