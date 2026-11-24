# ESP32 Wi-Fi radio as SDR (ESP-SDR): fit for the bench fleet

Research note, 2026-11-24. Status: information only, nothing tried on hardware yet.

## Bottom line

- All three ESP32 boards in the fleet can run ESP-SDR. Only the **T-Deck Plus** (DK5EN-14) gets
  continuous IQ streaming. The **Heltec V3** (DK5EN-1) and the **classic T-Beam** (DK5EN-92) get
  snapshot spectra only.
- ESP-SDR is receive-only and covers only about **1.85-2.5 GHz** (5 GHz only on the ESP32-C5,
  which is not in the fleet).
- It is no use to MeshCom: it uses the ESP32's own 2.4 GHz radio, not the LoRa chip. 433/868 MHz
  is out of reach.
- It is a separate firmware that replaces MeshCom on the board. A normal MeshCom flash restores the
  node afterwards.

## What was found

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
  2.4 GHz. With a divider trick the ESP32, C3, S2 and S3 reach down to about 1.85 GHz.
- **PC software:** SDR++, SDR#, Gqrx and GNU Radio connect through the bridge program
  esp-sdr-bridge (SpyServer or rtl_tcp protocol), or SoapyESPSDR on the S31.
- **Flashing:** browser installer, or an ESP-IDF build (`idf.py flash`). Not an Espressif-documented
  feature.

## Fit per board

The USB path decides the mode: continuous streaming needs the chip's native USB, and a board whose
USB-C port goes through a USB-serial chip only gets snapshots.

| Board (bench node)     | Chip           | USB path                               | ESP-SDR modes                                        |
| ---------------------- | -------------- | -------------------------------------- | ---------------------------------------------------- |
| T-Deck Plus (DK5EN-14) | ESP32-S3       | Native USB (serial number is the MAC)  | Snapshot spectra plus continuous IQ up to 250 kS/s   |
| Heltec V3 (DK5EN-1)    | ESP32-S3       | USB-serial chip (serial number `0001`) | Snapshot spectra only, over UART                     |
| T-Beam (DK5EN-92)      | original ESP32 | USB-serial chip (serial `573C000584`)  | Snapshot spectra only, over UART                     |
| T-Beam Supreme         | ESP32-S3       | Native USB                             | Same as the T-Deck Plus (no such board on the bench) |

The USB paths are inferred from the USB serial numbers in `tools/bench/fleet.json`. Whether the
USB-serial chips on the Heltec V3 and T-Beam manage 2 Mbaud is not checked. If not, use the
1 Mbaud fallback.

## Practical value

- **Useful for:** looking at 2.4 GHz activity (Wi-Fi channels, Bluetooth, interference near a
  gateway) and narrowband work in the 13 cm amateur band through the continuous mode.
- **Not a replacement for a real SDR:** the wide modes are bursts with gaps, and the continuous mode
  is at most 250 kHz wide. The project says signal quality still needs work (a leftover spike at
  the center frequency) and calls the S31 streaming mode experimental.
- **Known pitfalls from the README:** a board powered from a USB-UART adapter's 3.3 V pin can
  brown out and reboot in a loop. On Linux, ModemManager may reset Espressif USB devices and has to
  be stopped or told to ignore the device.

## If we try it

- Board: T-Deck Plus (DK5EN-14), the only bench node with native USB.
- Bench rules still apply: load `/bench`, run `lsof` on the port first, never DK5EN-98. The node
  is off the mesh while ESP-SDR runs, because MeshCom is no longer on it.
- Afterwards, flash MeshCom back. Local fork builds need `export MC_AU_DEV_CHANNEL=1`.

## Sources

- [ESP-SDR README (ESPARGOS)](https://raw.githubusercontent.com/ESPARGOS/esp-sdr/main/README.md)
- [Hackaday: The ESP32, An SDR In Itself (2026-10-03)](https://hackaday.com/2026/10/03/the-esp32-an-sdr-in-itself/)
- [CNX Software: ESP-SDR firmware turns ESP32 into a 2.4/5 GHz SDR (2026-10-04)](https://www.cnx-software.com/2026/10/04/esp-sdr-firmware-turns-esp32-into-a-2-4-5-ghz-software-defined-radio-sdr/)
- [RTL-SDR.com: Various projects independently find hidden SDR capabilities in ESP32 microcontrollers](https://www.rtl-sdr.com/various-projects-independently-find-hidden/)
- [byteiota: ESP32 hidden SDR](https://byteiota.com/esp32-hidden-sdr-your-5-chip-can-receive-2-4-ghz-signals/)
- The Hacker News thread itself was not found. The sources above cover the same discovery.
