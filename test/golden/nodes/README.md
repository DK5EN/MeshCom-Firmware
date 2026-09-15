# Bench node backups (test plan P0.3)

One masked copy of each bench node's configuration, taken before any golden
run reflashes it. Captured 2026-09-11 at base `dry-base-20260911`.

| Node        | Board           | Port                     | IP            | Layout     | FW    |
| ----------- | --------------- | ------------------------ | ------------- | ---------- | ----- |
| `rak-90`    | RAK4631 (nRF52) | `/dev/cu.usbmodem2101`   | 192.168.68.68 | `20260724` | 4.35s |
| `heltec-93` | Heltec V3 (S3)  | `/dev/cu.usbserial-0001` | 192.168.68.69 | `20260724` | 4.35s |

## These files are not restorable

The node's `GET /config.json` export carries live credentials in plaintext —
the WiFi password, the AP password, the web password and the BLE pairing code.
The restorable backups therefore live **outside the repository**, in
`~/MeshCom-bench-backups/` (mode 600). What is committed here is a masked copy:
every credential replaced with `<masked>`, everything else untouched, so a
before/after settings diff stays reviewable and the field set is on record.

Do not `POST` these files back to a node. `python3 test/golden/backup_nodes.py
--verify-masked` is part of `selftest.sh` and fails if a live credential ever
appears here.

The export is HTTP only. The test plan called it an `--export` serial command;
there is no such command (`web_functions.cpp:656`, `config_json.cpp`).

## What the two backups already prove

### The nRF52 settings struct is missing six fields, and has two of its own

Live confirmation of `OPT-D8` / audit defect 8, now with the exact names:

- **ESP32 only:** `node_disrot`, `node_spstart`, `node_spend`, `node_spstep`,
  `node_spsamp`, `node_bfakt`
- **nRF52 only:** `send_repeat_time`, `auto_join` -- both removed from the
  struct in W3c (2026-09-15); the rak-90 fixture still carries the two keys,
  which the importer ignores as unknown

The audit named six missing fields from a different angle (`node_ntp`,
`node_immediate_save`, `node_modus`, `node_mute`, `node_persist_to_flash`,
`node_disp_rot`). Those are struct members; this list is what reaches the JSON
export, which is the subset `config_json.cpp` enumerates. Both are true and
they are not the same list — `D1-05` has to reconcile them.

### The same JSON key holds different units on the two platforms

Four fields carry identically-named keys and identically-typed struct members
(`float`/`int` in both headers) but different encodings, written by
`lora_setcountry()` (`lora_setchip.cpp:193-503`, the `D3-05` item):

| key          | ESP32 (heltec-93) | nRF52 (rak-90) | what the split is        |
| ------------ | ----------------- | -------------- | ------------------------ |
| `node_freq`  | `433.175`         | `4.33175e+08`  | MHz vs Hz                |
| `node_track` | `433.775`         | `4.33775e+08`  | MHz vs Hz                |
| `node_bw`    | `250`             | `1`            | kHz vs radio-API index   |
| `node_cr`    | `6`               | `2`            | 4/N denominator vs index |

This is a hard constraint on `D1-05`: one schema table cannot carry a single
unit or range for these four. Either the schema gains a per-platform column, or
the stored values are normalized — and normalizing changes the flash contents,
which means an nRF52 migration (`D1-06`), not just a code change. It also
sharpens drift-matrix row `DR-17` from "to be measured" to a measured
`keep-split` in exactly these four fields.

**Not** drift, despite differing in the diff: `node_power` (2 vs 22) is dBm on
both sides and is simply how each bench node is set; `bt_code`, `node_aslo`,
`node_gcb*` and the `node_sset*` bitmasks are likewise per-node configuration.
