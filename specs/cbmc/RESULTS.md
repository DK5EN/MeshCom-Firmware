# Bounded model checking results (quick and heavy tier)

Written by `specs/cbmc/run_cbmc.sh --all --heavy`. Do not edit by hand.

- Date: 2026-11-27T02:51Z
- Commit: cdfc76ea plus uncommitted changes in specs/cbmc or src
- Tools: ESBMC version 8.7.0 64-bit aarch64 macos, 6.11.0 (cbmc-6.11.0)
- Jobs: 74, unexpected or tool error: 0

| job | tool | tier | proves | expected | result | verdict | peak MB | time |
|---|---|---|---|---|---|---|---|---|
| applink_ws/mut_code_nocap | esbmc | quick | code mutation: message cap removed from alWsFeed (APPLINK_MAX_IN=6) -> write past msg[] | fail | fail | ok | 982 | 14s |
| applink_ws/mut_short_in | esbmc | quick | mutation: input one byte short | fail | fail | ok | 975 | 8s |
| applink_ws/pass_any | esbmc | quick | any byte stream <=10 B, arbitrary chunking (3 feeds): bounds, msg_len/ctl_len limits, progress, sticky error | pass | pass | ok | 917 | 14s |
| applink_ws/pass_big_frag | esbmc | heavy | BIN(FIN=0,150)+CONT(150..151 = 300 / 301), one chunk per frame (2 calls), unwind 170: reassembly limit at 300, no overrun | pass | pass | ok | 4667 | 45s |
| applink_ws/pass_big_single | esbmc | heavy | one BIN frame, 16-bit length 297..302 vs APPLINK_MAX_IN=300, chunks header/payload: msg[300] filled but never overrun, 1009 beyond | pass | pass | ok | 8887 | 165s |
| applink_ws/pass_cap_small_frag | esbmc | heavy | reassembly limit with APPLINK_MAX_IN=6: any 18-byte stream (two masked frames fit), 3 feeds: no overrun | pass | pass | ok | 1706 | 131s |
| applink_ws/pass_cap_small | esbmc | quick | cap edge with APPLINK_MAX_IN=6: any 13-byte stream, 3 feeds: msg[] filled but never overrun, 1009 beyond | pass | pass | ok | 977 | 34s |
| applink_ws/witness_big_single | esbmc | heavy | witness: a message filling msg[300] exactly is decoded | fail | fail | ok | 9358 | 170s |
| applink_ws/witness_cap_small | esbmc | quick | witness: with APPLINK_MAX_IN=6 a message that fills msg[] exactly is decoded | fail | fail | ok | 1222 | 19s |
| applink_ws/witness_err1009 | esbmc | quick | witness: 1009 too-big error reachable | fail | fail | ok | 1078 | 10s |
| applink_ws/witness_msg | esbmc | quick | witness: complete message decoded | fail | fail | ok | 1095 | 12s |
| aprs/charset_contract | esbmc | quick | charset_filter_apply(PLAIN): in bounds, result <= len (decodeAPRS contract) | pass | pass | ok | 1788 | 57s |
| aprs/enc_mut_short_buf | esbmc | quick | mutation enc_mut_short_buf (encodeStartAPRS target one byte short) | fail | fail | ok | 226 | 2s |
| aprs/enc_mut_widepath | esbmc | quick | mutation: F3-8 old clamp (ilng >= 255 -> 254) reintroduced, path fields 140 wide -> write past the 255-byte target | fail | fail | ok | 2099 | 40s |
| aprs/enc_pass_widepath | esbmc | quick | F3-8 fixed: path fields 140 wide (MC_PATH_LEN raised) -> encodeStartAPRS stays within the 255-byte target | pass | pass | ok | 2267 | 40s |
| aprs/enc_pass | esbmc | quick | encodeStartAPRS vs 255-byte target (F3-8) | pass | pass | ok | 1847 | 18s |
| aprs/fcs | esbmc | quick | aprsFcsSum (decodeAPRS frame check sequence): reads only buf[0..n), sum <= 255*n, any buffer 0..255 B | pass | pass | ok | 106 | 3s |
| aprs/mut_pos_norange | esbmc | heavy | code mutation: lat/lon range check removed from decodeAPRSPOS -> out-of-range float-to-int conversion must be found | fail | fail | ok | 1909 | 92s |
| aprs/mut_short_buf | esbmc | quick | mutation mut_short_buf (decodeAPRS buffer one byte short) | fail | fail | ok | 189 | 2s |
| aprs/pass_40 | esbmc | quick | decodeAPRS memory safety + NUL-terminated fields, rsize 0..40 (complete up to 40 B; full frame is pass.check, heavy) | pass | pass | ok | 358 | 8s |
| aprs/pass | esbmc | heavy | decodeAPRS memory safety + NUL-terminated fields, rsize 0..255 (full UDP frame), charset filter modelled, FCS sum by contract (fcs.check) | pass | pass | ok | 11264 | 721s |
| aprs/pos_pass | esbmc | heavy | decodeAPRSPOS memory safety | pass | pass | ok | 2402 | 850s |
| aprs/witness_accept | esbmc | quick | witness: a frame with non-empty text is accepted (accept path reachable) | fail | fail | ok | 192 | 3s |
| byte_fifo/mut_cap_plus1 | esbmc | quick | mutation: cap reported one larger -> caught by the cap invariant | fail | fail | ok | 0 | 0s |
| byte_fifo/pass | esbmc | quick | cap 8, any 4 ops (push2/peek/peek_gen/pop_if/pop/iter), shadow model, wrap+eviction | pass | pass | ok | 421 | 58s |
| byte_fifo/witness_evict | esbmc | quick | witness: unread eviction reachable | fail | fail | ok | 287 | 8s |
| byte_fifo/witness_popif | esbmc | quick | witness: pop_if success reachable | fail | fail | ok | 276 | 7s |
| byte_fifo/witness_wrap | esbmc | quick | witness: wrap-around reachable | fail | fail | ok | 256 | 7s |
| conf_frame/mut_code_nocalllen | esbmc | quick | code mutation: call length check removed from parseConfFrame -> read past the payload | fail | fail | ok | 0 | 1s |
| conf_frame/mut_short_buf | esbmc | quick | mutation mut_short_buf | fail | fail | ok | 0 | 0s |
| conf_frame/pass | esbmc | quick | memory safety + NUL-terminated fields, len 0..255 (full UDP frame) | pass | pass | ok | 0 | 1s |
| conf_frame/witness_accept | esbmc | quick | mutation witness_accept | fail | fail | ok | 0 | 1s |
| fw_assets/leak | esbmc | quick | state reset: nothing of the previous asset survives beginAsset (byte-wise, 6 free bytes in asset 1) | pass | pass | ok | 365 | 5s |
| fw_assets/mut_cap | esbmc | quick | mutation mut_cap: url cap + 1 | fail | fail | ok | 758 | 19s |
| fw_assets/mut_nulurl | esbmc | quick | mutation: raw control check removed -> NUL in browser_download_url gives found with an empty url | fail | fail | ok | 0 | 0s |
| fw_assets/mut_short | esbmc | quick | mutation mut_short: declared length = array size + 1 | fail | fail | ok | 1187 | 9s |
| fw_assets/pass_doc | esbmc | quick | valid release doc with 8 free bytes inside browser_download_url: safety + result fields | pass | pass | ok | 928 | 17s |
| fw_assets/pass_free | esbmc | quick | memory safety + UB + result fields, 8 free bytes (any value), nondet chunk split | pass | pass | ok | 1960 | 19s |
| fw_assets/pass_nulurl | esbmc | quick | raw control byte (incl. NUL) inside browser_download_url fails the scan closed; an accepted asset never has an empty url | pass | pass | ok | 0 | 1s |
| fw_assets/pass_urlcap | esbmc | quick | 250-char url prefix + 8 free bytes (cap 255 edge): safety + url NUL-terminated within cap | pass | pass | ok | 841 | 11s |
| fw_assets/step | esbmc | quick | inductive step: any invariant state + any byte stays safe, keeps the invariant, result fields NUL-terminated (unbounded input) | pass | pass | ok | 717 | 140s |
| fw_assets/witness_accept | esbmc | quick | witness: an asset with a non-empty url is produced (accept path reachable) | fail | fail | ok | 0 | 1s |
| remote_cmd/canon | esbmc | quick | rmCanonical alone (contract of the rmCheck proofs): memory safety, 0 with out[0]==0 on failure, else 1..n-1 with NUL at len and len <= 94 | pass | pass | ok | 0 | 0s |
| remote_cmd/empty_secret_direct | esbmc | quick | empty / space-only / NULL secret: RM_REJ_DISABLED, no HMAC computed, never dispatched (audit M1) | pass | pass | ok | 886 | 8s |
| remote_cmd/mut_canon_max | esbmc | quick | mutation: canonical text never longer than 93 -> must fail (the contract bound 94 is reached) | fail | fail | ok | 0 | 0s |
| remote_cmd/mut_canon_swap | esbmc | quick | code mutation: rmCheck builds the canonical text with dst/src swapped -> the AUTH claim must fail | fail | fail | ok | 1096 | 12s |
| remote_cmd/mut_noauth_direct | esbmc | quick | opposite property: dispatch only when HMAC never ran -> must fail | fail | fail | ok | 1145 | 12s |
| remote_cmd/mut_short_text | esbmc | quick | mutation: text buffer one byte short (no terminator) read by rmParse | fail | fail | ok | 124 | 3s |
| remote_cmd/mut_tagflip_direct | esbmc | quick | opposite property: dispatched tag differs from MAC -> must fail | fail | fail | ok | 1094 | 11s |
| remote_cmd/pass_auth_direct | esbmc | quick | rmCheck over any RmCmd (all bytes nondet), nondet secret/sender/state: memory safety, dispatch => HMAC verified over canonical text | pass | pass | ok | 1263 | 159s |
| remote_cmd/pass_parse_free | esbmc | quick | rmParse on free text up to 32 B, NUL anywhere: memory safety + output shape | pass | pass | ok | 143 | 2s |
| remote_cmd/pass_parse_struct | esbmc | quick | rmParse on frame-shaped text up to 90 B (cmd/args caps, 32-bit ctr overflow): memory safety + output shape | pass | pass | ok | 633 | 9s |
| remote_cmd/pass_replay_direct | esbmc | heavy | rmCheck over any RmCmd (all bytes nondet), nondet secret/sender/state: memory safety, two rounds on one state: dispatch => HMAC verified, an executed frame is never executed twice (replay) | pass | pass | ok | 6700 | 1452s |
| remote_cmd/witness_cached_direct | esbmc | heavy | witness: the reply cache (RM_CACHED) is reachable in round 2 | fail | fail | ok | 6788 | 210s |
| remote_cmd/witness_dispatch_direct | esbmc | quick | witness: dispatch reachable (RmCmd direct) | fail | fail | ok | 1088 | 11s |
| remote_cmd/witness_ok_direct | esbmc | quick | witness: RM_OK reachable | fail | fail | ok | 819 | 9s |
| remote_cmd/witness_parsed | esbmc | quick | witness: rmParse accepts a frame-shaped text | fail | fail | ok | 473 | 6s |
| settings_decode/field | esbmc | quick | decode_field_value all types: no OOB read, no write past field | pass | pass | ok | 243 | 4s |
| settings_decode/mut_signed_len | esbmc | quick | mutation: value buffer one smaller than value_len (decode_signed) | fail | fail | ok | 0 | 1s |
| settings_decode/mut_string_cap | esbmc | quick | mutation: out buffer one smaller than cap | fail | fail | ok | 0 | 1s |
| settings_decode/mut_string_len | esbmc | quick | mutation: value buffer one smaller than value_len | fail | fail | ok | 0 | 1s |
| settings_decode/real | esbmc | quick | decode_real/decode_bool no OOB read | pass | pass | ok | 0 | 0s |
| settings_decode/signed | esbmc | quick | decode_signed: no OOB read, result inside native width and range | pass | pass | ok | 91 | 2s |
| settings_decode/string | esbmc | quick | unescape_string: no write past cap, NUL, no read past value_len | pass | pass | ok | 0 | 1s |
| settings_decode/unsigned | esbmc | quick | decode_unsigned: no OOB read, result inside native width and range | pass | pass | ok | 0 | 0s |
| settings_decode/witness_field | esbmc | quick | witness: decode_field_value accepts a value | fail | fail | ok | 249 | 3s |
| settings_decode/witness_signed | esbmc | quick | witness: decode_signed accepts a value | fail | fail | ok | 0 | 0s |
| settings_decode/witness_string | esbmc | quick | witness: unescape_string accepts non-empty value | fail | fail | ok | 0 | 1s |
| settings_decode/witness_unsigned | esbmc | quick | witness: decode_unsigned accepts a value | fail | fail | ok | 0 | 1s |
| url_decode/func | esbmc | quick | valid hex pair %XX decodes to that byte (len 3) | pass | pass | ok | 0 | 1s |
| url_decode/mut_cap | esbmc | quick | mutation: buffer one byte short | fail | fail | ok | 0 | 1s |
| url_decode/mut_code_noroom | esbmc | quick | code mutation: room check removed from escape_at -> read past the buffer on a trailing % | fail | fail | ok | 0 | 1s |
| url_decode/pass | esbmc | quick | memory safety, ret<=len, NUL at buf[ret]; len 0..24, buffer len+1, any bytes | pass | pass | ok | 124 | 1s |
| url_decode/witness_decode | esbmc | quick | witness: escape decode reachable | fail | fail | ok | 0 | 1s |
