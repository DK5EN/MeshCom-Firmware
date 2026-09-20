# UDP-1990 golden, Heltec-93 (test plan step H6)

Captured 2026-09-11. `tools/mock/meshcom_server.py` replayed the 37-datagram
corpus at the node; the node was pointed at it with `--srvip 192.168.68.58`
and put into `--gateway on --udplog on`.

| File             | Side | Content                                                       |
| ---------------- | ---- | ------------------------------------------------------------- |
| `udp-log.txt`    | stub | every datagram, both directions, monotonic clock              |
| `udp-tx.bin`     | stub | what the node sent, length-prefixed — 3 KEEPs                 |
| `udp-rx.bin`     | stub | what the node received, length-prefixed — the replayed corpus |
| `udp-rx-log.txt` | node | the node's `[GW];`/`[UDP];` lines, normalized                 |

What the node made of the corpus: 24 `DATA`, 7 `BEAT`, 3 `CET`, 2 `OTHER`,
1 `CONF`. The malformed entries (`GA`, `XXXX`, the oversize and zero-run
cases) land in `OTHER` or are dropped, which is the point of carrying them.

## The CONF entry provisions the node

Replaying this corpus renamed Heltec-93 to `DK5EN-1` / `BNCH` and it stayed
renamed until the configuration was restored. That is not a bug in the stub:
the node applied a valid server provisioning frame, exactly as it is supposed
to. Two consequences:

- **Restore after this capture, not only before it.** The stub now logs a
  warning when it replays a CONF.
- It disproved `architecture/11-wire-format.md` §2.2, which said CONF was
  nRF52-only. The ESP32 branch (`udp_functions.cpp:515-600`, added by TM-39)
  parses, validates and applies it. The document is corrected.

## Not captured here

Step H7 — the node's own `DATA` uploads — needs LoRa frames injected with
`--injectraw` while the gateway is on. Only the 3 KEEPs appear in `udp-tx.bin`
because nothing was injected during this run.
