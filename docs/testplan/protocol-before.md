# Protocol: before-run (G0/G1)

Template for section 8 of [`testplan-dry-unification-20260910.md`](../testplan-dry-unification-20260910.md).
**Do not edit the row set when filling this in.** Rows are pre-seeded so that the before-run only fills
cells; a row that did not run keeps its cells empty, and an empty cell means the run is
incomplete. It does not mean "not applicable" -- put `n/a` and a reason in **Note** for that.

Filling rules, from section 8:

- **Normalized hash** and **Result** are pasted from tool output, never typed by hand. The
  normalizer is `test/golden/normalize.py`; binary captures go through `mask_binary()`.
- **Compared to** names the artifact this cell was diffed against -- for the before-run this is the other node's artifact,
  the second independent run of the same node, or `--` for the first capture of a cell.
- **Result** is one of `identical`, `expected-diff` (and then **Note** must name the row in
  `EXPECTED-DIFF.md` that predicted it), or `DIFF` for anything else. A "better" diff is still
  `DIFF`.
- Nodes are the four bench nodes: `heltec-93`, `rak-90`, `t-beam-92`, `t-deck-14`. Step IDs are
  the `H1`..`H11` of section 6.

Two row sets are deliberately wider than the node column of section 6, because the G0 run went
wider than the plan and the protocol has to have a cell for what was actually captured:

- **H4 BLE** — section 6 says RAK-90 and Heltec-93; G0 captured all four nodes, and all four are
  reproducible across two independent runs. Four rows.
- **H8 EXTUDP** — section 6 says RAK-90 and Heltec-93; the phase-0 stand tracks it for all four
  (`heltec-93` done, the other three owed). Four rows, three of which will stay empty until those
  captures exist — which is the point of an empty cell.

## Per-step protocol

| Node       | Step | File                          | Lines/bytes | Normalized hash | Compared to | Result | Note |
| ---------- | ---- | ----------------------------- | ----------- | --------------- | ----------- | ------ | ---- |
| heltec-93  | H1   | `boot.txt`                    |             |                 |             |        |      |
| rak-90     | H1   | `boot.txt`                    |             |                 |             |        |      |
| t-beam-92  | H1   | `boot.txt`                    |             |                 |             |        |      |
| t-deck-14  | H1   | `boot.txt`                    |             |                 |             |        |      |
| heltec-93  | H2   | `settings.txt, settings.json` |             |                 |             |        |      |
| rak-90     | H2   | `settings.txt, settings.json` |             |                 |             |        |      |
| t-beam-92  | H2   | `settings.txt, settings.json` |             |                 |             |        |      |
| t-deck-14  | H2   | `settings.txt, settings.json` |             |                 |             |        |      |
| heltec-93  | H3   | `cmd-usb.txt`                 |             |                 |             |        |      |
| rak-90     | H3   | `cmd-usb.txt`                 |             |                 |             |        |      |
| t-beam-92  | H3   | `cmd-usb.txt`                 |             |                 |             |        |      |
| t-deck-14  | H3   | `cmd-usb.txt`                 |             |                 |             |        |      |
| heltec-93  | H3   | `cmd-2323.txt`                |             |                 |             |        |      |
| rak-90     | H3   | `cmd-2323.txt`                |             |                 |             |        |      |
| t-beam-92  | H3   | `cmd-2323.txt`                |             |                 |             |        |      |
| heltec-93  | H3   | `cmd-instr.txt`               |             |                 |             |        |      |
| build only | H3   | `cmd-names-<env>.txt`         |             |                 |             |        |      |
| heltec-93  | H4   | `ble-frames.bin`              |             |                 |             |        |      |
| rak-90     | H4   | `ble-frames.bin`              |             |                 |             |        |      |
| t-beam-92  | H4   | `ble-frames.bin`              |             |                 |             |        |      |
| t-deck-14  | H4   | `ble-frames.bin`              |             |                 |             |        |      |
| heltec-93  | H5   | `rx-log.txt`                  |             |                 |             |        |      |
| rak-90     | H5   | `rx-log.txt`                  |             |                 |             |        |      |
| t-beam-92  | H5   | `rx-log.txt`                  |             |                 |             |        |      |
| t-deck-14  | H5   | `rx-log.txt`                  |             |                 |             |        |      |
| heltec-93  | H6   | `udp-rx-log.txt`              |             |                 |             |        |      |
| rak-90     | H6   | `udp-rx-log.txt`              |             |                 |             |        |      |
| t-beam-92  | H6   | `udp-rx-log.txt`              |             |                 |             |        |      |
| heltec-93  | H7   | `udp-tx.bin`                  |             |                 |             |        |      |
| rak-90     | H7   | `udp-tx.bin`                  |             |                 |             |        |      |
| t-beam-92  | H7   | `udp-tx.bin`                  |             |                 |             |        |      |
| heltec-93  | H8   | `extudp.txt`                  |             |                 |             |        |      |
| rak-90     | H8   | `extudp.txt`                  |             |                 |             |        |      |
| t-beam-92  | H8   | `extudp.txt`                  |             |                 |             |        |      |
| t-deck-14  | H8   | `extudp.txt`                  |             |                 |             |        |      |
| heltec-93  | H9   | `settings-roundtrip.json`     |             |                 |             |        |      |
| rak-90     | H9   | `settings-roundtrip.json`     |             |                 |             |        |      |
| t-beam-92  | H9   | `settings-roundtrip.json`     |             |                 |             |        |      |
| t-deck-14  | H9   | `settings-roundtrip.json`     |             |                 |             |        |      |
| build only | H10  | `regions.csv`                 |             |                 |             |        |      |
| t-deck-14  | H11  | `tdeck-checklist.md`          |             |                 |             |        |      |

## Run summary

| Item | Native suites green | Twin-diff equals fixture | HW goldens equal (steps) | Expected diffs matched | Region gate | Open |
| ---- | ------------------- | ------------------------ | ------------------------ | ---------------------- | ----------- | ---- |
|      |                     |                          |                          |                        |             |      |

Reference points for the summary row, so the cells have something to be measured against:

- **Native suites**: the env list is in `platformio.ini`; the gate is
  `sh test/golden/selftest.sh` plus every `native*` env. The `N0` baseline was taken at tag
  `dry-base-20260911`.
- **Twin-diff**: the six `N1` suites that exist are `U1` `test_udp_frame_twin`, `U2`
  `test_udp_send_twin`, `U3` `test_serial_command_twin`, `U6` `test_country_twin`, `U7`
  `test_aprs_epilogue`, `U8` `test_mheard_render`. `U4` and `U5` are static gates
  (`settings_layout_lint.py`, `command_ladder_lint.py`), not twins, and the summary must not
  count them as twin rows.
- **Region gate**: `tools/resource_watch.py regions`, all 32 envs, from one base build. Never
  run `snapshot` to produce these numbers -- it overwrites the baseline being compared against.
