// CLK-01 (docs/campaign-clk-01-20261107.md) -- redraw gate of
// sendDisplayTime() as a pure decision, for host tests. Behaviour is identical
// to the former inline early returns, in the same order.
#pragma once

enum class DispClkDecision
{
    Draw,
    SkipNoDisplay,   // no u8g2 / board without this clock line
    SkipOff,         // bDisplayIsOff
    SkipType0,       // iDisplayType == 0 (message page, F1)
    SkipHold,        // page hold (not consulted by sendDisplayTime today)
    SkipSetDisplay,  // bSetDisplay: a draw is in progress
    SkipPing,        // configured ping skips the caller (not decided here)
    SkipGate15       // second not in {0,15,30,45} (F2)
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
    if (in.second == 0 || in.second == 15 || in.second == 30 || in.second == 45
        || in.oneButton || in.displayDirty)
        return DispClkDecision::Draw;
    return DispClkDecision::SkipGate15;
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
