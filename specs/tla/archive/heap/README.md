# TLA+ models of the ESP32-S3 internal heap around Auto Update (OTA-LOWHEAP-01)

> [!IMPORTANT]
> **Archived 2026-11-23, not part of `run_tlc.sh --all`.** Review: `docs/formal-review-20261123.md`.
>
> - `heap.tla` is retired. Its properties H1 to H8 hold by construction. The eight rules stay valid as
>   requirements and become checks on real allocation traces (BACKLOG `FORMAL-LINK`).
> - `heap_frag.tla` is frozen. It models the Auto Update gate with two TLS record buffers from the heap. The code
>   now serves the records from the reserved 32 KB buffer. The findings below explain why the old gate failed.
> - Line anchors below are those of 2026-11-22 and no longer match `src/esp32/fw_update_net.cpp`.
> - Run one by hand: `specs/tla/run_tlc.sh archive/heap/heap_frag_f2.cfg`. The results table below is the last run.

Goal: find bugs, not prove tautologies. Every property that holds has a mutation cfg (a deliberately broken
design or constant) that makes TLC report a violation; both runs are in the table below.

## Specs

| file            | models                                                                                                    |
| --------------- | --------------------------------------------------------------------------------------------------------- |
| `heap.tla`      | Ownership only (no placement): one AU job with Fail at every step, reboot at any time, idle-task handover |
| `heap_frag.tla` | Fragmentation: 24 cells x 4 KB, today's design (`Static = FALSE`) and the static reservation variant      |
| `run_tlc.sh`    | TLC / SANY wrapper (`--all` regenerates the table and `traces/`)                                          |

`heap.tla` checks H1 (one owner per live block), H2 (job end leaves nothing), H3 (every allocation freed or permanent,
liveness), H4 (permanent set bounded `K` and stops changing; the first-session object is the parameter `FirstPerm`),
H5 (`AuFail` is enabled at every AU step incl. begin/gate/tls/body/erase/nvs/digest/crc/dry-run; stall, timeout, toobig
are all "fail at body"), H6 (handover to the idle task in one step), H7 (shed hold and loop-stopped services always
undone, incl. the restart path `Restart = TRUE`), H8 (reboot resets RAM, keeps nvs and the browser/phone side).

`heap_frag.tla` checks F1 (gate decision backed by largest block and probe), F2 (once the gate passed no allocation of
the job fails), F3 (F2 with a reservation token taken at the gate), F4 (peak budget), F5 (no pin between sessions),
F6 (dry run fits when it starts), F7 / LQ (a persistent tab or phone / a quiet environment must not block the update
forever). The static variant adds `NoOverlap` (dry run never overlaps TLS), `PoolOne` (at most one user of the global
mbedTLS hook), `NoRefuse` (gate never refuses), and F2 under a Chaos environment.

## Abstraction

- Heap = `N = 24` cells of 4 KB (96 KB). Sizes round up: record buffer 16717 B = 5 cells, stack 12288 B = 3, dict 32 KB
  = 8, DryMem ~11 KB = 3, handshake arena 2 x 2, staging 1, 2 x `FwJsonScan` 1, SHA bounce 1, lwIP pbufs 1, permanent
  object 1 (the 0.5 KB object poisons a whole cell: deliberately pessimistic).
- Placement `Policy`: `any` (every fitting run; over-approximates TLSF: a proof carries over, a counterexample is a
  candidate to confirm with `--heapgap`), `edge` (lowest or highest fitting run: subset of any-fit, counterexamples stay
  valid, proofs are weaker), `first` (lowest, deterministic).
- Actors: AU job (alloc steps one pc each, so every other actor interleaves between them), web tabs (2 cells each, up to 2),
  TIME_WAIT pcbs (1 cell, purged by the gate shed `twPurge` :673-697), phone (2 x 1 cell, BLE never paused on S3),
  console (1 cell), idle task (frees the finished job's stack), environment churn bounded by `ChurnMax` (2), `MaxReboots` 1.
- Gate = `heapGateEval` :701-722: largest block >= `FWNET_MIN_BLOCK` (5 cells) and `twoRecBufProbe` :652-654 (2 + 5 + 5
  cells at once, all freed at once), shed (`twPurge`), settle, second evaluation, else `lowheap`. Nothing is held between
  the probe and the allocations (:843-845, :607).
- Modelled platform: S3 without PSRAM (Heltec V3 class), `AU_BLE_PAUSE 0` (fw_update_net.h:240), `FWNET_MIN_FREE 0` (:130-135).
- Static variant: cells 1..19 are a reserved pool (stack, staging, scan, both record buffers, handshake arena at fixed slots;
  dict and DryMem overlay the TLS arena); cells 20..24 are the general region (web, phone, console, lwIP, SHA bounce,
  permanent object). Admission rule: an environment allocation is admitted only while `FreeGeneral - size >= R` (R = 2).
  The console's mbedTLS use goes through the pool (hook is global, :313) behind a lock. Chaos = alloc/free any single general
  cell under the same rule.
- Not modelled: PSRAM boards, the classic-ESP32 BLE pause, the probe's counter restore (:663-667, erases other tasks' alloc
  failures), WiFi driver buffers, time. Fairness: weak on AU steps, idle, TIME_WAIT expiry, serving, console release; strong on
  the loop task (`heap.tla`) and optionally on the AU gate (`FairLock`); none on tabs/phone/console leaving, Chaos, Reboot.

## Run

```
specs/tla/run_tlc.sh --sany heap_frag.tla            # parse check
specs/tla/run_tlc.sh heap_frag.tla heap_frag_f2.cfg  # one model
TRACE_OUT=/tmp/t.txt specs/tla/run_tlc.sh heap_frag.tla heap_frag_f2.cfg   # keep the compacted counterexample
specs/tla/run_tlc.sh --all                           # everything, ~6 min, writes traces/, prints the table
```

Env: `TLA_JAR` (default `~/.local/share/tla/tla2tools.jar`), `JAVA` (default `/opt/homebrew/opt/openjdk/bin/java`),
`TLC_WORKERS`, `TLC_HEAP`. A cfg marker `\* tlc: expect-fail` means a violation is the expected result (exit 0 when TLC
finds one, exit 1 when it passes: a vacuous check), `\* tlc: nodeadlock` passes `-deadlock` (terminal states are by bound).
Exit codes: 0 all as expected, 1 unexpected result, 2 tool or spec error. Heap traces are compacted to one char per cell
(`.` free, `S` stack, `g` staging, `c` scan, `1 2` record buffers, `h H` handshake, `a` SHA, `l` lwIP, `P` permanent,
`D d` dict/DryMem, `W` web, `t` TIME_WAIT, `b` phone, `n` console, `m` console TLS, `x` chaos).

Constants for the exhaustive runs: `N 24`, `ChurnMax 2` (3 does not finish in 5 min), `MaxReboots 1`, `Webs {w1,w2}`,
`Tws {x1,x2}`, `R 2`, `PoolN 19`; `heap.tla`: `MaxJobs 0` (unbounded job sequence), `MaxReboots 1`. Exhaustive any-fit runs with
churn do not finish, so any-fit is used where the violation is found early (F2) or the environment is switched off; the
other rows say `first` or `edge`. A TLC bug (Java 27, `ArrayIndexOutOfBounds` while printing a very long liveness
counterexample) is worked around with `EnvChurn = FALSE` in `heap_mut_permcycle_h4.cfg`. "Distinct states" of a violated
run is the number found until the violation, not the full space.

## Findings (counterexamples in `traces/<cfg>.txt`, heap at the last state)

Today's design:

- F2 any-fit, `traces/heap_frag_f2.txt` (7 states, quiet environment): the gate passes with the stack in the middle
  (`.g....c....SSS..11111...`); staging, scan and record buffer 1 land so that record buffer 2 finds no 5-cell run. The probe
  frees everything at once (:657-661) and nothing is held until the allocations (:843-845, :607). Needs an allocator free to
  place anywhere; confirm with `--heapgap`. With `edge` or `first` a quiet heap is fine (`heap_frag_lq*.cfg` hold).
- F2 first-fit, `traces/heap_frag_f2_first.txt` (15 states): two tabs (4 cells) plus the 22-cell session peak fill the 24 cells;
  the permanent-object alloc fails after the gate passed (`WWWWSSSgc1111122222hhHHa`). The gate looks at 39.6 KB
  (:701-722, `FWNET_MIN_FREE` 0 :130-135) while the session peaks at 22 cells; the failure surfaces as `lowheap_tls` (:558-580).
- F3: the token must also survive the redirect and the dry run. Token = record buffers + staging
  (`heap_frag_f3_token_mut_partial`): fails at handshake (:607). Token = every TLS-phase block (21 cells, `f3_token_tls`):
  a tab or phone takes the freed hole at the redirect (new session, :607) or after TLS close, and the next alloc fails.
  Token = everything incl. dict + DryMem needs 30 cells > 24 (`reach_token_all`: no job ever runs). A reservation that works
  is the static one.
- F4 (`traces/heap_frag_f4.txt`): session peak 22 cells + worst-case others (2 tabs 4, 2 TIME_WAIT 2, phone 2, console 1 = 9) = 31 > 24,
  so safety is never by budget, only by luck, shedding and the gate.
- F5 (edge-fit, `traces/heap_frag_f5_edge.txt`, 43 states, quiet environment): the permanent object sits at cell 19; the stack of
  job 2 lands at the top end (largest block 18), the stack of job 3 at the bottom end (largest block 15). The largest block shrinks
  with nothing else changed because the allocator may place the stack on either side of the pin. First-fit holds (`heap_frag_f5`);
  a block leaked by the job is caught (`f5_mut_pin`).
- F6 (first-fit, `traces/heap_frag_f6.txt`, 20 states): after the TLS client closes (:1385-1403) a tab and a phone take the freed
  cells, no 8-cell run is left for the dict (:1137-1139) and the job ends in `nomem` (:1142) after erase and full download. In a
  quiet environment it holds (`f6_edge`).
- F7 (`traces/heap_frag_f7.txt`, lasso, not minimal): one tab and the phone stay, a job fills 24 of 24 cells and fails, forever.
  Reproduces the field behaviour: a connected browser or phone blocks AU even after a reboot (both reconnect first).

Static reservation:

- F2 under Chaos with R = 2 holds (38787 states), as do NoRefuse, NoOverlap, PoolOne, the actors environment and F7. R = 1 is not
  enough (`mut_r1`): perm object + SHA bounce need 2 free general cells in the first session. `BugNoReserve`, `BugLateClose` and
  `BugNoPoolLock` each break F2.
- The pool lock needs fairness (`static_f7_unfair`, 9-state lasso): a console that keeps starting TLS starves the AU gate; the
  lock must queue or prefer AU (`FairLock`, strong fairness).
- Cost: 19 cells (76 KB) pool + R 2 leaves 3 of 24 cells for web, phone, console; with one tab open the admission rule refuses
  the phone's 2 cells. Pool size and R come from measured numbers; the model only shows the shape.

## F8: update from the web GUI (operator 2026-11-23)

The update is started in the browser, so the requesting tab is open by construction; "close the tab" is no remedy.

- F8 (one tab, nothing else): holds today and static.
- F8p (one tab plus a phone on BLE, the field report): violated today (`traces/heap_frag_f8p.txt`), holds static with
  PoolN = 19.
- F8s (with tab and phone persistent the phone stays served): holds today, violated static with PoolN = 19: the
  admission rule starves the phone instead of the update.
- Non-vacuity: `heap_frag_static_reach_tabphone.cfg` shows tab + served phone + successful update is reachable.
- Sweep PoolN 15..19 x R 1..3 (static, 24 cells): no pair satisfies F8p and F8s together. PoolN <= 18 starves the
  update, PoolN = 19 starves the phone. In this model the budget TLS peak + tab + phone exceeds the free heap:
  static reservation alone only chooses who loses; the TLS footprint must shrink or the heap must grow. Cell sizes
  are model values (1 cell = 4 KB) until calibrated against the 2026-11-23 bench probes.

## Advisor rules checked against the code

- F1's "largest free >= 13 cells" is not what the code asks: the gate needs one block >= 17000 B (5 cells) and the probe of
  6144 + 2 x 16717 B as three separate blocks (:643-654, :701-722); modelled that way.
- The dry-run allocation is checked (:1142 `refuse("nomem")`), but late (after erase and download) and as two blocks (dict 32 KB +
  DryMem ~11 KB), not one 44 KB block (:1120-1122).
- The restart-on-new-stack path (:1536-1545) exists only with `AU_BLE_PAUSE` (classic ESP32); on the S3 it is not compiled in.
  `heap_restart.cfg` covers it as a classic-ESP32 check.
- SHA bounce buffer comment is at fw_update.h:1135 (not 1134).

## Results

`holds / holds` = property holds as intended; `violated / violated` = expected counterexample (design finding or mutation).
A row is a mutation when its cfg name contains `mut` (or `nopre`, `unfair`, `lq_mut`); `reach_*` rows are reachability probes (a job really
completes: `violated` there means the probe found a completed job; `reach_token_all` holds because no job ever runs).

| spec          | cfg                                       | checks                                        | expected | result   | distinct states | time |
| ------------- | ----------------------------------------- | --------------------------------------------- | -------- | -------- | --------------- | ---- |
| heap.tla      | heap.cfg                                  | H1 H2 H4Bound H6Idle H3 H4Stable H6Step H7 H8 | holds    | holds    | 39296           | 17s  |
| heap.tla      | heap_restart.cfg                          | H1 H2 H4Bound H6Idle H3 H4Stable H6Step H7 H8 | holds    | holds    | 91008           | 47s  |
| heap.tla      | heap_mut_k0_h4.cfg                        | H4Bound                                       | violated | violated | 217             | 1s   |
| heap.tla      | heap_mut_leak_h2.cfg                      | H2                                            | violated | violated | 5952            | 0s   |
| heap.tla      | heap_mut_leak_h3.cfg                      | H3                                            | violated | violated | 30686           | 6s   |
| heap.tla      | heap_mut_norestore_h7.cfg                 | H7                                            | violated | violated | 41600           | 3s   |
| heap.tla      | heap_mut_permcycle_h4.cfg                 | H4Stable                                      | violated | violated | 528             | 1s   |
| heap.tla      | heap_mut_permevery_h4.cfg                 | H4Bound                                       | violated | violated | 8378            | 0s   |
| heap.tla      | heap_mut_rebootkeep_h8.cfg                | H8                                            | violated | violated | 102             | 1s   |
| heap.tla      | heap_mut_restart_norestore_h7.cfg         | H7                                            | violated | violated | 71727           | 4s   |
| heap.tla      | heap_mut_stackleak_h2.cfg                 | H2                                            | violated | violated | 150             | 1s   |
| heap.tla      | heap_mut_stackleak_h3.cfg                 | H3                                            | violated | violated | 16320           | 3s   |
| heap.tla      | heap_mut_stackleak_h6.cfg                 | H6Idle                                        | violated | violated | 175             | 1s   |
| heap.tla      | heap_mut_twostep_h1.cfg                   | H1                                            | violated | violated | 187             | 0s   |
| heap.tla      | heap_mut_twostep_h6.cfg                   | H6Step                                        | violated | violated | 173             | 1s   |
| heap_frag.tla | heap_frag.cfg                             | F1                                            | holds    | holds    | 382281          | 7s   |
| heap_frag.tla | heap_frag_f1_edge.cfg                     | F1                                            | holds    | holds    | 141779          | 2s   |
| heap_frag.tla | heap_frag_f1_nopre.cfg                    | F1                                            | violated | violated | 48231           | 1s   |
| heap_frag.tla | heap_frag_f2_first.cfg                    | F2                                            | violated | violated | 6584            | 1s   |
| heap_frag.tla | heap_frag_f2.cfg                          | F2                                            | violated | violated | 1001400         | 12s  |
| heap_frag.tla | heap_frag_f3_token_all.cfg                | F2                                            | holds    | holds    | 1454            | 1s   |
| heap_frag.tla | heap_frag_f3_token_mut_partial.cfg        | F2                                            | violated | violated | 3664            | 1s   |
| heap_frag.tla | heap_frag_f3_token_tls.cfg                | F2                                            | violated | violated | 5216            | 1s   |
| heap_frag.tla | heap_frag_f4.cfg                          | F4                                            | violated | violated | 1712            | 1s   |
| heap_frag.tla | heap_frag_f5_edge.cfg                     | F5                                            | violated | violated | 149488          | 2s   |
| heap_frag.tla | heap_frag_f5_mut_pin.cfg                  | F5                                            | violated | violated | 156301          | 6s   |
| heap_frag.tla | heap_frag_f5.cfg                          | F5                                            | holds    | holds    | 606383          | 13s  |
| heap_frag.tla | heap_frag_f6_edge.cfg                     | F6                                            | holds    | holds    | 141779          | 2s   |
| heap_frag.tla | heap_frag_f6_mut_boot.cfg                 | F6                                            | violated | violated | 56              | 1s   |
| heap_frag.tla | heap_frag_f6.cfg                          | F6                                            | violated | violated | 22437           | 1s   |
| heap_frag.tla | heap_frag_f7.cfg                          | F7                                            | violated | violated | 154436          | 15s  |
| heap_frag.tla | heap_frag_lq_edge.cfg                     | LQ                                            | holds    | holds    | 45627           | 4s   |
| heap_frag.tla | heap_frag_lq_mut_boot.cfg                 | LQ                                            | violated | violated | 1399            | 1s   |
| heap_frag.tla | heap_frag_lq.cfg                          | LQ                                            | holds    | holds    | 154436          | 15s  |
| heap_frag.tla | heap_frag_reach_static_actors.cfg         | NeverOk                                       | violated | violated | 25602           | 1s   |
| heap_frag.tla | heap_frag_reach_static.cfg                | NeverOk                                       | violated | violated | 8836            | 1s   |
| heap_frag.tla | heap_frag_reach_today.cfg                 | NeverOk                                       | violated | violated | 34835           | 2s   |
| heap_frag.tla | heap_frag_reach_token_all.cfg             | NeverOk                                       | holds    | holds    | 1454            | 1s   |
| heap_frag.tla | heap_frag_reach_token_tls.cfg             | NeverOk                                       | violated | violated | 8556            | 1s   |
| heap_frag.tla | heap_frag_static_actors_mut_noreserve.cfg | F2                                            | violated | violated | 897113          | 3s   |
| heap_frag.tla | heap_frag_static_actors.cfg               | F2 NoRefuse NoOverlap PoolOne                 | holds    | holds    | 118942          | 2s   |
| heap_frag.tla | heap_frag_static_f7_mut_noreserve.cfg     | F7                                            | violated | violated | 189300          | 8s   |
| heap_frag.tla | heap_frag_static_f7_unfair.cfg            | F7                                            | violated | violated | 5985            | 1s   |
| heap_frag.tla | heap_frag_static_f7.cfg                   | F7                                            | holds    | holds    | 5985            | 1s   |
| heap_frag.tla | heap_frag_static_mut_lateclose.cfg        | F2                                            | violated | violated | 6466            | 0s   |
| heap_frag.tla | heap_frag_static_mut_noreserve.cfg        | F2                                            | violated | violated | 673267          | 3s   |
| heap_frag.tla | heap_frag_static_mut_r1.cfg               | F2                                            | violated | violated | 3377            | 1s   |
| heap_frag.tla | heap_frag_static_nogate_mut.cfg           | NoRefuse                                      | violated | violated | 7928532         | 32s  |
| heap_frag.tla | heap_frag_static_overlay_mut.cfg          | NoOverlap                                     | violated | violated | 4377            | 1s   |
| heap_frag.tla | heap_frag_static_overlay.cfg              | NoOverlap                                     | holds    | holds    | 38787           | 1s   |
| heap_frag.tla | heap_frag_static_pool_f2_mut.cfg          | F2                                            | violated | violated | 1492            | 1s   |
| heap_frag.tla | heap_frag_static_pool_mut.cfg             | PoolOne                                       | violated | violated | 309             | 0s   |
| heap_frag.tla | heap_frag_static_pool.cfg                 | PoolOne                                       | holds    | holds    | 38787           | 1s   |
| heap_frag.tla | heap_frag_static.cfg                      | F2 NoRefuse                                   | holds    | holds    | 38787           | 1s   |
