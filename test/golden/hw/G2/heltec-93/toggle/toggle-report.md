# toggle soak /dev/cu.usbserial-0001 soak=120.0s 2026-09-18T09:15:35.905

| step | reboot expected | verdict | lines | s | reset reason |
|---|---|---|---|---|---|
| `probe:--seset` | no | OK | 47 | 8.2 |  |
| `probe:--info` | no | OK | 67 | 8.2 |  |
| `--setboostedgain off` | yes | EXPECTED-REBOOT | 484 | 123.5 | [BOOT] RESET_REASON=3 SW |
| `--setboostedgain on` | yes | EXPECTED-REBOOT | 463 | 123.5 | [BOOT] RESET_REASON=3 SW |
| `--webserver off` | no | OK | 387 | 120.4 |  |
| `--webserver on` | no | OK | 385 | 120.4 |  |
| `--display off` | no | OK | 378 | 120.3 |  |
| `--display on` | no | OK | 386 | 120.3 |  |
| `--gateway on` | yes | OK | 402 | 120.3 |  |
| `--gateway off` | yes | OK | 380 | 120.3 |  |
| `--mesh on` | no | OK | 393 | 120.3 |  |
| `--mesh off` | no | OK | 393 | 120.3 |  |
| `--extudp on` | no | OK | 390 | 120.3 |  |
| `--extudp off` | no | OK | 391 | 120.4 |  |
| `run:cd /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main && uv run --with bleak tools/bench/ble_cycle.py --name DK5EN-1 --cycles 3 --hold 30 --gap 10` | no | OK | 462 | 136.3 |  |
| `probe:--info` | no | OK | 64 | 8.2 |  |

## evidence

### --setboostedgain off -> EXPECTED-REBOOT
    CLIENT SETUP
    [BOOT] RESET_REASON=3 SW
### --setboostedgain on -> EXPECTED-REBOOT
    CLIENT SETUP
    [BOOT] RESET_REASON=3 SW

=== overall PASS
