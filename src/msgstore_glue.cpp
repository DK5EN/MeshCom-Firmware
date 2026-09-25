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
#include "sto_notice.h"

#include "aprs_functions.h"
#include "via_functions.h"
#include "mheard_functions.h"
#include "loop_functions.h"        // addTxRingEntryOnce() with its default args
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

    // P15 (txring_functions.cpp): addTxRingEntryOnce() classifies the slot
    // with READY (getMessagePriority() reads the destination and scores this
    // MSG_PRIO_CRITICAL, the correct classification for a mailbox delivery --
    // it genuinely is a personal DM, just relayed on the destination's
    // behalf, and it should not queue behind relay traffic and beacons any
    // more than the user's own DM would) and stores DONE, both under the
    // same lock -- the mailbox's own 9-step ladder (D3) is the only retry
    // schedule a delivery ever gets, never the TX ring's own retransmit
    // logic. No more hand-written status flip after the call, and no more
    // race window between it and doTX() on nRF52 (the trap
    // addTxRingEntryOnce() removes; ex-SendAckMessage()/N-14 precedent).
    int slot = addTxRingEntryOnce(buf, len, "mbox");
    if(slot < 0)
        return false;

    return true;
}

static void glueLog(const char *line)
{
    if(bLORADEBUG)
        printfdeb("%s\n", line);
}

// Stage 4 (docs/dm-stage4-plan-20260914.md §5): builds and enqueues the
// :sto custody notice back to the DM's original sender. Same shape as
// glueDeliver() above with three differences: the notice's own destination
// is the sender (e->src), not the mailbox's held destination; source/path
// are this node's own call, since the notice originates here, not relayed
// on someone else's behalf; and max_hop is left at the normal text hop
// count (initAPRS() already sets it from meshcom_settings.max_hop_text for
// a ':' message -- D1: the sender is normally several hops away, so a
// hop-0 notice would only ever reach a direct neighbour). Never acked,
// never insertOwnTx()'d, never uploaded to the server -- same as deliver().
static bool glueNotify(const struct MsgStoreEntry *e)
{
    if(e == NULL)
        return false;

    char payload[32];   // "%-9.9s:sto%03u %s" -> at most 9+4+3+1+9+1 = 27 bytes
    int  plen = stoNoticeBuild(payload, sizeof(payload), e->src, e->nnn, e->dst);
    if(plen <= 0)
        return false;

    struct aprsMessage m;
    initAPRS(m, ':');

    m.msg_id                = millis();
    m.msg_source_call       = meshcom_settings.node_call;
    m.msg_source_path       = meshcom_settings.node_call;
    m.msg_destination_call  = e->src;
    m.msg_destination_path  = e->src;
    m.msg_payload           = String(payload);

    checkVia(m);

    uint8_t buf[MAX_MSG_LEN_PHONE];
    uint16_t len = encodeAPRS(buf, m);

    if(len == 0)
        return false;

    // Same addTxRingEntryOnce() enqueue as glueDeliver() above: the
    // mailbox's own ladder (for a delivery) or "pending until acked by
    // notified==2" (for a notice) is the only retry schedule either ever
    // gets, never the TX ring's own retransmit logic.
    int slot = addTxRingEntryOnce(buf, len, "sto");
    if(slot < 0)
        return false;

    return true;
}

static const struct MsgStoreEnv msgstore_env = {
    glueNowMs,
    glueOwnCall,
    glueHeardAgeMs,
    glueBpState,
    glueUtilPct,
    glueRandomBetween,
    glueDeliver,
    glueLog,
    glueNotify
};

void msgstoreGlueInit(void)
{
    msgstoreInit(&msgstore_env);
}

#endif // ENABLE_MSGSTORE
