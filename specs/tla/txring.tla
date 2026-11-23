---------------------------- MODULE txring ----------------------------
\* FORMAL-01: TX slot pool (ringBuffer) of the MeshCom firmware, both platforms.
\* One model, two writers on nRF52 (loop task + LORA task via OnRxDone), one writer on ESP32 (all in loopTask).
\*
\* Code anchors (src/):
\*   addTxRingEntryCore txring_functions.cpp:860 (callers: addTxRingEntry :1179, ...Once :1216, ...RmReply :1272)
\*     lock taskENTER_CRITICAL (nRF52 only) :919, exit :1112ff; ESP32 lock-free by design :793-800 (single writer)
\*     slot scan :923-934, eviction :1018-1067, data write memcpy :1089-1092, status byte :1093,
\*     length published LAST :1112
\*   txRingBeginTx :285 (READY/DONE -> INFLIGHT 0x81 :296, stamp re-check, locked on nRF52)
\*   txRingFinishTx :318 (acts only while INFLIGHT; SENT parks own TEXT, else frees; ROLLBACK restores; DROP frees)
\*   txRingSweep :593 (TTL/CET drop, skips INFLIGHT :606, locked on nRF52, loop task every 2 s)
\*   doTX lora_functions.cpp:2974-3322 (pick getNextTxSlot unlocked, BeginTx :2979, Finish :3114-3322)
\*   ACK scan findAndStopRingSlotImpl lora_functions.cpp:405-450 (locked on RAK4630; skips INFLIGHT)
\*
\* Granularity: one action = one atomic region of the real code.  A critical section (nRF52) is modelled as a
\* mutual-exclusion token `cs`: while one process holds it the other takes no step (the scheduler is off).
\* Without the token (ESP32, or BugNoCS) the micro-steps interleave.
EXTENDS Naturals, FiniteSets, Sequences

CONSTANTS
    Platform,          \* "esp32" | "nrf52"
    Slots,             \* ring size (MAX_RING, small)
    LoopIds,           \* frame ids enqueued by the loop task (user frames, ACK forwards)
    LoraIds,           \* frame ids enqueued by the LORA task (relay from OnRxDone); {} on esp32
    HiPrio,            \* ids with priority 1 (more important); every other id has priority 2
    OwnSet,            \* ids that are own TEXT (stored READY, parked as SENT after TX); others stored DONE
    RbMax, SwMax, AkMax, \* bounds on rollbacks, sweep expiries, ACK clears (keeps the model finite)
    BugNoCS,           \* mutation: no critical section on nRF52
    BugLenFirst,       \* mutation: length published before the data
    BugForget,         \* mutation: an error path leaves doTX without txRingFinishTx
    BugEvictInflight,  \* mutation: the evictor does not skip INFLIGHT
    BugSweepInflight,  \* mutation: the sweep does not skip INFLIGHT
    BugAckInflight     \* mutation: the ACK scan does not skip INFLIGHT

Procs   == IF Platform = "esp32" THEN {"loop"} ELSE {"loop", "lora"}
Mine(p) == IF p = "loop" THEN LoopIds ELSE LoraIds
AllIds  == LoopIds \cup LoraIds
SlotSet == 1..Slots
UseCS   == Platform = "nrf52" /\ ~BugNoCS
Prio(i) == IF i \in HiPrio THEN 1 ELSE 2
Own(i)  == i \in OwnSet
InitSt(i) == IF Own(i) THEN "READY" ELSE "DONE"
WP      == {"eLenF", "eWr1", "eWr2", "ePub"}     \* writer has chosen a slot and is not finished with it

VARIABLES ln, d1, d2, st, gen, cs, pc, fid, wsl, wmode, tsl, tstamp, tprev, tfid, r1, r2, tout,
          todo, fate, sentLog, evictCnt, enqDone, evInf, expInf, bud

vars == <<ln, d1, d2, st, gen, cs, pc, fid, wsl, wmode, tsl, tstamp, tprev, tfid, r1, r2, tout,
          todo, fate, sentLog, evictCnt, enqDone, evInf, expInf, bud>>

Init == /\ ln = [s \in SlotSet |-> 0] /\ d1 = [s \in SlotSet |-> 0] /\ d2 = [s \in SlotSet |-> 0]
        /\ st = [s \in SlotSet |-> "DONE"] /\ gen = [s \in SlotSet |-> 0]
        /\ cs = "none" /\ pc = [p \in Procs |-> "idle"] /\ fid = [p \in Procs |-> 0]
        /\ wsl = [p \in Procs |-> 0] /\ wmode = [p \in Procs |-> "none"]
        /\ tsl = 0 /\ tstamp = 0 /\ tprev = "DONE" /\ tfid = 0 /\ r1 = 0 /\ r2 = 0 /\ tout = "none"
        /\ todo = [p \in Procs |-> Mine(p)] /\ fate = [i \in AllIds |-> "new"]
        /\ sentLog = {} /\ evictCnt = 0 /\ enqDone = 0 /\ evInf = FALSE /\ expInf = FALSE
        /\ bud = [rb |-> 0, sw |-> 0, ak |-> 0]

\* a process may step unless the other one sits in a critical section
G(p) == cs \in {"none", p}
Keep(x) == UNCHANGED x

\* ---- Enqueue (addTxRingEntryCore), per process p -------------------------------------------------
EStart(p) == /\ G(p) /\ pc[p] = "idle" /\ todo[p] # {}
             /\ \E i \in todo[p] : fid' = [fid EXCEPT ![p] = i]
             /\ pc' = [pc EXCEPT ![p] = "eLock"]
             /\ UNCHANGED <<ln, d1, d2, st, gen, cs, wsl, wmode, tsl, tstamp, tprev, tfid, r1, r2, tout,
                            todo, fate, sentLog, evictCnt, enqDone, evInf, expInf, bud>>

\* taskENTER_CRITICAL :919 (nRF52 only)
ELock(p) == /\ G(p) /\ pc[p] = "eLock"
            /\ IF UseCS THEN cs = "none" /\ cs' = p ELSE cs' = cs
            /\ pc' = [pc EXCEPT ![p] = "ePick"]
            /\ UNCHANGED <<ln, d1, d2, st, gen, fid, wsl, wmode, tsl, tstamp, tprev, tfid, r1, r2, tout,
                           todo, fate, sentLog, evictCnt, enqDone, evInf, expInf, bud>>

\* slot scan :923-934 plus pool-full eviction :1018-1067 (victim = worst priority, strictly worse than the
\* newcomer; oldest-first among equals is over-approximated by "any"); INFLIGHT never evictable :1022
EPick(p) ==
    /\ G(p) /\ pc[p] = "ePick"
    /\ LET free == {s \in SlotSet : ln[s] = 0}
           cand == {s \in SlotSet : st[s] # "INFLIGHT" \/ BugEvictInflight}
           worstP == CHOOSE m \in {Prio(ln[s]) : s \in cand} : \A x \in {Prio(ln[s]) : s \in cand} : m >= x
           NextPc == IF BugLenFirst THEN "eLenF" ELSE "eWr1"
       IN \/ /\ free # {}
             /\ LET w == CHOOSE s \in free : \A t \in free : s <= t IN
                  /\ wsl' = [wsl EXCEPT ![p] = w] /\ wmode' = [wmode EXCEPT ![p] = "w"]
             /\ pc' = [pc EXCEPT ![p] = NextPc]
             /\ UNCHANGED <<fate, evictCnt, evInf>>
          \/ /\ free = {} /\ cand # {} /\ worstP > Prio(fid[p])
             /\ \E v \in {s \in cand : Prio(ln[s]) = worstP} :
                  /\ wsl' = [wsl EXCEPT ![p] = v] /\ wmode' = [wmode EXCEPT ![p] = "w"]
                  /\ fate' = IF fate[ln[v]] = "queued" THEN [fate EXCEPT ![ln[v]] = "evicted"] ELSE fate
                  /\ evictCnt' = evictCnt + 1
                  /\ evInf' = (evInf \/ st[v] = "INFLIGHT")
             /\ pc' = [pc EXCEPT ![p] = NextPc]
          \/ /\ free = {} /\ ~(cand # {} /\ worstP > Prio(fid[p]))     \* RING_DROP_NEW :1069ff
             /\ wmode' = [wmode EXCEPT ![p] = "drop"]
             /\ fate' = [fate EXCEPT ![fid[p]] = "dropped"]
             /\ pc' = [pc EXCEPT ![p] = "eUnlock"]
             /\ UNCHANGED <<wsl, evictCnt, evInf>>
    /\ UNCHANGED <<ln, d1, d2, st, gen, cs, fid, tsl, tstamp, tprev, tfid, r1, r2, tout, todo, sentLog,
                   enqDone, expInf, bud>>

\* mutation BugLenFirst: length (and status) before the data
ELenF(p) == /\ G(p) /\ pc[p] = "eLenF"
            /\ ln' = [ln EXCEPT ![wsl[p]] = fid[p]]
            /\ st' = [st EXCEPT ![wsl[p]] = InitSt(fid[p])]
            /\ gen' = [gen EXCEPT ![wsl[p]] = @ + 1]
            /\ fate' = [fate EXCEPT ![fid[p]] = "queued"]
            /\ pc' = [pc EXCEPT ![p] = "eWr1"]
            /\ UNCHANGED <<d1, d2, cs, fid, wsl, wmode, tsl, tstamp, tprev, tfid, r1, r2, tout, todo,
                           sentLog, evictCnt, enqDone, evInf, expInf, bud>>

\* memcpy of the frame :1089-1092 (two cells stand for "the bytes"; a splice shows as d1 # d2)
EWr1(p) == /\ G(p) /\ pc[p] = "eWr1"
           /\ d1' = [d1 EXCEPT ![wsl[p]] = fid[p]]
           /\ pc' = [pc EXCEPT ![p] = "eWr2"]
           /\ UNCHANGED <<ln, d2, st, gen, cs, fid, wsl, wmode, tsl, tstamp, tprev, tfid, r1, r2, tout, todo,
                          fate, sentLog, evictCnt, enqDone, evInf, expInf, bud>>

\* second half of the copy, then status byte :1093 (and a new enqueue stamp :1100)
EWr2(p) == /\ G(p) /\ pc[p] = "eWr2"
           /\ d2' = [d2 EXCEPT ![wsl[p]] = fid[p]]
           /\ IF BugLenFirst THEN UNCHANGED <<st, gen>>
              ELSE /\ st' = [st EXCEPT ![wsl[p]] = InitSt(fid[p])]
                   /\ gen' = [gen EXCEPT ![wsl[p]] = @ + 1]
           /\ pc' = [pc EXCEPT ![p] = IF BugLenFirst THEN "eUnlock" ELSE "ePub"]
           /\ UNCHANGED <<ln, d1, cs, fid, wsl, wmode, tsl, tstamp, tprev, tfid, r1, r2, tout, todo, fate,
                          sentLog, evictCnt, enqDone, evInf, expInf, bud>>

\* length last :1112
EPub(p) == /\ G(p) /\ pc[p] = "ePub"
           /\ ln' = [ln EXCEPT ![wsl[p]] = fid[p]]
           /\ fate' = [fate EXCEPT ![fid[p]] = "queued"]
           /\ pc' = [pc EXCEPT ![p] = "eUnlock"]
           /\ UNCHANGED <<d1, d2, st, gen, cs, fid, wsl, wmode, tsl, tstamp, tprev, tfid, r1, r2, tout, todo,
                          sentLog, evictCnt, enqDone, evInf, expInf, bud>>

\* taskEXIT_CRITICAL
EUnlock(p) == /\ G(p) /\ pc[p] = "eUnlock"
              /\ cs' = IF UseCS THEN "none" ELSE cs
              /\ pc' = [pc EXCEPT ![p] = "idle"] /\ wsl' = [wsl EXCEPT ![p] = 0]
              /\ wmode' = [wmode EXCEPT ![p] = "none"]
              /\ todo' = [todo EXCEPT ![p] = @ \ {fid[p]}]
              /\ enqDone' = enqDone + 1
              /\ UNCHANGED <<ln, d1, d2, st, gen, fid, tsl, tstamp, tprev, tfid, r1, r2, tout, fate, sentLog,
                             evictCnt, evInf, expInf, bud>>

\* ---- doTX (loop task only) ---------------------------------------------------------------------
\* getNextTxSlot: unlocked pick of a READY/DONE slot, remembers the enqueue stamp (:2974)
TPick == /\ G("loop") /\ pc["loop"] = "idle"
         /\ \E s \in SlotSet :
              /\ ln[s] > 0 /\ st[s] \in {"READY", "DONE"}
              /\ tsl' = s /\ tstamp' = gen[s]
         /\ pc' = [pc EXCEPT !["loop"] = IF UseCS THEN "tBeg" ELSE "tChk"]
         /\ UNCHANGED <<ln, d1, d2, st, gen, cs, fid, wsl, wmode, tprev, tfid, r1, r2, tout, todo, fate,
                        sentLog, evictCnt, enqDone, evInf, expInf, bud>>

TReset == tsl' = 0 /\ tstamp' = 0 /\ tprev' = "DONE" /\ tfid' = 0 /\ r1' = 0 /\ r2' = 0 /\ tout' = "none"

Claimable(s) == ln[s] > 0 /\ st[s] \in {"READY", "DONE"} /\ gen[s] = tstamp

\* txRingBeginTx :285-304, check and set in one critical section (nRF52)
TBeg == /\ G("loop") /\ pc["loop"] = "tBeg" /\ cs = "none"
        /\ IF Claimable(tsl)
           THEN /\ tprev' = st[tsl] /\ tfid' = ln[tsl]
                /\ st' = [st EXCEPT ![tsl] = "INFLIGHT"]
                /\ pc' = [pc EXCEPT !["loop"] = "tR1"]
                /\ UNCHANGED <<tsl, tstamp, r1, r2, tout>>
           ELSE /\ pc' = [pc EXCEPT !["loop"] = "idle"] /\ TReset /\ UNCHANGED st
        /\ UNCHANGED <<ln, d1, d2, gen, cs, fid, wsl, wmode, todo, fate, sentLog,
                       evictCnt, enqDone, evInf, expInf, bud>>

\* same, without a critical section (ESP32: only one task; BugNoCS): check and set are two steps
TChk == /\ G("loop") /\ pc["loop"] = "tChk"
        /\ IF Claimable(tsl)
           THEN /\ tprev' = st[tsl] /\ tfid' = ln[tsl] /\ pc' = [pc EXCEPT !["loop"] = "tSet"]
                /\ UNCHANGED <<tsl, tstamp, r1, r2, tout>>
           ELSE /\ pc' = [pc EXCEPT !["loop"] = "idle"] /\ TReset
        /\ UNCHANGED <<ln, d1, d2, st, gen, cs, fid, wsl, wmode, todo, fate,
                       sentLog, evictCnt, enqDone, evInf, expInf, bud>>

TSet == /\ G("loop") /\ pc["loop"] = "tSet"
        /\ st' = [st EXCEPT ![tsl] = "INFLIGHT"]
        /\ pc' = [pc EXCEPT !["loop"] = "tR1"]
        /\ UNCHANGED <<ln, d1, d2, gen, cs, fid, wsl, wmode, tsl, tstamp, tprev, tfid, r1, r2, tout, todo,
                       fate, sentLog, evictCnt, enqDone, evInf, expInf, bud>>

\* frame bytes copied into lora_tx_buffer (read of the slot)
TR1 == /\ G("loop") /\ pc["loop"] = "tR1" /\ r1' = d1[tsl] /\ pc' = [pc EXCEPT !["loop"] = "tR2"]
       /\ UNCHANGED <<ln, d1, d2, st, gen, cs, fid, wsl, wmode, tsl, tstamp, tprev, tfid, r2, tout, todo,
                      fate, sentLog, evictCnt, enqDone, evInf, expInf, bud>>
TR2 == /\ G("loop") /\ pc["loop"] = "tR2" /\ r2' = d2[tsl] /\ pc' = [pc EXCEPT !["loop"] = "tOut"]
       /\ UNCHANGED <<ln, d1, d2, st, gen, cs, fid, wsl, wmode, tsl, tstamp, tprev, tfid, r1, tout, todo,
                      fate, sentLog, evictCnt, enqDone, evInf, expInf, bud>>

\* outcome of doTX: radio send :3217/:3297, rollback :3155-3264, refuse :3114/:3322, and (mutation) a path
\* that returns without txRingFinishTx
TOut == /\ G("loop") /\ pc["loop"] = "tOut"
        /\ \/ /\ tout' = "sent" /\ pc' = [pc EXCEPT !["loop"] = "tFin"]
              /\ UNCHANGED <<tsl, tstamp, tprev, tfid, r1, r2>>
              /\ sentLog' = sentLog \cup {[id |-> tfid, a |-> r1, b |-> r2]}
              /\ fate' = IF fate[tfid] = "queued" THEN [fate EXCEPT ![tfid] = "sent"] ELSE fate
           \/ /\ tout' = "rollback" /\ bud.rb < RbMax /\ pc' = [pc EXCEPT !["loop"] = "tFin"]
              /\ UNCHANGED <<sentLog, fate, tsl, tstamp, tprev, tfid, r1, r2>>
           \/ /\ tout' = "drop" /\ pc' = [pc EXCEPT !["loop"] = "tFin"]
              /\ UNCHANGED <<sentLog, fate, tsl, tstamp, tprev, tfid, r1, r2>>
           \/ /\ BugForget /\ TReset /\ pc' = [pc EXCEPT !["loop"] = "idle"]
              /\ UNCHANGED <<sentLog, fate>>
        /\ UNCHANGED <<ln, d1, d2, st, gen, cs, fid, wsl, wmode, todo,
                       evictCnt, enqDone, evInf, expInf, bud>>

\* txRingFinishTx :318-345: acts only while INFLIGHT
TFin == /\ G("loop") /\ pc["loop"] = "tFin"
        /\ IF st[tsl] = "INFLIGHT"
           THEN IF tout = "rollback"
                THEN /\ st' = [st EXCEPT ![tsl] = tprev] /\ UNCHANGED <<ln, fate>>
                ELSE IF tout = "sent" /\ tprev # "DONE" /\ Own(d1[tsl])
                     THEN /\ st' = [st EXCEPT ![tsl] = "SENT"] /\ UNCHANGED <<ln, fate>>
                     ELSE /\ st' = [st EXCEPT ![tsl] = "DONE"] /\ ln' = [ln EXCEPT ![tsl] = 0]
                          /\ fate' = IF tout = "drop" /\ fate[tfid] = "queued"
                                     THEN [fate EXCEPT ![tfid] = "refused"] ELSE fate
           ELSE UNCHANGED <<st, ln, fate>>
        /\ bud' = IF tout = "rollback" THEN [bud EXCEPT !.rb = @ + 1] ELSE bud
        /\ pc' = [pc EXCEPT !["loop"] = "idle"] /\ TReset
        /\ UNCHANGED <<d1, d2, gen, cs, fid, wsl, wmode, todo,
                       sentLog, evictCnt, enqDone, evInf, expInf>>

\* ---- txRingSweep :593-660, loop task, locked on nRF52 (atomic); TTL abstracted to "any time" -----------
Sweep == /\ G("loop") /\ pc["loop"] = "idle" /\ bud.sw < SwMax
         /\ \E s \in SlotSet :
              /\ ln[s] > 0 /\ (st[s] # "INFLIGHT" \/ BugSweepInflight)
              /\ ln' = [ln EXCEPT ![s] = 0] /\ st' = [st EXCEPT ![s] = "DONE"]
              /\ fate' = IF fate[ln[s]] = "queued" THEN [fate EXCEPT ![ln[s]] = "expired"] ELSE fate
              /\ expInf' = (expInf \/ st[s] = "INFLIGHT")
         /\ bud' = [bud EXCEPT !.sw = @ + 1]
         /\ UNCHANGED <<d1, d2, gen, cs, pc, fid, wsl, wmode, tsl, tstamp, tprev, tfid, r1, r2, tout, todo,
                        sentLog, evictCnt, enqDone, evInf>>

\* ---- ACK scan findAndStopRingSlotImpl lora_functions.cpp:405-450: frees a parked SENT slot (not INFLIGHT) --
Ack(p) == /\ G(p) /\ pc[p] = "idle" /\ bud.ak < AkMax
          /\ \E s \in SlotSet :
               /\ ln[s] > 0 /\ (st[s] = "SENT" \/ (BugAckInflight /\ st[s] = "INFLIGHT"))
               /\ ln' = [ln EXCEPT ![s] = 0] /\ st' = [st EXCEPT ![s] = "DONE"]
               /\ expInf' = (expInf \/ st[s] = "INFLIGHT")
          /\ bud' = [bud EXCEPT !.ak = @ + 1]
          /\ UNCHANGED <<d1, d2, gen, cs, pc, fid, wsl, wmode, tsl, tstamp, tprev, tfid, r1, r2, tout, todo,
                         fate, sentLog, evictCnt, enqDone, evInf>>

EnqStep(p) == EStart(p) \/ ELock(p) \/ EPick(p) \/ ELenF(p) \/ EWr1(p) \/ EWr2(p) \/ EPub(p) \/ EUnlock(p)
LoopTx == TPick \/ TBeg \/ TChk \/ TSet \/ TR1 \/ TR2 \/ TOut \/ TFin
Core(p) == EnqStep(p) \/ (p = "loop" /\ LoopTx)
Adv(p)  == (p = "loop" /\ Sweep) \/ Ack(p)
Next == \E p \in Procs : Core(p) \/ Adv(p)

\* Fairness: every process that is not idle keeps stepping (weak fairness per process); the adversarial
\* actions (sweep, ACK, rollback) are bounded by SwMax/AkMax/RbMax, so after the budgets are used up only
\* progress steps remain.  A real scheduler does not starve the loop task or the LORA task.
Spec == Init /\ [][Next]_vars /\ \A p \in Procs : WF_vars(Core(p))

\* ---- Properties --------------------------------------------------------------------------------
TypeOK == /\ \A s \in SlotSet : ln[s] \in {0} \cup AllIds /\ d1[s] \in {0} \cup AllIds /\ d2[s] \in {0} \cup AllIds
                                /\ st[s] \in {"READY", "DONE", "SENT", "INFLIGHT"}
          /\ cs \in {"none"} \cup Procs
          /\ \A i \in AllIds : fate[i] \in {"new", "queued", "sent", "expired", "evicted", "dropped", "refused"}
          /\ evictCnt \in Nat /\ enqDone \in Nat

\* no two in-flight enqueues own one slot
NoSharedSlot == \A p, q \in Procs : p # q =>
                  ~(pc[p] \in WP /\ pc[q] \in WP /\ wsl[p] = wsl[q])

\* a frame that went to the radio has all its bytes from one enqueue
NoSplice == \A x \in sentLog : x.a = x.id /\ x.b = x.id

\* a published slot (length > 0, no writer inside it) holds one complete frame
SlotConsistent == \A s \in SlotSet :
                    (ln[s] > 0 /\ ~(\E p \in Procs : pc[p] \in WP /\ wsl[p] = s)) => (d1[s] = ln[s] /\ d2[s] = ln[s])

\* every INFLIGHT slot is owned by the running doTX
InflightOwned == \A s \in SlotSet : st[s] = "INFLIGHT" /\ ln[s] > 0 =>
                    (pc["loop"] \in {"tR1", "tR2", "tOut", "tFin", "tSet"} /\ tsl = s)

\* a frame counted as queued sits in a slot
QueuedInSlot == \A i \in AllIds : fate[i] = "queued" => \E s \in SlotSet : ln[s] = i

NoEvictInflight == ~evInf
\* neither the sweep nor an ACK scan frees a slot while doTX holds it INFLIGHT
NoExpireInflight == ~expInf
EvictBounded == evictCnt <= enqDone

\* reachability probes (cfgs txring_reach_*, expect-fail: the violation is the witness that the path exists)
ReachEvict == evictCnt = 0
ReachParkedEvict == ~(\E i \in AllIds : fate[i] = "sent") \/ evictCnt = 0
ReachExpire == ~(\E i \in AllIds : fate[i] = "expired")
ReachDropNew == ~(\E i \in AllIds : fate[i] = "dropped")
ReachSpliceWindow == ~(\E p \in Procs : pc[p] = "ePub" /\ pc["loop"] = "tR1")

\* liveness
NoStuckInflight == \A s \in SlotSet : (st[s] = "INFLIGHT") ~> (st[s] # "INFLIGHT")
\* each accepted frame ends as sent, expired, evicted (counted) or refused (logged): nothing stays queued
NoSilentLoss == \A i \in AllIds : (fate[i] = "queued") ~> (fate[i] \notin {"new", "queued"})
AllEnqueued == \A i \in AllIds : <>(fate[i] # "new")
=============================================================================
