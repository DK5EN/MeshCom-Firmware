---------------------------- MODULE extqueue ----------------------------
\* FORMAL-01: the deferred sendExtern ring (concurrency map F2-14).  Two slots, one writer, one reader.
\*
\* Code anchors (src/extudp_functions.cpp unless noted):
\*   MAX_EXTERN_QUEUE 2 :93, slot struct with atomic<bool> used :104-110, externQueueWrite plain int :115
\*   writer queueExtern :1088-1102 (entry = &externQueue[externQueueWrite] :1090, memcpy :1098,
\*     metadata :1099-1101, used.store(true) :1102 region, externQueueWrite advance :1103)
\*     -- it NEVER reads `used`.  Called from OnRxDone lora_functions.cpp:1828.
\*   reader flushExternQueue :1356-1368: for i in 0..1, if used.load(acquire) :1360, sendExtern(...buffer,
\*     buflen...) :1364 (blocking UDP write), used.store(false) :1366.
\*   reader callers: nRF52 loop nrf52_main.cpp:2851, ESP32 loop esp32_main.cpp:5471.
\*
\* Execution contexts (docs/architecture/09-concurrency-map.md section 2, F2-14):
\*   Ctx = "NRF52": OnRxDone runs in the LORA task (nrf52_main.cpp:308-314 rxsOnRxDone) and in the timer
\*     service task (RadioBgIrqProcess); flush runs in loop().  The writer can run between any two reader
\*     steps, in particular while sendExtern blocks.
\*   Ctx = "ESP32": OnRxDone runs in loopTask only (checkRX esp32_main.cpp:3840, plus loraRxParkDrain
\*     esp32_main.cpp:6018-6043, which calls checkRXProcess in a while loop for up to RX_PARK_SLOTS = 4
\*     parked packets, lora_rx_park.h:20).  Flush runs later in the same iteration (:5471).  Writer and
\*     reader never overlap; one loop iteration is: up to Burst writes, then one full flush pass.
\*
\* Frame = id 1..N.  A slot copy is two non-atomic halves (head, tail) so that a torn frame is visible
\* (memcpy :1098 is not atomic against the reader's read of buffer :1364).
EXTENDS Naturals, FiniteSets

CONSTANTS
    Ctx,          \* "NRF52" | "ESP32"
    N,            \* number of frames offered by the radio
    Burst,        \* ESP32 only: max OnRxDone calls per loop iteration before the flush pass
    Fix,          \* "none" (today) | "check" (writer drops+counts when used) | "checklate" (check after the copy; mutation)
    CountDrops,   \* writer counts a drop (FALSE = mutation: silent drop)
    ClearUsed     \* reader clears used after the send (FALSE = mutation: duplicates)

Slots == 0..1
Frames == 1..N

VARIABLES used, head, tail, wr, nxt, wpc, ws, wid, wcnt,
          fpc, fi, fh, ft, sentOK, torn, dropped, phase, passDone

vars == <<used, head, tail, wr, nxt, wpc, ws, wid, wcnt, fpc, fi, fh, ft, sentOK, torn, dropped, phase, passDone>>

Init == /\ used = [s \in Slots |-> FALSE]
        /\ head = [s \in Slots |-> 0]
        /\ tail = [s \in Slots |-> 0]
        /\ wr = 0 /\ nxt = 1 /\ wpc = "idle" /\ ws = 0 /\ wid = 0 /\ wcnt = 0
        /\ fpc = "idle" /\ fi = 0 /\ fh = 0 /\ ft = 0
        /\ sentOK = [i \in Frames |-> 0]
        /\ torn = FALSE
        /\ dropped = {}
        /\ phase = "rx" /\ passDone = FALSE

WriterMayStart == IF Ctx = "ESP32" THEN phase = "rx" /\ wcnt < Burst ELSE TRUE
WriterMayStep  == IF Ctx = "ESP32" THEN phase = "rx" ELSE TRUE
ReaderMayStart == IF Ctx = "ESP32" THEN phase = "fl" /\ ~passDone ELSE TRUE
ReaderMayStep  == IF Ctx = "ESP32" THEN phase = "fl" ELSE TRUE

Drop(id) == dropped' = IF CountDrops THEN dropped \cup {id} ELSE dropped

\* queueExtern entry: externQueueWrite read once (:1090); Fix="check": used.load before touching the slot.
WStart == /\ wpc = "idle" /\ nxt <= N /\ WriterMayStart
          /\ ws' = wr /\ wid' = nxt /\ nxt' = nxt + 1 /\ wcnt' = wcnt + 1
          /\ IF Fix = "check" /\ used[wr]
                THEN /\ Drop(nxt) /\ wpc' = "idle"
                ELSE /\ dropped' = dropped /\ wpc' = "w1"
          /\ UNCHANGED <<used, head, tail, wr, fpc, fi, fh, ft, sentOK, torn, phase, passDone>>
\* memcpy first half :1098
W1 == /\ wpc = "w1" /\ WriterMayStep
      /\ head' = [head EXCEPT ![ws] = wid] /\ wpc' = "w2"
      /\ UNCHANGED <<used, tail, wr, nxt, ws, wid, wcnt, fpc, fi, fh, ft, sentOK, torn, dropped, phase, passDone>>
\* memcpy second half + metadata :1098-1101
W2 == /\ wpc = "w2" /\ WriterMayStep
      /\ tail' = [tail EXCEPT ![ws] = wid] /\ wpc' = "w3"
      /\ UNCHANGED <<used, head, wr, nxt, ws, wid, wcnt, fpc, fi, fh, ft, sentOK, torn, dropped, phase, passDone>>
\* used.store(true) :1102 (Fix="checklate": the check happens only here, after the copy: mutation)
W3 == /\ wpc = "w3" /\ WriterMayStep
      /\ IF Fix = "checklate" /\ used[ws]
            THEN /\ Drop(wid) /\ wpc' = "idle" /\ used' = used
            ELSE /\ used' = [used EXCEPT ![ws] = TRUE] /\ wpc' = "w4" /\ dropped' = dropped
      /\ UNCHANGED <<head, tail, wr, nxt, ws, wid, wcnt, fpc, fi, fh, ft, sentOK, torn, phase, passDone>>
\* externQueueWrite = (externQueueWrite + 1) % 2 :1103
W4 == /\ wpc = "w4" /\ WriterMayStep
      /\ wr' = (wr + 1) % 2 /\ wpc' = "idle"
      /\ UNCHANGED <<used, head, tail, nxt, ws, wid, wcnt, fpc, fi, fh, ft, sentOK, torn, dropped, phase, passDone>>

\* flushExternQueue loop entry :1358
FStart == /\ fpc = "idle" /\ ReaderMayStart
          /\ fpc' = "chk" /\ fi' = 0
          /\ UNCHANGED <<used, head, tail, wr, nxt, wpc, ws, wid, wcnt, fh, ft, sentOK, torn, dropped, phase, passDone>>
\* used.load(acquire) :1360
FChk == /\ fpc = "chk" /\ ReaderMayStep
        /\ IF used[fi] THEN /\ fpc' = "rdh" /\ fi' = fi /\ passDone' = passDone
           ELSE IF fi = 1 THEN /\ fpc' = "idle" /\ fi' = 0 /\ passDone' = TRUE
                ELSE /\ fpc' = "chk" /\ fi' = fi + 1 /\ passDone' = passDone
        /\ UNCHANGED <<used, head, tail, wr, nxt, wpc, ws, wid, wcnt, fh, ft, sentOK, torn, dropped, phase>>
\* sendExtern reads buffer (first / second half) :1364
FRdH == /\ fpc = "rdh" /\ ReaderMayStep
        /\ fh' = head[fi] /\ fpc' = "rdt"
        /\ UNCHANGED <<used, head, tail, wr, nxt, wpc, ws, wid, wcnt, fi, ft, sentOK, torn, dropped, phase, passDone>>
FRdT == /\ fpc = "rdt" /\ ReaderMayStep
        /\ ft' = tail[fi] /\ fpc' = "snd"
        /\ UNCHANGED <<used, head, tail, wr, nxt, wpc, ws, wid, wcnt, fi, fh, sentOK, torn, dropped, phase, passDone>>
\* the datagram leaves: intact only if both halves are the same frame
FSnd == /\ fpc = "snd" /\ ReaderMayStep
        /\ IF fh = ft /\ fh \in Frames
              THEN /\ sentOK' = [sentOK EXCEPT ![fh] = @ + 1] /\ torn' = torn
              ELSE /\ torn' = TRUE /\ sentOK' = sentOK
        /\ fpc' = "clr"
        /\ UNCHANGED <<used, head, tail, wr, nxt, wpc, ws, wid, wcnt, fi, fh, ft, dropped, phase, passDone>>
\* used.store(false) :1366, loop increment
FClr == /\ fpc = "clr" /\ ReaderMayStep
        /\ used' = IF ClearUsed THEN [used EXCEPT ![fi] = FALSE] ELSE used
        /\ IF fi = 1 THEN /\ fpc' = "idle" /\ fi' = 0 /\ passDone' = TRUE
           ELSE /\ fpc' = "chk" /\ fi' = fi + 1 /\ passDone' = passDone
        /\ UNCHANGED <<head, tail, wr, nxt, wpc, ws, wid, wcnt, fh, ft, sentOK, torn, dropped, phase>>

\* ESP32 loop iteration: rx part (writes), then flush part
ToFlush == /\ Ctx = "ESP32" /\ phase = "rx" /\ wpc = "idle" /\ fpc = "idle"
           /\ phase' = "fl" /\ passDone' = FALSE
           /\ UNCHANGED <<used, head, tail, wr, nxt, wpc, ws, wid, wcnt, fpc, fi, fh, ft, sentOK, torn, dropped>>
ToRx == /\ Ctx = "ESP32" /\ phase = "fl" /\ fpc = "idle" /\ passDone
        /\ phase' = "rx" /\ wcnt' = 0
        /\ UNCHANGED <<used, head, tail, wr, nxt, wpc, ws, wid, fpc, fi, fh, ft, sentOK, torn, dropped, passDone>>

Next == WStart \/ W1 \/ W2 \/ W3 \/ W4 \/ FStart \/ FChk \/ FRdH \/ FRdT \/ FSnd \/ FClr \/ ToFlush \/ ToRx
Spec == Init /\ [][Next]_vars

TypeOK == /\ used \in [Slots -> BOOLEAN] /\ head \in [Slots -> 0..N] /\ tail \in [Slots -> 0..N]
          /\ wr \in Slots /\ nxt \in 1..(N + 1) /\ wcnt \in 0..N
          /\ sentOK \in [Frames -> 0..2] /\ dropped \subseteq Frames
          /\ wpc \in {"idle", "w1", "w2", "w3", "w4"} /\ fpc \in {"idle", "chk", "rdh", "rdt", "snd", "clr"}

Done == nxt > N /\ wpc = "idle" /\ fpc = "idle" /\ \A s \in Slots : ~used[s]

\* P1: a frame that reaches the wire is never a splice of two frames.
NoTorn == ~torn
\* P2: no frame is sent twice.
NoDup == \A i \in Frames : sentOK[i] <= 1
\* P3: once everything has settled, every accepted frame was uploaded exactly once or counted as dropped.
NoSilentDrop == Done => \A i \in Frames : sentOK[i] = 1 \/ i \in dropped
=============================================================================
