--------------------------- MODULE netconsole ---------------------------
\* FORMAL-01, F2-10: the ESP32 network console (src/net_console.cpp) -- one listening socket, ONE handshake
\* slot (s_hs_running) with a generation counter, HMAC challenge/response in authTask, one session socket s_fd,
\* a takeover of the session by a new authenticated client, a takeover of the handshake slot from a silent
\* peer (F2-10-SLOT), stop/start, teardown from the loop task.
\*
\* Rework 2026-11-23 (docs/formal-review-20261123.md 3.1): the first version ended a preempted handshake in one
\* atomic close.  The code does not: accept() calls shutdown() on the old socket and the old authTask keeps
\* running until its recv() wakes, then marks, sends FAIL, closes its own fd and calls freeSlot().  Every authTask
\* is now its own process (one per connection), so the late steps of a preempted task interleave with the new
\* holder and with further accepts.
\*
\* Code (functions, see specs/manifest.json for the drift check):
\*   authTask            fd, myGen; markRx = `s_hs_rx_gen = myGen` (unguarded store); freeSlot =
\*                       `if (s_hs_gen == myGen) s_hs_running = false`; recv loop under a 10 s total deadline
\*                       WITHOUT the mutex; markRx after every received byte and once more when the receive
\*                       phase ends; FAIL path: send, close(fd), log line, freeSlot (close and freeSlot are two
\*                       steps with a serial print between them); OK path: install under s_mutex (takeover:
\*                       close old s_fd, s_fd = fd, s_authenticated = true), then freeSlot.
\*   loopNetConsole      accept(); slot busy: netConsoleSlotMayPreempt(running, rxSeen = (s_hs_rx_gen == s_hs_gen),
\*                       silent >= 3 s) -> shutdown(s_hs_fd), else reject + close; then s_hs_gen++, s_hs_fd,
\*                       s_hs_running = true, spawn authTask.  Dead-session probe on s_fd without the lock.
\*   teardownClient / teardownIfCurrent (s_fd == fd check) / stopNetConsole / startNetConsole / netConsoleRead.
\*
\* Granularity: everything under s_mutex is one action.  In authTask the receive of a byte and the markRx that
\* follows it are two actions (another task can run between them), likewise close(fd) and freeSlot.  The accept
\* path (accept, preempt decision, shutdown, slot take-over) is ONE action: the check-then-act window inside it
\* (a few instructions, described in the code comment) is NOT modelled.
\*
\* Abstractions: a connection is a conn id (1..MaxConn) with a behaviour fixed at accept (good = answers the
\* challenge correctly, bad = wrong response, slow = never sends); fd numbers come from NF slots, lowest free
\* first like lwIP when Reuse; time is abstracted: "silent for 3 s" is any moment at which the rx mark does not
\* show the current generation, the 10 s deadline is an action that may always fire (HasDeadline) under weak
\* fairness -- the deadline arithmetic itself is outside the model; a socket that got shutdown() delivers EOF and
\* nothing else; one reader (loop task: accept, reads, probes and stop/start all run there, hence they do not
\* interleave with each other -- AcceptAnyTime drops that); no password is the same as a password for the
\* handshake; foreign sockets (web server, OTA) taking the same fd number are not modelled; a failing
\* xTaskCreate is not modelled.
EXTENDS Naturals, FiniteSets

CONSTANTS
    MaxConn,            \* connections over the whole run
    NF,                 \* fd numbers
    Reuse,              \* TRUE: a new socket gets the lowest free fd number (lwIP); FALSE: numbers are never reused
    MaxStops,           \* stop/start cycles
    HasDeadline,        \* TRUE: the handshake has the 10 s total deadline
    AcceptAnyTime,      \* TRUE: accept may run while the loop task sits between recv and teardownIfCurrent
    Takeover,           \* TRUE (today): a new connection takes the handshake slot from a holder without rx mark
    MarkGuard,          \* FALSE (today): markRx stores the task's generation unconditionally.
                        \* TRUE (proposal): a task marks only while it is the current holder (s_hs_gen == myGen)
    BugNoFdCheck,       \* teardownIfCurrent without the s_fd == fd test
    BugFlagSurvive,     \* teardown leaves s_authenticated set
    BugNoReset,         \* a new connection becomes s_fd at accept, before auth, s_authenticated not reset
    BugNoClose,         \* the session takeover does not close the old socket
    BugStaleFd,         \* teardown closes s_fd but does not reset it
    BugPreemptActive    \* the slot takeover ignores the rx mark

VARIABLES
    started, stops,     \* s_started, stop/start cycles so far
    open,               \* open[fd] = conn id holding that fd number, 0 = free
    shut,               \* connections whose socket got shutdown() from the accept path
    peerClosed,         \* connections whose peer hung up
    kind, nconn,        \* behaviour per conn, connections so far
    authed,             \* ghost: conns that passed the HMAC
    sfd, sAuth,         \* s_fd (0 = -1), s_authenticated
    hs, hsGen, hsRx, hsFd,          \* s_hs_running, s_hs_gen, s_hs_rx_gen, s_hs_fd
    hsConn,             \* ghost: the conn that holds the slot
    tpc, tfd, tgen,     \* authTask of conn c: program counter, its fd, its generation
    got,                \* got[c]: the task has received at least one byte
    marked,             \* ghost: the task stored its rx mark while it was the current holder
    rpc, rfd, rconn,    \* reader between recv and teardownIfCurrent
    cmdBad, wrongTear, dblClose,                    \* ghost verdict flags
    silentBlock, kickMarked, foreignShut, deadInst  \* ghost verdict flags of the slot takeover

slotV == <<hs, hsGen, hsRx, hsFd, hsConn>>
taskV == <<tpc, tfd, tgen, got, marked>>
readV == <<rpc, rfd, rconn>>
sessV == <<sfd, sAuth>>
sockV == <<open, shut, peerClosed>>
slotGhostV == <<silentBlock, kickMarked, foreignShut, deadInst>>
vars == <<started, stops, open, shut, peerClosed, kind, nconn, authed, sfd, sAuth, hs, hsGen, hsRx, hsFd, hsConn,
          tpc, tfd, tgen, got, marked, rpc, rfd, rconn, cmdBad, wrongTear, dblClose,
          silentBlock, kickMarked, foreignShut, deadInst>>

Conns == 1..MaxConn
Fds == 1..NF

Init ==
    /\ started = TRUE /\ stops = 0
    /\ open = [f \in Fds |-> 0] /\ shut = {} /\ peerClosed = {}
    /\ kind = [c \in Conns |-> "good"] /\ nconn = 0 /\ authed = {}
    /\ sfd = 0 /\ sAuth = FALSE
    /\ hs = FALSE /\ hsGen = 0 /\ hsRx = 0 /\ hsFd = 0 /\ hsConn = 0
    /\ tpc = [c \in Conns |-> "none"] /\ tfd = [c \in Conns |-> 0] /\ tgen = [c \in Conns |-> 0]
    /\ got = [c \in Conns |-> FALSE] /\ marked = [c \in Conns |-> FALSE]
    /\ rpc = "idle" /\ rfd = 0 /\ rconn = 0
    /\ cmdBad = FALSE /\ wrongTear = FALSE /\ dblClose = FALSE
    /\ silentBlock = FALSE /\ kickMarked = FALSE /\ foreignShut = FALSE /\ deadInst = FALSE

\* ---- loop task: accept, preempt decision, slot take-over ---------------------------------------------
\* netConsoleSlotMayPreempt: slot held, no rx mark of the current generation (time abstracted)
MayPreempt == Takeover /\ hs /\ (hsRx # hsGen \/ BugPreemptActive)
\* ghost truth, not the flag: the holder is a peer that has sent nothing and its task still waits for it
HolderSilent == hsConn # 0 /\ tpc[hsConn] = "await" /\ kind[hsConn] = "slow" /\ ~marked[hsConn]

Accept ==
    /\ started /\ nconn < MaxConn
    /\ (rpc = "idle" \/ AcceptAnyTime)
    /\ LET c == nconn + 1
           free == {g \in Fds : open[g] = 0}
       IN /\ free # {}
          /\ \E k \in {"good", "bad", "slow"} :
             LET f == IF Reuse THEN CHOOSE g \in free : \A h \in free : g <= h ELSE c
                 withNew == [open EXCEPT ![f] = c]      \* accept() has already returned the new socket
                 hit == IF hsFd = 0 THEN 0 ELSE withNew[hsFd]   \* the conn behind the fd number shutdown() gets
             IN /\ f \in free
                /\ nconn' = c /\ kind' = [kind EXCEPT ![c] = k]
                /\ IF hs /\ ~MayPreempt
                   THEN \* "Rejected: auth already in progress", the new socket is closed at once
                        /\ open' = open
                        /\ silentBlock' = (silentBlock \/ HolderSilent)
                        /\ UNCHANGED <<shut, hs, hsGen, hsFd, hsConn, tpc, tfd, tgen, sfd, kickMarked, foreignShut>>
                   ELSE \* slot free, or taken over: shutdown(s_hs_fd), s_hs_gen++, spawn authTask
                        /\ open' = withNew
                        /\ shut' = (IF hs /\ hit # 0 THEN shut \cup {hit} ELSE shut)
                        /\ kickMarked' = (kickMarked \/ (hs /\ marked[hsConn]))
                        /\ foreignShut' = (foreignShut \/ (hs /\ hit # 0 /\ hit # hsConn))
                        /\ hs' = TRUE /\ hsGen' = hsGen + 1 /\ hsFd' = f /\ hsConn' = c
                        /\ tpc' = [tpc EXCEPT ![c] = "await"]
                        /\ tfd' = [tfd EXCEPT ![c] = f]
                        /\ tgen' = [tgen EXCEPT ![c] = hsGen + 1]
                        /\ sfd' = (IF BugNoReset THEN f ELSE sfd)   \* mutation: connection installed before auth
                        /\ UNCHANGED silentBlock
    /\ UNCHANGED <<started, stops, peerClosed, authed, sAuth, hsRx, got, marked, rpc, rfd, rconn,
                   cmdBad, wrongTear, dblClose, deadInst>>

\* ---- a peer hangs up whenever it likes -------------------------------------------------------------
PeerClose ==
    /\ \E f \in Fds : open[f] # 0 /\ open[f] \notin peerClosed /\ peerClosed' = peerClosed \cup {open[f]}
    /\ UNCHANGED <<started, stops, open, shut, kind, nconn, authed, sfd, sAuth, hs, hsGen, hsRx, hsFd, hsConn,
                   tpc, tfd, tgen, got, marked, rpc, rfd, rconn, cmdBad, wrongTear, dblClose,
                   silentBlock, kickMarked, foreignShut, deadInst>>

\* ---- authTask of connection c, outside the mutex ----------------------------------------------------
\* markRx: today an unguarded store of the task's own generation
MarkVal(c) == IF MarkGuard /\ hsGen # tgen[c] THEN hsRx ELSE tgen[c]

TRecv(c) ==     \* recv() returns the first byte of the peer
    /\ tpc[c] = "await" /\ ~got[c] /\ kind[c] # "slow" /\ c \notin peerClosed /\ c \notin shut
    /\ got' = [got EXCEPT ![c] = TRUE] /\ tpc' = [tpc EXCEPT ![c] = "mark"]
    /\ UNCHANGED <<started, stops, open, shut, peerClosed, kind, nconn, authed, sfd, sAuth, hs, hsGen, hsRx, hsFd,
                   hsConn, tfd, tgen, marked, rpc, rfd, rconn, cmdBad, wrongTear, dblClose,
                   silentBlock, kickMarked, foreignShut, deadInst>>

TMark(c) ==     \* markRx() after the byte
    /\ tpc[c] = "mark"
    /\ hsRx' = MarkVal(c)
    /\ marked' = [marked EXCEPT ![c] = (@ \/ hsGen = tgen[c])]
    /\ tpc' = [tpc EXCEPT ![c] = "await"]
    /\ UNCHANGED <<started, stops, open, shut, peerClosed, kind, nconn, authed, sfd, sAuth, hs, hsGen, hsFd,
                   hsConn, tfd, tgen, got, rpc, rfd, rconn, cmdBad, wrongTear, dblClose,
                   silentBlock, kickMarked, foreignShut, deadInst>>

TLine(c) ==     \* the response line is complete (readOk)
    /\ tpc[c] = "await" /\ got[c] /\ c \notin peerClosed /\ c \notin shut
    /\ tpc' = [tpc EXCEPT ![c] = "postok"]
    /\ UNCHANGED <<started, stops, open, shut, peerClosed, kind, nconn, authed, sfd, sAuth, hs, hsGen, hsRx, hsFd,
                   hsConn, tfd, tgen, got, marked, rpc, rfd, rconn, cmdBad, wrongTear, dblClose,
                   silentBlock, kickMarked, foreignShut, deadInst>>

TEof(c) ==      \* recv() <= 0 (peer closed, shutdown() from the accept path) or the total deadline
    /\ tpc[c] = "await"
    /\ (c \in peerClosed \/ c \in shut \/ HasDeadline)
    /\ tpc' = [tpc EXCEPT ![c] = "postfail"]
    /\ UNCHANGED <<started, stops, open, shut, peerClosed, kind, nconn, authed, sfd, sAuth, hs, hsGen, hsRx, hsFd,
                   hsConn, tfd, tgen, got, marked, rpc, rfd, rconn, cmdBad, wrongTear, dblClose,
                   silentBlock, kickMarked, foreignShut, deadInst>>

TPost(c) ==     \* markRx() "receive phase over", then the HMAC compare
    /\ tpc[c] \in {"postok", "postfail"}
    /\ hsRx' = MarkVal(c)
    /\ marked' = [marked EXCEPT ![c] = (@ \/ hsGen = tgen[c])]
    /\ IF tpc[c] = "postok" /\ kind[c] = "good"
       THEN /\ tpc' = [tpc EXCEPT ![c] = "verified"] /\ authed' = authed \cup {c}
       ELSE /\ tpc' = [tpc EXCEPT ![c] = "fail"] /\ authed' = authed
    /\ UNCHANGED <<started, stops, open, shut, peerClosed, kind, nconn, sfd, sAuth, hs, hsGen, hsFd,
                   hsConn, tfd, tgen, got, rpc, rfd, rconn, cmdBad, wrongTear, dblClose,
                   silentBlock, kickMarked, foreignShut, deadInst>>

TFailClose(c) ==    \* send FAIL, close(fd): the task closes its own socket
    /\ tpc[c] = "fail"
    /\ IF open[tfd[c]] = c
       THEN /\ open' = [open EXCEPT ![tfd[c]] = 0] /\ dblClose' = dblClose
       ELSE /\ open' = open /\ dblClose' = TRUE       \* the number is free or belongs to someone else
    /\ tpc' = [tpc EXCEPT ![c] = "free"]
    /\ UNCHANGED <<started, stops, shut, peerClosed, kind, nconn, authed, sfd, sAuth, hs, hsGen, hsRx, hsFd,
                   hsConn, tfd, tgen, got, marked, rpc, rfd, rconn, cmdBad, wrongTear,
                   silentBlock, kickMarked, foreignShut, deadInst>>

TInstall(c) ==      \* banner, then under the mutex: session takeover or drop on a stopped console
    /\ tpc[c] = "verified"
    /\ IF started
       THEN /\ IF sfd # 0 /\ ~BugNoClose
               THEN IF open[sfd] = 0
                    THEN /\ dblClose' = TRUE /\ open' = open
                    ELSE /\ open' = [open EXCEPT ![sfd] = 0] /\ dblClose' = dblClose
               ELSE /\ open' = open /\ dblClose' = dblClose
            /\ sfd' = tfd[c] /\ sAuth' = TRUE
            /\ deadInst' = (deadInst \/ c \in shut)
       ELSE /\ open' = [open EXCEPT ![tfd[c]] = 0]
            /\ UNCHANGED <<sfd, sAuth, dblClose, deadInst>>
    /\ tpc' = [tpc EXCEPT ![c] = "free"]
    /\ UNCHANGED <<started, stops, shut, peerClosed, kind, nconn, authed, hs, hsGen, hsRx, hsFd,
                   hsConn, tfd, tgen, got, marked, rpc, rfd, rconn, cmdBad, wrongTear,
                   silentBlock, kickMarked, foreignShut>>

TFree(c) ==     \* freeSlot(): only the current holder frees the slot
    /\ tpc[c] = "free"
    /\ hs' = (IF hsGen = tgen[c] THEN FALSE ELSE hs)
    /\ tpc' = [tpc EXCEPT ![c] = "none"]
    /\ UNCHANGED <<started, stops, open, shut, peerClosed, kind, nconn, authed, sfd, sAuth, hsGen, hsRx, hsFd,
                   hsConn, tfd, tgen, got, marked, rpc, rfd, rconn, cmdBad, wrongTear, dblClose,
                   silentBlock, kickMarked, foreignShut, deadInst>>

\* the steps the task takes on its own (the peer's bytes, TRecv and TLine, are the peer's choice)
TaskStep(c) == TMark(c) \/ TEof(c) \/ TPost(c) \/ TFailClose(c) \/ TInstall(c) \/ TFree(c)

\* ---- the loop task reads the session: recv under the mutex, the verdict acts after releasing it -------
ReadData ==     \* r == 1, the byte goes to the command path
    /\ rpc = "idle" /\ sAuth /\ sfd # 0 /\ open[sfd] # 0
    /\ open[sfd] \notin peerClosed /\ open[sfd] \notin shut
    /\ cmdBad' = (cmdBad \/ open[sfd] \notin authed)
    /\ UNCHANGED <<started, stops, open, shut, peerClosed, kind, nconn, authed, sfd, sAuth, hs, hsGen, hsRx, hsFd,
                   hsConn, tpc, tfd, tgen, got, marked, rpc, rfd, rconn, wrongTear, dblClose,
                   silentBlock, kickMarked, foreignShut, deadInst>>

ReadDead ==     \* recv says EOF or an error on the fd read from s_fd; the lock is released
    /\ rpc = "idle" /\ sAuth
    /\ \/ sfd # 0 /\ open[sfd] # 0 /\ (open[sfd] \in peerClosed \/ open[sfd] \in shut) /\ rconn' = open[sfd]
       \/ sfd # 0 /\ open[sfd] = 0 /\ rconn' = 0           \* socket gone: EBADF
       \/ sfd = 0 /\ rconn' = 0                            \* s_fd = -1: recv fails
    /\ rfd' = sfd /\ rpc' = "held"
    /\ UNCHANGED <<started, stops, open, shut, peerClosed, kind, nconn, authed, sfd, sAuth, hs, hsGen, hsRx, hsFd,
                   hsConn, tpc, tfd, tgen, got, marked, cmdBad, wrongTear, dblClose,
                   silentBlock, kickMarked, foreignShut, deadInst>>

TeardownClient ==   \* teardownClient body, mutex held by the caller
    /\ IF sfd # 0
       THEN IF open[sfd] = 0
            THEN /\ dblClose' = TRUE /\ open' = open /\ wrongTear' = wrongTear
            ELSE /\ open' = [open EXCEPT ![sfd] = 0]
                 /\ dblClose' = dblClose
                 /\ wrongTear' = (wrongTear \/ (rpc = "held" /\ open[sfd] # rconn))
       ELSE /\ open' = open /\ dblClose' = dblClose /\ wrongTear' = wrongTear
    /\ sfd' = (IF BugStaleFd THEN sfd ELSE 0)
    /\ sAuth' = (IF BugFlagSurvive THEN sAuth ELSE FALSE)

TeardownIfCurrent ==    \* take the mutex, close only if s_fd == fd
    /\ rpc = "held"
    /\ rpc' = "idle"
    /\ IF sfd = rfd \/ BugNoFdCheck
       THEN TeardownClient
       ELSE UNCHANGED <<open, sfd, sAuth, dblClose, wrongTear>>
    /\ UNCHANGED <<started, stops, shut, peerClosed, kind, nconn, authed, hs, hsGen, hsRx, hsFd, hsConn,
                   tpc, tfd, tgen, got, marked, rfd, rconn, cmdBad, silentBlock, kickMarked, foreignShut, deadInst>>

\* ---- stopNetConsole / startNetConsole, both on the loop task -----------------------------------------
Stop ==
    /\ started /\ rpc = "idle" /\ stops < MaxStops
    /\ started' = FALSE /\ stops' = stops + 1
    /\ TeardownClient       \* rpc = "idle": no wrongTear judgement
    /\ UNCHANGED <<shut, peerClosed, kind, nconn, authed, hs, hsGen, hsRx, hsFd, hsConn,
                   tpc, tfd, tgen, got, marked, rpc, rfd, rconn, cmdBad, silentBlock, kickMarked, foreignShut, deadInst>>

Start ==
    /\ ~started /\ rpc = "idle"
    /\ started' = TRUE
    /\ UNCHANGED <<stops, open, shut, peerClosed, kind, nconn, authed, sfd, sAuth, hs, hsGen, hsRx, hsFd, hsConn,
                   tpc, tfd, tgen, got, marked, rpc, rfd, rconn, cmdBad, wrongTear, dblClose,
                   silentBlock, kickMarked, foreignShut, deadInst>>

Next == \/ Accept \/ PeerClose \/ ReadData \/ ReadDead \/ TeardownIfCurrent \/ Stop \/ Start
        \/ \E c \in Conns : TRecv(c) \/ TLine(c) \/ TaskStep(c)

\* Fairness: every authTask keeps running (weak, per task).  None on the peers, accept, stop/start.
Spec == Init /\ [][Next]_vars /\ \A c \in Conns : WF_vars(TaskStep(c))

\* ---- properties ------------------------------------------------------------------------------------
TypeOK ==
    /\ open \in [Fds -> 0..MaxConn] /\ shut \subseteq Conns /\ peerClosed \subseteq Conns /\ authed \subseteq Conns
    /\ sfd \in 0..NF /\ hsFd \in 0..NF /\ hsConn \in 0..MaxConn
    /\ hsGen \in 0..MaxConn /\ hsRx \in 0..MaxConn
    /\ \A c \in Conns : tpc[c] \in {"none", "await", "mark", "postok", "postfail", "verified", "fail", "free"}
    /\ rpc \in {"idle", "held"}

AuthedOpen == {f \in Fds : open[f] # 0 /\ open[f] \in authed}

\* the install of a session takeover is the only overlap of two authenticated sockets
OneSession    == Cardinality(AuthedOpen) <= 1 \/ \E c \in Conns : tpc[c] = "verified"
NoUnauthCmd   == ~cmdBad                  \* nothing from an unauthenticated connection reaches the command path
SessionAuthed == (sAuth /\ sfd # 0 /\ open[sfd] # 0) => open[sfd] \in authed
NoWrongTear   == ~wrongTear               \* teardownIfCurrent never closes a session other than the one it judged
NoDoubleClose == ~dblClose                \* no close() on an fd number that is free or belongs to another conn
SessFlag      == sAuth => sfd # 0         \* the session flag does not outlive its socket

\* Slot takeover.  All four are judged on what really happened (ghost truth), not on the rx mark the code reads.
NoSilentBlock == ~silentBlock             \* no connection is turned away because of a peer that never sent a byte
NoKickMarked  == ~kickMarked              \* a holder that has marked rx is never preempted
NoForeignShut == ~foreignShut             \* shutdown() never hits a socket other than the old holder's
NoDeadInstall == ~deadInst                \* a socket that got shutdown() never becomes the session

\* the single handshake slot is never held forever (the deadline is fairness here, see Abstractions)
SlotFree == hs ~> ~hs
=============================================================================
