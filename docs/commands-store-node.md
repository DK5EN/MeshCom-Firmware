# Store node (mailbox) commands

Available only on boards built with `ENABLE_MSGSTORE` (ESP32-S3, nRF52840 /
`BOARD_RAK4630`). On every other board the four setters below still parse and
answer `[STORE];unavailable` — a web `setparam` never falls into "unknown
command" — and nothing is stored.

- `--store off|own|list|heard` — sets the store mode (default `off`). Bare
  `--store` prints the current state. Switching from `off` to anything else
  also prints the RAM/24-7 warning and the current free heap.
- `--storecall CALL1,CALL2,...` — the `list` mode's store set: up to 16
  callsigns, `A-Z0-9` with an optional `-SSID` (1..99), 9 characters max
  before the SSID. Entries are upper-cased. Bare `--storecall` prints the
  list; `--storecall none` clears it.
- `--storetime <h>` — hold time, 1..168 h (default 24). Out of range answers
  `[ERR]` and changes nothing.
- `--storeslots <n>` — slot count, 1..100 (default 50). Out of range answers
  `[ERR]`. Shrinking while entries are held is allowed; the core caps its use
  at the new value immediately.

`--info` adds one line: `STORE mode=<name> used=<n>/<slots> time=<h>h`.
`--mbox` prints the mailbox summary line followed by one line per held entry
— never the payload text:

```
[MBOX];<slot>;<dst>;<src>;<nnn>;<state>;<cycles>.<attempt>;age;<s>
```

## Output lines

- `[STORE];mode;<name>;slots;<n>;time;<h>` — after any setter, and for the
  bare status commands.
- `[STORE];warning;...` (the RAM/24-7 warning, verbatim) and
  `[STORE];heap;<bytes>` — printed once, on `off` -> any other mode.
- `[STORE];list;<csv>` — after `--storecall`, and for the bare form.
- `[STORE];unavailable` — any of the four commands, on an ineligible board.
- `[ERR];storetime;...` / `[ERR];storeslots;...` / `[ERR];storecall;...` —
  rejected out-of-range or invalid input; nothing is changed or saved.

## Persistence (T13)

Storage settings never live in `struct s_meshcom_settings` — a
`FLASH_STRUCT_VERSION` bump wipes every updating node's callsign and WLAN
credentials. Instead:

- **ESP32 (S3):** own NVS keys in the same `Credentials` namespace the rest
  of the node's settings use — `store_mode` (u8), `store_slots` (u8),
  `store_time` (u16), `store_list` (string). Missing keys fall back to the
  defaults above.
- **nRF52 (RAK4630):** one small LittleFS file, `/msgstore.cfg`, with its own
  magic-tagged struct. A missing file or a bad magic falls back to the
  defaults; the whole settings struct on this port is not touched.

The mailbox itself (held messages) is RAM-only on both platforms — that is
what the "off -> on" warning is telling the operator.
