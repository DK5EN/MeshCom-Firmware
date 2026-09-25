# Verifier V2: section 4.5 NCNT (finder C1-C5, finder F5)

Tree: feature-neighbour-matrix @ 082c2412. Field data: `scratchpad/fable/combined_window.log`, the
DK5EN-98 capture from 2026-09-23 00:12 to 2026-09-24 08:46 (31.6 h usable). Replay scripts:
`scratchpad/v2_snr.py` and `scratchpad/v2_trickle.py`.

| ID                                       | Verdict                    | Severity (corrected)                 |
| ---------------------------------------- | -------------------------- | ------------------------------------ |
| C1 HN veto missing                       | PARTLY                     | Low (finder said High)               |
| C2 SYM flap resets trickle               | PARTLY                     | Low-Medium (finder said Medium-High) |
| C3 B3/B6 not addressed                   | PARTLY, plus one new point | Low                                  |
| C4 mixed NCNT semantics in local MH JSON | PARTLY                     | Low                                  |
| C5 worked example unsourced              | REFUTED (substance)        | Nit                                  |
| F5 no clock gate on MH send path         | REFUTED                    | -                                    |

## C1: SYM formula drops the HN-report veto (PARTLY)

What holds:

- `nbrHearsSym()` (src/nbr_matrix.cpp:1184-1219) has three branches. First, the observed cell
  `cells[mrow][x]` is fresh (12 h) -> true (:1192-1194). Second, the reverse cell is fresh with
  `snr >= NBR_SYM_MIN_SNR` (:1199-1203). Third, a VETO: if x has `NBR_FLAG_RPT` and
  `now - rpt_min < NBR_REPORT_VALID_MIN` (45 min, nbr_matrix.h:145-146), the function returns
  false (:1205-1212).
- The paper's `NCNT = popcount(D60 & (HM | SYM))` (paper.txt:176-179) is branches 1 and 2 without
  branch 3, even though it cites "derselbe Wert wie --nbrsym". The data for the veto survives in
  the new 12-B row (paper.txt:92 keeps `rpt_min` and `flags`), so adding the veto is a single flag
  test.
- Concrete case: neighbour x runs `--nbrreport`, sends a complete report (at most 8 entries, no
  `+`) that does not name me, has relayed none of my frames in 12 h, and I hear x at -10 dB within
  60 min. The paper's formula counts x. `nbrHearsSym()` would veto it. The paper's option table
  calls the chosen row "Zwei-Wege-Nachbarn", so by that label this count is wrong.

What does not hold:

- The code enforces no NCNT invariant here. The veto exists only inside the relay decision
  (`nbrRelayNeed`, behind `--nbrsym`). Today's NCNT is one-sided `getMheardCount()`
  (src/mheard_functions.cpp:691-710).
- The paper's own decision sentence counts x ("oder bei mir mit mindestens -16 dB ankommen",
  paper.txt:175). The paper already accepts the resulting overcount explicitly in Fehlerszenarien
  (paper.txt:281: "Asymmetrische Strecke ... zählt über SYM mit | NCNT zu hoch; keine Entscheidung
  hängt daran").
- The veto means less than the finder implies. A report lists only neighbours heard within 60 min
  at >= -16 dB (nbrBuildReport, src/nbr_matrix.cpp:745-761). "Missing from the report" therefore
  means "does not hear me stably", not "does not hear me". A report with more than 8 candidates is
  truncated (`+`) and cannot veto (:958-964). At super nodes the veto rarely applies.
- Field incidence today is zero. The capture contains 0 received `[NBR]|RPT` lines and 0 `SYM|VETO`
  lines. The only report traffic is DK5EN-98's own 93 `RPTTX`.

Recommendation: add the veto term, or state in 4.5 why negative report evidence is ignored.

## C2: SYM threshold without hysteresis flaps the trickle reset (PARTLY)

Mechanism confirmed:

- The SNR on an edge is the last frame's value, overwritten on every direct reception
  (src/nbr_matrix.cpp:566, :680). There is no EMA and no hysteresis, and none in `nbrHearsSym()`
  either.
- Trickle resets to `TRICKLE_IMIN_S` (30 s) whenever the sampled count changes
  (src/esp32/esp32_main.cpp:3480-3491, src/nrf52/nrf52_main.cpp:2018-2025). The reset also zeroes
  `trickle_consistent_count` before the suppression test, so a reset always sends a HEY.
- Section 4.5 "Folgen" (paper.txt:190) is silent on this.

The finder overstates it:

- The count is not re-evaluated on every frame. It is sampled only at trickle ticks (every 30 s
  up to 900 s, config global.h:454-455), and only a net difference between two consecutive samples
  resets.
- Only neighbours without HM proof can flap. `HM | SYM` masks SYM flips on proven neighbours.

Quantification from the DK5EN-98 capture:

- Today: 9 `TOPO_CHANGE` in 32.5 h, all within 40 min of a reboot (09:20, 19:24, 20:02). In
  steady state there are 0, and every 15-min HEY is suppressed (`consistent=10..15`).
- Received SNR at DK5EN-98 per last hop:

  | Neighbour                            | Frames per hour | Median | Range   | Frames below -16 |
  | ------------------------------------ | --------------- | ------ | ------- | ---------------- |
  | DB0ED-99                             | 100             | -4     | -12..0  | 0                |
  | DL2JA-2                              | 67              | -7     | -18..1  | 6 (12 crossings) |
  | DK5EN-1                              | 43              | 6      | 3..8    | 0                |
  | DL2UD-1                              | 33              | -5     | -19..2  | 2 (4 crossings)  |
  | DL2JA-1 (the only SYM-dependent one) | 7               | -8     | -15..-5 | 0                |

- Replaying the trickle state machine with the paper's NCNT at -16 dB gives 0 resets in 31.6 h.
  That holds even with HM switched off for every neighbour (Stufe-4 extreme). The two dippers dip
  on single frames and recover within about 1 min.
- Sensitivity: one SYM-dependent neighbour whose SNR distribution straddles the threshold (the
  replay sets the threshold to its median) gives 0.7 resets/h at 7 frames/h (DL2JA-1), up to
  3.4 resets/h at 67 frames/h (DL2JA-2).
- Cost per reset, from the log: the resets at 19:56, 20:32 and 21:02 were each followed by 3 HEY
  SENDs before suppression resumed. The resets at 09:37 and 20:02 were followed by 2.
- Exposure: roughly 2-10 extra flooded HEYs per hour per straddling, unproven neighbour, against
  a steady state of 0.

Exposure grows in Stufe 4. The paper itself says Auto-Via strips relay proof from redundant nodes
(paper.txt:183), which makes more neighbours SYM-dependent.

Cheap fixes, any one of which suffices:

- Hysteresis, for example enter at >= -16 dB and leave below -19 dB.
- SYM on the best or median SNR within D60.
- Reset trickle on D60 membership only.

The Stufe-2 shadow comparison (paper.txt:19) would show the flapping as DIFF lines, but only if
someone looks for it.

## C3: B3/B6 not carried over (PARTLY, plus one new point)

Code facts are correct, and both sites are unchanged:

- B3: the sender emits `/N` only when greater than 0 (src/loop_functions.cpp:4545-4550). The
  decoder requires a digit 1-9 (src/aprs_functions.cpp:856). The receiver takes only values
  greater than 0 (src/lora_functions.cpp:1155, :1177).
- B6: the dead trigger is at src/esp32/esp32_main.cpp:3382-3392 and src/nrf52/nrf52_main.cpp:
  1919-1927. `posinfo_timer_min` is reset at esp32_main.cpp:3462 and :3467.

Not a substance gap: the NCNT paper already decided both as no-change. B3 is "nicht ändern" (its
4.3). B6 is "nicht reparieren, Entfernen gehört in die DRY-Kampagne" (its 4.6). The omission is
traceability only.

New points:

- (a) The B6 block is a fifth `getMheardCount()` reader. It is missing from the paper's reader
  inventory (paper.txt:59 lists R<n>, /N, trickle, HN). Stufe 3 "Leser auf nbr_views" must rewire
  or delete it.
- (b) The NCNT paper's B3 mitigation was a Stufe-2 age display for the NCNT value ("Empfangszeit",
  "Altersanzeige"). The new 12-B row (paper.txt:92) has `ncnt` but no timestamp of its own. AGE in
  4.6 is the row's `last_min`, which any frame refreshes. The mitigation is silently dropped.
- (c) Symmetric NCNT reaches 0 more often (the paper's own -18 dB example). This exercises B3
  more, but the change also forces a trickle reset and a HEY `R0`, so it stays bounded to 15 min
  or less.

## C4: Old and new NCNT semantics shown side by side in local MH JSON (PARTLY)

Correct:

- MH JSON `NCNT` carries whatever the sender reported (src/mheard_functions.cpp:433, :920).
- Relay groups still use `getMheardCount()` (src/lora_functions.cpp:1736, :1863).
- None of the 7 new fields labels the semantics.

Why it matters little:

- It is display only. The paper drops NCNT from every decision (the old Auto-Via rule is
  rejected, paper.txt:255).
- The MH list is already heterogeneous today: caps are 30/50/80 by board (paper.txt:81), and five
  TLORA nodes sit at exactly 30.
- The root cause is the open point already listed (paper.txt:299). The paper should name the
  local views next to mcmap, and note that an MH field would be needed to carry the discriminator.

## C5: Worked example unsourced (REFUTED in substance)

The values reproduce from the capture the paper cites for Abb. 6 (paper.txt:252). Latest
`(DK5EN-98, x)` edge SNR:

| Neighbour | Capture                            | Paper |
| --------- | ---------------------------------- | ----- |
| DL2JA-2   | -13 (08:20)                        | -13   |
| DK5EN-1   | 6 (08:42)                          | 6     |
| DB0ED-99  | -6 (08:46)                         | -6    |
| DL2UD-1   | -13 (08:41; last 8 values -13/-14) | -12   |

- DL2JA-1 has zero `(DK5EN-98, DL2JA-1)` edges in 31.6 h, which confirms "leitet nie weiter". Its
  reception median at DK5EN-98 is -8, which matches.
- `neighbors=5` holds throughout steady state.
- Nits: the paper gives -12 for DL2UD-1 where the capture shows -13. The 08:55 timestamp lies
  after the local capture ends (08:46), and no source is cited for this example.

## F5: No clock gate on the MH send path (REFUTED)

- `updateMheard()` returns at src/mheard_functions.cpp:302-304 when the year is before 2025. That
  is before the JSON is built (:405-433) and before `addBLEOutBuffer()` (:438-440).
- Its only caller is src/lora_functions.cpp:1197. `mh_date` comes from `getDateString()`, which
  formats `node_date_year` (src/loop_functions.cpp:3250-3256). That value is 0 on a clockless
  boot (comment at mheard_functions.cpp:1239-1243).
- `sendMheard()` only iterates entries that exist, and those are created only past the gate. The
  paper's "wie heute kein MH-Rahmen" is accurate.
- Finder F read :405-440 and missed the early return.
- Caveat (not verified further): T-Deck SD restore can load entries on a clockless boot
  (mheard_functions.cpp:1296). Those entries carry saved valid dates, and they also seed the clock
  (time_functions.cpp:131).
