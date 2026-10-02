#!/bin/sh
# Gate for the golden-capture tooling. Every tool here carries its own tests
# against real repository fixtures, not against invented input -- run them all
# before trusting a capture produced with them.
#
#   sh test/golden/selftest.sh
#
# Since W7-II(b) the last step (variant_ini_effective.py) shells out to
# `pio project config`, so this script is no longer toolchain-free and must
# not run while a `pio` build is in progress (one pio process at a time).
set -e
cd "$(dirname "$0")/../.."

python3 test/golden/mc_frame.py
python3 test/golden/normalize.py --self-test
python3 test/golden/corpus_lint.py --self-test
python3 test/golden/extract_commands.py --self-test
python3 test/golden/build_corpus.py --self-test
python3 test/golden/backup_nodes.py --self-test
python3 test/golden/backup_nodes.py --verify-masked
python3 tools/bench/ble_golden.py --self-test
python3 test/golden/command_name_scan.py --self-test
python3 test/golden/compare_udp.py --self-test
python3 test/golden/compare_extudp.py --self-test
python3 test/golden/radio_units_lint.py --self-test
python3 test/golden/radio_units_lint.py
python3 test/golden/nano_printf_lint.py --self-test
python3 test/golden/nano_printf_lint.py
python3 test/golden/twin_stub_lint.py --self-test
python3 test/golden/twin_stub_lint.py
python3 test/golden/carve_extern_lint.py --self-test
python3 test/golden/carve_extern_lint.py
python3 test/golden/command_ladder_lint.py --self-test
python3 test/golden/command_ladder_lint.py
python3 test/golden/help_parity_lint.py --self-test
python3 test/golden/help_parity_lint.py
# W3 bench 2026-09-16: the persist-only schema rows are never exported, so the
# upgrade check that diffs GET /config.json cannot see them -- 12 of the 17 had
# no read-back at all. --persiststat is that read-back; this gate keeps it from
# falling behind the schema, which would reopen the gap invisibly.
# Mutation-verified: drop one printed field and it fails.
python3 test/golden/persist_readback_lint.py --self-test
python3 test/golden/persist_readback_lint.py

# D2-06 turned 70 of the ladder's on/off rungs into COMMAND_TOGGLES[] rows. A
# table has no rung order to lean on, so the properties the ladder used to get
# from its layout have to be asserted: no row shadows another under the D2-10
# exact-token rule, no command is a table row AND a hand-written rung (the row
# would always win and the rung would be dead code), and every mask has a
# register. Mutation-verified: re-add a `debug on` rung and it fails.
python3 test/golden/toggle_table_lint.py --self-test
python3 test/golden/toggle_table_lint.py
# Every internal producer of a command string (web setup handlers, buttons,
# UI code) must hit a parser rung that is compiled in on every board where
# the producer is, and every web GUI element must have its handler compiled
# in wherever the element is. Written for the web "Voltage" switch, which sent
# a bare --volt for two and a half months after the rung became "volt on|off"
# (docs/bugreport-web-volt-toggle-20260923.md). Fails on that tree, passes
# after the fix.
python3 test/golden/producer_match_lint.py --self-test
python3 test/golden/producer_match_lint.py
# WEB-SW: sub_page_info() (the web info page) is supposed to print one
# "Label: value" line per sub_page_setup() switch, so a node's state can be
# read without opening the settings page -- nothing but this lint keeps the
# two pages in step when a switch is added, renamed or dropped.
python3 test/golden/info_switch_lint.py --self-test
python3 test/golden/info_switch_lint.py
# settings_layout_lint.py (the struct-twin diff between src/esp32/esp32_flash.h
# and src/nrf52/WisBlock-API.h) was retired in the D1-04 W3 struct merge: with
# ONE struct (src/meshcom_settings.h) there is no twin left to diff. Its
# replacement is the member-level fail-closed gate,
# test/test_settings_members (native_settings_members_esp32/_nrf52 envs) --
# see docs/BACKLOG.md D1-04 W3 step 3.
# W3 step 1: both settings gates stand BEFORE the migration rewrites this
# code, not after. settings_persist_lint is currently clean -- DR-13's
# failure mode (a field in the X() table with no preferences.put* call, so
# the node silently forgets it) is not present today; the gate exists to
# keep it that way while W3 replaces 266 hand-written NVS calls with a
# schema walk. Mutation-verified: deleting one putInt() makes it fail.
python3 test/golden/settings_persist_lint.py --self-test
python3 test/golden/settings_persist_lint.py
# W3 step 2: the schema gate. settings_schema.{h,cpp} builds the persistence
# descriptor table by expanding config_json.h's CFG_FIELD_LIST plus the
# persist-only list, so the two must not drift from each other or from the
# D1-04 field triage. This gate holds four properties the table cannot assert
# about itself: every PERSIST field is covered or explicitly exempted, no
# RUNTIME field slipped in, no key or member is duplicated, and triage
# disagreement (a) stays at zero. The 7 exemptions (EXCLUDED_FROM_SCHEMA) are
# pinned in-script -- the running node_msgid counter plus the 6 last-sensor-
# reading caches; the D1-04 W3 struct merge closed the 2 Arduino String
# exemptions (node_audio_start/node_audio_msg are now ordinary covered
# members, char[128] with a real schema row) and dropped node_ackid outright
# (removed from the struct).
python3 test/golden/settings_schema_lint.py --self-test
python3 test/golden/settings_schema_lint.py
python3 test/golden/variant_macros_lint.py --self-test
python3 test/golden/variant_macros_lint.py
# W7-II(b): the root platformio.ini plus all 31 variants/*/platformio.ini were
# restructured to hoist keys the variants repeated verbatim (upload_command,
# a handful of redundant monitor_speed/upload_protocol duplicates) into two
# new base sections, [esp32_s3] and [esp32_classic]. This gate checks
# PlatformIO's OWN resolved configuration (`pio project config --json-output`)
# for all 68 envs against the pre-refactor baseline, ignoring only the
# structural `extends` key -- see test/golden/variant_ini_effective.py for why
# a macro dump (variant_macros_effective.py) cannot see this class of change.
python3 test/golden/variant_ini_effective.py --self-test
python3 test/golden/variant_ini_effective.py
python3 test/golden/twin_diff.py --self-test
# The U1/U2 twin-diff artefacts (testplan 4.4/9.1). The pass condition is
# NOT an empty diff -- the platform drift is expected -- it is that the
# committed before-diff still matches what the current dumps produce. A
# block flipping agree<->differ fails here, including a drift row that was
# deliberately FIXED: that has to be acknowledged by regenerating, not
# absorbed silently. Dumps are regenerated by the twin suites themselves
# (pio test -e native_udp_frame_twin / native_udp_send_twin).
python3 test/golden/twin_diff.py --check test/golden/native/u1-esp32.txt test/golden/native/u1-nrf52.txt test/golden/native/u1-twin-diff-before.txt
python3 test/golden/twin_diff.py --check test/golden/native/u2-esp32.txt test/golden/native/u2-nrf52.txt test/golden/native/u2-twin-diff-before.txt
python3 test/golden/drift_matrix_lint.py --self-test
# --phase implementation is TEMPORARY. The M2 review happened on 2026-09-12 and
# filled all 29 verdicts, so the empty-verdict check is now enforced HARD and
# --phase pre-review is obsolete. What this phase still downgrades is the
# asserting_test column: 10 rows are decided but not yet implemented, so the
# test that would fail does not exist yet. Drop the flag -- run this line with
# no --phase at all -- once the unification waves have written them. A phase
# flag left in place after its window has passed is how a requirement quietly
# expires, which is why the script prints the count on every run.
python3 test/golden/drift_matrix_lint.py --phase implementation
# The W3 upgrade proof's instrument. Not a golden gate -- it is pointed at real
# hardware by a human after the nRF52 cutover, comparing a fresh config export
# against the baselines in docs/bench/w3-baseline/ that were captured while the
# nodes still ran a 20260724 image. Its self-test runs here so it cannot rot
# between now and the bench run that depends on it.
python3 tools/bench/w3_upgrade_check.py --self-test
python3 test/golden/verify_captures.py
python3 test/golden/corpus_lint.py test/golden/corpus/
python3 -m unittest discover tools/mock 2>&1 | tail -3
