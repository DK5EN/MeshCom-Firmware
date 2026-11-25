# RF spectrum on the bench fleet: ESP32 Wi-Fi SDR and SX1262 spectral scan

Research note, 2026-11-24. Status: information and plan only. Nothing tried on hardware, no code
changed.

## Bottom line

- **ESP32 Wi-Fi radio as SDR (ESP-SDR):** all three ESP32 boards in the fleet can run it, but only
  the T-Deck Plus (DK5EN-14) gets continuous IQ streaming. It covers only about 1.85-2.5 GHz and is
  no use for MeshCom.
- **LoRa radio (SX1262) spectral scan:** the firmware already has `--spectrum`. It is too coarse for
  the MeshCom channel (234 kHz steps over 10 MHz, about 17 ms per step).
- **The 234 kHz limit is very likely a myth.** The scan patch holds neither frequency nor
  bandwidth. Both are set with the chip's normal commands, and Semtech runs the same scan engine at
  234 kHz and 467 kHz.
- **The target is feasible:** a scan of ±0.5-1 MHz around 433.175 MHz with a filter of 10-40 kHz
  and several seconds per frequency. It needs new scan code and one bench test that proves narrow
  filters work.
- **Motivation:** an SDR shows LoRa chirps around the MeshCom frequency and a busy band. A narrow,
  long scan from a node itself shows what the node's own receiver is up against.

## Part 1: ESP32 Wi-Fi radio as SDR (ESP-SDR)

### What was found

Around late September and October 2026 several projects independently found an undocumented debug
path in the ESP32 radio. It copies the raw IQ samples (the radio's digitized signal, before any
decoding) into the chip's RAM before the Wi-Fi/Bluetooth modem processes them. On the S3 the
buffer is 64 KiB at `0x3fcd0000`.

The most complete project is **ESP-SDR** by ESPARGOS. It turns an ordinary ESP32 board into a
receive-only SDR:

- **Snapshot mode (all chips):** short bursts at 80, 40 or 16 MS/s (million samples per second),
  up to about 16,380 samples each, with gaps between bursts. Shown as FFT spectra with 256 to 2048
  bins.
- **Continuous mode (native USB only):** the second core filters the 16 MS/s stream down to
  15.625-250 kS/s and sends it without gaps over native USB. That is at most 250 kHz of bandwidth.
- **Transport:** a simple line-based protocol over native USB, or over UART at 2 Mbaud. Slow
  USB-serial chips (for example some CH340 boards) can drop to 1 Mbaud from the viewer.
- **Tuning:** the firmware accepts 100-6000 MHz, but the frequency synthesizer only locks around
  2.4 GHz. With a divider trick the ESP32, C3, S2 and S3 reach down to about 1.85 GHz. 5 GHz works
  only on the ESP32-C5.
- **PC software:** SDR++, SDR#, Gqrx and GNU Radio connect through the bridge program
  esp-sdr-bridge (SpyServer or rtl_tcp protocol), or SoapyESPSDR on the S31.
- **Flashing:** separate firmware that replaces MeshCom on the board. Browser installer, or an
  ESP-IDF build (`idf.py flash`). Not an Espressif-documented feature.

### Fit per board

Continuous streaming needs the chip's native USB. A board whose USB-C port goes through a
USB-serial chip only gets snapshots.

| Board (bench node)     | Chip           | USB path                               | ESP-SDR modes                                        |
| ---------------------- | -------------- | -------------------------------------- | ---------------------------------------------------- |
| T-Deck Plus (DK5EN-14) | ESP32-S3       | Native USB (serial number is the MAC)  | Snapshot spectra plus continuous IQ up to 250 kS/s   |
| Heltec V3 (DK5EN-1)    | ESP32-S3       | USB-serial chip (serial number `0001`) | Snapshot spectra only, over UART                     |
| T-Beam (DK5EN-92)      | original ESP32 | USB-serial chip (serial `573C000584`)  | Snapshot spectra only, over UART                     |
| T-Beam Supreme         | ESP32-S3       | Native USB                             | Same as the T-Deck Plus (no such board on the bench) |

The USB paths are inferred from the USB serial numbers in `tools/bench/fleet.json`. Whether the
USB-serial chips on the Heltec V3 and T-Beam manage 2 Mbaud is not checked. If not, use the
1 Mbaud fallback.

### Practical value

- **Useful for:** looking at 2.4 GHz activity (Wi-Fi channels, Bluetooth, interference near a
  gateway) and narrowband work in the 13 cm amateur band through the continuous mode.
- **Not a replacement for a real SDR:** the wide modes are bursts with gaps, and the continuous mode
  is at most 250 kHz wide. The project says signal quality still needs work (a leftover spike at
  the center frequency) and calls the S31 streaming mode experimental.
- **Known pitfalls from the README:** a board powered from a USB-UART adapter's 3.3 V pin can
  brown out and reboot in a loop. On Linux, ModemManager may reset Espressif USB devices and has to
  be stopped or told to ignore the device.
- **If tried:** use the T-Deck Plus (DK5EN-14). The node is off the mesh while ESP-SDR runs.
  Afterwards flash MeshCom back (local fork builds need `export MC_AU_DEV_CHANNEL=1`).

## Part 2: what the LoRa radio can tell about the spectrum

### Radio chip per bench board

| Board (bench node)     | LoRa chip         | Radio library  | `--spectrum`                                  |
| ---------------------- | ----------------- | -------------- | --------------------------------------------- |
| Heltec V3 (DK5EN-1)    | SX1262            | RadioLib       | Available                                     |
| T-Deck Plus (DK5EN-14) | SX1262            | RadioLib       | Available                                     |
| T-Beam (DK5EN-92)      | SX127x            | RadioLib       | Prints "not implemented"                      |
| RAK4631 (DK5EN-90)     | SX1262 (on nRF52) | SX126x-Arduino | Not built for nRF52, by reading the code only |

Sources: `variants/heltec_wifi_lora_32_V3/configuration.h` (`SX1262_V3`),
`variants/t_deck_plus/configuration.h` (`SX1262X`), `variants/ttgo_tbeam/configuration.h`
(`SX127X`). RadioLib is pinned at 7.6.0 in `platformio.ini`.

### Measurements already in the firmware

| Function                                          | What it tells you                                                                                                      | Where                                                     |
| ------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------- |
| `--spectrum` (spectral scan)                      | Power histogram per 200 kHz step over the band (see below)                                                             | `src/spectral_scan.cpp`, `src/command_functions.cpp:1688` |
| Instantaneous RSSI, `getRSSI(false)`              | Signal level right now, so a noise floor when nothing is transmitting. Logged as `rssi_inst` in the `RX_SILENCE` line. | `src/esp32/esp32_radio_diag.cpp:36`                       |
| Per-packet RSSI, SNR and frequency error          | How strong each packet was, how far above the noise, and how far the sender's crystal is off frequency                 | `src/esp32/esp32_main.cpp:5884`                           |
| Channel activity detection (CAD), `scanChannel()` | Whether a LoRa signal is on the channel now. Busy/total counters give a rough channel-load figure.                     | `src/esp32/esp32_main.cpp:4237`                           |

Not used yet: the chip's own counters for CRC and header errors (`GetStats`), which show packets
the node heard but could not decode.

For MeshCom's own traffic these in-band numbers (noise floor, CAD busy ratio, decode errors) are
more useful than a sweep: they need no switch away from LoRa receive, and the node stays on air.

### The existing `--spectrum`

- **Sweep:** 430-440.2 MHz, or 863-870 MHz when the node runs on 868, in 200 kHz steps.
- **Per step:** 2048 samples (about 17 ms) through a 234.3 kHz filter, then 100 ms pause. A full
  sweep takes about 6 s. The node cannot receive meanwhile; afterwards the code re-applies the
  MeshCom LoRa settings.
- **Output:** text on the serial console, one `FREQ` line and one `SCAN <33 counts> END` line per
  step, in the format of RadioLib's plot script `RadioLib/extras/SX126x_Spectrum_Scan/SpectrumScan.py`.
- **Limits:** the MeshCom channel (433.050-433.300 MHz at 433.175 MHz, 250 kHz wide) is about one
  step. 17 ms per step catches a packet only if it is on air at that moment.
- **Code comment:** `src/spectral_scan.cpp:72` claims other filter widths do not work and guesses
  that the width is fixed in the patch. The findings below contradict that.

### The scan patch

The patch comes from Semtech's reference software for their LoRa gateway chips
([sx1302_hal](https://github.com/Lora-net/sx1302_hal)). In those gateways a small companion radio,
the SX1261, does listen-before-talk and spectrum scans. RadioLib reuses a patch for the SX1262:
`RadioLib/src/modules/SX126x/patches/SX126x_patch_scan.h` (BSD licence).

**Loading** (`SX126x::uploadPatch()`, same sequence as Semtech's `sx1261_load_pram()`):

1. Standby on the internal RC oscillator.
2. Register `0x0610` = `0x10` enables patch writes.
3. Each word goes to patch RAM at `0x8000 + 4*i`.
4. Register `0x0610` = `0x00`, then command `0xD9` activates the patch.
5. The version string can be read at `0x0320` before and after.

The patch is volatile: a reset or power cycle removes it. Nothing is written permanently, so it
cannot brick the radio. RadioLib still calls it experimental with possibly undocumented side
effects.

**Contents:**

- RadioLib's blob is 65 words of 24 bits, about 200 bytes. Semtech's own file
  (`libloragw/src/sx1261_pram.var`) has 386 words, so RadioLib's is a different, shorter build.
  Neither carries a readable version string.
- Structure: repeating blocks that look like jumps into routines built into the chip (addresses
  `0x7E80`-`0x7FE1`), each ending in the terminator word `0x23FFFF`.
- No constants match a frequency or a filter-width setting. The chip's instruction set is not
  public, so it was not disassembled fully; the host-side evidence below settles the question.

**What it adds:** the measuring engine and nothing else.

| Item             | Address / opcode             | Meaning                                                                                        |
| ---------------- | ---------------------------- | ---------------------------------------------------------------------------------------------- |
| Start scan       | command `0x9B`               | Sample count (16 bit, max 65,535) and interval code (10 = 7.68 µs, 11 = 8.20 µs, 12 = 8.68 µs) |
| Status           | register `0x07CD`            | `0x00` none, `0x0F` running, `0xF0` aborted, `0xFF` done                                       |
| Results          | registers `0x0401`, 66 bytes | 33 counters of 16 bits, big-endian                                                             |
| Averaging window | register `0x089B`            | Default `0x05 << 2` = 20. Writing 0 stops the scan.                                            |

**Running one step** (`SX126x::spectralScanStart()`): write the averaging window, start receive
with no timeout, then send `0x9B`. The patch takes the given number of readings and sorts each into
one of 33 bins 4 dB apart.

- **RadioLib's description:** bin 0 is -11 dBm, each further bin 4 dB lower.
- **Semtech's:** each bin counts readings **above** its threshold (`-4*i + offset`, default offset
  -11 dB for the SX1261 path). Bins 31 and 32 share the lowest threshold (-135 dBm), and bin 32
  counts readings below it.

So one step gives a histogram, not an average. A steady noise floor gives one narrow peak. A signal
that was on air part of the time gives two peaks, noise and signal, and their ratio is the share of
time it was transmitting.

### How frequency, filter and gain are set

All standard SX126x commands, issued before a scan. None comes from the patch.

| Item             | Command                                      | Evidence                                                                                                                              |
| ---------------- | -------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| Frequency        | `SetRfFrequency` (`0x86`)                    | Semtech: `SX1261_FREQ_TO_REG(f) = f * 2^25 / 32 MHz`. RadioLib `setFrequency()` before each step.                                     |
| Band calibration | `CalibrateImage` (`0x98`), once per band     | Semtech sends `0x6B 0x6F` for 430-440 MHz, `0xD7 0xDB` for 863-870 MHz. One calibration covers a 2 MHz scan.                          |
| Filter width     | `SetModulationParams` (`0x8B`), byte 4 (FSK) | Semtech uses `0x0A` (234.3 kHz) for 125 kHz channels and `0x09` (467 kHz) for 250 kHz channels, with the same patch.                  |
| Receiver gain    | register `0x08AC` = `0xCB`                   | Semtech writes it in `sx1261_setup()` ("sensi adjust"). RadioLib defines `RADIOLIB_SX126X_RX_GAIN_SPECTRAL_SCAN` but never writes it. |

The SX1262 FSK receiver offers 21 filter widths (RadioLib `RADIOLIB_SX126X_GFSK_RX_BW_*`): 4.8, 5.8,
7.3, 9.7, 11.7, 14.6, 19.5, 23.4, 29.3, 39.0, 46.9, 58.6, 78.2, 93.8, 117.3, 156.2, 187.2, 234.3,
312.0, 373.6 and 467.0 kHz.

Two consequences:

- **The "234 kHz only" comment is very likely wrong.** Semtech runs the scan engine at two filter
  widths with the same patch. Narrow widths still need one bench test before relying on them.
- **Our scan probably runs below full sensitivity.** It never sets the `0xCB` gain value that
  Semtech uses for scanning.

## Part 3: plan for a narrow, long scan around 433.175 MHz

### Requirement

An SDR shows LoRa chirps around the MeshCom frequency and a busy band. Useful from a node:

- Scan **1 MHz, at most 2 MHz**, around the MeshCom frequency.
- **Several seconds per frequency**, so bursty traffic adds up in the bins.

### Getting several seconds per frequency

- **Per call:** at most 65,535 samples × 8.2 µs ≈ **0.54 s**, because the counters are 16 bits.
- **Longer totals:** add the results of many calls together in the ESP32, in 32-bit counters.
  There is no hard limit.
- **Visit frequencies in rotation:** do many quick passes and add each frequency's results across
  passes, instead of spending 5 s on one frequency before moving on. Traffic comes in bursts; one
  block of time per frequency would make the result depend on whether a packet happened to fall
  into it. Rotating spreads every frequency evenly over the whole observation time.

### Scan variants

| Variant   | Span     | Filter / step     | Steps | Time at 5 s per step | Resolution                                                        |
| --------- | -------- | ----------------- | ----- | -------------------- | ----------------------------------------------------------------- |
| Coarse    | ±1 MHz   | 117 kHz / 100 kHz | 21    | about 2 min          | Shows which 250 kHz channels are busy                             |
| Fine      | ±1 MHz   | 39 kHz / 25 kHz   | 81    | about 7 min          | About 10 steps across the MeshCom channel; LPD channels separable |
| Very fine | ±0.5 MHz | 9.7 kHz / 10 kHz  | 101   | about 8.5 min        | Narrowband carriers and spurs                                     |

Step slightly narrower than the filter, so neighbouring steps overlap and the sweep has no gaps.

### What to expect around 433.175 MHz

- **MeshCom channel:** 433.175 MHz, 250 kHz bandwidth, SF11, coding rate 4/6
  (`src/configuration_default.h`), spanning 433.050-433.300 MHz.
- **Noise floor:** about -114 dBm in a 234 kHz filter, about -122 dBm at 39 kHz (thermal noise plus
  about 6 dB receiver noise figure, estimated).
- **Weak MeshCom packets stay invisible:** LoRa at SF11 still decodes about 17 dB below the noise.
  A narrow filter does not help here, because LoRa power is spread over 250 kHz and each narrow
  slice gets proportionally less. The scan shows strong nearby stations and interference.
- **LPD433 walkie-talkies:** these licence-free handhelds use a 25 kHz channel raster starting at
  433.075 MHz. Channel 5 is exactly 433.175 MHz, and channels 1-10 fall inside the MeshCom channel.
- **Other 433 MHz devices:** remote controls and weather sensors (mostly around 433.92 MHz),
  70 cm repeaters, spurious emissions from nearby electronics.

### Constraints

- **The node is deaf during a scan,** for minutes. This is a dedicated scan mode for a bench node,
  not something to run during normal operation.
- **Fork-only:** new scan code goes into the instrumentation files with one-line hooks, per the
  project rules.
- **Bench rules:** the scan only receives. Load `/bench`, run `identity_guard.py` and `lsof` on the
  port first, never DK5EN-98.

### Next steps (not started)

1. **Bench test on DK5EN-1 (Heltec V3):** sweep 432.175-434.175 MHz at 234 kHz and then at 39 kHz,
   with register `0x08AC` = `0xCB` set. If both runs give plausible histograms and the MeshCom
   channel shows up at the right place, narrow filters work. Compare with the SDR waterfall.
2. **If it works, extend `--spectrum`:** settings for center, span, filter width, step and total
   time; rotating passes with 32-bit accumulation; the gain setting; output one line of 33 bins per
   step.
3. **Plot script under `tools/`:** a frequency-by-power map that can be laid over the SDR waterfall.
4. **In-band trend, independent of the scan:** log noise floor (`rssi_inst`), CAD busy ratio and the
   chip's decode-error counters over time in normal LoRa receive mode.
5. **SX127x (T-Beam DK5EN-92):** a slow sweep would also be possible with FSK mode and a narrow
   receive filter (down to 2.6 kHz), reading the signal level per step. New code; lowest priority.

## Open questions

- Do narrow filter widths work with the scan patch? Expected yes, not tested.
- Does the `0xCB` gain value measurably improve the scan on the SX1262 (Semtech sets it for the
  SX1261)?
- What does RadioLib's 65-word patch leave out compared with Semtech's 386-word one?
- Bin mapping: RadioLib and Semtech describe the lowest bins slightly differently (see above).

## Sources

ESP-SDR:

- [ESP-SDR README (ESPARGOS)](https://raw.githubusercontent.com/ESPARGOS/esp-sdr/main/README.md)
- [Hackaday: The ESP32, An SDR In Itself (2026-10-03)](https://hackaday.com/2026/10/03/the-esp32-an-sdr-in-itself/)
- [CNX Software: ESP-SDR firmware turns ESP32 into a 2.4/5 GHz SDR (2026-10-04)](https://www.cnx-software.com/2026/10/04/esp-sdr-firmware-turns-esp32-into-a-2-4-5-ghz-software-defined-radio-sdr/)
- [RTL-SDR.com: Various projects independently find hidden SDR capabilities in ESP32 microcontrollers](https://www.rtl-sdr.com/various-projects-independently-find-hidden/)
- [byteiota: ESP32 hidden SDR](https://byteiota.com/esp32-hidden-sdr-your-5-chip-can-receive-2-4-ghz-signals/)
- The Hacker News thread itself was not found. The sources above cover the same discovery.

SX1262 spectral scan:

- Semtech sx1302_hal: [`libloragw/src/loragw_sx1261.c`](https://github.com/Lora-net/sx1302_hal/blob/master/libloragw/src/loragw_sx1261.c),
  [`util_spectral_scan/src/spectral_scan.c`](https://github.com/Lora-net/sx1302_hal/blob/master/util_spectral_scan/src/spectral_scan.c),
  [`libloragw/inc/sx1261_defs.h`](https://github.com/Lora-net/sx1302_hal/blob/master/libloragw/inc/sx1261_defs.h),
  [`libloragw/src/sx1261_pram.var`](https://github.com/Lora-net/sx1302_hal/blob/master/libloragw/src/sx1261_pram.var)
- RadioLib 7.6.0 (local copy under `.pio/libdeps/*/RadioLib`): `src/modules/SX126x/SX126x.cpp`
  (`uploadPatch`, `spectralScanStart`, `spectralScanGetResult`), `SX126x_registers.h`,
  `SX126x_commands.h`, `patches/SX126x_patch_scan.h`, example
  `examples/SX126x/SX126x_Spectrum_Scan_Frequency`
- MeshCom fork: `src/spectral_scan.cpp`, `src/command_functions.cpp`,
  `src/esp32/esp32_radio_diag.cpp`, `src/esp32/esp32_main.cpp`, `src/configuration_default.h`
