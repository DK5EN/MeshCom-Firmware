// gw_reg_state.h -- gateway registration state decision logic. Arduino-free, header-only (native test).
//
// A KEEP datagram goes to the MeshCom server (IP D). A reply from D within GW_REG_WINDOW_MS after the last
// KEEP whose payload contains "BLOCK" sets the registration-failed state. The state is stored as a flag plus
// the callsign at that moment; it is cleared when the callsign changes.
#ifndef GW_REG_STATE_H
#define GW_REG_STATE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define GW_REG_WINDOW_MS 10000u   // reply window after the last KEEP
#define GW_REG_CALL_LEN  10       // char[10] incl. NUL, same size as the callsign setting

struct GwRegState
{
    int flag;                    // 0/1
    char call[GW_REG_CALL_LEN];  // canonical callsign (with SSID) when the flag was set
};

// payload is binary-safe: length based, may contain NUL
inline bool gwRegIsBlockReply(const uint8_t *payload, size_t len)
{
    static const char key[] = "BLOCK";
    const size_t klen = sizeof(key) - 1;
    if (payload == NULL || len < klen)
        return false;
    for (size_t i = 0; i + klen <= len; i++)
        if (memcmp(payload + i, key, klen) == 0)
            return true;
    return false;
}

// both IPs as uint32 in the same byte order; zero never matches
inline bool gwRegSourceOk(uint32_t srcIp, uint32_t keepDestIp)
{
    return srcIp != 0 && srcIp == keepDestIp;
}

// wrap-safe: unsigned difference; the boundary itself (10000 ms) is outside
inline bool gwRegInWindow(uint32_t nowMs, uint32_t lastKeepMs, bool keepSent)
{
    return keepSent && (uint32_t)(nowMs - lastKeepMs) < GW_REG_WINDOW_MS;
}

inline bool gwRegShouldEnter(uint32_t srcIp, uint32_t keepDestIp, uint32_t nowMs, uint32_t lastKeepMs, bool keepSent,
                             const uint8_t *payload, size_t len)
{
    return gwRegSourceOk(srcIp, keepDestIp) && gwRegInWindow(nowMs, lastKeepMs, keepSent) &&
           gwRegIsBlockReply(payload, len);
}

// canonical form: spaces dropped at both ends, upper case, bounded to GW_REG_CALL_LEN - 1 chars
inline void gwRegCanon(const char *in, char *out)
{
    out[0] = 0;
    if (in == NULL)
        return;
    while (*in == ' ')
        in++;
    size_t n = 0;
    while (in[n] != 0 && n < GW_REG_CALL_LEN - 1)
    {
        char c = in[n];
        out[n] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
        n++;
    }
    while (n > 0 && out[n - 1] == ' ')
        n--;
    out[n] = 0;
}

// fail-closed on both sides: an empty stored call (corrupted record) or an empty current call (not set yet)
// counts as "not changed", the state stays
inline bool gwRegCallChanged(const char *storedCall, const char *currentCall)
{
    char a[GW_REG_CALL_LEN], b[GW_REG_CALL_LEN];
    gwRegCanon(storedCall, a);
    gwRegCanon(currentCall, b);
    if (a[0] == 0 || b[0] == 0)
        return false;
    return strcmp(a, b) != 0;
}

inline void gwRegEnter(GwRegState &s, const char *currentCall)
{
    s.flag = 1;
    gwRegCanon(currentCall, s.call);
}

// returns true if the state was cleared because the callsign changed
inline bool gwRegEvaluate(GwRegState &s, const char *currentCall)
{
    if (s.flag == 0 || !gwRegCallChanged(s.call, currentCall))
        return false;
    s.flag = 0;
    s.call[0] = 0;
    return true;
}

#endif
