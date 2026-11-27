// phone_frame.h -- decode of one frame from the phone (BLE or AppLink) for readPhoneCommand()
// (pure, header-only, no Arduino; host test: test/test_phone_frame, BMC: later, FORMAL-02).
//
// readPhoneCommand() used to take only the buffer pointer (MAX_MSG_LEN_PHONE bytes) and index it from the
// header: conf_data[6], conf_data[i + 3] with a VLA sized by conf_data[2], and msg_len checked in only some
// branches. A frame whose header claims more bytes than the stack delivered made it read past the received
// data (and, on a BLE write of a few bytes, past the bytes the stack filled in). Here every read is bounded
// by `len`, the number of bytes actually received, and every field is copied once into a fixed-size output.
//
// Frame layout (unchanged): [0] msg_len (whole frame including this byte), [1] type, [2..] payload.
//   0x10 hello       [2]=0x20 [3]=0x30, [4..35] PIN hash when msg_len >= 35
//   0x20 time        [2..5] uint32 UNIX seconds, native byte order
//   0x50 callsign    [2] n, [3..3+n) callsign bytes
//   0x70/80/90       [2..5] float lat / float lon / int32 altitude, [6] 0x0A save, 0x0B no save
//   0x95 symbol      [2] table, [3] symbol
//   0xA0 text        [2..msg_len) text
//   0x55 wifi        [2] ssid_len, [3..) ssid, pwd_len, pwd (the three lengths are checked against msg_len)
//   0xF0 save+reboot
//
// Rules:
//   - len < 2 or len > PHONE_FRAME_MAX: the frame is dropped;
//   - msg_len > len (the header claims more than was received): dropped, nothing is decoded;
//   - a field that would end after len: dropped (PF_TRUNCATED), no partial value;
//   - everything else decodes exactly as the old inline code did for a well-formed frame.
#ifndef PHONE_FRAME_H
#define PHONE_FRAME_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PHONE_FRAME_MAX 300 // == MAX_MSG_LEN_PHONE (static_assert in phone_commands.cpp)
#define PHONE_FRAME_DATA_MAX 256
#define PHONE_FRAME_SSID_CAP 64 // >= sizeof(meshcom_settings.node_ssid)
#define PHONE_FRAME_PWD_CAP 64  // >= sizeof(meshcom_settings.node_pwd)

enum PhoneFrameStatus : uint8_t
{
    PF_OK = 0,
    PF_TOO_SHORT,   // len < 2: no type byte
    PF_TOO_LONG,    // len > PHONE_FRAME_MAX
    PF_CLAIMS_MORE, // msg_len > len
    PF_TRUNCATED    // a field of this type reaches past len
};

enum PhoneFrameCmd : uint8_t
{
    PFC_NONE = 0, // unknown type, or a frame the old code ignored as well
    PFC_HELLO,
    PFC_TIME,
    PFC_CALL,
    PFC_LAT,
    PFC_LON,
    PFC_ALT,
    PFC_SYMBOL,
    PFC_TEXT,
    PFC_WIFI,
    PFC_SAVE_RESET
};

struct PhoneFrame
{
    PhoneFrameStatus status;
    PhoneFrameCmd cmd;
    uint8_t msg_len;
    uint8_t type;
    bool has_hash;   // HELLO: a full 32-byte hash is present
    bool save_flag;  // LAT, LON, ALT: [6] == 0x0A
    bool dash;       // TEXT: starts with "--"
    uint8_t sym[2];  // SYMBOL: table, symbol
    uint16_t dlen;   // CALL, TEXT: bytes in data
    uint8_t ssid_len; // WIFI: as sent
    uint8_t pwd_len;
    uint32_t ts;     // TIME
    float fval;      // LAT, LON
    int32_t ival;    // ALT
    union
    {
        uint8_t hash[32];                    // HELLO
        uint8_t data[PHONE_FRAME_DATA_MAX];  // CALL, TEXT; NUL-terminated (dlen <= 255)
        struct
        {
            char ssid[PHONE_FRAME_SSID_CAP]; // NUL-terminated, cut at the cap
            char pwd[PHONE_FRAME_PWD_CAP];
        } wifi;
    } u;
};

// Decode buf[0..len). Never reads buf[len] or beyond. out is fully initialised.
static inline void phoneFrameDecode(const uint8_t *buf, size_t len, PhoneFrame *out)
{
    memset(out, 0, sizeof(*out));
    out->cmd = PFC_NONE;

    if (len < 2)
    {
        out->status = PF_TOO_SHORT;
        return;
    }
    if (len > PHONE_FRAME_MAX)
    {
        out->status = PF_TOO_LONG;
        return;
    }

    const uint8_t msg_len = buf[0];
    const uint8_t type = buf[1];
    out->msg_len = msg_len;
    out->type = type;

    if ((size_t)msg_len > len)
    {
        out->status = PF_CLAIMS_MORE;
        return;
    }

    out->status = PF_OK;

    switch (type)
    {
    case 0x10:
    {
        if (len < 3)
            break;
        if (buf[2] != 0x20)
            return; // not a hello (the old code did not look at [3] either)
        if (len < 4)
            break;
        if (buf[3] == 0x30)
        {
            out->cmd = PFC_HELLO;
            if (msg_len >= 35)
            {
                if (len >= 36)
                {
                    out->has_hash = true;
                    memcpy(out->u.hash, buf + 4, 32);
                }
            }
        }
        return;
    }

    case 0x20:
    {
        if (len < 6)
            break;
        out->cmd = PFC_TIME;
        memcpy(&out->ts, buf + 2, sizeof(out->ts));
        return;
    }

    case 0x50:
    {
        if (len < 3)
            break;
        const size_t n = buf[2];
        if (3 + n > len)
            break;
        out->cmd = PFC_CALL;
        memcpy(out->u.data, buf + 3, n); // n <= 255, data holds 256
        out->u.data[n] = 0;
        out->dlen = (uint16_t)n;
        return;
    }

    case 0x70:
    case 0x80:
    case 0x90:
    {
        if (len < 6)
            break;
        // the save flag follows the 4-byte value; a frame without it never saves
        out->save_flag = (len >= 7 && buf[6] == 0x0A);
        if (type == 0x90)
        {
            out->cmd = PFC_ALT;
            memcpy(&out->ival, buf + 2, sizeof(out->ival));
        }
        else
        {
            out->cmd = (type == 0x70) ? PFC_LAT : PFC_LON;
            memcpy(&out->fval, buf + 2, sizeof(out->fval));
        }
        return;
    }

    case 0x95:
    {
        if (len < 4)
            break;
        out->cmd = PFC_SYMBOL;
        out->sym[0] = buf[2];
        out->sym[1] = buf[3];
        return;
    }

    case 0xA0:
    {
        if (msg_len < 2)
            return; // too short for length+type: ignored, as before
        const size_t n = (size_t)msg_len - 2; // <= 253, msg_len <= len
        out->cmd = PFC_TEXT;
        memcpy(out->u.data, buf + 2, n);
        out->u.data[n] = 0;
        out->dlen = (uint16_t)n;
        // the old check read [2] and [3] whatever the text length; keep the result, read only what was received
        out->dash = (len >= 4 && buf[2] == '-' && buf[3] == '-');
        return;
    }

    case 0x55:
    {
        if (len < 3)
            break;
        const size_t ssid_len = buf[2];
        if (4 + ssid_len > msg_len) // msg_len <= len, so buf[ssid_len + 3] is in range
            return;
        const size_t pwd_len = buf[ssid_len + 3];
        if (4 + ssid_len + pwd_len > msg_len)
            return;
        if (ssid_len > 0 && pwd_len > 0)
        {
            const size_t sc = ssid_len < PHONE_FRAME_SSID_CAP - 1 ? ssid_len : PHONE_FRAME_SSID_CAP - 1;
            const size_t pc = pwd_len < PHONE_FRAME_PWD_CAP - 1 ? pwd_len : PHONE_FRAME_PWD_CAP - 1;
            out->cmd = PFC_WIFI;
            out->ssid_len = (uint8_t)ssid_len;
            out->pwd_len = (uint8_t)pwd_len;
            memcpy(out->u.wifi.ssid, buf + 3, sc);
            out->u.wifi.ssid[sc] = 0;
            memcpy(out->u.wifi.pwd, buf + 4 + ssid_len, pc);
            out->u.wifi.pwd[pc] = 0;
        }
        return;
    }

    case 0xF0:
        out->cmd = PFC_SAVE_RESET;
        return;

    default:
        return;
    }

    // a field of a known type reaches past len
    out->status = PF_TRUNCATED;
}

#endif
