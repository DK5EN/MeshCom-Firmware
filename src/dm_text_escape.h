#ifndef _DM_TEXT_ESCAPE_H_
#define _DM_TEXT_ESCAPE_H_

// P15: Arduino-free core of the brace-escape exception for sendMessage()
// (natively testable, test/test_dm_text_escape). For a DM, sendMessage()
// replaces every '{' in the text with '(' (comment there: "A '{' inside the
// user text breaks the receiver's NNN parse"). {ping} and {SET} are not
// free-text messages, though, but tags of their own:
//   * a {ping} would become (ping}{NNN -- no longer a ping, the peer never
//     answers with {pong}, and nothing stops a retransmission.
//   * a {SET}n;m; would become (SET}n;m; -- the remote hop-limit command
//     (sendDisplayText(), startsWith("{SET}")) never fires.
// dmTextEscapeFrom() returns the index FROM which escaping must start: 0 in
// the normal case (behaviour unchanged), otherwise the length of the
// recognised tag -- a '{' AFTER the tag still breaks the receiver's NNN parse
// (aprsmsg.msg_payload.indexOf("{", 1)) and is escaped as before.
//
// Only an EXACT leading tag counts ("{pingx"/"{SETX" do not match --
// strncmp() also compares the closing '}').

#include <stddef.h>
#include <string.h>

inline size_t dmTextEscapeFrom(const char *text)
{
    if(text == NULL)
        return 0;

    static const char PING_TAG[] = "{ping}";
    static const char SET_TAG[]  = "{SET}";
    const size_t pingLen = sizeof(PING_TAG) - 1;
    const size_t setLen  = sizeof(SET_TAG) - 1;

    if(strncmp(text, PING_TAG, pingLen) == 0)
        return pingLen;

    if(strncmp(text, SET_TAG, setLen) == 0)
        return setLen;

    return 0;
}

// W0c (McApp ask 2): a remote-management frame ("RM1 <ctr> <cmd> [args] <tag>",
// command or reply) goes on air exactly once: without the "{NNN" suffix (the
// receiver then sends no DM ACK) and without a retransmission ladder. The
// frame carries its own counter and HMAC; an ACK/retry would only burn
// airtime and open a replay window at the receiver.
// Only the EXACT leading "RM1 " (4 bytes incl. the space) counts: "RM10",
// "rm1 ", " RM1 ", "xRM1 ", "RM1" without a space and a later "RM1 " in the
// text do not match. Known consequence: a hand-typed chat DM that starts
// with "RM1 " likewise gets neither ACK nor retry (accepted).
inline bool dmTextIsRm1Frame(const char *text)
{
    return text != NULL && strncmp(text, "RM1 ", 4) == 0;
}

// One place for "this text is never retransmitted": {CET}/{MCP}/{SET}
// (time/remote-control/hop tags, not free text) and RM1 frames. {mcp} is the
// wire form of {MCP}: sendMessage() rewrites a typed {MCP}/{mcp} after a
// {DEST} into "{mcp}<id>" before the tag check, so both spellings count; no
// other lowercase variant does. {ping} is handled by dmTextSendOnce(). Both go
// out via the Once path of the TX ring (see dmTextSendOnce()). A {CET} DM never
// matches: dmTextEscapeFrom() knows only {ping}/{SET}, so sendMessage() turns
// it into "(CET}..." first; {CET} counts to a group or "*". The {NNN suffix
// is a separate decision: only RM1 drops it (dmTextIsRm1Frame), {CET}/{SET}
// keep it as before.
// isDM: the RM1 exception applies to direct messages only; a group text that
// happens to start with "RM1 " is retransmitted like any other group text.
inline bool dmTextNoRetransmit(const char *text, bool isDM)
{
    if(text == NULL)
        return false;

    return strncmp(text, "{CET}", 5) == 0 ||
           strncmp(text, "{MCP}", 5) == 0 ||
           strncmp(text, "{mcp}", 5) == 0 ||
           strncmp(text, "{SET}", 5) == 0 ||
           (isDM && dmTextIsRm1Frame(text));
}

// RCA 2026-10-10: every own text message that goes on air exactly once must
// be enqueued via addTxRingEntryOnce() (classified READY -> own DM/group
// priority, stored DONE -> no retry). Passing status 0xFF to addTxRingEntry()
// instead makes getMessagePriority() read the slot as a relay (NORMAL,
// base 4500 ms): an RM1 reply then shares the back-off window of every
// neighbour relaying the request and collides with it (4 of 9 lost on the
// bench). Covers {ping}, {CET}/{MCP}/{SET} and RM1 frames (dmTextNoRetransmit).
inline bool dmTextSendOnce(const char *payload, bool isDM)
{
    if(payload == NULL)
        return false;
    return strncmp(payload, "{ping}", 6) == 0 || dmTextNoRetransmit(payload, isDM);
}

#endif // _DM_TEXT_ESCAPE_H_
