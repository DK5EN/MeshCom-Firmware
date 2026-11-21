#include "sha1_base64.h"
#include <string.h>

static inline uint32_t rol32(uint32_t v, unsigned n)
{
    return (v << n) | (v >> (32 - n));
}

static void sha1Block(uint32_t h[5], const uint8_t *p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) | ((uint32_t)p[4 * i + 2] << 8) |
               (uint32_t)p[4 * i + 3];
    for (int i = 16; i < 80; i++)
        w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++)
    {
        uint32_t f, k;
        if (i < 20)
        {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        uint32_t t = rol32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol32(b, 30);
        b = a;
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

void sha1(const uint8_t *data, size_t len, uint8_t out[20])
{
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    size_t i = 0;
    for (; i + 64 <= len; i += 64)
        sha1Block(h, data + i);
    uint8_t tail[128];
    size_t rem = len - i;
    if (rem)
        memcpy(tail, data + i, rem);
    tail[rem++] = 0x80;
    size_t total = (rem + 8 <= 64) ? 64 : 128;
    memset(tail + rem, 0, total - rem);
    const uint64_t bits = (uint64_t)len * 8u;
    for (int b = 0; b < 8; b++)
        tail[total - 1 - b] = (uint8_t)(bits >> (8 * b));
    sha1Block(h, tail);
    if (total == 128)
        sha1Block(h, tail + 64);
    for (int k = 0; k < 5; k++)
    {
        out[4 * k] = (uint8_t)(h[k] >> 24);
        out[4 * k + 1] = (uint8_t)(h[k] >> 16);
        out[4 * k + 2] = (uint8_t)(h[k] >> 8);
        out[4 * k + 3] = (uint8_t)h[k];
    }
}

size_t base64Encode(const uint8_t *in, size_t len, char *out, size_t out_size)
{
    static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (!out || out_size == 0)
        return 0;
    const size_t need = ((len + 2) / 3) * 4;
    if (out_size < need + 1)
    {
        out[0] = '\0';
        return 0;
    }
    size_t o = 0, i = 0;
    for (; i + 3 <= len; i += 3)
    {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = tab[(v >> 18) & 63];
        out[o++] = tab[(v >> 12) & 63];
        out[o++] = tab[(v >> 6) & 63];
        out[o++] = tab[v & 63];
    }
    if (len - i == 1)
    {
        uint32_t v = (uint32_t)in[i] << 16;
        out[o++] = tab[(v >> 18) & 63];
        out[o++] = tab[(v >> 12) & 63];
        out[o++] = '=';
        out[o++] = '=';
    }
    else if (len - i == 2)
    {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[o++] = tab[(v >> 18) & 63];
        out[o++] = tab[(v >> 12) & 63];
        out[o++] = tab[(v >> 6) & 63];
        out[o++] = '=';
    }
    out[o] = '\0';
    return o;
}

bool wsAcceptKey(const char *client_key, char out[29])
{
    static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    if (!client_key || !out)
        return false;
    const size_t kl = strlen(client_key);
    if (kl == 0 || kl > 60)
        return false;
    uint8_t buf[60 + sizeof(guid)];
    memcpy(buf, client_key, kl);
    memcpy(buf + kl, guid, sizeof(guid) - 1);
    uint8_t dig[20];
    sha1(buf, kl + sizeof(guid) - 1, dig);
    return base64Encode(dig, 20, out, 29) == 28;
}
