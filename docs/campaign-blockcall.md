# Campaign BLK-01: call blocklist (`--blockcall`)

Fork-only feature. Replaces the blocklist of the third-party build sku2atsc/MeshCom-Firmware
(DM3KS, DB0SD area), so that build has no reason to exist. Upstream since 2026-10-25 inside
[#1238](https://github.com/icssw-org/MeshCom-Firmware/pull/1238) (all fork firmware, merged).

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

| Wave | Content                                                     | State                                                                                          |
| ---- | ----------------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| W1   | core module `src/blocked_calls.{h,cpp}`, 31 host tests      | `ecf00df0`                                                                                     |
| W2   | RX path, console, `--info`, web Setup/Info, RM1 `blockcall` | `1635ba56`; advisor rework (server-frame leaks, HN/NBR feed, NVS policy) folded in             |
| W3   | bench + web GUI in Chrome                                   | done 2026-10-24, see below; `tools/remote_cmd.py` gained `blockcall` (Python allowlist parity) |
| W4   | docs                                                        | done                                                                                           |

## Bench 2026-10-24 (build `1635ba56`)

| Check                                                                | Node(s)                 | Result                                                                                                 |
| -------------------------------------------------------------------- | ----------------------- | ------------------------------------------------------------------------------------------------------ |
| Console: show, full write with normalisation, invalid entry rejected | DK5EN-1, DK5EN-90       | `dk5en-92,DL1ABC-01, dl2xyz-0` stored as `DK5EN-92,DL1ABC-1,DL2XYZ`; `ABC` and `X` rejected, list kept |
| Blocked source on air (TEST group from DK5EN-92)                     | DK5EN-1, DK5EN-90       | direct copy and the copy relayed by DK5EN-98 both dropped, `q=blocked`                                 |
| Bare call blocks all SSIDs (`DK5EN`)                                 | DK5EN-1                 | DK5EN-92 and other DK5EN SSIDs dropped, incl. an HN report                                             |
| Control (`none`)                                                     | DK5EN-1                 | message received, second copy deduplicated as usual                                                    |
| Persistence across reboot                                            | DK5EN-1 (NVS), DK5EN-90 | list kept; RAK dropped a blocked frame 18 s after boot                                                 |
| RM1 read and full write                                              | DK5EN-1 to DK5EN-92     | write stored `DL1ABC-1,DL2XYZ`; read reply `ok b=DL1ABC-1,DL2XYZ`                                      |
| Web Setup card set and clear, Info row, Remote page card             | DK5EN-1 (Chrome)        | set normalised, empty field clears; Info row "none (dropped 26)"                                       |
| Flash only, no test traffic (production gateway, 22 dBm)             | DK5EN-98                | boots, server connected, list empty                                                                    |

Side effect seen on the bench: a node that blocks a neighbour also drops that neighbour's RM replies and ACKs
(they come from the blocked call), and its own frames echoed back by a blocked relay no longer release the
TX slot (normal retries follow). Both are what "no effect" means.

## Open

- DK5EN-14 (T-Deck Plus) not flashed: unreachable on WiFi and not on USB during the session (heard on RF).
- The one-time migration of the fork's `Credentials/block_list` key is host-reviewed only, not benched.
- The web Setup field keeps the typed text after a save; the normalised list shows after a reload.
- An invalid entry in the web field raises the GUI's standard "Value could not be set" alert; not clicked
  through in the browser (an alert blocks the automation), covered by the console check and the setparam code.
