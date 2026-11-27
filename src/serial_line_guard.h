// serial_line_guard.h -- is a completed serial console line something a person typed? (pure, header-only;
// host test: test/test_serial_line_guard). SER-JUNK-01, 2026-11-26.
//
// E22 nodes with a 1 W / 2 W PA see supply spikes during their own LoRa TX. The spikes put noise on the UART RX
// line; the noise arrives as random bytes, and a run that happened to start with "::" or "--" and end with CR/LF
// was sent over LoRa as a text message or executed as a command. A person at a terminal types printable ASCII,
// UTF-8 (umlauts), TAB and the editing keys BS/DEL, and ends the line with CR and/or LF. Anything else in a line
// -- other control bytes, bytes that are not valid UTF-8 -- is noise, and the whole line is dropped.
#ifndef SERIAL_LINE_GUARD_H
#define SERIAL_LINE_GUARD_H

#include <stddef.h>
#include <stdint.h>

// Length of the valid UTF-8 sequence starting at s[i] (2..4), 0 if s[i] does not start one. Rejects overlong
// forms, surrogates (U+D800..U+DFFF) and code points above U+10FFFF, like charset_filter's PLAIN mode.
static inline size_t serialUtf8SeqLen(const unsigned char *s, size_t i, size_t len)
{
    const unsigned char c = s[i];
    size_t n;
    unsigned char lo = 0x80, hi = 0xBF; // allowed range of the first continuation byte
    if (c >= 0xC2 && c <= 0xDF)
        n = 2;
    else if (c >= 0xE0 && c <= 0xEF)
    {
        n = 3;
        if (c == 0xE0)
            lo = 0xA0; // no overlong 3-byte form
        else if (c == 0xED)
            hi = 0x9F; // no surrogates
    }
    else if (c >= 0xF0 && c <= 0xF4)
    {
        n = 4;
        if (c == 0xF0)
            lo = 0x90; // no overlong 4-byte form
        else if (c == 0xF4)
            hi = 0x8F; // not above U+10FFFF
    }
    else
        return 0;
    if (i + n > len)
        return 0;
    if (s[i + 1] < lo || s[i + 1] > hi)
        return 0;
    for (size_t k = 2; k < n; k++)
        if (s[i + k] < 0x80 || s[i + k] > 0xBF)
            return 0;
    return n;
}

// True when every byte of line[0..len) is printable ASCII, TAB, CR, LF, BS (0x08), DEL (0x7F) or part of a valid
// UTF-8 sequence.
static inline bool serialLineIsClean(const char *line, size_t len)
{
    if (line == 0)
        return false;
    const unsigned char *s = (const unsigned char *)line;
    size_t i = 0;
    while (i < len)
    {
        const unsigned char c = s[i];
        if ((c >= 0x20 && c <= 0x7E) || c == '\t' || c == '\r' || c == '\n' || c == 0x08 || c == 0x7F)
        {
            i++;
            continue;
        }
        const size_t n = serialUtf8SeqLen(s, i, len);
        if (n == 0)
            return false;
        i += n;
    }
    return true;
}

// What the UART reader does in this loop pass (SER-JUNK-01, step 2). The supply spikes come with the node's own
// LoRa TX, so bytes that arrive while it transmits are noise: they stay in the UART buffer during TX and are
// discarded when TX ends, together with a line they were appended to.
enum SerialTxGate : uint8_t
{
    SERIAL_TX_READ = 0,  // no TX: read as usual
    SERIAL_TX_HOLD = 1,  // TX running: do not read
    SERIAL_TX_FLUSH = 2, // TX just ended: discard what arrived meanwhile
};

static inline SerialTxGate serialTxGateStep(bool txBusy, bool *wasBusy)
{
    if (txBusy)
    {
        *wasBusy = true;
        return SERIAL_TX_HOLD;
    }
    if (*wasBusy)
    {
        *wasBusy = false;
        return SERIAL_TX_FLUSH;
    }
    return SERIAL_TX_READ;
}

#endif // SERIAL_LINE_GUARD_H
