#include "udp_frame_platform.h"
#include <configuration.h>            // UDP_TX_BUF_SIZE etc. for the extern header
#include <loop_functions_extern.h>   // rebootAuto, bLED_ORANGE

// FORMAL-02 platform seam, ESP32 side (see udp_frame_platform.h). The state
// lives in udp_functions.cpp and is not declared in a header, so it is
// externed here; carve_extern_lint.py compares these spellings against the
// definitions.
extern unsigned long last_upd_timer;
extern bool had_initial_udp_conn;
extern IPAddress node_hostip;

void udpPlatformNoteServerRx(unsigned long now_ms, bool initial_conn)
{
    last_upd_timer = now_ms;
    if(initial_conn)
        had_initial_udp_conn = true;
}

IPAddress udpPlatformServerAddr(void)
{
    return node_hostip;
}

void udpPlatformNoteRelayedFrame(void)
{
    bLED_ORANGE = true;   // consumed by esp32_main.cpp (LED handling); no nRF52 reader
}

void udpPlatformScheduleConfReboot(unsigned long delay_ms)
{
    // same auto-reboot (and T-Deck exception) as a changed --setcall
    // (commandAction(), src/command_functions.cpp)
#if !defined(BOARD_T_DECK) && !defined(BOARD_T_DECK_PLUS)
    rebootAuto = millis() + delay_ms;
#else
    (void)delay_ms;
#endif
}
