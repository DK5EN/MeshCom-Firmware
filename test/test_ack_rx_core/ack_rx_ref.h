// ack_rx_ref.h -- FROZEN copy of the decision logic of handleACK() (lora_functions.cpp, as of 770764e5,
// before FORMAL-02 wave 1). Do not "improve" it: the new core src/ack_rx_core.h is compared against it.
// Changes against the original text, and only these:
//  - every side effect is an Ev appended to a recorder (same order as the call order of the original);
//  - the globals it read are fields of RefEnv (tables, gw id, flags); is_new_packet(), checkOwnTx(),
//    checkServerRx(), addLoraRxBuffer() and the TX ring stop run against the RefEnv copies;
//  - direct payload reads go through RD() so a read at index >= size is noticed (oob).
#pragma once

#include <stdint.h>
#include <string.h>
#include <vector>

#include <configuration.h>
#include <ack_functions.h>
#include <ack_attribution.h>
#include <pn_retry.h>
#include <mesh_policy.h>

enum EvKind
{
    EV_RETURN_FALSE = 1,    // a=reason: 1 type/size, 2 implausible
    EV_DBG_REJECT,          // a=b5 b=size
    EV_DBG_APPENDIX,        // a=hash b=size
    EV_DBG_DEDUP,           // a=id b=slot (-1 = new)
    EV_DEDUP_COUNT,         // a=isNew
    EV_DISPLAY_LOG,         // a=!isNew, buf = payload copy, len=size
    EV_CHECKOWN_LOG,        // a=id b=slot
    EV_LATE_STATUS,         // a=id: ownMsgStatusSet(id, 0x02)
    EV_ADD_RX,              // a=mid b=serverFlag
    EV_PHONE_FRAME,         // a=ackedId (buildAckPhoneFrame + addBLEOutBuffer)
    EV_GW_ACK_COUNT,        // dmstat_gw_ack++
    EV_PHONE_LOG,           // a=ackedId
    EV_SET_STATUS_ACK,      // a=slot
    EV_STOP_RING,           // a=ackedId b=returned slot
    EV_RETX_LOG,            // a=slot b=ackedId
    EV_SERVER_RX_ASK,       // buf = key bytes (4)
    EV_FWD_TX,              // buf = frame, len
    EV_FWD_LOG,
    EV_NEWLINE,
    EV_RETURN_TRUE
};

struct Ev
{
    int kind;
    uint32_t a, b;
    uint8_t buf[ACK_WIRE_MAX_LEN];
    uint16_t len;
};

struct RefEnv
{
    uint8_t dedup[MAX_DEDUP_RING][5];
    uint8_t own[MAX_RING][5];
    uint8_t loraWrite;
    uint32_t gwId;
    bool ackInfo, meshOn;
    std::vector<Ev> ev;
    bool oob;
};

static inline void refEv(RefEnv &e, int kind, uint32_t a = 0, uint32_t b = 0, const uint8_t *buf = NULL, uint16_t len = 0)
{
    Ev x;
    memset(&x, 0, sizeof(x));
    x.kind = kind;
    x.a = a;
    x.b = b;
    if(buf != NULL && len > 0)
        memcpy(x.buf, buf, len > sizeof(x.buf) ? sizeof(x.buf) : len);
    x.len = len;
    e.ev.push_back(x);
}

// the TX ring stop is out of scope of the decision: a deterministic stand-in
static inline int refStopRing(uint32_t id)
{
    return (id % 3 == 0) ? (int)(id % MAX_RING) : -1;
}

// dedup ring insert as addLoraRxBuffer() does it
static inline void refAddRx(RefEnv &e, uint32_t mid, bool srv)
{
    uint8_t slot = e.loraWrite;
    e.dedup[slot][3] = mid >> 24;
    e.dedup[slot][2] = mid >> 16;
    e.dedup[slot][1] = mid >> 8;
    e.dedup[slot][0] = mid;
    e.dedup[slot][4] = srv ? 1 : 0;
    uint8_t next = slot + 1;
    if(next >= MAX_DEDUP_RING)
        next = 0;
    e.loraWrite = next;
}

static inline bool refCheckServerRx(RefEnv &e, const uint8_t *key)
{
    for(int ilo = 0; ilo < MAX_DEDUP_RING; ilo++)
    {
        if(memcmp(e.dedup[ilo], key, 4) == 0)
        {
            if(e.dedup[ilo][4] == 1)
                return true;
            break;
        }
    }
    return false;
}

static inline bool ackRxRef(RefEnv &e, const uint8_t *payload, uint16_t size)
{
#define RD(i) (((i) >= size ? (e.oob = true) : false), payload[(i)])
    if(RD(0) != MSG_TYPE_ACK)
    {
        refEv(e, EV_RETURN_FALSE, 1);
        return false;
    }

    if(size < 12)
    {
        refEv(e, EV_RETURN_FALSE, 1);
        return false;
    }

    if(!isPlausibleAckFrame(payload, size, MAX_HOP_LIMIT))
    {
        refEv(e, EV_DBG_REJECT, RD(5), size);
        refEv(e, EV_RETURN_FALSE, 2);
        return false;
    }

    uint8_t print_buff[30];

    uint8_t n = ackWireAppendixLen(payload, size);
    memcpy(print_buff, payload, 12 + n);

    if(n == ACK_WIRE_APPENDIX_LEN)
        refEv(e, EV_DBG_APPENDIX, ackWireHash(payload), size);

    // is_new_packet(print_buff+1)
    bool isNewRaw = true;
    {
        int slot = -1;
        for(int ib = 0; ib < MAX_DEDUP_RING; ib++)
        {
            if(memcmp(print_buff + 1, e.dedup[ib], 4) == 0)
            {
                slot = ib;
                break;
            }
        }
        uint32_t id = (uint32_t)print_buff[1] | ((uint32_t)print_buff[2] << 8) | ((uint32_t)print_buff[3] << 16) | ((uint32_t)print_buff[4] << 24);
        refEv(e, EV_DBG_DEDUP, id, (uint32_t)slot);
        isNewRaw = slot < 0;
    }
    bool bIsNew = isNewRaw;
    refEv(e, EV_DEDUP_COUNT, bIsNew);

    refEv(e, EV_DISPLAY_LOG, !bIsNew, 0, payload, size);

    bool bServerFlag = false;
    if((print_buff[5] & 0x80) == 0x80)
        bServerFlag=true;

    unsigned msg_id = print_buff[6] | (print_buff[7] << 8) | (print_buff[8] << 16) | (print_buff[9] << 24);

    msg_id = pnOwnTxLookupId(msg_id, e.gwId);

    // checkOwnTx(msg_id)
    int itxcheck = -1;
    if(msg_id != 0)
    {
        for(int ilo = 0; ilo < MAX_RING; ilo++)
        {
            unsigned own_id = e.own[ilo][0] | (e.own[ilo][1] << 8) | (e.own[ilo][2] << 16) | (e.own[ilo][3] << 24);
            if(own_id == msg_id)
            {
                refEv(e, EV_CHECKOWN_LOG, msg_id, ilo);
                itxcheck = ilo;
                break;
            }
        }
    }
    if(itxcheck < 0)
        refEv(e, EV_LATE_STATUS, msg_id);

    if(bIsNew || itxcheck >= 0)
    {
        if(bIsNew)
        {
            unsigned int mid=(print_buff[1]) | (print_buff[2]<<8) | (print_buff[3]<<16) | (print_buff[4]<<24);
            refEv(e, EV_ADD_RX, mid, bServerFlag);
            refAddRx(e, mid, bServerFlag);
        }

        if(itxcheck >= 0)
        {
            if(e.ackInfo || e.own[itxcheck][4] < 2)
            {
                if(ackMsgIdFromNode(msg_id, e.gwId))
                {
                    refEv(e, EV_PHONE_FRAME, msg_id);
                    refEv(e, EV_GW_ACK_COUNT);
                    refEv(e, EV_PHONE_LOG, msg_id);
                }

                refEv(e, EV_SET_STATUS_ACK, itxcheck);
                e.own[itxcheck][4] = 0x02;
            }

            int ackSlot = refStopRing(msg_id);
            refEv(e, EV_STOP_RING, msg_id, (uint32_t)ackSlot);
            if(ackSlot >= 0)
                refEv(e, EV_RETX_LOG, ackSlot, msg_id);
        }
        else
        {
            if((print_buff[5] & 0x7F) > 0x00 && meshTxAllowed(MESH_TX_FOREIGN_RELAY, e.meshOn) && itxcheck < 0
               && (refEv(e, EV_SERVER_RX_ASK, 0, 0, print_buff + 6, 4), !refCheckServerRx(e, print_buff + 6)))
            {
                print_buff[5]--;

                refEv(e, EV_FWD_TX, 0, 0, print_buff, 12 + n);
                refEv(e, EV_FWD_LOG);
            }
        }
    }

    refEv(e, EV_NEWLINE);
    refEv(e, EV_RETURN_TRUE);
    return true;
#undef RD
}
