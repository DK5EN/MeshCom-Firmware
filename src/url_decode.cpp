#include <url_decode.h>
#include <hex_digit.h>

namespace
{
    // Returns the decoded byte of the escape at buf[i], or -1 if buf[i] does
    // not start a complete %XX escape.
    int escape_at(const char *buf, size_t len, size_t i)
    {
        if (i + 2 >= len || buf[i] != '%')
            return -1;
        int hi = mcHexVal(buf[i + 1]);
        int lo = mcHexVal(buf[i + 2]);
        if (hi < 0 || lo < 0)
            return -1;
        return (hi << 4) | lo;
    }
}

size_t url_percent_decode(char *buf, size_t len)
{
    return urlDecode(buf, len, URLD_LEGACY);
}

size_t urlDecode(char *buf, size_t len, uint8_t policy)
{
    if (buf == nullptr)
        return 0;

    const bool strict = (policy == URLD_STRICT);
    size_t in = 0;
    size_t out = 0;

    while (in < len)
    {
        char c = buf[in];

        if (c == '+' && !strict)
        {
            buf[out++] = ' ';
            in++;
            continue;
        }

        int b = escape_at(buf, len, in);
        if (b < 0)
        {
            if (strict && ((unsigned char)c < 0x20 || (unsigned char)c == 0x7F))
                return (size_t)-1;
            buf[out++] = c;  // plain byte, or a '%' that starts no escape
            in++;
            continue;
        }
        in += 3;

        if (strict)
        {
            if (b < 0x20 || b == 0x7F)
                return (size_t)-1;  // decoded control or DEL
            buf[out++] = (char)b;
        }
        else if (b == 0x0D && escape_at(buf, len, in) == 0x0A)
        {
            buf[out++] = '-';  // line break -> "-", as before
            in += 3;
        }
        else if (b == '"' || b < 0x20 || b == 0x7F)
        {
            // dropped: quote as before, controls so none reach the payload
        }
        else
        {
            buf[out++] = (char)b;
        }
    }

    buf[out] = '\0';
    return out;
}
