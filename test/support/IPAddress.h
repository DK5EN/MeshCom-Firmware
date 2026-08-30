// Minimaler IPAddress-Ersatz fuer den nativen Testbuild (env:native).
#pragma once

#ifndef NATIVE_BUILD
#error "test/support/IPAddress.h darf nur im nativen Testbuild verwendet werden"
#endif

#include <cstdint>

class IPAddress
{
public:
    IPAddress() { _o[0] = _o[1] = _o[2] = _o[3] = 0; }
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
    {
        _o[0] = a; _o[1] = b; _o[2] = c; _o[3] = d;
    }

    uint8_t operator[](int i) const { return _o[i & 3]; }

    operator uint32_t() const
    {
        return ((uint32_t)_o[0] << 24) | ((uint32_t)_o[1] << 16) | ((uint32_t)_o[2] << 8) | _o[3];
    }

    bool operator==(const IPAddress &o) const
    {
        return _o[0] == o._o[0] && _o[1] == o._o[1] && _o[2] == o._o[2] && _o[3] == o._o[3];
    }
    bool operator!=(const IPAddress &o) const { return !(*this == o); }

private:
    uint8_t _o[4];
};
