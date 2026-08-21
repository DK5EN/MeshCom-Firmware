# MeshCom mock server

A test double for the MeshCom **node ↔ server UDP protocol** (port 1990),
implemented against `docs/architecture/11-wire-format.md` §2. Stdlib
Python 3 only — no external dependencies.

## Files

- `meshcom_server.py` — the mock server: registers clients from `KEEP`,
  answers every `KEEP` with a `BEAT`, validates and redistributes `DATA`
  frames as `GATE` to every _other_ registered client, and drops corrupt
  datagrams (`MAX_ZEROS` rule). Also exposes `send_gate()` / `send_conf()`
  and the `build_*_datagram()` byte-builders for direct use from tests or
  scripts.
- `mock_client.py` — a minimal softnode-style client: sends `KEEP`, receives
  `BEAT`, can send a `DATA`-wrapped LoRa frame, and prints anything received
  (`BEAT`/`GATE`/`CONF`) as hex plus a short summary.
- `test_mock_server.py` — stdlib `unittest` suite covering the protocol
  surface end-to-end over real UDP sockets.

## Running the server

```sh
python3 tools/mock/meshcom_server.py --port 1990 --verbose
```

Options: `--host`, `--port` (default 1990), `--callsign` (server-side
callsign sent in `BEAT`, default `MOCK-SRV`), `--beat-status` (adds the
optional status TLV to every `BEAT`), `--registry-ttl` (seconds before an
idle client is expired, default 120), `--verbose`.

## Running the client

```sh
python3 tools/mock/mock_client.py --server-port 1990 --gateway-id 0x48A4690D \
    --callsign MOCK-01 --frame-hex 21AB13F1E991...
```

Sends a `KEEP`, prints the `BEAT` reply, optionally sends a `DATA`-wrapped
frame (raw LoRa bytes as hex), then prints anything else the server sends.

## Running the tests

```sh
python3 -m unittest discover tools/mock -v
python3 tools/mock/test_mock_server.py
```

Each test starts the mock server in-process on an ephemeral UDP port
(`port=0` + `getsockname()`) and drives it with real UDP sockets acting as
fake nodes. DATA-header tests use real captured frames (`f001`, `f006`) read
directly out of `test/test_aprs_corpus/corpus.txt`.

## Doc-11 discrepancies found while implementing this

Per the task brief, the mock follows **doc 11 §2 as the contract under
test**; these are reported, not silently patched into the mock:

1. **`MAX_ZEROS` corrupt-datagram check.** Doc 11 §2.2 states plainly:
   "Datagrams with more than `MAX_ZEROS` = 6 consecutive zero bytes are
   discarded as corrupt." `meshcom_server.py`'s `_has_excess_zero_run()`
   implements exactly that — a scan for the longest run of `0x00` bytes
   anywhere in the datagram.

   The actual firmware check
   (`src/udp_functions.cpp:122-136`, cloned at
   `src/nrf52/nrf_eth.cpp:210-246`) is narrower:

   ```c
   for (int i = 0; i + 1 < packetSize; i += 2) {
     if (buf[i] == 0x00 && buf[i + 1] == 0x00) zerocount += 2;
     else zerocount = 0;
   }
   if (zerocount <= MAX_ZEROS) { /* process */ }
   ```

   This walks the buffer in non-overlapping 2-byte steps and resets the
   counter to 0 on any step that is not an all-zero pair — so it only ever
   measures the **trailing** run of zero-byte _pairs_ (pair-aligned to an
   even offset), not the longest zero run anywhere in the packet. A 7+
   zero-byte run in the **middle** of an otherwise well-formed datagram,
   followed by non-zero bytes, resets `zerocount` back to 0 and is **not**
   rejected by real firmware — only by a literal reading of the doc's prose
   (which is what this mock, and the task's test #4, implement). A doc fix
   would need to either restate the rule precisely or note it only reliably
   catches trailing zero runs.

No other discrepancies against doc 11 §2 were found; the `KEEP`/`DATA`
header layouts, the `BEAT` TLV, the `GATE` envelope, and the `CONF` TLV
(including the little-endian `int32` lat/lon/alt encoding confirmed against
`src/nrf52/nrf_eth.cpp:497-587`) all matched firmware and the independent
`mc-chat/meshcom_mock/` implementation byte-for-byte.
