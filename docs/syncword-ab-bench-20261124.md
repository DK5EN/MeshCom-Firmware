# Sync word 0x242B against 0x24B4 on nRF52: bench A/B (2026-11-24)

**On our bench the change of the nRF52 sync word from `0x242B` to `0x24B4` (upstream #1320) has no side effect: a
RAK4631 with either value receives and is received by Heltec V3, T-Deck Plus and T-Beam v1.2 equally well, in both
directions. Two things are not measured: an old nRF52 against a new nRF52 (we have one RAK), and the margin near the
sensitivity limit (every link on the bench table is far above it).**

Backlog row `SYNC-NRF-01`. Campaign: [campaign-rc-bench-20261125.md](campaign-rc-bench-20261125.md), step 4.
Background on air: [syncword-onair-20261121.md](syncword-onair-20261121.md).

## Key facts

- **Images:** commit `732238bd`. Image B = the regular build (`0x24B4`). Image A = the same commit with `0x242B` at
  the two nRF52 call sites, a local bench build, never committed
  (`tools/bench/runs/harness/rc-bench-20261125/images/image-A.patch`).
- **Nodes:** DK5EN-90 (RAK4631, SX1262) against DK5EN-1 (Heltec V3, SX1262), DK5EN-14 (T-Deck Plus, SX1262) and
  DK5EN-92 (T-Beam v1.2). MeshCom EU8, 433.175 MHz, SF11, BW 250 kHz. All nodes under a metre apart.
- **Method:** each node sends 12 group-9999 messages in turn through its web API, every other node listens with
  `--setlog on`. Per pair: sent (own `TX ... src=o` lines), decoded directly (path without a relay), relay-only
  copies, error lines in the window, RSSI and SNR. Tool: `tools/bench/syncword_ab.py`. Runs at 2 dBm and at -9 dBm
  (the T-Beam refuses below -4 dBm and stays at 2 dBm in the low runs).
- **Result:** every frame the RAK sent was decoded by all three ESP32 nodes under both images at 2 dBm. Every frame
  missed in any run is explained by what the receiver was doing at that moment: its own channel check found the
  channel busy, it was transmitting, or a third node transmitted at the same time.
- **Signal levels:** RSSI -34 to -78 dBm, SNR +5 to +13 dB on every link. SF11 decodes down to about -17 dB SNR, so
  these links say nothing about the margin near the sensitivity limit. The operator ruled that part out for the
  bench table (2026-11-24).

## Missed frames per run

| Run                   | RAK receives: missed / sent | own CAD busy | own TX | other | RAK sends: ESP32 misses                               |
| --------------------- | --------------------------- | ------------ | ------ | ----- | ----------------------------------------------------- |
| B (`0x24B4`), 2 dBm   | 7 / 40                      | 7            | 0      | 0     | 0                                                     |
| B, -9 dBm             | 10 / 40                     | 7            | 2      | 1     | 1 (CAD busy)                                          |
| A (`0x242B`), 2 dBm   | 12 / 37                     | 11           | 1      | 0     | 0                                                     |
| A, -9 dBm             | 10 / 39                     | 10           | 0      | 0     | 4: one frame at all three (collision), one CAD busy   |

- **Why the RAK misses frames at all:** the RAK is an Ethernet gateway and queues ACKs it receives from the server.
  Its channel check before sending (CAD) found the channel busy, and that busy channel was the frame it then did not
  decode: after a busy CAD the node goes back to idle instead of receiving. This depends on the gateway's queue, not on
  the sync word, and shows under both images. New finding, see below.
- Per-pair tables with RSSI and SNR: campaign doc, step 4.

## Passive comparison with field stations

- **What it is:** the RAK only listens for 60 minutes under each image; every copy that DK5EN-98 (Heltec V3, outdoor
  antenna) decoded from a station outside the bench is looked up in the RAK's log. Tool
  `tools/bench/syncword_passive.py`; the transmitter's board comes from the last-hop hardware byte of each frame.
- **Window B (16:06-17:06):** the only field station heard was DL2JA-2 (SX126x ESP32), 74 copies at DK5EN-98, all below
  SNR -10 dB; the RAK on its desk antenna decoded none of them. No nRF52 field station transmitted in the window.
- **Window A (17:32-18:32):** the same picture: DL2JA-2 only, 92 copies at DK5EN-98 below SNR -10 dB, none at the
  RAK; no nRF52 field station transmitted. So the passive part shows no A/B difference and nothing about nRF52.
- **What it can show:** whether the RAK with the old or the new value hears field SX126x stations differently, and,
  if a field nRF52 (old value on air) transmits in a window, whether image B hears it worse than image A.
- **What it cannot show:** old nRF52 against new nRF52 at low SNR as a controlled pair. Both receivers sit a few
  metres apart with different antennas, so only the A/B difference of the ratio carries meaning.

## Known gaps

- **No second RAK:** an nRF52 node with `0x242B` against one with `0x24B4` is not measured on the bench. That pair
  exists in the field as long as not all nRF52 nodes are updated; routes that run only over nRF52 nodes depend on it.
  On air the old value sends the second sync symbol at bin +20, the new at -40
  ([syncword-onair-20261121.md](syncword-onair-20261121.md)); mixed nodes are reported to hear each other in the field,
  which is inferred from the net, not measured.
- **Margin near the sensitivity limit:** not testable on the bench table (operator 2026-11-24).
- **TRACK/APRS mode:** checked once under image B (2026-11-24 18:34): `--track on`, one `--sendtrack` (APRS frame on
  433.775 MHz, own call, 2 dBm), back to 433.175 MHz right after `OnTXDone`, `--track off`; afterwards the RAK decoded
  10 of 12 frames of the ESP32 nodes and they decoded 12 of 12 of the RAK. One beacon only; a long TRACK session
  with many APRS transmissions was not run.

## New finding: a node misses the frame that makes its own channel check busy

- **What:** a node with something in its TX queue runs CAD before sending. When a frame starts during that check, the
  CAD reports "busy" for exactly that frame, the node goes back to idle and does not decode it; it gets the frame only
  as a relay copy, if at all.
- **Where seen:** mostly on the RAK4631 gateway (7 to 11 of about 40 frames per run), because it queues gateway ACKs
  from the server; once on DK5EN-1 (Heltec V3) in run B-low. So it is channel-access behaviour on both platforms, not
  the sync word.
- **Code:** CSMA path in `src/lora_functions.cpp` and the platform loops; related to the open CSMA work (CSMA-02,
  CSMA-04). Backlog row `CAD-RXLOSS-01`.
