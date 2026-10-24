// No-op TFT_eSPI stand-in for the native build: just enough surface for
// src/tft_display_functions.cpp (HAS_TFT block) to compile and run.
#pragma once

#include <Arduino.h>

#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#define TL_DATUM 0
#define MC_DATUM 4

#include <string>

// Call recorders the tests read/reset (inline statics: one copy per binary).
struct TftStubStats
{
    int createSprite = 0;
    int deleteSprite = 0;
    int pushSprite = 0;
    int fillSprite = 0;
    int drawString = 0;
    std::string lastString;
    bool failNextCreate = false;   // test-controlled: next createSprite() fails
    void reset() { *this = TftStubStats(); }
};
inline TftStubStats g_tftStats;

class TFT_eSPI
{
public:
    TFT_eSPI() {}
    void init() {}
    void begin() {}
    void setRotation(int) {}
    void setTextFont(int) {}
    void fillScreen(uint16_t) {}
    void setTextSize(int) {}
    void setTextColor(uint16_t, uint16_t) {}
    void setTextDatum(int) {}
    void drawString(const String &s, int, int)
    {
        g_tftStats.drawString++;
        g_tftStats.lastString = s.c_str();
    }
    int width() { return 160; }
    int height() { return 80; }
};

class TFT_eSprite
{
public:
    explicit TFT_eSprite(TFT_eSPI *) {}
    void createSprite(int, int)
    {
        g_tftStats.createSprite++;
        _created = !g_tftStats.failNextCreate;
        g_tftStats.failNextCreate = false;
    }
    void deleteSprite()
    {
        g_tftStats.deleteSprite++;
        _created = false;
    }
    bool created() const { return _created; }
    void fillSprite(uint16_t) { g_tftStats.fillSprite++; }
    void fillRect(int, int, int, int, uint16_t) {}
    void setTextFont(int) {}
    void setTextSize(int) {}
    void setTextColor(uint16_t, uint16_t) {}
    void setTextWrap(bool, bool) {}
    void setCursor(int, int) {}
    int textWidth(const String &) { return 0; }
    void drawString(const String &, int, int) {}
    void println(const String &) {}
    void pushSprite(int, int) { g_tftStats.pushSprite++; }
    void drawRoundRect(int, int, int, int, int, uint16_t) {}
    void fillRoundRect(int, int, int, int, int, uint16_t) {}

private:
    bool _created = false;
};
