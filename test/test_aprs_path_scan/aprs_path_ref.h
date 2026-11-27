// Reference for test_aprs_path_scan: the byte-by-byte path loops of decodeAPRS() as they stood before
// B-APRS-PATH-01 (src/aprs_functions.cpp at fork-dev cdfc76ea, lines 233-282 and 344-377), kept verbatim in
// their logic so the range scan in src/aprs_path_scan.h can be compared against them field by field.
#ifndef APRS_PATH_REF_H
#define APRS_PATH_REF_H

#include <stdint.h>
#include <string.h>
#include "aprs_path_scan.h"
#include "mc_text.h"

#define REF_BUF 255        // UDP_TX_BUF_SIZE
#define REF_PATH_LEN 121   // MC_PATH_LEN
#define REF_CALL_LEN_Z 21  // MC_CALL_LEN_Z

struct PathOut
{
    char path[REF_PATH_LEN];
    char last[REF_CALL_LEN_Z];
    char call[REF_CALL_LEN_Z]; // source field only
    uint8_t cnt;               // source field only: msg_last_path_cnt
    bool found;
    uint16_t inext;            // unchanged (= inext_in) unless found
};

// Old loop. source=true: the source path loop (term '>', fills call and cnt); false: the destination loop.
static inline void refScan(const uint8_t *RcvBuffer, uint16_t start, uint16_t rsize, uint8_t term, bool source,
                           uint16_t inext_in, PathOut &o)
{
    memset(&o, 0, sizeof(o));
    uint16_t inext = inext_in;
    bool bEndOk = false;
    bool bSourceCall = true;
    uint8_t cnt = 1;
    char cConcat1[REF_BUF];
    memset(cConcat1, 0x00, REF_BUF);
    int iConcat1 = 0;
    char cConcat2[REF_BUF];
    memset(cConcat2, 0x00, REF_BUF);
    int iConcat2 = 0;
    char cConcat3[REF_BUF];
    memset(cConcat3, 0x00, REF_BUF);
    int iConcat3 = 0;
    for (uint16_t ib = start; ib < rsize && (ib - start) < 120; ib++)
    {
        if (RcvBuffer[ib] == term)
        {
            inext = ib + 1;
            bEndOk = true;
            break;
        }
        else
        {
            if (RcvBuffer[ib] < 0x20 || RcvBuffer[ib] > 0x7E)
                break;
            cConcat1[iConcat1] = (char)RcvBuffer[ib];
            iConcat1++;
            if (RcvBuffer[ib] == ',')
            {
                if (source)
                {
                    cnt++;
                    bSourceCall = false;
                }
                memset(cConcat2, 0x00, REF_BUF);
                iConcat2 = 0;
            }
            else
            {
                cConcat2[iConcat2] = (char)RcvBuffer[ib];
                iConcat2++;
            }
            if (source && bSourceCall)
            {
                cConcat3[iConcat3] = (char)RcvBuffer[ib];
                iConcat3++;
            }
        }
    }
    mcSet(o.path, sizeof(o.path), cConcat1);
    mcSet(o.last, sizeof(o.last), cConcat2);
    if (source)
    {
        mcSet(o.call, sizeof(o.call), cConcat3);
        o.cnt = cnt;
    }
    o.found = bEndOk;
    o.inext = inext;
}

// New range scan, used exactly as decodeAPRS() uses it now.
static inline void newScan(const uint8_t *RcvBuffer, uint16_t start, uint16_t rsize, uint8_t term, bool source,
                           uint16_t inext_in, PathOut &o)
{
    memset(&o, 0, sizeof(o));
    const AprsPathField f = aprsPathScan(RcvBuffer, start, rsize, term);
    o.inext = f.found ? (uint16_t)(f.end + 1) : inext_in;
    o.found = f.found;
    aprsPathCopy(o.path, sizeof(o.path), RcvBuffer, f.start, f.end);
    aprsPathCopy(o.last, sizeof(o.last), RcvBuffer, f.last_start, f.end);
    if (source)
    {
        aprsPathCopy(o.call, sizeof(o.call), RcvBuffer, f.start, f.first_end);
        o.cnt = (uint8_t)(1 + f.commas);
    }
}

// 0 when both agree, else a short description of the first differing field.
static inline const char *pathDiff(const PathOut &a, const PathOut &b)
{
    if (a.found != b.found) return "found";
    if (a.inext != b.inext) return "inext";
    if (a.cnt != b.cnt) return "cnt";
    if (strcmp(a.path, b.path) != 0) return "path";
    if (strcmp(a.last, b.last) != 0) return "last";
    if (strcmp(a.call, b.call) != 0) return "call";
    return 0;
}

// Payload: the former copy loop of decodeAPRS() (R2-04 form) against aprsPayloadScan() plus one copy.
struct PayloadOut
{
    int len;
    char text[REF_BUF];
    bool found;
    uint16_t inext;
};

static inline void refPayload(const uint8_t *RcvBuffer, uint16_t inext_in, uint16_t rsize, PayloadOut &o)
{
    memset(&o, 0, sizeof(o));
    uint16_t inext = inext_in;
    bool bPayloadEndOk = false;
    char cConcat1[REF_BUF];
    memset(cConcat1, 0x00, REF_BUF);
    int iConcat1 = 0;
    for (uint16_t ib = inext; ib < rsize && iConcat1 < (int)sizeof(cConcat1) - 1; ib++)
    {
        if (RcvBuffer[ib] == 0x00)
        {
            inext = ib + 1;
            bPayloadEndOk = true;
            break;
        }
        else
        {
            cConcat1[iConcat1] = (char)RcvBuffer[ib];
            iConcat1++;
        }
    }
    o.len = iConcat1;
    memcpy(o.text, cConcat1, sizeof(o.text));
    o.found = bPayloadEndOk;
    o.inext = inext;
}

static inline void newPayload(const uint8_t *RcvBuffer, uint16_t inext_in, uint16_t rsize, PayloadOut &o)
{
    memset(&o, 0, sizeof(o));
    char cConcat1[REF_BUF];
    memset(cConcat1, 0x00, REF_BUF);
    bool found = false;
    const uint16_t pend = aprsPayloadScan(RcvBuffer, inext_in, rsize, (uint16_t)(sizeof(cConcat1) - 1), &found);
    const int n = (int)(pend - inext_in);
    if (n > 0)
        memcpy(cConcat1, RcvBuffer + inext_in, (size_t)n);
    o.len = n;
    memcpy(o.text, cConcat1, sizeof(o.text));
    o.found = found;
    o.inext = found ? (uint16_t)(pend + 1) : inext_in;
}

static inline const char *payloadDiff(const PayloadOut &a, const PayloadOut &b)
{
    if (a.found != b.found) return "payload found";
    if (a.inext != b.inext) return "payload inext";
    if (a.len != b.len) return "payload len";
    if (memcmp(a.text, b.text, sizeof(a.text)) != 0) return "payload bytes";
    return 0;
}

#endif
