// No-op TFT_eSPI stand-in for the native build: just enough surface for
// src/tft_display_functions.cpp (HAS_TFT block) to compile and run.
#pragma once

#include <Arduino.h>

#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF

class TFT_eSPI
{
public:
    TFT_eSPI() {}
    void init() {}
    void begin() {}
    void setRotation(int) {}
    void setTextFont(int) {}
    void fillScreen(uint16_t) {}
};

class TFT_eSprite
{
public:
    explicit TFT_eSprite(TFT_eSPI *) {}
    void createSprite(int, int) {}
    void fillSprite(uint16_t) {}
    void fillRect(int, int, int, int, uint16_t) {}
    void setTextFont(int) {}
    void setTextSize(int) {}
    void setTextColor(uint16_t, uint16_t) {}
    void setTextWrap(bool, bool) {}
    void setCursor(int, int) {}
    int textWidth(const String &) { return 0; }
    void drawString(const String &, int, int) {}
    void println(const String &) {}
    void pushSprite(int, int) {}
    void drawRoundRect(int, int, int, int, int, uint16_t) {}
    void fillRoundRect(int, int, int, int, int, uint16_t) {}
};
