# Upstream issue drafts (2026-11-25), not filed

Drafts for icssw-org/MeshCom-Firmware, English, to be assigned to DK5EN. Filed only after the operator's go.
Campaign: [campaign-rc-bench-20261125.md](campaign-rc-bench-20261125.md). The combined PR closes them with
`Closes #N` once they have numbers.

---

## Draft 1: Auto Update fails with "Not enough free memory" after uptime; T-Beam resets during the staging erase

**Title:** Auto Update: download refused for low memory after uptime (ESP32 without PSRAM), T-Beam watchdog reset
during staging

**Body:**

Two Auto Update failures on ESP32 boards without PSRAM, both present in release 11.21 and 11.22.

**1. "Not enough free memory" after the node has run for a while**

- Field reports after the 11.22 rollout (2026-11-22): a Heltec V3 failed three download attempts after running
  overnight; a reboot did not help, a power cycle did. A T-Beam Supreme and a Heltec V3 succeeded only on the 5th to
  10th attempt.
- Cause: the download needs 66-69 KiB of internal heap at its peak (measured on Heltec V3 and T-Beam, 2026-11-23).
  Half of it are the two mbedTLS record buffers of 16,717 bytes each, which must exist at the same time. After uptime
  the heap is fragmented; the gate before the download refuses, or the TLS setup fails after it passed.
- On the classic ESP32 the dry-run inflate check after the download needs a further 32 KB dictionary plus 11 KB of
  state, which no longer exist once WiFi is up.

**2. T-Beam resets with `task_wdt: IDLE0` about 6 s into the install**

- The staging erase holds CPU0 for 260-900 ms per 64 KB block (19-20 blocks); with only a 1-tick yield between blocks
  the idle task on CPU0 starves past the 5 s task watchdog.
- Seen on release 11.21 and later builds: 4 of 11 T-Beam installs that reached the erase reset (bench, 2026-11-24).

**Proposed fix (PR to follow):**

- Admission rules before the download that measure what the job needs, and a lean TLS client for the update.
- One buffer per update job, allocated while the large free block still exists: it holds both TLS record buffers
  during the download and then serves as the inflate dictionary.
- Staging erase: a real sleep after each block and a bounded wait until the CPU0 idle task ran.
- On the T-Beam: the image check after the download moves to a verify boot before WiFi when memory is short.

**Bench evidence:** Heltec V3 and T-Beam, 2026-11-24 (10 of 10 web installs on the T-Beam, no watchdog reset).
Re-measured without any vendored library code on 2026-11-25: [to fill from step 3].

**Not covered:** T-Beam Supreme, Wireless Tracker and E22 boards were not measured.

---

## Draft 2: INA226 calibration is not written for common shunt values

**Title:** INA226: calibration silently not written when imax x shunt exceeds 81.9 mV (reads 0 mA)

**Body:**

- **What happens:** with `--shunt 0.1` and the default `--imax` of 20 A the INA226 reads 0 mA.
- **Cause:** the RobTillaart library `INA226::setMaxCurrentShunt()` refuses a pair whose full-scale shunt voltage is
  above 81.9 mV (0.1 Ohm x 20 A = 2 V) and then writes no calibration. The firmware ignores the return value
  (`src/ina226_functions.cpp`, `setupINA226`).
- **Field case:** Heltec V3 with a 0.1 Ohm module, 2026-11-22.
- **Proposed fix (PR to follow):** keep the shunt value and lower imax in 10 mA steps until the pair fits; `--imax`
  refuses a pair over the limit; a stored pair that does not fit is clamped at boot and the rejection is logged.
  Also: the web label of the INA226 bus voltage shows a stray `<`.
- **Verification:** host tests only (7 cases, the library's own limit as the oracle). No INA226 on our bench.

---

## Draft 3: `--postime` has no effect with `--gps on` and no GPS module

**Title:** ESP32: with `--gps on` and no GPS module the position interval falls back to 1800 s; `--postime` is
ignored

**Body:**

- **What happens:** a node with `--gps on` but no detected GPS module sends its position beacon every 30 minutes,
  whatever `--postime` says.
- **Where:** `src/esp32/esp32_main.cpp` (upstream/dev `68286e07`, around line 4670). With `bGPSON` set and
  `gpsDetected` false, `igps` stays 0. The `else` branch counts `no_gps_reset_counter` and after 10 GPS polls sets
  `posinfo_interval = POSINFO_INTERVAL` (1800 s). The `node_postime` value is only used in the `!bGPSON` branch.
- **Expected:** without a GPS module (or without a fix) the configured `--postime` applies, as with `--gps off`.
- **Found:** bench, 2026-11-22, while checking the position beacon timing. Not a regression.
- **Proposed fix:** in the `igps == 0` branch use `node_postime` when it is set, `POSINFO_INTERVAL` otherwise. Not
  implemented yet.
