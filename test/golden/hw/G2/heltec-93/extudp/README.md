# EXTUDP golden, Heltec-93 (test plan step H8) -- G2

Captured 2026-09-17 on a stock (non-instrument) build of `dry-unification`
`HEAD`, after `W5`, `W6`, `C4d` and `W7` step 1 had landed. Same procedure as
G0/G1: all 23 corpus objects from `test/golden/corpus/extudp/` sent to UDP 1799
at 3.5 s intervals, everything the node emitted recorded and normalized.

## Result: PASS

```
A 8 corpus responses, 3 relayed, 0 beacons      <- G1
B 8 corpus responses, 1 relayed, 4 beacons      <- G2
corpus responses identical (8)
```

The relayed and beacon counts differ and `compare_extudp.py` declares that a
non-failure by design: relayed frames are live traffic off the air and beacons
fire on their own timers, so neither is deterministic within a capture window.
The comparable surface -- the node's own responses to the corpus -- is
identical, all eight.

## The capture contains one `"dst":"*"`, and it is not ours

```
{"src_type":"lora", "src":"OE1XAR-33,DK5EN-98", "dst":"*", "msg":"{CET}2026-09-17 15:46:37", ...}
```

`src_type` is `lora`: this is somebody else's time broadcast, heard off the air
and forwarded to the EXTUDP peer, which is what an EXTUDP peer is for. Every
frame this node originated (`src_type":"node"`) went to `DK5EN-1` or group
`9999`. A grep for `dst":"*"` in an EXTUDP capture looks alarming and is not,
so it is written down here rather than re-investigated next time.

## Addressing was piloted first, as at G0

Two corpus entries were sent and what went on the air was inspected before the
remaining 21, per the G0 runbook and the standing rule (group 9/9999 or a
direct contact, never `*`).

## A capture was thrown away first, and why

The first attempt at the rak-90 capture recorded two datagrams -- from
**192.168.68.71**, which is Heltec-93. Its `--extudp off` had not taken effect:
the command was sent over a freshly opened CP2102 port, which resets the board,
and the reset swallowed it. The node was still streaming to port 1799 while a
different node's capture was running.

Recording the **source address** per datagram is what caught it; a capture that
only stored payloads would have silently attributed one node's output to
another. This is the cross-contamination `compare_extudp.py` names as GLD-01
gap 2 and cannot check for itself once `normalize.py` has masked the address to
`<ADDR>`. Lesson for the next run: send a shutdown command with `--wait-boot`
and verify with `--info` before arming the next node, never assume it landed.
