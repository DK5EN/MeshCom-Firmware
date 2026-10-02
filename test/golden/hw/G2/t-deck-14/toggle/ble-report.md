# toggle soak /dev/cu.usbmodem101 soak=120.0s 2026-09-18T10:35:38.033

| step | reboot expected | verdict | lines | s | reset reason |
|---|---|---|---|---|---|
| `probe:--info` | no | OK | 127 | 8.2 |  |
| `run:cd /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main && uv run --with bleak tools/bench/ble_cycle.py --address 13F0A5AD-5932-3436-D57B-09A43B5C9C35 --cycles 1 --hold 120 --gap 5 --send '--setboostedgain off' --send '--setboostedgain on' --send '--webserver off' --send '--webserver on' --send '--display off' --send '--display on' --send '--gateway on' --send '--gateway off' --send '--mesh on' --send '--mesh off' --send '--extudp on' --send '--extudp off'` | no | OK | 2049 | 1453.7 |  |
| `probe:--info` | no | OK | 48 | 8.2 |  |

## evidence


=== overall PASS
