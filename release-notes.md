> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.
>
> This build is a **stability release**: official MeshCom 4.35p with a collection of robustness, hardening, and reliability improvements layered on top. A node running this build interoperates with official 4.35p on the air and toward the apps. Three fixes in this release do change what a node puts on the air, and every one of them is a correction rather than a feature — they are spelled out under **What changes on the air** below. Every improvement is documented in detail, and all of them are being offered back upstream as individual pull requests. We built this because we love the project and use it every day; field-test reports are very welcome.

> [!TIP]
> **Your settings survive this update.** Until now the reset condition compared `FLASH_VERSION` — a date raised for every release — so **every** update wiped the stored callsign, WiFi credentials and sensor settings of every node, whether or not the settings layout had actually changed. That is fixed here, and it is fixed in the direction that helps: updating from `v4.35p.08.22-stability` to this build keeps your configuration. Verified on three nodes across both MCU families.

## [MeshCom Changelog](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35p.08.27-stability/docs/CHANGELOG-stability.md)

## [MeshCom@ICSSW Projektseite](https://icssw.org/en/meshcom/)

Based on official **MeshCom 4.35p** (upstream `dev`, merge-base `8114d7ae`). Flash version `20260827`. The number that decides whether settings are cleared is now a separate one, `FLASH_STRUCT_VERSION` — it stands at `20260724` and only moves when the settings layout really changes. No reconfiguration needed.

## What changed since v4.35p.08.22-stability

- **Updating no longer erases your configuration.** The reset condition compared `FLASH_VERSION`, a release date, so every update wiped callsign, WiFi credentials and sensor settings — even with the settings layout unchanged. Layout generation is now a separate value and the only trigger. `--flash-reset` still resets.
- **The battery divider polarity is measured, not guessed.** The active-LOW branch hung on `BOARD_HELTEC_V31`, which is defined **nowhere** in the tree — unreachable in every image ever built. It is now probed once at boot, and is a no-op on boards that were already correct.
- **An empty battery is distinguishable from no battery.** `/B=` was only sent above one percent, so a nearly flat pack reported nothing at all. Both `/B=` producers now write the tag whenever battery hardware is present; a missing tag means no battery to report.
- **Text fragments were accepted as ACKs and flooded into the mesh.** `handleACK()` checked only `payload[0] == 0x41` — the ASCII letter `A`. Over 32.7 hours and 8741 field frames, not one of the 506 implausible frames acknowledged anything the node had heard; some carried hop budgets as high as 116.
- **ESP32 channel-utilisation figures were overstated by 1.8× to 4.1×.** `checkRX()` booked the airtime of a 255-byte packet after every receive, because RadioLib never writes the real length back. A reported `util=18%` was really about 7%.
- **New: raw frame capture at runtime (`--txcapture`).** Raw frames go into a 768-byte ring and are printed from the main loop, so the log finally shows what was on the channel instead of what our parser made of it. It found the utilisation bug above on its first run on hardware.
- **`--info` no longer prints passwords in clear text.** That output also goes to TCP port 2323, which requires no authentication unless `node_passwd` is set. A set password now prints as `***`; empty stays empty.
- **`{SET}` range-checks `max_hop`** instead of writing a typo straight into the hop field of every outgoing packet.
- **`printfdeb()` no longer doubles every `%%`** — the source of `util=18%%` and `BATT 100 %%` in the logs, which broke the log-analysis tooling along with them.
- **Tests replay real field traffic through the shipping code.** 48 node-hours of decision traces: 5647 dedup verdicts, 505 priority classifications, 30 honoured ACKs — zero deviations, all mutation-checked. `encodeAPRS()` reproduces the sender-computed byte sum of 2422 distinct real frames without exception.
- **The dedup ring stays at 100**, measured against those traces: of 112 evicted-and-re-flooded ids exactly one was a genuine duplicate, and a ring of 500 would start discarding legitimate messages.

## What changes on the air

Almost everything above is invisible from outside the node. These three are not:

- **`/B=000` is now transmitted** when the battery is measured and empty. A missing `/B=` tag now means the node has no battery hardware to report — previously the two cases were indistinguishable.
- **Implausible frames in the ACK path are no longer relayed.** Against 8741 measured field frames this drops 5.7% of what reached that path, none of which acknowledged anything the node had heard. They were previously re-transmitted into the mesh at priority 1.
- **ESP32 utilisation numbers drop sharply.** Nothing about the radio changed; the number is simply correct now. Expect roughly 7% where the same node used to report 18%.

## Supported Hardware

### Bench-tested for this release

Each item below was verified on real hardware during this release cycle, on the change it demonstrates. There was no single final soak of the finished tree — what stands behind the released tree itself is the full build of every release environment plus the native test suite (6 environments, 220 cases).

- **Heltec V3** — OTA update from `20260821` with settings intact (`FLASH layout 20260821 ok, build 20260827`), and a second OTA in the settled state that also does not clear. ADC_CTRL probe `high=971 low=0 -> active HIGH`, BATT 4.14 V / 90 % with ±1 count of ADC spread, position beacon triggered and sent, no task-watchdog events. Raw frame capture over the network console on port 2323 for five minutes — the run that exposed the utilisation bug.
- **T-Beam v1.2** (ESP32-D0WDQ6, AXP2101, SX1276) — OTA update with callsign and WiFi credentials preserved. AXP2101 battery path unchanged, BATT 4.15 V / 100 %, no task-watchdog events; the PMU failure branch correctly does not engage there.
- **WisBlock RAK4631** (nRF52) — DFU update with callsign and Ethernet configuration preserved, flash version `20260724` afterwards.

### Built and shipped, not on our bench

The remaining boards build cleanly from the same source and inherit every improvement, but we could not put them on our own bench. Please tell us how they behave.

- **T-Beam Supreme** — builds clean and is included, but still unverified on a Supreme. The specific gap from last time is unchanged: it carries an **L76K** GPS and both modules on our bench are u-blox, so the L76K branch of the probe is exercised by no test.
- E22-DevKitC, E22_1262-DevKitC, E22_1262_S3-DevKitC-1-N16R8, E22_1268_S3-DevKitC-1-N16R8, E22_XML-DevKitC
- esp32-loraprs-e22, esp32-loraprs-ra01
- heltec_wifi_lora_32_V2, heltec_wifi_lora_32_V4, heltec_wireless_stick, heltec_wireless_tracker, wireless-paper
- vision-master-e213, vision-master-e290
- ttgo-lora32-v21, ttgo_tbeam_SX1262, ttgo_tbeam_SX1268, T-Beam-1W
- T3_S3_V13, t_connect_pro, t_deck, t_deck_plus, t_deck_pro, T-ETH-ELITE_1262
- heltec_t114, t_echo

(The T5 e-paper variant is not included: it does not build from the current tree for a pre-existing include-path reason unrelated to these changes.)

### Known gaps, stated plainly

- **The battery zero point on a real pack** is proven from the logic and from tests, but no battery on our bench was flat enough to trigger the `/B=000` report in the field.
- **The INA226 branch** of the position encoder is untested — no board on the bench carries one. The change there is a format alignment from `%i` to `%03d`; the decoded value is identical either way.
- **The `--txcapture` transmit side over a real radio** is not systematically verified against a second receiver. The receive path did deliver frames on a Heltec V3, and three transmit frames appeared in the same run.
- **L76K GPS modules** remain untested, as in the previous release.
- **Boot on battery with no USB host** is still not verifiable on this bench — the Heltec V3 does not enable USB-CDC-on-boot and the classic T-Beam has no native USB.

## Installing

- **First install / full flash:** flash bootloader, partitions, otadata, safeboot, and firmware at the addresses listed in the [README](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35p.08.27-stability/README.md#flashing-firmware) (`bootloader.bin` for classic ESP32, `bootloader-s3.bin` for ESP32-S3).
- **Already running MeshCom 4.x with safeboot:** just OTA the `firmware.bin` for your board — via the node's OTA web page, or scripted: `python3 tools/webflash.py <YOUR-CALLSIGN>.local`
- **RAK4631:** copy the `.uf2` onto the bootloader volume (double-tap reset), or `adafruit-nrfutil --verbose dfu serial --package wiscore_rak4631.zip -p <PORT> --singlebank --touch 1200`

## Thank you

To the MeshCom maintainers and the ICSSW team: this project is a gift to the amateur radio community. Every one of these changes is meant to find its way home.
