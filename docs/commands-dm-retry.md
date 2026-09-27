# DM retry ladder ("Enhanced message transport protection")

- `--dmretry off|3` — sender-side retry mode for user-to-user DMs
  (destination is a callsign, payload carries `{NNN`); groups, broadcasts,
  positions and ACKs are untouched. Bare `--dmretry` prints the current
  state. Invalid values answer `[ERR];dmretry;<text> not one of off|3` and
  change nothing.

| Mode  | Schedule                                                                                         |
| ----- | ------------------------------------------------------------------------------------------------ |
| `off` | ring retry (default): the original send plus up to three retries, 40 s apart                     |
| `3`   | outbox ladder: four sends total at 0/40/80/120 s (attempt 1 = original, attempts 2-4 the ladder) |

- Every retry — on the ring path (`off`) and on the outbox ladder (`3`) alike
  — carries a fresh msg_id: attempt n (2..4) is `first_id ^ ((n-1) << 10)`
  (the official XOR retry format, `src/pn_retry.h`), never the same id twice
  and never a `millis()`-derived id. An echo of the DM does not stop the
  ladder; only a `:ackNNN` for it does.
- Switching from `off` to `3` prints once:
  `[DMRETRY];warning;receiving nodes must run this firmware or newer; older
nodes show every retry as a new message` — a fresh-id retry is a new message
  to any node that doesn't fold attempts on `(source, NNN)`.
- The sender's outbox holds one entry per in-flight DM (5 slots on
  ESP32-S3/nRF52840, 3 on classic ESP32). A DM that would exceed it is
  **refused** with a notice on the originating transport — nothing is
  silently dropped, nothing is queued behind it.
- `--info` adds `DMRETRY mode=<name>`.

## Storage (T13, outside `struct s_meshcom_settings`)

- ESP32: NVS key `dm_retry` (u8: 0/3) in the `Credentials` namespace, own
  `Preferences` handle. Absent or out-of-range value -> `off`; a stored `9`
  from an older build is read back as `3`.
- nRF52: own file `/dm.cfg` (`{magic 'DMC1', mode}`), memcmp-guarded write.
  Absent file or bad magic/value -> `off`; a stored `9` from an older build
  is read back as `3`. Saved only from `commandAction()` (loop task).
