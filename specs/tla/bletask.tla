---------------------------- MODULE bletask ----------------------------
\* FORMAL-02 wave 3: BLE host side (NimBLE host task / Bluefruit callback task) against the main loop.
\* One model, platform as a constant: Plat = "ESP32" | "NRF52".
\*
\* Code anchors (line numbers of the code BEFORE the BLE-QDROP-01/BLE-STALE-01 fix; the fix adds BleQueueItem.gen,
\*   g_bleQ and bleQueueAccept/bleQueueOnSend in src/ble_session.h, called at the sites below)
\*   ESP32 src/esp32/esp32_main.cpp: BleQueueItem :326, bleQueue :331, bleSessionResetAll :366, onConnect :388,
\*     onDisconnect :420, onWrite :491 (static item, xQueueSend(...,0) :501, return value ignored),
\*     xQueueCreate(5,...) :2567, loop drain xQueueReceive :4855 -> readPhoneCommand (src/phone_commands.cpp:295).
\*   nRF52 src/nrf52/nrf52_ble.cpp: bleuart_rx_callback :394 (g_ble_uart.read + xQueueSend, empty read skipped,
\*     return value ignored), connect_callback :233, disconnect_callback :267, xQueueCreate(5,...) :97;
\*     loop drain src/nrf52/nrf52_main.cpp:2070.  Library: Bluefruit BLEUart.cpp bleuart_rxd_cb :85 writes the
\*     FIFO and calls the rx callback back to back; setRxCallback(deferred = true) runs both in the callback task.
\*   src/ble_session.h bleSessionReset (flags reset inside the connect/disconnect callbacks, BLC-01).
\*   g_task_event_type |= BLE_DATA (nrf52_ble.cpp:398) is never read anywhere (only OR-ed): the RMW "race" has no
\*   consumer; not modelled.
\*
\* Contexts: Host = NimBLE host task / Bluefruit callback task (write, connect, disconnect callbacks, serialized with
\*   each other); Loop = main loop (dequeue, then readPhoneCommand).  Dequeue and apply are two steps: the host can
\*   run between them (the item is "in flight").
\* Abstraction: a phone frame f has Sz[f] byte units <<f,k>>.  Application of an item = set isPhoneReady and record
\*   which session authenticated (every frame stands for a hello).  readPhoneCommand decodes only the first frame
\*   of an item (phone_frame.h phoneFrameDecode: msg_len <= len -> OK, trailing bytes ignored; msg_len > len ->
\*   logged drop).  A logged decoder drop counts as a counted drop.  Pause/resume (s_bleLife), AppLink ownership,
\*   TX path and the unconsumed g_task_event_type are abstracted away.
EXTENDS Naturals, Sequences, FiniteSets

CONSTANTS
    Plat,        \* "ESP32" | "NRF52"
    N,           \* number of phone frames the phone sends over the whole run
    Big,         \* set of frames that are 2 byte units long; all others are 1 unit
    QCap,        \* queue capacity (5 in the code)
    Cap,         \* nRF52: bytes per g_ble_uart.read (MAX_MSG_LEN_PHONE 300 >= any frame; mutation: smaller)
    MaxSess,     \* number of connections modelled
    Deferred,    \* nRF52: FIFO write + rx callback back to back in one task (TRUE in the code); FALSE = mutation
    SkipEmpty,   \* nRF52: an empty FIFO read is not enqueued (TRUE in the code); FALSE = mutation
    CountDrops,  \* queue-full drop is counted (TRUE since BLE-QDROP-01; FALSE = the old ignored xQueueSend result)
    ResetInCb,   \* session flags reset in the connect/disconnect callbacks (TRUE in the code, BLC-01); FALSE = old loop-edge reset
    Fix,         \* "none" (before BLE-STALE-01) | "flush" (xQueueReset in the callbacks) | "tag" (item carries session, loop checks it: the code since BLE-STALE-01, bleQueueAccept in src/ble_session.h)
    CountStale   \* a stale item / flushed item dropped by Fix is counted

Sz == [f \in 1..N |-> IF f \in Big THEN 2 ELSE 1]
Frames == 1..N
Units(f) == [k \in 1..Sz[f] |-> <<f, k>>]
FramesOf(s) == {s[i][1] : i \in DOMAIN s}
Min(a, b) == IF a < b THEN a ELSE b

VARIABLES queue, infl, fifo, cbp, connected, sess, ready, authSess, oldConn,
          nxt, acc, proc, drp, torn, staleApplied, emptyItem

vars == <<queue, infl, fifo, cbp, connected, sess, ready, authSess, oldConn,
          nxt, acc, proc, drp, torn, staleApplied, emptyItem>>

Init == /\ queue = <<>> /\ infl = <<>> /\ fifo = <<>> /\ cbp = 0
        /\ connected = FALSE /\ sess = 0 /\ ready = FALSE /\ authSess = 0 /\ oldConn = FALSE
        /\ nxt = 1 /\ acc = {} /\ proc = {} /\ drp = {}
        /\ torn = FALSE /\ staleApplied = FALSE /\ emptyItem = FALSE

Pending == UNION {FramesOf(queue[i].us) : i \in DOMAIN queue}
           \cup FramesOf(fifo)
           \cup UNION {FramesOf(infl[i].us) : i \in DOMAIN infl}

CountedAdd(S, count) == IF count THEN drp \cup S ELSE drp

\* ---- Host task / callback task ------------------------------------------------------------------------

ResetFlags == /\ ready' = FALSE /\ authSess' = 0

HostConnect ==
    /\ ~connected /\ sess < MaxSess
    /\ connected' = TRUE /\ sess' = sess + 1
    /\ IF ResetInCb THEN ResetFlags ELSE UNCHANGED <<ready, authSess>>
    /\ IF Fix = "flush"
          THEN /\ queue' = <<>> /\ drp' = CountedAdd(UNION {FramesOf(queue[i].us) : i \in DOMAIN queue}, CountStale)
          ELSE UNCHANGED <<queue, drp>>
    /\ UNCHANGED <<infl, fifo, cbp, oldConn, nxt, acc, proc, torn, staleApplied, emptyItem>>

HostDisconnect ==
    /\ connected
    /\ connected' = FALSE
    /\ IF ResetInCb THEN ResetFlags ELSE UNCHANGED <<ready, authSess>>
    /\ IF Fix = "flush"
          THEN /\ queue' = <<>> /\ drp' = CountedAdd(UNION {FramesOf(queue[i].us) : i \in DOMAIN queue}, CountStale)
          ELSE UNCHANGED <<queue, drp>>
    /\ UNCHANGED <<infl, fifo, cbp, sess, oldConn, nxt, acc, proc, torn, staleApplied, emptyItem>>

\* xQueueSend(bleQueue, &item, 0) of one item
Enqueue(us) ==
    IF Len(queue) < QCap
       THEN /\ queue' = Append(queue, [us |-> us, sess |-> sess]) /\ drp' = drp
       ELSE /\ queue' = queue /\ drp' = CountedAdd(FramesOf(us), CountDrops)

\* bleuart_rx_callback: read up to Cap units from the FIFO, skip an empty read, enqueue
ReadN(f) == Min(Cap, Len(f))

\* a phone write (ESP32: onWrite one item; nRF52: FIFO write [+ callback if Deferred])
HostWrite ==
    /\ connected /\ nxt <= N
    /\ nxt' = nxt + 1 /\ acc' = acc \cup {nxt}
    /\ IF Plat = "ESP32"
          THEN /\ Enqueue(Units(nxt))
               /\ UNCHANGED <<fifo, cbp, ready, authSess>>
          ELSE IF Deferred
          THEN LET f1 == fifo \o Units(nxt)
                   n  == ReadN(f1)
               IN /\ fifo' = SubSeq(f1, n + 1, Len(f1))
                  /\ IF n = 0 /\ SkipEmpty
                        THEN UNCHANGED <<queue, drp>>
                        ELSE Enqueue(SubSeq(f1, 1, n))
                  /\ UNCHANGED <<cbp, ready, authSess>>
          ELSE /\ fifo' = fifo \o Units(nxt) /\ cbp' = cbp + 1
               /\ UNCHANGED <<queue, drp, ready, authSess>>
    /\ UNCHANGED <<infl, connected, sess, oldConn, proc, torn, staleApplied, emptyItem>>

\* separate callback (Deferred = FALSE only)
HostCb ==
    /\ Plat = "NRF52" /\ ~Deferred /\ cbp > 0
    /\ LET n == ReadN(fifo)
       IN /\ cbp' = cbp - 1
          /\ fifo' = SubSeq(fifo, n + 1, Len(fifo))
          /\ IF n = 0 /\ SkipEmpty
                THEN UNCHANGED <<queue, drp>>
                ELSE Enqueue(SubSeq(fifo, 1, n))
    /\ UNCHANGED <<infl, connected, sess, ready, authSess, oldConn, nxt, acc, proc, torn, staleApplied, emptyItem>>

\* ---- Main loop ----------------------------------------------------------------------------------------

\* old code (BLC-01 before): reset only on a connected -> disconnected edge seen by the loop
LoopEdge ==
    /\ ~ResetInCb
    /\ oldConn' = connected
    /\ IF oldConn /\ ~connected THEN ResetFlags ELSE UNCHANGED <<ready, authSess>>
    /\ UNCHANGED <<queue, infl, fifo, cbp, connected, sess, nxt, acc, proc, drp, torn, staleApplied, emptyItem>>

LoopDequeue ==
    /\ Len(queue) > 0 /\ infl = <<>>
    /\ infl' = <<Head(queue)>> /\ queue' = Tail(queue)
    /\ UNCHANGED <<fifo, cbp, connected, sess, ready, authSess, oldConn, nxt, acc, proc, drp, torn, staleApplied, emptyItem>>

\* readPhoneCommand(item.data, item.length)
LoopApply ==
    /\ Len(infl) = 1
    /\ LET it == infl[1]
           us == it.us
           f  == IF Len(us) > 0 THEN us[1][1] ELSE 0
           exact == Len(us) > 0 /\ us = Units(f)
           okFrame == Len(us) > 0 /\ us[1][2] = 1 /\ Len(us) >= Sz[f]
           stale == Fix = "tag" /\ (it.sess # sess \/ ~connected)
       IN /\ infl' = <<>>
          /\ torn' = (torn \/ ~exact)
          /\ emptyItem' = (emptyItem \/ Len(us) = 0)
          /\ IF stale
                THEN /\ drp' = CountedAdd(FramesOf(us), CountStale)
                     /\ UNCHANGED <<proc, ready, authSess, staleApplied>>
                ELSE IF okFrame
                THEN /\ proc' = proc \cup {f}
                     /\ ready' = TRUE /\ authSess' = it.sess
                     /\ staleApplied' = (staleApplied \/ it.sess # sess \/ ~connected)
                     /\ UNCHANGED drp
                ELSE /\ drp' = drp \cup FramesOf(us)      \* "Frame dropped" is logged
                     /\ UNCHANGED <<proc, ready, authSess, staleApplied>>
    /\ UNCHANGED <<queue, fifo, cbp, connected, sess, oldConn, nxt, acc>>

Next == HostConnect \/ HostDisconnect \/ HostWrite \/ HostCb \/ LoopEdge \/ LoopDequeue \/ LoopApply

Spec == Init /\ [][Next]_vars

\* ---- Invariants ---------------------------------------------------------------------------------------

TypeOK == /\ Len(queue) <= QCap /\ Len(infl) <= 1
          /\ connected \in BOOLEAN /\ ready \in BOOLEAN
          /\ sess \in 0..MaxSess /\ authSess \in 0..MaxSess
          /\ nxt \in 1..(N + 1)
          /\ acc \subseteq Frames /\ proc \subseteq Frames /\ drp \subseteq Frames
          /\ cbp \in Nat

\* Code meaning: every frame the BLE stack handed to onWrite / the rx callback ends up processed by
\* readPhoneCommand, in a counted/logged drop, or is still queued (queue, FIFO, the item the loop holds).
\* A silent xQueueSend failure, a silently ignored trailing frame, or a silent flush violates it.
NoLostFrame == acc \subseteq (proc \cup drp \cup Pending)

\* Code meaning: every item readPhoneCommand receives is exactly one phone frame (no split over two items,
\* no two frames in one item, no empty item).
NoTornFrame == ~torn

\* Code meaning: no empty item (len 0) is handed to readPhoneCommand.
NoEmptyItem == ~emptyItem

\* Code meaning: an item queued in connection k is never applied in connection k+1, and a hello applied in
\* connection k does not leave isPhoneReady set in connection k+1 (the new central must say hello itself); isPhoneReady is never set without a link.
NoStaleSession == ~staleApplied /\ (ready => (connected /\ authSess = sess))

=============================================================================
