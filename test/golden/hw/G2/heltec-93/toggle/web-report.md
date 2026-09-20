# toggle soak /dev/cu.usbserial-0001 soak=120.0s 2026-09-18T09:41:59.374

| step | reboot expected | verdict | lines | s | reset reason |
|---|---|---|---|---|---|
| `probe:--info` | no | OK | 78 | 8.2 |  |
| `run:curl -s -m 10 'http://192.168.68.71/setparam/?webserver=off'` | no | OK | 3 | 0.2 |  |
| `wait` | no | OK | 396 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.71/setparam/?webserver=on'` | no | OK | 3 | 0.2 |  |
| `wait` | no | OK | 384 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.71/setparam/?display=off'` | no | OK | 3 | 0.2 |  |
| `wait` | no | OK | 378 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.71/setparam/?display=on'` | no | OK | 3 | 0.2 |  |
| `wait` | no | OK | 388 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.71/setparam/?gateway=on'` | no | OK | 3 | 0.2 |  |
| `wait` | no | OK | 397 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.71/setparam/?gateway=off'` | no | OK | 3 | 0.1 |  |
| `wait` | no | OK | 385 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.71/setparam/?mesh=on'` | no | OK | 3 | 0.2 |  |
| `wait` | no | OK | 377 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.71/setparam/?mesh=off'` | no | OK | 3 | 0.2 |  |
| `wait` | no | OK | 388 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.71/setparam/?extudp=on'` | no | OK | 3 | 0.2 |  |
| `wait` | no | OK | 398 | 120.0 |  |
| `run:curl -s -m 10 'http://192.168.68.71/setparam/?extudp=off'` | no | OK | 5 | 0.2 |  |
| `wait` | no | OK | 384 | 120.0 |  |
| `probe:--info` | no | OK | 67 | 8.2 |  |

## evidence


=== overall PASS
