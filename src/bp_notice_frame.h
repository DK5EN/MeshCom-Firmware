#ifndef _BP_NOTICE_FRAME_H_
#define _BP_NOTICE_FRAME_H_

#include <stddef.h>
#include <stdio.h>

#include <aprs_functions.h>

// BP-01: the notice frame for the phone app / web GUI, filled here rather
// than inline in bpNoticeToPhone() (loop_functions.cpp) so the framing is
// native-testable (test/test_bp_notice_frame).
//
// Sender is the node's own callsign: a pseudo-sender ("response", the
// addBLECommandBack() framing) is not a valid call and lands in McApp's
// spam class (group 9999), where the operator never sees the notice
// (operator decision 2026-08-31).
//
// BP-06: the destination is now the same target the triggering message was
// sent to (a group, a DM call, or "*"), not a hardcoded broadcast -- a
// notice for a message the operator typed into group 20 should show up in
// the 20 chat, not vanish into "*". msg_app_offline still keeps the frame
// local: for a DM dst this makes the notice appear in the sender's own DM
// thread, but the frame is never announced, never retransmitted and never
// goes on the air, so the DM partner never sees it.
static inline void bpNoticeFillFrame(struct aprsMessage &aprsmsg,
                                     const char *node_call,
                                     const char *text,
                                     unsigned int msg_id,
                                     const char *dst)
{
    initAPRS(aprsmsg, ':');

    aprsmsg.msg_len = 0;
    aprsmsg.payload_type = ':';
    aprsmsg.msg_id = msg_id;
    aprsmsg.msg_destination_path = dst;
    aprsmsg.msg_destination_call = dst;
    aprsmsg.msg_source_path = node_call;
    aprsmsg.msg_payload = text;

    aprsmsg.msg_app_offline = true; // Rückmeldungen niemals announcen
}

// BP-06: peek at the destination call/group a raw text-message argument
// names, without touching the string and without any of sendMessage()'s
// side effects. Mirrors the iCall<11 destination-call extraction in
// sendMessage() (loop_functions.cpp, strDestinationCall) -- same leading
// '{', same brace search bounded to the first 11 characters, same
// uppercase + trim -- because the BP-01 refuse check runs BEFORE that
// parsing happens: a message refused into QRT still needs its notice
// addressed to the target the sender actually named.
//
// One DELIBERATE divergence: an empty or whitespace-only target ("{}" /
// "{ }") yields "*" here, while sendMessage()'s String::trim() produces an
// empty strDestinationCall -- a notice addressed to "" would land nowhere,
// "*" at least reaches the broadcast view. (Advisor finding BP-06/2.)
//
// out_len-safe: writes at most out_len-1 characters plus a NUL. The valid
// content under the iCall<11 rule is at most 9 characters ("OE1KBC-99"), so
// a 10-or-more byte out buffer never truncates a real call/group.
static inline void bpPeekDst(const char *raw, char *out, size_t out_len)
{
    if(out == nullptr || out_len == 0)
        return;

    if(raw == nullptr || raw[0] != '{')
    {
        snprintf(out, out_len, "*");
        return;
    }

    int brace = -1;
    for(int i = 1; i < 11 && raw[i] != '\0'; i++)
    {
        if(raw[i] == '}')
        {
            brace = i;
            break;
        }
    }

    if(brace < 0)
    {
        snprintf(out, out_len, "*");
        return;
    }

    int start = 1;
    int end = brace; // exclusive

    // isspace()-equivalent trim, matching String::trim() in the authoritative
    // extraction (which strips tab/CR/LF too, not only 0x20) -- advisor
    // finding BP-06/3. Plain ASCII checks, no <ctype.h> locale surprises.
    while(start < end && (raw[start] == ' ' || raw[start] == '\t' ||
                          raw[start] == '\r' || raw[start] == '\n'))
        start++;
    while(end > start && (raw[end - 1] == ' ' || raw[end - 1] == '\t' ||
                          raw[end - 1] == '\r' || raw[end - 1] == '\n'))
        end--;

    int content_len = end - start;
    if(content_len <= 0)
    {
        snprintf(out, out_len, "*");
        return;
    }

    size_t max_copy = out_len - 1;
    if((size_t)content_len > max_copy)
        content_len = (int)max_copy;

    for(int i = 0; i < content_len; i++)
    {
        char c = raw[start + i];
        if(c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        out[i] = c;
    }
    out[content_len] = '\0';
}

#endif // _BP_NOTICE_FRAME_H_
