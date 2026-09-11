# T-Deck-14: BLE and UDP-1990 captures owed

The BLE capture that stood here was taken on 2026-09-11 **before** the
broadcast fix and contained four frames this node transmitted to `*` on the
live network. It has been deleted rather than kept: a poisoned baseline is
worse than a missing one, because a later session would diff G1 against it and
read the broadcast as expected behaviour.

`test/golden/verify_captures.py` now fails on any committed capture containing
an own-originated broadcast, so this cannot recur silently.

Owed for this node, both needing it powered and on USB:

- BLE golden, with the corrected corpus (no message sends) and
  `verify_no_broadcast` active — the other three nodes are done and clean.
- UDP-1990 golden (test plan H6/H7) — the other three are done.

The node was not reachable when the other three were re-captured: no USB port,
no answer at 192.168.68.71, not advertising over BLE.
