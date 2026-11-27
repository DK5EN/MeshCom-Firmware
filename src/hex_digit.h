#ifndef HEX_DIGIT_H
#define HEX_DIGIT_H

// The one hex-digit decoder of the firmware (FORMAL-02). Header-only, no includes, so header-only
// units (fw_update_*, applink_auth.h, ext_reg.h, web_guard.h) can use it in every native env.

// '0'-'9', 'a'-'f', 'A'-'F' -> 0..15; every other char value -> -1.
static inline int mcHexVal(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

#endif
