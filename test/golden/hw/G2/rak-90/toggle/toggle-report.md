# toggle soak /dev/cu.usbmodem2101 soak=120.0s 2026-09-18T14:25:32.330

| step | reboot expected | verdict | lines | s | reset reason |
|---|---|---|---|---|---|
| `probe:--info` | no | OK | 33 | 8.2 |  |
| `--setboostedgain on` | yes | EXPECTED-REBOOT | 75 | 123.5 | [BOOT] RESETREAS=0x00000004 |
| `--setboostedgain off` | yes | EXPECTED-REBOOT | 67 | 123.5 | [BOOT] RESETREAS=0x00000004 |
| `--webserver off` | no | OK | 7 | 120.4 |  |
| `--webserver on` | no | OK | 6 | 120.4 |  |
| `--display on` | no | OK | 14 | 120.4 |  |
| `--display off` | no | OK | 6 | 120.3 |  |
| `--gateway on` | yes | OK | 15 | 120.3 |  |
| `--gateway off` | yes | OK | 8 | 120.3 |  |
| `--mesh on` | no | OK | 7 | 120.3 |  |
| `--mesh off` | no | OK | 11 | 120.3 |  |
| `--extudp off` | no | OK | 6 | 120.4 |  |
| `--extudp on` | no | OK | 6 | 120.3 |  |
| `--extudp off` | no | OK | 6 | 120.4 |  |
| `run:curl -s -m 10 'http://192.168.68.66/setparam/?display=on'` | no | OK | 3 | 1.3 |  |
| `wait` | no | OK | 6 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.66/setparam/?display=off'` | no | OK | 3 | 1.0 |  |
| `wait` | no | OK | 5 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.66/setparam/?gateway=on'` | no | OK | 3 | 1.0 |  |
| `wait` | no | OK | 14 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.66/setparam/?gateway=off'` | no | OK | 3 | 1.0 |  |
| `wait` | no | OK | 6 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.66/setparam/?mesh=on'` | no | OK | 3 | 1.2 |  |
| `wait` | no | OK | 5 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.66/setparam/?mesh=off'` | no | OK | 5 | 1.1 |  |
| `wait` | no | OK | 3 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.66/setparam/?extudp=on'` | no | OK | 3 | 1.2 |  |
| `wait` | no | OK | 5 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.66/setparam/?extudp=off'` | no | OK | 5 | 1.1 |  |
| `wait` | no | OK | 3 | 120.0 |  |
| `run:cd /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main && uv run --with bleak tools/bench/ble_cycle.py --address C33126E9-4928-7AD3-BE5F-A0064C5DD11D --pin 424242 --cycles 3 --hold 30 --gap 10` | no | OK | 24 | 132.3 |  |
| `run:cd /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main && uv run --with bleak tools/bench/ble_cycle.py --address C33126E9-4928-7AD3-BE5F-A0064C5DD11D --pin 424242 --cycles 1 --hold 120 --gap 5 --send '--webserver off' --send '--webserver on' --send '--display on' --send '--display off' --send '--gateway on' --send '--gateway off' --send '--mesh on' --send '--mesh off' --send '--extudp on' --send '--extudp off'` | no | OK | 86 | 1211.4 |  |
| `probe:--tempoff in 999999` | no | OK | 3 | 8.5 |  |
| `probe:--tempoff in 0` | no | OK | 4 | 8.4 |  |
| `probe:--info` | no | OK | 33 | 8.2 |  |

## evidence

### --setboostedgain on -> EXPECTED-REBOOT
    [BOOT] RESETREAS=0x00000004
### --setboostedgain off -> EXPECTED-REBOOT
    [BOOT] RESETREAS=0x00000004

=== overall PASS
