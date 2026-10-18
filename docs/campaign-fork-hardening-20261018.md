# Campaign 2026-10-18: publication docs, production setting lock, heap, BMx280, LED

Resume point of this multi-wave campaign. Branch `fork-dev`. Operator approvals 2026-10-18: plan, the
locked-settings draft (`docs/production-locked-settings.md`), flash permission for DK5EN-92, DK5EN-1,
DK5EN-90, DK5EN-14 (DK5EN-98 untouched), the heap bisect. BMx280 is field tested (no bench sensor).

## Waves

| Wave | Content                                                                                        | Status  |
| ---- | ---------------------------------------------------------------------------------------------- | ------- |
| W0   | Docs: fork vs upstream, locked settings spec, time zones (+ test), RM1 signature, RM1 cmds     | done    |
| W0h  | T-Beam heap bisect c9e64344..85b0c457, idle-heap method                                        | pending |
| W1   | BMx280 presence helper, LED core helper, server reply core, heap gate tool + mock option       | pending |
| W2   | LED surfaces (console, ESP32 main/sleep/PMU/RM, web)                                           | pending |
| W3   | Server reply integration (UDP frames, LoRa gates, settings); heap levers (stack, rings, marks) | pending |
| W4   | LED nRF52 + variants; production lock incl. `--stor` and `--autoupdate auto`                   | pending |
| W5   | Bench: T-Beam heap, LED, server reply on DK5EN-1 and DK5EN-90; docs; restore                   | pending |

## Decisions carried into the waves

- Auto update: code stays; the `auto` value must be refused on every surface (console, BLE, config
  import, web). Notify stays the only mode. Remote `upd go <tag>` (manual remote install) stays.
- Mailbox announcement (`--stor`): must not be switchable in production builds (W4 lock).
- `{SET}` over LoRa: production accepts text hop 1..6 only and ignores the position hop (spec in
  `docs/production-locked-settings.md`, open points listed there).
- Phone rings freed during the BLE pause: messages for the phone during an update check are dropped
  (accepted).

## W0 result

- New: `docs/fork-dev-vs-upstream-20261018.md`, `docs/production-locked-settings.md`,
  `docs/timezones.md` (42 strings pinned by `test_doc_table` in `test/test_tz_rule`),
  `docs/rm1-hmac.md` (vector verified), `docs/rm1-commands.md` (25 commands).
- Corrected: `docs/adr-remote-hmac.md` (vector counts, allowlist, sync limiter strict-only),
  `docs/rm-gui/unification/inventory-rm1.md` (25 commands).
- Found in W0: `{SET}` over LoRa is not password-gated and accepts 0..7; `--stor on` and
  `--autoupdate auto` are settable today; `--ptime` fallback outside its range; `--postime` dead
  branch; `--spec*` settings overwritten by the web spectrum page. All go to W4.
