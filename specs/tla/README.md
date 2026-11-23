# TLA+ models

**Five models of task, interrupt and peer interleavings. Each proves properties of a model written by hand from the
C code named in `../manifest.json`.** Rules, commands and limits: [`../README.md`](../README.md).

> [!WARNING]
> **Unrun since the rework of 2026-11-23, and edited without a parser.** Run `./run_tlc.sh --sany-all` first.
> `netconsole.tla` is a full rewrite. Three of its configurations predict defects in today's code.

## Models

| Model            | Subject                                                            | Configs | Notes                                                                 |
| ---------------- | ------------------------------------------------------------------ | ------- | --------------------------------------------------------------------- |
| `bytefifo.tla`   | Byte ring with generation counters, drain reader, history reader   | 11      | Unchanged. `bf_reset`, `bf_detach`, `bf_attach` are not modelled.     |
| `extqueue.tla`   | Two-slot ring between the radio callback and the UDP flush         | 10      | `*_was_*` is the writer before the fix (F2-14).                       |
| `txring.tla`     | TX slot pool: enqueue, claim, finish, sweep, ACK scan              | 14      | Evictor and ACK mutations are now judged by `NoSplice`.               |
| `rxflags.tla`    | ESP32 radio interrupt flags against the loop                       | 8       | New: `Listening`, `TxArmed`. Timeout restart corrected.               |
| `netconsole.tla` | Network console: handshake slot with generations, session takeover | 14      | Rewritten: one process per auth task, late steps of a preempted task. |

## Configuration names

- `<model>.cfg`, `<model>_<platform>.cfg`: the design as it is in `src/` today. Every property must hold.
- `<model>_today_*.cfg`: a defect of today's code, expected red.
- `<model>_was_*.cfg`: a defect of an earlier design, expected red, kept as the record.
- `<model>_mut_*.cfg`: a deliberately broken design, expected red.
- `<model>_reach_*.cfg`: a reachability witness, expected red.
- Other names are sensitivity runs: a defence is removed and the property must still hold or break as stated in the
  file's first comment.

## Predicted findings in the network console (not yet run)

The slot takeover (F2-10-SLOT) stores the rx mark without checking the generation
(`src/net_console.cpp`, `markRx` in `authTask`).

- **`netconsole_today_kickmarked.cfg`:** a preempted task wakes late and overwrites the mark of the new holder
  with its own old generation. The new holder then looks silent and a third connection preempts it.
- **`netconsole_today_deadinstall.cfg`:** if that holder was already verified, it installs its shut-down socket as
  the session and closes the previous good session.
- **`netconsole_today_foreignshut.cfg`:** a holder that closed its socket but has not freed the slot yet is
  preempted. `accept()` has already reused the fd number, so `shutdown()` hits the new connection.
- **`netconsole_markguard.cfg`** is a proposal, not code: mark only while the task is the current holder.
- **Reach:** all three need LAN access to the console port and three connections in a narrow order. None grants a
  session without the password. If TLC does not find them, the runner reports the configuration as vacuous.

## Changes of the rework (2026-11-23)

- **Removed as tautologies:** `rxflags` `NoSilentLoss` configurations, `extqueue_mut_nocount_nosilentdrop`,
  `netconsole_fix_slot` with its scripted scenario, `NoEvictInflight` and `NoExpireInflight` as design properties.
- **`rxflags` timeout restart:** the model guarded the step with "no receive flag". The code has no such check. The
  guard left the model stuck and cut every behaviour behind it.
- **Bounds:** the `rxflags` configurations of today's design offer 3 frames and 2 park slots.
- **Heap models** moved to `archive/heap/`, see the README there.

## Known gaps

- **`txring`:** the retry of parked own frames is not modelled. The ESP32 configuration has one process, so its
  race properties are empty by assumption (single writer, C-01).
- **`extqueue`:** one writer. The header names two contexts for the radio callback on nRF52.
- **`netconsole`:** the check-then-act window inside the accept path is one atomic step. Deadline arithmetic and a
  failing task creation are not modelled.
- **`rxflags`:** no liveness. `blind` counts packets lost in a restart, no property bounds it.
