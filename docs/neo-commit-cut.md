# neo: from 182 campaign commits to the chapter cut

Written 2026-09-19. Companion to `docs/neo-campaign.md`. This document proposes
the commit cut for `fork-neo-test` and `fork-neo` and shows the evidence behind
it. Chapter titles are German because they become the chapter headings of
`docs/CHANGELOG-neo.md`; the analysis around them follows the docs/ house style.

## 1. What the delta actually is

Measured on `c09e22b8` (stage 0, `upstream/dev` merged into `dry-unification`),
over the filter set `src/ lib/ variants/ config/ platformio.ini`:

| quantity                                          | value |
| ------------------------------------------------- | ----- |
| files differing from `upstream/dev`               | 248   |
| of those, touched by the 182 DRY-campaign commits | 243   |
| **residual not from the campaign**                | **5** |

The five: `src/safeboot/ElegantOTA.cpp`, `src/safeboot/ElegantOTA.h`,
`src/safeboot/main.cpp` (TM-49 fail-closed OTA gate, TM-46 stall-watchdog race),
`src/t-deck/tdeck_helpers.cpp` (the `[KBL];set` bench marker) and
`src/idf_component.yml.orig` (a stray file, deleted).

So "neo is upstream/dev plus the DRY campaign" holds, with a five-file footnote.
That footnote matters for one reason: `tdeck_helpers.cpp` carries a bench marker,
so it is a strip candidate for `fork-neo`, not a production change.

## 2. The chapter cut

Every one of the 248 files is assigned to exactly one chapter -- verified
mechanically, 248/248, no file unassigned and none claimed twice. That property
is what makes the projection checkable: each chapter is a path set, and the
closing `git diff` over the union of all path sets must be empty.

| #   | Chapter                               | files | +    | -     | campaign IDs                                   |
| --- | ------------------------------------- | ----- | ---- | ----- | ---------------------------------------------- |
| K01 | Vendor-Ballast entfernen              | 57    | 0    | 18508 | W1                                             |
| K02 | Settings-Store: ein Schema            | 15    | 3493 | 1204  | D1-04, W3, W3c, W7, W7-II                      |
| K03 | BLE-Settings v1                       | 4     | 1075 | 28    | D1-04, R1-02                                   |
| K04 | Kommandotabelle und Setter            | 8     | 1517 | 1232  | D2-01, D2-06/07/09/10, R3-11, TD-16/17, H6-01  |
| K05 | UDP-Rahmen und Gateway-Dienst         | 11    | 2098 | 571   | D1-01, D3-01/02/05, DR-03/14/16, EXT-01, R2-04 |
| K06 | MHeard                                | 3     | 572  | 253   | R2-01, R2-04, R3-12, DR-28                     |
| K07 | Loop-Scheduler und Plattform-Aktionen | 8     | 1154 | 236   | D1-10, R1-04, DISP-01, GRD-01                  |
| K08 | APRS und Textcodec                    | 7     | 519  | 430   | R2-04, D3-01/02/05                             |
| K09 | LoRa-Schicht und Funkparameter        | 7     | 503  | 352   | R2-04, R3-12, D1-10                            |
| K10 | LVGL: ui_common und ein lv_conf       | 35    | 551  | 14990 | R4-01, D1-01, R2-01                            |
| K11 | nRF52-Netzstack                       | 7     | 291  | 1469  | ETH-02, ETH-02b, ETH-03, EXT-02, D1-10         |
| K12 | ESP32-Kern und Peripherie             | 5     | 125  | 428   | R3-03, GRD-01, DISP-01                         |
| K13 | T-Deck                                | 4     | 20   | 14    | R2-04                                          |
| K14 | EXTUDP und Aufzeichnung               | 5     | 232  | 52    | EXT-01, EXT-02, R3-11, D2-09                   |
| K15 | Safeboot und OTA                      | 4     | 238  | 98    | TM-46, TM-49 (outside the campaign)            |
| K16 | Web-GUI                               | 2     | 102  | 70    | R1-04, R2-01, R2-04                            |
| K17 | Instrumentierung und MC_DIAG          | 2     | 82   | 4     | R3-11, D2-09                                   |
| K18 | Build-Konfiguration                   | 64    | 6987 | 619   | W7, W7-II                                      |
| K19 | Testgeruest (nur `fork-neo-test`)     | 446+  | --   | --    | der Beleg der ganzen Kampagne                  |

Two chapters dominate the line counts and both are explainable: K01 is pure
subtraction (vendor fonts, dead platforms), K10's 14990 deletions are the twenty
duplicated `lv_conf.h` copies collapsing into one `config/lv_conf.h`. K18's
+6987 is mostly the 34 `[env:native*]` blocks in `platformio.ini` plus
`lib/tinyxml2`. The native blocks move to K19 and do not reach `fork-neo`;
**`lib/tinyxml2` does travel** -- it is the MEM-04 fix (`95d6fbe6`) worth
5768 B DRAM and 572 B IRAM on `E22_XML-DevKitC`, not ballast. An earlier draft
of this document had that backwards.

The "twenty copies into one" wording needs care in the changelog too: `t_deck`
and `t_deck_plus` still carry their own `lv_conf.h`, and `config/lv_conf.h`
already existed upstream. Twenty **non-LVGL** copies were deleted; twenty copies
were not unified into one. Do not claim a unification that did not happen.

## 3. The problem this analysis found

**Campaign commits cross chapter boundaries, and some cross many.** `R2-04`
(`aprsMessage` carries `text`, not `String`) touches eight of the eighteen
chapters. `ETH-03` touches five. The `D3-01/D3-02/D3-05` wave touches six.

Two consequences, and the second one is the serious one:

**The cut is a re-narration, not a re-ordering.** Chapters are subsystems, not
commits. A cross-cutting item like R2-04 will be named in several chapters'
changelog text while its code lands in whichever chapter owns each path. That is
fine for the changelog and fine for the projection.

**Per-path chapters are not automatically compilable on their own.** The
projection takes the final state of each path. If K04 lands
`command_functions.cpp` using the new `text` type while K06's
`mheard_functions.cpp` still holds upstream's `String` version, that commit does
not build. A type or signature change that crosses chapters forces either an
order (the defining header first) or a merge of the chapters involved.

This is exactly what the per-commit build gate on 8 envs is for. Expect it to
force consolidation: **18 is the upper bound, not the answer.** A realistic
landing zone is 10 to 14 chapters once the cross-cutting items have had their
say. The alternative -- discovering it at the end -- is what the gate exists to
prevent.

## 4. Proposed order

Dependency first, narrative second. Foundations before consumers, deletions
early so later diffs are small.

0. **Projection note**: every chapter is applied with
   `git checkout --no-overlay`, or its deletions silently do not happen. K01 and
   K10 together are 77 of the 248 files and would otherwise commit nothing.
1. **K01 Vendor-Ballast** -- pure subtraction, depends on nothing
2. **K18 Build-Konfiguration** -- the variant and env restructuring the rest
   builds against
3. **K02 Settings-Store** -- defines the schema and the struct everything reads
4. **K03 BLE-Settings v1** -- consumes K02's struct
5. **K05 UDP-Rahmen und Gateway-Dienst** -- transport types
6. **K08 APRS und Textcodec** -- the `text` type that R2-04 pushes outward
7. **K06 MHeard** -- consumes K08's type
8. **K09 LoRa-Schicht** -- consumes K08
9. **K04 Kommandotabelle** -- consumes K02 and K08
10. **K07 Loop-Scheduler** -- consumes nearly everything above
11. **K11 nRF52-Netzstack**
12. **K12 ESP32-Kern**
13. **K10 LVGL / ui_common**
14. **K13 T-Deck**
15. **K14 EXTUDP**
16. **K16 Web-GUI**
17. **K15 Safeboot und OTA**
18. **K17 Instrumentierung** -- second to last, one of the two chapters that do
    not exist on `fork-neo`
19. **K19 Testgeruest** -- `test/` plus the 34 `[env:native*]` blocks, last and
    `fork-neo-test` only. Last because a native env's `build_src_filter` set is
    complete only at the tip; run after an intermediate chapter it fails for
    reasons that have nothing to do with that chapter. This makes
    `platformio.ini` the one file split across two chapters: K18 lands it without
    the native blocks, K19 appends them.

Putting K19 last is deliberate: it is the one chapter `fork-neo` does not
replay.

K17 is no longer such a chapter. `upstream/dev` already carries
`src/instrument.h` (byte-identical) and `src/instrument.cpp` (18 lines apart),
and seven files include it there. K17's real content is those 18 lines plus four
additional includers -- a small fork addition that ships on both branches, not a
chapter to drop. The strip is therefore `test/`, the 34 `[env:native*]` blocks
and the unguarded bench marker at `src/t-deck/tdeck_helpers.cpp:143`; nothing
else, and no source rewrite.

## 5. Advisor pass, 2026-09-19

An independent Fable advisor re-derived the numbers in section 1 and 2 against
the tree: the 248-file count, the 18 chapter file counts, the +19559/-40558 line
totals and the 243/5 split all reconcile exactly. What it refuted was the
procedure around them, not the arithmetic -- see
`docs/neo-campaign.md` section 12 for the full list. The three findings that
changed this document are the `--no-overlay` projection flag, `lib/tinyxml2`
travelling after all, and K19.

## 6. What this does not yet answer

- The final chapter count, which the build gate decides, not this document.
- Whether K05 and K06 can stand apart at all given R2-04.
- Whether K18 has to split further. It already does once, for K19's native env
  blocks; `platformio.ini` is additionally a transformed file on `fork-neo` and a
  copied one on `fork-neo-test`.
- The per-chapter changelog text itself, which is the next deliverable.

## 7. Reproducing this

The classifier lives in the session scratchpad and is not part of the tree. It
reads `git diff --numstat upstream/dev dry-unification` over the filter set,
applies one regex list per chapter, and asserts that every path matches exactly
one chapter. Any future change to the cut should re-run that assertion before
the projection starts -- a path claimed twice, or by nothing, breaks the closing
tree-identity check in a way that is tedious to diagnose afterwards.
