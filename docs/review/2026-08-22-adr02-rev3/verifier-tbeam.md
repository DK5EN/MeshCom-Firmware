# Verifier: T-Beam buffer-cap claims (Claim Set A vs Claim Set B)

Verified independently against `/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main`
(branch v4.35p_prio) and `heys.jsonl` / `fleet.json` in the scratchpad. All quotes reproduced,
not taken from any finder.

## VERDICTS

- **Claim Set A: CONFIRMED** (one line-number nit: the dead branch is at **186-192**, values at
  187-192; both docs cited do support it).
- **Claim Set B: REFUTED.** No T-Beam builds with MAX_MHEARD=10 / MAX_DEDUP_RING=10. The cited
  "configuration_global.h:189-190" lines sit inside a branch guarded by `ENABLE_TBEAM`, which is
  defined nowhere in the tree. Classic T-Beams get 30/70, T-Beam Supreme and T-Beam-1W get 80/100.
  The fleet data is flatly incompatible with cap 10 (29 classic-T-Beam nodes report >10; several
  are pinned at exactly 30; none exceed 30).

## 1. The preprocessor ladder (src/configuration_global.h, current file)

```
169: #if defined(ENABLE_XML) || defined(ENABLE_SBUFFER)
172: #define MAX_MHEARD 50
173: #define MAX_MHPATH 50
174: #define MAX_RING 20
175: #define MAX_DEDUP_RING 60
178: #elif defined(CONFIG_IDF_TARGET_ESP32S3) || defined(BOARD_RAK4630)
180: #define MAX_MHEARD 80
181: #define MAX_MHPATH 100
182: #define MAX_RING 20
183: #define MAX_DEDUP_RING 100
186: #elif defined(ENABLE_TBEAM)                // very smal version only for developer tests
187: #define MAX_MHEARD 10
188: #define MAX_MHPATH 10
189: #define MAX_RING 10
190: #define MAX_DEDUP_RING 10
193: #else
195: #define MAX_MHEARD 30
196: #define MAX_MHPATH 40
197: #define MAX_RING 30
198: #define MAX_DEDUP_RING 70
201: #endif
```

`grep -rn ENABLE_TBEAM` over the whole tree (src, variants, platformio.ini, boards, tools) hits
**only src/configuration_global.h itself** — no `-D ENABLE_TBEAM` in any ini, no `#define` in any
header, no indirect mapping from `BOARD_TBEAM*`. The branch is unreachable in every build. Dead.

## 2. What each T-Beam variant actually defines

| Variant env        | `board =`              | Distinguishing flag          | MCU          | Branch taken | MHEARD / MHPATH / RING / DEDUP |
| ------------------ | ---------------------- | ---------------------------- | ------------ | ------------ | ------------------------------ |
| ttgo_tbeam         | ttgo-t-beam            | `-D BOARD_TBEAM` (ini:28)    | ESP32        | `#else`      | 30 / 40 / 30 / 70              |
| ttgo_tbeam_SX1262  | ttgo-t-beam            | (esp32+common flags only)    | ESP32        | `#else`      | 30 / 40 / 30 / 70              |
| ttgo_tbeam_SX1268  | ttgo-t-beam            | (esp32+common flags only)    | ESP32        | `#else`      | 30 / 40 / 30 / 70              |
| LilyGo_T-Beam-1W   | esp32-s3-wroom-1-n16r8 | `-D BOARD_TBEAM_1W` (ini:24) | **ESP32-S3** | S3 branch    | 80 / 100 / 20 / 100            |
| ttgo_tbeam_supreme | t-beams3-supreme       | `-D BOARD_TBEAM_V3` (ini:31) | **ESP32-S3** | S3 branch    | 80 / 100 / 20 / 100            |

`BOARD_TBEAM` / `BOARD_TBEAM_V3` / `BOARD_TBEAM_1W` never map to `ENABLE_TBEAM` anywhere.
`CONFIG_IDF_TARGET_ESP32S3` comes from the framework sdkconfig on S3 boards. Same fallback class
(30/40/30/70) as TLORA V2.1.6 (`board = ttgo-lora32-v21`, classic ESP32) — Claim A's "same as
TLORA" holds.

## 3. Docs check

- `docs/architecture/08-defect-catalogue.md:201` — C-11: "That block is `#if ENABLE_TBEAM` —
  **defined nowhere**. Dead code quoted as live." Supports A.
- `docs/architecture/10-buffer-inventory.md` §1.2 (lines 59-79): table lists the ENABLE_TBEAM
  column (10/10/10/10) and the branch-liveness table says `ENABLE_TBEAM | **no** | defined
nowhere` with the T-Beam classics in the fallback class and T-Beam-1W / Supreme in the S3 class.
  Supports A. (Nit: §1.2's "Declared at :104-109" line refs are stale vs the current file, where
  the dead branch is :186-192; values match exactly.)

## 4. Empirical cross-check (heys.jsonl 96,074 unique frames + fleet.json 1,430 nodes)

- **DB0HOB-12**: fleet label `TBEAM V1.2`, latest origin `nct=26`. Confirmed. Impossible under
  cap 10, consistent with cap 30.
- **27 classic-T-Beam-labeled origins with latest nct>10** (29 counting the merged relay view).
  Top ones: DK7WK-12 nct=30, OE3RAB-2 nct=29, OE3XKD-1 nct=29, OE3RAB-1 nct=29, DB0KH-11 nct=29,
  OE3XIA-12 nct=27, IZ5TIY-12 nct=26 (that is >5 beyond DB0HOB-12).
- **Ceiling test**: max value ever reported by any `TBEAM V*` node is exactly **30**; zero
  cap-30-class nodes (T-Beam, TLORA, Heltec V2.1, E22, T-Echo, T114) report >30. The observed
  ceiling IS the fallback cap — direct in-field proof of 30, refutation of 10.

### Corrected saturation table (latest merged value = relay 3-value mheard where seen, else origin nct; hardware-mapped real caps)

| Cap class      | Boards (fleet share)                                                                                                               | MHEARD cap | Nodes at >= cap-1                                                                      |
| -------------- | ---------------------------------------------------------------------------------------------------------------------------------- | ---------- | -------------------------------------------------------------------------------------- |
| fallback 30/70 | TLORA V2.1.6, TBEAM V1.1/V1.2 (+1262/1268), EBYTE E22, HELTEC V2.1, T-ECHO, T114 — 921 nodes (64.4%)                               | 30         | **10** (5 at exactly 30: IZ5RGO-10, IZ5FSA-12, DB0KH-11, DK7WK-12, IW5EIA-12; 5 at 29) |
| S3/RAK 80/100  | HELTEC V3/V4/Stick/Tracker/E290/Paper/E213, RAK4631, T-DECK(+), T-BEAM 1W, TBEAM SUPREME, T3-S3, T-CONNECT-PRO — 483 nodes (33.8%) | 80         | **0**                                                                                  |
| unlabeled      | 26 nodes (1.8%)                                                                                                                    | —          | skipped                                                                                |

Relay-3-value-segments-only (analyze_deep.py's stricter view): 4 saturated, all cap-30 class,
0 in cap-80 class.

Fleet split of "T-Beams": classic (cap 30/70) = 370 nodes = 25.9%; S3-class T-Beams (1W +
Supreme, cap 80/100) = 42 nodes = 2.9%; combined 28.8% — the "~28%" fleet share in Claim B is
right, its cap is wrong.

## 5. What Rev. 3 should print, and what survives of the "weak flank"

Real cap table (per board class, with fleet share):

- ESP32-S3 + RAK4631 class (33.8% of fleet, incl. T-Beam Supreme, T-Beam-1W): MAX_MHEARD 80,
  MAX_MHPATH 100, MAX_RING 20, MAX_DEDUP_RING 100.
- Fallback class (64.4% of fleet, incl. ALL classic T-Beams and TLORA V2.1.6): MAX_MHEARD 30,
  MAX_MHPATH 40, MAX_RING 30, MAX_DEDUP_RING 70.
- ENABLE_XML (E22_XML-DevKitC dev board only, ~0% of fleet): 50/50/20/60.
- ENABLE_TBEAM 10/10/10/10: dead, print nowhere.

Corrected saturation: 10 nodes (5 pinned at 30) in the cap-30 class, 0 in the cap-80 class.

The T-Beam "weak flank" argument as stated (dedup ring 10 wraps -> zombies) is **dead**: classic
T-Beams have MAX_DEDUP_RING 70, and their TX ring (MAX_RING 30) is actually LARGER than the S3
boards' 20 — so no TX-queue-size flank either; if anything, MAX_RING makes S3 boards the smaller
TX queue. What survives is a much weaker, class-wide (not T-Beam-specific) version: the entire
cap-30 fallback class (64% of the fleet — TLORA equally) has MHEARD 30 (observably saturating:
5 nodes pinned at the cap) and a dedup ring of 70 vs 100 — a 30% smaller dedup window than S3
boards, plausibly relevant only under extreme storm rates, and never singling out T-Beams from
TLORA V2.1.6. Any zombie/re-flood argument must be re-derived for ring size 70 and attributed to
the whole fallback class, or dropped.
