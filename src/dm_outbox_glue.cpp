// dm_outbox_glue.cpp -- firmware-side DmOutboxEnv for the stage 1 retry
// ladder (docs/dm-stage1-plan-20260914.md, src/dm_outbox_api.h). Wires the
// platform-neutral core to millis(), back-pressure state, the TX ring and
// the phone status frame, so dm_outbox.cpp itself never has to know about
// any of that -- same shape as src/msgstore_glue.cpp for the store node.
//
// Every board (ESP32-S3, nRF52840, classic ESP32): the outbox is the
// sender side of a DM, unlike msgstore's store-node role which is
// ENABLE_MSGSTORE-only.
#include "Arduino.h"
#include "configuration.h"

#include "dm_outbox_api.h"
#include "dm_settings.h"
#include "dm_stats.h"                // F4: dmstat_giveup / dmstat_giveup_held

#include "aprs_functions.h"
#include "via_functions.h"
#include "ack_attribution.h"
#include "loop_functions.h"          // addTxRingEntry()/insertOwnTx()/checkOwnTx() with default args
#include <loop_functions_extern.h>   // ringBuffer[]/own_msg_id[]/bpCurrentState()/bGATEWAY/bEXTUDP
#include "dedup_functions.h"         // addLoraRxBuffer()
#include "printfdeb_functions.h"

// Same board test as the store-node role's slot table (configuration_global.h):
// ESP32-S3 and nRF52840 get the full 5 slots, classic ESP32 (tighter RAM/flash) 3.
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(BOARD_RAK4630)
#define DM_OUTBOX_BOOT_SLOTS 5
#else
#define DM_OUTBOX_BOOT_SLOTS 3
#endif

static uint32_t glueNowMs(void)
{
    return millis();
}

static int glueBpState(void)
{
    return bpCurrentState();
}

static uint32_t glueMintId(void)
{
    return millis();
}

static void glueLog(const char *line)
{
    if(bLORADEBUG)
        printfdeb("%s\n", line);
}

// Rebuilds the frame exactly as sendMessage() does for a DM (loop_functions.cpp
// ~:4098-4139), with the ladder's own msg_id (same id for a same-id retry,
// a fresh one otherwise -- the core decides which and passes it in) and no
// node_msgid++/save_settings()/dmStatNoteSent(): those fire once, in
// sendMessage(), for attempt 1 only.
static bool glueTransmit(const struct DmOutboxEntry *e, uint32_t msg_id)
{
    if(e == NULL)
        return false;

    struct aprsMessage m;
    initAPRS(m, ':');

    m.msg_id = msg_id;
    m.msg_source_path      = meshcom_settings.node_call;
    m.msg_destination_path = e->dst;
    m.msg_destination_call = e->dst;

    char nnnbuf[8];   // "%03u" of a uint16_t -- same sizing as msgstore_glue.cpp's glueDeliver()
    snprintf(nnnbuf, sizeof(nnnbuf), "%03u", (unsigned)e->nnn);
    m.msg_payload = String(e->payload) + "{" + nnnbuf;

    m.max_hop = e->max_hop;

    checkVia(m);

    uint8_t  buf[MAX_MSG_LEN_PHONE];
    uint16_t len = encodeAPRS(buf, m);

    if(len == 0)
        return false;

    // Ring accept first, same order as sendMessage(): only a slot the ring
    // actually took earns the own-tx/dedup bookkeeping below. 0xFF (=
    // RING_STATUS_DONE): the ladder is this attempt's only retry schedule,
    // the ring must never retransmit it on its own.
    int slot = addTxRingEntry(buf, len, 0xFF, "dm_retry");
    if(slot < 0)
        return false;

    // T2: own_msg_id[] is not the ladder's authority (the outbox keeps its
    // own first_id/last_id), but every attempt still calls insertOwnTx() so
    // echo recognition (dmstat_echo, the HEARD mark) and the stage 4 held
    // mark keep working on whichever id is currently on the air.
    insertOwnTx(msg_id);

    if(bGATEWAY && meshcom_settings.node_hasIPaddress)
        addLoraRxBuffer(msg_id, true);
    else
        addLoraRxBuffer(msg_id, false);

    // F3 (fable-dm-stage1-verdict-20260914.md): deliberately NOT mirrored to
    // the server/EXTUDP here, unlike sendMessage()'s own upload for attempt
    // 1. sendMessage() uploads once per DM (loop_functions.cpp addNodeData/
    // sendExtern); the ring's pre-stage-1 same-id retries never re-upload
    // either -- attempt 1 already reached the server, and every attempt
    // here (same-id retry or fresh-id) is a retry of that same DM, not a
    // new message. Re-uploading each of up to nine attempts would hand the
    // server, mcmap and every phone on every other gateway that many
    // distinct-id copies of one DM (same class as the "OE1XAR-62 BBS posts
    // were gwflood" incident) for no benefit: the stage 2.1 fold that makes
    // fresh-id retries safe exists only on this firmware's RF ingress.
    return true;
}

// Phone status frame for e->first_id -- the app's key for this message
// regardless of which id the current attempt carried (plan section 3).
// Called by dmOutboxLoop() for the give-up outcome (0x03) only: the ack
// outcome (0x02) deliberately does not call this, see dmOutboxOnAck()'s
// comment in dm_outbox.cpp.
static void glueReport(const struct DmOutboxEntry *e, uint8_t status)
{
    if(e == NULL)
        return;

    // F4 (fable-dm-stage1-verdict-20260914.md): dm_outbox.cpp now calls
    // report() for every give-up, held or not -- "caller decides the held
    // case" (dm_outbox_api.h). Count here unconditionally, THEN decide
    // whether a 0x03 goes on the wire: without this, the stage 0 DM line's
    // giveup=/giveuph= fields stayed at zero for every laddered DM even
    // though OUTBOX give= already counted it (the ring's own give-up path
    // that dmstat_giveup/dmstat_giveup_held were originally written against,
    // lora_functions.cpp, is unreachable for outbox-driven DMs -- their ring
    // slots carry 0xFF, no ring-side retry).
    if(status == 0x03)
    {
        dmstat_giveup.fetch_add(1);
        if(e->held)
            dmstat_giveup_held.fetch_add(1);
    }

    // A held message is not a failure on the wire (stage 4 decision 3) --
    // never send 0x03 for it. Counted above already; nothing else to do.
    if(e->held)
        return;

    uint8_t  buf[ACK_PHONE_MAX_LEN];
    uint16_t plen = buildAckPhoneFrame(buf, e->first_id, status, e->dst);
    addBLEOutBuffer(buf, plen);

    // own_msg_id[][4] backs checkOwnTx(first_id) for later HEARD/ack
    // recognition -- never downgrade an already-final 0x02, never overwrite
    // a stage 4 0x04 (held) with this ladder's 0x03 (a held message is not
    // a failure, same rule as dmstat_giveup_held elsewhere).
    int idx = checkOwnTx(e->first_id);
    if(idx < 0)
        return;

    uint8_t cur = own_msg_id[idx][4];
    if(cur == 0x02)
        return;
    if(cur == 0x04 && status == 0x03)
        return;

    own_msg_id[idx][4] = status;
}

static const struct DmOutboxEnv dm_outbox_env = {
    glueNowMs,
    glueBpState,
    glueMintId,
    glueTransmit,
    glueReport,
    glueLog
};

void dmOutboxGlueInit(void)
{
    dmOutboxInit(&dm_outbox_env, DM_OUTBOX_BOOT_SLOTS);
}
