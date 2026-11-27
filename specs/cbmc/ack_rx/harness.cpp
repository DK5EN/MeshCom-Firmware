// FORMAL-02 ESBMC harness: ackRxDecide() (src/ack_rx_core.h), the pure decision core of handleACK().
// Surface: LoRa RX, any 0x41 frame; payload is untrusted. Real code: ack_rx_core.h, id_table_find.h, ack_functions.h,
// ack_attribution.h, pn_retry.h, mesh_policy.h. Only configuration.h is a stub (stubs/, macros only).
// Contract: payload holds exactly `size` bytes (any read past it is an out-of-bounds error), content fully nondet;
// size 0..SZMAX; both tables fully nondet; gwId, ackInfo, meshOn nondet.
// Modes:
//   (default)      memory safety + verdict consistency against an independent re-computation
//   -DWITNESS_x    asserts the interesting verdict is NEVER produced -> must fail (path reachable):
//                  ACCEPT, FWD, STOP, LATE
//   -DMUT_*        run with a mutated copy of the source in gen/ (see the .check files)
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ack_rx_core.h>

#ifndef SZMAX
#define SZMAX 16
#endif

extern "C" unsigned char nondet_uchar();
extern "C" unsigned int nondet_uint();
extern "C" int nondet_int();

int main()
{
    uint16_t size = (uint16_t)nondet_uint();
    __ESBMC_assume(size <= SZMAX);
    uint8_t *payload = (uint8_t *)malloc(size);   // exactly size bytes; size 0 gives a zero-byte object
    __ESBMC_assume(payload != NULL);                // the caller always passes its receive buffer
    for(uint16_t i = 0; i < size; i++)
        payload[i] = nondet_uchar();

    uint8_t dedup[MAX_DEDUP_RING][5];
    uint8_t own[MAX_RING][5];
    for(int i = 0; i < MAX_DEDUP_RING; i++)
        for(int j = 0; j < 5; j++)
            dedup[i][j] = nondet_uchar();
    for(int i = 0; i < MAX_RING; i++)
        for(int j = 0; j < 5; j++)
            own[i][j] = nondet_uchar();

    uint32_t gwId = nondet_uint();
    bool ackInfo = nondet_int() != 0;
    bool meshOn = nondet_int() != 0;

    AckRxVerdict v = ackRxDecide(payload, size, dedup, own, gwId, ackInfo, meshOn);

#if defined(WITNESS_ACCEPT)
    assert(v.action != ACK_RX_ACCEPT);
#elif defined(WITNESS_FWD)
    assert(!v.fwdCandidate);
#elif defined(WITNESS_STOP)
    assert(!v.stopRing);
#elif defined(WITNESS_LATE)
    assert(!(v.action == ACK_RX_ACCEPT && v.lateAck));
#else
    if(v.action != ACK_RX_ACCEPT)
    {
        // not an ACK, or implausible: nothing to act on
        assert(v.dedupSlot == -1 && v.ownSlot == -1);
        assert(!v.isNew && !v.lateAck && !v.addRx && !v.phoneStatus && !v.phoneFrame && !v.stopRing && !v.fwdCandidate);
        assert(v.n == 0 && v.frameLen == 0);
        if(size < 12)
            assert(v.action == ACK_RX_NOT_ACK);
    }
    else
    {
        assert(size >= 12 && payload[0] == MSG_TYPE_ACK);
        assert(v.n == 0 || v.n == ACK_WIRE_APPENDIX_LEN);
        assert(v.frameLen == 12 + v.n);
        assert(v.frameLen <= sizeof(v.frame));
        assert(size >= v.frameLen);                      // the copy stayed inside the payload
        assert(memcmp(v.frame, payload, v.frameLen) == 0);
        assert((payload[5] & 0x80) == 0x80 && (payload[5] & 0x7F) <= MAX_HOP_LIMIT);

        // dedup: independent loop
        int dfirst = -1;
        for(int i = 0; i < MAX_DEDUP_RING; i++)
            if(dedup[i][0] == payload[1] && dedup[i][1] == payload[2] && dedup[i][2] == payload[3] && dedup[i][3] == payload[4])
            {
                dfirst = i;
                break;
            }
        assert(v.isNew == (dfirst < 0));
        assert(v.dedupSlot == dfirst);
        assert(v.dedupSlot == -1 || (v.dedupSlot >= 0 && v.dedupSlot < MAX_DEDUP_RING));
        uint32_t rx = (uint32_t)payload[1] | ((uint32_t)payload[2] << 8) | ((uint32_t)payload[3] << 16) | ((uint32_t)payload[4] << 24);
        assert(v.rxId == rx);
        assert(v.serverFlag);

        // own table: independent loop on the folded id
        uint32_t acked = (uint32_t)payload[6] | ((uint32_t)payload[7] << 8) | ((uint32_t)payload[8] << 16) | ((uint32_t)payload[9] << 24);
        uint32_t folded = pnOwnTxLookupId(acked, gwId);
        assert(v.ackedId == folded);
        int ofirst = -1;
        if(folded != 0)
            for(int i = 0; i < MAX_RING; i++)
            {
                uint32_t rid = (uint32_t)own[i][0] | ((uint32_t)own[i][1] << 8) | ((uint32_t)own[i][2] << 16) | ((uint32_t)own[i][3] << 24);
                if(rid == folded)
                {
                    ofirst = i;
                    break;
                }
            }
        assert(v.ownSlot == ofirst);
        assert(v.ownSlot >= -1 && v.ownSlot < MAX_RING);
        assert(v.lateAck == (v.ownSlot < 0));
        assert(v.stopRing == (v.ownSlot >= 0));
        if(v.ownSlot >= 0)
        {
            assert(folded != 0);
            assert(v.phoneStatus == (ackInfo || own[v.ownSlot][4] < 2));
            assert(!v.phoneFrame || v.phoneStatus);
            assert(!v.fwdCandidate);
        }
        else
        {
            assert(!v.phoneStatus && !v.phoneFrame);
        }
        assert(v.addRx == v.isNew);
        if(v.fwdCandidate)
        {
            assert(v.ownSlot < 0 && v.isNew && meshOn && (payload[5] & 0x7F) > 0);
        }
        if(v.ownSlot < 0 && v.isNew && (payload[5] & 0x7F) > 0 && meshOn)
            assert(v.fwdCandidate);
        if(v.n == ACK_WIRE_APPENDIX_LEN)
            assert(v.appendixHash == (((uint32_t)payload[12] | ((uint32_t)payload[13] << 8) | ((uint32_t)payload[14] << 16)) & 0x3FFFFF));
    }
#endif
    return 0;
}
