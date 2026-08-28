// (C) 2026 MeshCom contributors
//
// Implementation notes / entry-point choice
// ------------------------------------------
// The real receive path is:
//   OnRxDone (ISR) -> decodeAPRS(RcvBuffer, size, aprsmsg)                  [aprs_functions.cpp]
//                   -> queueDisplayText(aprsmsg, rssi, snr)                 [lora_functions.cpp:133]
//                   -> main loop flushDeferredDisplayUpdates()              [esp32/esp32_main.cpp:1816]
//                   -> sendDisplayText(_msg, _rssi, _snr)                   [loop_functions.cpp:2052]
//                   -> tdeck_add_MSG(aprsmsg, true) (T-Deck/T-Deck Plus)    [t-deck/lv_obj_functions.cpp:4182]
//
// queueDisplayText() is exactly the right entry point semantically (it is
// the one function that turns a decoded aprsMessage into "please show this
// like it just arrived"), but it is declared `static` in lora_functions.cpp
// and therefore has internal linkage -- it cannot be called from this
// translation unit, and lora_functions.cpp is out of scope for this change.
//
// queueDisplayText() itself does nothing but copy the message into three
// already-exported globals and raise a flag:
//
//   pendingDisplayMsg = aprsmsg; pendingDisplayRssi = rssi;
//   pendingDisplaySnr = snr;     bPendingDisplayText = true;
//
// Those globals are declared `extern` in src/loop_functions_extern.h
// specifically so other translation units (esp32_main.cpp, nrf52_main.cpp)
// can read them back in flushDeferredDisplayUpdates(). We use that same
// extern surface to write them, which reproduces queueDisplayText()'s exact
// effect -- the main loop's flush code cannot tell the difference between a
// message queued this way and one queued by the real ISR path. No raw APRS
// frame needs to be built (no encodeAPRS() round trip) because the queue
// consumes a decoded aprsMessage, not bytes off the radio.
#include "test_inject.h"

#if MC_INJECT_HOOKS

#include <Arduino.h>
#include <string.h>

#include <loop_functions.h>         // meshcom_settings, aprsMessage, sendDisplayText() decl
#include <loop_functions_extern.h>  // bPendingDisplayText / pendingDisplayMsg / ...rssi/...snr
#include <aprs_functions.h>         // struct aprsMessage, initAPRS(), CheckGroup()
#include <regex_functions.h>        // checkRegexCall()

namespace {

const char *const kDefaultSrcCall = "DK5EN-93";

// Real received text messages are bounded by MAX_APRS_FRAME_SIZE
// (aprs_functions.cpp: 340 bytes) minus a variable-length header/trailer
// (msg id, flags, source/destination path, hw/mod/fcs trailer, NUL
// terminator). We never build that raw frame (see file header), so there is
// no exact rsize to bound against MAX_APRS_FRAME_SIZE. This fixed cap
// leaves generous headroom below it for any realistic path length.
const size_t kMaxPayloadLen = 200;

// Monotonically increasing message id, seeded from millis() XOR a fixed salt
// so two runs started at different uptimes still diverge, and every
// subsequent call within a run is guaranteed distinct from the last
// regardless of millis() resolution.
unsigned int nextInjectMsgId()
{
    static unsigned int counter = (unsigned int)millis() ^ 0xC0FFEEu;
    counter++;
    return counter;
}

} // namespace

bool inject_text_message(const char *dst, const char *text, const char *src_call, int16_t rssi, int8_t snr)
{
    if(text == nullptr || text[0] == 0x00)
    {
        Serial.printf("[INJECT];err;empty text\n");
        return false;
    }

    size_t text_len = strlen(text);

    if(text_len > kMaxPayloadLen)
    {
        Serial.printf("[INJECT];err;text too long (%u > %u)\n", (unsigned)text_len, (unsigned)kMaxPayloadLen);
        return false;
    }

    String strDst = (dst != nullptr) ? String(dst) : String("");
    strDst.trim();

    bool bDstIsGroup = CheckGroup(strDst) != 0;

    if(strDst.length() == 0 || (!bDstIsGroup && !checkRegexCall(strDst)))
    {
        Serial.printf("[INJECT];err;invalid dst\n");
        return false;
    }

    String strSrc = (src_call != nullptr) ? String(src_call) : String("");
    strSrc.trim();
    if(strSrc.length() == 0)
        strSrc = kDefaultSrcCall;

    // The deferred-display hand-off is a single pending slot (see
    // queueDisplayText()/flushDeferredDisplayUpdates()) -- refuse rather than
    // clobber a message the main loop has not flushed yet.
    if(bPendingDisplayText)
    {
        Serial.printf("[INJECT];err;queue full\n");
        return false;
    }

    struct aprsMessage aprsmsg;
    initAPRS(aprsmsg, ':');   // text message -- same payload_type decodeAPRS() uses (0x3A)

    unsigned int msg_id = nextInjectMsgId();

    aprsmsg.msg_id = msg_id;
    aprsmsg.msg_len = (uint16_t)text_len;   // approximate: no raw frame is built (see above)

    // Mark it as received direct from src_call -- no relay hops, matching what
    // decodeAPRS() leaves in msg_source_path/msg_source_last/msg_source_call
    // for a one-hop packet (all three equal, msg_last_path_cnt == 1).
    aprsmsg.msg_source_path = strSrc;
    aprsmsg.msg_source_call = strSrc;
    aprsmsg.msg_source_last = strSrc;
    aprsmsg.msg_last_path_cnt = 1;

    aprsmsg.msg_destination_path = strDst;
    aprsmsg.msg_destination_call = strDst;

    aprsmsg.msg_payload = text;

    // Same effect as lora_functions.cpp's (static, unreachable here)
    // queueDisplayText() -- see file header.
#if defined(BOARD_RAK4630)
    taskENTER_CRITICAL();
#endif
    pendingDisplayMsg = aprsmsg;
    pendingDisplayRssi = rssi;
    pendingDisplaySnr = snr;
    bPendingDisplayText = true;
#if defined(BOARD_RAK4630)
    taskEXIT_CRITICAL();
#endif

    Serial.printf("[INJECT];ok;id;%08X;dst;%s;src;%s;len;%u\n",
                  msg_id, strDst.c_str(), strSrc.c_str(), (unsigned)text_len);

    return true;
}

#endif // MC_INJECT_HOOKS
