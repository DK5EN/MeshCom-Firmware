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
// NVS namespace "fwstage": key "rec" = FWS2 record blob (FW_STAGE_ENC_LEN bytes,
// FwStageRecord::off is relative to the start of ota_0; crc32 is the zlib CRC-32
// of the staged compressed bytes), key "last" = tag string last staged (AU-D16).

#pragma once

#if defined(ESP32)

#include <stddef.h>
#include <stdint.h>

#include "../fw_update.h"

enum FwNetJob : uint8_t
{
    FWJ_NONE = 0,
    FWJ_CHECK,
    FWJ_DOWNLOAD
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

// Thread-safe snapshot (portMUX). Never blocks on the network.
void fwNetGetStatus(FwNetStatus &out);

// A valid FWS2 record exists in NVS. Cached (read once, refreshed by DOWNLOAD),
// cheap enough for every loop pass.
bool fwNetStagedPending(void);

// Reads and decodes the record from NVS. False if there is none or it is invalid.
// A record for another env, or whose tag is not newer than the running version, is
// stale: it is removed from NVS and false is returned.
bool fwNetLoadRecord(FwStageRecord &out);

// NVS "fwstage"/"last"; "" if none. n must be >= 1.
void fwNetLastInstalledTag(char *out, size_t n);

#endif // ESP32
