#ifndef _BP_NOTICE_FRAME_H_
#define _BP_NOTICE_FRAME_H_

#include <aprs_functions.h>

// BP-01: the notice frame for the phone app / web GUI, filled here rather
// than inline in bpNoticeToPhone() (loop_functions.cpp) so the framing is
// native-testable (test/test_bp_notice_frame).
//
// Sender is the node's own callsign: a pseudo-sender ("response", the
// addBLECommandBack() framing) is not a valid call and lands in McApp's
// spam class (group 9999), where the operator never sees the notice
// (operator decision 2026-08-31). msg_app_offline keeps the frame local —
// never announced, never retransmitted, never on the air.
static inline void bpNoticeFillFrame(struct aprsMessage &aprsmsg,
                                     const char *node_call,
                                     const char *text,
                                     unsigned int msg_id)
{
    initAPRS(aprsmsg, ':');

    aprsmsg.msg_len = 0;
    aprsmsg.payload_type = ':';
    aprsmsg.msg_id = msg_id;
    aprsmsg.msg_destination_path = "*";
    aprsmsg.msg_destination_call = "*";
    aprsmsg.msg_source_path = node_call;
    aprsmsg.msg_payload = text;

    aprsmsg.msg_app_offline = true; // Rückmeldungen niemals announcen
}

#endif // _BP_NOTICE_FRAME_H_
