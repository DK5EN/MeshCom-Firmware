# RM1 remote admin: network read, auto-update control, rate-limit visibility

Handover from McApp to the firmware team. Date: 2026-10-17, DK5EN. Base: `fork-dev` at
`59c9571e`. Firmware references are file:line on that commit.

McApp's Node Admin view ("Remote Management") administers a node over RM1. Operators asked for
three things McApp cannot do today because no RM1 command carries them:

1. **Network card** — MTU, the MeshCom server the node uses (OE, DL, IT, ...), and Internet vs
   HAMNET.
2. **Check for update** — ask the node whether its auto-update has found a newer release.
3. **Do update** — tell the node to install the release it reported, then restart into it.

Plus one optional request about the 10 s rate limit (section 4).

Everything below is a **proposal**. Change names and formats freely — McApp follows whatever lands
in `tools/tests/remote_cmd_vectors.json`. McApp's prepared UI shows these cards greyed out until a
node answers them.

## 1. What exists today (no change needed to the console side)

| Need                 | Setting              | Console                          | Where                                                      |
| -------------------- | -------------------- | -------------------------------- | ---------------------------------------------------------- |
| MTU                  | `node_ethmtu`        | `--mtu` (alias `--ethmtu`)       | `meshcom_settings.h:174`, `command_functions.cpp:3827`     |
| MeshCom server       | `node_gwsrv` char[3] | `--gateway srv <CC>`             | `meshcom_settings.h:151`, `gw_servers.h` table             |
| Internet vs HAMNET   | `node_hamnet_only`   | `--setnet auto\|inet\|hamnet`    | `meshcom_settings.h:92`, `command_functions.cpp:4211`      |
| Network really used  | `gwSelectNet()`      | `[GW];srv` line                  | `gw_servers.h` (`GwTarget.path` = `inet`/`hamnet`)         |
| Auto-update mode     | `node_autoupd`       | `--autoupdate off\|notify\|auto` | `meshcom_settings.h:207`, `command_functions.cpp:5012`     |
| Update channel       | `node_updchan`       | `--updchan prod\|dev`            | `meshcom_settings.h:209`                                   |
| Update state         | `_au.availTag`, ...  | `--update status`                | `command_functions.cpp:5203` (`[AU];status;...;newer;...`) |
| Check / stage / boot | AU (#1187)           | `--update check\|install\|apply` | `command_functions.cpp:5097-5222`                          |

None of these is reachable through RM1 (`rm_commands.h:33-56`).

## 2. Proposed command `net` (READ)

`X(net, READ, NONE)` in `RM_COMMAND_LIST`.

Reply, `ok` + key=value tokens (draft-2 grammar, `-` = absent):

```
ok mtu=1280 srv=OE net=auto eff=hamnet
```

| Key   | Value                    | Meaning                                                                                  |
| ----- | ------------------------ | ---------------------------------------------------------------------------------------- |
| `mtu` | 1280..1500               | `node_ethmtu`                                                                            |
| `srv` | 2-letter code            | `node_gwsrv`, resolved like `gwFind()` (empty/unknown reads as the row it falls back to) |
| `net` | `auto`, `inet`, `hamnet` | Configured mode, the same words `--setnet` accepts (map 0/legacy as the firmware does)   |
| `eff` | `inet`, `hamnet`, or `-` | Network the node really uses right now (`GwTarget.path`); `-` when it has no IP uplink   |

Under 50 chars, well inside `RM_MAX_RESULT` (108).

Out of scope for now: writing any of these remotely. A wrong server or network mode can cut a
gateway off the internet, and RM1 then has no way back.

## 3. Proposed command `upd` (RW: no args = read, args = action)

`X(upd, RW, UPD)` with a new shape `UPD`: empty, `check`, or `go <tag>`. Tag charset suggestion
`[A-Za-z0-9._-]{1,24}`. Two forms only, on purpose — McApp shows one "Check" and one "Update now"
button.

### 3.1 `upd` — read the update state

```
ok m=notify c=prod n=1 a=v4.41a st=- b=0 e=-
```

| Key  | Value                            | Source                                                     |
| ---- | -------------------------------- | ---------------------------------------------------------- |
| `m`  | `off`, `notify`, `auto`          | `node_autoupd`                                             |
| `c`  | `prod`, `dev`                    | `node_updchan`                                             |
| `n`  | `0`, `1`                         | `_au.availNewer` — **the "node wants to update" flag**     |
| `a`  | tag or `-`                       | `_au.availTag`                                             |
| `st` | tag or `-`                       | `_au.stagedTag` when `_au.staged`                          |
| `b`  | `0`, `1`                         | `_auBusy` (check or download running)                      |
| `e`  | short error or `-`               | `_au.lastErr`, truncated so the reply fits 108             |
| `ck` | minutes since last check, or `-` | optional, only if the AU keeps a timestamp (we found none) |

The running version is already in `status` (`v=`), so it is not repeated.

### 3.2 `upd check` — start a check now

Same as `--update check`. The GitHub request takes longer than an RM1 reply should wait, so the
reply only says whether it started:

- `ok checking` — started; McApp reads `upd` again about 30 s later.
- `err busy`, `err no wifi` — not started.

### 3.3 `upd go <tag>` — install the reported release and restart into it

One remote action for "do the update", because McApp can keep only one command in flight per node
and should not have to sequence install → poll → apply over LoRa.

1. **The tag must equal `_au.availTag`.** McApp sends the tag it read from `upd`; this is the guard
   against installing something the operator never saw. Mismatch → `err tag <availTag>`.
2. If not staged yet: start the download/stage (the `install` path). When staging finishes, hand
   over to Safeboot **without** waiting for the update window — the operator just asked for it.
   Whether the battery/idle checks of the auto path still apply is your call; please document it.
3. If already staged with that tag: hand over now (the `apply` path).
4. Reply **before** anything long or the reboot, like `reboot` does (`kRebootDelayMs`,
   `rm_runtime.cpp:39`):
   - `ok installing v4.41a` (download started) or `ok applying v4.41a` (staged, reboot follows)
   - `err not newer`, `err no asset`, `err busy`, `err no wifi`, `err tag v4.41b`

McApp treats `upd go` like `reboot`: confirm dialog, never re-asked, and the button is enabled only
after a verified `upd` read with `n=1`.

## 4. Optional: make the 10 s rate limit visible or harmless

McApp now lets the operator send the next command **as soon as the verified reply arrives**
(operator decision 2026-10-17). The node anchors `RM_RATE_MS` at acceptance (`rmAccept`,
`remote_cmd.cpp:690`), just before the reply goes out. So with `node_rmstrict` on, a command sent
right after a fast reply can land inside the window. The node then returns `RM_REJ_RATE`
silently (`remote_cmd.cpp:658`, no strike, no DM). McApp sees no reply, waits 120 s and adds a
3 min cool-down. The same applies to a second `sync` within `RM_SYNC_RATE_MS` (60 s,
`remote_cmd.cpp:651`).

Strict is off by default (`meshcom_settings.h:205`), and McApp cannot tell which mode a node runs.
Either of these would close the gap — we would be glad of one:

- **(a) Report strict in `status`.** Append a separate token `rs=0|1`. Please do **not** add it as
  a letter to `s=`: McApp's parser rejects the whole status on an unknown `s=` letter, while it
  ignores unknown tokens. McApp would then keep the 10 s spacing only for `rs=1` nodes.
- **(b) Defer instead of drop.** A frame that passes tag and replay checks but hits the rate window
  stays in the 2-slot queue (`rm_queue.h`) until the window opens, then runs. A late execution is
  still answered, so McApp needs no change at all.

## 5. Test corpus and McApp side

- Add `net` and `upd` to `RM_COMMAND_LIST` and regenerate `tools/tests/remote_cmd_vectors.json`
  (`tools/remote_cmd.py --gen-vectors`): command vectors for `net`, `upd`, `upd check`,
  `upd go v4.41a`; reply vectors for the examples above; refusal vectors for a malformed `upd`
  argument and a bad tag charset.
- McApp copies that file to `src/mcapp/remote_cmd_vectors.json` with a sha256 pin. Its test
  requires McApp's command allowlist to equal the corpus command set exactly, so McApp can send
  `net`/`upd` only once the corpus has them. Until then the prepared cards stay greyed out.
- Please tell us the final reply grammar if it differs from sections 2 and 3; McApp's parsers are
  written against this draft.
