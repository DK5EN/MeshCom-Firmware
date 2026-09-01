#ifndef _BP_NOTICE_FRAME_H_
#define _BP_NOTICE_FRAME_H_

#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

// BP-07: bytes, not characters -- see the length budget table in
// docs/bp-l1-l4-impl-plan.md (section "src/bp_notice_frame.h"). Chosen so the
// EXTUDP path (the tightest: 400-byte c_json, ~141-byte JSON skeleton with
// the longest possible callsign/dst) still has three-digit headroom, while
// BLE/Web (255-byte UDP_TX_BUF_SIZE) stay comfortably within budget too.
#define BP_NACK_TEXT_MAX 120

// BP-07 (L1/L2): compose the "<prefix><text>" body of a per-message nack --
// "QRT NOT SENT - Hello World 17" -- for the transport that just had a
// message refused or dropped. prefix is always one of the two hardcoded
// literals from bpNackPrefix() (backpressure.h) and is copied verbatim;
// text is the operator's own message and is truncated and sanitized:
//
//  1. Never cuts mid UTF-8 sequence. text arrives already decoded (after the
//     %-escape loop in sendMessage()), so umlauts and emoji are real
//     multi-byte sequences; truncating at a fixed byte count can land inside
//     one. Fix: once the BP_NACK_TEXT_MAX-byte cut point is chosen, back up
//     over UTF-8 continuation bytes (10xxxxxx, 0x80..0xBF) until a lead byte
//     or plain ASCII byte is reached.
//  2. Replaces '"', '\' and every byte < 0x20 with a space. The EXTUDP path
//     embeds this text in JSON; without this rule a text full of quotes
//     could double in length when ArduinoJson escapes it and blow the
//     datagram buffer. This keeps the byte count 1:1 through serialization.
//
// out_len-safe: writes at most out_len-1 bytes plus a NUL, even if out_len is
// smaller than the prefix (defensive only -- every real caller sizes its
// buffer for prefix + BP_NACK_TEXT_MAX + "...", see the plan's budget table).
// Returns the number of bytes written, excluding the NUL; 0 if out/out_len
// is invalid.
static inline size_t bpNackCompose(char *out, size_t out_len,
                                   const char *prefix, const char *text)
{
    if(out == nullptr || out_len == 0)
        return 0;

    out[0] = '\0';

    if(prefix == nullptr)
        prefix = "";
    if(text == nullptr)
        text = "";

    size_t room = out_len - 1;   // bytes available before the terminating NUL

    // 1) Prefix, verbatim -- never sanitized, it is not operator text.
    size_t prefix_len = strlen(prefix);
    if(prefix_len > room)
        prefix_len = room;
    memcpy(out, prefix, prefix_len);
    size_t written = prefix_len;
    room -= prefix_len;

    // 2) Text, truncated to BP_NACK_TEXT_MAX bytes (rule 1's boundary is
    // applied once, below, after every clamp has already picked the final
    // cut point -- reapplying it after a later clamp would be wrong, since a
    // second clamp could itself land back inside a multi-byte sequence).
    size_t text_len = strlen(text);
    bool truncated = text_len > BP_NACK_TEXT_MAX;
    size_t take = truncated ? BP_NACK_TEXT_MAX : text_len;

    // The caller's buffer is the harder limit in a pathologically small
    // out_len (test-only case, see out_len-safe above) -- clamp take (and
    // drop the "..." first, then the text) to whatever room is left.
    size_t suffix_len = truncated ? 3 : 0;   // "..."
    if(take + suffix_len > room)
    {
        if(room <= suffix_len)
        {
            take = 0;
            suffix_len = (room >= 3) ? 3 : 0;
        }
        else
        {
            take = room - suffix_len;
        }
    }

    while(take > 0 && ((unsigned char)text[take] & 0xC0) == 0x80)
        take--;

    for(size_t i = 0; i < take; i++)
    {
        char c = text[i];
        if(c == '"' || c == '\\' || (unsigned char)c < 0x20)
            c = ' ';
        out[written++] = c;
    }
    room -= take;

    if(suffix_len == 3 && room >= 3)
    {
        out[written++] = '.';
        out[written++] = '.';
        out[written++] = '.';
    }

    out[written] = '\0';
    return written;
}

#endif // _BP_NOTICE_FRAME_H_
