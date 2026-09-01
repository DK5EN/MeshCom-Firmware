// Minimaler UDP-Ersatz fuer den nativen Testbuild (env:native).
// Nur die Methoden, die NtpAsync benutzt.
#pragma once

#ifndef NATIVE_BUILD
#error "test/support/Udp.h darf nur im nativen Testbuild verwendet werden"
#endif

#include <cstddef>
#include <cstdint>

#include "IPAddress.h"

class UDP
{
public:
    virtual ~UDP() {}
    virtual uint8_t begin(uint16_t port) = 0;
    virtual int beginPacket(IPAddress ip, uint16_t port) = 0;
    virtual size_t write(const uint8_t *buffer, size_t size) = 0;
    virtual int endPacket() = 0;
};
