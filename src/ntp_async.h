/**
 * Non-blocking NTP client (TM-35).
 *
 * The stock NTPClient blocks the caller for up to 1 s per refresh and, worse,
 * drains every queued datagram off the socket it shares with the MeshCom
 * gateway (forceUpdate() flushes before it sends). Both nRF52 (W5100S) and
 * ESP32 (WiFi) run NTP on the gateway socket, so a refresh stalls loopTask and
 * can eat GATE/CONF packets.
 *
 * This client only ever sends. The reply is picked up by the regular receive
 * path, which hands every datagram to tryConsume() before parsing it as a
 * MeshCom frame.
 */
#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <Udp.h>

#define NTP_ASYNC_PACKET_SIZE 48
#define NTP_ASYNC_SERVER_PORT 123
// same local port the stock NTPClient used, so the source port on the wire
// does not change (the MeshCom server answers to the KEEP source port)
#define NTP_ASYNC_LOCAL_PORT 1337

class NtpAsync
{
public:
    explicit NtpAsync(UDP &udp) : _udp(&udp) {}

    void setPoolServerIP(IPAddress ip);
    void begin(unsigned int port = NTP_ASYNC_LOCAL_PORT);
    void end();

    // refresh interval after a successful sync, default 15 min
    void setUpdateInterval(uint32_t interval_ms) { _intervalMs = interval_ms; }

    // non-blocking state machine, safe to call every loop pass
    void loop();

    // ask for a refresh at the next loop() call
    void requestNow() { _nextDueMs = millis(); }

    // offer a received datagram; returns true if it was our NTP reply
    bool tryConsume(IPAddress remoteIp, uint16_t remotePort, const uint8_t *buf, int len);

    bool isTimeSet() const { return _haveTime; }
    unsigned long getEpochTime() const;
    String getFormattedTime() const;

private:
    bool sendRequest();

    UDP *_udp = nullptr;
    IPAddress _serverIp = IPAddress(0, 0, 0, 0);
    bool _udpSetup = false;
    bool _haveTime = false;

    unsigned long _epoch = 0;   // seconds since 1970 at _epochAtMs
    uint32_t _epochAtMs = 0;

    uint32_t _intervalMs = 15UL * 60UL * 1000UL;
    uint32_t _nextDueMs = 0;
    uint32_t _pendingSince = 0; // 0 = no request in flight
    uint16_t _fails = 0;
};
