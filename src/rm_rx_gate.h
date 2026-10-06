// rm_rx_gate.h -- the one receive hook for remote-management DMs ("RM1 <ctr> ...", RM-04, #1189).
//
// Called for a DM to the exact own call, with the "{NNN" suffix already stripped, from every ingress:
// OnRxDone (LoRa) and the server ingress of a gateway node (udp_frame_esp32.cpp / udp_frame_nrf52.cpp).
//
// Since 2026-10-06 (RM-GWRELAY, operator decision) a COMMAND is accepted from either path, like a reply
// always was: the HMAC tag and the counter authenticate it wherever it travelled. The former "LoRa only"
// rule (RM-D6) keyed on the server flag, which a gateway also sets on every frame it relays over RF, so a
// node behind a gateway could not be managed at all. A frame that arrives on both paths is harmless: the
// second copy has the same counter and tag and gets the cached reply (rmCheck, RM_CACHED), nothing runs twice.
//
// Header-only; needs rmIsReply() (remote_cmd.cpp) and the queues of rm_queue.h.
#ifndef RM_RX_GATE_H
#define RM_RX_GATE_H

#include <string.h>

#include "remote_cmd.h"
#include "rm_queue.h"

// RM switch on and a node password set (an empty or blank password switches RM off, concept 6.2)
inline bool rmRxEnabled(int nodeRm, const char *passwd)
{
    return nodeRm == 1 && passwd != nullptr && passwd[0] != 0x00 && passwd[0] != ' ';
}

// true = consumed: the caller must neither display the DM nor hand it to the phone. A full queue also
// counts as consumed (command dropped, the sender repeats it with a fresh counter); verify, rate limit
// and lockout are rmDrain()'s, in the loop task.
//  - a REPLY ("RM1 <ctr> ok ..." / "RM1 <ctr> err ...") is for the operator to read: never consumed, and
//    queued for verification only while a command this node sent still waits for its reply
//  - a COMMAND with RM off or no password is ordinary text
inline bool rmRxTryQueue(const char *src, const char *text, bool enabled)
{
    if (src == nullptr || text == nullptr || strncmp(text, "RM1 ", 4) != 0)
        return false;

    if (rmIsReply(text))
    {
        if (rmqReplyWanted())
            rmReplyPush(src, text);
        return false;
    }

    if (!enabled)
        return false;

    rmQueuePush(src, text);
    return true;
}

#endif // RM_RX_GATE_H
