# LoRa sync word on air: what each chip sends for 0x2B (2026-11-21)

**With `0x24B4` the RAK4631 sends the MeshCom sync word exactly like a Heltec V3 (SX1262). Today's
nRF52 setting `0x242B` puts the second sync symbol where no other board sends it, and the SX1278 sends
it 128 bins away from every SX126x.** Nothing is changed in the firmware yet. The switch to `0x24B4` is
backlog row SYNC-NRF-01 and waits for OE1KFR's decision.

## Why this was measured

- **Problem:** the LR2012 (Semtech Gen4) drops frames from RAK/nRF52 boards and from SX127x boards with a
  header CRC error. It receives frames from ESP32 boards with an SX1262.
- **Suspicion:** the boards put the same sync word on different start frequencies, and the LR2012 checks
  it more strictly than the older chips.
- **How:** OE1KFR recorded each board with an RTL-SDR and gqrx (1.152 MS/s), 7 to 15 frames per board, all
  sent directly. DK5EN dechirped every frame and measured both sync symbols to a fraction of a bin. The
  sdr project's LoRa decoder decoded 43 of the 44 frames with a good CRC; one was cut off at the end of
  its file.

## How the sync word goes on air

- **Two symbols:** after the preamble, one LoRa symbol per hex digit of the sync word.
- **Bins:** at SF11 a symbol starts at one of 2048 frequency steps (bins) of about 122 Hz. The preamble
  sits at bin 0; each sync digit is sent at digit x 8. Counting wraps, so bin 2008 is -40.

## Result (MeshCom EU8: 433.175 MHz, BW 250 kHz, SF11)

| Board and chip                | Configured          | Frames | 1st symbol | 2nd symbol | Meaning                       |
| ----------------------------- | ------------------- | ------ | ---------- | ---------- | ----------------------------- |
| Heltec V3, SX1262, OE1KFR-7   | `0x2B` via RadioLib | 15     | 16.0       | -40.0      | digit B sent as -5            |
| RAK4631, SX1262, OE1KFR-3     | `0x24B4`            | 7      | 16.0       | -40.0      | identical to the Heltec V3    |
| RAK4631, SX1262, OE1KFR-3     | `0x242B` (today)    | 13     | 16.0       | +20.0      | between digit 2 and 3, unique |
| T-Beam V1.1, SX1278, OE1KFR-6 | `0x2B` via RadioLib | 9      | 15.9       | +87.9      | digit B sent as +11           |

Median over all frames; the spread per board is below 0.1 bin.

## Reading of the SX126x register (interpretation from three values, not a Semtech statement)

- **Rule:** bin = (register byte read as signed int8 - 4) / 2, rounded to a whole bin.
- **Fits:** `0x24` gives 16, `0xB4` (-76) gives -40, `0x2B` gives 19.5 and was measured at 20.
- **Consequences:** the usual `0xX4 0xY4` form gives digit x 8; digits 8 to F turn negative on the
  SX126x. The SX126x reaches at most about +61, so it cannot send the SX1278's +88, and the SX1278
  (digit x 8, 0 to 120) cannot send -40.

## Firmware facts (v4.40a.11.22)

- **nRF52:** `Radio.SetCustomSyncWord(0x242b)` in `src/nrf52/nrf52_main.cpp:1254` and
  `src/lora_setchip.cpp:303`. The comment at `src/nrf52/nrf52_main.cpp:251` already names `b4` as the
  intended second byte.
- **ESP32:** RadioLib `radio.setSyncWord(rf_syncword)` with `0x2B`, `src/lora_setchip.cpp:552`, which
  RadioLib writes as `0x24 0xB4` on the SX126x.
- **Define:** `SYNC_WORD_SX127x_2BYTE 0x2b24` in `src/configuration_global.h:653` is unused and has its
  bytes swapped; the natural place for a corrected `0x24B4`.

## Proposal sent to OE1KFR (2026-11-21)

1. **RAK/nRF52 to `0x24B4`** at both places (SYNC-NRF-01). The register also sets what the RAK listens
   for. Mixed nodes hear each other today despite +20 vs -40, so SX126x/SX127x receivers check the
   second symbol only coarsely; this is inferred from the net, not measured. Roll out on one RAK first
   and watch a long-term log.
2. **Record the LR2012**, normal and compatibility mode, to learn where it sends and what it expects.
3. **Net-wide sync word:** with `0x2B` no setting puts SX127x and SX126x on the same bin. A sync word
   with both digits 0 to 7 is sent identically by every chip (digit 2 landed at 16 on all three).
   Not `0x12` (LoRa APRS) and not `0x34` (LoRaWAN). Needs a cut-over date; decide after the LR2012
   capture.

## Not checked

- **LR2012:** not measured. That it expects -40 is an assumption from the field.
- **Receive side:** how strictly each chip checks the sync word was not measured.
- **One board per chip:** other SX1262 and SX127x boards should send the same; not measured.

Sources: report to OE1KFR (German, HTML) and campaign doc in the sdr project,
`docs/reference/analysis/meshcom-sync-word-varianten-2026-11-21.html` and
`docs/reference/analysis/meshcom-sync-word-campaign-2026-11-21.md`; analysis script
`scripts/lora-sync-bins.py` there. Captures are kept outside git.
