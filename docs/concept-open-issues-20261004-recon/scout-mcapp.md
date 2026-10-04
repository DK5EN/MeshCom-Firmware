<!-- Recon report by a read-only Sonnet scout, 2026-10-04, fork-dev a9e64cf6. Line references are a snapshot; re-verify before editing. -->

# Scout: McApp, remote node admin (HMAC over DM)

BLUF: McApp = TWO repos. Backend `/Users/martinwerner/WebDev/MCProxy` (GitHub DK5EN/McApp, Python 3.11 FastAPI,
package `mcapp`) + frontend `/Users/martinwerner/WebDev/webapp` (Vue 3 + TS SPA). mc-chat is a different backend
(software gateway on rpizero, not McApp). Mobile app and the backup dir are not McApp.
R = /Users/martinwerner/WebDev/MCProxy ; W = /Users/martinwerner/WebDev/webapp

## Findings

### 1. Repo, stack, deployment, settings storage

1. Identity: R/CLAUDE.md:5 ("McApp is a message proxy ... FastAPI"), `git remote origin` = DK5EN/McApp.
   W/README.md:35-47 (Architecture box names McApp as backend); W/CLAUDE.md:5,22-27 (mcapp.local = 192.168.68.74,
   rpizero.local = .64; "Both have identical setups"). R/src/mcapp/classifier + contract are subtrees from mc-chat
   (R/CLAUDE.md "Vendored Subtrees"); everything else is McApp-only.
2. Backend entry: R/src/mcapp/main.py (3462 lines) -> `MessageRouter` pub/sub hub (main.py:411-412 subscribe
   ble_message/udp_message). REST/SSE routers in R/src/mcapp/sse_routes/*.py (stream, prefs, qrz, monitor, ...).
   BLE is a separate service: R/ble_service/src/{main,ble_adapter}.py (own systemd unit mcapp-ble.service),
   talked to by R/src/mcapp/ble_client_remote.py over HTTP (`/api/ble/send`, ble_client_remote.py:606-612).
3. Deploy on Pi: systemd, NOT docker. R/bootstrap/templates/mcapp.service (ExecStart venv `mcapp`, MCAPP_ENV=prod,
   ReadWritePaths /etc/mcapp /var/lib/mcapp), slot-based OTA (~/mcapp-slots/slot-N, current symlink; update runner
   R/scripts/update-runner.py :2985, doc R/doc/operations-reference.md:329-377). Caddy fronts :80/:443 with internal
   CA (R/bootstrap/templates/caddy/Caddyfile.mcapp:33 `auto_https disable_redirects`), lighttpd :8082 serves SPA,
   backend API :8082 behind it.
4. Config: `/etc/mcapp/config.json` (R/config.sample.json; keys CALL_SIGN, MESHCOM_IOT_TARGET, DB_PATH, BLE_API_KEY),
   runtime overlay runtime.json (R/src/mcapp/config_loader.py:37 `_RUNTIME_OVERLAY_KEYS`), dataclasses config_loader.py:124-219
   (UDPConfig, BLEConfig, NodeConsoleConfig:175 incl. a console `password`). No per-node registry of "own nodes".
5. Persistent state: SQLite `/var/lib/mcapp/messages.db`, schema chain R/src/mcapp/storage/migrations.py,
   `LATEST_SCHEMA_VERSION = 34` (R/src/mcapp/storage/constants.py:23). Pref tables precedent: read_cursors (migrations.py:~623),
   filter_prefs, hidden_destinations, blocked_texts, kickban_callsigns, qrz_state (migrations.py:~760, single-row, `password_enc`).
   Pref API precedent: R/src/mcapp/sse_routes/prefs.py (GET/POST /api/filter_prefs:183-188 etc.), PrefsMixin R/src/mcapp/storage/prefs.py.
6. Frontend prefs: Pinia `userSettings` (W/src/stores/userSettings.ts) persisted in IndexedDB (W/src/services/indexedDB/appStores.ts)
   with selected families mirrored to backend (`_pushFamilySettings`, userSettings.ts:272-279; dirty-flag who-wins policy :25-30).
   localStorage only for theme (userSettings.ts:199). UserAttributes type: W/src/types/userSettings.ts:17-60 (call, MAC, node, extudpip...).
7. Server-side secret store ALREADY EXISTS: R/src/mcapp/secret_box.py (AES-256-GCM, key = HKDF(install key
   /var/lib/mcapp/secret.key 0600, Pi SoC serial), `SecretBox.encrypt/decrypt(plaintext, associated_data)`:129ff). Used only by
   R/src/mcapp/qrz_service.py:40,152,242 (AAD = username). Write-only UI precedent: W/src/components/settings/QrzLookupCard.vue
   (password field cleared after PUT, :157-163; API never returns it, R/src/mcapp/sse_routes/qrz.py:1-45). Threat model
   R/doc/2026-10-04_0848-qrz-callsign-lookup-plan.md section 5.

### 2. How a DM is sent / received

8. UI: W/src/components/chat/ChatInput.vue:140-162 `sendMessage()`: if `ble.connected` -> `enqueueMessage({type:'BLE', dst, msg})`
   else `enqueueMessage({dst, msg})` (default type "msg" = Extern-UDP). Queue: W/src/stores/sendQueueStore.ts:391 `enqueueMessage`,
   POST via W/src/composables/useSSEClient.ts:455-472 `/api/send`.
9. Backend: R/src/mcapp/sse_routes/stream.py:99-163 `POST /api/send` (SendMessageRequest, R/src/mcapp/schemas.py:109).
   type "command" -> `route_command` (stream.py:125-132); "BLE" -> publish ble_message (:138-143); else publish udp_message (:145).
   Handlers main.py:2100 `_udp_message_handler`, :2187 `_ble_message_handler`, shared `_handle_outbound` main.py:2024-2090.
10. Wire format: BLE path = `{dst}msg` in an 0xA0 text frame (R/ble_service/src/ble_adapter.py:1427-1445; frame cap 160 B, schemas.py:93-94).
    UDP path = JSON {type,dst,msg,src} datagram to the node on port 1799 (R/src/mcapp/udp_handler.py:927-965, config_loader.py:113),
    node re-wraps as `:{dst}msg` (schemas.py:100-106: msg <= 150 B, `3+dst+msg <= 159` B, UTF-8 bytes, rejected not truncated, 422).
    The `{NNN` ack/message number is NOT added by McApp; the node mints msg_id and appends `{NNN` on DMs (R/doc/2026-09-27_1807-ed25519-message-signature-scout.md:51-52).
11. Text mangling hazards for an HMAC'd payload (all in the send path):
    - `normalize_unified` (R/src/mcapp/commands/parsing.py:359-376): `msg.strip()` AND `strip_ack_suffix` (trailing `{digits`, util.py:66-80). dst is upper-cased, msg is not.
    - Any msg starting with `!` is treated as a mesh command: `is_command` (R/src/mcapp/suppression.py:18-20); if target is missing/own call it is
      SUPPRESSED and run locally (suppression.py:39-75), so the admin text must NOT start with `!`.
    - dst == own callsign -> routed to local command handler, never sent (main.py:2276-2290).
    - Firmware rewrites `{` to `(` in DM text (dm_text_escape.h per ed25519 scout :36-37) -> keep tag/counter alphabet `[A-Za-z0-9_-]` (base64url/hex).
    - Reserved tail markers: `{NNN`, `:ackNNN`, `:rejNNN`, `:stoNNN`, `{ping}`, `{pong}`, `{SET}`, `{CET}`, `{MCP}` (scout :99-101).
12. Receive: node replies as a DM to our src callsign -> BLE frame or Extern-UDP -> `store_message` (R/src/mcapp/storage/ingest.py) -> SSE `msg`
    event (R/src/mcapp/sse_handler.py:892-940 broadcast) -> W stores/messages.ts, shown as a bubble in the DM conversation with that callsign
    (keyed by conversation_key `A<>B`, R/src/mcapp/storage/prefs.py:32-60). Delivery status: SSE `msg:status` (sse_handler.py:887-890),
    glyph precedence failed > acked > held > sent (W/CLAUDE.md "Store-and-Forward Delivery Status"). Reply must carry dst = our call incl. SSID.
    Inbound dedup (src,msg_id) 60 min: ingest.py:288-326; command throttle 5 min on identical text: commands/dedup.py:66-90, routing.py:131-143
    (matters only for `!` text).
13. Outbox hazard: W sendQueueStore persists ordinary chat sends to an offline outbox and replays them later (sendQueueStore.ts:405-415).
    `type==='command'` is excluded (:405-409 comment). An HMAC command with counter/timestamp MUST be excluded likewise (stale replay = firmware reject or worse).

### 3. Existing crypto / per-node state

14. Crypto in McApp: HMAC-SHA256 only in R/src/mcapp/node_console.py:61,428-430 (NONCE/HMAC handshake to the node's TCP console 2323, password from
    config `node_console.password`); AES-GCM in secret_box.py; sha1/md5 non-security hashes in classifier/ and commands/dedup.py. No admin-secret handling elsewhere.
    Webapp: NO crypto.subtle / HMAC code (grep of W/src: only unrelated hits for sw/push/test vectors). Ed25519 signing was scouted, not built:
    R/doc/2026-09-27_1807-ed25519-message-signature-scout.md (BLUF + budget: sign on proxy, base64url tail, timestamp mandatory, notes WebCrypto needs a secure context).
15. Secure context: Caddy serves HTTPS with an internal CA (W/CLAUDE.md:~20, W/docs/pi-ops.md:7-10) but `disable_redirects` leaves plain http reachable
    (Caddyfile.mcapp:33) and the ed25519 scout :133 calls `http://mcapp.local` "the default". Client-side `crypto.subtle` (HMAC works in secure contexts only)
    is therefore not guaranteed on http. Needs a decision (see Q2).
16. Per-node/pn-retry state: McApp NEVER retransmits a PN (firmware docs/pn-retry-mcapp.md:10-11), so there is no retry counter. Existing per-message/per-node
    bookkeeping a counter could sit next to: `message_acks` (migrations.py:~590, PK msg_id,kind,from_call), `messages.msg_id/send_success/acked/delivery_status`
    (migrations.py:~640-654), `link_uptime_state`/`link_uptime_segments` (migrations.py:~435-443), `callsign_info` (:733), `qrz_state` (single-row state+secret, closest template),
    linkcheck sessions (R/src/mcapp/commands/linkcheck.py, sse_routes/linkcheck.py:64-90, POST /api/linkcheck). `msg_core()` helper R/src/mcapp/util.py (retry-id normalisation).
    Firmware-side retry XOR: msg_id bits 10-11 change per retry, text and `{NNN` unchanged (docs/pn-retry-mcapp.md:10-17), so a reply/ack key should use msg_core, not full msg_id.

### 4. UI structure and test harness

17. Router W/src/router/index.ts: /messages/:dst?, /settings (tabs by ?section=, SettingsView.vue:42-65, sections General/Groups/Filters/Notifications),
    /bluetooth, /mheard, /wx, /monitor, /telemetry (TelemetryConfigView), /update, /admin (index.ts:80-87), /help. Nav list W/src/constants/navigation.ts:28-80
    (NavItem has `requiresAdminBackend`, today meaning mc-chat only; AdminView.vue = fleet stats cards, not node control).
    Settings cards: W/src/components/settings/*Card.vue (QrzLookupCard, DeviceTimeCard, PushNotificationsCard ... SettingsCard.vue wrapper). A "Node admin" fits as
    (a) new SettingsView card for secrets and (b) new view/route `/nodeadmin` (+ NAV_ITEMS entry) for compose/history, or a tab in /bluetooth.
18. Webapp tests: vitest 5 + happy-dom (W/vitest.config.ts:45) with ratcheted coverage floors (:72-80), `npm run test` / `lint` / `format:check` / `typecheck` / `build:strict`
    (W/package.json scripts); component specs in `__tests__/` next to code (e.g. components/settings/**tests**/QrzLookupCard.spec.ts, stores/**tests**/*.spec.ts);
    Playwright only for SW smoke (W/e2e/sw-smoke.spec.ts). Shared vectors/contracts are hand-copied JSON with sha256 pins (W/CLAUDE.md "push_contract.json").
19. Backend tests: NO pytest in McApp. Suites are `run_*_tests()` functions in `src/mcapp/*_tests.py`, registered in `main()` of
    R/scripts/run_startup_tests.py (qrz at :61,107; node_console :59,197; AND-ed at :301-313). A suite not registered there is not gated.
    Gates (R/CLAUDE.md): `uvx ruff check`, `uvx ruff format --check .` (also formats ```python in .md), `uv run mypy src/mcapp ble_service/src` (strict, zero),
    `uv run python scripts/run_startup_tests.py`. Schema change = migrations.py block + LATEST_SCHEMA_VERSION bump same commit; migration_chain_tests asserts it.
    Cross-repo shared corpora go through mc-chat + subtree (R/CLAUDE.md "Vendored Subtrees"); a McApp-only feature needs none.

### 5. Existing "command to node" feature

20. Local node commands: webapp posts `{type:'command', msg:'--xxx'}` to /api/send (stream.py:125-132) -> `route_command` main.py:676-760: `--set*`/`--sym*` ->
    `_handle_device_set_command`, other `--*` -> `_handle_device_a0_command` (main.py:744-751, 1991-1996) -> `client.send_command` -> `ble_adapter.send_command`
    (R/ble_service/src/ble_adapter.py:1461-1480, 253 B cap, ValueError over cap). This is a LOCAL command to the BLE-attached node only (A0 frame), NOT to a remote node,
    and not a DM. Builders: W/src/utils/firmwareCommands.ts; sequencer W/src/composables/useCommandSequencer.ts (sendSequence via sendQueueStore.enqueueCommandAsync:578);
    UIs: W/src/views/BluetoothView.vue (components/bluetooth), TelemetryConfigView. Backend also sends `--pos`, `--reboot`, `--save` itself (ble_client_remote.py:630-638, weather.py:77).
21. In-chat `!command` bot (remote variant is a `!CMD TARGET` DM): R/src/mcapp/commands/handler.py (mixins; admin_commands.py `!group`,`!kb` gated by `_is_admin`),
    palette W/src/components/command-palette/CommandPalette.vue + data/helpContent.ts:45-331 (docs section "Execution Types ... Remote: send command to target node", :22-30).
    These are bot commands on the proxy, not firmware `--commands`.
22. Net console (TCP 2323) with HMAC handshake: node_console.py + sse_routes/monitor.py:72-93 (`/api/monitor/console*`) - sends `--loradebug`, `--txcapture`, `--info`
    via the node's LAN console. Reusable pattern for "secret in config + nonce-HMAC", but it is LAN-only, ESP32-only, single client (node_console.py docstring), not LoRa.

## Open questions for the operator

Q1. Who computes the tag? "Client-side" = browser. Alternative = McApp backend signs (secret never leaves Pi, works over plain http, same code for a Python test vector).
Browser signing needs HTTPS (secure context) and a per-browser secret store (IndexedDB) or fetching the secret from the Pi.
Q2. Is every access to McApp guaranteed over https://mcapp.local? Caddyfile.mcapp:33 keeps http reachable; ed25519 scout assumed http.
Q3. Secret storage: reuse `SecretBox` + new table (write-only API like QRZ) vs IndexedDB in the browser? Per browser means re-entering on every device and counters diverge between devices.
Q4. Counter vs timestamp: counter must be stored server-side per (our callsign, target) to be shared across devices; timestamp needs node RTC (TZ/RTC campaign just landed in firmware, node clock reliability?).
Q5. Exact wire grammar (owned by the firmware paper): must avoid leading `!`, `{`, `:` prefixes, trailing `{digits`, whitespace at ends; tag alphabet. Max budget: 150 B msg - dst.
Q6. Reply format: plain DM text from the node to our call; do we parse (ok/err/counter-resync) or just display?
Q7. Source callsign: replies go to the node's `src` = McApp's own CALL_SIGN (BLE-attached node). Is the sysop's own-node list fixed by config, or free-form dst with a secret per callsign?
Q8. Transport selection: ChatInput uses BLE when `ble.connected`, else UDP. Admin commands: pin one transport or inherit? BLE cap 160 B frame vs UDP 150 B msg.
Q9. Retry policy: McApp never resends a PN; a retry with the same counter is fine only if the firmware tolerates equal counter on identical (cmd,tag); decide resend UX (manual only?).
Q10. mc-chat parity: webapp also talks to mc-chat (rpizero). Node-admin only for McApp (hide on other backends, like `requiresAdminBackend`)?

## Suggested file-ownership list (McApp implementation; one line per task)

T1 backend storage+secrets: R/src/mcapp/storage/migrations.py (new `current_version < 35` block: `node_admin_keys`, `node_admin_log`), R/src/mcapp/storage/constants.py (LATEST_SCHEMA_VERSION), R/src/mcapp/storage/node_admin.py (new mixin) , R/src/mcapp/sqlite_storage.py (mix in), R/src/mcapp/storage/migration_chain_tests.py
T2 backend service+HMAC: R/src/mcapp/node_admin_service.py (new; tag build/verify, counter allocate-and-persist, uses secret_box.py read-only), R/src/mcapp/node_admin_tests.py (new), R/scripts/run_startup_tests.py (register + AND into result, lines ~61/107/301)
T3 backend API: R/src/mcapp/sse_routes/node_admin.py (new router, write-only secret PUT, send, history GET), R/src/mcapp/schemas.py (request models), R/src/mcapp/sse_handler.py (mount router, manager attr), R/src/mcapp/main.py (construct service, wire to manager like qrz_service main.py:2734,2823,2908; reuse `publish("sse","udp_message"/"ble_message")`)
T4 backend send-path guards: R/src/mcapp/commands/parsing.py or util.py only if raw-text preservation is needed (strip/ack-suffix, parsing.py:359-376), R/src/mcapp/suppression.py (no change unless the grammar needs `!`), R/src/mcapp/storage/ingest.py (reply correlation/hook); NOTE main.py appears in T3 and T4 -> serialize
T5 frontend settings: W/src/components/settings/NodeAdminSecretsCard.vue (new, modelled on QrzLookupCard.vue), W/src/views/SettingsView.vue (mount card), W/src/components/settings/**tests**/NodeAdminSecretsCard.spec.ts
T6 frontend admin view: W/src/views/NodeAdminView.vue (new), W/src/stores/nodeAdmin.ts (new), W/src/composables/useProxyAPI.ts (reuse, no change), W/src/router/index.ts, W/src/constants/navigation.ts, W/src/stores/**tests**/nodeAdmin.spec.ts, W/src/views/**tests**/NodeAdminView.spec.ts
T7 frontend send guard: W/src/stores/sendQueueStore.ts (exclude admin sends from the offline outbox, :405-415) + its spec (stores/**tests**/sendQueueStore.outbox.spec.ts)
T8 docs: R/doc/<date>_node-admin-plan.md, R/doc/architecture-reference.md, W/CLAUDE.md or W/docs (one section)
Collisions to flag: R/src/mcapp/main.py (T3, T4), R/scripts/run_startup_tests.py (T2 only if T1 tests also register there -> give T1 test registration to T2), W/src/router/index.ts + navigation.ts + SettingsView.vue are single-owner shared files touched only by T5/T6 respectively, W/src/stores/sendQueueStore.ts only T7.
If browser-side HMAC is chosen (Q1): add W/src/utils/hmac.ts (+ test vectors shared with R/src/mcapp/node_admin_service.py via a JSON vector file copied byte-identically, sha256-pinned in both suites).
Read-only note: nothing was run or modified in any repo; findings from static reads only.
