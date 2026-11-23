------------------------- MODULE heap_frag -------------------------
\* OTA-LOWHEAP-01 wave 1d: fragmentation model of the ESP32-S3 internal heap around one Auto
\* Update (AU) job.  The heap is N cells of 4 KB (default 24 = 96 KB).  A block occupies a run of
\* cells; an allocation takes ANY free run that is long enough (Policy "any", over-approximates
\* TLSF, so a safety proof carries over to the real allocator and a counterexample is a candidate
\* to confirm with --heapgap on the bench), the lowest or highest one (Policy "edge", a subset of
\* any-fit: counterexamples stay valid, proofs are weaker) or the lowest one (Policy "first", a
\* deterministic sanity check).  Block sizes are rounded UP to whole cells (a 16717 B record buffer is 5 cells).
\*
\* Static = FALSE : today's design.  Static = TRUE: static reservation variant (AU blocks sit in
\* a reserved pool region at fixed slots, dry-run buffers overlay the TLS arena, everything else
\* lives in the general region behind an admission rule that keeps R cells free).
\*
\* Code anchors (src/esp32/fw_update_net.cpp): gate heapGateEval :701-722, twoRecBufProbe :652-654
\* (frees all three at once :657-661), shed + settle :778-786, stack :1735, staging :843-845,
\* :1207, TLS session http.GET() :607, redirect = new session, dry-run dict + DryMem :1137-1139
\* (nomem :1142), client scope ends before stageFinish :1385-1403, TLS alloc hook :313.
EXTENDS Integers, FiniteSets, Sequences

CONSTANTS
    N,              \* cells in the internal heap
    Policy,         \* "any" (every fitting run) | "edge" (lowest or highest fitting run) | "first" (lowest)
    Static,         \* BOOLEAN: static reservation variant
    EnvMode,        \* "actors" (tabs, phone, console, TIME_WAIT) | "chaos" (alloc/free any general cell)
    PoolN,          \* Static: cells 1..PoolN are the reserved pool
    R,              \* Static: residual, free general cells kept for non-hook allocs (lwIP, SHA bounce, perm)
    Webs, Tws,      \* sets of block names: web client contexts, TIME_WAIT pcbs
    ChurnMax,       \* bound on browser/phone/console arrivals+departures
    MaxReboots,
    FirstPerm,      \* first TLS session after boot leaves one permanent object (86->61 KB split)
    FairLock,       \* BOOLEAN: the pool lock is fair to the AU gate (strong fairness: queue / AU priority)
    StopAfterOk,    \* BOOLEAN: no new job after the first success (liveness runs)
    TrackF5,        \* BOOLEAN: keep the history variables F5 needs (off = smaller state space)
    ReserveSet,     \* non-static: blocks taken at the gate instead of the probe (F3 token); {} = today
    BootCells,      \* cells occupied at boot by blocks that are never freed
    \* mutation switches (FALSE in the real designs)
    BugNoProbe,     \* gate checks the largest block only, no twoRecBuf probe (F1)
    BugNoReserve,   \* Static without the reservation: pool blocks use the general any-fit allocator
    BugLateClose,   \* TLS client scope still open during the dry run (overlay violated)
    BugPin,         \* the job leaves its staging block behind (a pinned block between sessions, F5)
    BugNoPoolLock   \* console mbedTLS and AU share the hook without arbitration

AuJob == {"stg", "scn", "r1", "r2", "hs1", "hs2", "sha", "lw", "dict", "dry"}  \* freed at job end
JobFree == IF BugPin THEN AuJob \ {"stg"} ELSE AuJob
Phone == {"p1", "p2"}
Blocks == AuJob \cup {"stk", "perm", "cn", "cm", "ch", "boot"} \cup Phone \cup Webs \cup Tws
Free == "free"

Size(b) == CASE b = "stk" -> 3 [] b = "stg" -> 1 [] b = "scn" -> 1 [] b = "r1" -> 5 [] b = "r2" -> 5
             [] b = "hs1" -> 2 [] b = "hs2" -> 2 [] b = "sha" -> 1 [] b = "lw" -> 1
             [] b = "perm" -> 1 [] b = "dict" -> 8 [] b = "dry" -> 3 [] b \in Phone -> 1
             [] b = "cn" -> 1 [] b = "cm" -> 1 [] b = "ch" -> 1 [] b = "boot" -> 1
             [] b \in Webs -> 2 [] b \in Tws -> 1
\* Static pool slots (start cell).  dict/dry overlay r1/r2/hs1; cm (console mbedTLS) overlays hs1.
PoolBlocks == {"stk", "stg", "scn", "r1", "r2", "hs1", "hs2", "dict", "dry", "cm"}
Slot(b) == CASE b = "stk" -> 1 [] b = "stg" -> 4 [] b = "scn" -> 5 [] b = "r1" -> 6 [] b = "r2" -> 11
             [] b = "hs1" -> 16 [] b = "hs2" -> 18 [] b = "dict" -> 6 [] b = "dry" -> 14 [] b = "cm" -> 16
MinBlk == 5     \* FWNET_MIN_BLOCK 17000 B rounded up to cells
Order == <<"dict", "dry", "r1", "r2", "hs1", "hs2", "stg", "scn", "sha", "lw", "perm">>

VARIABLES heap, job, env, sys
vars == <<heap, job, env, sys>>

\* --- heap helpers ---------------------------------------------------------------------------------
Pooled(b) == Static /\ ~BugNoReserve /\ b \in PoolBlocks
GenLo == IF Static /\ ~BugNoReserve THEN PoolN + 1 ELSE 1
FreeAt(h, s, len) == /\ s >= 1 /\ s + len - 1 <= N /\ \A i \in s..(s + len - 1) : h[i] = Free
Starts(h, b) ==
    LET all == IF Pooled(b) THEN {s \in {Slot(b)} : FreeAt(h, s, Size(b))}
               ELSE {s \in GenLo..N : FreeAt(h, s, Size(b))}
        lo  == CHOOSE s \in all : \A t \in all : s <= t
        hi  == CHOOSE s \in all : \A t \in all : s >= t
    IN IF all = {} THEN {}
       ELSE IF Policy = "first" THEN {lo} ELSE IF Policy = "edge" THEN {lo, hi} ELSE all
Put(h, b, s) == [i \in 1..N |-> IF i >= s /\ i < s + Size(b) THEN b ELSE h[i]]
Del(h, S) == [i \in 1..N |-> IF h[i] \in S THEN Free ELSE h[i]]
In(h, b) == \E i \in 1..N : h[i] = b
FreeCells(h, lo) == Cardinality({i \in lo..N : h[i] = Free})
Admit(h, sz) == ~Static \/ FreeCells(h, GenLo) - sz >= R      \* admission rule of the static variant
Ok(l, h) == l = 0 \/ \E s \in 1..N : FreeAt(h, s, l)
LF(h) == CHOOSE l \in 0..N : Ok(l, h) /\ \A m \in (l + 1)..N : ~Ok(m, h)
Disj(a, la, b, lb) == a + la <= b \/ b + lb <= a
\* what twoRecBufProbe asks (:643-654): job block 6 KB (2 cells) + two record buffers (5 + 5), at once
Probe(h) == \E s1, s2, s3 \in 1..N : /\ FreeAt(h, s1, 5) /\ FreeAt(h, s2, 5) /\ FreeAt(h, s3, 2)
                                      /\ Disj(s1, 5, s2, 5) /\ Disj(s1, 5, s3, 2) /\ Disj(s2, 5, s3, 2)
RECURSIVE PutAll(_, _)
PutAll(h, sq) == IF sq = <<>> THEN {h}
                 ELSE UNION {PutAll(Put(h, Head(sq), s), Tail(sq)) : s \in Starts(h, Head(sq))}
ResList == SelectSeq(Order, LAMBDA b : b \in ReserveSet /\ (b # "perm" \/ (FirstPerm /\ ~sys.permDone)))
InitHeap == [i \in 1..N |-> IF i \in BootCells THEN "boot" ELSE Free]

IdlePending == In(heap, "stk") /\ job.pc \in {"idle", "done"}   \* a finished job's stack waits for the idle task
Quiet == env.tabs = {} /\ ~env.phone /\ ~env.con

JobIdle == [pc |-> "idle", gate |-> FALSE, failed |-> {}, purged |-> FALSE, hops |-> 0, held |-> {}]

Init == /\ heap = InitHeap /\ job = JobIdle
        /\ env = [tabs |-> {}, phone |-> FALSE, con |-> FALSE, conTls |-> FALSE, churn |-> 0]
        /\ sys = [permDone |-> FALSE, reboots |-> 0, result |-> "none", everOk |-> FALSE, pool |-> {},
                  lastLF |-> -1, lastPD |-> FALSE, envChg |-> TRUE, f5viol |-> FALSE,
                  gOk |-> TRUE]

\* --- AU job ---------------------------------------------------------------------------------------
JobStart ==
    /\ job.pc \in {"idle", "done"} /\ ~IdlePending /\ ~(StopAfterOk /\ sys.everOk)
    /\ \/ /\ Starts(heap, "stk") # {} /\ \E s \in Starts(heap, "stk") : heap' = Put(heap, "stk", s)
          /\ job' = [JobIdle EXCEPT !.pc = "gate"] /\ sys' = sys
       \/ /\ Starts(heap, "stk") = {} /\ heap' = heap     \* xTaskCreate fails (:1735): job never runs
          /\ job' = [JobIdle EXCEPT !.pc = "done"] /\ sys' = [sys EXCEPT !.result = "nostack"]
    /\ env' = env

\* One evaluation of the gate; records what it saw (F1, F5).
GateRec(lf, pr, passed) ==
    IF TrackF5
    THEN [sys EXCEPT !.gOk = lf >= MinBlk /\ pr, !.lastLF = lf, !.lastPD = sys.permDone, !.envChg = FALSE,
                     !.f5viol = @ \/ (sys.lastLF >= 0 /\ ~sys.envChg /\ sys.lastPD /\ lf < sys.lastLF)]
    ELSE [sys EXCEPT !.gOk = lf >= MinBlk /\ pr]

GateStatic ==
    /\ Static /\ (sys.pool = {} \/ BugNoPoolLock)          \* AU waits while the console holds the hook
    /\ heap' = heap
    /\ job' = [job EXCEPT !.pc = "prep1", !.gate = TRUE]
    /\ sys' = [GateRec(LF(heap), TRUE, TRUE) EXCEPT !.pool = @ \cup {"au"}]

Refuse ==   \* first refusal: shed (twPurge frees the TIME_WAIT pcbs :673-697, settle), second: lowheap
    IF ~job.purged
    THEN /\ heap' = Del(heap, Tws) /\ job' = [job EXCEPT !.purged = TRUE]
    ELSE /\ heap' = heap /\ job' = [job EXCEPT !.pc = "unwind"]

GateDyn ==
    /\ ~Static
    /\ LET lf == LF(heap)  pr == Probe(heap)
           okD == lf >= MinBlk /\ (BugNoProbe \/ pr)
       IN IF ReserveSet = {}
          THEN IF okD
               THEN /\ heap' = heap /\ job' = [job EXCEPT !.pc = "prep1", !.gate = TRUE]
                    /\ sys' = GateRec(lf, pr, TRUE)
               ELSE /\ Refuse
                    /\ IF job.purged THEN sys' = [GateRec(lf, pr, FALSE) EXCEPT !.result = "lowheap"]
                       ELSE sys' = [GateRec(lf, pr, FALSE) EXCEPT !.envChg = TRUE]
          ELSE \* F3: the gate itself takes the blocks (a reservation token) instead of a probe; a token block is
               \* used by the session at its normal point and freed at its normal point (no re-reservation)
               \/ /\ PutAll(heap, ResList) # {} /\ \E h2 \in PutAll(heap, ResList) : heap' = h2
                  /\ job' = [job EXCEPT !.pc = "prep1", !.gate = TRUE, !.held = ReserveSet]
                  /\ sys' = [GateRec(lf, pr, TRUE) EXCEPT !.permDone = @ \/ ("perm" \in ReserveSet /\ FirstPerm)]
               \/ /\ PutAll(heap, ResList) = {} /\ Refuse
                  /\ IF job.purged THEN sys' = [GateRec(lf, pr, FALSE) EXCEPT !.result = "lowheap"]
                     ELSE sys' = [GateRec(lf, pr, FALSE) EXCEPT !.envChg = TRUE]

Gate == /\ job.pc = "gate" /\ env' = env /\ (GateStatic \/ GateDyn)

AuAlloc(from, b, actor, to) ==
    /\ job.pc = from /\ env' = env
    /\ IF b \in job.held
       THEN /\ heap' = heap /\ job' = [job EXCEPT !.pc = to] /\ sys' = sys
       ELSE \/ /\ Starts(heap, b) # {} /\ \E s \in Starts(heap, b) : heap' = Put(heap, b, s)
               /\ job' = [job EXCEPT !.pc = to] /\ sys' = sys
            \/ /\ Starts(heap, b) = {} /\ heap' = heap
               /\ job' = [job EXCEPT !.pc = "unwind", !.failed = @ \cup {actor}]
               /\ sys' = [sys EXCEPT !.result = "fail"]

AuFree(from, S, to) ==
    /\ job.pc = from /\ env' = env /\ sys' = sys
    /\ heap' = Del(heap, S) /\ job' = [job EXCEPT !.pc = to, !.held = @ \ S]   \* a freed token block is gone

Prep1 == AuAlloc("prep1", "stg", "au", "prep2")
Prep2 == AuAlloc("prep2", "scn", "au", "r1")
TlsR1 == AuAlloc("r1", "r1", "tls", "r2")
TlsR2 == AuAlloc("r2", "r2", "tls", "hs1")
TlsH1 == AuAlloc("hs1", "hs1", "tls", "hs2")
TlsH2 == AuAlloc("hs2", "hs2", "tls", "sha")
TlsSha == AuAlloc("sha", "sha", "tls", "perm")
PermStep ==
    IF ~FirstPerm \/ sys.permDone
    THEN /\ job.pc = "perm" /\ job' = [job EXCEPT !.pc = "hsdone"] /\ UNCHANGED <<heap, env, sys>>
    ELSE /\ job.pc = "perm" /\ env' = env
         /\ \/ /\ Starts(heap, "perm") # {} /\ \E s \in Starts(heap, "perm") : heap' = Put(heap, "perm", s)
               /\ job' = [job EXCEPT !.pc = "hsdone"] /\ sys' = [sys EXCEPT !.permDone = TRUE]
            \/ /\ Starts(heap, "perm") = {} /\ heap' = heap
               /\ job' = [job EXCEPT !.pc = "unwind", !.failed = @ \cup {"tls"}]
               /\ sys' = [sys EXCEPT !.result = "fail"]
HsDone == AuFree("hsdone", {"hs1", "hs2", "sha"}, "lw")
Lw == AuAlloc("lw", "lw", "lwip", "body")
\* body: either the body pumps (-> tlsclose) or a redirect closes the client and opens a new session
Body == \/ /\ job.pc = "body" /\ job' = [job EXCEPT !.pc = "tlsclose"] /\ UNCHANGED <<heap, env, sys>>
        \/ /\ job.pc = "body" /\ job.hops < 1 /\ env' = env /\ sys' = sys
           /\ heap' = Del(heap, {"r1", "r2", "lw"})
           /\ job' = [job EXCEPT !.pc = "r1", !.hops = 1, !.held = @ \ {"r1", "r2", "lw"}]
\* the client scope ends here (:1385-1403 braces); BugLateClose keeps it open through the dry run
TlsClose == IF BugLateClose
            THEN /\ job.pc = "tlsclose" /\ job' = [job EXCEPT !.pc = "post"] /\ UNCHANGED <<heap, env, sys>>
            ELSE AuFree("tlsclose", {"r1", "r2", "lw"}, "post")
Post == /\ job.pc = "post" /\ job' = [job EXCEPT !.pc = "dry1"] /\ UNCHANGED <<heap, env, sys>>  \* erase, nvs, digest, crc
Dry1 == AuAlloc("dry1", "dict", "au", "dry2")
Dry2 == AuAlloc("dry2", "dry", "au", "dryfin")
DryFin == AuFree("dryfin", {"dict", "dry"}, "finish")
Finish == /\ job.pc = "finish" /\ env' = env
          /\ heap' = Del(heap, JobFree)   \* staging (+ BugLateClose leftovers)
          /\ job' = [job EXCEPT !.pc = "done", !.gate = FALSE, !.held = {}]
          /\ sys' = [sys EXCEPT !.result = "ok", !.everOk = TRUE, !.pool = @ \ {"au"}]
Unwind == /\ job.pc = "unwind" /\ env' = env
          /\ heap' = Del(heap, JobFree)
          /\ job' = [job EXCEPT !.pc = "done", !.gate = FALSE, !.held = {}]
          /\ sys' = [sys EXCEPT !.pool = @ \ {"au"}]
\* the idle task frees the stack of the finished job task
IdleFree == /\ IdlePending /\ heap' = Del(heap, {"stk"}) /\ UNCHANGED <<job, env, sys>>

AuSteps == Gate \/ Prep1 \/ Prep2 \/ TlsR1 \/ TlsR2 \/ TlsH1 \/ TlsH2 \/ TlsSha \/ PermStep \/ HsDone
           \/ Lw \/ Body \/ TlsClose \/ Post \/ Dry1 \/ Dry2 \/ DryFin \/ Finish \/ Unwind

\* --- environment -------------------------------------------------------------------------------------
Touch == sys' = IF TrackF5 THEN [sys EXCEPT !.envChg = TRUE] ELSE sys
\* Place one block of the environment (admission rule applies): the set of resulting heaps.
PlaceEnv(h, b) == IF Admit(h, Size(b)) THEN {Put(h, b, s) : s \in Starts(h, b)} ELSE {}

TabArrive ==
    /\ EnvMode = "actors" /\ env.churn < ChurnMax
    /\ \E w \in Webs \ env.tabs : env' = [env EXCEPT !.tabs = @ \cup {w}, !.churn = @ + 1]
    /\ UNCHANGED <<heap, job>> /\ Touch
\* a tab leaves: its context is freed, a TIME_WAIT pcb may stay (:673-697)
TabLeave ==
    /\ EnvMode = "actors" /\ env.churn < ChurnMax
    /\ \E w \in env.tabs :
          /\ env' = [env EXCEPT !.tabs = @ \ {w}, !.churn = @ + 1]
          /\ \/ heap' = Del(heap, {w})
             \/ \E x \in Tws : \E h2 \in {Put(Del(heap, {w}), x, s) : s \in Starts(Del(heap, {w}), x)} :
                   ~In(heap, x) /\ heap' = h2
    /\ UNCHANGED job /\ Touch
WebServe ==
    /\ EnvMode = "actors"
    /\ \E w \in env.tabs : ~In(heap, w) /\ \E h2 \in PlaceEnv(heap, w) : heap' = h2
    /\ UNCHANGED <<job, env>> /\ Touch
TwExpire ==
    /\ EnvMode = "actors"
    /\ \E x \in Tws : In(heap, x) /\ heap' = Del(heap, {x})
    /\ UNCHANGED <<job, env>> /\ Touch
PhoneOn ==
    /\ EnvMode = "actors" /\ ~env.phone /\ env.churn < ChurnMax
    /\ env' = [env EXCEPT !.phone = TRUE, !.churn = @ + 1] /\ UNCHANGED <<heap, job>> /\ Touch
PhoneServe ==
    /\ EnvMode = "actors" /\ env.phone /\ ~In(heap, "p1")
    /\ Admit(heap, 2) /\ \E h2 \in PutAll(heap, <<"p1", "p2">>) : heap' = h2
    /\ UNCHANGED <<job, env>> /\ Touch
PhoneOff ==
    /\ EnvMode = "actors" /\ env.phone /\ env.churn < ChurnMax
    /\ env' = [env EXCEPT !.phone = FALSE, !.churn = @ + 1] /\ heap' = Del(heap, Phone)
    /\ UNCHANGED job /\ Touch
ConOn ==
    /\ env.churn < ChurnMax /\ ~env.con
    /\ env' = [env EXCEPT !.con = TRUE, !.churn = @ + 1] /\ UNCHANGED <<heap, job>> /\ Touch
ConServe ==
    /\ EnvMode = "actors" /\ env.con /\ ~In(heap, "cn")
    /\ \E h2 \in PlaceEnv(heap, "cn") : heap' = h2
    /\ UNCHANGED <<job, env>> /\ Touch
ConOff ==
    /\ env.churn < ChurnMax /\ env.con
    /\ env' = [env EXCEPT !.con = FALSE, !.churn = @ + 1, !.conTls = FALSE]
    /\ heap' = Del(heap, {"cn", "cm"}) /\ UNCHANGED job
    /\ sys' = [sys EXCEPT !.envChg = TRUE, !.pool = @ \ {"con"}]
\* The mbedTLS hook is global (:313): the console's TLS use goes through the pool, so it must wait
\* for the AU job (and the AU job for it).  Static variant only.
ConTlsStart ==
    /\ Static /\ env.con /\ ~env.conTls /\ (sys.pool = {} \/ BugNoPoolLock)
    /\ env' = [env EXCEPT !.conTls = TRUE]
    /\ \/ \E s \in Starts(heap, "cm") : heap' = Put(heap, "cm", s)
       \/ Starts(heap, "cm") = {} /\ heap' = heap       \* slot busy: the console's TLS itself fails
    /\ sys' = [sys EXCEPT !.pool = @ \cup {"con"}] /\ UNCHANGED job
ConTlsEnd ==
    /\ Static /\ env.conTls /\ env' = [env EXCEPT !.conTls = FALSE]
    /\ heap' = Del(heap, {"cm"}) /\ sys' = [sys EXCEPT !.pool = @ \ {"con"}] /\ UNCHANGED job
ChaosAlloc ==
    /\ EnvMode = "chaos"
    /\ \E c \in GenLo..N : heap[c] = Free /\ Admit(heap, 1) /\ heap' = [heap EXCEPT ![c] = "ch"]
    /\ UNCHANGED <<job, env>> /\ Touch
ChaosFree ==
    /\ EnvMode = "chaos"
    /\ \E c \in 1..N : heap[c] = "ch" /\ heap' = [heap EXCEPT ![c] = Free]
    /\ UNCHANGED <<job, env>> /\ Touch

Reboot == /\ sys.reboots < MaxReboots /\ heap' = InitHeap /\ job' = JobIdle
          /\ env' = [env EXCEPT !.conTls = FALSE]
          /\ sys' = [sys EXCEPT !.reboots = @ + 1, !.permDone = FALSE, !.pool = {}, !.lastLF = -1,
                                !.lastPD = FALSE, !.envChg = TRUE]

EnvAct == TabArrive \/ TabLeave \/ WebServe \/ TwExpire \/ PhoneOn \/ PhoneServe \/ PhoneOff \/ ConOn
          \/ ConServe \/ ConOff \/ ConTlsStart \/ ConTlsEnd \/ ChaosAlloc \/ ChaosFree
Next == JobStart \/ AuSteps \/ IdleFree \/ EnvAct \/ Reboot

\* Fairness as given: AU steps, idle task, TIME_WAIT expiry, serving, console release.  None on a
\* tab/phone/console leaving, chaos, reboot (adversary).
Spec == Init /\ [][Next]_vars /\ WF_vars(JobStart) /\ WF_vars(AuSteps) /\ WF_vars(IdleFree)
        /\ WF_vars(TwExpire) /\ WF_vars(WebServe) /\ WF_vars(PhoneServe) /\ WF_vars(ConServe)
        /\ WF_vars(ConTlsEnd) /\ (FairLock => SF_vars(Gate))

\* Trace legend (run_tlc.sh compacts the heap of a counterexample to one char per cell): . free, S stack,
\* g staging, c scan, 1 2 record buffers, h H handshake arena, a SHA bounce, l lwIP, P permanent object,
\* D dict, d DryMem, W web client, t TIME_WAIT pcb, b phone ring, n console, m console mbedTLS, x chaos,
\* B boot.

\* --- properties --------------------------------------------------------------------------------------
TypeOK == /\ heap \in [1..N -> Blocks \cup {Free}]
          /\ job.failed \subseteq {"au", "tls", "lwip"}
\* F1 the gate decision was backed by what it saw: largest block and the probe (today's gate).
F1 == (job.gate /\ ~Static /\ ReserveSet = {}) => sys.gOk
\* F2 once the gate passed, no allocation of the job (or of mbedTLS / lwIP on its behalf) fails.
F2 == job.gate => job.failed = {}
\* F4 peak budget: session cells plus the worst case of the others fit in the heap.
SessionPcs == {"prep1", "prep2", "r1", "r2", "hs1", "hs2", "sha", "perm", "hsdone", "lw", "body",
               "tlsclose", "post", "dry1", "dry2", "dryfin", "finish"}
Used(h, S) == Cardinality({i \in 1..N : h[i] \in S})
EnvWorst == 2 * Cardinality(Webs) + Cardinality(Tws) + 2 + 1
F4 == job.pc \in SessionPcs => Used(heap, AuJob \cup {"stk", "perm"}) + EnvWorst <= N
\* F5 no pin between sessions: with the environment unchanged the largest block at gate k+1 is not
\* smaller than at gate k (except across the first session, which leaves the permanent object).
F5 == ~sys.f5viol
\* F6 the dry run fits when it starts (nothing but the pc is looked at: env may act meanwhile).
F6 == job.pc = "dry1" => (\/ "dict" \in job.held \/ Starts(heap, "dict") # {})
\* Static: the dry-run buffers overlay the TLS arena, never alive together.
TlsLive == \E b \in {"r1", "r2", "hs1", "hs2", "sha"} : In(heap, b)
NoOverlap == ~(TlsLive /\ job.pc \in {"dry1", "dry2", "dryfin"})
\* Static: the gate (pool lock) never refuses and the job stack is always available.
NoRefuse == sys.result \notin {"lowheap", "nostack"}
\* Static: the global hook has at most one user.
PoolOne == Cardinality(sys.pool) <= 1
\* Reachability probe (cfgs *_reach_*, expected violated): a job really runs to the end in this model.
NeverOk == sys.result # "ok"
\* F7 / liveness (StopAfterOk runs): a persistent tab or phone must not block the update forever.
F7 == (<>[](~Quiet)) => <>sys.everOk
LQ == (<>[]Quiet) => <>sys.everOk
\* F8 (operator 2026-11-23): the update is started from the web GUI, so the requesting tab is open by
\* construction. With exactly that one tab persistent the update must eventually succeed; F8p adds a phone
\* on BLE (the field report).
OneTab == Cardinality(env.tabs) = 1 /\ ~env.phone /\ ~env.con
OneTabPhone == Cardinality(env.tabs) = 1 /\ env.phone /\ ~env.con
F8 == (<>[]OneTab) => <>sys.everOk
F8p == (<>[]OneTabPhone) => <>sys.everOk
\* F8s: F8p is only meaningful if the phone is actually served (its rings placed) while the tab is open;
\* the static admission rule may starve the phone instead of the update.
PhoneServed == Phone \subseteq {heap[i] : i \in 1..N}
F8s == (<>[]OneTabPhone) => <>[]PhoneServed
\* reachability probe (expect violated = reachable): tab + phone served + update succeeded
NotTabPhoneOk == ~(OneTabPhone /\ PhoneServed /\ sys.everOk)
=============================================================================
