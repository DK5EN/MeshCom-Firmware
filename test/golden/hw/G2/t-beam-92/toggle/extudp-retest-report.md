# toggle soak /dev/cu.usbserial-573C0005841 soak=120.0s 2026-09-18T13:02:44.677

| step | reboot expected | verdict | lines | s | reset reason |
|---|---|---|---|---|---|
| `--extudp on` | no | OK | 189 | 120.3 |  |
| `--extudp off` | no | OK | 166 | 120.3 |  |
| `run:curl -s -m 10 'http://192.168.68.76/setparam/?extudp=on'` | no | OK | 3 | 0.3 |  |
| `wait` | no | OK | 174 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.76/setparam/?extudp=off'` | no | OK | 3 | 0.2 |  |
| `wait` | no | OK | 164 | 120.0 |  |
| `probe:--tempoff in 999999` | no | OK | 11 | 6.5 |  |
| `probe:--tempoff in 0` | no | OK | 10 | 6.4 |  |
| `probe:--netconsole off` | no | OK | 11 | 6.4 |  |
| `probe:--info` | no | OK | 45 | 6.2 |  |

## evidence


=== overall PASS
