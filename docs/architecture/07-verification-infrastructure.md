# 07 — Verification Infrastructure

> **What can actually be observed, driven and asserted on this firmware — and what
> should be added?**

[06 — Test Strategy](06-test-strategy.md) describes _what_ to test and in which layer.
This document is the concrete build-out: the instrumentation that already exists, the
hooks that are missing, and the physical bench design.

Written against the available hardware: **2 × Heltec WiFi LoRa 32 V3** (ESP32-S3 + SX1262)
connected to a MacBook.

## Guiding principle

The firmware is **well instrumented and badly asserted**. There are 50+ machine-readable
event markers, 218 command verbs, three independent I/O channels and a log parser that
already understands the event stream. What is missing is not observation — it is a layer
that turns observations into pass/fail, and enough determinism that a failure means
something.

So the work is mostly _connecting existing parts_, not building new ones.

---

## 1. Assets that already exist

### 1.1 The `[MC-SM]` / `[MC-DBG]` event stream

`src/lora_functions.cpp`, `src/loop_functions.cpp`, `src/esp32/esp32_main.cpp` and
`src/nrf52/nrf52_main.cpp` emit a structured event stream. Format and semantics are
already documented in [`docs/loradebug-serial-output.md`](../loradebug-serial-output.md).

**State machine — `[MC-SM]`**

`IDLE` · `RX_LISTEN` · `RX_PROCESS` · `TX_PREPARE` · `TX_ACTIVE` · `TX_DONE`

Every transition prints `rc=`; `rc != 0` is an error and `tools/serial_monitor.py` already
treats it as an alert.

**Events — `[MC-DBG]`**, grouped by what they let you assert:

| Group           | Markers                                                                                                                                                                       | Assertable property                                  |
| --------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------- |
| Deduplication   | `RX_DEDUP_NEW`, `RX_DEDUP_DUP`, `RX_DEDUP_ADD`                                                                                                                                | a repeated `msg_id` is recognised exactly once       |
| TX ring         | `RING_WRITE`, `RING_TX_READ`, `RING_STATUS`, `RING_PRIO`, `RING_OVERFLOW`, `RING_DROP_NEW`, `RING_DROP_PRIO`                                                                  | overflow drops the right entry, priority order holds |
| Channel access  | `CAD_SCAN`, `CAD_FREE`, `CAD_BUSY`, `CAD_FREE_NO_TX`, `CAD_FALSE_POSITIVE`, `CAD_ABORT_BY_RX`, `CAD_SAFETY_TIMEOUT`                                                           | CSMA actually defers; false-positive rate            |
| Transmission    | `TX_GATE_ENTER`, `TX_START`, `RADIO_TX`, `TX_DONE`                                                                                                                            | gate → start → done ordering, no lost TX             |
| Retransmission  | `RETRANSMIT`, `RETRANSMIT_GIVEUP`, `ACK_RECEIVED`, `ACK_SKIP_READY`                                                                                                           | retry count, ACK stops retries                       |
| Relaying        | `RELAY_QUEUED`, `RELAY_LOOP_BLOCKED`                                                                                                                                          | loop prevention works                                |
| RX buffers      | `RX_BUF_SWITCH`, `RX_BUF_RELEASE`, `RX_BUF_OVERWRITE`                                                                                                                         | double-buffer discipline, no overwrite under load    |
| RX errors       | `RX_ERROR`, `RX_OTHER_ERROR`, `CRC_ERROR`, `CRC_PAYLOAD`, `ERR_PAYLOAD`, `HDR_DETECT`                                                                                         | malformed frames are rejected, not parsed            |
| RX timing / IRQ | `RX_TIMEOUT`, `RX_TIMEOUT_FIRE`, `RX_TIMEOUT_DEFERRED`, `RX_RESTART`, `RX_RESTART_EARLY`, `RX_RESTARTED`, `RX_IRQ_STALE`, `RX_IRQ_STALE_EARLY`, `RX_FLAG_PROCESS`, `IRQ_POLL` | listener never gets stuck                            |
| Performance     | `ONRXDONE_TIME`, `ONRXDONE_STATS` (`max=…ms warn=… (>50ms)`)                                                                                                                  | RX callback stays inside its budget                  |
| Load            | `CHANNEL_UTIL`, `CHECKRX`                                                                                                                                                     | channel occupancy during a test run                  |

**Gating — important for test design:**

- Most markers are behind the **runtime** flag `bLORADEBUG`, enabled with `--loradebug on`
  and persisted to flash. `--loradebug on` also sets `bDisplayInfo` and `bDisplayRetx`.
- Five sites in `src/lora_functions.cpp` are behind the **compile-time** macro
  `LORA_ISR_DEBUG`, which is **not defined anywhere in the repository**.
  **Corrected 2026-07-31:** all five sit inside `#if defined BOARD_RAK4630`, so
  `-D LORA_ISR_DEBUG` is a **no-op on the Heltec V3** this document is written for — which
  makes §10 step 2 of the build-out order useless as written. The hook is real but
  RAK4631-only. See [08 C-06](08-defect-catalogue.md#c-06--remaining-concept-corrections--confirmed).

**Caveat to design around:** enabling the stream changes timing. `printfdeb` does a
blocking `Serial.printf` from inside the radio path, and the `ONRXDONE_TIME` counter warns
above 50 ms. Any timing assertion must either tolerate the instrumentation overhead or be
measured with the stream reduced. Do not treat "it passes with debug on" as proof it
passes with debug off — and vice versa.

### 1.2 The command surface as a test API

218 verbs, of which a large subset is directly useful as a scripted test interface:

| Purpose                 | Commands                                                                                                                       |
| ----------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| **RF setup**            | `--txfreq <MHz>` `--txpower <dBm>` `--txsf` `--txbw` `--txcr` `--setboostedgain on/off` `--setctry`                            |
| **RF readback**         | `--lora` → frequency, power, BW, SF, CR in one block                                                                           |
| **Deterministic TX**    | `--sendpos` `--posshot` `--sendtele` `--sendhey` `--sendtrack` `--msg` `--hey`                                                 |
| **State readback**      | `--info` `--mheard` `--mh` `--path` `--pos` `--values` `--tel` `--wx` `--io` `--regex` `--showi2c`                             |
| **Node identity**       | `--setcall` `--setname` `--setlat` `--setlon` `--setalt` `--symid` `--symcd`                                                   |
| **Behaviour toggles**   | `--gateway on/off` `--mesh on/off` `--relay on/off` `--shortpath on/off` `--setretx on/off` `--nomsgall on/off` `--via on/off` |
| **Debug channels**      | `--debug on/off/csv/man/en/de` `--loradebug` `--bledebug` `--gpsdebug` `--viadebug` `--wxdebug` `--softserdebug`               |
| **Timing**              | `--postime <s>` `--ptime` `--webtimer` `--utcoff` `--settime` `--setrtc`                                                       |
| **Channel measurement** | `--spectrum` `--specstart` `--specend` `--specstep` `--specsamples`                                                            |
| **Persistence**         | `--save` `--cleanflash` `--spiffs reset` `--reboot` `--format`                                                                 |
| **Remote access**       | `--netconsole on/off` `--passwd` `--webserver on/off`                                                                          |

This is unusually good coverage for scripted testing: almost every relevant state is both
**settable** and **readable** from outside, which is exactly what an integration harness
needs.

Two caveats:

- Ordering in `commandAction()` is load-bearing (see
  [04](04-complexity-and-duplication.md)) — `--setinfo off` must be matched before
  `--setinfo`. A harness that generates commands should use the exact literal strings.
- `--cleanflash` / `--format` / `--spiffs reset` are destructive. A test run should start
  from a known state, but reflashing config on every test wears flash. Prefer
  `--save`-based snapshots and restore, and reserve `--cleanflash` for the start of a
  session.

### 1.3 Three independent I/O channels

| Channel         | Transport                       | Driver                                      | Notes                                                                                                           |
| --------------- | ------------------------------- | ------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| **USB serial**  | UART / USB-CDC                  | `tools/serial_monitor.py`                   | Primary. Carries the full event stream.                                                                         |
| **Net console** | TCP port 2323, HMAC-SHA256      | `tools/hmac_connect.py`, `hmac_connect.ps1` | `src/net_console.cpp`. Challenge-response; password never transmitted. Open if `node_passwd` empty. ESP32 only. |
| **BLE**         | NimBLE, `MAX_MSG_LEN_PHONE 300` | phone app                                   | `commandAction(…, ble=true)` returns via `addBLECommandBack()`.                                                 |

**The net console is under-appreciated for bench work.** It frees the serial port, works
over WiFi or Ethernet, and lets the harness drive a node that is physically inconvenient —
or drive both nodes while the serial ports are reserved purely for capturing the event
stream. `--netconsole` reports current status and IP.

Note the trade-off recorded in the header of `src/net_console.h`: it replaced a TLS console
specifically to free ~36 KB of mbedTLS I/O buffers. Do not "upgrade" it back to TLS without
re-checking the RAM budget.

### 1.4 `--debug csv` — the machine-readable output mode

`bDEBUGCSV` (also settable persistently via `meshcom_settings.node_sset4 & 0x0001`) changes
`printfdeb` formatting:

- **off (`--debug man`)**: `;` in a format string is rendered as a space — human-readable.
- **on (`--debug csv`)**: `;` is preserved — the output becomes semicolon-separated.
- With `--debug de` additionally: `.` is rewritten to `,` for German CSV/Excel locales.

For a harness, `--debug csv --debug en` is the right combination: stable separators, dot as
decimal point.

### 1.5 Existing tooling

| Tool                                                                     | What it does                                                                                                                                                                                                                                               |
| ------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `tools/serial_monitor.py`                                                | Tracks the LoRa state machine, packet counts, alerts, periodic summaries. Logs to timestamped files. **Has `--replay <logfile>`** — the parser can run offline against captured logs. `--no-dtr` avoids the reset-on-open problem on CP2102-style bridges. |
| `tools/hmac_connect.py`                                                  | Net-console client (nonce → HMAC-SHA256 response).                                                                                                                                                                                                         |
| `tools/ram_snapshot.py`                                                  | RAM/flash snapshot across build targets; `/ram-snapshot` skill writes `docs/ram-comparison-*.md`.                                                                                                                                                          |
| `tools/loganalyse.sh`                                                    | Log post-processing; `/logauswertung` skill does the full analysis.                                                                                                                                                                                        |
| `tools/code_audit_scan.py`                                               | Static scan behind the `/code-audit` skill.                                                                                                                                                                                                                |
| `tools/meshcom_monitor/*.log`                                            | **17 captured sessions with raw frame hex + decoded interpretation.** The golden-vector corpus.                                                                                                                                                            |
| `tools/e213_monitor.py`, `tools/esp32_erase.sh`, `tools/dump_otadata.sh` | Board-specific helpers.                                                                                                                                                                                                                                    |
| `tools/arch_metrics.py`, `tools/arch_duplication.py`                     | Complexity and clone metrics behind this documentation set.                                                                                                                                                                                                |

### 1.6 Spectral scan — verifying the test conditions

`src/spectral_scan.cpp` (`--spectrum`, `--specstart/--specend/--specstep/--specsamples`)
sweeps 430.0–440.2 MHz using the SX126x. On a bench this answers _"was the channel actually
free while I measured CSMA?"_ — which is the difference between a valid CAD measurement and
a coincidence.

Run it before and after any timing test and record the result alongside the test output.

---

## 2. Determinism — what currently prevents reproducible results

### 2.1 CSMA backoff uses the hardware TRNG

`src/lora_functions.cpp:2076`:

```c
return base + (unsigned long)random(0, slots + 1) * CSMA_SLOT_SIZE;
```

On arduino-esp32, `random()` resolves to `esp_random()` — the **hardware true RNG** —
unless `randomSeed()` has been called. Verified in
`framework-arduinoespressif32/cores/esp32/WMath.cpp`:

```c
static bool s_useRandomHW = true;
void randomSeed(unsigned long seed) { if (seed != 0) { srand(seed); s_useRandomHW = false; } }
long random(long howbig) { uint32_t val = (s_useRandomHW) ? esp_random() : rand(); return val % howbig; }
```

**`randomSeed()` is never called anywhere in `src/`.** CSMA backoff is therefore genuinely
non-reproducible: two runs of the same test produce different timings.

Consequence for the bench: without a fix you can only assert _bounds and distributions_,
never values. The parameters you would be asserting against:

| Priority                | Base    | Slots | Max jitter | Used for                       |
| ----------------------- | ------- | ----- | ---------- | ------------------------------ |
| `MSG_PRIO_CRITICAL` 1   | 3000 ms | 10    | 350 ms     | ACK (`0x41`) + personal DM     |
| `MSG_PRIO_HIGH` 2       | 3000 ms | 10    | 350 ms     | group messages + broadcast `*` |
| `MSG_PRIO_NORMAL` 3     | 4500 ms | 10    | 350 ms     | mesh relay (forwarded packets) |
| `MSG_PRIO_LOW` 4        | 5500 ms | 10    | 350 ms     | position (`0x21`)              |
| `MSG_PRIO_BACKGROUND` 5 | 5500 ms | 10    | 350 ms     | HEY (`0x40`)                   |

Plus `CSMA_SLOT_SIZE 35 ms` (28 ms CAD + 2 ms TX-switch + 5 ms safety),
`CSMA_MAX_ATTEMPTS 3`, then rapid-fire with a `CSMA_RAPID_RX_MS 100` preamble window.
Retry reduction: −17 % on attempt 2, −33 % on attempt 3.

**Fix (one line, test builds only):**

```c
#ifdef MC_TEST_SEED
    randomSeed(MC_TEST_SEED);   // switches the core to the software PRNG
#endif
```

placed in `esp32setup()`. With `-D MC_TEST_SEED=12345` the backoff sequence becomes
byte-for-byte replayable, and you can assert exact slot selection and exact retry timing.

Do **not** ship this in release builds — deterministic backoff across a fleet would
synchronise collisions, which is the exact failure CSMA exists to prevent.

### 2.2 Wall-clock scheduling

Everything in `esp32loop()` is `if (millis() - x_timer >= interval)`. On hardware you
cannot fast-forward; a 15-minute telemetry interval takes 15 minutes. Mitigations:

- Use the shortest legal intervals for bench runs (`--postime`, `--ptime`, `--webtimer 0`)
  and document that the test ran outside production timing.
- For anything that needs time compression, use the host-side `FakeClock` from
  [06, Layer 3](06-test-strategy.md) — that is what it is for.

### 2.3 The live network

The captured logs contain real nodes — `DK5EN-98`, `DB0ED-99`, `DL2JA-2`, `DL7OSX-1`,
`DB0HOB-12`. An over-the-air bench in range of the live mesh measures _the mesh_, not your
change: foreign traffic triggers CAD, fills the dedup ring, and perturbs every timing
number. It also injects test frames into a production network.

This is the single biggest threat to result validity, and §4 addresses it.

---

## 3. Proposed test hooks

All gated behind one compile-time switch so release builds are bit-identical to today:

```ini
[env:heltec_wifi_lora_32_V3-test]
extends = env:heltec_wifi_lora_32_V3
build_flags =
    ${env:heltec_wifi_lora_32_V3.build_flags}
    -D MC_TEST_HOOKS
    -D MC_TEST_SEED=12345
    -D LORA_ISR_DEBUG
```

**Design rules:**

- Every hook inside `#ifdef MC_TEST_HOOKS`; nothing outside it changes.
- No hook may alter behaviour when the macro is off — verify with a RAM/flash snapshot
  diff against the release build.
- Hooks live in one new file (`src/test_hooks.cpp`) plus minimal call-sites, so the
  upstream diff stays reviewable under the project's "absolute minimum" rule.

### H-01 — `--inject <hex>` (highest value)

Feed a raw frame from serial straight into the RX path, as if the radio had delivered it.

```
--inject 21 50 35 A4 91 91 44 4B 35 45 4E 2D 39 38 ...
```

Implementation sketch: parse hex into a buffer, then call the same entry point `OnRxDone()`
uses, with synthetic `rssi`/`snr` (optionally settable: `--inject <hex> rssi=-95 snr=7`).

**What this unlocks:**

- The **entire RX path** — dedup ring, `decodeAPRS`/`decodeAPRSPOS`, `updateMheard`,
  `updateHeyPath`, routing decision, relay/forward, BLE and UDP enqueue — becomes testable
  on **one board**, with **no radio**, **deterministically**, in milliseconds.
- The 17 captured logs become a replayable regression corpus **against real hardware**,
  complementing the native golden vectors from [06, Layer 2](06-test-strategy.md).
- Malformed and hostile frames become testable without a transmitter. `OnRxDone()` is the
  single highest-consequence function in the firmware and currently has no test path at all.
- Dedup-ring wraparound (`MAX_DEDUP_RING`, observed wrapping in the field) becomes a
  scripted test instead of a field report.

Roughly 30–50 lines. This is the best value-per-line change available anywhere in the
test story.

### H-02 — `randomSeed(MC_TEST_SEED)`

As described in §2.1. One line.

### H-03 — `--dump <what>` state snapshot

A machine-readable dump of internal state that currently has no accessor:

| Target          | Contents                                                               |
| --------------- | ---------------------------------------------------------------------- |
| `--dump ring`   | TX ring: index, `msg_id`, priority, `RING_STATUS_*`, retry count, age  |
| `--dump dedup`  | dedup ring contents and write pointer                                  |
| `--dump mheard` | already partly covered by `--mheard`; add raw fields                   |
| `--dump heap`   | free / min-ever / largest block (the heap monitor already computes it) |

Asserting on `RING_STATUS_READY/SENT/DONE` transitions currently requires inferring state
from the event stream. A direct dump makes ring tests unambiguous.

### H-04 — `--faketime <ms>` (optional, lower priority)

Advance the internal timer base to trigger interval-driven work without waiting. Risky —
it interacts with the retransmission and CAD timers — so treat as optional and only if
§2.2's mitigations prove insufficient. The host-side `FakeClock` is the safer answer.

### H-05 — Counter export

An event-counter block (`RX total / dedup-dropped / relayed / TX attempted / TX succeeded /
CAD busy / retransmit / giveup`) printed on demand as one CSV line. Turns most assertions
from log-scraping into a single parse.

---

## 4. Bench design

### 4.1 Wired, not over the air

```
[Heltec V3 #A] --SMA-- [30–40 dB attenuator] --SMA-- [Heltec V3 #B]
      |                                                     |
   USB serial                                          USB serial
      \_______________ MacBook (harness) ________________/
```

Rationale:

- **Deterministic path loss.** No fading, no multipath, no antenna orientation.
- **Isolation from the live mesh** in both directions.
- **Repeatability**: the same test yields the same RSSI/SNR, so RSSI-dependent logic
  becomes assertable.

Attenuation: aim for a received level well above the SX1262 sensitivity floor but far below
saturation. Start around 40 dB and adjust using the RSSI reported in `MH-LoRa:` lines.
Never connect two transmitters directly without attenuation — front-end damage is a real
risk even at low power.

**If no attenuator is available:** `--txpower` to minimum, antennas removed, boards a few
metres apart, and `--txfreq` off the production frequency. Worse determinism, still far
better than a live-mesh over-the-air test. Record in the test log which mode was used.

### 4.2 Frequency plan

`--txfreq` validates against `430.0 + BW/200 … 439.0 − BW/200` MHz and
`869.4 + … 869.65 − …` MHz (`src/command_functions.cpp:3994`).

Pick a bench frequency inside 430–439 MHz that is clearly away from the production MeshCom
channel, set it on **both** nodes, and verify with `--lora` before every run. A test that
silently ran on the production frequency is worse than no test.

Operating a transmitter requires an appropriate amateur radio licence; both bench nodes
should identify with your own callsign via `--setcall`.

### 4.3 Roles

With exactly two boards:

| Role       | Node | Configuration                                                               |
| ---------- | ---- | --------------------------------------------------------------------------- |
| **DUT**    | A    | firmware under test, `--loradebug on`, `--debug csv --debug en`             |
| **Driver** | B    | known-good reference build, scripted via `--msg` / `--sendpos` / `--inject` |

Read **both** serial ports simultaneously — the harness needs the TX side to correlate what
was sent with what the DUT reports. Timestamp both streams from the host, not from the
device, so they share a clock.

Optional third node: a passive monitor with `--gateway off --mesh off` that only listens.
Useful for confirming what was actually on air when DUT and driver disagree. Not required
to start.

### 4.4 Practical notes

- `serial_monitor.py --no-dtr` avoids reset-on-open. Check which is needed for your board
  (`--no-dtr` is documented for the CP2102 path, plain for CDC-ACM).
- Serial port contention: close Chrome Web Serial before flashing, `lsof /dev/cu.*` to
  check (already recorded in the project `CLAUDE.md`).
- Use the net console (§1.3) for one node if you want its serial line reserved purely for
  capture.
- Start each session from a known state: `--cleanflash`, then a scripted config block,
  then `--save`.

---

## 5. On-device unit tests (`pio test`)

The most under-used capability available. PlatformIO can build a Unity test firmware,
upload it to the Heltec, run assertions **on the real MCU**, collect results over serial and
return an exit code.

```ini
[env:heltec_v3-hwtest]
extends = env:heltec_wifi_lora_32_V3
test_framework = unity
test_port = /dev/cu.usbserial-XXXX
test_speed = 115200
build_flags =
    ${env:heltec_wifi_lora_32_V3.build_flags}
    -D MC_TEST_HOOKS
    -D MC_TEST_SEED=12345
```

**What belongs here rather than in native tests:**

- flash/NVS persistence: write settings → reboot → read back
- ADC and battery conversion against a known input
- I²C bus enumeration (`scanI2C`)
- RadioLib init and `--lora` parameter round-trip on the real SX1262
- timing budgets that depend on the real MCU (e.g. `ONRXDONE_TIME` under load)
- with two boards and `test_port` per environment: real TX → RX assertions

**What does not belong here:** anything pure. Codec, CSMA math, mheard bookkeeping and
routing decisions belong in the native environment ([06, Layer 1](06-test-strategy.md)) —
they run in milliseconds there and need no hardware.

---

## 6. Assertion harness

`tools/serial_monitor.py` already parses the event stream and has a `--replay` mode. The
missing piece is assertions and an exit code.

Proposed: `tools/bench_runner.py` — a scenario runner that

1. opens both serial ports (or one serial + one net console),
2. drives configuration through the command surface,
3. triggers the scenario,
4. matches the resulting event stream against expectations,
5. exits non-zero on failure and writes a timestamped transcript.

Scenario sketch:

```yaml
name: dedup-rejects-replay
setup:
  dut:
    [
      "--cleanflash",
      "--setcall OE0TEST-1",
      "--txfreq 434.500",
      "--loradebug on",
      "--debug csv",
      "--debug en",
      "--save",
    ]
  driver: ["--setcall OE0TEST-2", "--txfreq 434.500"]
steps:
  - driver: "--msg hallo"
  - expect_dut: ["RX_DEDUP_NEW", "RX_DEDUP_ADD"]
  - driver: "--inject <same frame, same msg_id>"
  - expect_dut: ["RX_DEDUP_DUP"]
  - expect_dut_not: ["RELAY_QUEUED"]
assert:
  - counter: { relayed: 1 }
```

The `expect_dut_not` case is the important one: most real regressions are _extra_ actions
(a duplicate forwarded, a retransmit that should not have fired), and a harness that only
checks for presence will not catch them.

---

## 7. Test scenario catalogue

The concrete cases worth building, in rough priority order. Column "Needs" indicates the
cheapest sufficient environment.

| #   | Scenario                                                        | Asserted via                                         | Needs                   |
| --- | --------------------------------------------------------------- | ---------------------------------------------------- | ----------------------- |
| 1   | Decode every captured frame from the 17 logs correctly          | golden vectors, field-by-field                       | native                  |
| 2   | Decode → re-encode is byte-identical                            | round-trip                                           | native                  |
| 3   | Malformed / truncated / oversized frame is rejected, not parsed | `CRC_ERROR`, `ERR_PAYLOAD`, no `RX_DEDUP_ADD`        | native / `--inject`     |
| 4   | Frame length field honoured (buffer has stale bytes past `len`) | decoder ignores trailing garbage                     | native / `--inject`     |
| 5   | Duplicate `msg_id` recognised, not relayed                      | `RX_DEDUP_DUP`, absence of `RELAY_QUEUED`            | `--inject`              |
| 6   | Dedup ring wraparound (`MAX_DEDUP_RING` + 1 distinct ids)       | oldest evicted, no false duplicate                   | `--inject`              |
| 7   | Hop budget: `max_hop` decrements; frame at 0 is dropped         | `RELAY_QUEUED` present/absent                        | `--inject`              |
| 8   | Relay loop prevention (own call already in path)                | `RELAY_LOOP_BLOCKED`                                 | `--inject`              |
| 9   | TX ring overflow drops per policy, indices stay consistent      | `RING_OVERFLOW`, `RING_DROP_NEW/PRIO`, `--dump ring` | `--inject` + hooks      |
| 10  | Priority ordering under contention (CRITICAL before BACKGROUND) | `RING_PRIO`, `RING_TX_READ` order                    | `--inject` + hooks      |
| 11  | CSMA backoff within `[base, base + slots × 35 ms]` per priority | `TX_GATE_ENTER` → `TX_START` deltas                  | 2-node bench            |
| 12  | CSMA backoff exact sequence with seeded PRNG                    | as above, exact values                               | 2-node + `MC_TEST_SEED` |
| 13  | CAD defers TX while the channel is busy                         | `CAD_BUSY`, no `TX_START`                            | 2-node bench            |
| 14  | CAD false-positive rate on an idle channel                      | `CAD_FALSE_POSITIVE` count vs `--spectrum`           | 2-node bench            |
| 15  | Retransmit fires after ACK window, stops on ACK                 | `RETRANSMIT`, `ACK_RECEIVED`                         | 2-node bench            |
| 16  | Retransmit gives up after the limit                             | `RETRANSMIT_GIVEUP`                                  | 2-node bench            |
| 17  | RX callback stays inside budget under load                      | `ONRXDONE_STATS max=`, `warn=0`                      | 2-node bench            |
| 18  | RX listener never gets stuck (no `RX_IRQ_STALE` accumulation)   | long soak, event counts                              | 2-node soak             |
| 19  | Settings survive reboot                                         | write → `--save` → `--reboot` → `--info`             | on-device               |
| 20  | RF parameters actually applied                                  | `--txsf/--txbw/--txcr` → `--lora` round-trip         | on-device               |
| 21  | BLE: message > 255 bytes arrives intact                         | phone app / BLE client                               | on-device               |
| 22  | BLE: pair / unpair / re-pair after bond deletion                | phone app                                            | on-device               |
| 23  | Heap does not trend down over a long soak                       | heap monitor line, 60 s interval                     | soak                    |
| 24  | RAM/flash footprint unchanged by a refactor                     | `tools/ram_snapshot.py` diff                         | build only              |
| 25  | All 32 environments still build                                 | CI matrix                                            | build only              |

Scenarios 1–10 need **no radio at all** once `--inject` exists. That is the point of H-01.

---

## 8. Coverage: what 2 × Heltec V3 does and does not reach

Both boards are identical — ESP32-S3 + SX1262 — so the bench validates the **shared core**
with high fidelity and the **variant matrix** not at all.

| Covered                                             | Not covered                                                                     |
| --------------------------------------------------- | ------------------------------------------------------------------------------- |
| LoRa RX/TX state machine, CAD, CSMA, retransmission | SX1268 and SX127x radio paths                                                   |
| Wire format encode/decode on real hardware          | nRF52 path entirely (`wiscore_rak4631`, `heltec_t114`, `t_echo`)                |
| Dedup, ring buffers, relay decisions                | `nrf52loop()` — the duplicated scheduler's other half                           |
| mheard / HEY path bookkeeping                       | 4 display stacks: e-paper, LVGL/T-Deck, T-Deck-Pro, T5                          |
| BLE phone interface (NimBLE on S3)                  | the 17 boards on `batt_function_old.cpp` (V3 itself is one of them — see below) |
| WiFi/UDP backhaul, net console                      | Ethernet (`T-ETH-ELITE`), modem (`T_Connect_Pro`)                               |
| Flash/NVS persistence, heap behaviour               | PMU variants (XPowersLib boards), PSRAM boards                                  |
| U8g2 OLED path                                      | GxEPD2, TFT_eSPI, epdiy, LVGL                                                   |
| SF/BW/CR/power parameter handling                   | 29 of 32 build environments                                                     |

**One useful coincidence:** `heltec_wifi_lora_32_V3` is one of the 17 boards still on
`batt_function_old.cpp` ([04](04-complexity-and-duplication.md)). The bench can therefore
be used to characterise the old battery path and validate the `USE_NEW_BATT` migration for
it — one of the 17 done, with evidence.

This coverage gap is not an argument against the bench. It is the argument for keeping the
native layer ([06, Layers 1–2](06-test-strategy.md)) as the primary regression net: it is
the only layer that covers logic shared by all 32 environments.

---

## 9. Gaps found while surveying

| ID   | Finding                                                                                                                                                                                             | Fix                                                  |
| ---- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------- |
| V-01 | `heltec_wifi_lora_32_V3` has **no `monitor_filters = esp32_exception_decoder`**, while 12 other variants do. A crash on the primary test board yields raw addresses instead of a decoded backtrace. | one line                                             |
| V-02 | `randomSeed()` is never called → CSMA is irreproducible (§2.1).                                                                                                                                     | one line, test build                                 |
| V-03 | `LORA_ISR_DEBUG` guards 5 ISR-path traces in `lora_functions.cpp` but is **defined nowhere**. Undocumented dormant hook.                                                                            | document + test env                                  |
| V-04 | No packet-injection path exists → `OnRxDone()`, the highest-consequence function, has no test entry point.                                                                                          | H-01                                                 |
| V-05 | Debug output is emitted with a blocking `Serial.printf` from the radio path; enabling it perturbs the timing it measures.                                                                           | design around; consider a ring-buffered logger later |
| V-06 | `.gitignore:10` is `.*`, so dotfile-based tooling config (e.g. `.prettierignore`) is silently untracked.                                                                                            | negation rule or `git add -f`                        |

### On JTAG debugging — correction

An earlier assumption that the Heltec V3's USB-C is wired to a CP2102 bridge does not match
the board definition: `platform-espressif32`'s `heltec_wifi_lora_32_V3.json` declares
`hwids: [["0x303A", "0x1001"]]` — Espressif's own VID/PID for the ESP32-S3 **native
USB-Serial/JTAG** peripheral, not Silicon Labs (`0x10C4`).

If the board enumerates with that VID/PID, on-chip JTAG is present and
`debug_tool = esp-builtin` gives GDB with hardware breakpoints over the same USB cable, no
extra adapter. Confirm empirically when you plug a board in:

```bash
ls /dev/cu.*                       # 303A:1001 usually appears as usbmodem*
system_profiler SPUSBDataType | grep -A4 -i "vendor id"
```

Note that the firmware sets `-DARDUINO_USB_MODE=1` with `ARDUINO_USB_CDC_ON_BOOT`
commented out, so `Serial` goes to UART0 regardless. Enabling JTAG debugging and keeping
the serial event stream may therefore need both paths — worth checking before planning
around breakpoints.

---

## 10. Build-out order

| #   | Step                                                                | Effort     | Hardware | Unblocks                            |
| --- | ------------------------------------------------------------------- | ---------- | -------- | ----------------------------------- |
| 1   | V-01 exception decoder for `heltec_wifi_lora_32_V3`                 | minutes    | no       | readable crash reports              |
| 2   | H-02 `randomSeed` + `MC_TEST_HOOKS`/`MC_TEST_SEED` test environment | minutes    | no       | reproducible CSMA                   |
| 3   | H-01 `--inject <hex>`                                               | ~30–50 LOC | no       | scenarios 3–10 without a radio      |
| 4   | Vector extractor over `tools/meshcom_monitor/*.log`                 | small      | no       | scenarios 1–2, native and on-device |
| 5   | `tools/bench_runner.py` assertion harness with exit code            | small      | no       | everything below becomes pass/fail  |
| 6   | H-03 `--dump ring/dedup`, H-05 counters                             | small      | no       | scenarios 9–10 unambiguous          |
| 7   | `pio test` environment + first on-device tests                      | medium     | 1 board  | scenarios 19–20                     |
| 8   | Wired 2-node bench (§4)                                             | medium     | 2 boards | scenarios 11–18                     |
| 9   | CI: build matrix on PR (see [02, B-07](02-build-and-variants.md))   | small      | no       | scenario 25                         |
| 10  | Long-run soak + heap trend                                          | low        | 2 boards | scenarios 18, 23                    |

Steps 1–6 need no hardware and are what make the bench produce _findings_ rather than
_impressions_. Step 8 is then reserved for what genuinely requires radio.
