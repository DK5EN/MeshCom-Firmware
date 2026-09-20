# toggle soak /dev/cu.usbserial-0001 soak=120.0s 2026-09-18T10:10:47.364

| step | reboot expected | verdict | lines | s | reset reason |
|---|---|---|---|---|---|
| `probe:--info` | no | OK | 76 | 8.2 |  |
| `run:cd /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main && uv run --with bleak tools/bench/ble_cycle.py --address F4FAC724-A414-5DD6-1B9A-EE2C75A723EB --cycles 3 --hold 30 --gap 10` | no | OK | 457 | 135.6 |  |
| `run:cd /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main && uv run --with bleak tools/bench/ble_cycle.py --address F4FAC724-A414-5DD6-1B9A-EE2C75A723EB --cycles 1 --hold 20 --gap 40 --send '--setboostedgain off'` | yes | EXPECTED-REBOOT | 298 | 70.0 | [BOOT] RESET_REASON=3 SW |
| `run:cd /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main && uv run --with bleak tools/bench/ble_cycle.py --address F4FAC724-A414-5DD6-1B9A-EE2C75A723EB --cycles 1 --hold 20 --gap 40 --send '--setboostedgain on'` | yes | EXPECTED-REBOOT | 300 | 70.4 | [BOOT] RESET_REASON=3 SW |
| `run:cd /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main && uv run --with bleak tools/bench/ble_cycle.py --address F4FAC724-A414-5DD6-1B9A-EE2C75A723EB --cycles 1 --hold 120 --gap 5 --send '--webserver off' --send '--webserver on' --send '--display off' --send '--display on' --send '--gateway on' --send '--gateway off' --send '--mesh on' --send '--mesh off' --send '--extudp on' --send '--extudp off'` | no | OK | 4022 | 1213.1 |  |
| `probe:--info` | no | OK | 69 | 8.2 |  |

## evidence

### run:cd /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main && uv run --with bleak tools/bench/ble_cycle.py --address F4FAC724-A414-5DD6-1B9A-EE2C75A723EB --cycles 1 --hold 20 --gap 40 --send '--setboostedgain off' -> EXPECTED-REBOOT
    CLIENT SETUP
    [BOOT] RESET_REASON=3 SW
### run:cd /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main && uv run --with bleak tools/bench/ble_cycle.py --address F4FAC724-A414-5DD6-1B9A-EE2C75A723EB --cycles 1 --hold 20 --gap 40 --send '--setboostedgain on' -> EXPECTED-REBOOT
    CLIENT SETUP
    [BOOT] RESET_REASON=3 SW

=== overall PASS
