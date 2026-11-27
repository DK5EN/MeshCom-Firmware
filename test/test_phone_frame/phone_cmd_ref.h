// phone_cmd_ref.h -- frozen copy of the parsing logic of readPhoneCommand() as of commit 770764e5
// (src/phone_commands.cpp, before FORMAL-02). Only the actions are replaced by a recorder (Rec) and every
// read of conf_data goes through Probe::at() so the test can see how far the old code read. The control
// flow, the index arithmetic and the length checks (or their absence) are the original ones. Do not "fix".
#ifndef PHONE_CMD_REF_H
#define PHONE_CMD_REF_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

enum RefCmd
{
    RC_NONE = 0,
    RC_HELLO,
    RC_TIME,
    RC_CALL,
    RC_LAT,
    RC_LON,
    RC_ALT,
    RC_SYM,
    RC_TEXT,
    RC_WIFI,
    RC_SAVE
};

struct Rec
{
    int cmd;
    bool has_hash;     // HELLO: header says msg_len >= 35
    uint8_t hash[32];  // HELLO
    uint32_t ts;       // TIME
    float fval;        // LAT, LON
    int32_t ival;      // ALT
    bool save_flag;    // LAT, LON, ALT: conf_data[6] == 0x0A
    uint8_t sym[2];    // SYM
    bool dash;         // TEXT: text starts with "--"
    uint16_t dlen;     // CALL, TEXT: bytes in data
    uint8_t data[256]; // CALL, TEXT
    uint8_t ssid_len, pwd_len;
    char ssid[33];     // WIFI, clamped like the action (sizeof node_ssid)
    char pwd[64];      // WIFI, clamped like the action (sizeof node_pwd)
};

// Read tracker: highest byte index touched + 1.
struct Probe
{
    const uint8_t *b;
    size_t extent;
    uint8_t at(size_t i)
    {
        if (i + 1 > extent)
            extent = i + 1;
        return b[i];
    }
    void copy(void *dst, size_t off, size_t n)
    {
        if (n && off + n > extent)
            extent = off + n;
        if (n)
            memcpy(dst, b + off, n);
    }
};

// conf_data must point to a buffer of at least 512 readable bytes (the test provides guard bytes).
// Returns the number of leading bytes of conf_data the old code read (highest index + 1).
static inline size_t refReadPhoneCommand(const uint8_t *conf_data, Rec &out)
{
    Probe p = {conf_data, 0};
    memset(&out, 0, sizeof(out));

    uint8_t msg_len = p.at(0);
    uint8_t msg_type = p.at(1);
    uint8_t msg_payload_len = p.at(2);

    bool save_setting = false;

    // get save settings flag if position setting
    if (msg_type == 0x70 || msg_type == 0x80 || msg_type == 0x90)
    {
        if (p.at(6) == 0x0A)
            save_setting = true;
        if (p.at(6) == 0x0B)
            save_setting = false;
    }

    switch (msg_type)
    {
    case 0x10:
    {
        if (p.at(2) == 0x20 && p.at(3) == 0x30)
        {
            out.cmd = RC_HELLO;
            if (msg_len >= 35)
            {
                out.has_hash = true;
                p.copy(out.hash, 4, 32);
            }
        }
        break;
    }

    case 0x20:
    {
        uint32_t timestamp = 0;
        p.copy(&timestamp, 2, sizeof(timestamp));
        out.cmd = RC_TIME;
        out.ts = timestamp;
        break;
    }

    case 0x50:
    {
        out.cmd = RC_CALL;
        for (int i = 0; i < msg_payload_len; i++)
            out.data[i] = p.at(i + 3);
        out.dlen = msg_payload_len;
        break;
    }

    case 0x70:
    {
        float lat_phone = 0.0;
        p.copy(&lat_phone, 2, sizeof(lat_phone));
        out.cmd = RC_LAT;
        out.fval = lat_phone;
        out.save_flag = save_setting;
        break;
    }

    case 0x80:
    {
        float long_phone = 0.0;
        p.copy(&long_phone, 2, sizeof(long_phone));
        out.cmd = RC_LON;
        out.fval = long_phone;
        out.save_flag = save_setting;
        break;
    }

    case 0x90:
    {
        int altitude = 0;
        p.copy(&altitude, 2, sizeof(altitude));
        out.cmd = RC_ALT;
        out.ival = altitude;
        out.save_flag = save_setting;
        break;
    }

    case 0x95:
    {
        out.cmd = RC_SYM;
        out.sym[0] = p.at(2);
        out.sym[1] = p.at(3);
        break;
    }

    case 0xA0:
    {
        if (msg_len < 2)
            break;

        uint8_t txt_msg_len_phone = msg_len - 2;

        // memcmp(conf_data + 2, "--", 2): reads two bytes whatever the text length
        uint8_t d0 = p.at(2), d1 = p.at(3);
        out.dash = (d0 == '-' && d1 == '-');
        p.copy(out.data, 2, txt_msg_len_phone);
        out.dlen = txt_msg_len_phone;
        out.cmd = RC_TEXT;
        break;
    }

    case 0x55:
    {
        uint8_t ssid_len = p.at(2);

        if ((unsigned)(4 + ssid_len) > msg_len)
            break;

        uint8_t pwd_len = p.at(ssid_len + 3);

        if ((unsigned)(4 + ssid_len + pwd_len) > msg_len)
            break;

        if (ssid_len > 0 && pwd_len > 0)
        {
            char ssid_arr[33] = {0};
            char pwd_arr[64] = {0};
            uint8_t ssid_copy_len = (ssid_len < sizeof(ssid_arr) - 1) ? ssid_len : (sizeof(ssid_arr) - 1);
            uint8_t pwd_copy_len = (pwd_len < sizeof(pwd_arr) - 1) ? pwd_len : (sizeof(pwd_arr) - 1);
            p.copy(ssid_arr, 3, ssid_copy_len);
            p.copy(pwd_arr, 4 + ssid_len, pwd_copy_len);
            out.cmd = RC_WIFI;
            out.ssid_len = ssid_len;
            out.pwd_len = pwd_len;
            memcpy(out.ssid, ssid_arr, sizeof(out.ssid));
            memcpy(out.pwd, pwd_arr, sizeof(out.pwd));
        }
        break;
    }

    case 0xF0:
        out.cmd = RC_SAVE;
        break;
    }
    return p.extent;
}

#endif
