# Campaign BLK-01: call blocklist (`--blockcall`)

Fork-only feature. Replaces the blocklist of the third-party build sku2atsc/MeshCom-Firmware
(DM3KS, DB0SD area), so that build has no reason to exist. No upstream PR.

## Decisions (operator, 2026-10-24)

- Command `--blockcall A,B` is a full write; `--blockcall none` clears; no argument shows the list.
- Callsign-SSID validated like `--setcall` (base 3-6, letter and digit, `-01` = `-1`, `-0` = bare).
- A bare call blocks the call with every SSID. A call with SSID blocks only that SSID.
- Frames are dropped when the blocked call is the source or any relay in the path.
- RAM: only the canonical CSV string (160 B). No built-in default callsign.
- Persisted: ESP32 NVS namespace `blocklist` (own namespace, survives `--reset` and settings layout bumps; the fork key `Credentials/block_list` is migrated once and removed), nRF52 `/blocklist.cfg` (`BLK1:` prefix, survives reset too). Same file as the
  fork, so a node migrating from that build keeps its list.
- Shown on the `--info` output and the web Info page; editable on the web Setup page.
- Remote management: RM1 command `blockcall` (read, full write). Limited by `RM_MAX_ARGS` (39 chars,
  about 3-4 calls per write).
- Not in the config backup (`config.json` covers the settings struct only, the list lives outside it).

## Waves

| Wave | Content                                                        | Files                                                                                                     | State   |
| ---- | -------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------- | ------- |
| W1   | core module + host tests                                       | `src/blocked_calls.{h,cpp}`, `test/test_blocked_calls/`, `platformio.ini`                                 | ecf00df0 |
| W2-B | RX path: OnRxDone, server UDP frames, load at boot             | `lora_functions.cpp`, `udp_frame_{esp32,nrf52}.cpp`, `{esp32,nrf52}_main.cpp`, twin tests                 | W2 commit |
| W2-C | console, `--info`, web Setup card, web Info row                | `command_functions.cpp`, `web_functions.cpp`, `web_setup.cpp`, `Web-API_documentation.txt`                | W2 commit |
| W2-D | RM1 `blockcall`                                                | `rm_commands.h`, `remote_cmd.cpp`, `rm_exec_{read,write}.cpp`, `rm_format.h`, `web_rm_page.cpp`, RM tests | W2 commit |
| W3   | bench (DK5EN-1, DK5EN-92, DK5EN-90) + web GUI in Chrome        | none                                                                                                      | open    |
| W4   | docs: BACKLOG, RESUME, defect catalogue, test-suite map        | docs                                                                                                      | open    |

## Resume

Next step after W2: gate (`tools/regression.sh --stage 1,2`, board builds), Opus advisor pass, commit.
