// FORMAL-02 harness: phoneFrameDecode() (src/phone_frame.h) for every frame of 0..LMAX bytes.
// Surface: BLE / AppLink frames from the phone, untrusted bytes. Contract: buf is allocated with EXACTLY len bytes
// (any read at or beyond len is an out-of-bounds dereference), content nondet, out pre-filled with a sentinel.
// Proves: no read past len, status consistency, every string field NUL-terminated inside its array, lengths <= arrays.
// Modes (-D):
//   LMAX=n          longest frame (default 12)
//   TYPE=0xNN       type byte fixed (shaped input), otherwise nondet
//   WIT_xxx         witness: asserts the path is NOT reached -> must fail
//     WIT_HELLO_PIN, WIT_HELLO_NOPIN, WIT_TIME, WIT_TEXT, WIT_CLAIMS, WIT_TRUNC
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include "phone_frame.h"

#ifndef LMAX
#define LMAX 12
#endif

extern "C" int nondet_int();
extern "C" unsigned char nondet_uchar();

static bool nul_within(const char *s, size_t cap)
{
    for (size_t i = 0; i < cap; i++)
        if (s[i] == 0)
            return true;
    return false;
}

int main()
{
    int n = nondet_int();
    __ESBMC_assume(n >= 0 && n <= LMAX);
    size_t len = (size_t)n;
    uint8_t *buf = (uint8_t *)malloc(len > 0 ? len : 1); // exact size; a zero-length frame is never read
    __ESBMC_assume(buf != NULL);                          // allocation failure is not the property under test
    if (len > 0)
    {
        for (size_t i = 0; i < len; i++)
            buf[i] = nondet_uchar();
    }
#ifdef TYPE
    if (len >= 2)
        buf[1] = TYPE;
#endif

    PhoneFrame out;
    memset(&out, 0xA5, sizeof(out));
    phoneFrameDecode(buf, len, &out);

    assert(out.status <= PF_TRUNCATED);
    if (len < 2)
        assert(out.status == PF_TOO_SHORT && out.cmd == PFC_NONE);
    else if (len > PHONE_FRAME_MAX)
        assert(out.status == PF_TOO_LONG && out.cmd == PFC_NONE);
    else if (buf[0] > len)
        assert(out.status == PF_CLAIMS_MORE && out.cmd == PFC_NONE);
    else
        assert(out.status == PF_OK || out.status == PF_TRUNCATED);
    if (out.status == PF_OK)
    {
        assert(len >= 2 && len <= PHONE_FRAME_MAX);
        assert(out.msg_len <= len);
        assert(out.msg_len == buf[0] && out.type == buf[1]);
    }
    else
        assert(out.cmd == PFC_NONE);

    if (out.cmd == PFC_CALL)
    {
        assert(out.dlen <= PHONE_FRAME_DATA_MAX - 1);
        assert(out.u.data[out.dlen] == 0);
        assert(out.dlen == buf[2] && (size_t)out.dlen + 3 <= len);
    }
    if (out.cmd == PFC_TEXT)
    {
        assert(out.dlen <= PHONE_FRAME_DATA_MAX - 1);
        assert(out.u.data[out.dlen] == 0);
        assert(out.dlen + 2 == out.msg_len);
    }
    if (out.cmd == PFC_WIFI)
    {
        assert(out.ssid_len > 0 && out.pwd_len > 0);
        assert((size_t)out.ssid_len + out.pwd_len + 4 <= out.msg_len);
        assert(nul_within(out.u.wifi.ssid, PHONE_FRAME_SSID_CAP));
        assert(nul_within(out.u.wifi.pwd, PHONE_FRAME_PWD_CAP));
    }
    if (out.cmd == PFC_HELLO && out.has_hash)
        assert(len >= 36 && out.msg_len >= 35);

#if defined(WIT_HELLO_PIN)
    assert(!(out.status == PF_OK && out.cmd == PFC_HELLO && out.has_hash));
#elif defined(WIT_HELLO_NOPIN)
    assert(!(out.status == PF_OK && out.cmd == PFC_HELLO && !out.has_hash));
#elif defined(WIT_TIME)
    assert(!(out.status == PF_OK && out.cmd == PFC_TIME));
#elif defined(WIT_TEXT)
    assert(!(out.status == PF_OK && out.cmd == PFC_TEXT));
#elif defined(WIT_CLAIMS)
    assert(out.status != PF_CLAIMS_MORE);
#elif defined(WIT_TRUNC)
    assert(out.status != PF_TRUNCATED);
#endif
    free(buf);
    return 0;
}
