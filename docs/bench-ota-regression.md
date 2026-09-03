# Bench OTA regression (TM-40)

Proves that a WiFi-capable node can be re-flashed over the air, unattended,
and comes back the same node it was: reachable, same hardware, WLAN rejoined,
settings unchanged, no crash loop. Covers the three ESP32 bench boards
(T-Deck Plus, Heltec V3, T-Beam v1.2); the RAK4631 has no WiFi and stays on
its UF2/DFU path, out of scope here.

Tools (this backlog item):

- `tools/webflash.py` — the OTA client library + CLI (safeboot trigger, poll,
  upload, poll). `flash()` is importable and returns a structured
  `OtaResult`; no `sys.exit()` anywhere below `main()`.
- `tools/bench/ota_regression.py` — holds one USB serial session open for the
  whole run, drives `webflash.flash()` against the node's HTTP server, and
  cross-checks the result against what the node itself prints on serial.
- `tools/bench/test_ota_regression.py` — the state machine and its
  assertions against an injectable fake serial port and fake HTTP node, no
  hardware required (`python3 -m unittest tools/bench/test_ota_regression.py`).

## Bench fleet (from `docs/BACKLOG.md`'s port table)

| Board       | Port                            | Env                      | Call     | Notes                                              |
| ----------- | ------------------------------- | ------------------------ | -------- | -------------------------------------------------- |
| T-Deck Plus | `/dev/cu.usbmodem1101`          | `t_deck_plus`            | DK5EN-14 | Reboots on open; wait for `CLIENT STARTED` (+~11s) |
| Heltec V3   | `/dev/cu.usbserial-0001`        | `heltec_wifi_lora_32_V3` | DK5EN-93 | Reboots on open even with dtr/rts low              |
| T-Beam v1.2 | `/dev/cu.usbserial-573C0005841` | `ttgo_tbeam`             | DK5EN-92 | Reboots on open (CH9102, dtr=False/rts=False)      |

`--gateway srv`/`--webserver on` must already be set on the node (the OTA
endpoint, `/callfunction/?otaupdate`, does not exist with the webserver off —
verify with `--info`, which prints `...Webserver on/off`). If it is off,
enable it once over serial before the first bench run:

```
python3 tools/bench/serial_session.py /dev/cu.usbserial-573C0005841 \
    --wait-boot "--webserver on"
```

This persists (`save_settings()`), so it only has to be done once per node.

## Running it

```bash
# T-Beam (DK5EN-92) -- host or IP, whichever answers on the WLAN the node joined
python3 tools/bench/ota_regression.py \
    --port /dev/cu.usbserial-573C0005841 --env ttgo_tbeam \
    --ip 192.168.1.90                       # or --host dk5en-92.local

# Heltec V3 (DK5EN-93)
python3 tools/bench/ota_regression.py \
    --port /dev/cu.usbserial-0001 --env heltec_wifi_lora_32_V3 \
    --host dk5en-93.local

# T-Deck Plus (DK5EN-14)
python3 tools/bench/ota_regression.py \
    --port /dev/cu.usbmodem1101 --env t_deck_plus \
    --host dk5en-14.local

# re-derive a verdict from a previous run's serial.log, no hardware
python3 tools/bench/ota_regression.py --parse-only tools/bench/runs/ota_<ts>/serial.log
```

Firmware defaults to `.pio/build/<env>/firmware.bin`; override with `--bin
PATH`. `--settings-check` turns a settings drift across the flash from a
warning into a hard failure (off by default -- see below). Every run writes
`tools/bench/runs/ota_<ts>/{serial.log,summary.json,summary.txt}` and exits
0 (PASS) or 1 (FAIL).

## What is asserted

1. **Reachable and identified** — `webflash.node_info()` against `/` reports
   the hardware string the env expects (`webflash.ENV_HARDWARE`: `TDECK_PLUS`
   / `HELTEC_V3` / `TBEAM`) before anything is triggered. A mismatch aborts
   before safeboot is ever entered (`--force` skips this).
2. **Safeboot round trip** — `/callfunction/?otaupdate`, poll `/update`,
   `/ota/start?mode=fr&hash=<md5>`, `POST /ota/upload`. The md5 sent to
   `/ota/start` is what the firmware's own `Update.setMD5()` verifies the
   upload against (`src/safeboot/ElegantOTA.cpp`) — a corrupted or
   mismatched upload is rejected before it ever reaches flash, so an
   `ota.ok` result already proves that exact image was written.
3. **A real reboot, witnessed on serial, not just HTTP** — the serial log
   must show safeboot's own `"OTA update finished successfully!"`, then the
   app's `[BOOT];ready;ms;...` and `[WIFI];event;got_ip;ms;...`. This is
   checked independently of what the HTTP poll saw, so a cached or stale
   HTTP response can't be mistaken for a genuine reboot.
4. **WLAN rejoined** — post-flash `--wifistat` reports a non-`0.0.0.0`
   `localip`.
5. **Hardware unchanged** — post-flash `--info` reports the same hardware
   string as the pre-flash snapshot and the env's expectation.
6. **Settings survived the flash** — a pre/post snapshot taken purely over
   serial (`--info`, `--maxhop`, `--lora`, `--wifistat`; a channel the OTA
   itself never touches) is diffed on callsign, webserver flag, maxhop
   text/pos, LoRa frequency/power/BW/SF/CR, and WLAN SSID. Settings live in a
   separate NVS partition the app-image OTA does not overwrite, so this is
   expected to hold — the check exists to catch a regression in that
   assumption, not because drift is likely. Off by default a mismatch is a
   `WARN`; `--settings-check` makes it a hard `FAIL`.
7. **No crash loop** — at most 2 `rst:0x` resets after the OTA trigger
   (app→safeboot, safeboot→app are both expected and legitimate resets); a
   3rd is flagged as a possible boot loop.

### On the "build fingerprint"

A bench run typically re-flashes the _exact bytes already running_ — there
is no second, differently-built image sitting around, and building one here
is out of scope (`pio run` races the other envs mid-wave). "The version and
build date/time read the same before and after" is therefore the _expected_
outcome, not a failure: what the run actually proves is the full
safeboot → upload → verify → reboot round trip completing and the node
landing back in the same state it left, evidenced by the serial reboot
markers and the settings snapshot — not a version bump. `summary.json`
records `ota_md5` (the hash the upload was verified against) and the parsed
`build` field of `--info` in both `before` and `after` so a run against a
genuinely different image (a real firmware update) shows the same fields
changing meaningfully.

## TM-49 arm — truncated upload must not switch partitions (owed)

Not yet run. This is the bench proof for the fail-closed completion gate in
`src/safeboot/ElegantOTA.cpp` (`_ota_image_valid`, false from `/ota/start`,
set true only after `Update.end(true)` plus `Update.isFinished()`).

Board: a **4 MB single-slot** ESP32 — the Heltec V3 (`DK5EN-93`) or the
T-Beam (`DK5EN-92`). The 16 MB T-Deck is the wrong instrument here: its
bootloader's slot validation masks the defect, which is why the original
observation was only indirect.

Procedure:

1. Hold a USB serial session open on the node (`tools/meshlogger.py` or the
   harness's own session) for the whole run.
2. Trigger safeboot and start an upload exactly as `webflash.flash()` does —
   `/callfunction/?otaupdate`, poll `/update`, `/ota/start?mode=fr&hash=<md5>`,
   then `POST /ota/upload`.
3. **Kill the upload mid-transfer** — close the TCP connection at roughly
   50 % of the body, before the last chunk. `curl --limit-rate` plus a
   `SIGKILL`, or a socket write that stops and closes, both do it.

Assertions:

- The POST completes with **HTTP 400**, body `Upload incomplete: image never
verified` (or the `Update` error string if one was set).
- Serial shows `[SAFEBOOT];ota;abort;reason;incomplete_upload` and **no**
  `[SAFEBOOT];ota;verify;result;ok`.
- Serial shows `[SAFEBOOT];ota;end;result;error` — **not** `success`, so
  `setBootPartition_APP()` never runs.
- The node stays in safeboot and recovers via the 180 s fallback, not by
  booting a half-written app image.

Control arm: the same run without the kill must still produce
`[SAFEBOOT];ota;verify;result;ok`, `result;success` and a normal reboot — the
gate must not have broken the good path.

## `--parse-only`

Re-derives the verdict from an existing `serial.log` with no hardware or
network: it splits the log on the last `"OTA update finished successfully!"`
line into a before/after half, parses each half's settings snapshot the same
way the live run does, and checks `[BOOT];ready`, WLAN reassociation, reset
count and the settings diff. Useful to re-score a run after tuning the
diff/timeout logic, or to inspect a log saved from a run interrupted before
it wrote `summary.json`.

## Measured run — T-Beam v1.2 (DK5EN-92), 2026-08-30

status: **not yet run** — the wave's shared `.pio/build/ttgo_tbeam/firmware.bin`
artefact this task was scoped to reuse (see the task brief: "byte-identical
to what runs on the node right now, flashed from this build at 19:07") was
not present in `.pio/build/` when this task ran; only `.pio/build/native`
existed at the start, and `t_deck_plus` / `heltec_wifi_lora_32_V3` appeared
under `.pio/build/` later (built by sibling wave agents) but `ttgo_tbeam`
never did in the time available. Building it here was out of scope (rule 6:
no `pio run` for any board env — the build cache races with the siblings).

Once `.pio/build/ttgo_tbeam/firmware.bin` exists, the real run is:

```bash
python3 tools/bench/ota_regression.py \
    --port /dev/cu.usbserial-573C0005841 --env ttgo_tbeam \
    --ip <DK5EN-92's localip, from --wifistat>
```

`tools/bench/test_ota_regression.py`'s `test_pass` exercises the identical
code path (serial session, snapshot diff, reboot-marker watch, `webflash.flash()`)
against a fake node standing in for exactly this scenario, so the state
machine itself is verified; only the hardware round trip is outstanding.
