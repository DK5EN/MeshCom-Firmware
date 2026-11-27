// Request body accumulator (FORMAL-02 wave 7a, docs/web-server-seams-20261127.md section 6.5).
//
// Header-only, no String, no Arduino: the pure part of the two body readers that exist today,
//   web_read_body()  (web_functions.cpp, cap CONFIG_JSON_MAX + 1 = 6145)  -> int: bytes, -1, -2, -3
//   rmReadBody()     (web_rm_handlers.cpp, cap RM_BODY_MAX + 1 = 261)      -> const char *: nullptr, "size", "short"
// Both readers share one shape: reject a bad length, loop reading bytes while got < Content-Length,
// NUL-terminate, compare got with Content-Length. Only the I/O loop differs (deadlines, yield, WDT);
// that stays outside this header.
//
// Return-code mapping (1:1 with today):
//   code          value  web_read_body        rmReadBody         when
//   0             0      bytes read (> 0)     nullptr            got == want
//   WB_NOLEN     -1      -1                   "size"             Content-Length <= 0 (absent, invalid, zero, negative)
//   WB_SIZE      -2      -2                   "size"             Content-Length >= cap (web_read_body: > CONFIG_JSON_MAX
//                                                                 with cap = CONFIG_JSON_MAX + 1, same set of values)
//   WB_SHORT     -3      -3                   "short"            source stalled/closed before want bytes
// web_read_body returns a.got on success (End gives 0, the caller reads a.got); rmReadBody folds
// NOLEN and SIZE into "size".
//
// Content-Length is a long (webHeadContentLength: exact atol, may be negative or huge). webBodyBegin
// compares it as long BEFORE narrowing: <= 0 is WB_NOLEN (LONG_MIN included), every value >= cap is
// WB_SIZE, so anything above INT32_MAX or 65535 is WB_SIZE for any realistic cap and never truncates
// into a small accepted length. Only a value < cap (cap <= 65535) is stored in the int32 want.
// cap is clamped to 65535 (uint16_t fields); cap == 0 leaves no room for the NUL and is WB_SIZE.
//
// The I/O glue (web_io.cpp, since 7b step 7):
//   int webReadBody(char *buf, size_t cap, long content_length, uint32_t idle_ms, uint32_t total_ms)
//     1. webBodyBegin; on a negative code return it (buf[0] is already '\0').
//     2. loop while a.got < a.want and (now - last_byte) <= idle_ms and (now - start) <= total_ms:
//        yield() and meshcom_wdt_feed(); if a byte is available read it (a negative read breaks the loop),
//        webBodyPush, last_byte = now; else if the client is no longer connected break.
//        Deadlines are inclusive (<=) as before. web_read_body: idle WEB_TIMEOUT_TIME, total 4000 ms;
//        rmReadBody: idle RM_BODY_TIMEOUT_MS (2000), total RM_BODY_TOTAL_MS (3000).
//     3. webBodyEnd: 0 -> return a.got (bytes read); WB_SHORT -> return WB_SHORT (B10: the HTTP layer
//        answers 408 for a stalled body). Callers map the code to their own error (see table above):
//        web_read_body returns it as is (sub_config_upload: 408 "incomplete upload"), rmReadBody maps
//        NOLEN/SIZE -> "size", SHORT -> "short" (rmAnswer / sub_rm_send: 408 for "short", else 422).
#pragma once

#include <stddef.h>
#include <stdint.h>

#define WB_NOLEN (-1)
#define WB_SIZE (-2)
#define WB_SHORT (-3)

struct WebBodyAcc
{
    char    *buf;
    uint16_t cap, got;
    int32_t  want;
};

// Starts an accumulation. buf[0] is set to '\0' whenever cap > 0 (as both readers do before any check).
// Returns 0, WB_NOLEN (content_length <= 0) or WB_SIZE (content_length >= cap, or cap == 0).
inline int8_t webBodyBegin(WebBodyAcc *a, char *buf, size_t cap, long content_length)
{
    if (cap > 65535u)
        cap = 65535u;
    a->buf  = buf;
    a->cap  = (uint16_t)cap;
    a->got  = 0;
    a->want = 0;
    if (cap > 0)
        buf[0] = '\0';
    if (content_length <= 0)
        return WB_NOLEN;
    if (cap == 0 || content_length >= (long)cap) // compared as long: no truncation of huge values
        return WB_SIZE;
    a->want = (int32_t)content_length;
    return 0;
}

// Stores one byte. false (nothing written) once got >= want; want < cap, so never writes at or past cap.
inline bool webBodyPush(WebBodyAcc *a, char c)
{
    if (a->want <= 0 || (int32_t)a->got >= a->want || a->got >= a->cap)
        return false;
    a->buf[a->got++] = c;
    return true;
}

// NUL-terminates at buf[got]. 0 when got == want, WB_SHORT otherwise. A Begin that failed (want == 0)
// gives WB_SHORT here; the caller has already returned the Begin code.
inline int8_t webBodyEnd(WebBodyAcc *a)
{
    if (a->cap > 0 && a->got < a->cap)
        a->buf[a->got] = '\0';
    return (a->want > 0 && (int32_t)a->got == a->want) ? 0 : WB_SHORT;
}
