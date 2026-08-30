# Automation runner runbook — full regression over USB serial on the bench fleet

Status 2026-08-29: everything below runs **by hand** from this checkout with the four nodes on
USB. Turning it into an unattended runner (TM-29) is open and low priority; this document is what
the runner would execute and what it must see. Nothing here needs network access to the nodes.

## 1. Prerequisites

| Item          | Requirement                                                                                                                                 |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| Host          | macOS with this repository, PlatformIO (`pio`), Python 3 with `pyserial`, `uv` (for `pytest`), `npx` (prettier, docs only)                  |
| Nodes         | T-Deck Plus `DK5EN-14`, Heltec V3 `DK5EN-93`, T-Beam v1.2 `DK5EN-92`, RAK4631 `DK5EN-90` — all on USB, same desk, same WLAN `ORBI63`        |
| Ports         | match by USB serial number (`ioreg -p IOUSB -l -w0 \| grep -E 'USB Product Name\|USB Serial Number'`), see `BACKLOG.md` §3.8f "Bench fleet" |
| Port hygiene  | no other client on a port (`lsof /dev/cu.*`); Chrome Web Serial closed                                                                      |
| Node settings | Heltec `--debug off`, GPS on, BME280 on; T-Beam track off, GPS on; T-Deck `--debug csv` (the harness tolerates both separators since today) |
| Branch        | `tdeck-partial-refresh-trace` (or fork main after merge-back), clean tree, `git log -1` noted in the run record                             |

Facts that bite: opening a port **reboots every ESP32 node** (~5 s to `CLIENT STARTED`, 9-14 s to
`[BOOT];ready`); the RAK does not reboot but stays silent unless `dtr=True`; the T-Deck ignores
serial for ~11 s after `CLIENT STARTED` and consumes one character per loop pass (the harness
sends slowly); esptool at 921600 fails on the CP2102/CH9102 bridges — use 460800.

## 2. Sequence

Run from the repository root; raw device logs land in `tools/bench/runs/` when the harnesses are
started from there.

### 2.1 Build gate (no hardware)

```bash
pio test -e native                                   # expect: 50 test cases: 50 succeeded
uv run --with pytest pytest tools/bench/test_tdeck_parse.py -q   # expect: 54 passed
for e in t_deck_plus t_deck heltec_wifi_lora_32_V3 heltec_wifi_lora_32_V4 heltec_wireless_stick ttgo_tbeam wiscore_rak4631; do
  pio run -e $e | grep -E "SUCCESS|FAILED"           # expect: SUCCESS for all seven
done
```

Build the targets **sequentially** — parallel `pio run` corrupts the shared build cache.

### 2.2 Flash

```bash
# T-Deck Plus (native USB, 921600 is fine)
pio run -e t_deck_plus --target upload --upload-port /dev/cu.usbmodem1101
# Heltec V3 and T-Beam: esptool at 460800, app only at 0xC0000 (full layout on first flash: see variants/*/platformio.ini upload_command)
pio pkg exec -p tool-esptoolpy -- esptool --port /dev/cu.usbserial-0001        -b 460800 write_flash 0xC0000 .pio/build/heltec_wifi_lora_32_V3/firmware.bin
pio pkg exec -p tool-esptoolpy -- esptool --port /dev/cu.usbserial-573C0005841 -b 460800 write_flash 0xC0000 .pio/build/ttgo_tbeam/firmware.bin
# RAK4631: serial DFU, node must be running (not in UF2 mode)
pio run -e wiscore_rak4631 --target upload --upload-port /dev/cu.usbmodem201301
```

Expected: `Hash of data verified` / `Device programmed`. Never flash a node while a harness holds
its port.

### 2.3 Per-node harnesses

```bash
cd tools/bench/runs
python3 ../tdeck_harness.py --scenario all --out tdeck-<date>.json             # T-Deck Plus, ~6 min
python3 ../oled_harness.py  --scenario all --out heltec-<date>.json            # Heltec V3, ~3 min
python3 ../oled_harness.py  --scenario all --port /dev/cu.usbserial-573C0005841 --out tbeam-<date>.json
```

Expected verdicts and numbers (as of 2026-08-29 evening):

| Harness / scenario        | Expected                                                                                                                                                                                           |
| ------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| T-Deck: all 13 (`--list`) | PASS. `audio_stall` loop max < 100 ms (now 23 ms); `idle` ~7 invalidations/s; `map` zoom 0..9, `center_err 0/0`, no crash; `input` trackball steps == requested, repaint p50 < 20 ms; ready 9-14 s |
| OLED Heltec: all 7        | PASS. `boot` shows `OLED on Wire1 ... ack=0 clock=400000`; `timing` flush avg ~32-35 ms, loop max < 60 ms                                                                                          |
| OLED T-Beam: all 7        | PASS. Flush avg ~38 ms. The harness normalises track mode and restores GPS                                                                                                                         |
| RAK                       | **no harness yet** (TM-26): manual `--info`, `--instr` (loop avg ~100 ms, max ~105 ms), OTA exchange below                                                                                         |

Exit code 1 from a harness = at least one FAIL; the JSON in `--out` has every step. Any
`Guru Meditation`, `rst:0x` beyond the port-open reset, `Backtrace:` or `abort()` in the raw log
is a failure regardless of verdicts.

### 2.4 Cross-node LoRa exchange (all four)

`scratchpad`-style script as used on 2026-08-29 (open all four ports, wait 50 s, `--loradebug on`
everywhere, `--sendpos` from each node in turn with 25 s spacing, `--mheard` on each, `--loradebug
off`). Expected: every node's beacon appears as `MH-LoRa` on the other three; every `--mheard`
lists the other three bench calls. This should become `tools/bench/mesh_exchange.py` (TM-26).

### 2.5 Operator-only checks (cannot be automated yet)

- Eye: T-Deck map centred with red dots during `map`; bubbles appear per `inject`; OLED pages show
  content (until TM-27 gives a frame-buffer CRC on Heltec/T-Beam — the T-Deck panel has no readback).
- Ear: message tone and start tone actually audible (`audio` scenario only proves the queue).
- Hand: `tools/bench/experiments/rolltest.py` — real trackball roll, expect `edges == events`.

### 2.6 Test traffic goes to group `TEST` (TM-42)

## Why `TEST`, not `9999`

Group `9999` is a real, server-visible group: the central server relays anything sent to it onward
to the map and dashboard like any other group traffic. Injected or beacon test messages sent to
`9999` therefore show up where real operators look, even though nothing about the message is real.

Group `TEST` (and `TESTER`) is different: `checkRegexCall()` in `src/regex_functions.cpp` already
lists both as accepted destination callsigns (explicit literal matches, same class as `BOT GATE` or
`WLNK-1`), so the firmware has always been willing to send and relay them — but the central server
filters the `TEST` group, so traffic sent to it never reaches the map or dashboard. It is the
correct default for anything that is not a real message: bench scenarios, the frame injector's
smoke tests, ad-hoc `--injectmsg` probes at the console.

`TEST`/`TESTER` are matched case-sensitively and as exact literals — `TESTX` or lowercase `test` do
not get the same treatment and fall through to the general callsign regex, which rejects both (see
`test/test_regex_call/test_regex_call.cpp`).

## What changed

- `src/test_inject.h` documents `TEST` as the destination group for `inject_text_message()`
  instead of the old `9999` example. The function still takes `dst` as a mandatory argument with no
  default — callers choose the group explicitly.
- `tools/bench/tdeck_harness.py` and `tools/bench/oled_harness.py` each define a module-level
  `TEST_GROUP = "TEST"` constant and use it everywhere they call `--injectmsg`.
- `tools/bench/experiments/gwflood.py` is unaffected: it injects raw, pre-built LoRa frames over UDP
  (destination `*`, broadcast, baked into the corpus-derived frame bytes) rather than calling
  `--injectmsg` with a group, so there is no `9999` usage to move.
- `0x9999` / `uintervall == 0x9999` in the position-beacon "send now" path
  (`src/extudp_functions.cpp`, `src/beacon_rate.h`) is an unrelated sentinel value, not a
  destination group, and is untouched by this change.

## Proving it once, end to end

On a node with `--gateway on`:

```
--injectmsg TEST hello
```

Expected:

- the frame appears in the node's own console/log (it was queued and sent like any other message);
- it does **not** appear on the map or dashboard, because the central server filters the `TEST`
  group before it gets that far.

This is a one-time hardware proof, not part of the automated bench scenarios (no hardware access in
this wave — the ports were busy). See `docs/automation-runner-runbook.md` for how it fits into the
regular bench run.

### 2.7 AP-reboot recovery run (TM-38)

Runs outside the Claude Code harness because the bench Mac loses its own WLAN for ~2 minutes:
`tools/bench/experiments/apreboot.py start ...` detaches, holds all four USB ports, prompts the
operator to power-cycle the access points and writes `summary.txt` on its own. Full procedure,
prerequisites (`--gateway on` on the three ESP32 nodes) and pass criteria:
[`bench-ap-reboot.md`](bench-ap-reboot.md).

### 2.8 OTA regression, country-server probe, T-Deck display test (TM-40, TM-39, TM-41)

- OTA: `python3 tools/bench/ota_regression.py --port <port> --env <env> --ip <ip> --settings-check`
  per WiFi node -- [`bench-ota-regression.md`](bench-ota-regression.md).
- Country servers: `cd tools/bench/runs && python3 ../experiments/srvprobe.py --port <port>` --
  [`bench-country-servers.md`](bench-country-servers.md).
- T-Deck display: `python3 tools/bench/tdeck_harness.py --scenario disptest` --
  [`tdeck-display-test.md`](tdeck-display-test.md).
- Back-pressure (BP-01): `::{TEST}...` burst on the console, assert `[BP];notice;QRS|QRT|QRV` and
  `[BP];refuse` lines; QTA needs relay load (`gwflood.py`) on top.

## 3. Run record

Per run, keep: branch + SHA, date/time, per-harness JSON, the raw `*_run_*.log` files, the numbers
from the table above, and the operator checks. Append a one-line summary to `BACKLOG.md` §3.8f
("Cross-board regression") when the run gates a commit.

## 4. Toward an unattended runner (TM-29, low priority)

- A Mac with the fleet attached runs 2.1-2.4 on a schedule (launchd or `/loop`), fails on any
  non-zero exit or crash marker, and posts the summary.
- Blockers to remove first: RAK harness (TM-26), OLED CRC (TM-27) so screens are asserted rather
  than eyeballed, the T-Deck audio needs a microphone or an I2S loopback to assert tones, and the
  four-node exchange as a script.
- Native tests run nowhere automatically today (fork workflows disabled) — the runner would be the
  first place they run unattended.
