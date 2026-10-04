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
  scripts. `send_gate()` refuses to inject a frame whose source path carries
  any callsign outside `OWN_CALLSIGN_PREFIXES` (`DK5EN-`): a gateway radiates
  injected frames verbatim, so a foreign callsign there would go on the air
  under a licence we do not hold. Only frames relayed from a registered
  gateway's own `DATA` are exempt (`relayed=True`).
  `parse_stor()` / `StorParseError` and the `STOR` handling are described in
  the next section.
- `mock_client.py` — a minimal softnode-style client: sends `KEEP`, receives
  `BEAT`, can send a `DATA`-wrapped LoRa frame, and prints anything received
  (`BEAT`/`GATE`/`CONF`) as hex plus a short summary.
- `test_mock_server.py` — stdlib `unittest` suite covering the protocol
  surface end-to-end over real UDP sockets, including `STOR` parsing, chunk
  assembly, expiry and routing on/off.

## Running the server

```sh
python3 tools/mock/meshcom_server.py --port 1990 --verbose
```

Options: `--host`, `--port` (default 1990), `--callsign` (server-side
callsign sent in `BEAT`, default `MOCK-SRV`), `--beat-status` (adds the
optional status TLV to every `BEAT`), `--registry-ttl` (seconds before an
idle client is expired, default 120), `--stor-routing` (see below),
`--verbose` (debug log, includes one `event=stor_snapshot` line with the full
call list per applied `STOR` snapshot).

## STOR (store-node announce set, SNF-GW-08, issue #1188)

The mock understands the `STOR` datagram proposed in
`docs/snf-gateway-concept-20261002.md` section 5.3. It is a **proposal**, not
part of doc 11, and the real server does not implement it (yet).

```
STOR<gwid:8 hex><call:9><ver:4><sub:1><hold_h>;<seq>/<total>;<CALL>;<CALL>;...;\0
```

- `parse_stor(payload) -> dict` validates strictly and raises `StorParseError`:
  prefix, 8 hex gwid, ASCII, a decimal `hold_h`, `seq/total` with
  `1 <= seq <= total`, a trailing `;`, calls of 1..9 chars from `[A-Za-z0-9/-]`
  with no empty element, at most 255 bytes. One trailing NUL is stripped, a NUL
  anywhere else is an error. An empty list (`...24;1/1;`) is valid and means
  "withdraw everything". It returns `gwid` (int), `call`, `ver`, `sub`,
  `hold_h`, `seq`, `total`, `calls`.
- Chunks are assembled per `(source addr, gwid)`. A chunk with `seq` 1 starts a
  new round and drops an unfinished one. Chunks 2..n join the round in any
  order. A chunk with no round in progress, with a different `total`, or with a
  `seq` the round already holds is a straggler of an older round and is
  dropped. A round with a missing chunk is never applied, and the previous
  snapshot stays in place until its own expiry.
- A complete round is stored as
  `server.stor_snapshots[gw_call] = {"calls", "hold_h", "t" (monotonic), "addr"}`
  and logged as `event=stor gw=... calls=n`. An empty snapshot is kept as an
  entry with `calls == []`. Snapshots older than 45 min (`stor_ttl`, 3 x 15 min)
  are expired lazily in the same path as the client registry.
- `STOR` is recorded by the recorder like every other datagram and is **never
  answered**. What the real server would reply, if anything, is unknown, so
  the mock does not invent a reply. `STOR` does not register the sender as a
  client; only `KEEP` does.

**Routing is a mock assumption, default off.** With `--stor-routing`
(constructor `stor_routing=True`) a `DATA` text frame (type `0x3A`) whose
destination call (`SRC>DEST:`) is in a live snapshot is also forwarded as
`GATE` to that snapshot's gateway. This follows the concept's _expected_ server
behaviour ("in addition to whatever the server does today, never instead"),
not a verified one. It is added on top of the normal redistribution (a
gateway that is already a target gets one copy, not two), also works with
`--no-redistribute`, never goes back to the sender, ignores expired and
withdrawn snapshots, and keeps `relayed=True` semantics: only genuine `DATA`
from a registered gateway is relayed unchecked, `send_gate()` injection keeps
the own-callsign guard.

## Golden-capture mode (test plan P0.6, steps H6/H7)

For the DRY-unification goldens the same server doubles as the capture
instrument: it records every datagram in both directions and replays a fixed
corpus at the node under test.

```sh
python3 tools/mock/meshcom_server.py --port 1990 \
    --replay test/golden/corpus/udp1990/ --gap 1.0 --repeat 1 \
    --capture-dir test/golden/hw/G0/rak-90/ --no-redistribute --verbose
```

- `--replay DIR` reads `.bin` (verbatim) and `.hex` (whitespace- and
  `#`-comment-tolerant) files in **sorted file order**, so corpus entries carry
  a numeric prefix. Each file is one complete datagram including its
  4-byte indicator. The server waits for the node's first `KEEP`
  (`--replay-timeout`, default 90 s) before it starts, because that `KEEP` is
  where it learns the node's source port.
- `--repeat N` sends the whole corpus N times — the dedup tests need the same
  frame twice.
- `--capture-dir` writes three files on exit, named from the **node's** point
  of view: `udp-log.txt` (one line per datagram: monotonic time, direction,
  peer, indicator, length, hex), `udp-tx.bin` (what the node sent) and
  `udp-rx.bin` (what the node received), both length-prefixed
  (`<uint16 LE len><bytes>` per record). They are written once, at the end, so
  a crashed run cannot leave a truncated file that looks complete.
- `--no-redistribute` turns off the `DATA` → `GATE` broadcast. That routing is
  a mock assumption (see the note at the end of this file), and a golden
  capture must contain only what the replay deliberately sent.
- `--linger` keeps recording for N seconds after the last replayed datagram,
  so the node's reaction lands in the same capture.

Corpus entries are **not** checked against `OWN_CALLSIGN_PREFIXES` on replay —
they are sent verbatim by design. The corpus lint (test plan P0.5) is what
guarantees no foreign callsign can reach the air, and it runs when the corpus
is built. `send_gate()` keeps its check for frames built on the fly.

Captured text is compared after `test/golden/normalize.py`; the binary files
are compared byte for byte after masking the volatile offsets
(`normalize.mask_binary()`).

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
header layouts, the `GATE` envelope, and the `CONF` TLV (including the
little-endian `int32` lat/lon/alt encoding confirmed against
`src/nrf52/nrf_eth.cpp:497-587`) all matched firmware and the independent
`mc-chat/meshcom_mock/` implementation byte-for-byte.

**`BEAT` is a partial exception to that "byte-for-byte" claim.** The
firmware side only ever parses the 4-byte `"BEAT"` indicator itself
(`src/nrf52/nrf_eth.cpp`, `src/udp_functions.cpp`) — it never reads the
TLV body this mock and the tests build. The TLV shape (`0x00 <len>
<callsign> [0x01 <len> <status>]`) is not specified by doc 11 §2 at all;
it comes from live-capture, decoded by mc-chat's `decoder.py`
(`_decode_beat_struct`, see doc 11 §2.2). So "matches firmware" is true
only for the indicator; "matches the observed wire shape" is the accurate
claim for the rest of the `BEAT` datagram, and that's what this mock and
`test_mock_server.py`'s byte-exact `BEAT` assertions actually verify.

**Mock-only assumption beyond doc 11: `DATA` → `GATE` broadcast routing.**
Doc 11 §2 specifies the _wire shape_ of `DATA` and `GATE` but says nothing
about server-side routing policy. This mock's choice — forward every valid
`DATA` frame as `GATE` to all _other_ registered clients, never back to the
sender — is a mock assumption, not a documented or verified real-server
behavior. Real-server routing semantics (dedup, group filtering, etc.)
remain unmocked, and the optional `--stor-routing` forwarding above is the
same kind of assumption; acceptable for a wire-shape test double, but do not read
`TestDataRedistribution` as proof of real-server routing policy.
