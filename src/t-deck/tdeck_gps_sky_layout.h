// GPS-SKY: pure geometry of the T-Deck Position tab (320 x 240 panel, page 166 px high). No LVGL here so a
// native test can pin it. Used by tdeck_gps_sky.cpp and the track_ta / SEND POS setup in lv_obj_functions.cpp.
#ifndef TDECK_GPS_SKY_LAYOUT_H
#define TDECK_GPS_SKY_LAYOUT_H

namespace skylayout
{
struct Rect
{
    int x, y, w, h;
    constexpr int right() const { return x + w; }
    constexpr int bottom() const { return y + h; }
};

constexpr int kPageW = 320;
constexpr int kPageH = 166;
constexpr int kMaxX = 318;   // 2 px right margin
constexpr int kBorder = 2;   // card border width

// card (sky plot and text view share it); right edge x = 250
constexpr Rect kCard = {2, 1, 248, 163};
// side tabs: active x = card right edge - border (the nose covers the card border); inactive is shifted right
// by kTabInactiveDx = border + 2 px gap
constexpr Rect kTabSky = {kCard.x + kCard.w - kBorder, 4, 64, 30};
constexpr Rect kTabText = {kCard.x + kCard.w - kBorder, 42, 64, 30};   // 8 px below the SKY tab
constexpr int kTabInactiveDx = kBorder + 2;
constexpr Rect kInfo = {254, 80, 62, 34};        // "SAT n" / AGE, two lines of the 12 px font at 17 px line height
constexpr Rect kSendPos = {252, 120, 64, 40};    // two-line label "SEND / POS", 48 px below the TEXT tab, bottom at 160
constexpr int kCx = 124, kCy = 81, kR = 66;      // sky plot centre / radius inside the card
}   // namespace skylayout

#endif
