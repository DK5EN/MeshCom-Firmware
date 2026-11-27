# Parked jobs of `nimble_att_write`

These 12 jobs run sequences of two or three ATT prepare-write requests through the extracted
`ble_att_svr.c` code. None of them fits the budget:

- ESBMC 8.7 with Bitwuzla: out of memory at 6 GB within 23-58 s; `pass_prep_cancel` also at 15 GB (93 s).
- With `--z3`: no answer within 900 s, even with pointer and bounds checks off.

The project rule for a job over 15 GB is to make the code under proof verifiable, not to raise the budget. Planned
redesign: function-level jobs (`ble_att_svr_prep_validate`, `ble_att_svr_insert_prep_entry`,
`ble_att_svr_prep_extract`, `ble_att_svr_prep_clear` each on a queue state the harness builds directly) instead of
request sequences. `run_cbmc.sh --all` matches `*/*.check` only, so files in this directory are not run. To run one,
move it back one level (the `.check` preamble and `CFLAGS` use paths relative to the group directory).

Active jobs of the group (one prepare, or the leak mutation): `today_prepend_noleading`, `pass_prepend_leading`,
`mut_code_noprepfree`.
