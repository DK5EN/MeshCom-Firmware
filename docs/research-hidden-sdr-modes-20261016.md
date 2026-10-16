# Research: hidden SDR modes in ESP32 and LR2021 (2026-10-16)

## Summary

- Three of the four bench nodes have an ESP32 with the undocumented WiFi baseband IQ mode: DK5EN-14, DK5EN-1 and DK5EN-92. It receives only and covers 2.4/5 GHz, so it does not reach the LoRa bands.
- None of the bench nodes has the Semtech LR2021, the LoRa chip with the hidden SDR mode. All of them use SX12xx radios.
- The MeshCom firmware does not support the LR2021: `platformio.ini:160` sets `-DRADIOLIB_EXCLUDE_LR2021=1`.
- For SDR experiments, the most practical setup is a Waveshare Core2021 module plus an ESP32-P4 board, for about 28 USD.

## Sources

- Hackaday, 2026-10-15, "Another wireless chip has a hidden SDR revealed":
  https://hackaday.com/2026/10/15/another-wireless-chip-has-a-hidden-sdr-revealed/
- Hackaday, 2026-10-03 (ESP32 hidden baseband mode); discovered independently by Espargos
  (espargos.net/espsdr) and Reddit user h0m3us3r (reddit.com/r/esp32/comments/1wq37xz/).
- lr2021-sdr repository: https://github.com/SAMS0N1TE/lr2021-sdr
- RTL-SDR.com, LakeShark update (LR2021 as SDR on the ESP32-P4):
  https://www.rtl-sdr.com/lakeshark-update-using-an-lr2021-lora-chip-as-an-sdr-for-portable-p25-pocsag-and-fm-reception-on-the-esp32-p4/

## 1. ESP32 WiFi baseband IQ mode

What the article says:

- The mode is available on ESP32 chips "from the earlier Tensilica hardware onwards". It is not available on the BLE-only parts or on the P4, which has no radio.
- It taps the I/Q stream before the WiFi modem, so external SDR software can read it.
- It receives only, on 2.4 GHz and on 5 GHz where the chip supports it.

What the article does not cover:

- How to unlock the mode.
- The sample rate. The Reddit URL slug mentions "ESP32-S3 at 80 MSPS", but the article text does not confirm it.
- Which ESP32 variants are affected. The article gives no per-variant list.

Bench nodes:

| Node     | Board       | MCU                | LoRa radio | IQ mode |
| -------- | ----------- | ------------------ | ---------- | ------- |
| DK5EN-14 | T-Deck Plus | ESP32-S3           | SX1262     | yes     |
| DK5EN-1  | Heltec V3   | ESP32-S3           | SX1262     | yes     |
| DK5EN-92 | T-Beam v1.2 | ESP32 (classic)    | SX12xx     | yes     |
| DK5EN-90 | RAK4631     | nRF52840 (no WiFi) | SX1262     | no      |

The two ESP32-S3 boards are the first candidates, because the 80 MSPS figure refers to the S3. This mode has no bearing on LoRa, since it does not reach 433 or 868 MHz.

## 2. LR2021 hidden SDR mode

- The chip has a sub-GHz radio covering 150-960 MHz and a 2.4 GHz radio.
- The SDR mode is 16-bit and can both receive and transmit. The project is at an early stage; its main demo is P25 digital voice, and RTL-SDR.com also shows POCSAG and FM reception.
- Reference hardware is the LilyGO T-Display-P4 in its LR2021 variant, with an ESP32-P4 host. The firmware is in `firmware/esp32p4`.
- The host talks to the LR2021 over plain SPI, limited to 16 MHz, and reads the FIFO.
- Article comments raise two points, neither verified: the hidden functions may exist for factory QA testing, and using them outside the certified frequency ranges may void the certification.

Bench status:

- No bench node has an LR2021.
- RadioLib is built with `RADIOLIB_EXCLUDE_LR2021=1` (`platformio.ini:160`), and no MeshCom variant targets the chip.
- The article does not mention an SDR mode for the SX1262 or the SX127x.

## 3. LR2021 hardware you can buy

Prices and stock come from search results on 2026-10-16. They change quickly, so check the product page and the frequency variant before ordering.

### Complete boards

| Board                               | Host MCU             | Price     | Notes                                                                                                |
| ----------------------------------- | -------------------- | --------- | ---------------------------------------------------------------------------------------------------- |
| LilyGO T-Display-P4, LR2021 variant | ESP32-P4 + ESP32-C6  | –         | SDR reference board. No shop listing found for this variant; the SX1262 variant costs about 200 USD. |
| Meshnology W12                      | ESP32-S3, 8 MB PSRAM | 69.99 USD | Sold out from the manufacturer; an Amazon listing exists.                                            |
| Seeed LR2021 LoRa Plus kit          | nRF54L15             | –         | Semtech development kit, not ESP32-based.                                                            |

### Modules

| Module                   | Bands                                   | Price                          |
| ------------------------ | --------------------------------------- | ------------------------------ |
| Waveshare Core2021 HF    | 850-930 MHz + 2.4 GHz                   | 12-13 USD (in stock at PiShop) |
| Waveshare Core2021 LF    | 410-510 MHz + 2.4 GHz                   | about 14 USD (Spotpear)        |
| Seeed Wio-LR2021         | Dual-band, 22-pin SMT part to solder on | 12.67 EUR, out of stock        |
| NiceRF LoRa2021F33-2G4   | 2 W sub-GHz, 1 W 2.4 GHz                | no price listed                |
| Coral RF LR2021 / N459AS | Gateway-oriented; N459AS is 5 W         | 25 USD                         |

### Recommendation

- SDR: two Waveshare Core2021 modules, HF for 868 MHz and LF for 70 cm, plus a Waveshare ESP32-P4-WIFI6 board. RTL-SDR.com describes this build at about 28 USD. The SDR firmware targets the ESP32-P4; whether it runs on an ESP32-S3 such as the Meshnology W12 is unknown.
- MeshCom: the LR2021 needs a firmware port before it is of any use. As a first step that means removing `RADIOLIB_EXCLUDE_LR2021` and adding a variant.
- Bench rules: the high-power modules (NiceRF 2 W, Coral 5 W) conflict with the 2 dBm bench TX limit. This does not matter for receive-only SDR use.

### Shop links

- Waveshare Core2021: https://www.waveshare.com/core2021-xf.htm
- PiShop Core2021 HF: https://www.pishop.us/product/core2021-hf-dual-band-lora-module-based-on-lr2021-transceiver/
- Spotpear Core2021: https://spotpear.com/shop/Core2021-HF-LF-LoRa-LR2021-Sub-GHz-2.4GHz/Core2021-HF.html
- eBay Core2021: https://www.ebay.com/itm/257496690113
- Seeed Wio-LR2021 (Openelab): https://openelab.io/products/seeed-studio-wio-lr2021-wireless-module-tape-reel-dual-band-lora
- NiceRF LoRa2021F33-2G4: https://www.nicerf.com/lora-module/lora2021f33-2g4.html
- Coral RF LR2021: https://www.coralradio.com/lr2021/
- Meshnology W12: https://meshnology.com/products/meshnology-w12-lr2021-ultra-long-range-lora-meshtastic-device-kit
- Meshnology W12 (Amazon): https://www.amazon.com/Meshnology-Development-Upgraded-ESP32-S3-Bluetooth/dp/B0H74Y877D
- CNX, T-Display-P4 with SX1262/LR2021: https://www.cnx-software.com/2026/03/31/t-display-p4-smartphone-like-devkit-features-esp32-p4-mcu-esp32-c6-wireless-soc-and-sx1262-lr2021-lora-transceiver/
- CNX, Seeed LR2021 LoRa Plus: https://www.cnx-software.com/2026/02/16/lr2021-lora-plus-board-combines-semtech-lr2021-and-nordic-nrf54l15-for-high-speed-flrc-and-lora-connectivity/
- Openelab T-Display P4 SX1262: https://openelab.com/products/lilygo-t-display-p4-sx1262
