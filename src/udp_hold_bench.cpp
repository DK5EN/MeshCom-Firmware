// Bench hook for the text-message outage hold (src/udp_hold.h). INSTRUMENT
// builds only (-DINSTRUMENT_ENABLED=1), RAM only, never persisted: a reboot
// clears it. Nothing goes on LoRa.
//
//   --udphold                 status line
//   --udphold status          status line
//   --udphold outage on       behave as if the server were dead: frames to the
//                             server are dropped at the drain, received server
//                             frames do not count as sign of life
//   --udphold outage off      end the simulation
#include <configuration.h>
#include "instrument.h"

#if INSTRUMENT_ENABLED

#include <Arduino.h>
#include <string.h>
#include <debugconf.h>   // Serial is mirrored to the 2323 console through this
#include "udp_hold.h"

void udpHoldBenchCommand(const char *args)
{
    while (args && (*args == ' ' || *args == '\t'))
        args++;
    if (!args)
        args = "";

    char line[240];
    UdpHoldRt &r = udpHoldRt();

    if (strncmp(args, "outage on", 9) == 0 || strncmp(args, "outage off", 10) == 0)
    {
        if (!r.enabled)
        {
            Serial.printf("[GW];hold;bench;n/a;note;hold not compiled in or gateway RAM off\n");
            return;
        }
        r.benchOutage = (strncmp(args, "outage on", 9) == 0);
        Serial.printf("[GW];hold;bench;outage;%s\n", r.benchOutage ? "on" : "off");
        return;
    }
    if (*args == '\0' || strncmp(args, "status", 6) == 0)
    {
        udpHoldStatusLine(line, sizeof(line));
        Serial.printf("%s;bench_outage;%d\n", line, r.benchOutage ? 1 : 0);
        return;
    }
    Serial.printf("[GW];hold;bench;usage;--udphold [status|outage on|outage off]\n");
}

#endif
