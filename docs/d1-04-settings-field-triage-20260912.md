# D1-04 field triage: persist vs. runtime-only, all 147 settings fields

Date: 2026-09-12. Branch `dry-unification`, read-only against `src/`. This is step 1 of the
`D1-04` target architecture (`docs/BACKLOG.md`, "`D1-04` target architecture: schema-driven
settings, decided 2026-09-12") -- the prerequisite for extending the `X()` table and for `OPT-07`'s
nRF52 keyed-store sizing. No code changed; this document is the whole deliverable.

## 1. Outcome

| Metric                               |     Count |
| ------------------------------------ | --------: |
| Total fields (`settings-layout.txt`) |       147 |
| **PERSIST**                          |       106 |
| **RUNTIME**                          |        41 |
| **UNCLEAR**                          |         0 |
| In the `X()` table today             |  89 (60%) |
| Persisted in ESP32 NVS today         | 112 (76%) |

**Zero UNCLEAR.** Not expected going in -- the codebase already answers the question twice over:
`esp32_flash.h`'s own `// nicht im Flash` divider line, and `config_json.h:127-137`'s explicit list
of "persisted but deliberately not exported" fields with reasons. Every one of the 147 fields is
decidable from those two sources plus a targeted grep for where it is assigned at runtime.

**The four disagreement counts** (detail in §4):

| #   | Disagreement                                                     | Count | Verdict                     |
| --- | ---------------------------------------------------------------- | ----: | --------------------------- |
| a   | in `X()` table, no ESP32 NVS key, despite having an ESP32 member |     0 | none found                  |
| b   | persisted in ESP32 NVS, absent from the `X()` table              |    25 | all intentional (see below) |
| c   | classified RUNTIME here, yet persisted in ESP32 NVS today        |     8 | real drift, decision owed   |
| d   | single-platform fields that are nonetheless persisted            |    16 | struct-growth decision owed |

**PERSIST-set size for `OPT-07`:** 106 fields, **1 325 bytes** of fixed-width storage across 104 of
them, plus 2 Arduino `String` fields with no compile-time bound (§5).

## 2. Method -- commands used, so this is re-derivable

```bash
# the 147-field list with type/platform verdict (already committed, not re-derived)
cat test/golden/native/settings-layout.txt

# which of the 147 struct members appear in config_json.cpp's X() table (89 unique, after
# de-duplicating the 16 node_mcp17t[i] and 6 node_gcb[i] array rows back to one field each --
# the naive regex undercounts by 2 unless the type token's character class includes digits,
# because CFG_U32 contains "32")
python3 -c '
import re
rows = re.findall(r"X\(\s*\"([^\"]+)\",\s*[A-Z0-9_]+,\s*([A-Za-z0-9_]+)(?:\[[0-9]+\])?,", open("src/config_json.cpp").read())
print(len(set(m for k,m in rows)))'

# which of the 147 struct members have a preferences.put*/get* call in esp32_flash.cpp
grep -n 'meshcom_settings\.<field>\b' src/esp32/esp32_flash.cpp

# where a field NOT found above is actually assigned (the RUNTIME evidence)
grep -rn 'meshcom_settings\.<field>\b' src/ | grep -v esp32_flash.cpp | grep -v nrf52_flash.cpp

# the struct's own persistence-intent divider and documented exclusions
sed -n '1,260p' src/esp32/esp32_flash.h      # "// nicht im Flash" at the line before node_press_alt
sed -n '1,145p'  src/config_json.h           # "Fields deliberately NOT in the file, although
                                              #  they are persisted" (:127-137)

# nRF52's whole-struct persistence (context for why nRF52 evidence is not used for classification)
sed -n '91,404p' src/nrf52/nrf52_flash.cpp
```

Byte sizes use the real array bounds from `settings-layout.txt` (`char[N]` = N,
`char[16][16]` = 256, `int[6]` = 24, `uint8_t[8]` = 8, `int`/`float`/`uint32_t`/`unsigned long` = 4,
`double` = 8, `char`/`bool`/`uint8_t` = 1 -- `unsigned long` is 4 bytes on both `xtensa-esp32-elf-gcc`
and `arm-none-eabi-gcc`, per the operator-decision note already in `BACKLOG.md`). Two `String`
fields (T-Deck only) have no fixed size and are called out separately.

## 3. The full table -- all 147 fields

Columns: **Type** from `settings-layout.txt` (ESP32 spelling where it differs, both are 4 bytes
today per the resolved `node_gpsbaud`/`node_update` mismatches). **X()** = has a row in
`config_json.cpp`'s `CFG_FIELD_LIST`. **ESP32 NVS** = has a `preferences.put*/get*` call in
`esp32_flash.cpp`; `n/a` for the two nRF52-only fields, which have no ESP32 struct member at all.
**Notes** carries the file:line justification for every RUNTIME row and for every PERSIST row whose
persistence is not self-evident (i.e. rows 25 of disagreement (b) below); plain config fields with
an obvious NVS key are left blank.

| Field                     | Type          | Platform   | X() | ESP32 NVS             | Class       | Notes                                                                                                                                                                                                                                     |
| ------------------------- | ------------- | ---------- | --- | --------------------- | ----------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `auto_join`               | bool          | nrf52-only | yes | n/a (no ESP32 member) | **PERSIST** | nRF52 LoRaWAN auto-join toggle; user-configurable (X() table, config_json.cpp:210-211), carried through the compat migration (nrf52_flash.cpp:136) -- real config, just absent on ESP32 (no LoRaWAN OTAA path there)                      |
| `bt_code`                 | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `max_hop_pos`             | int           | both       | no  | no                    | **RUNTIME** | src/esp32/esp32_main.cpp:993 (boot default MAX_HOP_POS_DEFAULT), src/loop_functions.cpp:2349 (overwritten from a received position frame's hop count) -- deliberately not persisted (config_json.cpp:394-395 comment) nor exported        |
| `max_hop_text`            | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_ackid`              | int           | both       | no  | yes                   | **RUNTIME** | src/esp32/esp32_flash.cpp:101,392 -- running ack-id counter, same rationale as node_msgid                                                                                                                                                 |
| `node_age`                | unsigned long | both       | no  | no                    | **RUNTIME** | grep across src/ finds no assignment site at all outside the declaration and the nRF52 compat copy-through (nrf52_flash.cpp:148, which only copies old value forward) -- field is never computed; dead/always-0                           |
| `node_alt`                | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_analog_alpha`       | float         | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_analog_atten`       | float         | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_analog_batt_faktor` | float         | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_analog_faktor`      | float         | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_analog_offset`      | float         | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_analog_pin`         | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_analog_slope`       | float         | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_aprsmc`             | char[10]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_atxt`               | char[40]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_audio_msg`          | String        | esp32-only | no  | yes                   | **PERSIST** | T-Deck-only device UI/behaviour state (config_json.h:133-134), persisted per-device, deliberately excluded from the portable JSON export                                                                                                  |
| `node_audio_start`        | String        | esp32-only | no  | yes                   | **PERSIST** | T-Deck-only device UI/behaviour state (config_json.h:133-134), persisted per-device, deliberately excluded from the portable JSON export                                                                                                  |
| `node_backlightlock`      | bool          | esp32-only | no  | yes                   | **PERSIST** | T-Deck-only device UI/behaviour state (config_json.h:133-134), persisted per-device, deliberately excluded from the portable JSON export                                                                                                  |
| `node_button_pin`         | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_bw`                 | float         | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_call`               | char[10]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_cleanflash`         | int           | both       | no  | yes                   | **PERSIST** | one-shot 'wipe at next boot' trigger (config_json.h:131-132); only works if it survives the very reboot it triggers                                                                                                                       |
| `node_co2`                | float         | both       | no  | yes                   | **RUNTIME** | src/esp32/esp32_flash.cpp:124,419 -- last CO2 sensor reading, same as node_temp                                                                                                                                                           |
| `node_contrast`           | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_country`            | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_cr`                 | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_date_day`           | int           | both       | no  | no                    | **RUNTIME** | src/gps_functions.cpp:1133, src/time_functions.cpp:151, src/phone_commands.cpp:398,420                                                                                                                                                    |
| `node_date_hour`          | int           | both       | no  | no                    | **RUNTIME** | src/gps_functions.cpp:1135, src/time_functions.cpp:153, src/phone_commands.cpp:400,422                                                                                                                                                    |
| `node_date_hundredths`    | int           | both       | no  | no                    | **RUNTIME** | src/esp32/esp32_main.cpp:1815, src/nrf52/nrf52_main.cpp:548,966 -- always reset to 0 at boot                                                                                                                                              |
| `node_date_minute`        | int           | both       | no  | no                    | **RUNTIME** | src/gps_functions.cpp:1136, src/time_functions.cpp:154, src/phone_commands.cpp:401,423                                                                                                                                                    |
| `node_date_month`         | int           | both       | no  | no                    | **RUNTIME** | src/gps_functions.cpp:1132, src/time_functions.cpp:150, src/phone_commands.cpp:397,419                                                                                                                                                    |
| `node_date_second`        | int           | both       | no  | no                    | **RUNTIME** | src/gps_functions.cpp:1137, src/time_functions.cpp:155, src/phone_commands.cpp:402,424                                                                                                                                                    |
| `node_date_year`          | int           | both       | no  | no                    | **RUNTIME** | src/gps_functions.cpp:1131, src/time_functions.cpp:149, src/phone_commands.cpp:396,418 -- assigned from GPS/RTC clock repeatedly, current wall-clock cache                                                                                |
| `node_device_eui`         | uint8_t[8]    | both       | no  | no                    | **RUNTIME** | src/nrf52/nrf52_main.cpp:723-727 -- recomputed from the radio's own MAC/device address every boot (deterministic given the hardware); on ESP32 esp32_flash.h:14 is a hardcoded compile-time constant, never assigned anywhere, no NVS key |
| `node_disp_rot`           | int           | esp32-only | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_dns`                | char[40]      | both       | no  | no                    | **RUNTIME** | src/udp_functions.cpp:963-1001 -- rebuilt from node_owndns or the live DHCP lease on every (re)connect                                                                                                                                    |
| `node_eqns`               | char[50]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_extern`             | char[40]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_fanon`              | bool          | both       | no  | no                    | **RUNTIME** | src/esp32/esp32_main.cpp:3641,3643 -- live fan-relay state, derived from node_ntctemp every poll                                                                                                                                          |
| `node_format`             | char[50]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_freq`               | float         | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_fversion`           | int           | both       | no  | yes                   | **PERSIST** | flash/firmware-layout bookkeeping (config_json.h:128-130); must persist to detect a stale layout across reboot, deliberately excluded from export so a restored backup can't fake it                                                      |
| `node_fwversion`          | char[8]       | both       | no  | yes                   | **PERSIST** | same bookkeeping role as node_fversion (config_json.h:128-130)                                                                                                                                                                            |
| `node_gas_res`            | float         | both       | no  | yes                   | **RUNTIME** | src/esp32/esp32_flash.cpp:121,416 -- last BME680 gas-resistance reading, same as node_temp                                                                                                                                                |
| `node_gcb`                | int[6]        | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_gpsbaud`            | uint32_t      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_gpsdebug`           | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_gw`                 | char[40]      | both       | no  | no                    | **RUNTIME** | src/udp_functions.cpp:960,1000, src/esp32/esp32_eth.cpp:52 -- rebuilt from node_owngw or the live DHCP/Ethernet lease                                                                                                                     |
| `node_gwsrv`              | char[3]       | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_hamnet_only`        | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_hasIPaddress`       | bool          | both       | no  | no                    | **RUNTIME** | src/udp_functions.cpp:141,196 -- live network-link flag, set on every connect/disconnect                                                                                                                                                  |
| `node_hum`                | float         | both       | no  | yes                   | **RUNTIME** | src/esp32/esp32_flash.cpp:83,374 -- last sensor reading, same as node_temp                                                                                                                                                                |
| `node_imax`               | float         | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_immediate_save`     | bool          | esp32-only | no  | yes                   | **PERSIST** | T-Deck-only device UI/behaviour state (config_json.h:133-134), persisted per-device, deliberately excluded from the portable JSON export                                                                                                  |
| `node_ip`                 | char[40]      | both       | no  | no                    | **RUNTIME** | src/udp_functions.cpp:952,959,999 -- rebuilt from node_ownip or WiFi.localIP() on every (re)connect                                                                                                                                       |
| `node_isamp`              | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_kbl_sync`           | bool          | esp32-only | no  | yes                   | **PERSIST** | T-Deck-only device UI/behaviour state (config_json.h:133-134), persisted per-device, deliberately excluded from the portable JSON export                                                                                                  |
| `node_kbllightlock`       | bool          | esp32-only | no  | yes                   | **PERSIST** | T-Deck-only device UI/behaviour state (config_json.h:133-134), persisted per-device, deliberately excluded from the portable JSON export                                                                                                  |
| `node_keyboardlock`       | bool          | esp32-only | no  | yes                   | **PERSIST** | T-Deck-only device UI/behaviour state (config_json.h:133-134), persisted per-device, deliberately excluded from the portable JSON export                                                                                                  |
| `node_last_upd_timer`     | unsigned long | both       | no  | no                    | **RUNTIME** | src/esp32/gateway_service_esp32.cpp:76, src/nrf52/gateway_service_nrf52.cpp:55 -- heartbeat timestamp (millis()-based), meaningless after a reboot resets millis()                                                                        |
| `node_lat`                | double        | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_lat_c`              | char          | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_lon`                | double        | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_lon_c`              | char          | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_lora_call`          | char[10]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_map`                | int           | esp32-only | no  | yes                   | **PERSIST** | T-Deck-only device UI state (config_json.h:133-134), persisted so the device preference survives a reboot; deliberately excluded from the portable JSON export                                                                            |
| `node_maxv`               | float         | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_mcp17in`            | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_mcp17io`            | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_mcp17out`           | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_mcp17t`             | char[16][16]  | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_modus`              | int           | esp32-only | no  | yes                   | **PERSIST** | T-Deck-only device UI/behaviour state (config_json.h:133-134), persisted per-device, deliberately excluded from the portable JSON export                                                                                                  |
| `node_msgid`              | int           | both       | no  | yes                   | **RUNTIME** | src/esp32/esp32_flash.cpp:100,391 -- running message-id counter, persisted only so IDs stay monotone across reboot, never exported (config_json.cpp:214-219)                                                                              |
| `node_mute`               | bool          | esp32-only | no  | yes                   | **PERSIST** | T-Deck-only device UI/behaviour state (config_json.h:133-134), persisted per-device, deliberately excluded from the portable JSON export                                                                                                  |
| `node_mversion`           | int           | both       | no  | yes                   | **PERSIST** | same bookkeeping role as node_fversion (config_json.h:128-130)                                                                                                                                                                            |
| `node_name`               | char[20]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_netmode`            | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_ntctemp`            | float         | both       | no  | no                    | **RUNTIME** | src/esp32/esp32_main.cpp:3638 -- live NTC-thermistor reading, overwritten every poll                                                                                                                                                      |
| `node_ntp`                | char[40]      | esp32-only | no  | no                    | **RUNTIME** | src/udp_functions.cpp:985,987 -- copied from node_ownntp or a hardcoded default ("") on every connect; esp32-only                                                                                                                         |
| `node_opwd`               | char[40]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_ossid`              | char[40]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_owgpio`             | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_owndns`             | char[20]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_owngw`              | char[20]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_ownip`              | char[20]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_ownms`              | char[20]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_ownntp`             | char[40]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_parm`               | char[50]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_parm_1`             | char[100]     | both       | no  | no                    | **RUNTIME** | src/tinyxml_functions.cpp:260 -- telemetry PARM string rebuilt from node_parm/unit/format/eqns/values at each softserial parse                                                                                                            |
| `node_parm_id`            | char[100]     | both       | no  | no                    | **RUNTIME** | src/tinyxml_functions.cpp:268 -- same rebuild as node_parm_1                                                                                                                                                                              |
| `node_parm_t`             | char[150]     | both       | no  | no                    | **RUNTIME** | src/tinyxml_functions.cpp:266 -- same rebuild as node_parm_1                                                                                                                                                                              |
| `node_parm_time`          | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_passwd`             | char[15]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_persist_to_flash`   | bool          | esp32-only | no  | yes                   | **PERSIST** | T-Deck-only device UI/behaviour state (config_json.h:133-134), persisted per-device, deliberately excluded from the portable JSON export                                                                                                  |
| `node_persist_to_sd`      | bool          | esp32-only | no  | yes                   | **PERSIST** | T-Deck-only device UI/behaviour state (config_json.h:133-134), persisted per-device, deliberately excluded from the portable JSON export                                                                                                  |
| `node_pingcall`           | char[10]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_pingcount`          | int           | both       | no  | no                    | **RUNTIME** | src/command_functions.cpp:3768,3775, src/loop_functions.cpp:2188 -- live ping-protocol counter, reset by command                                                                                                                          |
| `node_pingduration`       | unsigned long | both       | no  | no                    | **RUNTIME** | src/loop_functions.cpp:2233,3364 -- millis()-based round-trip timer, meaningless after a reboot resets millis()                                                                                                                           |
| `node_pingmax`            | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_pingtime`           | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_postime`            | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_power`              | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_preamplebits`       | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_press`              | float         | both       | no  | yes                   | **RUNTIME** | src/esp32/esp32_flash.cpp:84,375 -- last sensor reading, same as node_temp                                                                                                                                                                |
| `node_press_alt`          | int           | both       | no  | no                    | **RUNTIME** | src/bme680.cpp:161, src/esp32/esp32_main.cpp:3748,3803 -- pressure-derived altitude, recomputed every sensor poll                                                                                                                         |
| `node_press_asl`          | float         | both       | no  | no                    | **RUNTIME** | src/extudp_functions.cpp:204 -- assigned from an incoming EXTUDP QNH message; received over the air, not configuration                                                                                                                    |
| `node_pwd`                | char[64]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_relay`              | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_sf`                 | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_short`              | char[6]       | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_shunt`              | float         | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_specend`            | float         | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_specsamples`        | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_specstart`          | float         | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_specstep`           | float         | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_ss_baud`            | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_ss_rx_pin`          | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_ss_tx_pin`          | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_sset`               | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_sset2`              | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_sset3`              | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_sset4`              | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_ssid`               | char[33]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_subnet`             | char[40]      | both       | no  | no                    | **RUNTIME** | src/udp_functions.cpp:953,967,1002 -- rebuilt from node_ownms or WiFi.subnetMask()                                                                                                                                                        |
| `node_symcd`              | char          | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_symid`              | char          | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_temp`               | float         | both       | no  | yes                   | **RUNTIME** | src/esp32/esp32_flash.cpp:82,373 -- last sensor reading, overwritten every sensor poll (config_json.cpp:218 "not configuration")                                                                                                          |
| `node_temp2`              | float         | both       | no  | yes                   | **RUNTIME** | src/esp32/esp32_flash.cpp:116,411 -- last sensor reading (2nd probe), same as node_temp                                                                                                                                                   |
| `node_tempi_off`          | float         | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_tempo_off`          | float         | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_track_freq`         | float         | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_unit`               | char[50]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_update`             | char[20]      | both       | no  | no                    | **RUNTIME** | src/nrf52/nrf52_main.cpp:1284, src/esp32/esp32_main.cpp:2866-2887 -- "first GPS fix after boot" timestamp string, written once per boot run from the live clock                                                                           |
| `node_utcoff`             | float         | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_values`             | char[50]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_vbus`               | float         | both       | no  | no                    | **RUNTIME** | src/ina226_functions.cpp:31, src/loop_functions.cpp:2732 -- live INA226 bus-voltage reading                                                                                                                                               |
| `node_vcurrent`           | float         | both       | no  | no                    | **RUNTIME** | src/ina226_functions.cpp:33 -- live INA226 current reading                                                                                                                                                                                |
| `node_via`                | char[40]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_vpower`             | float         | both       | no  | no                    | **RUNTIME** | src/esp32/esp32_main.cpp:3847, src/nrf52/nrf52_main.cpp:2360 -- live INA226 power reading (derived from vbus*vcurrent)                                                                                                                    |
| `node_vshunt`             | float         | both       | no  | no                    | **RUNTIME** | src/esp32/esp32_main.cpp:3845, src/nrf52/nrf52_main.cpp:2358 -- live INA226 shunt-voltage reading                                                                                                                                         |
| `node_webpwd`             | char[20]      | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_wifi_power`         | int           | both       | yes | yes                   | **PERSIST** |                                                                                                                                                                                                                                           |
| `node_wifion`             | bool          | esp32-only | no  | yes                   | **PERSIST** | T-Deck-only device UI/behaviour state (config_json.h:133-134), persisted per-device, deliberately excluded from the portable JSON export                                                                                                  |
| `send_repeat_time`        | uint32_t      | nrf52-only | yes | n/a (no ESP32 member) | **PERSIST** | nRF52 periodic-wake interval (api_functions.cpp:341-379); user-configurable (X() table, config_json.cpp:210), absent on ESP32                                                                                                             |
| `valid_mark_1`            | uint8_t       | both       | no  | no                    | **RUNTIME** | src/nrf52/nrf52_flash.cpp:333 -- nRF52 raw-blob integrity marker, not user data; no NVS key on ESP32 (esp32_flash.h:10, never read/written in esp32_flash.cpp) -- has no role once D1-04 moves to a keyed store                           |
| `valid_mark_2`            | uint8_t       | both       | no  | no                    | **RUNTIME** | src/nrf52/nrf52_flash.cpp:333 -- same marker mechanism as valid_mark_1                                                                                                                                                                    |

Verified: 147 markdown data rows above, 147 distinct field names, every one of the 147 rows of
`settings-layout.txt` matched exactly once (`comm`/`diff` against the source list, no leftovers
either direction).

## 4. The disagreements -- this is the part someone has to decide in W3

### (a) In the `X()` table, but not persisted in ESP32 NVS -- **none**

Every field in the `X()` table that actually has an ESP32 struct member also has an ESP32 NVS key.
The only two `X()` rows that showed up on the naive diff (`auto_join`, `send_repeat_time`) are
nRF52-only fields with **no ESP32 member to persist in the first place** -- not a gap, just the
platform split already visible in `settings-layout.txt`'s verdict column. Nothing for W3 to decide
here.

### (b) Persisted in ESP32 NVS, absent from the `X()` table -- 25 fields

```
node_ackid  node_audio_msg  node_audio_start  node_backlightlock  node_cleanflash  node_co2
node_fversion  node_fwversion  node_gas_res  node_hum  node_immediate_save  node_kbl_sync
node_kbllightlock  node_keyboardlock  node_map  node_modus  node_msgid  node_mute
node_mversion  node_persist_to_flash  node_persist_to_sd  node_press  node_temp
node_temp2  node_wifion
```

All 25 are **already explained in code comments** -- `config_json.h:127-137` and
`config_json.cpp:214-219` name every one of them and say why. They split three ways:

- **13 T-Deck-only device UI/behaviour fields** (`node_map`, `node_audio_start`,
  `node_audio_msg`, `node_keyboardlock`, `node_backlightlock`, `node_kbllightlock`, `node_modus`,
  `node_mute`, `node_persist_to_flash`, `node_persist_to_sd`, `node_immediate_save`,
  `node_kbl_sync`, `node_wifion`) -- per-device preferences, not portable configuration
  (`config_json.h:133-134`). **Decision for W3:** keep excluding these from the export, but decide
  whether they still get an `X()` row (schema-driven persistence needs a table entry to be
  _persisted_ at all under the target architecture, export-or-not is a separate flag).
- **4 flash/firmware bookkeeping fields** (`node_fversion`, `node_mversion`, `node_fwversion`,
  `node_cleanflash`) -- must persist (they gate the stale-layout and wipe-trigger logic across a
  reboot), must not import (`config_json.h:128-132`). Same W3 decision as above: needs a schema
  entry, needs an `export=false` flag.
- **8 fields that are also disagreement (c)** -- see below; these are the ones worth arguing about.

### (c) Classified RUNTIME here, yet persisted in ESP32 NVS today -- 8 fields

```
node_msgid  node_ackid  node_temp  node_hum  node_press  node_temp2  node_gas_res  node_co2
```

Two groups, same shape: **a value that is recomputed shortly after every boot, given a reboot-
surviving NVS slot anyway.**

- `node_msgid` / `node_ackid`: running message-id / ack-id counters. `config_json.cpp:214-217`
  already explains why they must never be _imported_ (would rewind the counter and collide with
  the dedup ring of every neighbour) -- but that comment is about import, not about whether they
  should persist across a normal reboot at all. Persisting them today prevents ID reuse after a
  power cycle; that is arguably correct, which is exactly why this is RUNTIME-shaped data with a
  PERSIST-shaped requirement, not a bug. **Decision for W3:** keep persisting (probably yes, for the
  ID-reuse reason), but they are not "settings" in the schema sense -- decide whether the keyed
  store gets a separate "counters" namespace instead of living in the same table as `node_call`.
- `node_temp` / `node_hum` / `node_press` / `node_temp2` / `node_gas_res` / `node_co2`: last sensor
  readings. `config_json.cpp:218-219` already calls these "not configuration" for export purposes.
  Persisting them buys a display value for the few seconds between boot and the first sensor poll,
  at the cost of a flash write on every sensor cycle if `save_settings()` is ever called from that
  path (it currently is not, `save_settings()` is comand-triggered) so today's actual write
  frequency is low, but the design is still storing transient sensor data as if it were durable
  configuration. **Decision for W3:** drop these 6 from the persist set entirely (cheapest option,
  they cost nothing to leave uninitialised across a reboot) or keep them for the "last known value"
  UX -- either way it belongs in the schema/config split as its own decision, not folded silently
  into "settings."

### (d) Single-platform fields that are nonetheless persisted -- 16 fields (struct-growth decisions)

Of the **15 esp32-only fields**, 14 are persisted and 1 is not:

- **14 persisted esp32-only fields** (`node_disp_rot`, and the 13 T-Deck fields from (b) above) --
  if `D1-04` truly merges into one struct/schema for both platforms, these either (i) get added to
  the shared schema and become inert-but-present on nRF52, or (ii) the schema-driven design keeps a
  platform-conditional field list (config_json.cpp already has exactly this shape via
  `CFG_FIELD_LIST_PLATFORM`, so the mechanism to keep them platform-scoped already exists and
  merging the _struct_ does not force merging the _field set_). Correcting the record here:
  `OPT-D8` (BACKLOG.md) names only 6 of these 14 ("node_ntp, node_immediate_save, node_modus,
  node_mute, node_persist_to_flash, node_disp_rot") as the nRF52 gap -- the other 8 T-Deck fields
  (`node_audio_msg`, `node_audio_start`, `node_backlightlock`, `node_kbl_sync`,
  `node_kbllightlock`, `node_keyboardlock`, `node_map`, `node_persist_to_sd`, `node_wifion`) are
  the same class of gap and are not named there. Whoever picks up `OPT-D8` needs the full list of
  14, not 6.
- **1 not-persisted esp32-only field** (`node_ntp`) -- pure RUNTIME cache (§3), dies with no
  migration cost; already excluded correctly.

Of the **2 nrf52-only fields**, both are persisted and both are real, user-configurable settings
(`auto_join`, `send_repeat_time` -- LoRaWAN auto-join and the periodic send interval, both reachable
via the nRF52 BLE settings characteristic and the `X()` table). **Decision for W3:** these are
LoRaWAN-OTAA concepts that have no ESP32 equivalent path in this fork (ESP32 boards run raw LoRa,
not LoRaWAN join); the likely call is "stays nRF52-only forever," but that is an operator call, not
one this triage can make.

### Additional observation: the struct's own "not persisted" divider is stale

`esp32_flash.h`'s `// nicht im Flash` comment line (before `node_press_alt`) is the single best
piece of _positional_ evidence in the struct for what was originally meant to be transient -- but
it is not reliable on its own, in both directions:

- `max_hop_text` sits **below** the divider yet **is** persisted (`esp32_flash.cpp:181-184,396`).
  The CS-01 comment at `esp32_flash.cpp:175-179` explains why: the NVS key was added 2026-08-30,
  after the struct was laid out, and the field was never moved above the line.
- The entire 13-field T-Deck block also sits **below** the divider yet **is** persisted
  (guarded `#if BOARD_T_DECK...`, `esp32_flash.cpp:255-269,533-547`).

So "position relative to the divider" was never a safe signal by itself -- this triage used the
divider only as a starting hint and confirmed every field against the actual NVS calls (or their
absence) plus a runtime-assignment grep, never against struct position alone. Worth recording
because the same trap is available to whoever writes the `D1-04` schema by hand from the struct
listing instead of from this table.

## 5. PERSIST-set size (for `OPT-07`)

**106 fields.** Of those, **104 have a fixed compile-time size, totalling 1 325 bytes.** The
remaining 2 (`node_audio_start`, `node_audio_msg`, both T-Deck-only) are Arduino `String` objects
with no compile-time bound -- today's usage default is a single-character path (`"/"`), so the
_typical_ footprint is ~2 bytes, but nothing in the code enforces a maximum, so **this is the one
number in this triage that is not a hard measurement**: if `OPT-07`'s keyed store needs a fixed
worst-case per-record size for these two keys, that bound has to be decided (e.g. a `char[64]`
cap matching the SPIFFS/LittleFS path-length practice elsewhere in the tree), not derived.

For reference, the total struct today (all 147 fields, fixed-size ones only) is 2 015 bytes; the
41 RUNTIME fields account for the other 690 bytes, none of which `OPT-07`'s persist-set sizing
needs to include.

Largest PERSIST fields, for anyone budgeting per-record overhead against them:

| Field                                                                                 |   Bytes |
| ------------------------------------------------------------------------------------- | ------: |
| `node_mcp17t`                                                                         |     256 |
| `node_pwd`                                                                            |      64 |
| `node_eqns` / `node_format` / `node_parm` / `node_unit` / `node_values`               | 50 each |
| `node_atxt` / `node_extern` / `node_opwd` / `node_ossid` / `node_ownntp` / `node_via` | 40 each |
| `node_ssid`                                                                           |      33 |
| `node_gcb`                                                                            |      24 |

## 6. What is NOT known

- **The 2 T-Deck `String` fields' worst-case size** (§5) -- needs an operator decision on a cap,
  not more code reading.
- **Whether the 8 disagreement-(c) fields should keep persisting at all** -- this triage surfaces
  the tension (RUNTIME-shaped data in a PERSIST-shaped slot) but does not resolve it; resolving it
  changes the PERSIST-set count and byte total above by up to 8 fields / ~28 bytes
  (`node_msgid`+`node_ackid` = 8 bytes, the 6 sensor floats = 24 bytes).
- **Whether the 14 esp32-only persisted fields join a shared schema or stay platform-scoped** (§4d)
  -- both are structurally possible today (`CFG_FIELD_LIST_PLATFORM` already exists), this triage
  only surfaces that `OPT-D8`'s list of 6 undercounts the real gap of 14.
- **Whether `auto_join`/`send_repeat_time` ever get an ESP32 equivalent** -- almost certainly no
  (no LoRaWAN OTAA path on ESP32 in this fork), but that is an operator call.
- **Live/native test coverage for this table** -- this document was produced by reading, grepping
  and one Python script over committed files; it was not run through `settings_layout_lint.py` or
  any other automated check because the brief for this triage was read-only and doc-only. Whoever
  picks up step 2 (`Extend the X() table to the persist set`) should re-derive the PERSIST/RUNTIME
  split against that tool's output rather than trusting this document blindly, in case the tool
  encodes rules this manual pass missed.

## 7. Sources

- `test/golden/native/settings-layout.txt` -- the 147-field list (not re-derived, per brief)
- `src/config_json.cpp`, `src/config_json.h` -- the `X()` table and its documented exclusions
- `src/esp32/esp32_flash.cpp`, `src/esp32/esp32_flash.h` -- ESP32 NVS calls and the struct layout
- `src/nrf52/nrf52_flash.cpp`, `src/nrf52/WisBlock-API.h` -- nRF52 whole-struct persistence and
  the compat-migration field list (context only, not used for classification -- see BACKLOG.md,
  "nRF52 keeps `s_meshcomcompat_settings`... tells you nothing about intent")
- `docs/BACKLOG.md`, "`D1-04` target architecture" and "`D1-04` settings drift" sections
- File:line references throughout §3-4 are grep results against this tree at the commit noted in
  the git status below, not re-verified against a running build.
