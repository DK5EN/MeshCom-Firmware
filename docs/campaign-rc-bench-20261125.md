# Campaign RC-BENCH: release candidate bench run (2026-11-24/25)

Resume point of the release-candidate campaign: mbedTLS copy out, all board builds, flash the fleet, sync-word A/B
test, `hmac_connect.py` fix, wave-5 bench, one night, release and PR documents. Operator brief 2026-11-24.

## Operator decisions (2026-11-24, do not re-ask)

- The copied mbedTLS code under `src/esp32/idf_override/mbedtls/` is removed before builds and tests; Auto Update
  builds, links and works without it.
- DK5EN-98: exactly one OTA flash of the release-candidate image in this campaign. Nothing else on it (no outage
  simulation, no test traffic, no setting change). Logs may be read.
- The next upstream PR is ONE combined PR: Auto Update, small fixes, gateway, remote management.
- Stop point: fork-dev committed and pushed, all documents prepared. No tag, no PR branch pushed, no PR, no issue
  without the operator's go.

## Steps and status

| Step | What                                                    | Status  |
| ---- | ------------------------------------------------------- | ------- |
| 1    | mbedTLS copy removed, host gate, AU re-measured -1, -92 | running |
| 2    | full host gate, every board env                         | open    |
| 3    | flash all bench nodes + DK5EN-98 (OTA), AU on T-Deck    | open    |
| 4    | sync-word A/B on DK5EN-90                               | open    |
| 5    | `tools/hmac_connect.py` fixes (HMAC-WIN-01)             | open    |
| 6    | wave 5 bench (#1307, #1308, #1303)                      | open    |
| 7    | one night: AU check + download x3 per ESP32 node        | open    |
| 8    | documents (release notes, journal, PR text, issues)     | open    |

## Step 1: mbedTLS copy removed

Effect on the Auto Update path: the output record buffer goes back to the stock 16717 B (content 16384). The AU
buffer of a job (`FWNET_BUF32_LEN`, max(in + out record, inflate dictionary)) grows from 32768 B to 33440 B
(2 x 16720 in 16-byte slots), +672 B at the job peak. Outside Auto Update every TLS session costs 12 KB more than
with the copy, the state before `469bb942`.

- Code: `src/esp32/idf_override/mbedtls/` and `README-mbedtls.md` deleted (about 23,000 lines); link marker
  `mc_mbedtls_out4k_marker` removed from `mc_ssl_client.cpp` (the partial-write loop in `mc_ssl_send` stays, it is
  correct for any record size); `FWNET_TLS_OUT_BUF` = `FWNET_TLS_REC_BUF` (16717 B); AU buffer length from
  `fwAuBufLenFor()` in `src/fw_au_buf.h`.
- Regression test: `native_fw_au_buf` `test_au_buffer_holds_both_stock_records`. Red with the old output constant
  (`-DFWNET_TLS_OUT_BUF=4429u`: "Expected 16717 Was 4429"), green now; suite 17/17, `native_fw_update` 133/133.
- Gate 2026-11-24 14:48 (`tools/regression.sh --stage 1,2`): 129 Unity envs, 4235 cases, 0 failed; pytest 1830
  passed; board links t_echo 55.7 %, heltec_t114 53.2 %, wiscore_rak4631 83.7 %, heltec_wifi_lora_32_V3 52.9 %,
  ttgo_tbeam 55.9 %. One red step, the suite inventory (new case), regenerated and rechecked.
- Bench re-measure on DK5EN-1 and DK5EN-92: with the step-3 image (same AU code), see step 3.
