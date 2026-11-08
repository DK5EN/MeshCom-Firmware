// CLK-01 (docs/campaign-clk-01-20261107.md) -- redraw gate of
// sendDisplayTime() as a pure decision, for host tests.
// W2 contract: the clock line is redrawn every second (no 15 s gate, F2) on every page that HAS a
// clock line. The message page (iDisplayType == 0) has none: pageText[0] / the TFT header bar carry
// the sender header ("GM<grp> <CALL> <rssi>"), so the clock stays frozen there (SkipType0, F1
// operator decision 2026-11-08) and is drawn again when the next clock page is built (every
// sendDisplayHead/Position/Track/WX call sendDisplayMainline(), which draws the current time).
// Returned: Draw, SkipNoDisplay, SkipOff, SkipType0, SkipSetDisplay. SkipGate15 (F2) and SkipHold are
// no longer returned here; the enumerators and reason strings stay for the INSTRUMENT [DISPCLK]
// contract. SkipPing (dispClkPingActive()) and the WP_DISP "gate15" (10 s e-paper refresh) are
// emitted by the caller in loop_functions.cpp. Ping pages (DisplayPong) are full-screen text
// without a clock line and are treated like the message page: no clock draw, no overwrite.
#pragma once

enum class DispClkDecision
{
    Draw,
    SkipNoDisplay,   // no u8g2 / board without this clock line
    SkipOff,         // bDisplayIsOff
    SkipType0,       // iDisplayType == 0 (message page, F1): no clock line there, content stays
    SkipHold,        // page hold (not consulted by sendDisplayTime today)
    SkipSetDisplay,  // bSetDisplay: a draw is in progress
    SkipPing,        // configured ping skips the caller (not decided here)
    SkipGate15       // legacy (F2): no longer returned by dispClkDecide
};

struct DispClkInputs
{
    bool hasDisplay;     // false -> SkipNoDisplay (u8g2 NULL or e-paper/T-Deck board)
    bool displayIsOff;
    int  displayType;
    int  pageHold;       // reserved, not consulted (current behaviour)
    bool setDisplay;
    int  second;         // node_date_second
    bool oneButton;
    bool displayDirty;
};

static inline DispClkDecision dispClkDecide(const DispClkInputs &in)
{
    if (!in.hasDisplay)        return DispClkDecision::SkipNoDisplay;
    if (in.displayIsOff)       return DispClkDecision::SkipOff;
    if (in.displayType == 0)   return DispClkDecision::SkipType0;
    if (in.setDisplay)         return DispClkDecision::SkipSetDisplay;
    return DispClkDecision::Draw;
}

// Configured ping (call set, interval and count > 0): the ping/pong frames own the screen and have
// no clock line, so the clock draw is skipped (SkipPing). Same predicate as the caller gates.
static inline bool dispClkPingActive(bool pingCallSet, unsigned pingTime, unsigned pingCount)
{
    return pingCallSet && pingTime != 0 && pingCount != 0;
}

static inline const char *dispClkReason(DispClkDecision d)
{
    switch (d)
    {
    case DispClkDecision::Draw:           return "draw";
    case DispClkDecision::SkipNoDisplay:  return "nodisplay";
    case DispClkDecision::SkipOff:        return "off";
    case DispClkDecision::SkipType0:      return "type0";
    case DispClkDecision::SkipHold:       return "hold";
    case DispClkDecision::SkipSetDisplay: return "setdisplay";
    case DispClkDecision::SkipPing:       return "ping";
    case DispClkDecision::SkipGate15:     return "gate15";
    }
    return "?";
}
