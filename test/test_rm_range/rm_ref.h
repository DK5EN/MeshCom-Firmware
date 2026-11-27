// Reference for test_rm_range: the byte-by-byte versions of the RM1 output builder and of the cmd/args copy in
// rmParse() as they stood before B-RM-RANGE-01 (src/remote_cmd.cpp at fork-dev cdfc76ea), kept verbatim in their
// logic so the range-based code can be compared against them.
#ifndef RM_REF_H
#define RM_REF_H

#include <stdint.h>
#include <string.h>
#include <remote_cmd.h>

namespace rmref
{
struct Out
{
    char *p;
    size_t n;
    size_t len;
    bool ok;
};

inline void outInit(Out &o, char *buf, size_t n)
{
    o.p = buf;
    o.n = n;
    o.len = 0;
    o.ok = (buf != nullptr && n > 0);
}

inline void outStr(Out &o, const char *s)
{
    if (!o.ok)
        return;
    if (s == nullptr)
    {
        o.ok = false;
        return;
    }
    while (*s)
    {
        if (o.len + 1 >= o.n)
        {
            o.ok = false;
            return;
        }
        o.p[o.len++] = *s++;
    }
}

inline void outU32(Out &o, uint32_t v)
{
    char tmp[11];
    unsigned i = sizeof(tmp);
    tmp[--i] = '\0';
    do
    {
        tmp[--i] = (char)('0' + (v % 10));
        v /= 10;
    } while (v != 0);
    outStr(o, tmp + i);
}

inline size_t outEnd(Out &o)
{
    if (!o.ok)
    {
        if (o.p != nullptr && o.n > 0)
            o.p[0] = '\0';
        return 0;
    }
    o.p[o.len] = '\0';
    return o.len;
}

inline size_t canonical(const RmCmd &c, const char *dst, const char *src, char *out, size_t n)
{
    Out o;
    outInit(o, out, n);
    outStr(o, "RM1|");
    outStr(o, dst);
    outStr(o, "|");
    outStr(o, src);
    outStr(o, "|");
    outU32(o, c.ctr);
    outStr(o, "|");
    outStr(o, c.cmd);
    if (c.args[0] != '\0')
    {
        outStr(o, " ");
        outStr(o, c.args);
    }
    return outEnd(o);
}

inline size_t replyCanonical(uint32_t ctr, const char *dst, const char *src, const char *result, char *out, size_t n)
{
    Out o;
    outInit(o, out, n);
    outStr(o, "RM1R|");
    outStr(o, dst);
    outStr(o, "|");
    outStr(o, src);
    outStr(o, "|");
    outU32(o, ctr);
    outStr(o, "|");
    outStr(o, result);
    return outEnd(o);
}

inline bool isLowerHex(char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); }

inline bool tagTokenValid(const char *text, size_t len)
{
    if (len < 17)
        return false;
    const size_t tagPos = len - 16;
    if (text[tagPos - 1] != ' ')
        return false;
    for (size_t i = tagPos; i < len; i++)
        if (!isLowerHex(text[i]))
            return false;
    return true;
}

inline bool ctrToken(const char *text, size_t i, size_t limit, uint32_t &ctr, size_t &next)
{
    const size_t start = i;
    uint64_t v = 0;
    while (i < limit && text[i] >= '0' && text[i] <= '9')
    {
        v = v * 10 + (uint64_t)(text[i] - '0');
        if (v > 0xFFFFFFFFULL)
            return false;
        i++;
    }
    const size_t digits = i - start;
    if (digits == 0 || (digits > 1 && text[start] == '0'))
        return false;
    ctr = (uint32_t)v;
    next = i;
    return true;
}

inline bool parse(const char *text, RmCmd &out)
{
    memset(&out, 0, sizeof(out));
    if (text == nullptr)
        return false;
    size_t len = 0;
    while (len < 200 && text[len])
        len++;
    if (len >= 200 || len < 4 + 1 + 1 + 1 + 1 + 16 || strncmp(text, "RM1 ", 4) != 0)
        return false;
    for (size_t i = 3; i < len; i++)
    {
        const unsigned char ch = (unsigned char)text[i];
        if (ch < 0x20 || ch > 0x7e)
            return false;
    }
    if (text[len - 1] == ' ')
        return false;
    if (!tagTokenValid(text, len))
        return false;
    const size_t tagPos = len - 16;
    size_t i = 4;
    uint32_t ctr = 0;
    if (!ctrToken(text, i, tagPos - 1, ctr, i))
        return false;
    if (i >= tagPos - 1 || text[i] != ' ')
        return false;
    i++;
    const size_t bodyEnd = tagPos - 1;
    if (i >= bodyEnd || text[i] == ' ')
        return false;
    size_t cl = 0;
    while (i < bodyEnd && text[i] != ' ')
    {
        if (cl + 1 >= sizeof(out.cmd) || (text[i] >= 'A' && text[i] <= 'Z'))
            return false;
        out.cmd[cl++] = text[i++];
    }
    if (i < bodyEnd)
    {
        i++;
        if (i >= bodyEnd || text[i] == ' ')
            return false;
        size_t al = 0;
        while (i < bodyEnd)
        {
            if (al + 1 >= sizeof(out.args))
                return false;
            if (text[i] == ' ' && text[i + 1] == ' ')
                return false;
            out.args[al++] = text[i++];
        }
    }
    out.ctr = ctr;
    if (out.ctr == 0 && strcmp(out.cmd, "sync") != 0)
        return false;
    memcpy(out.tag, text + tagPos, 16);
    out.tag[16] = '\0';
    return true;
}
} // namespace rmref

#endif
