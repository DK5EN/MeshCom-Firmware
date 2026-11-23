---------------------------- MODULE rxflags ----------------------------
\* FORMAL-01: ESP32 radio event flags (concurrency map F2-7): the DIO1 ISRs against the loop state machine.
\*
\* Code anchors (src/esp32/esp32_main.cpp unless noted):
\*   ISRs setFlagReceive :1078-1090 and setFlagSent :1097-1110: `if(E) R = true; if(ET) T = true;` (check, then set,
\*     two separate atomics; identical bodies).  E = bEnableInterruptReceive :1057, R = receiveFlag :1056,
\*     ET = bEnableInterruptTransmit, T = transmittedFlag.
\*   loop dispatch :3941 `if(receiveFlag || transmittedFlag)`, RX branch :3944; clears E=false :3954, R=false :3955;
\*     checkRX() :3965 (read :5732-5790, RX restart inside the read: detach :5785-5786, startReceive :5791,
\*     attach :5792, E=true :5793, DIO1-level recovery :5794-5799 "missed_edge");
\*   TX-done branch :3969-4046: E=false :3975, ET=false, T=false, OnTxDone, restart RX :4023-4040 (E=true :4032,
\*     recovery :4036-4041);
\*   RX timeout restart :3883-3907 (E=false :3883, startReceive, E=true :3901, recovery :3905-3908);
\*   TX prepare :4076-4204: getIrqFlags header/preamble check -> abort :4094-4108, E=false + detach :4116-4117,
\*     CAD :4150-4166, CAD busy: restore RX :4240, CAD free: ET=true :4171, setPacketSentAction :4172, doTX :4177;
\*   park service loraRxParkService :6053-6081 (web wait path): gate rxParkShouldService lora_rx_park.h:42,
\*     txBusy = ET || tx_is_active || T :6059, clears E,R :6065-6066; drain loraRxParkDrain :6018-6043.
\*
\* Abstractions: one chip, one packet in flight at a time; ISR = atomic step triggered by the chip event (Done / TxDone)
\* and by nothing else; the loop is a program counter whose every flag-touching statement is its own step.  The
\* untimed interleaving lets the loop stall arbitrarily between two statements; NoStall=TRUE forbids the packet end
\* (Done) while the loop sits between the IRQ-flag check and the gate close (airtime >= tens of ms vs microseconds).
\* Fairness: none; "no lost DIO1 event" is checked as the safety predicate NoDeaf (a packet latched in the chip,
\* loop idle, radio in RX, and no flag set, i.e. nothing will ever look at the chip again until the timeout restart,
\* which clears the chip IRQ and so loses the packet).  The radio read, startReceive and the processing are atomic
\* statements except where the flag order matters.
EXTENDS Naturals

CONSTANTS
    NF,        \* frames the air offers
    NTx,       \* TX requests the application queues
    Recovery,  \* BOOLEAN: DIO1-level recovery after E=true (FALSE = mutation: drop it)
    ClearLate, \* BOOLEAN: R=false only after the RX restart (TRUE = mutation: the clear overwrites a later set)
    IrqGuard,  \* BOOLEAN: getIrqFlags abort before TX (FALSE = mutation)
    NoStall,   \* BOOLEAN: no stall between the IRQ check and the gate close
    ParkGate,  \* BOOLEAN: ringFull leaves the packet on the chip (FALSE = mutation: park service reads regardless)
    RingMax,   \* park ring slots (abstract, RX_PARK_SLOTS = 4)
    Timeout,   \* BOOLEAN: model the periodic RX timeout restart
    ReEnable   \* BOOLEAN: E=true after every RX restart (FALSE = mutation: the gate stays shut)

VARIABLES E, R, ET, T, att, mode, air, pkt, txdone, pc, kind, ring, arr, txq,
          processed, silent, blind, rxInTx

vars == <<E, R, ET, T, att, mode, air, pkt, txdone, pc, kind, ring, arr, txq, processed, silent, blind, rxInTx>>

Init == /\ E = TRUE /\ R = FALSE /\ ET = FALSE /\ T = FALSE
        /\ att = "rx" /\ mode = "rx" /\ air = "none" /\ pkt = FALSE /\ txdone = FALSE
        /\ pc = "idle" /\ kind = "loop" /\ ring = 0 /\ arr = 0 /\ txq = NTx
        /\ processed = 0 /\ silent = 0 /\ blind = 0 /\ rxInTx = FALSE

\* The ISR body (both ISRs): setFlagReceive/setFlagSent :1082-1095, :1100-1109.
Isr == /\ IF att # "none" /\ E THEN R' = TRUE ELSE R' = R
       /\ IF att # "none" /\ ET THEN T' = TRUE ELSE T' = T

\* A packet starts on the air (only while the chip is in RX).
AirStart == /\ mode = "rx" /\ air = "none" /\ ~pkt /\ arr < NF
            /\ air' = "inflight" /\ arr' = arr + 1
            /\ UNCHANGED <<E, R, ET, T, att, mode, pkt, txdone, pc, kind, ring, txq, processed, silent, blind, rxInTx>>
\* RxDone: IRQ latched in the chip, DIO1 rises, ISR runs atomically.
AirDone == /\ air = "inflight" /\ mode = "rx"
           /\ ~(NoStall /\ pc = "txB")
           /\ air' = "none" /\ pkt' = TRUE /\ Isr
           /\ UNCHANGED <<E, ET, att, mode, txdone, pc, kind, ring, arr, txq, processed, silent, blind, rxInTx>>
\* TxDone: the chip finishes the transmission, DIO1 rises, ISR runs.
TxDoneEv == /\ mode = "tx" /\ ~txdone
            /\ txdone' = TRUE /\ Isr
            /\ UNCHANGED <<E, ET, att, mode, air, pkt, pc, kind, ring, arr, txq, processed, silent, blind, rxInTx>>

\* ---- loop dispatch :3941 ----
LDispatch == /\ pc = "idle"
             /\ IF R THEN pc' = "rxE" ELSE IF T THEN pc' = "tE" ELSE IF ring > 0 THEN pc' = "drain"
                ELSE IF txq > 0 THEN pc' = "txA" ELSE IF Timeout /\ mode = "rx" THEN pc' = "tmo" ELSE pc' = "idle"
             /\ kind' = "loop"
             /\ UNCHANGED <<E, R, ET, T, att, mode, air, pkt, txdone, ring, arr, txq, processed, silent, blind, rxInTx>>
\* loraRxParkService from a web wait, at any loop idle point :6061
ParkSvc == /\ pc = "idle" /\ R /\ ~ET /\ ~T /\ (~ParkGate \/ ring < RingMax)
           /\ E' = FALSE /\ R' = FALSE /\ pc' = "rxRead" /\ kind' = "park"
           /\ UNCHANGED <<ET, T, att, mode, air, pkt, txdone, ring, arr, txq, processed, silent, blind, rxInTx>>
\* loraRxParkDrain :6020-6040: one parked packet processed
Drain == /\ pc = "drain" /\ ring' = ring - 1 /\ processed' = processed + 1 /\ pc' = "idle"
         /\ UNCHANGED <<E, R, ET, T, att, mode, air, pkt, txdone, kind, arr, txq, silent, blind, rxInTx>>

\* ---- RX branch :3954-3965 ----
RxE == /\ pc = "rxE" /\ E' = FALSE /\ pc' = "rxR"
       /\ UNCHANGED <<R, ET, T, att, mode, air, pkt, txdone, kind, ring, arr, txq, processed, silent, blind, rxInTx>>
RxR == /\ pc = "rxR" /\ IF ClearLate THEN R' = R ELSE R' = FALSE
       /\ pc' = "rxRead"
       /\ UNCHANGED <<E, ET, T, att, mode, air, pkt, txdone, kind, ring, arr, txq, processed, silent, blind, rxInTx>>
\* checkRXRead :5732: reads whatever the chip holds now.  Reading while the chip transmits is a violation.
RxRead == /\ pc = "rxRead"
          /\ rxInTx' = (rxInTx \/ mode = "tx")
          /\ IF pkt
                THEN IF kind = "park"
                        THEN IF ring < RingMax THEN /\ ring' = ring + 1 /\ silent' = silent /\ processed' = processed
                             ELSE /\ ring' = ring /\ silent' = silent + 1 /\ processed' = processed
                        ELSE /\ ring' = ring /\ silent' = silent /\ processed' = processed + 1
                ELSE /\ ring' = ring /\ silent' = silent /\ processed' = processed
          /\ pkt' = FALSE
          /\ pc' = "rA"
          /\ UNCHANGED <<E, R, ET, T, att, mode, air, txdone, kind, arr, txq, blind>>

\* ---- RX restart sequence, shared by RX, TX-done, timeout, CAD busy :5785-5799, :4023-4041, :3883-3907 ----
RA == /\ pc = "rA" /\ att' = "none" /\ pc' = "rB"
      /\ UNCHANGED <<E, R, ET, T, mode, air, pkt, txdone, kind, ring, arr, txq, processed, silent, blind, rxInTx>>
\* startReceive: the chip leaves TX/RX, clears its IRQs (a packet latched or in flight is lost: hardware blind window)
RB == /\ pc = "rB" /\ mode' = "rx" /\ pkt' = FALSE /\ txdone' = FALSE /\ air' = "none"
      /\ blind' = blind + (IF pkt \/ air = "inflight" THEN 1 ELSE 0)
      /\ pc' = "rC"
      /\ UNCHANGED <<E, R, ET, T, att, kind, ring, arr, txq, processed, silent, rxInTx>>
RC == /\ pc = "rC" /\ att' = "rx" /\ pc' = "rD"
      /\ UNCHANGED <<E, R, ET, T, mode, air, pkt, txdone, kind, ring, arr, txq, processed, silent, blind, rxInTx>>
RD == /\ pc = "rD" /\ E' = (IF ReEnable THEN TRUE ELSE E) /\ pc' = "rE"
      /\ UNCHANGED <<R, ET, T, att, mode, air, pkt, txdone, kind, ring, arr, txq, processed, silent, blind, rxInTx>>
\* digitalRead(LORA_DIO1) == HIGH -> receiveFlag = true  :5794-5799
RE == /\ pc = "rE"
      /\ IF Recovery /\ pkt THEN R' = TRUE ELSE R' = R
      /\ pc' = IF ClearLate /\ kind \in {"loop", "park"} THEN "rF" ELSE "idle"
      /\ UNCHANGED <<E, ET, T, att, mode, air, pkt, txdone, kind, ring, arr, txq, processed, silent, blind, rxInTx>>
\* mutation ClearLate: the receiveFlag clear that today sits at :3955 runs after the restart
RF == /\ pc = "rF" /\ R' = FALSE /\ pc' = "idle"
      /\ UNCHANGED <<E, ET, T, att, mode, air, pkt, txdone, kind, ring, arr, txq, processed, silent, blind, rxInTx>>

\* ---- TX-done branch :3969-4046 ----
TE == /\ pc = "tE" /\ ET' = FALSE /\ E' = FALSE /\ pc' = "tT"
      /\ UNCHANGED <<R, T, att, mode, air, pkt, txdone, kind, ring, arr, txq, processed, silent, blind, rxInTx>>
TT == /\ pc = "tT" /\ T' = FALSE /\ pc' = "rA"
      /\ UNCHANGED <<E, R, ET, att, mode, air, pkt, txdone, kind, ring, arr, txq, processed, silent, blind, rxInTx>>

\* ---- RX timeout restart :3883 ----
\* The code does not look at receiveFlag here (esp32_main.cpp, "FIX BUG #1" block): a flag the ISR set after the
\* dispatch is left standing and the restart clears the chip IRQ under it (counted in blind).  An earlier version
\* of this model guarded the step with ~R, which left pc stuck at "tmo" and cut every behaviour behind it.
Tmo == /\ pc = "tmo" /\ E' = FALSE /\ pc' = "rA"
       /\ UNCHANGED <<R, ET, T, att, mode, air, pkt, txdone, kind, ring, arr, txq, processed, silent, blind, rxInTx>>

\* ---- TX prepare :4076-4204 ----
\* getIrqFlags: header/preamble (and the latched RxDone) seen -> abort TX :4094-4108
TxA == /\ pc = "txA"
       /\ IF IrqGuard /\ (air = "inflight" \/ pkt) THEN pc' = "idle" ELSE pc' = "txB"
       /\ UNCHANGED <<E, R, ET, T, att, mode, air, pkt, txdone, kind, ring, arr, txq, processed, silent, blind, rxInTx>>
TxB == /\ pc = "txB" /\ E' = FALSE /\ att' = "none" /\ pc' = "txD"        \* :4116-4117
       /\ UNCHANGED <<R, ET, T, mode, air, pkt, txdone, kind, ring, arr, txq, processed, silent, blind, rxInTx>>
\* CAD scan :4150-4166: busy -> restore RX path :4240; free -> :4171
TxD == /\ pc = "txD"
       /\ IF air = "inflight" THEN pc' = "rA" ELSE pc' = "txE"
       /\ UNCHANGED <<E, R, ET, T, att, mode, air, pkt, txdone, kind, ring, arr, txq, processed, silent, blind, rxInTx>>
TxE == /\ pc = "txE" /\ ET' = TRUE /\ att' = "tx" /\ pc' = "txG"           \* :4171-4172
       /\ UNCHANGED <<E, R, T, mode, air, pkt, txdone, kind, ring, arr, txq, processed, silent, blind, rxInTx>>
\* doTX :4177: the chip goes to TX; whatever it held is lost
TxG == /\ pc = "txG" /\ mode' = "tx" /\ txdone' = FALSE /\ pkt' = FALSE /\ air' = "none"
       /\ blind' = blind + (IF pkt \/ air = "inflight" THEN 1 ELSE 0)
       /\ txq' = txq - 1 /\ pc' = "idle"
       /\ UNCHANGED <<E, R, ET, T, att, kind, ring, arr, processed, silent, rxInTx>>

Next == AirStart \/ AirDone \/ TxDoneEv \/ LDispatch \/ ParkSvc \/ Drain \/ RxE \/ RxR \/ RxRead
        \/ RA \/ RB \/ RC \/ RD \/ RE \/ RF \/ TE \/ TT \/ Tmo \/ TxA \/ TxB \/ TxD \/ TxE \/ TxG
Spec == Init /\ [][Next]_vars

TypeOK == /\ {E, R, ET, T, pkt, txdone, rxInTx} \subseteq BOOLEAN
          /\ att \in {"none", "rx", "tx"} /\ mode \in {"rx", "tx"} /\ air \in {"none", "inflight"}
          /\ ring \in 0..RingMax /\ arr \in 0..NF /\ txq \in 0..NTx
          /\ pc \in {"idle", "rxE", "rxR", "rxRead", "rA", "rB", "rC", "rD", "rE", "rF", "tE", "tT", "tmo",
                     "txA", "txB", "txD", "txE", "txG", "drain"}

\* P1 (no lost DIO1 event / deaf node): with the loop idle and the chip in RX, a latched packet always has its flag;
\*    a finished TX always has its flag.
NoDeaf == pc = "idle" => ( (mode = "rx" /\ pkt => R) /\ (mode = "tx" /\ txdone => T) )
\* P2: the receive path never reads the chip while it transmits.
NoRxInTx == ~rxInTx
\* P3 (probe only): the park service never reads a packet it cannot store.  Holds by construction, the gate in
\* ParkSvc is the property; no cfg checks it any more (review 2026-11-23).
NoSilentLoss == silent = 0
\* P4: no stuck state.  Whenever the loop is back at idle with the chip in RX, the receive gate is open and the
\* ISR is attached; with the chip in TX the transmit gate is open and the ISR is attached.  Without this a node
\* is deaf (or never sees TxDone) with no packet latched, which NoDeaf does not notice.
Listening == (pc = "idle" /\ mode = "rx") => (E /\ att = "rx")
TxArmed   == (pc = "idle" /\ mode = "tx") => (ET /\ att = "tx")
=============================================================================
