---------------------------- MODULE bytefifo ----------------------------
\* FORMAL-01, F2-5: the byte_fifo ring (src/byte_fifo.{h,cpp}) with one producer, one drain reader and one
\* web-history iterator.  Every BF_LOCK section of the C code is ONE atomic action here.  What the model
\* checks: the reader peeks (gets tail_gen), works outside the lock, then pop_if(gen) -- a frame that was
\* evicted and replaced meanwhile must not be popped, no torn frame is delivered, every pushed frame is
\* delivered once or counted as evicted, the counters stay consistent.
\*
\* Code anchors:
\*   struct byte_fifo.h:17-27   bf_push2 byte_fifo.cpp:73-125 (evict loop :94-111, write :113-122)
\*   bf_peek :128   bf_peek_gen :143   bf_pop_if :159-172   bf_pop :175   bf_iter_begin :188   bf_iter_next :204
\*   BF_LOCK = taskENTER_CRITICAL (nRF52 :10) / portMUX (ESP32 :20)
\*   readers: BLE drain ble_phone_drain.h:182 (peek_gen) .. :200/:228/:262 (pop_if);
\*            UDP drain src/esp32/udp_drain_esp32.cpp:54 (tail_gen), :56 (peek), :187 (tail_gen ==), :188 (bf_pop).
\*
\* Abstractions: capacity C cells (a cell is one byte), frames of 1-2 payload bytes (+1 length byte); one
\* ring, one drain reader (the ring has exactly one consumer by design, byte_fifo.h), one writer thread (the
\* push is atomic, so several writers add no interleaving); the writer pushes at most MaxPush frames;
\* bf_reset/bf_detach/bf_attach are not modelled; 16-bit gen wrap is modelled by GenMod (default large).
EXTENDS Naturals, Sequences, FiniteSets

CONSTANTS
    C,                  \* ring capacity in bytes
    MaxPush,            \* number of frames the producer pushes in total
    MaxIter,            \* number of history iterations (web page)
    GenMod,             \* tail_gen modulus (65536 in the code; small only for the wrap sensitivity cfg)
    UdpStyle,           \* TRUE: reader shaped like udp_drain_esp32.cpp (gen read, then peek, then check, then bf_pop)
    BugPopNoGen,        \* pop_if ignores the generation
    BugNoTailBump,      \* evicting an unread frame does not bump tail_gen (byte_fifo.cpp:104)
    BugNoEvictBump,     \* eviction does not bump evict_gen (byte_fifo.cpp:107)
    BugPeekUnlocked,    \* the peek copies the payload outside the lock, byte by byte
    BugNoLostCount      \* push does not count the unread frames it evicted

VARIABLES
    buf,                \* buf[0..C-1] -> [f, i, l]: frame id, index inside the frame (0 = length byte), payload length
    head, tail, oldest, used, frames, unread, tgen, egen,    \* struct fields (byte_fifo.h:20-26)
    q,                  \* ghost: ids oldest..newest of the frames in the ring
    lenOf, npush, status, sent, lostRet, badPop, tornSent, iterTorn,   \* ghost
    rpc, rgen, rsnap, rid, rpos, rk,                          \* drain reader
    ipc, ipos, ileft, igen, iters                             \* history iterator

vars == <<buf, head, tail, oldest, used, frames, unread, tgen, egen, q, lenOf, npush, status, sent, lostRet,
          badPop, tornSent, iterTorn, rpc, rgen, rsnap, rid, rpos, rk, ipc, ipos, ileft, igen, iters>>

Ids == 1..MaxPush
Empty == [f |-> 0, i |-> 0, l |-> 0]

Size(id) == 1 + lenOf[id]
RECURSIVE SumSz(_, _)
SumSz(s, k) == IF k = 0 THEN 0 ELSE Size(s[k]) + SumSz(s, k - 1)

KSet(need) == {k \in 0..Len(q) : C - (used - SumSz(q, k)) >= need}
KMin(need) == CHOOSE k \in KSet(need) : \A j \in KSet(need) : k <= j

Init ==
    /\ buf = [p \in 0..(C - 1) |-> Empty]
    /\ head = 0 /\ tail = 0 /\ oldest = 0 /\ used = 0 /\ frames = 0 /\ unread = 0 /\ tgen = 0 /\ egen = 0
    /\ q = <<>> /\ lenOf = [x \in Ids |-> 0] /\ npush = 0
    /\ status = [x \in Ids |-> "none"] /\ sent = [x \in Ids |-> 0]
    /\ lostRet = 0 /\ badPop = FALSE /\ tornSent = FALSE /\ iterTorn = FALSE
    /\ rpc = "idle" /\ rgen = 0 /\ rsnap = <<>> /\ rid = 0 /\ rpos = 0 /\ rk = 0
    /\ ipc = "idle" /\ ipos = 0 /\ ileft = 0 /\ igen = 0 /\ iters = 0

\* ---- producer: bf_push2 byte_fifo.cpp:73-125, one lock section --------------------------------------
Push(len) ==
    LET id    == npush + 1
        need  == len + 1
        k     == KMin(need)                 \* evict loop :94-111: oldest frames go until len+1 bytes are free
        nr    == frames - unread            \* already-read frames sit in front of the unread ones
        lostn == IF k > nr THEN k - nr ELSE 0     \* was_unread evictions (:99, :104-107)
        sk    == SumSz(q, k)
        newOld == (oldest + sk) % C
    IN  /\ npush < MaxPush
        /\ need <= C                        \* :88 reject oversize (never with the chosen C)
        /\ oldest' = newOld
        /\ used' = used - sk + need
        /\ frames' = Len(q) - k + 1
        /\ unread' = unread - lostn + 1
        /\ tail' = IF lostn > 0 THEN newOld ELSE tail      \* :103
        /\ tgen' = IF BugNoTailBump THEN tgen ELSE (tgen + lostn) % GenMod   \* :104 tail_gen++ per lost frame
        /\ egen' = IF BugNoEvictBump THEN egen ELSE egen + k                 \* :107 evict_gen++ per eviction
        /\ lostRet' = IF BugNoLostCount THEN lostRet ELSE lostRet + lostn    \* :105 lost++ -> return value
        /\ buf' = [p \in 0..(C - 1) |->
                     IF \E j \in 0..len : p = (head + j) % C
                     THEN [f |-> id, i |-> CHOOSE j \in 0..len : p = (head + j) % C, l |-> len]
                     ELSE buf[p]]                                            \* :113-122 length byte, payload
        /\ head' = (head + need) % C                                         \* :124
        /\ q' = SubSeq(q, k + 1, Len(q)) \o <<id>>
        /\ lenOf' = [lenOf EXCEPT ![id] = len]
        /\ npush' = id
        /\ status' = [x \in Ids |-> IF x = id THEN "unread"
                                    ELSE IF \E j \in (nr + 1)..k : q[j] = x THEN "lost" ELSE status[x]]
        /\ UNCHANGED <<sent, badPop, tornSent, iterTorn, rpc, rgen, rsnap, rid, rpos, rk, ipc, ipos, ileft, igen, iters>>

\* ---- the frame a pop at the current tail would take (ghost) ---------------------------------------
TailId == q[frames - unread + 1]

\* pop: byte_fifo.cpp:164-168 / :180-184 (same body in pop_if and pop)
PopEffect ==
    /\ tail' = (tail + 1 + buf[tail].l) % C
    /\ unread' = unread - 1
    /\ tgen' = (tgen + 1) % GenMod
    /\ status' = [status EXCEPT ![TailId] = "read"]
    /\ badPop' = (badPop \/ TailId /= rid)
    /\ UNCHANGED <<buf, head, oldest, used, frames, egen, q, lenOf, npush, sent, lostRet, tornSent, iterTorn,
                   rsnap, rid, rpos, rk, ipc, ipos, ileft, igen, iters>>

\* ---- drain reader ------------------------------------------------------------------------------
Copy(pos, l) == [j \in 0..l |-> buf[(pos + j) % C]]

\* bf_peek_gen :143-157, frame and gen in ONE lock (BLE drain, ble_phone_drain.h:182)
PeekGen ==
    /\ ~UdpStyle /\ ~BugPeekUnlocked
    /\ rpc = "idle" /\ unread > 0
    /\ rsnap' = Copy(tail, buf[tail].l)
    /\ rgen' = tgen
    /\ rid' = TailId
    /\ rpc' = "work"
    /\ UNCHANGED <<buf, head, tail, oldest, used, frames, unread, tgen, egen, q, lenOf, npush, status, sent, lostRet,
                   badPop, tornSent, iterTorn, rpos, rk, ipc, ipos, ileft, igen, iters>>

\* mutation: the peek takes the lock only for the length byte and then copies cell by cell outside it
PeekStart ==
    /\ ~UdpStyle /\ BugPeekUnlocked
    /\ rpc = "idle" /\ unread > 0
    /\ rpos' = tail /\ rk' = 0 /\ rsnap' = <<>> /\ rgen' = tgen /\ rid' = TailId
    /\ rpc' = "copy"
    /\ UNCHANGED <<buf, head, tail, oldest, used, frames, unread, tgen, egen, q, lenOf, npush, status, sent, lostRet,
                   badPop, tornSent, iterTorn, ipc, ipos, ileft, igen, iters>>

PeekCell ==
    /\ rpc = "copy"
    /\ LET l == IF rk = 0 THEN buf[rpos].l ELSE rsnap[0].l IN
       /\ rsnap' = [j \in 0..rk |-> IF j = rk THEN buf[(rpos + rk) % C] ELSE rsnap[j]]
       /\ IF rk = l THEN rpc' = "work" ELSE rpc' = "copy"
       /\ rk' = IF rk = l THEN 0 ELSE rk + 1
    /\ UNCHANGED <<buf, head, tail, oldest, used, frames, unread, tgen, egen, q, lenOf, npush, status, sent, lostRet,
                   badPop, tornSent, iterTorn, rgen, rid, rpos, ipc, ipos, ileft, igen, iters>>

\* udp_drain_esp32.cpp:54 bf_tail_gen (one lock) -- the peek at :56 is a second lock section
UdpGen ==
    /\ UdpStyle
    /\ rpc = "idle" /\ unread > 0
    /\ rgen' = tgen /\ rpc' = "peekU"
    /\ UNCHANGED <<buf, head, tail, oldest, used, frames, unread, tgen, egen, q, lenOf, npush, status, sent, lostRet,
                   badPop, tornSent, iterTorn, rsnap, rid, rpos, rk, ipc, ipos, ileft, igen, iters>>

UdpPeek ==      \* :56 bf_peek (empty ring: msg_len 0, nothing to send, back to idle)
    /\ UdpStyle /\ rpc = "peekU"
    /\ IF unread > 0
       THEN /\ rsnap' = Copy(tail, buf[tail].l) /\ rid' = TailId /\ rpc' = "work"
       ELSE /\ rsnap' = rsnap /\ rid' = rid /\ rpc' = "idle"
    /\ UNCHANGED <<buf, head, tail, oldest, used, frames, unread, tgen, egen, q, lenOf, npush, status, sent, lostRet,
                   badPop, tornSent, iterTorn, rgen, rpos, rk, ipc, ipos, ileft, igen, iters>>

Consistent(snap, id) == /\ id \in Ids
                        /\ DOMAIN snap = 0..lenOf[id]
                        /\ \A j \in 0..lenOf[id] : snap[j] = [f |-> id, i |-> j, l |-> lenOf[id]]

\* the send happens outside the lock (BLE sink ble_phone_drain.h:236, udpEndRaw :158)
Send ==
    /\ rpc = "work"
    /\ sent' = [sent EXCEPT ![rid] = @ + 1]
    /\ tornSent' = (tornSent \/ ~Consistent(rsnap, rid))
    /\ rpc' = IF UdpStyle THEN "chk" ELSE "pop"
    /\ UNCHANGED <<buf, head, tail, oldest, used, frames, unread, tgen, egen, q, lenOf, npush, status, lostRet,
                   badPop, iterTorn, rgen, rsnap, rid, rpos, rk, ipc, ipos, ileft, igen, iters>>

\* bf_pop_if :159-172: compare and pop under one lock
PopIf ==
    /\ ~UdpStyle /\ rpc = "pop"
    /\ rpc' = "idle"
    /\ IF unread > 0 /\ (BugPopNoGen \/ tgen = rgen)
       THEN PopEffect
       ELSE UNCHANGED <<buf, head, tail, oldest, used, frames, unread, tgen, egen, q, lenOf, npush, status, sent,
                        lostRet, badPop, tornSent, iterTorn, rsnap, rid, rpos, rk, ipc, ipos, ileft, igen, iters>>
    /\ UNCHANGED <<rgen>>

\* udp_drain_esp32.cpp:187 `if (bf_tail_gen(...) == myGen)`: one lock section, the bf_pop at :188 is another
UdpCheck ==
    /\ UdpStyle /\ rpc = "chk"
    /\ rpc' = IF tgen = rgen THEN "pop2" ELSE "idle"
    /\ UNCHANGED <<buf, head, tail, oldest, used, frames, unread, tgen, egen, q, lenOf, npush, status, sent, lostRet,
                   badPop, tornSent, iterTorn, rgen, rsnap, rid, rpos, rk, ipc, ipos, ileft, igen, iters>>

UdpPop ==       \* :188 bf_pop: pops whatever sits at tail now
    /\ UdpStyle /\ rpc = "pop2"
    /\ rpc' = "idle"
    /\ IF unread > 0
       THEN PopEffect
       ELSE UNCHANGED <<buf, head, tail, oldest, used, frames, unread, tgen, egen, q, lenOf, npush, status, sent,
                        lostRet, badPop, tornSent, iterTorn, rsnap, rid, rpos, rk, ipc, ipos, ileft, igen, iters>>
    /\ UNCHANGED <<rgen>>

\* ---- history iterator: bf_iter_begin :188-197, bf_iter_next :204-224 -----------------------------
IterBegin ==
    /\ ipc = "idle" /\ iters < MaxIter
    /\ ipos' = oldest /\ ileft' = frames /\ igen' = egen /\ ipc' = "run" /\ iters' = iters + 1
    /\ UNCHANGED <<buf, head, tail, oldest, used, frames, unread, tgen, egen, q, lenOf, npush, status, sent, lostRet,
                   badPop, tornSent, iterTorn, rpc, rgen, rsnap, rid, rpos, rk>>

FrameOk(pos) ==
    LET c0 == buf[pos] IN
    /\ c0.f \in Ids /\ c0.i = 0
    /\ \A j \in 0..c0.l : buf[(pos + j) % C] = [f |-> c0.f, i |-> j, l |-> c0.l]

IterNext ==
    /\ ipc = "run"
    /\ IF ileft > 0 /\ igen = egen
       THEN /\ iterTorn' = (iterTorn \/ ~FrameOk(ipos))
            /\ ipos' = (ipos + 1 + buf[ipos].l) % C
            /\ ileft' = ileft - 1
            /\ ipc' = IF ileft = 1 THEN "idle" ELSE "run"
       ELSE /\ ipc' = "idle" /\ ileft' = 0 /\ UNCHANGED <<iterTorn, ipos>>
    /\ UNCHANGED <<buf, head, tail, oldest, used, frames, unread, tgen, egen, q, lenOf, npush, status, sent, lostRet,
                   badPop, tornSent, rpc, rgen, rsnap, rid, rpos, rk, igen, iters>>

Next ==
    \/ \E len \in 1..2 : Push(len)
    \/ PeekGen \/ PeekStart \/ PeekCell \/ UdpGen \/ UdpPeek \/ Send \/ PopIf \/ UdpCheck \/ UdpPop
    \/ IterBegin \/ IterNext

Spec == Init /\ [][Next]_vars

\* ---- properties ------------------------------------------------------------------------------------
CountLost == Cardinality({x \in Ids : status[x] = "lost"})

\* counters consistent with the contents; every frame in the ring is intact at its offset
Counters ==
    /\ frames = Len(q) /\ unread <= frames /\ used <= C
    /\ used = SumSz(q, Len(q))
    /\ head = (oldest + used) % C
    /\ tail = (oldest + SumSz(q, frames - unread)) % C
    /\ lostRet = CountLost
    /\ \A n \in 1..Len(q) : \A j \in 0..lenOf[q[n]] :
         buf[(oldest + SumSz(q, n - 1) + j) % C] = [f |-> q[n], i |-> j, l |-> lenOf[q[n]]]

\* every status is explained: unread <=> among the last `unread` frames of the ring
Accounting ==
    \A x \in Ids :
        /\ (x <= npush) <=> (status[x] /= "none")
        /\ (status[x] = "unread") <=> (\E n \in (frames - unread + 1)..frames : q[n] = x)

PopOnlyPeeked == ~badPop             \* pop_if never removes a frame other than the one peeked
NoTornFrame   == ~tornSent           \* the snapshot the reader sends is exactly one frame
NoTornIter    == ~iterTorn           \* every frame the history iterator yields is intact
NoDup         == \A x \in Ids : sent[x] <= 1
NoLoss        == \A x \in Ids : status[x] = "read" => sent[x] >= 1   \* popped means sent
\* at the end (everything pushed, reader idle, ring drained): delivered or counted-evicted
Delivery == (npush = MaxPush /\ rpc = "idle" /\ unread = 0) =>
              \A x \in Ids : sent[x] >= 1 \/ status[x] = "lost"

=============================================================================
