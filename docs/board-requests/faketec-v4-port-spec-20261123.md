# TECHNICAL PORT SPECIFICATION & INTEGRATION REPORT

## Target Board: FakeTec v4 (Nordic nRF52840 + Semtech SX1262 HT-RA62) for MeshCom 4.x

---

### 1. Introduction & Scope

This document consolidates the complete development efforts for integrating native hardware support for the **FakeTec v4** board (Pro Micro nRF52840 / Nice!Nano footprint + Heltec HT-RA62 LoRa module + 0.96" SSD1306 OLED + GPS) into the MeshCom codebase.

It unifies the **low-level hardware enablement layer** (solving power gating, clock sources, NFC pin unmasking, SPI routing, and TCXO voltages) with the **upstream refactoring introduced by the fork maintainer** (preprocessor encapsulation, regional PL frequencies, and companion app integration).

The port has been fully verified on physical hardware over the air: successful two-way APRS frame exchange, multi-hop mesh digipeating, stable BLE mobile companion connection, and U8g2 OLED page cycling.

---

### 2. Part I: Hardware Enablement Layer (Physical Bring-Up)

#### A. Hardware Quirks & Implemented Solutions:

1. **3.3V Power Rail Gating (VCC / EXT_VCC) — Pin `P0.13`:**
   - Pro Micro nRF52840 modules feature an on-board ME6217 LDO regulator powering external peripherals, gated via **`P0.13`**. A 10 MΩ pulldown keeps the rail disabled by default.
   - In `initVariant()`, `P0.13` is explicitly driven **`HIGH`** (`pinMode(13, OUTPUT); digitalWrite(13, HIGH); delay(100);`), powering the HT-RA62 module and providing I2C pull-up voltage.
2. **Low-Frequency Clock Source (`#define USE_LFRC`):**
   - The board lacks an external 32.768 kHz crystal oscillator (XL1/XL2 pins are unpopulated/NC).
   - The default WisBlock `#define USE_LFXO` causes FreeRTOS/SoftDevice to hang indefinitely. Forcing the internal calibrated RC oscillator via **`#define USE_LFRC`** resolves all boot hangs.
3. **NFC Multiplexed Pins Unlocking (`P0.09` & `P0.10`):**
   - `P0.09` (`LORA_RESET`) and `P0.10` (`LORA_DIO1`) are routed to the on-chip NFC antenna by default. Flashing via UF2 bootloaders leaves the UICR configuration intact.
   - A hardware bootstrap check was implemented in `initVariant()`:
     ```cpp
     if ((NRF_UICR->NFCPINS & 1) == 1) {
         NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
         while (NRF_NVMC->READY == NVMC_READY_READY_Busy);
         NRF_UICR->NFCPINS = 0; // Unmasks P0.09 and P0.10 as standard GPIOs
         while (NRF_NVMC->READY == NVMC_READY_READY_Busy);
         NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
         while (NRF_NVMC->READY == NVMC_READY_READY_Busy);
         NVIC_SystemReset();
     }
     ```
     This allows active-high `DIO1` interrupt signals to reach the NVIC, eliminating CAD timeouts and enabling packet reception.
4. **Heltec HT-RA62 TCXO & RF Switching:**
   - **TCXO (DIO3):** The HT-RA62 integrated TCXO requires **strictly 1.8V**. Configured via:
     ```cpp
     _hwConfig.USE_DIO3_TCXO = true;
     _hwConfig.TCXO_CTRL_VOLTAGE = TCXO_CTRL_1_8V;
     ```
     The default 1.6V RAK configuration resulted in reference frequency drift on Heltec modules.
   - **RF Switch (DIO2 & RXEN):** Internal RF switching is driven automatically by `DIO2` (`USE_DIO2_ANT_SWITCH = true`). In addition, pin `P0.17` (`RADIO_RXEN = 17`, `USE_RXEN_ANT_PWR = true`) must be asserted `HIGH` to power the front-end LNA path during reception.
5. **Dedicated SPI Instance (`SPI_LORA` on `SPIM2`):**
   - Configured dedicated hardware SPI to avoid bus contention:
     ```cpp
     SPIClass SPI_LORA(NRF_SPIM2, 2, 43, 47); // MISO: P0.02, SCK: P1.11, MOSI: P1.15
     ```
6. **I2C Routing & 0.96" SSD1306 OLED Display:**
   - Explicit pin routing prior to `Wire.begin()`:
     ```cpp
     Wire.setPins(PIN_WIRE_SDA, PIN_WIRE_SCL); // SDA: 36 (P1.04), SCL: 11 (P0.11)
     Wire.begin();
     ```
   - Enforced type `1` in `esp32_isSSD1306()` to support write-only panels, passing SCL/SDA directly into the U8g2 constructor:
     ```cpp
     U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2_1(U8G2_R0, U8X8_PIN_NONE, 11, 36);
     ```
7. **Peripherals (GPS & Battery):**
   - Enabled the GPS P-MOSFET gate on `P0.24`.
   - Configured battery voltage monitoring on analog input `P0.31` (AIN7).

---

#### B. Full Pinout Matrix:

| Function               | nRF52840 Port | Arduino Logical Pin | Electrical Description / Hardware Role            |
| :--------------------- | :------------ | :------------------ | :------------------------------------------------ |
| **3.3V Power Gate**    | `P0.13`       | `13`                | ME6217 LDO enable input (OUTPUT, HIGH)            |
| **LoRa SCK**           | `P1.11`       | `43`                | Hardware SPI clock (`NRF_SPIM2`)                  |
| **LoRa MOSI**          | `P1.15`       | `47`                | Hardware SPI MOSI (`NRF_SPIM2`)                   |
| **LoRa MISO**          | `P0.02`       | `2`                 | Hardware SPI MISO (`NRF_SPIM2`)                   |
| **LoRa NSS (CS)**      | `P1.13`       | `45`                | SX1262 Chip Select (Software CS)                  |
| **LoRa RESET**         | `P0.09`       | `9`                 | SX1262 active-low reset (NFC pin)                 |
| **LoRa DIO1 (IRQ)**    | `P0.10`       | `10`                | SX1262 active-high interrupt (NFC pin)            |
| **LoRa BUSY**          | `P0.29`       | `29`                | SX1262 busy status output                         |
| **LoRa RXEN**          | `P0.17`       | `17`                | RF front-end RX enable line                       |
| **GPS Power Gate**     | `P0.24`       | `24`                | MOSFET power switch (HIGH = active)               |
| **GPS UART RX**        | `P0.20`       | `20`                | Hardware `Serial1` (MCU RX <- GPS TX)             |
| **GPS UART TX**        | `P0.22`       | `22`                | Hardware `Serial1` (MCU TX -> GPS RX)             |
| **I2C SDA**            | `P1.04`       | `36`                | Wire data line (OLED & environmental sensors)     |
| **I2C SCL**            | `P0.11`       | `11`                | Wire clock line (OLED & environmental sensors)    |
| **Battery Sense**      | `P0.31`       | `31`                | Channel AIN7, 1.5 MΩ / 1.0 MΩ resistor divider    |
| **User Button (UBTN)** | `P1.00`       | `32`                | OneButton input (cycles display pages, force pos) |
| **Status LED**         | `P0.15`       | `15`                | Nice!Nano on-board blue LED (active LOW)          |

---

### 3. Part II: Upstream Refactoring & Fork Integration (Maintainer Additions)

To merge the FakeTec port cleanly without introducing regressions, the fork maintainer implemented the following refinements:

1. **Preprocessor Isolation (Zero-Regression Policy):**
   - All shared-code changes are strictly enclosed within **`#if defined(BOARD_FAKETEC_V4)`**.
   - Preserved original configurations for other targets: RAK4631, Heltec T114, and LilyGO T-Echo maintain their distinct TCXO voltages and pin configs without global side-effects.
2. **Display & Font Stack Integrity:**
   - Retained standard T-Deck font configurations (`lv_conf`).
   - The neighbor hardware table (`nbrHwNames`) preserves standard `"RAK4631"` entries for remote nodes, while FakeTec identifies locally via BLE DIS as **`"RAK4631 FakeTec"`**.
3. **Protocol & Server Compatibility (HWID):**
   - Preserved on-air hardware identifier as **`HWID = 9`**, ensuring uninterrupted interoperability with central MeshCom servers, HAMNET gateways, and APRS routers.
4. **Polish Regional RF Defaults:**
   - Configured regional frequencies:
     - **MeshCom PL Frequency:** `433.175 MHz`
     - **LoRa APRS PL Frequency:** `434.855 MHz`
5. **New Companion App Integration:**
   - **FakeTec v4** has been added as a supported target in the new companion mobile application, enabling direct target selection and seamless OTA updates.

---

### 4. Part III: Field Verification Results

- **Binary Footprint:** Flash: **84.3%** (706 kB / 815 kB), RAM: **39.5%** (98 kB / 248 kB).
- **Boot & Power:** Flashing via `.uf2` executes a one-time UICR reboot and comes up permanently stable.
- **RF Link Performance:** Reliable 22 dBm transmission; clean two-way communication established with a rooftop gateway (`SP6TSE-1`, 300 m away) with **`RSSI: -63 dBm, SNR: 5 dB`**; zero CAD lockups (`CAD_FREE`).
- **Mesh Routing:** Confirmed automated digipeating (`RELAY_QUEUED`), MHeard list population, and end-to-end multi-hop Direct Messaging (DM received from remote station `SQ5FNF-1`).
- **UI & Connectivity:** 0.96" OLED operates smoothly with page scrolling via UBTN (`P1.00`), and BLE link with the mobile app runs without handshake timeouts.
