# toggle soak /dev/cu.usbmodem101 soak=120.0s 2026-09-18T09:42:54.415

| step | reboot expected | verdict | lines | s | reset reason |
|---|---|---|---|---|---|
| `probe:--info` | no | OK | 123 | 8.2 |  |
| `--setboostedgain off` | no | OK | 171 | 120.6 |  |
| `--setboostedgain on` | no | OK | 177 | 120.5 |  |
| `--webserver off` | no | OK | 174 | 120.4 |  |
| `--webserver on` | no | OK | 168 | 120.4 |  |
| `--display off` | no | OK | 168 | 120.4 |  |
| `--display on` | no | OK | 174 | 120.4 |  |
| `--gateway on` | no | OK | 171 | 120.4 |  |
| `--gateway off` | no | OK | 169 | 120.4 |  |
| `--mesh on` | no | OK | 164 | 120.3 |  |
| `--mesh off` | no | OK | 170 | 120.3 |  |
| `--extudp on` | no | OK | 176 | 120.3 |  |
| `--extudp off` | no | OK | 164 | 120.4 |  |
| `run:cd /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main && uv run --with bleak tools/bench/ble_cycle.py --name DK5EN-14 --cycles 3 --hold 30 --gap 10` | no | OK | 207 | 136.1 |  |
| `probe:--info` | no | OK | 52 | 8.2 |  |

## evidence


=== overall PASS
