# toggle soak /dev/cu.usbserial-0001 soak=45.0s 2026-09-18T13:40:23.232

| step | reboot expected | verdict | lines | s | reset reason |
|---|---|---|---|---|---|
| `probe:--gateway on` | no | OK | 30 | 6.4 |  |
| `run:python3 /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/tools/bench/udp_inject.py 192.168.68.71 1990 /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/test/golden/corpus/udp1990/33-m03-trailing-zeros.hex` | no | OK | 7 | 0.1 |  |
| `wait` | no | OK | 68 | 45.0 |  |
| `probe:--gateway off` | no | OK | 11 | 6.4 |  |

## evidence


=== overall PASS
