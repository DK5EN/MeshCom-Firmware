---------------------------- MODULE heap ----------------------------
\* OTA-LOWHEAP-01 wave 1d: ownership model of the ESP32-S3 internal heap around one
\* Auto Update (AU) job.  No placement here (see heap_frag.tla): a block is live or not and has
\* exactly one owner.  Checks H1-H8 of the advisor rule set, with a nondeterministic Fail at
\* every AU step and a Reboot at any time.
\*
\* Code anchors (src/esp32/fw_update_net.cpp unless noted):
\*   job task create :1735, run :1480-1591, restart path :1536-1545 (only #if AU_BLE_PAUSE, i.e.
\*   NOT compiled on the S3, fw_update_net.h:240); staging + 2 FwJsonScan :843-845; TLS session
\*   inside http.GET() :607; dry-run dict + DryMem :1137-1139; shed hold :1480ff, loop restore
\*   esp32_main.cpp:2795.
EXTENDS Naturals, FiniteSets

CONSTANTS
    FirstPerm,      \* BOOLEAN: the first TLS session after boot leaves one permanent object
    K,              \* max number of permanent blocks the design allows (H4)
    MaxJobs,        \* bound on AU jobs per run; 0 = unbounded (the job counter then stays 0)
    MaxReboots,     \* bound on reboots per run
    EnvChurn,       \* BOOLEAN: browser/phone/console come and go (FALSE only to keep counterexamples short)
    Restart,        \* BOOLEAN: restart-on-new-stack path compiled in (classic ESP32 only)
    \* mutation switches, all FALSE / "none" in the real design
    BugLeakAt,      \* "none" | step name: the unwind after a Fail at that step forgets the staging block
    BugPermEvery,   \* every TLS session leaves a permanent object
    BugPermCycle,   \* every TLS session replaces the permanent object by a new one (count stays 1)
    BugNoRestore,   \* a failed job does not clear the shed hold
    BugStackLeak,   \* the finished job never hands its stack over to the idle task
    BugTwoStep,     \* stack handover is two steps (owner none in between)
    BugRebootKeep   \* reboot keeps the shed hold (RAM state survives)

AuBlocks   == {"stk", "stk2", "stg", "scn", "r1", "r2", "hs", "lw", "dict", "dry"}
PermBlocks == {"p1", "p2", "p3"}
EnvBlocks  == {"w1", "b1", "c1"}
Blocks     == AuBlocks \cup PermBlocks \cup EnvBlocks
Stacks     == {"stk", "stk2"}
None       == "none"
Owners     == {"au", "tls", "lwip", "idle", "web", "ble", "con"}

Run == {"begin", "gate", "prep", "tls", "body", "tlsclose", "erase", "nvs", "digest", "crc",
        "dryrun", "finish"}

VARIABLES live, owner, pc, shedMask, svcStopped, perm, nvs, env, jobs, reboots, cur, failStep,
          restarted

vars == <<live, owner, pc, shedMask, svcStopped, perm, nvs, env, jobs, reboots, cur, failStep,
          restarted>>

InitOwner == [b \in Blocks |-> None]

Init == /\ live = {} /\ owner = InitOwner /\ pc = "idle" /\ shedMask = 0 /\ svcStopped = FALSE
        /\ perm = {} /\ nvs = [attempts |-> 0] /\ env = [web |-> FALSE, phone |-> FALSE, con |-> FALSE]
        /\ jobs = 0 /\ reboots = 0 /\ cur = "stk" /\ failStep = None /\ restarted = FALSE

Take(set, o)  == /\ live' = live \cup set
                 /\ owner' = [b \in Blocks |-> IF b \in set THEN o ELSE owner[b]]
Drop(set)     == /\ live' = live \ set
                 /\ owner' = [b \in Blocks |-> IF b \in set THEN None ELSE owner[b]]

NextPerm == CHOOSE p \in PermBlocks \ perm : TRUE

\* --- AU job ------------------------------------------------------------------------------------

JobStart == /\ pc \in {"idle", "done"} /\ (MaxJobs = 0 \/ jobs < MaxJobs)
            /\ owner["stk"] = None /\ owner["stk2"] = None   \* gate settle: idle task ran (:141)
            /\ Take({"stk"}, "au") /\ cur' = "stk"
            /\ pc' = "begin" /\ jobs' = (IF MaxJobs = 0 THEN 0 ELSE jobs + 1) /\ failStep' = None /\ restarted' = FALSE
            /\ UNCHANGED <<shedMask, svcStopped, perm, nvs, env, reboots>>

Begin == /\ pc = "begin" /\ shedMask' = 1 /\ pc' = "gate"
         /\ UNCHANGED <<live, owner, svcStopped, perm, nvs, env, jobs, reboots, cur, failStep, restarted>>

Gate == /\ pc = "gate" /\ pc' = "prep"
        /\ UNCHANGED <<live, owner, shedMask, svcStopped, perm, nvs, env, jobs, reboots, cur, failStep, restarted>>

Prep == /\ pc = "prep" /\ Take({"stg", "scn"}, "au") /\ pc' = "tls"
        /\ UNCHANGED <<shedMask, svcStopped, perm, nvs, env, jobs, reboots, cur, failStep, restarted>>

\* TLS session open: two record buffers + handshake arena; first session also the permanent object.
Tls == /\ pc = "tls"
       /\ LET mkPerm == (FirstPerm /\ perm = {}) \/ BugPermEvery
              cyc    == BugPermCycle /\ perm # {}
              old    == IF cyc THEN perm ELSE {}
              pb     == IF cyc THEN {CHOOSE p \in PermBlocks \ perm : TRUE}
                        ELSE IF mkPerm /\ perm # PermBlocks THEN {NextPerm} ELSE {}
              held   == (live \ old)
          IN /\ live' = held \cup {"r1", "r2", "hs"} \cup pb
             /\ owner' = [b \in Blocks |-> IF b \in {"r1", "r2", "hs"} \cup pb THEN "tls"
                                           ELSE IF b \in old THEN None ELSE owner[b]]
             /\ perm' = (perm \ old) \cup pb
       /\ pc' = "body"
       /\ UNCHANGED <<shedMask, svcStopped, nvs, env, jobs, reboots, cur, failStep, restarted>>

Body == /\ pc = "body"
        /\ live' = (live \ {"hs"}) \cup {"lw"}
        /\ owner' = [b \in Blocks |-> IF b = "hs" THEN None ELSE IF b = "lw" THEN "lwip" ELSE owner[b]]
        /\ pc' = "tlsclose"
        /\ UNCHANGED <<shedMask, svcStopped, perm, nvs, env, jobs, reboots, cur, failStep, restarted>>

TlsClose == /\ pc = "tlsclose" /\ Drop({"r1", "r2", "lw"}) /\ pc' = "erase"
            /\ UNCHANGED <<shedMask, svcStopped, perm, nvs, env, jobs, reboots, cur, failStep, restarted>>

Plain(from, to) == /\ pc = from /\ pc' = to
                   /\ UNCHANGED <<live, owner, shedMask, svcStopped, perm, env, jobs, reboots, cur, failStep, restarted>>
Erase  == Plain("erase", "nvs") /\ UNCHANGED nvs
Nvs    == Plain("nvs", "digest") /\ nvs' = [attempts |-> 0]   \* a good write clears the attempt counter
Digest == Plain("digest", "crc") /\ UNCHANGED nvs
Crc    == Plain("crc", "dryrun") /\ UNCHANGED nvs

DryRun == /\ pc = "dryrun" /\ Take({"dict", "dry"}, "au") /\ pc' = "finish"
          /\ UNCHANGED <<shedMask, svcStopped, perm, nvs, env, jobs, reboots, cur, failStep, restarted>>

Finish == /\ pc = "finish" /\ Drop({"dict", "dry", "stg", "scn"}) /\ pc' = "exit"
          /\ UNCHANGED <<shedMask, svcStopped, perm, nvs, env, jobs, reboots, cur, failStep, restarted>>

\* Nondeterministic failure at ANY step (H5): alloc failure, handshake, stall, timeout, toobig, flash...
AuFail == /\ pc \in Run /\ pc' = "unwind" /\ failStep' = pc
          /\ nvs' = [attempts |-> IF nvs.attempts < 2 THEN nvs.attempts + 1 ELSE 2]
          /\ UNCHANGED <<live, owner, shedMask, svcStopped, perm, env, jobs, reboots, cur, restarted>>

Mine(b) == owner[b] \in {"au", "tls", "lwip"} /\ b \notin perm /\ b \notin Stacks

Unwind == /\ pc = "unwind"
          /\ LET rel == {b \in Blocks : Mine(b)} \ (IF BugLeakAt = failStep THEN {"stg"} ELSE {})
             IN Drop(rel)
          /\ pc' = IF Restart /\ ~restarted /\ failStep \in {"gate", "prep", "tls"} THEN "restartpt" ELSE "exit"
          /\ UNCHANGED <<shedMask, svcStopped, perm, nvs, env, jobs, reboots, cur, failStep, restarted>>

\* :1536-1545: new task created before the old one ends; shed hold stays; old stack goes to idle.
RestartTask == /\ pc = "restartpt"
               /\ \/ /\ owner["stk2"] = None
                     /\ live' = live \cup {"stk2"}
                     /\ owner' = [b \in Blocks |-> IF b = "stk2" THEN "au" ELSE IF b = "stk" THEN "idle" ELSE owner[b]]
                     /\ cur' = "stk2" /\ restarted' = TRUE /\ pc' = "begin" /\ failStep' = None
                  \/ /\ pc' = "exit" /\ UNCHANGED <<live, owner, cur, restarted, failStep>>   \* xTaskCreate failed
               /\ UNCHANGED <<shedMask, svcStopped, perm, nvs, env, jobs, reboots>>

Exit == /\ pc = "exit"
        /\ shedMask' = IF BugNoRestore /\ failStep # None THEN shedMask ELSE 0
        /\ owner' = [b \in Blocks |-> IF b \in Stacks /\ owner[b] = "au"
                                         THEN (IF BugStackLeak THEN "au" ELSE IF BugTwoStep THEN None ELSE "idle")
                                         ELSE owner[b]]
        /\ pc' = IF BugTwoStep THEN "handover" ELSE "done"
        /\ UNCHANGED <<live, svcStopped, perm, nvs, env, jobs, reboots, cur, failStep, restarted>>

Handover == /\ pc = "handover"       \* only reachable with BugTwoStep
            /\ owner' = [b \in Blocks |-> IF b \in Stacks /\ b \in live /\ owner[b] = None THEN "idle" ELSE owner[b]]
            /\ pc' = "done"
            /\ UNCHANGED <<live, shedMask, svcStopped, perm, nvs, env, jobs, reboots, cur, failStep, restarted>>

\* The idle task frees what deleted tasks left behind.
Idle == /\ \E b \in Blocks : owner[b] = "idle" /\ Drop({b})
        /\ UNCHANGED <<pc, shedMask, svcStopped, perm, nvs, env, jobs, reboots, cur, failStep, restarted>>

\* --- loop task (esp32_main.cpp:2795): executes the shed hold --------------------------------------
LoopShed == /\ shedMask # 0 /\ ~svcStopped /\ svcStopped' = TRUE
            /\ UNCHANGED <<live, owner, pc, shedMask, perm, nvs, env, jobs, reboots, cur, failStep, restarted>>
LoopRestore == /\ shedMask = 0 /\ svcStopped /\ svcStopped' = FALSE
               /\ UNCHANGED <<live, owner, pc, shedMask, perm, nvs, env, jobs, reboots, cur, failStep, restarted>>

\* --- environment: browser tab, phone, console come and go; blocks follow while up -----------------
EnvToggle == /\ EnvChurn /\ \E k \in DOMAIN env : env' = [env EXCEPT ![k] = ~@]
             /\ UNCHANGED <<live, owner, pc, shedMask, svcStopped, perm, nvs, jobs, reboots, cur, failStep, restarted>>
EnvBlock == {<<"web", "w1">>, <<"phone", "b1">>, <<"con", "c1">>}
OwnerOfEnv(k) == IF k = "web" THEN "web" ELSE IF k = "phone" THEN "ble" ELSE "con"
Serve == /\ \E t \in EnvBlock : /\ env[t[1]] /\ owner[t[2]] = None /\ Take({t[2]}, OwnerOfEnv(t[1]))
         /\ UNCHANGED <<pc, shedMask, svcStopped, perm, nvs, env, jobs, reboots, cur, failStep, restarted>>
Release == /\ \E t \in EnvBlock : /\ ~env[t[1]] /\ owner[t[2]] # None /\ Drop({t[2]})
           /\ UNCHANGED <<pc, shedMask, svcStopped, perm, nvs, env, jobs, reboots, cur, failStep, restarted>>

\* --- H8: reboot, warm = cold; nvs and the browser/phone side survive ---------------------------------
Reboot == /\ reboots < MaxReboots /\ reboots' = reboots + 1
          /\ live' = {} /\ owner' = InitOwner /\ pc' = "idle" /\ perm' = {}
          /\ shedMask' = IF BugRebootKeep THEN shedMask ELSE 0
          /\ svcStopped' = IF BugRebootKeep THEN svcStopped ELSE FALSE
          /\ cur' = "stk" /\ failStep' = None /\ restarted' = FALSE
          /\ UNCHANGED <<nvs, env, jobs>>

Next == JobStart \/ Begin \/ Gate \/ Prep \/ Tls \/ Body \/ TlsClose \/ Erase \/ Nvs \/ Digest \/ Crc
        \/ DryRun \/ Finish \/ AuFail \/ Unwind \/ RestartTask \/ Exit \/ Handover \/ Idle
        \/ LoopShed \/ LoopRestore \/ EnvToggle \/ Serve \/ Release \/ Reboot

\* Fairness: every AU step and the idle task weak; the loop task strong (it polls every iteration, the
\* shed mask is stable for far longer than one loop period; with WF alone back-to-back jobs would
\* starve the restore).  None on EnvToggle/AuFail/Reboot (adversary).
Steps == Begin \/ Gate \/ Prep \/ Tls \/ Body \/ TlsClose \/ Erase \/ Nvs \/ Digest \/ Crc \/ DryRun
         \/ Finish \/ Unwind \/ RestartTask \/ Exit \/ Handover
Spec == Init /\ [][Next]_vars /\ WF_vars(Steps) /\ WF_vars(Idle)
        /\ SF_vars(LoopShed) /\ SF_vars(LoopRestore) /\ WF_vars(JobStart)

TypeOK == /\ live \subseteq Blocks /\ owner \in [Blocks -> Owners \cup {None}] /\ perm \subseteq PermBlocks

\* H1 every live block has exactly one owner (owner is a function), every dead block none.
H1 == \A b \in Blocks : (b \in live) <=> (owner[b] # None)
\* H2 job end leaves nothing: au owns nothing, tls owns only permanent blocks.
H2 == pc = "done" => /\ \A b \in Blocks : owner[b] # "au"
                     /\ \A b \in Blocks : owner[b] = "tls" => b \in perm
\* H4 (safety half) permanent set bounded; (liveness half) it stops growing.
H4Bound == Cardinality(perm) <= K
H4Stable == <>[][UNCHANGED perm]_vars
\* H3 every AU allocation is freed or declared permanent.
H3 == \A b \in AuBlocks : (b \in live) ~> (b \notin live \/ b \in perm)
\* H6 handover moves ownership in one step; a block that stays live never becomes unowned.
H6Step == [][\A b \in Blocks : (b \in live /\ b \in live') => (owner[b] # None /\ owner'[b] # None)]_vars
H6Idle == pc = "done" => \A b \in Stacks : b \in live => owner[b] = "idle"
\* H7 shed always undone (both the mask and the services the loop stopped).
H7 == /\ (shedMask # 0) ~> (shedMask = 0)
      /\ svcStopped ~> ~svcStopped
\* H8 after a reboot RAM state equals a cold start; nvs and env are untouched.
Fresh == /\ live' = {} /\ owner' = InitOwner /\ pc' = "idle" /\ shedMask' = 0 /\ svcStopped' = FALSE
         /\ perm' = {} /\ nvs' = nvs /\ env' = env
H8 == [][Reboot => Fresh]_vars
=============================================================================
