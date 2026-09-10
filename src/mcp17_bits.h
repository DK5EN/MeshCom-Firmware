#ifndef _MCP17_BITS_H_
#define _MCP17_BITS_H_

#include <stdint.h>

// MCP23017 port A inputs as an eight-character bit string, shared by the
// position beacon (/D=, PositionToAPRS()) and the digital slot of the APRS
// T# telemetry frame (sendTelemetry(), upstream issue 1076).
//
// Order follows the APRS BITS convention: out[0] is GPA0, out[7] is GPA7.
// `in` is meshcom_settings.node_mcp17in as read by loopMCP23017()
// (io_functions.cpp: bit n == pin n), `io_mask` is node_mcp17io (1 == pin
// configured as OUTPUT). Output pins always read '0' so a stale input word
// after a --setio change cannot leak into the frame. Port B (bits 8-15) is
// ignored. Arduino-free so the contract is native-testable
// (test/test_mcp17_bits).
#define MCP17_BITS_LEN 8

static inline void mcp17PortABits(uint16_t in, uint16_t io_mask,
                                  char out[MCP17_BITS_LEN + 1])
{
    for(int bit = 0; bit < MCP17_BITS_LEN; bit++)
    {
        uint16_t m = (uint16_t)(1u << bit);
        out[bit] = ((in & m) && !(io_mask & m)) ? '1' : '0';
    }
    out[MCP17_BITS_LEN] = 0;
}

#endif // _MCP17_BITS_H_
