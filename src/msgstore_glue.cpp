// msgstore_glue.cpp -- firmware-side MsgStoreEnv for the store node core
// (docs/dm-stage3-wave-plan-20260914.md, src/msgstore_api.h). Wires the
// platform-neutral core to millis(), the mheard table, back-pressure/
// utilisation state, Arduino's random() and the TX ring, so msgstore.cpp
// itself never has to know about any of that.
//
// ESP32-S3 and nRF52840 only (ENABLE_MSGSTORE, configuration_global.h);
// classic ESP32 compiles nothing of the store node role.
#include "Arduino.h"
#include "configuration.h"   // defines ENABLE_MSGSTORE -- must come BEFORE the guard

#if defined(ENABLE_MSGSTORE)

#include "msgstore_api.h"

#include "aprs_functions.h"
#include "via_functions.h"
#include "mheard_functions.h"
#include "loop_functions.h"        // addTxRingEntry() with its default args
#include <loop_functions_extern.h> // ringBuffer[], bpCurrentState(), stat_last_window
#include "printfdeb_functions.h"

static uint32_t glueNowMs(void)
{
    return millis();
}

static const char *glueOwnCall(void)
{
    return meshcom_settings.node_call;
}

static int32_t glueHeardAgeMs(const char *call)
{
    return mheardAgeMs(call);
}

static int glueBpState(void)
{
    return bpCurrentState();
}

static uint8_t glueUtilPct(void)
{
    return stat_last_window.util_pct;
}

static uint32_t glueRandomBetween(uint32_t lo, uint32_t hi)
{
    if(hi <= lo)
        return lo;
    // Arduino random(min, max) is exclusive on max; +1 makes hi inclusive,
    // same idiom as the CSMA backoff jitter (lora_functions.cpp).
    return (uint32_t)random((long)lo, (long)hi + 1);
}

// Builds and enqueues one hop-0 delivery frame for a mailbox entry -- the
// same shape as the mesh relay re-encode (src/lora_functions.cpp:1511-1568),
// with a fresh msg_id, max_hop pinned to 0 (D2: one hop to the destination,
// nobody relays it) and no 0x41 custody ack ever built (D3). initAPRS()
// already sets msg_source_hw/msg_source_mod/msg_last_hw to the same values
// the relay path re-derives, so there is nothing to redo here.
static bool glueDeliver(const struct MsgStoreEntry *e)
{
    if(e == NULL)
        return false;

    struct aprsMessage m;
    initAPRS(m, ':');

    m.msg_id            = millis();
    m.msg_source_call   = e->src;
    m.msg_source_path   = String(e->src) + "," + meshcom_settings.node_call;
    m.msg_destination_call = e->dst;
    m.msg_destination_path = e->dst;

    char nnnbuf[8];
    snprintf(nnnbuf, sizeof(nnnbuf), "%03u", (unsigned)e->nnn);
    m.msg_payload = String(e->payload) + "{" + nnnbuf;

    m.max_hop = 0;   // D2: raw hop count on the wire, direct neighbour only

    checkVia(m);

    uint8_t buf[MAX_MSG_LEN_PHONE];
    uint16_t len = encodeAPRS(buf, m);

    if(len == 0)
        return false;

    // SendAckMessage() precedent (loop_functions.cpp): enqueue READY, then
    // flip the slot to DONE so the ring never retransmits it -- the ladder
    // above is the mailbox's own retry schedule, not the ring's.
    //
    // F5 (docs/review/fable-dm-stage3-verdict-20260914.md): this inherits
    // the same N-14 race window as SendAckMessage() -- getMessagePriority()
    // (txring_functions.cpp) classifies MSG_TYPE_TEXT by destination as
    // MSG_PRIO_CRITICAL *unless* the slot status already reads DONE, in
    // which case it is the relay's MSG_PRIO_NORMAL. If a priority read lands
    // inside this window it sees READY and scores CRITICAL, same as a live
    // user-typed personal DM would; that is the correct classification for
    // a mailbox delivery too -- it genuinely is a personal DM, just relayed
    // on the destination's behalf, and it should not queue behind relay
    // traffic and beacons any more than the user's own DM would. Enqueuing
    // straight into DONE (addTxRingEntry's ring_status argument makes that
    // one line) would close the window but always reads NORMAL instead --
    // trading the correct-but-racy classification for a wrong-but-safe one.
    // Enqueuing and staying at READY forever is not an option either: that
    // is what the TX ring's own retransmit logic keys on, and D3 requires
    // the mailbox's 9-step ladder to be the only retry schedule a delivery
    // ever gets. So: no code change, same accepted window as the ACK path.
    int slot = addTxRingEntry(buf, len, 0x00, "mbox");
    if(slot < 0)
        return false;

    ringBuffer[slot][1] = 0xFF;

    return true;
}

static void glueLog(const char *line)
{
    if(bLORADEBUG)
        printfdeb("%s\n", line);
}

static const struct MsgStoreEnv msgstore_env = {
    glueNowMs,
    glueOwnCall,
    glueHeardAgeMs,
    glueBpState,
    glueUtilPct,
    glueRandomBetween,
    glueDeliver,
    glueLog
};

void msgstoreGlueInit(void)
{
    msgstoreInit(&msgstore_env);
}

#endif // ENABLE_MSGSTORE
