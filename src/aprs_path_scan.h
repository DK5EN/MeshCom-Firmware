// aprs_path_scan.h -- range-based scan of the source and destination path of a MeshCom APRS frame
// (pure, header-only, no Arduino; host test: test/test_aprs_path_scan, BMC: specs/cbmc/aprs/).
//
// decodeAPRS() used to copy the path byte by byte into three 255-byte stack buffers, each with its own
// counter that grew with every byte and restarted at every comma. Correct, but the write position then
// depended on the whole byte history, and the bounded model checker could not close the proof for frames
// of 40 bytes or more (memout at 15 GB, 2026-11-26). Here a field is first scanned for its end, then split
// at the first and last comma, and every output is one contiguous range of the receive buffer copied once.
// The result is byte-identical to the old loops (differential test in test_aprs_path_scan).
//
// Field rules (unchanged):
//   - a field starts at `start` and ends before `term` ('>' for the source path, the payload type for the
//     destination path), before the first byte outside 0x20..0x7E, at `rsize`, or after APRS_PATH_SCAN_MAX
//     bytes, whichever comes first; `found` is true only when `term` ended it;
//   - path  = the whole field;
//   - first = the field up to the first comma (the source call);
//   - last  = the field after the last comma (the last hop, or the destination call);
//   - commas = number of commas in the field.
#ifndef APRS_PATH_SCAN_H
#define APRS_PATH_SCAN_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define APRS_PATH_SCAN_MAX 120 // longest path field the decoder looks at (bytes)

struct AprsPathField
{
    uint16_t start;      // index of the first field byte
    uint16_t end;        // index after the last field byte (the terminator's index when found)
    uint16_t first_end;  // index of the first comma, or end
    uint16_t last_start; // index after the last comma, or start
    uint16_t commas;     // number of commas in [start, end)
    bool found;          // the field ended at term
};

// Scan one path field of buf[0..rsize) from start. rsize is the frame length (decodeAPRS caps it at
// MAX_APRS_FRAME_SIZE); start may be >= rsize, then the field is empty.
static inline AprsPathField aprsPathScan(const uint8_t *buf, uint16_t start, uint16_t rsize, uint8_t term)
{
    AprsPathField f;
    f.start = start;
    f.end = start;
    f.first_end = start;
    f.last_start = start;
    f.commas = 0;
    f.found = false;

    bool have_comma = false;
    for (uint16_t n = 0; n < APRS_PATH_SCAN_MAX; n++)
    {
        const uint16_t ib = (uint16_t)(start + n);
        if (ib >= rsize)
            break;
        const uint8_t c = buf[ib];
        if (c == term)
        {
            f.found = true;
            break;
        }
        if (c < 0x20 || c > 0x7E)
            break;
        if (c == ',')
        {
            if (!have_comma)
                f.first_end = ib;
            have_comma = true;
            f.last_start = (uint16_t)(ib + 1);
            f.commas++;
        }
        f.end = (uint16_t)(ib + 1);
    }
    if (!have_comma)
        f.first_end = f.end;
    return f;
}

// Scan the payload of buf[0..rsize) from start: it ends before the first NUL (then *found = true), at rsize, or
// after maxlen bytes, whichever comes first; returns the end index. Same bounds as the former copy loop of
// decodeAPRS() (B-APRS-PATH-01): a NUL right after maxlen bytes is not seen.
static inline uint16_t aprsPayloadScan(const uint8_t *buf, uint16_t start, uint16_t rsize, uint16_t maxlen,
                                       bool *found)
{
    *found = false;
    uint16_t ib = start;
    while (ib < rsize && (uint16_t)(ib - start) < maxlen)
    {
        if (buf[ib] == 0x00)
        {
            *found = true;
            break;
        }
        ib++;
    }
    return ib;
}

// Frame check sequence of a received frame: the plain sum of buf[0..n) (n = the bytes before the FCS field).
// B-APRS-PATH-01: its own function so the bounded model checker proves it once on its own (specs/cbmc/aprs/fcs.check)
// and the full-frame decodeAPRS() proof can use its contract instead of unrolling up to 255 additions over a variable
// length (that sum was what ran the full-frame proof out of 15 GB). A proof harness that defines APRS_FCS_EXTERNAL
// supplies that contract; the firmware never defines it.
#ifndef APRS_FCS_EXTERNAL
static inline unsigned int aprsFcsSum(const uint8_t *buf, uint16_t n)
{
    unsigned int sum = 0;
    for (uint16_t i = 0; i < n; i++)
        sum += (unsigned int)buf[i];
    return sum;
}
#else
unsigned int aprsFcsSum(const uint8_t *buf, uint16_t n);
#endif

// Copy buf[from..to) into dst as a C string, cut at dstsz - 1 bytes (the rule of mcSet()).
static inline void aprsPathCopy(char *dst, size_t dstsz, const uint8_t *buf, uint16_t from, uint16_t to)
{
    if (dst == 0 || dstsz == 0)
        return;
    size_t n = (to > from) ? (size_t)(to - from) : 0;
    if (n > dstsz - 1)
        n = dstsz - 1;
    if (n > 0)
        memcpy(dst, buf + from, n);
    dst[n] = 0;
}

#endif // APRS_PATH_SCAN_H
