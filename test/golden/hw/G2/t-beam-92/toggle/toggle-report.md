# toggle soak /dev/cu.usbserial-573C0005841 soak=120.0s 2026-09-18T12:16:31.410

| step | reboot expected | verdict | lines | s | reset reason |
|---|---|---|---|---|---|
| `probe:--netconsole on` | no | OK | 26 | 8.4 |  |
| `probe:--info` | no | OK | 43 | 8.2 |  |
| `--setboostedgain on` | yes | OK | 88 | 120.5 |  |
| `--setboostedgain off` | yes | OK | 87 | 120.5 |  |
| `--webserver off` | no | OK | 85 | 120.4 |  |
| `--webserver on` | no | OK | 84 | 120.4 |  |
| `--display on` | no | OK | 84 | 120.3 |  |
| `--display off` | no | OK | 86 | 120.3 |  |
| `--gateway on` | yes | OK | 93 | 120.3 |  |
| `--gateway off` | yes | OK | 89 | 120.3 |  |
| `--mesh on` | no | OK | 86 | 120.2 |  |
| `--mesh off` | no | OK | 85 | 120.3 |  |
| `--extudp on` | no | OK | 87 | 120.3 |  |
| `--extudp off` | no | OK | 115 | 120.3 |  |
| `run:cd /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main && uv run --with bleak tools/bench/ble_cycle.py --address A53B5215-4472-F03A-5A5D-575274D96CC9 --cycles 3 --hold 30 --gap 10` | no | OK | 205 | 133.7 |  |
| `probe:--info` | no | OK | 49 | 8.2 |  |

## evidence


=== overall PASS
