# Frozen UDP frame handlers (FORMAL-02 differential reference)

Byte copies of the two gateway UDP receive handlers at `fork-dev` 3139a4d6
(git blob ids 1c459480.. for ESP32, 6cc11d2f.. for nRF52). The only edit is the
entry-point rename (`handleUdpFrame_esp32` -> `handleUdpFrame_esp32_frozen`,
`handleUdpFrame_nrf52` -> `handleUdpFrame_nrf52_frozen`, including the one
comment mention). Do not edit them; they are the reference the live handlers
are compared against by `native_udp_frame_diff` until the soak of the
consolidation is over (docs/udp-consolidation-20261127.md, step 5).

| File                         | sha256                                                             |
| ---------------------------- | ------------------------------------------------------------------ |
| `udp_frame_esp32.frozen.cpp` | `7808e418fd9ff619dd91c22ec2c61d4036bd2d65b9cfed1e821784510c946eb7` |
| `udp_frame_nrf52.frozen.cpp` | `6f01d2548451a1e69d99c4c03f86f0f4dd4c0eaa52a79209d3b20ab06a02d40b` |

Originals (before the rename):

| File                            | sha256                                                             |
| ------------------------------- | ------------------------------------------------------------------ |
| `src/esp32/udp_frame_esp32.cpp` | `36f58a8b7494e929cbaf12d872a290aaadf73ff77d5fd52d8e08118ca934c338` |
| `src/nrf52/udp_frame_nrf52.cpp` | `80a88aaf2fbcee416f497e4f306a69794a209aff14748a8f09d978bf8ec53a18` |

Check: `shasum -a 256 test/test_udp_frame_diff/frozen/*.cpp`.

The frozen files include live headers (`udp_frame.h`, `dedup_functions.h`, ...),
so a header change that alters their meaning shows up in the differential too.
