# toggle soak /dev/cu.usbserial-573C0005841 soak=120.0s 2026-09-18T12:53:44.543

| step | reboot expected | verdict | lines | s | reset reason |
|---|---|---|---|---|---|
| `probe:--info` | no | OK | 62 | 8.2 |  |
| `run:curl -s -m 10 'http://192.168.68.76/setparam/?display=on'` | no | OK | 7 | 1.4 |  |
| `wait` | no | OK | 162 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.76/setparam/?display=off'` | no | OK | 3 | 0.3 |  |
| `wait` | no | OK | 164 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.76/setparam/?gateway=on'` | no | OK | 3 | 0.3 |  |
| `wait` | no | OK | 167 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.76/setparam/?gateway=off'` | no | OK | 5 | 0.3 |  |
| `wait` | no | OK | 165 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.76/setparam/?mesh=on'` | no | OK | 3 | 0.4 |  |
| `wait` | no | OK | 165 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.76/setparam/?mesh=off'` | no | OK | 3 | 0.2 |  |
| `wait` | no | OK | 165 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.76/setparam/?extudp=on'` | no | OK | 3 | 0.2 |  |
| `wait` | no | OK | 194 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.76/setparam/?extudp=off'` | no | OK | 5 | 0.2 |  |
| `wait` | no | OK | 147 | 120.0 |  |
| `run:cd /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main && uv run --with bleak tools/bench/ble_cycle.py --address A53B5215-4472-F03A-5A5D-575274D96CC9 --cycles 1 --hold 120 --gap 5 --send '--webserver off' --send '--webserver on' --send '--display on' --send '--display off' --send '--gateway on' --send '--gateway off' --send '--mesh on' --send '--mesh off' --send '--extudp on' --send '--extudp off'` | no | OK | 1646 | 1213.4 |  |
| `probe:--info` | no | OK | 49 | 8.2 |  |

## evidence


=== overall PASS
