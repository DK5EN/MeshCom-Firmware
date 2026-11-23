--------------------------- MODULE netconsole ---------------------------
\* FORMAL-01, F2-10: the ESP32 network console (src/net_console.cpp) -- one listening socket, ONE handshake
\* slot (s_hs_running), HMAC challenge/response in authTask, one session socket s_fd, a takeover by a new
\* authenticated client, stop/start, teardown from the loop task.  Everything under s_mutex is one atomic
\* action; what runs outside it (auth-phase recv, the gap between the reader's recv and teardownIfCurrent)
\* is a separate step.
\*
\* Code anchors (src/net_console.cpp):
\*   state s_fd :61 s_authenticated :65 s_hs_running :66 s_mutex :62 (created once :387)
\*   teardownClient :118-127 (clears flag, closes s_fd)   teardownIfCurrent :131-137 (s_fd == fd check :135)
\*   authTask :144-262: no-password grant :157, NONCE send :168, recv loop :183-196 under a 10 s total deadline
\*     (:175, :183) WITHOUT the mutex, HMAC compare :205-215, FAIL + close :219-227, banner :229,
\*     install under the mutex :236-249 (takeover: close old s_fd :243, s_fd=fd :244, s_authenticated :246),
\*     console stopped meanwhile -> drop :240/:253
\*   stopNetConsole :392-410 (teardownClient :405-407)   startNetConsole :383-390
\*   loopNetConsole accept :490, reject while handshake runs :507-510, spawn authTask :521-527,
\*     dead-session probe (recv MSG_PEEK on s_fd read without the lock) :472-480
\*   netConsoleRead :536-559 (recv under the mutex :549-553, teardownIfCurrent outside it :558),
\*   netConsoleAvailable :563-584   command path = whatever netConsoleRead returns to the loop task.
\*
\* Abstractions: a connection is a conn id (1..MaxConn) with a behaviour fixed at accept (good = answers the
\* challenge correctly, bad = wrong response, slow = never answers); fd numbers come from NF slots, with
\* lowest-free reuse like lwIP when Reuse; one reader (loop task: accept, reads, probes and stop/start all run
\* there, hence they do not interleave with each other -- AcceptAnyTime drops that); data on a session socket
\* is always available; no password is the same as a password for the handshake (the no-password grant :157 is
\* a configuration, not a race); TCP buffering, keepalive and the 90 s peer-vanished detection are the peer's
\* close; foreign sockets (web server, OTA) taking the same fd number are not modelled.
EXTENDS Naturals, FiniteSets

CONSTANTS
    MaxConn,            \* connections over the whole run
    NF,                 \* fd numbers
    Reuse,              \* TRUE: a new socket gets the lowest free fd number (lwIP); FALSE: numbers are never reused
    MaxStops,           \* stop/start cycles
    HasDeadline,        \* TRUE: the handshake has the 10 s total deadline (:175-183)
    AcceptAnyTime,      \* TRUE: accept may run while the loop task sits between recv and teardownIfCurrent
    BugNoFdCheck,       \* teardownIfCurrent without the s_fd == fd test (:135)
    BugFlagSurvive,     \* teardown leaves s_authenticated set (:120)
    BugNoReset,         \* a new connection becomes s_fd at accept, before auth, s_authenticated not reset
    BugNoClose,         \* the takeover does not close the old socket (:243)
    BugStaleFd,         \* teardown closes s_fd but does not reset it (:122)
    Takeover,           \* FIX F2-10-SLOT: a new connection takes the handshake slot from a peer that has sent no byte for N s
    BugPreemptActive,   \* mutation: the takeover also preempts a handshake whose peer is sending
    SilentFirst         \* scenario: conn 1 is a silent attacker (never sends), conn 2 an honest client

VARIABLES
    started,            \* s_started
    open,               \* open[fd] = conn id holding that fd, 0 = free
    kind,               \* kind[c]
    nconn, peerClosed, authed,      \* ghost: connections so far, peer-closed conns, conns that passed the HMAC
    sfd, sAuth,         \* s_fd (0 = -1), s_authenticated
    hs, apc, afd, aconn,            \* s_hs_running, authTask state, its fd and conn
    rpc, rfd, rconn,    \* reader between recv and teardownIfCurrent
    stops,
    sent,               \* the handshake peer has sent at least one byte (s_hs_rx)
    cmdBad, wrongTear, dblClose, rejGood, kickAct    \* ghost verdict flags (kickAct: a sending handshake was preempted)

vars == <<sent, kickAct, started, open, kind, nconn, peerClosed, authed, sfd, sAuth, hs, apc, afd, aconn, rpc, rfd, rconn, stops,
          cmdBad, wrongTear, dblClose, rejGood>>

Conns == 1..MaxConn
Fds == 1..NF

Init ==
    /\ started = TRUE /\ open = [f \in Fds |-> 0] /\ kind = [c \in Conns |-> "good"]
    /\ nconn = 0 /\ peerClosed = {} /\ authed = {}
    /\ sfd = 0 /\ sAuth = FALSE /\ hs = FALSE /\ apc = "none" /\ afd = 0 /\ aconn = 0
    /\ rpc = "idle" /\ rfd = 0 /\ rconn = 0 /\ stops = 0
    /\ sent = FALSE /\ kickAct = FALSE
    /\ cmdBad = FALSE /\ wrongTear = FALSE /\ dblClose = FALSE /\ rejGood = FALSE

FreeFds == {f \in Fds : open[f] = 0}
LowFree == CHOOSE f \in FreeFds : \A g \in FreeFds : f <= g

\* ---- loop task: accept :490-:527 --------------------------------------------------------------------
PreemptOK == Takeover /\ hs /\ apc = "await" /\ (~sent \/ BugPreemptActive)
    \* new rule (code: netConsoleSlotMayPreempt): the slot holder has sent no byte for N = 3 s (time abstracted: any
    \* moment while ~sent); the old socket is shut down and its task unwinds (abstracted to one atomic close here)

Accept ==
    /\ started /\ nconn < MaxConn
    /\ (rpc = "idle" \/ AcceptAnyTime)
    /\ LET base == IF PreemptOK THEN [open EXCEPT ![afd] = 0] ELSE open
           free == {g \in Fds : base[g] = 0}
           c == nconn + 1
       IN /\ free # {}
          /\ \E k \in (IF SilentFirst THEN (IF c = 1 THEN {"slow"} ELSE {"good"}) ELSE {"good", "bad", "slow"}) :
             LET f == IF Reuse THEN CHOOSE g \in free : \A h \in free : g <= h ELSE c
             IN /\ f \in free
                /\ nconn' = c /\ kind' = [kind EXCEPT ![c] = k]
                /\ IF hs /\ ~PreemptOK
                   THEN \* :507-510 rejected: "auth already in progress", socket closed at once
                        /\ open' = open
                        /\ rejGood' = (rejGood \/ k = "good")
                        /\ UNCHANGED <<hs, apc, afd, aconn, sfd, sent, kickAct>>
                   ELSE \* :521-523 slot taken (or taken over), authTask spawned
                        /\ open' = [base EXCEPT ![f] = c]
                        /\ hs' = TRUE /\ apc' = "await" /\ afd' = f /\ aconn' = c
                        /\ sent' = FALSE
                        /\ kickAct' = (kickAct \/ (hs /\ sent))
                        /\ sfd' = IF BugNoReset THEN f ELSE sfd     \* mutation: connection installed before auth
                        /\ UNCHANGED rejGood
    /\ UNCHANGED <<started, peerClosed, authed, sAuth, rpc, rfd, rconn, stops, cmdBad, wrongTear, dblClose>>

\* ---- a peer hangs up whenever it likes -------------------------------------------------------------
PeerClose ==
    /\ \E f \in Fds : open[f] # 0 /\ open[f] \notin peerClosed /\ peerClosed' = peerClosed \cup {open[f]}
    /\ UNCHANGED <<sent, kickAct, started, open, kind, nconn, authed, sfd, sAuth, hs, apc, afd, aconn, rpc, rfd, rconn, stops,
                   cmdBad, wrongTear, dblClose, rejGood>>

\* ---- authTask, outside the mutex -------------------------------------------------------------------
Send ==         \* :189 the first byte of the peer arrives (s_hs_rx); a silent peer never sends
    /\ apc = "await" /\ ~sent /\ kind[aconn] # "slow" /\ aconn \notin peerClosed
    /\ sent' = TRUE
    /\ UNCHANGED <<kickAct, started, open, kind, nconn, peerClosed, authed, sfd, sAuth, hs, apc, afd, aconn, rpc, rfd,
                   rconn, stops, cmdBad, wrongTear, dblClose, rejGood>>

Verify ==       \* :189-215 the peer answers correctly, HMAC compare ok, banner :229
    /\ apc = "await" /\ sent /\ kind[aconn] = "good" /\ aconn \notin peerClosed
    /\ apc' = "verified" /\ authed' = authed \cup {aconn}
    /\ UNCHANGED <<sent, kickAct, started, open, kind, nconn, peerClosed, sfd, sAuth, hs, afd, aconn, rpc, rfd, rconn, stops,
                   cmdBad, wrongTear, dblClose, rejGood>>

AuthFail ==     \* :219-227 wrong response, EOF (:190 r <= 0) or the total deadline (:183); FAIL, close, slot freed
    /\ apc = "await"
    /\ (kind[aconn] = "bad" \/ aconn \in peerClosed \/ HasDeadline)
    /\ open' = [open EXCEPT ![afd] = 0]
    /\ hs' = FALSE /\ apc' = "none"
    /\ UNCHANGED <<sent, kickAct, started, kind, nconn, peerClosed, authed, sfd, sAuth, afd, aconn, rpc, rfd, rconn, stops,
                   cmdBad, wrongTear, dblClose, rejGood>>

Install ==      \* :236-261 under the mutex
    /\ apc = "verified"
    /\ IF started
       THEN \* takeover: :243 close old s_fd, :244-246 s_fd, s_peek_valid, s_authenticated
            /\ IF sfd # 0 /\ ~BugNoClose
               THEN IF open[sfd] = 0
                    THEN /\ dblClose' = TRUE /\ open' = open
                    ELSE /\ open' = [open EXCEPT ![sfd] = 0] /\ UNCHANGED dblClose
               ELSE /\ open' = open /\ UNCHANGED dblClose
            /\ sfd' = afd /\ sAuth' = TRUE
       ELSE \* :240 console stopped meanwhile: :253 close, session dropped
            /\ open' = [open EXCEPT ![afd] = 0]
            /\ UNCHANGED <<sfd, sAuth, dblClose>>
    /\ hs' = FALSE /\ apc' = "none"
    /\ UNCHANGED <<sent, kickAct, started, kind, nconn, peerClosed, authed, afd, aconn, rpc, rfd, rconn, stops, cmdBad, wrongTear,
                   rejGood>>

\* ---- the loop task reads the session: recv under the mutex, the verdict acts after releasing it -------
ReadData ==     \* :549-553 r == 1, the byte goes to the command path
    /\ rpc = "idle" /\ sAuth /\ sfd # 0 /\ open[sfd] # 0 /\ open[sfd] \notin peerClosed
    /\ cmdBad' = (cmdBad \/ open[sfd] \notin authed)
    /\ UNCHANGED <<sent, kickAct, started, open, kind, nconn, peerClosed, authed, sfd, sAuth, hs, apc, afd, aconn, rpc, rfd, rconn,
                   stops, wrongTear, dblClose, rejGood>>

ReadDead ==     \* :549-558 / :472-479 recv says EOF or an error on the fd read from s_fd; the lock is released
    /\ rpc = "idle" /\ sAuth
    /\ \/ sfd # 0 /\ open[sfd] # 0 /\ open[sfd] \in peerClosed /\ rconn' = open[sfd]
       \/ sfd # 0 /\ open[sfd] = 0 /\ rconn' = 0           \* socket gone: EBADF
       \/ sfd = 0 /\ rconn' = 0                            \* s_fd = -1: recv fails
    /\ rfd' = sfd /\ rpc' = "held"
    /\ UNCHANGED <<sent, kickAct, started, open, kind, nconn, peerClosed, authed, sfd, sAuth, hs, apc, afd, aconn, stops,
                   cmdBad, wrongTear, dblClose, rejGood>>

TeardownClient ==   \* :118-127 body, mutex held by the caller; rc = the conn the caller meant (0 = any)
    /\ IF sfd # 0
       THEN IF open[sfd] = 0
            THEN /\ dblClose' = TRUE /\ open' = open /\ UNCHANGED wrongTear
            ELSE /\ open' = [open EXCEPT ![sfd] = 0]
                 /\ UNCHANGED dblClose
                 /\ wrongTear' = (wrongTear \/ (rpc = "held" /\ open[sfd] # rconn))
       ELSE /\ open' = open /\ UNCHANGED <<dblClose, wrongTear>>
    /\ sfd' = IF BugStaleFd THEN sfd ELSE 0
    /\ sAuth' = IF BugFlagSurvive THEN sAuth ELSE FALSE

TeardownIfCurrent ==    \* :131-137: take the mutex, close only if s_fd == fd
    /\ rpc = "held"
    /\ rpc' = "idle"
    /\ IF sfd = rfd \/ BugNoFdCheck
       THEN TeardownClient
       ELSE UNCHANGED <<open, sfd, sAuth, dblClose, wrongTear>>
    /\ UNCHANGED <<sent, kickAct, started, kind, nconn, peerClosed, authed, hs, apc, afd, aconn, rfd, rconn, stops, cmdBad, rejGood>>

\* ---- stopNetConsole :392-410 / startNetConsole :383-390, both on the loop task ---------------------------
Stop ==
    /\ started /\ rpc = "idle" /\ stops < MaxStops
    /\ started' = FALSE /\ stops' = stops + 1
    /\ TeardownClient       \* rpc = "idle": no wrongTear judgement
    /\ UNCHANGED <<sent, kickAct, kind, nconn, peerClosed, authed, hs, apc, afd, aconn, rpc, rfd, rconn, cmdBad, rejGood>>

Start ==
    /\ ~started /\ rpc = "idle"
    /\ started' = TRUE
    /\ UNCHANGED <<sent, kickAct, open, kind, nconn, peerClosed, authed, sfd, sAuth, hs, apc, afd, aconn, rpc, rfd, rconn, stops,
                   cmdBad, wrongTear, dblClose, rejGood>>

Next == Accept \/ Send \/ PeerClose \/ Verify \/ AuthFail \/ Install \/ ReadData \/ ReadDead \/ TeardownIfCurrent \/ Stop \/ Start

Spec == Init /\ [][Next]_vars /\ WF_vars(AuthFail) /\ WF_vars(Install)

\* ---- properties ------------------------------------------------------------------------------------
AuthedOpen == {f \in Fds : open[f] # 0 /\ open[f] \in authed}

OneSession    == Cardinality(AuthedOpen) <= 1 \/ apc = "verified"   \* the install of a takeover is the only overlap
NoUnauthCmd   == ~cmdBad                  \* nothing from an unauthenticated connection reaches the command path
SessionAuthed == (sAuth /\ sfd # 0 /\ open[sfd] # 0) => open[sfd] \in authed
NoWrongTear   == ~wrongTear               \* teardownIfCurrent never closes a session other than the one it judged
NoDoubleClose == ~dblClose                \* no close() on an fd that is already closed
SessFlag      == sAuth => sfd # 0         \* the session flag does not outlive its socket
NoRejectGood  == ~rejGood                 \* an honest client is never turned away because the slot is busy

NoKickActive  == ~kickAct                 \* a handshake whose peer is sending is never preempted

SlotFree == hs ~> ~hs                     \* the single handshake slot is never held forever
=============================================================================
