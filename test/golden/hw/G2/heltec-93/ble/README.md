# G2 H4 -- heltec-93, BLE, final image e64ce346 (2026-09-17 23:33, verified `build: Sep 17 2026 / 23:32:01`)

Capture: `uv run --with bleak python3 tools/bench/ble_golden.py --name DK5EN-93
--corpus test/golden/corpus/ble/writes.txt --restore "--maxhop 2" --out
test/golden/hw/G2/heltec-93/ble`. 24 writes, 20 replies compared, 1
spontaneous excluded. Flashed with `--upload-port /dev/cu.usbserial-0001`;
an earlier "G2" capture this evening was of the MORNING image because the
upload had gone to the T-Deck (port autodetect) -- the `--info` build time
is the check.

## Reproducibility first

Three captures of the morning image (same corpus): 1 vs 2 differed in the
max_hop byte only (a stale 5 from an old run, then the corpus's own
restore), 2 vs 3 **identical over all 20 frames**.

## G2 vs the same-corpus capture of the morning image

`--compare scratchpad/h4rep3/ble-frames.bin ble/ble-frames.bin`:
**5 differences, all the flags byte 36 -> 34** on the five `--wrong command`
responses = max_hop 4 -> 2 (this run restored the node's own value; G1 shows
34). No other byte moved: the final image (ETH-03, DR-28, W7-II, D1-10) is
invisible on the BLE surface, as EXPECTED-DIFF.md predicts (DR-28 changes
the MH register, excluded as live state).

## G2 vs G1 (2026-09-11)

10 differences, none attributable to firmware: SW.IP (DHCP), SN.GPS/TXP,
TM.*, SA.*, I.BLE/GCB* (bench settings entered since 11.09.), and frames
14-16 shifted because the corpus gained three adversarial writes. Flags byte
34 = G1.

Verdict: **H4 identical modulo settings drift and corpus growth.**
