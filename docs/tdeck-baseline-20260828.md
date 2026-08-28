# T-Deck Plus — measurement baseline (2026-08-28)

Device `DK5EN-14`, firmware `d26e39d5` (unmodified behaviour + instrumentation only, see
`src/instrument.h`). **Every later fix is judged against these numbers.** Recorded before any
defect from [`tdeck-gui-verdict.md`](tdeck-gui-verdict.md) was touched.

Conditions: USB powered, WLAN associated (`192.168.68.71`), GPS active, SD card present with
`/maps/europe` (5 909 tiles, z0-z9), no user interaction, no LoRa traffic addressed to the node.

## Run 1 — 60 s idle

```
[INSTR-FLUSH];n;184;total_us;6748175;avg_us;36674;max_us;36987
[INSTR-LOOP];n;8451;total_us;62241001;avg_us;7364;max_us;72000
[INSTR-HEAP];int_free;88040;int_min;86560;int_largest;77812;psram_free;8005183;psram_largest;7995380
[INSTR-GUI];msg_list_children;1;active_tab_bubbles;0;persisted_msgs;0;map_points;1
```

### P1 — display flush

| Metric                   | Measured     |
| ------------------------ | ------------ |
| Flushes in 60 s          | **184**      |
| Mean flush               | **36.7 ms**  |
| Max flush                | 37.0 ms      |
| Total blocking SPI time  | **6.75 s**   |
| Share of wall-clock time | **11.2 %**   |
| Effective SPI throughput | ~33.5 Mbit/s |

Two corrections to the verdict's estimate, in opposite directions:

- **Per-flush cost was overestimated.** The verdict predicted ~45 ms from
  `320*240*2 B / 27 MHz`. Measured **36.7 ms** — 24 % lower, so the real link runs at about
  33.5 Mbit/s rather than the nominal 27. The arithmetic was the right shape, the constant wrong.
- **Flush frequency was badly underestimated, and this dominates.** The verdict assumed roughly one
  invalidation per second (the clock tick), i.e. ~2.7 s per minute. The device actually flushes
  **184 times in 60 s — about 3 per second**. Total blocking time is **6.75 s per minute, 2.5x the
  estimate**.

So P1's real severity is higher than stated, for a reason the verdict did not identify: not the cost
per flush, but how often something invalidates. **Finding out what triggers ~3 invalidations per
second at idle is now the first task of the P1 fix**, ahead of any dual-buffer or DMA work — if two
of the three are avoidable, that is a bigger win than making each flush faster.

### Loop period

Mean 7.4 ms (~136 Hz), **max 72 ms**. The worst case is almost exactly two flushes
(2 x 36.7 = 73.4 ms), so a single loop iteration can absorb two full-screen transfers. This is the
window in which LoRa RX servicing does not happen — at idle, with nothing else going on.

### Heap

Internal free 88 040 B, largest free block 77 812 B → **11.6 % fragmentation at idle, before a
single message has been received**. `int_min` (86 560 B) sits close to current free, so nothing has
spiked yet. PSRAM 8.0 MB free of 8.39 MB — the LVGL draw buffer and one decoded map tile.

### GUI

`msg_list_children;1` with `active_tab_bubbles;0` is the empty-state hint label, not a message.
This is the clean starting point for the H1 experiment: the view counter must track the model
counter, and H1 predicts it will not.

## Still to record

**H1 stimulus is not yet run.** It needs N broadcasts arriving on the currently open group tab.
Generating them from `DK5EN-98` puts real traffic on the shared mesh, so it is not something to do
unannounced — pending an explicit decision on how to produce the stimulus.

Prediction to be tested: `int_largest` falls by ~250 B per received message while
`msg_list_children` grows without bound and `active_tab_bubbles` stops at 50.

## Map pipeline — works

```
[INIT]...Total space: 30417 MB
[ SDMAP ]...Kartenset 1 gefunden: /maps/europe (Zoom 0-9)
[ SDMAP ]...Zoom automatisch angepasst: 8 -> 3 (Originalzoom hatte keine Kachel)
[ SDMAP ]...Kachel geladen & dekodiert: /maps/europe/3/4/4.png (256x256, 131072 Bytes)
```

The card previously carried a Raspberry Pi image, so the firmware had been mounting the 537 MB
`bootfs` partition — that is the origin of the earlier `Total space: 509 MB`. After reformatting to
a single FAT32 partition the full 30 GB is visible.

131 072 B = 256 x 256 x 2, i.e. the tile is decoded and converted to RGB565 in PSRAM as designed.
Note the zoom fallback firing on the very first load (8 -> 3): the node has no position fix yet, so
`sdmap_refresh()` probed five zoom levels with `SD.exists()` before finding a tile. That is the
G05/TD-05 path, and it will keep costing lookups wherever coverage is sparse.
