# Console command golden, Heltec-93 (test plan D2-V / step H3)

Captured 2026-09-11. 432 commands driven over both transports:

| Transport | Answered | Timing             |
| --------- | -------: | ------------------ |
| USB       |  431/432 | quiet 0.35s cap 4s |
| TCP 2323  |  407/432 | quiet 1.2s cap 8s  |

## NOT YET A COMPARISON BASELINE

The two captures differ in 233 of 432 commands, and essentially none of that is
firmware behaviour. **A bench node receiving live mesh traffic cannot produce a
byte-comparable console golden**, and no amount of filtering fixes it:

- 54 of the 233 become identical once node chatter is removed line-by-line.
- Most of the rest are chatter landing **inside** a line rather than beside it:
  `--postime abc` comes back as `ostime abc`, `--posshot` as `-posshot`,
  `--setlog 1` as `log 1`. The async line is filtered out and what remains is
  a mutilated echo. A line-based filter cannot repair that, and a cleverer one
  would start hiding real differences.
- The rest is `[MESH]`, `MH-LoRa`, sensor readings and a node reboot mid-run —
  a prefix deny-list will never be complete, because any text can arrive.

Two differences that looked like genuine transport asymmetries were measured and were
not:

- **50 commands "silent" on 2323.** Re-driving exactly those 50 with a wider
  window answered all 50. The net console batches and delivers slowly; the USB
  timing was cutting its answers short. Fixed by per-transport defaults, and
  every capture header now records the timing it used.
- **An extra `START CHECK:` line on 2323.** It is gated on `bDisplayCont &&
bDisplayInfo` (`command_functions.cpp:181`) — runtime debug flags the script
  itself toggles, not the transport.

## What has to change before this is usable

Capture with the radio quiet — mesh and gateway off, and ideally no antenna —
so the node emits almost nothing unsolicited. That is already the standing
practice for `--injectraw` (project memory `injectraw-extudp-bench-recipe`:
"mesh/gateway off first") and it was simply not applied here.

Until then these two files are evidence that the driver works end to end and a
record of what the ladder answers, not a baseline to diff G1 against.
