# toggle soak /dev/cu.usbserial-0001 soak=60.0s 2026-09-18T13:30:15.872

| step | reboot expected | verdict | lines | s | reset reason |
|---|---|---|---|---|---|
| `probe:--info` | no | OK | 58 | 6.2 |  |
| `probe:--gateway on` | no | OK | 14 | 6.3 |  |
| `run:python3 /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/tools/bench/udp_inject.py 192.168.68.71 1990 /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/test/golden/corpus/udp1990/33-m03-trailing-zeros.hex` | no | OK | 3 | 0.0 |  |
| `wait` | no | OK | 82 | 60.0 |  |
| `run:python3 /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/tools/bench/udp_inject.py 192.168.68.71 1990 /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/test/golden/corpus/udp1990/34-m04-zeros-then-nonzero.hex --repeat 3 --gap 2` | no | OK | 10 | 4.1 |  |
| `wait` | no | OK | 101 | 60.0 |  |
| `probe:--gateway off` | no | OK | 10 | 6.3 |  |
| `probe:--onewire gpio 999999` | no | OK | 13 | 6.5 |  |
| `probe:--onewire gpio 0` | no | OK | 12 | 6.4 |  |
| `probe:--wx` | no | OK | 30 | 6.1 |  |

## evidence


=== overall PASS
