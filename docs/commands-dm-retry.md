# DM retry ladder ("Enhanced message transport protection")

- `--dmretry off|3|9` — sender-side retry mode for user-to-user DMs
  (destination is a callsign, payload carries `{NNN`); groups, broadcasts,
  positions and ACKs are untouched. Bare `--dmretry` prints the current
  state. Invalid values answer `[ERR];dmretry;<text> not one of off|3|9` and
  change nothing.

| Mode  | Schedule                                                                |
| ----- | ----------------------------------------------------------------------- |
| `off` | today's behaviour (default): three same-id retries, 40 s apart          |
| `3`   | one block of three attempts, 40 s apart; attempt 2+ take a fresh msg_id |
| `9`   | three blocks of three attempts, a minute between blocks (nine total)    |

- Switching from `off` to `3` or `9` prints once:
  `[DMRETRY];warning;receiving nodes must run this firmware or newer; older
nodes show every retry as a new message` — a fresh-id retry is a new message
  to any node that doesn't fold attempts on `(source, NNN)`.
- The sender's outbox holds one entry per in-flight DM (5 slots on
  ESP32-S3/nRF52840, 3 on classic ESP32). A DM that would exceed it is
  **refused** with a notice on the originating transport — nothing is
  silently dropped, nothing is queued behind it.
- `--info` adds `DMRETRY mode=<name>`.

## Storage (T13, outside `struct s_meshcom_settings`)

- ESP32: NVS key `dm_retry` (u8: 0/3/9) in the `Credentials` namespace, own
  `Preferences` handle. Absent or out-of-range value -> `off`.
- nRF52: own file `/dm.cfg` (`{magic 'DMC1', mode}`), memcmp-guarded write.
  Absent file or bad magic/value -> `off`. Saved only from `commandAction()`
  (loop task).
