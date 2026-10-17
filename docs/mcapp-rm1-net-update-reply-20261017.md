# Reply to McApp: RM1 `net`, `upd`, `rs=` (firmware side, 2026-10-17)

Answer to `docs/mcapp-rm1-net-update-handover-20261017.md`. Implemented on `fork-dev` (`5c1d12b5` plus
the `ck=` fix), benched over LoRa DK5EN-98 -> DK5EN-1. Copy `tools/tests/remote_cmd_vectors.json` from
that tree; it now carries `net` and `upd`.

## Final grammar

| Command        | Reply                                                                                                                                                                           |
| -------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `net`          | `ok mtu=<1280..1500> srv=<CC> net=<auto\|inet\|hamnet> eff=<inet\|hamnet\|->`                                                                                                   |
| `upd`          | `ok m=<off\|notify\|auto> c=<prod\|dev> n=<0\|1> a=<tag\|-> st=<tag\|-> b=<0\|1> g=<0\|1> ck=<min\|-> e=<err\|->`                                                               |
| `upd check`    | `ok checking`, `err aubusy`, `err nowifi`, `err unsupported`                                                                                                                    |
| `upd go <tag>` | `ok installing <tag>`, `ok applying <tag>`, `err stale <avail\|->`, `err current`, `err noasset`, `err notready`, `err aubusy`, `err nowifi`, `err safeboot`, `err unsupported` |
| `status`       | unchanged, plus a trailing token `rs=<0\|1>`                                                                                                                                    |

Tag charset `[A-Za-z0-9._-]`, length 1..23 (not 24: the node's buffers are `char[24]`).

## Where it differs from your draft

1. **Error tokens.** `err tag` already means "password mismatch" and `busy` already means "sender
   spacing"; error tokens are one word. Use `stale`, `aubusy`, `nowifi`, `current`, `noasset`, plus
   the new `notready` and `safeboot`.
2. **Enable "Update now" on `n=1` (with `a=<tag>`) OR `st=<tag>`, and only with `b=0` and `g=0`.**
   Staging marks the tag as installed, so a check after staging reports `n=0`. `upd go` accepts the
   staged tag (send `st` when it is set, else `a`).
3. **`g=`** (new): a remote go is armed and waits for its download or for the handover checks. It is
   RAM only; a reboot clears it.
4. **`ck=`** exists (minutes since the last successful check, `-` if none this boot).
5. **`e=`**: spaces and `=` are replaced by `_`, truncated to fit 108.
6. **Boards without auto-update** (RAK4631 / nRF52) answer every `upd` with `err unsupported`.
   `net` works on all boards; `eff=-` means no IP uplink.
7. **`rs=` is a separate token**, not an `s=` letter, as you asked. Option (b), deferring frames in
   the queue, was not taken.

## Operator decisions behind `upd go`

- Allowed regardless of the node's `--autoupdate` mode.
- A go that needs a download is refused with `err notready` when the auto path would not download
  either: no valid clock for TLS, a phone connected, or battery below the floor. `err safeboot`: the
  Safeboot is missing or too old, or a handover for that tag was already attempted this boot.
- The handover keeps every auto-path check except the update window (no phone, battery floor,
  Safeboot capable) and comes no earlier than 8 s after the reply. While it waits, `upd` shows `g=1`.

## Bench evidence (2026-10-17, DK5EN-98 sender, DK5EN-1 target, both verified replies)

```
status          ok v=4.40a up=1 bat=100 heap=112 s=GtdMWl p=2/0/22 rs=0   (rs=1 with --rmstrictsecurity on)
net             ok mtu=1280 srv=OE net=auto eff=inet
upd             ok m=off c=prod n=0 a=- st=- b=0 g=0 ck=- e=-
upd check       ok checking
upd go v0.0.0   err stale v4.40a.hbserver
upd go v4.40a.hbserver   err current
upd (staged)    ok m=off c=dev n=0 a=- st=v4.40a.10.17 b=0 g=0 ck=- e=-
upd go v4.40a.10.17      ok applying v4.40a.10.17   -> node offline 12 s later, back on the release
upd             ok m=off c=prod n=0 a=v4.40a.hbserver st=- b=0 g=0 ck=0 e=-
```

The staged image for the `applying` run came from the bench-only `--update stagelan` (the node had
installed v4.40a.10.17 before, so its reinstall guard kept `n=0`). The `installing` path, a download
started by `upd go`, runs the same download job as `--update install` and is covered by host tests,
not by this bench.
