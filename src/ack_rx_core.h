#pragma once

// FORMAL-02 wave 1: the decision part of handleACK() (lora_functions.cpp) as a pure function.
//
// handleACK() used to interleave decisions with side effects (log, display, dedup ring, TX ring,
// phone queue). ackRxDecide() returns everything handleACK() needs to act and touches nothing:
// no globals, no logging, no String, no VLA, no recursion, every payload read bounded by `size`,
// every loop with a constant bound (MAX_DEDUP_RING / MAX_RING). The caller passes the two tables it
// read from (full arrays, as the firmware holds them) and the few scalars the decision depends on.
// Differential test against the frozen former code: test/test_ack_rx_core/.
//
// Not decided here, on purpose:
//  - checkServerRx(frame+6): it must run AFTER addLoraRxBuffer() (that insert can add the very id it
//    looks up, or overwrite a ring slot), so the caller keeps the call; the verdict says whether to ask
//    (fwdCandidate).
//  - findAndStopRingSlotNoRm1(): it mutates the TX ring and skips RM1 command slots; the verdict only
//    says whether to call it (stopRing).

#include <stdint.h>
#include <string.h>

#include "configuration.h"
#include "ack_functions.h"      // isPlausibleAckFrame
#include "ack_attribution.h"    // ackWireAppendixLen, ackWireHash, ackMsgIdFromNode, ACK_WIRE_*
#include "pn_retry.h"           // pnOwnTxLookupId
#include "mesh_policy.h"        // meshTxAllowed
#include "id_table_find.h"      // idTableFind, idTableFindU32

enum AckRxAction : uint8_t
{
    ACK_RX_NOT_ACK = 0,     // not a 0x41 frame or shorter than 12 byte: caller continues with other handlers
    ACK_RX_IMPLAUSIBLE,     // 0x41 but fails isPlausibleAckFrame(): same return (false), plus a debug line
    ACK_RX_ACCEPT           // processed as ACK: handleACK() returns true
};

struct AckRxVerdict
{
    AckRxAction action;
    uint8_t     n;                      // wire appendix length: 0 or ACK_WIRE_APPENDIX_LEN
    uint8_t     frameLen;               // 12 + n
    uint8_t     frame[ACK_WIRE_MAX_LEN];// copy of the frame, the caller's print_buff
    uint32_t    appendixHash;           // ackWireHash(), only when n == ACK_WIRE_APPENDIX_LEN

    bool        isNew;                  // dedup ring has no entry for frame[1..4]
    int         dedupSlot;              // the ring slot that matched, -1 when isNew
    bool        serverFlag;             // frame[5] bit 7
    uint32_t    rxId;                   // frame[1..4]: the ACK's own msg_id (dedup key)
    uint32_t    ackedId;                // frame[6..9] folded to the original of a PN retry copy
    int         ownSlot;                // own_msg_id[] slot of ackedId, -1 when none

    bool        lateAck;                // ownSlot < 0: durable status row only (ownMsgStatusSet(ackedId, 0x02))
    bool        addRx;                  // addLoraRxBuffer(rxId, serverFlag)
    bool        phoneStatus;            // ownSlot >= 0 and (ackinfo or not yet ACKed): status block runs
    bool        phoneFrame;             // inside it: 0x01 frame to the phone, dmstat_gw_ack++
    bool        stopRing;               // ownSlot >= 0: findAndStopRingSlotNoRm1(ackedId)
    bool        fwdCandidate;           // ownSlot < 0, new, hops left, mesh on: caller asks checkServerRx(frame+6)
    // when the forward goes ahead the caller sends frame with byte 5 decremented, frameLen bytes
};

/**
 * @param payload  receive buffer, may be NULL
 * @param size     bytes valid in payload
 * @param dedupRing  ringBufferLoraRX, [MAX_DEDUP_RING][5] (id little endian in 0..3, byte 4 server flag)
 * @param ownTbl     own_msg_id,       [MAX_RING][5]        (id little endian in 0..3, byte 4 status)
 * @param gwId     _GW_ID
 * @param ackInfo  bAckInfo
 * @param meshOn   the --mesh switch (caller passes the global flag)
 */
static inline AckRxVerdict ackRxDecide(const uint8_t *payload, uint16_t size,
                                       const uint8_t (*dedupRing)[5], const uint8_t (*ownTbl)[5],
                                       uint32_t gwId, bool ackInfo, bool meshOn)
{
    AckRxVerdict v;
    memset(&v, 0, sizeof(v));
    v.action = ACK_RX_NOT_ACK;
    v.dedupSlot = -1;
    v.ownSlot = -1;

    // size first: the former code read payload[0] before looking at size
    if(payload == NULL || size < 12 || payload[0] != MSG_TYPE_ACK)
        return v;

    if(!isPlausibleAckFrame(payload, size, MAX_HOP_LIMIT))
    {
        v.action = ACK_RX_IMPLAUSIBLE;
        return v;
    }

    v.action = ACK_RX_ACCEPT;
    v.n = ackWireAppendixLen(payload, size);
    v.frameLen = (uint8_t)(12 + v.n);
    memcpy(v.frame, payload, v.frameLen);

    if(v.n == ACK_WIRE_APPENDIX_LEN)
        v.appendixHash = ackWireHash(payload);

    v.rxId = (uint32_t)v.frame[1] | ((uint32_t)v.frame[2] << 8) | ((uint32_t)v.frame[3] << 16) | ((uint32_t)v.frame[4] << 24);

    {
        const int slot = idTableFind(dedupRing, MAX_DEDUP_RING, v.frame + 1);
        v.isNew = slot < 0;
        if(slot >= 0)
            v.dedupSlot = slot;
    }

    v.serverFlag = (v.frame[5] & 0x80) == 0x80;

    uint32_t id = (uint32_t)v.frame[6] | ((uint32_t)v.frame[7] << 8) | ((uint32_t)v.frame[8] << 16) | ((uint32_t)v.frame[9] << 24);
    id = pnOwnTxLookupId(id, gwId);
    v.ackedId = id;

    if(id != 0)
    {
        v.ownSlot = idTableFindU32(ownTbl, MAX_RING, id);
    }

    v.lateAck = v.ownSlot < 0;

    if(v.isNew || v.ownSlot >= 0)
    {
        v.addRx = v.isNew;

        if(v.ownSlot >= 0)
        {
            v.phoneStatus = ackInfo || ownTbl[v.ownSlot][4] < 2;
            v.phoneFrame = v.phoneStatus && ackMsgIdFromNode(id, gwId);
            v.stopRing = true;
        }
        else
        {
            v.fwdCandidate = (v.frame[5] & 0x7F) > 0x00 && meshTxAllowed(MESH_TX_FOREIGN_RELAY, meshOn);
        }
    }

    return v;
}
