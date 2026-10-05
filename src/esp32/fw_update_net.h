// fw_update_net.h -- network and staging layer of the firmware auto update (#1187, AU-04)
//
// One FreeRTOS task does everything that blocks (TLS handshake 2-5 s, the
// download, the flash erase and write, the read-back): the loop task only
// starts a job and polls a status snapshot. At most one job runs at a time.
//
//   CHECK     GET api.github.com/repos/<repo>/releases/latest, whole body
//             through two FwJsonScan (the .bin.zz asset and the raw .bin), then
//             fwShouldInstall(). Never downloads. availNewer without an asset or
//             digest stays true (notify), installable is then false and zlen 0.
//   DOWNLOAD  needs the last CHECK (availNewer + digest). fwStageLayout(), erase
//             the stage range at the END of ota_0, stream the .bin.zz into it
//             in 4 KB blocks, SHA-256 against the API digest (mandatory), CRC32
//             read-back, then the FWS2 record into NVS.
//
// Battery, phone, WiFi-up policy and the retry timer (fwAttemptFailed) are the
// caller's job (fw_update.h). Serial markers: [AU];check|refuse|dl|stage|fail|heap.
//
// Bench staging (INSTRUMENT_ENABLED builds only, no symbol and no string in a release image):
//   STAGELAN  the same stage path as DOWNLOAD (fwStageLayout, erase, write, read-back CRC,
//             FWS2 record) fed from a bench-LAN HTTP server over PLAIN http. The URL is
//             vetted by fwLanUrlParse() (private IPv4 only). The compressed length is the
//             Content-Length, the SHA-256 is computed and recorded (no API digest to compare).
//
// NVS namespace "fwstage": key "rec" = FWS2 record blob (FW_STAGE_ENC_LEN bytes,
// FwStageRecord::off is relative to the start of ota_0; crc32 is the zlib CRC-32
// of the staged compressed bytes), key "last" = tag string last staged (AU-D16).

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Bench-LAN URL vetting (pure, host-tested in test/test_command_setters). Accepts exactly
// "http://A.B.C.D[:port]/path" with A.B.C.D in 10/8, 172.16/12 or 192.168/16. Everything
// else (https, hostnames, userinfo, public / loopback / link-local addresses, a missing
// path, control characters, spaces) is refused. Not guarded: it is plain C++.
// ---------------------------------------------------------------------------
struct FwLanUrl
{
    char host[16];   // dotted quad, canonical (no leading zeros)
    uint16_t port;   // 80 if the URL has none
    char path[160];  // starts with '/'
};

inline bool fwLanUrlParse(const char *url, FwLanUrl &out)
{
    out.host[0] = '\0';
    out.port = 0;
    out.path[0] = '\0';
    if (url == nullptr)
        return false;
    static const char kScheme[] = "http://";
    const char *p = url;
    for (size_t i = 0; i < sizeof(kScheme) - 1; i++)
        if (p[i] != kScheme[i])
            return false;
    p += sizeof(kScheme) - 1;

    uint32_t oct[4];
    for (int i = 0; i < 4; i++)
    {
        uint32_t v = 0;
        int n = 0;
        while (*p >= '0' && *p <= '9')
        {
            v = v * 10u + (uint32_t)(*p - '0');
            p++;
            if (++n > 3)
                return false;
        }
        if (n == 0 || v > 255u || (n > 1 && p[-n] == '0'))
            return false; // empty, out of range, or a leading zero (octal ambiguity)
        oct[i] = v;
        if (i < 3)
        {
            if (*p != '.')
                return false;
            p++;
        }
    }
    const bool priv = oct[0] == 10u || (oct[0] == 172u && oct[1] >= 16u && oct[1] <= 31u) ||
                      (oct[0] == 192u && oct[1] == 168u);
    if (!priv)
        return false;

    uint32_t port = 80u;
    if (*p == ':')
    {
        p++;
        port = 0;
        int n = 0;
        while (*p >= '0' && *p <= '9')
        {
            port = port * 10u + (uint32_t)(*p - '0');
            p++;
            if (++n > 5)
                return false;
        }
        if (n == 0 || port < 1u || port > 65535u)
            return false;
    }
    if (*p != '/')
        return false;
    size_t pl = 0;
    for (; p[pl] != '\0'; pl++)
    {
        const unsigned char c = (unsigned char)p[pl];
        if (c <= 0x20u || c >= 0x7Fu || c == '#' || c == '\\')
            return false;
        if (pl + 1 >= sizeof(out.path))
            return false;
    }
    for (size_t i = 0; i < pl; i++)
        out.path[i] = p[i];
    out.path[pl] = '\0';

    const unsigned n = (unsigned)snprintf(out.host, sizeof(out.host), "%u.%u.%u.%u",
                                                    (unsigned)oct[0], (unsigned)oct[1],
                                                    (unsigned)oct[2], (unsigned)oct[3]);
    if (n >= sizeof(out.host))
        return false;
    out.port = (uint16_t)port;
    return true;
}

#if defined(ESP32)

#include "../fw_update.h"

enum FwNetJob : uint8_t
{
    FWJ_NONE = 0,
    FWJ_CHECK,
    FWJ_DOWNLOAD,
    FWJ_STAGELAN   // INSTRUMENT_ENABLED builds only (fwNetStartLan)
};

enum FwNetState : uint8_t
{
    FWS_IDLE = 0,
    FWS_BUSY,
    FWS_DONE_OK,
    FWS_DONE_FAIL
};

struct FwNetStatus
{
    FwNetState state;
    FwNetJob job;          // the running or last finished job
    char availTag[24];     // tag of the newest release seen by the last CHECK, "" if none
    bool availNewer;       // fwShouldInstall() said yes (and not draft/prerelease)
    bool installable;      // the release carries the .bin.zz asset AND its sha256 digest
    uint32_t zlen, ilen;   // .bin.zz size (0 = not installable), raw .bin size (0 = none)
    char stagedTag[24];    // tag of the valid FWS2 record, "" if none
    bool staged;           // a valid FWS2 record exists in NVS
    char lastErr[48];      // short reason of the last failed job, "" otherwise
    uint32_t lastCheckEpoch; // time() at the last successful CHECK, 0 if none this boot
    uint32_t bytesDone;    // DOWNLOAD progress (compressed bytes written)
};

// False if a job runs, WiFi is not connected, or the task could not be created.
// `channel` is a FwChannel. The job runs in its own task (12 KB stack, prio 1,
// core 1) that deletes itself; the status moves BUSY -> DONE_OK / DONE_FAIL.
bool fwNetStart(FwNetJob job, uint8_t channel);

// INSTRUMENT_ENABLED builds only: start a STAGELAN job (see the header comment). `url` is
// vetted with fwLanUrlParse(), `ilen` is the expected inflated length (> 0), `tag` must
// parse with fwParseTag(). False if any of that fails, a job runs, or WiFi is down.
// Declared unconditionally (a prototype costs nothing); defined only with INSTRUMENT_ENABLED.
bool fwNetStartLan(const char *url, uint32_t ilen, const char *tag);

// Deliberate reboot into Safeboot, which applies the staged update (inflates the FWS2
// record's image into ota_0). The shared body of `--ota-update` and the AU handover; defined
// in command_functions.cpp. Shows the OTA screen, waits 2 s, sets the Safeboot partition as
// boot partition, clears the loop breadcrumb and restarts. Returns false (and does not
// restart) if there is no Safeboot partition; it does not return on success.
bool auRebootToSafeboot(void);

// Thread-safe snapshot (portMUX). Never blocks on the network.
void fwNetGetStatus(FwNetStatus &out);

// A valid FWS2 record exists in NVS. Cached (read once, refreshed by DOWNLOAD),
// cheap enough for every loop pass.
bool fwNetStagedPending(void);

// Reads and decodes the record from NVS. False if there is none or it is invalid.
// A record for another env, or whose tag is not newer than the running version, is
// stale: it is removed from NVS and false is returned.
bool fwNetLoadRecord(FwStageRecord &out);

// Removes the record and Safeboot's "tries" counter and clears the cached staged flag. Used
// when a handover cannot succeed (no Safeboot partition, boot partition not settable), so
// it is not retried at every boot. The "last" tag (reinstall guard) stays.
void fwNetClearRecord(void);

// NVS "fwstage"/"last"; "" if none. n must be >= 1.
void fwNetLastInstalledTag(char *out, size_t n);

#endif // ESP32
