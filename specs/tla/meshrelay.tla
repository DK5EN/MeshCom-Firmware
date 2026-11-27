---------------------------- MODULE meshrelay ----------------------------
\* FORMAL-02 wave 5: relay, dedup and ACK across several nodes with asymmetric links.
\* Question: what does a loud-but-deaf node (high receiver noise, normal TX) do, and which relay rule keeps it
\* from dominating?  Input: docs/asymmetric-nodes-20261127.md (Q6), docs/campaign-formal-02-20261127.md (wave 5).
\*
\* Firmware described (abstracted, all in time ticks; 1 tick ~ 1.5 s, TTLs are cfg constants, not the real 90/300 s):
\*   src/lora_functions.cpp  relay guard ~:2515/:2528/:2581 (hop nibble > 0, own call not in path), enqueue :2623,
\*                           ACK generation by the destination, rx_ack_fwd (ACK relay back), retry ladder :3335-3440
\*   src/dedup_functions.cpp is_new_packet(), ring of MAX_DEDUP_RING ids, oldest evicted (FIFO)
\*   src/txring_functions.cpp getNextTxSlot (lowest priority number first, FIFO within a class)
\*   src/csma_timing.h + configuration_global.h:513-534  base per priority + jitter, re-armed on EVERY received frame,
\*                           TTL per entry (ACK 90 s, relay 300 s)
\*   NBR case A/B (txring_functions.cpp:404-480, only with --nbrrelay on) is NOT modelled; case A is the constant
\*   OwnBase/RelayBase hold, the cancel of case B is the candidate rule Cand=2.
\* Abstractions:
\*   - one data frame id 1 from Src to Dst (+ optional retry copy id 2 with a fresh id, like pnRetryId), ACK ids 3/4,
\*     background frames 5.. (positions, prio 4, they only fill the dedup ring and re-arm holds).
\*   - a link is directed: Link(s,r) = RSSI at r of s; r decodes iff Link - Noise[r] >= SnrMin (Q6 "a link works if").
\*   - no collisions and no extra CRC loss: every tick each receiver decodes the STRONGEST audible sender (capture),
\*     ms-level offsets inside one tick are not resolved; a transmitting node, and a node whose co-located partner
\*     transmits (CoPairs, half duplex / wideband blocking), receives nothing in that tick.
\*   - CSMA is the hold only: entry becomes sendable at max(own due, last RX + base + jitter); no carrier sense of
\*     a transmission in the same tick. Jitter 0..J ticks is nondeterministic (adversarial).
\*   - relays are fire and forget (no retry at relays, analysis Q4); retry only at the originator, once, if no ACK.
\*   - the TX ring has one slot per msg id and node (dedup makes a second copy unreachable except after eviction).
EXTENDS Integers, Sequences, FiniteSets, TLC

CONSTANTS
  TMax,          \* last tick
  Dsz,           \* dedup ring size per node (MAX_DEDUP_RING, scaled down)
  H0,            \* hop count of an originated frame
  J,             \* jitter 0..J ticks (35 ms slot x 10 in firmware)
  AckBase, OwnBase, RelayBase, PosBase,   \* holds in ticks (3000/3000/4500/5500 ms)
  AckTtl, RelayTtl,                       \* entry TTL in ticks (RING_TTL_ACK_MS, relay TTL)
  Src, Dst,
  LinkTab,       \* set of <<s, r, rssi>>: s transmits, r hears it at rssi (absent = no link)
  Noise,         \* tuple: receiver noise level per node (dBm)
  SnrMin,        \* decode threshold SNR (dB)
  NoiseRef, PenDiv, DeafGap,   \* candidate penalty / property: noise above NoiseRef, per PenDiv dB one tick
  CoPairs,       \* set of <<a,b>> co-located pairs (both directions listed)
  BgSrc, BgTicks, BgHop,       \* background frames: sender, ticks (sequence), hop count
  RetryAt,       \* tick of the originator retry, -1 = none
  Cand,          \* 0 today; 1 deaf penalty (relay hold grows with own noise); 2 penalty + cancel on heard copy
  MutNoDedup,    \* mutation: relay guard ignores the dedup ring
  MutNoHopDec    \* mutation: relay does not decrement the hop count

Nodes == 1..Len(Noise)
Ids == 1..(4 + Len(BgTicks))
IsData(i) == i \in 1..2
IsAck(i)  == i \in 3..4
AckOf(i)  == i + 2
Prio(i)   == IF IsAck(i) THEN 1 ELSE IF IsData(i) THEN 3 ELSE 4
Target(i) == IF IsData(i) THEN Dst ELSE IF IsAck(i) THEN Src ELSE 0
Base(i, rel) == IF IsAck(i) THEN AckBase ELSE IF IsData(i) THEN (IF rel THEN RelayBase ELSE OwnBase) ELSE PosBase
Ttl(i) == IF IsAck(i) THEN AckTtl ELSE RelayTtl
NoEnt == [due |-> -1, hop |-> 0, birth |-> 0, rel |-> FALSE, dup |-> FALSE]
Max(a, b) == IF a > b THEN a ELSE b

Link(s, r) == IF \E x \in LinkTab : x[1] = s /\ x[2] = r
              THEN (CHOOSE x \in LinkTab : x[1] = s /\ x[2] = r)[3] ELSE -200
Heard(r, s) == Link(s, r) - Noise[r] >= SnrMin
Pen(n) == IF Cand >= 1 THEN Max(0, Noise[n] - NoiseRef) \div PenDiv ELSE 0

VARIABLES t, q, ring, relTx, totTx, evEver, acked, retried, bgN, vDeaf, vRedund
vars == <<t, q, ring, relTx, totTx, evEver, acked, retried, bgN, vDeaf, vRedund>>

RSet(r) == {r[k] : k \in 1..Len(r)}
RPush(r, id) == IF id \in RSet(r) THEN r
                ELSE IF Len(r) = Dsz THEN Append(Tail(r), id) ELSE Append(r, id)
Zero == [n \in Nodes |-> [i \in Ids |-> 0]]

Init ==
  /\ t = 0
  /\ q = [n \in Nodes |-> [i \in Ids |->
        IF n = Src /\ i = 1 THEN [due |-> 0, hop |-> H0, birth |-> 0, rel |-> FALSE, dup |-> FALSE] ELSE NoEnt]]
  /\ ring = [n \in Nodes |-> IF n = Src THEN <<1>> ELSE <<>>]
  /\ relTx = Zero /\ totTx = Zero
  /\ evEver = [n \in Nodes |-> [i \in Ids |-> FALSE]]
  /\ acked = FALSE /\ retried = FALSE /\ bgN = 0
  /\ vDeaf = FALSE /\ vRedund = FALSE

\* bookkeeping when the ring of n changes from o to w; cnt = tx counter before
RelAfter(n, o, w, i, cnt) == IF (i \in RSet(o)) # (i \in RSet(w)) THEN 0 ELSE cnt
EvAfter(n, o, w, i) == evEver[n][i] \/ (i \in RSet(o) /\ i \notin RSet(w))

OrigPending ==
  \/ (RetryAt >= 0 /\ t = RetryAt /\ ~retried)
  \/ (bgN < Len(BgTicks) /\ t = BgTicks[bgN + 1])

\* originator retry: fresh id 2, only if no ACK yet (retry ladder, lora_functions.cpp:3372)
OrigRetry ==
  /\ RetryAt >= 0 /\ t = RetryAt /\ ~retried
  /\ retried' = TRUE
  /\ LET w == RPush(ring[Src], 2) IN
     /\ IF acked THEN UNCHANGED <<q, ring, relTx, evEver>>
        ELSE /\ q' = [q EXCEPT ![Src][2] = [due |-> t, hop |-> H0, birth |-> t, rel |-> FALSE, dup |-> FALSE]]
             /\ ring' = [ring EXCEPT ![Src] = w]
             /\ relTx' = [relTx EXCEPT ![Src] = [i \in Ids |-> RelAfter(Src, ring[Src], w, i, relTx[Src][i])]]
             /\ evEver' = [evEver EXCEPT ![Src] = [i \in Ids |-> EvAfter(Src, ring[Src], w, i)]]
  /\ UNCHANGED <<t, totTx, acked, bgN, vDeaf, vRedund>>

OrigBg ==
  /\ bgN < Len(BgTicks) /\ t = BgTicks[bgN + 1]
  /\ LET id == 5 + bgN
         w  == RPush(ring[BgSrc], id) IN
     /\ q' = [q EXCEPT ![BgSrc][id] = [due |-> t, hop |-> BgHop, birth |-> t, rel |-> FALSE, dup |-> FALSE]]
     /\ ring' = [ring EXCEPT ![BgSrc] = w]
     /\ relTx' = [relTx EXCEPT ![BgSrc] = [i \in Ids |-> RelAfter(BgSrc, ring[BgSrc], w, i, relTx[BgSrc][i])]]
     /\ evEver' = [evEver EXCEPT ![BgSrc] = [i \in Ids |-> EvAfter(BgSrc, ring[BgSrc], w, i)]]
  /\ bgN' = bgN + 1
  /\ UNCHANGED <<t, totTx, acked, retried, vDeaf, vRedund>>

Tick ==
  /\ t < TMax
  /\ ~OrigPending
  /\ \E jit \in [Nodes -> 0..J] :
     LET Exp(n, i)  == q[n][i].due >= 0 /\ t - q[n][i].birth > Ttl(i)
         DueSet(n)  == {i \in Ids : q[n][i].due >= 0 /\ ~Exp(n, i) /\ q[n][i].due <= t}
         Key(n, i)  == (Prio(i) * 64 + q[n][i].birth) * 8 + i
         S          == {n \in Nodes : DueSet(n) # {}}
         Pick(n)    == CHOOSE i \in DueSet(n) : \A j \in DueSet(n) : Key(n, i) <= Key(n, j)
         Aud(r)     == {s \in S : Heard(r, s)}
         Blocked(r) == \E s \in S : <<r, s>> \in CoPairs
         Recv(r)    == r \notin S /\ ~Blocked(r) /\ Aud(r) # {}
         Best(r)    == CHOOSE s \in Aud(r) : \A u \in Aud(r) : Link(s, r) >= Link(u, r)
         \* propery witnesses, evaluated on the pre-tick state
         BadRed     == \E d \in S : q[d][Pick(d)].rel /\ q[d][Pick(d)].dup
         BadDeaf    == \E d \in S : q[d][Pick(d)].rel /\
                          \E b \in Nodes : b # d /\ Noise[b] <= Noise[d] - DeafGap
                            /\ q[b][Pick(d)].rel /\ q[b][Pick(d)].due >= 0 /\ ~Exp(b, Pick(d))
                            /\ ~(b \in S /\ Pick(b) = Pick(d))
         \* per node result
         Q1(n)      == [i \in Ids |-> IF (n \in S /\ Pick(n) = i) \/ Exp(n, i) THEN NoEnt ELSE q[n][i]]
         Fid(n)     == Pick(Best(n))
         Fhop(n)    == q[Best(n)][Fid(n)].hop
         Dup(n)     == Recv(n) /\ Fid(n) \in RSet(ring[n])
         DoRelay(n) == Recv(n) /\ (~Dup(n) \/ MutNoDedup) /\ Fhop(n) > 0 /\ n # Target(Fid(n))
         DoAck(n)   == Recv(n) /\ ~Dup(n) /\ IsData(Fid(n)) /\ n = Dst
         Rearm(n, e, i) == IF e.due >= 0
              THEN [e EXCEPT !.due = Max(e.due, t + Base(i, e.rel) + jit[n] + (IF e.rel THEN Pen(n) ELSE 0))]
              ELSE e
         Cancel(n, i) == Dup(n) /\ Cand = 2 /\ i = Fid(n) /\ Q1(n)[i].rel /\ Q1(n)[i].due >= 0
         Q2(n)      == IF ~Recv(n) THEN Q1(n) ELSE
              [i \in Ids |-> IF Cancel(n, i) THEN NoEnt
                             ELSE LET e == Rearm(n, Q1(n)[i], i) IN
                                  IF Dup(n) /\ i = Fid(n) /\ e.due >= 0 THEN [e EXCEPT !.dup = TRUE] ELSE e]
         Q3(n)      == [i \in Ids |->
              IF DoRelay(n) /\ i = Fid(n) /\ Q2(n)[i].due < 0
                THEN [due |-> t + Base(i, TRUE) + jit[n] + Pen(n), hop |-> IF MutNoHopDec THEN Fhop(n) ELSE Fhop(n) - 1,
                      birth |-> t, rel |-> TRUE, dup |-> FALSE]
              ELSE IF DoAck(n) /\ i = AckOf(Fid(n))
                THEN [due |-> t + AckBase + jit[n], hop |-> H0, birth |-> t, rel |-> FALSE, dup |-> FALSE]
              ELSE Q2(n)[i]]
         Ring2(n)   == IF Recv(n) /\ ~Dup(n)
                       THEN IF DoAck(n) THEN RPush(RPush(ring[n], Fid(n)), AckOf(Fid(n))) ELSE RPush(ring[n], Fid(n))
                       ELSE ring[n]
         Sent(n, i) == IF n \in S /\ Pick(n) = i /\ q[n][i].rel THEN 1 ELSE 0
     IN
     /\ q' = [n \in Nodes |-> Q3(n)]
     /\ ring' = [n \in Nodes |-> Ring2(n)]
     /\ relTx' = [n \in Nodes |-> [i \in Ids |-> RelAfter(n, ring[n], Ring2(n), i, relTx[n][i] + Sent(n, i))]]
     /\ totTx' = [n \in Nodes |-> [i \in Ids |-> totTx[n][i] + Sent(n, i)]]
     /\ evEver' = [n \in Nodes |-> [i \in Ids |-> EvAfter(n, ring[n], Ring2(n), i)]]
     /\ acked' = (acked \/ (Recv(Src) /\ IsAck(Fid(Src))))
     /\ vDeaf' = (vDeaf \/ BadDeaf)
     /\ vRedund' = (vRedund \/ BadRed)
     /\ t' = t + 1
     /\ UNCHANGED <<retried, bgN>>

Next == Tick \/ OrigRetry \/ OrigBg
Spec == Init /\ [][Next]_vars

\* ---------------------------------------------------------------- properties
EntryT == [due: -1..(TMax + 20), hop: 0..H0, birth: 0..TMax, rel: BOOLEAN, dup: BOOLEAN]
TypeOK ==
  /\ t \in 0..TMax
  /\ \A n \in Nodes : \A i \in Ids : q[n][i] \in EntryT /\ relTx[n][i] \in Nat /\ totTx[n][i] \in Nat /\ evEver[n][i] \in BOOLEAN
  /\ \A n \in Nodes : Len(ring[n]) <= Dsz /\ \A k \in 1..Len(ring[n]) : ring[n][k] \in Ids
  /\ acked \in BOOLEAN /\ vDeaf \in BOOLEAN /\ vRedund \in BOOLEAN

\* an id is relayed at most once while it is in the node's dedup ring (is_new_packet guard)
NoRelayLoop == \A n \in Nodes : \A i \in Ids : i \in RSet(ring[n]) => relTx[n][i] <= 1
\* a second relay of the same id needs an eviction in between (the dedup window)
DedupWindow == \A n \in Nodes : \A i \in Ids : totTx[n][i] >= 2 => evEver[n][i]
\* witness: no node ever relays an id twice (expected to fail: shows when the window re-opens)
NeverRelayTwice == \A n \in Nodes : \A i \in Ids : totTx[n][i] <= 1
\* every relayed copy has fewer hops than an originated frame (hop decrement)
HopBudget == \A n \in Nodes : \A i \in Ids : q[n][i].rel => q[n][i].hop < H0
\* the sender holds its ACK at the horizon (TTL and re-arm decide)
AckDelivery == t = TMax => acked
\* a node with noise >= DeafGap above another holder B never relays an id while B still holds it
DeafNotFirst == ~vDeaf
\* a relay is not sent after a copy of the same id was heard while it was queued
RedundantRelayBound == ~vRedund

\* ---------------------------------------------------------------- scenarios (cfg overrides)
\* nodes: 1 Src, 2 D (deaf: noise -90, 28 dB above the others, normal TX), 3 B (normal), 4 Dst.
\* decode needs rssi >= noise + SnrMin(-17): quiet node -101 dBm, D only -73 dBm. D hears only strong neighbours.
NoiseDeaf == <<-118, -90, -118, -118>>
\* D <-> Src and D <-> B close (-70, D hears them), B <-> Src, B <-> Dst symmetric (-100), D -> Dst (-95) but Dst -> D
\* (-100) is below D's threshold: D is loud, not heard back.
TabDeaf == {<<1,2,-70>>, <<2,1,-70>>, <<1,3,-100>>, <<3,1,-100>>, <<2,3,-70>>, <<3,2,-70>>,
            <<2,4,-95>>, <<4,2,-100>>, <<3,4,-100>>, <<4,3,-100>>}
\* chain Src - D - Dst, all links symmetric and decodable (D still deaf to anything weaker than -73 dBm); node 3 is a
\* position talker next to D (it is heard by D, hears nobody, so it is the source of the re-arming frames).
TabChain == {<<1,2,-70>>, <<2,1,-70>>, <<2,4,-70>>, <<4,2,-70>>, <<3,2,-70>>}
\* all four nodes quiet, same links as TabDeaf (control: asymmetry from the links alone)
NoiseQuiet == <<-118, -118, -118, -118>>
\* DB0TAW-14 / -13: B and D on one tower
CoDB == {<<2,3>>, <<3,2>>}
NoCo == {}
SnrEdge == -17
NoiseQuietRef == -118
NoRetry == -1
BgNone == <<>>
BgAck == <<8, 10, 12>>
BgFlood == <<2, 3>>
=============================================================================
