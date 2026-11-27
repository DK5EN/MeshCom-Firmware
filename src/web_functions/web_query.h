// web_query.h -- query field extractor of the web server (FORMAL-02 wave 7a, seams doc 6.4).
// Header-only, no String, no Arduino. Not wired into production yet.
//
// A query is the text after '?' of the request target (WebReq::query), without the " HTTP/1.1" tail.
//   webQueryFirst  "name=value": key up to the first '=', val = the rest (may hold '=' and '&'); false without '=' (D2, B4).
//                  An empty key ("=x") is returned as key.n == 0: the caller decides.
//   webQueryGet    field `key` (exact, first match), val up to the next '&'. A field starts at the query start or after '&'.
//   webQueryTail   field `key` (exact, first match), val = everything after "key=" to the end (today's `message` rule).
//   webQueryUntil  field `key` as above, val up to the first occurrence of the literal `stop` after "key=", or to the end
//                  (today's `tocall` rule: it ends at indexOf("&message"), not at the next '&'; documented usage
//                  webQueryUntil(q, "tocall", "&message", &v)). Differs from today only when "&message" comes BEFORE
//                  "&tocall=" (today substring() swaps its bounds and tocall becomes the text between the two markers).
//   webFieldDecode copy + decode into out[cap+1]; ret = decoded length, NUL at out[ret]. Streams straight into out (urlDecodeTo),
//                  no temporary buffer. (size_t)-1 when the decoded value does not fit (D11/B7: 422 instead of silent cut)
//                  unless policy == URLD_TRUNC (cut at cap on a UTF-8 boundary), or when STRICT rejects; out[0] is 0 then.
// Shared decoder: urlDecodeTo() of url_decode.h (url_decode.cpp must be linked). Caps by the caller (seams doc 6.4).
#ifndef WEB_QUERY_H
#define WEB_QUERY_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "web_request.h"
#include "url_decode.h"


namespace webquery_detail
{
// field `key` at query start or after '&'; on a hit *vstart = offset after "key=", true.
inline bool findKey(WebSpan q, const char *key, size_t *vstart)
{
    const size_t kl = webLitLen(key);
    if (kl == 0 || q.p == NULL)
        return false;
    for (size_t i = 0; i < q.n; i++)
    {
        if (i != 0 && q.p[i - 1] != '&')
            continue;
        if ((size_t)q.n - i < kl + 1)
            return false;
        if (memcmp(q.p + i, key, kl) == 0 && q.p[i + kl] == '=')
        {
            *vstart = i + kl + 1;
            return true;
        }
    }
    return false;
}
} // namespace webquery_detail

inline bool webQueryFirst(WebSpan query, WebSpan *key, WebSpan *val)
{
    if (query.p == NULL)
        return false;
    for (size_t i = 0; i < query.n; i++)
    {
        if (query.p[i] == '=')
        {
            key->p = query.p;
            key->n = (uint16_t)i;
            val->p = query.p + i + 1;
            val->n = (uint16_t)(query.n - i - 1);
            return true;
        }
    }
    return false;
}

inline bool webQueryGet(WebSpan query, const char *key, WebSpan *val)
{
    size_t vs = 0;
    if (!webquery_detail::findKey(query, key, &vs))
        return false;
    size_t e = vs;
    while (e < query.n && query.p[e] != '&')
        e++;
    val->p = query.p + vs;
    val->n = (uint16_t)(e - vs);
    return true;
}

inline bool webQueryTail(WebSpan query, const char *key, WebSpan *val)
{
    size_t vs = 0;
    if (!webquery_detail::findKey(query, key, &vs))
        return false;
    val->p = query.p + vs;
    val->n = (uint16_t)(query.n - vs);
    return true;
}

inline bool webQueryUntil(WebSpan query, const char *key, const char *stop, WebSpan *val)
{
    size_t vs = 0;
    if (!webquery_detail::findKey(query, key, &vs))
        return false;
    const size_t sl = webLitLen(stop);
    size_t e = query.n;
    for (size_t i = vs; sl > 0 && i + sl <= query.n; i++)
    {
        if (memcmp(query.p + i, stop, sl) == 0)
        {
            e = i;
            break;
        }
    }
    val->p = query.p + vs;
    val->n = (uint16_t)(e - vs);
    return true;
}

inline size_t webFieldDecode(WebSpan in, char *out, size_t cap, uint8_t policy)
{
    const size_t NOFIT = (size_t)-1;
    if (out == NULL)
        return NOFIT;
    out[0] = 0;
    if (in.p == NULL || in.n == 0)
        return 0;
    const size_t r = urlDecodeTo(in.p, in.n, out, cap, policy);
    if (r == NOFIT)
        out[0] = 0; // the partial output is not a value
    return r;
}

#endif
