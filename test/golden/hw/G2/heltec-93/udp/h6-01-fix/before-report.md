# toggle soak /dev/cu.usbserial-0001 soak=60.0s 2026-09-18T13:25:34.126

| step | reboot expected | verdict | lines | s | reset reason |
|---|---|---|---|---|---|
| `probe:--srvip 192.168.68.58` | no | OK | 29 | 6.5 |  |
| `probe:--gateway on` | no | OK | 10 | 6.3 |  |
| `run:python3 /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/tools/bench/udp_inject.py 192.168.68.71 1990 /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/test/golden/corpus/udp1990/33-m03-trailing-zeros.hex` | no | OK | 3 | 0.0 |  |
| `wait` | no | OK | 120 | 60.0 |  |
| `probe:--gateway off` | no | OK | 10 | 6.3 |  |
| `probe:--info` | no | OK | 46 | 6.2 |  |

## evidence


=== overall PASS
