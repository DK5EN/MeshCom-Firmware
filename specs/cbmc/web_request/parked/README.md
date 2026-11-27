# Parked job of `web_request`

`pass_parse_hdr` (one header line after a fixed request line, `Host:`/`X-MC:` with symbolic value bytes) runs
`webReqParse` over the whole header loop and does not fit 15 GB (ESBMC 8.7, Bitwuzla: out of memory after 69 s).
Project rule: a job over 15 GB means the code becomes verifiable, not that the budget grows. Planned fix before web
step 10 wires the guard to `WebReq`: split `webReqParse` into a per-line function proven on its own and used by
contract in the loop (BACKLOG FORMAL-WREQ-SPLIT). `run_cbmc.sh --all` matches `*/*.check` only, so this file is not
run; move it back one level to run it.

The constant-input jobs `pass_parse_corpus_hdr` and `pass_parse_corpus_rl` cover the header facts on 76 fixed heads
meanwhile.
