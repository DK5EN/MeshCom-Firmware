# Bounded model checking

**69 jobs in 8 groups check parsers and decoders of `src/` with ESBMC. A job compiles the real source, so a pass
holds for the code, up to the stated input length and unwind bound.** Rules, commands and limits:
[`../README.md`](../README.md).

> [!WARNING]
> **No complete run is on record.** The run of 2026-11-23 stopped after 22 jobs. The groups `fw_assets`,
> `remote_cmd`, `settings_decode` and `url_decode` and every job added in the rework are unmeasured: their memory
> budgets are defaults, not measurements.

## Groups

| Group             | Target                                           | Bound                                         | Quick | Heavy |
| ----------------- | ------------------------------------------------ | --------------------------------------------- | ----- | ----- |
| `conf_frame`      | `parseConfFrame`                                 | full frame, 255 B                             | 4     | 0     |
| `url_decode`      | `url_percent_decode`                             | 24 B                                          | 5     | 0     |
| `byte_fifo`       | byte ring operations                             | capacity 8, 4 operations                      | 5     | 0     |
| `settings_decode` | `decode_*`, `unescape_string`                    | values up to 8 B                              | 12    | 0     |
| `fw_assets`       | `FwJsonScan`                                     | any length through `step.check`               | 10    | 0     |
| `aprs`            | `decodeAPRS`, `encodeStartAPRS`, `decodeAPRSPOS` | 40 B quick, 255 B heavy                       | 8     | 2     |
| `applink_ws`      | `alWsFeed`                                       | 10 B, cap edge with a reduced cap             | 7     | 4     |
| `remote_cmd`      | `rmParse`, `rmCheck`                             | 32 B free text, 90 B shaped, two rounds heavy | 10    | 2     |

## Job names

- `pass*.check`: the property must hold.
- `witness_*.check`: asserts that an interesting path is never taken. Must fail, or the proof is over nothing.
- `mut_code_*.check`: a bounds check is removed from a copy of the source in `gen/`. Must fail. If the `sed` no
  longer changes the source, the job is a harness error.
- `mut_*.check`: the harness is broken on purpose, mostly a buffer one byte short. Proves the tool is awake, not
  that the property is sharp.

## Job keys

`TOOL`, `SOURCES`, `CFLAGS`, `FLAGS`, `EXPECT`, `EXPECT_PROP`, `CHECKS`, `TIMEOUT`, `TIER`, `MEM`,
`UNWIND_WAIVER`. They are documented at the top of `run_cbmc.sh`. A `.check` file is a shell fragment and may
generate files in `gen/` before the tool starts.

## Changes of the rework (2026-11-23)

- **Tiers.** Jobs that ran out of memory at 6 GB are `TIER=heavy`: `aprs/pass`, `aprs/pos_pass`, three 300-byte
  AppLink jobs.
- **Unwinding assertions** are back on in `aprs/pass` and `aprs/enc_pass_widepath`. If one of them now fails on an
  unwinding assertion, raise `--unwind`. Do not switch the assertion off.
- **`aprs/pass_40`:** `decodeAPRS` complete up to 40 bytes, sized for the quick tier.
- **AppLink cap at small scope:** `pass_cap_small`, `witness_cap_small`, `mut_code_nocap`, `pass_cap_small_frag`
  build the decoder with `APPLINK_MAX_IN` reduced to 6. The cap logic is the same code; the stream is 13 or 18
  bytes instead of 300.
- **Replay of remote management:** `remote_cmd/pass_replay_direct` runs two rounds on one state and asserts that
  an executed frame is never executed twice. `witness_cached_direct` shows the reply cache is reached.
- **Code mutations:** `conf_frame/mut_code_nocalllen`, `url_decode/mut_code_noroom`, `applink_ws/mut_code_nocap`.

## Known gaps

- **Full-frame `decodeAPRS`** needs the function split, or a contract per stage. Until then it is heavy and unproven.
- **No code mutation yet** for `byte_fifo`, `settings_decode`, `remote_cmd`, `fw_assets` beyond `mut_nulurl`.
- **Stubs are not audited:** the Arduino `String` stub, the `strtod` model, the HMAC model.
- **`checkRegexCall`** is modelled as "any answer". Its own safety is not covered.
