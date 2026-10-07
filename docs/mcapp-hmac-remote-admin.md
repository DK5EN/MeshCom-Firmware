# McApp: remote node administration over LoRa with HMAC-tagged commands

Date: 2026-10-04. Companion to the firmware concept
`MeshCom-Firmware-DEV-Main/docs/concept-open-issues-20261004.md`, section 6 (RM, upstream issue
#1189). Recon: `docs/concept-open-issues-20261004-recon/scout-mcapp.md`. Repos: backend
`~/WebDev/MCProxy` (FastAPI, package `mcapp`, GitHub DK5EN/McApp), frontend `~/WebDev/webapp`
(Vue 3 + TypeScript). Status: CONCEPT, nothing coded.

## 1. Bottom line

A SysOp sends `RM1 <ctr> <cmd> <tag>` as a personal message from their own BLE-attached node to a
managed node; the managed node verifies the tag with its `node_passwd`, executes an allowlisted
command and answers with a tagged reply. McApp owns the full admin cycle on the client side:

1. **Secrets:** one password per managed node, entered once in Settings, stored encrypted on the
   Pi (`SecretBox`, AES-256-GCM), write-only through the API, never shown again.
2. **Counter:** one monotonic counter per (own call, managed node), persisted in SQLite, bumped on
   every send, resynced with the node's `sync` reply.
3. **Compose and send:** a Node Admin view with a target picker, a command picker (the allowlist),
   arguments, and a send button; the backend computes the tag and sends the DM over the normal
   path (BLE if connected, else Extern-UDP), bypassing the offline outbox.
4. **Replies:** the backend correlates `RM1 <ctr> ok|err ...` DMs from the target, verifies the
   reply tag, marks the history row verified, and pushes it over SSE to the view.
5. **History:** every sent command with counter, time, transport, reply, verification state.

The tag is computed on the backend (decision D8 in the firmware paper): the secret never reaches
the browser, it works over plain `http://mcapp.local` (Caddy keeps HTTP reachable, so
`crypto.subtle` is not guaranteed), and one Python implementation is pinned to the same test
vectors as the firmware (`test/test_remote_cmd/vectors.json`).

## 2. What exists in McApp today (recon)

| Need                     | Existing piece                                                                                                      |
| ------------------------ | ------------------------------------------------------------------------------------------------------------------- |
| HMAC-SHA256              | `src/mcapp/node_console.py:61,428-430` (net console nonce handshake), Python `hmac`/`hashlib`                        |
| Encrypted secret store   | `src/mcapp/secret_box.py` (HKDF from `/var/lib/mcapp/secret.key` + SoC serial); only user `qrz_service.py`           |
| Write-only secret UI     | `webapp/src/components/settings/QrzLookupCard.vue:157-163`, API `src/mcapp/sse_routes/qrz.py`                       |
| Sending a DM             | `POST /api/send` (`sse_routes/stream.py:99-163`) -> `_handle_outbound` (`main.py:2024-2090`); BLE 0xA0 `{dst}msg` or UDP JSON to the node on 1799 |
| Receiving a DM           | `storage/ingest.py` `store_message` -> SSE `msg` (`sse_handler.py:892-940`), dedup (src, msg_id) 60 min             |
| Per-node state tables    | `message_acks`, `callsign_info`, `qrz_state` (single row + `password_enc`) in `storage/migrations.py`, schema v34    |
| Command UI pieces        | `webapp/src/utils/firmwareCommands.ts`, `composables/useCommandSequencer.ts`, `views/BluetoothView.vue`              |
| Settings page structure  | `views/SettingsView.vue` tabs by `?section=`, cards in `components/settings/*Card.vue`                              |
| Navigation               | `router/index.ts` (`/admin` exists, fleet stats), `constants/navigation.ts` `NAV_ITEMS`                              |
| Tests and gates          | backend: `scripts/run_startup_tests.py` suites (no pytest), `uvx ruff check`, `uvx ruff format --check .`, `uv run mypy src/mcapp ble_service/src`; frontend: vitest + happy-dom, `lint`, `format:check`, `typecheck`, `build:strict` |

Send-path hazards that the design must respect:

- `normalize_unified` strips whitespace and a trailing `{digits`; `dst` is upper-cased, `msg` is
  not (`commands/parsing.py:359-376`). The RM1 grammar has no leading or trailing whitespace and a
  hex tag, so nothing is mangled.
- A `msg` starting with `!` is a bot command (`suppression.py:18-20`); `dst == CALL_SIGN` is routed
  to the local handler and never sent (`main.py:2276-2290`). RM1 starts with `R`; the target must
  differ from McApp's own callsign (commanding the attached node itself is the existing local
  command path).
- Length: `msg <= 150 B`, `3 + dst + msg <= 159 B` on UDP, 160 B frame on BLE. An RM1 command is
  about 40-60 characters.
- The offline outbox (`sendQueueStore.ts:405-415`) replays ordinary sends later. Admin sends must be
  excluded like `type === 'command'` is today, or a stale counter goes out after a reconnect.
- The firmware mints `{NNN` and may XOR-retry the DM with a different msg_id; reply correlation keys
  on the `ctr` inside the text, not on msg_id.

## 3. Wire contract (owned by the firmware paper, repeated here)

```
command:  RM1 <ctr> <cmd> [args] <tag16hex>
reply:    RM1 <ctr> ok <status> <tag16hex>   |   RM1 <ctr> err <reason> <tag16hex>
sync:     RM1 0 sync <tag>  ->  RM1 0 ok ctr=<hwm> v=<ver> <tag>
key:      K = SHA-256(node_passwd stripped of trailing spaces)
tag:      HMAC-SHA256(K, "RM1|" + dst + "|" + src + "|" + ctr + "|" + cmd[ + " " + args])[0:8] hex
reply tag HMAC-SHA256(K, "RM1R|" + dst + "|" + src + "|" + ctr + "|" + result)[0:8] hex
```

`dst` is the managed node's full call (for example `DK5EN-90`), `src` is the sending node's call as
it appears in the LoRa frame (McApp's `CALL_SIGN` when sending through the attached node). The node
is silent on a bad tag, a replayed counter or an unknown command. It re-sends the cached reply when
the same valid `(ctr, tag)` arrives again within 10 minutes.

Allowlist v1: `reboot`, `status`, `sendpos`, `sendtrack`, `gps on|off`, `track on|off`,
`display on|off`, `gateway on|off`, `mesh on|off`, `txpower <n>`, `setout <n> <0|1>`, `sync`.

## 4. Design in McApp

### 4.1 Data model (schema v35)

```
node_admin_keys   (target_call TEXT PK, password_enc BLOB, created_at, updated_at)
node_admin_state  (target_call TEXT PK, ctr INTEGER NOT NULL DEFAULT 0, last_sync_at, last_hwm)
node_admin_log    (id INTEGER PK, target_call, ctr, cmd, args, sent_at, transport,
                   msg_id, reply_text, reply_at, verified INTEGER, result TEXT)
```

- `password_enc` is `SecretBox.encrypt(password, associated_data=target_call)`.
- `ctr` is allocated and persisted in one transaction before the send (`UPDATE ... SET ctr = ctr + 1
  RETURNING ctr`); a send failure does not roll it back (gaps are harmless, the node only requires
  "greater than").
- `last_hwm` from the last `sync` reply; if `ctr < last_hwm` the next allocation jumps to
  `last_hwm + 1` (state loss recovery after a DB restore).

### 4.2 Backend service `node_admin_service.py`

| Function                             | Behaviour                                                                                               |
| ------------------------------------ | ------------------------------------------------------------------------------------------------------- |
| `set_password(target, pw)`           | validate (1..14 chars, matches the firmware `--passwd` rule), encrypt, upsert; never returned           |
| `has_password(target)`               | boolean for the UI                                                                                      |
| `build_command(target, cmd, args)`   | allowlist check, allocate `ctr`, compute tag, return the text and a log row id                          |
| `send(target, cmd, args, transport)` | `build_command`, then `publish("sse", "ble_message" or "udp_message")` with `{dst: target, msg: text}`; log the chosen transport |
| `sync(target)`                       | `RM1 0 sync <tag>`; on reply update `last_hwm`                                                          |
| `on_incoming(msg)`                   | hook in `storage/ingest.py`: if `src == target` with an open log row and the text matches `RM1 <ctr> (ok|err) ... <tag>`, verify the reply tag, update the row, emit SSE `node_admin:reply` |
| `history(target, limit)`             | rows for the view                                                                                       |

Tag computation is a pure function `rm_tag(key, dst, src, ctr, cmd_line) -> str` in
`src/mcapp/remote_cmd.py`, tested against the shared vectors file copied from the firmware repo
with a sha256 pin (the webapp convention for shared vectors).

### 4.3 API (`sse_routes/node_admin.py`)

```
PUT    /api/node-admin/keys/{target}          {"password": "..."}      -> 204 (write-only)
DELETE /api/node-admin/keys/{target}                                   -> 204
GET    /api/node-admin/targets                 -> [{target, has_key, ctr, last_hwm, last_sync_at}]
POST   /api/node-admin/send                    {"target","cmd","args","transport":"auto|ble|udp"} -> {log_id, ctr, text}
POST   /api/node-admin/sync/{target}           -> {log_id}
GET    /api/node-admin/history?target=&limit=  -> [log rows]
SSE    node_admin:reply                        {log_id, ctr, result, verified, reply_text}
```

The router is mounted in `sse_handler.py` next to the QRZ router; the service is constructed in
`main.py` like `qrz_service` (`main.py:2734,2823,2908`). `/api/send` is not used for admin
commands, so the `!`-bot, local-command and outbox paths are never involved.

### 4.4 Frontend

- **Settings card** `NodeAdminSecretsCard.vue` (modelled on `QrzLookupCard.vue`): list of targets,
  "add target" with callsign and password, "key set" badge, "replace" and "remove". Write-only.
- **View** `/nodeadmin` (`NodeAdminView.vue`, store `stores/nodeAdmin.ts`, nav item with
  `requiresAdminBackend` so it is hidden on mc-chat backends): target select, command select with
  argument fields driven by a static allowlist table, transport indicator (BLE connected or UDP),
  Send and Sync buttons, live reply panel, history table with verified badge, "resend" disabled
  (the node caches the reply; a resend is a new counter and a new execution).
- **Outbox guard:** nothing to do if `/api/send` is not used; add a test that proves admin sends
  never enter `sendQueueStore`.
- **Shared types:** `types/nodeAdmin.ts`; `useProxyAPI.ts` reused unchanged.

### 4.5 Security notes

- The password is also the node's net-console and KISS password; the Settings card says so.
- The tag reveals nothing about the key; counters and commands are public by law. The McApp log
  stores only what was on the air.
- The browser never sees the password after entry; the API has no read endpoint.
- Target callsigns are free text but the backend refuses `target == CALL_SIGN` and any callsign
  without a key.

## 5. Decisions and defaults

| Id    | Question                                 | Default                                                                                     |
| ----- | ---------------------------------------- | ------------------------------------------------------------------------------------------- |
| MA-D1 | Where the tag is computed                | backend (Pi)                                                                                 |
| MA-D2 | Transport                                | `auto`: BLE when the attached node is connected, else Extern-UDP; shown in the history       |
| MA-D3 | Counter storage                          | SQLite on the Pi, per target; shared across all browsers of this McApp                       |
| MA-D4 | Retry                                    | manual only; a resend is a new counter (the node would re-execute a fresh counter)           |
| MA-D5 | Reply timeout                            | 120 s; after that the row shows "no reply", a later reply still updates it                   |
| MA-D6 | Own-node list                            | free targets with a stored key; no fixed config list                                         |
| MA-D7 | mc-chat parity                           | hidden on non-McApp backends                                                                  |

## 6. Waves (McApp, `/orchestrate-waves`, writers Sonnet)

| Wave | Owner | Files (exclusive)                                                                                                                                                      | Verification                                                    |
| ---- | ----- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------- |
| W1   | A1    | `src/mcapp/remote_cmd.py` (new, pure tag and grammar), `src/mcapp/remote_cmd_tests.py` (new), `tests/vectors/remote_cmd_vectors.json` (copied, sha256 pinned)         | `uv run python -c` suite call; ruff; mypy                       |
| W1   | A2    | `src/mcapp/storage/migrations.py` (v35 block), `src/mcapp/storage/constants.py`, `src/mcapp/storage/node_admin.py` (new mixin), `src/mcapp/sqlite_storage.py`, `src/mcapp/storage/migration_chain_tests.py` | migration chain suite; mypy                                     |
| W2   | B1    | `src/mcapp/node_admin_service.py` (new), `src/mcapp/node_admin_tests.py` (new), `src/mcapp/sse_routes/node_admin.py` (new), `src/mcapp/schemas.py`                      | service suite; ruff; mypy                                       |
| W2   | B2    | `src/mcapp/storage/ingest.py` (reply hook seam only)                                                                                                                   | ingest suite                                                    |
| W2   | orch  | `src/mcapp/main.py` (construct and wire), `src/mcapp/sse_handler.py` (mount), `scripts/run_startup_tests.py` (register both new suites)                                  | `uv run python scripts/run_startup_tests.py` all green          |
| W3   | C1    | `webapp/src/components/settings/NodeAdminSecretsCard.vue` (new), `views/SettingsView.vue`, `components/settings/__tests__/NodeAdminSecretsCard.spec.ts`                 | vitest; lint; typecheck                                         |
| W3   | C2    | `webapp/src/views/NodeAdminView.vue` (new), `stores/nodeAdmin.ts` (new), `types/nodeAdmin.ts` (new), `router/index.ts`, `constants/navigation.ts`, specs                 | vitest; lint; typecheck; `build:strict`                         |
| W3   | C3    | `webapp/src/stores/__tests__/sendQueueStore.outbox.spec.ts` (admin never enters the outbox)                                                                            | vitest                                                          |
| W4   | orch  | end-to-end on the bench: McApp on mcapp.local, attached node DK5EN-14 or DK5EN-1, target DK5EN-90 (RAK), `status`, `display off|on`, `sync`, `reboot`; replay and bad-tag cases from the firmware paper; docs `doc/<date>_node-admin-plan.md`, `doc/architecture-reference.md`, `webapp/docs` | gates of both repos                                             |

Dependency: W4 needs the firmware RM waves 1-2 flashed on DK5EN-90. W1-W3 can run against the
host test vectors alone.

## 7. Open questions

1. Should the Settings card also push the password to the attached node (`--passwd`) so that a
   SysOp sets it once? Default no: the node's password is set on the node, McApp only stores a copy.
2. Reply display in the normal DM conversation as well, or only in the admin view? Default: both;
   the chat bubble is unchanged, the admin view adds verification.
3. Should `sync` run automatically before the first command to a target after a McApp restart?
   Default yes (one extra DM).
