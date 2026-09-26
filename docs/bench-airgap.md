# The `--airgap` bench instrument (plan 0.5)

`--airgap on|off` makes a node convincingly out of range while its console stays alive. Power-down
loses the console exactly when the node's return is interesting; an SSID rename needs a settings
write, a reboot, and a wait for the store node to learn the new callsign via mheard.

- **RX:** `OnRxDone()` drops every frame at the radio boundary, before `is_new_packet()` and before
  any dedup or mheard work — the node behaves as if the frame never arrived, dedup ring and heard
  tables stay clean.
- **TX:** `doTX()` refuses to transmit while the flag is set, same place as the existing
  `TX DISABLED` backstop.
- **RAM-only.** The flag is never persisted (not in `meshcom_settings`), so a reboot always clears
  it. Compiled in only under `-D INSTRUMENT_ENABLED=1`; absent from a normal board build.

## Markers

| Command        | Output                          |
| -------------- | ------------------------------- |
| `--airgap on`  | `[AIRGAP];on`                   |
| `--airgap off` | `[AIRGAP];off`                  |
| `--airgap`     | `[AIRGAP];on` or `[AIRGAP];off` |

Printed to both the USB console and the 2323 net console.

## Acceptance test (T-0.4)

| Step | Action                         | Expect                               |
| ---- | ------------------------------ | ------------------------------------ |
| 1    | `--airgap on`                  | `[AIRGAP];on`                        |
| 2    | Send a frame from another node | No RX print, no mheard/dedup entry   |
| 3    | `--injectmsg` / normal send    | `doTX()` refuses, no on-air TX       |
| 4    | `--airgap off`                 | `[AIRGAP];off`                       |
| 5    | Reboot without `--airgap off`  | Boots with the flag clear (RAM-only) |
