#if defined(ESP32) || defined(NRF52_SERIES)

#include <Arduino.h>
#include <string.h>
#include "reboot_defer.h"
#if defined(ESP32)
#include <loop_breadcrumb.h> // INS-05: loopCrumbClear() before a deliberate reboot
#endif

static RebootDeferState s_state = {false, 0};
static char s_why[24] = "";

void rebootDeferArm(uint32_t delay_ms, const char *why)
{
    const uint32_t now = millis();
    const bool wasArmed = s_state.armed;
    const uint32_t before = s_state.at_ms;
    rebootDeferArmState(s_state, now, delay_ms);
    if (!wasArmed || s_state.at_ms != before) // the earlier deadline's reason stays with it
    {
        strncpy(s_why, why ? why : "", sizeof(s_why) - 1);
        s_why[sizeof(s_why) - 1] = '\0';
    }
}

void rebootDeferService(void)
{
    if (!rebootDeferDue(s_state.armed, s_state.at_ms, millis()))
        return;
    s_state.armed = false;
    Serial.printf("[REBOOT];deferred;%s\n", s_why);

#if defined(ESP32)
    loopCrumbClear();   // INS-05: deliberate reboot, no LAST_LOOP_SECTION at the next boot
    ESP.restart();
#endif

#if defined(NRF52_SERIES)
    NVIC_SystemReset();     // resets the device
#endif
}

#endif
