// dm_settings.cpp -- sender-side DM retry ladder setting persistence.
//
// docs/dm-stage1-plan-20260914.md sections 1, 2, 8: `--dmretry off|3|9`.
// dmRetryModeName/dmRetryModeParse/dmRetryMode/dmRetrySet are
// platform-neutral and stay at the top of this file -- env:native's
// build_src_filter links dm_settings.cpp for exactly those helpers
// (platformio.ini). All platform persistence code below is guarded behind
// `#ifndef NATIVE_BUILD` with an all-boards `#else` giving no-op Load/Save,
// same precedent as src/mheard_functions.cpp / src/extudp_functions.cpp, so
// `pio test -e native` still compiles and links this module.
#include "dm_settings.h"

#include <string.h>

static enum DmRetryMode s_mode = DM_RETRY_OFF;

const char *dmRetryModeName(enum DmRetryMode m)
{
    switch(m)
    {
        case DM_RETRY_3: return "3";
        case DM_RETRY_9: return "9";
        default:         return "off";
    }
}

bool dmRetryModeParse(const char *text, enum DmRetryMode *out)
{
    if(text == NULL || out == NULL)
        return false;

    if(strcmp(text, "off") == 0) { *out = DM_RETRY_OFF; return true; }
    if(strcmp(text, "3") == 0)   { *out = DM_RETRY_3;   return true; }
    if(strcmp(text, "9") == 0)   { *out = DM_RETRY_9;   return true; }

    return false;
}

enum DmRetryMode dmRetryMode(void) { return s_mode; }
void dmRetrySet(enum DmRetryMode m) { s_mode = m; }

#ifndef NATIVE_BUILD

// Pulls in the active variant's configuration.h -> configuration_global.h
// and, via Arduino.h, the nRF52 core's nrf.h (which #defines NRF52_SERIES) --
// same precedent as src/msgstore_settings.cpp:13-16.
#include <Arduino.h>
#include <configuration.h>

#if defined(ESP32)

// Own Preferences handle, same "Credentials" namespace and begin/end-per-call
// pattern src/msgstore_settings.cpp uses for its own keys (own-file rule,
// stage 3 brief) -- a separate handle, not msgstore_settings.cpp's or
// src/esp32/esp32_flash.cpp's global `preferences` object.
#include <Preferences.h>

static Preferences s_dm_prefs;

void dmSettingsLoad(void)
{
    s_dm_prefs.begin("Credentials", false);
    uint8_t raw = s_dm_prefs.getUChar("dm_retry", (uint8_t)DM_RETRY_OFF);
    s_dm_prefs.end();

    // Absent key -> DM_RETRY_OFF (getUChar's own default above); any stored
    // value that isn't 3 or 9 -- corrupt NVS, a future firmware's mode --
    // also falls back to off rather than being trusted.
    s_mode = (raw == (uint8_t)DM_RETRY_3 || raw == (uint8_t)DM_RETRY_9)
                 ? (enum DmRetryMode)raw : DM_RETRY_OFF;
}

void dmSettingsSave(void)
{
    s_dm_prefs.begin("Credentials", false);
    s_dm_prefs.putUChar("dm_retry", (uint8_t)s_mode);
    s_dm_prefs.end();
}

#elif defined(NRF52_SERIES)

// nRF52: own file "/dm.cfg" -- {magic, mode}, memcmp-guarded write. Same
// File/InternalFS handling as msgstore_settings.cpp's own "/msgstore.cfg"
// (own File instance, not src/nrf52/nrf52_flash.cpp's global `lora_file`).
// dmSettingsSave() is called only from commandAction() (loop task), never
// from the LoRa/BLE tasks -- same constraint nrf52_flash.cpp's
// save_settings() carries.
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
using namespace Adafruit_LittleFS_Namespace;

static const char kDmSettingsFileName[] = "/dm.cfg";

// 'D','M','C','1' packed little-endian, same style as
// msgstore_settings.cpp's MSGSTORE_FILE_MAGIC_V1/V2.
#define DM_SETTINGS_FILE_MAGIC (((uint32_t)'D') | ((uint32_t)'M' << 8) | ((uint32_t)'C' << 16) | ((uint32_t)'1' << 24))

struct DmSettingsFile
{
    uint32_t magic;
    uint8_t  mode;
};

static File s_dm_file(InternalFS);

void dmSettingsLoad(void)
{
    InternalFS.begin();   // idempotent -- init_flash() already mounted it at boot

    struct DmSettingsFile rec;
    memset(&rec, 0, sizeof(rec));
    bool ok = false;

    s_dm_file.open(kDmSettingsFileName, FILE_O_READ);
    if(s_dm_file)
    {
        // Size before read, like nrf52_flash.cpp/msgstore_settings.cpp -- a
        // short/garbled file must not be trusted.
        if(s_dm_file.size() == sizeof(rec))
        {
            s_dm_file.read((uint8_t *)&rec, sizeof(rec));
            ok = (rec.magic == DM_SETTINGS_FILE_MAGIC);
        }
        s_dm_file.close();
    }

    if(!ok || !(rec.mode == (uint8_t)DM_RETRY_3 || rec.mode == (uint8_t)DM_RETRY_9))
    {
        s_mode = DM_RETRY_OFF;
        return;
    }

    s_mode = (enum DmRetryMode)rec.mode;
}

void dmSettingsSave(void)
{
    InternalFS.begin();

    struct DmSettingsFile rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic = DM_SETTINGS_FILE_MAGIC;
    rec.mode = (uint8_t)s_mode;

    // Read-compare-write, like msgstore_settings.cpp's memcmp guard -- skip
    // the erase/write cycle when nothing actually changed.
    struct DmSettingsFile current;
    memset(&current, 0, sizeof(current));

    s_dm_file.open(kDmSettingsFileName, FILE_O_READ);
    if(s_dm_file)
    {
        if(s_dm_file.size() == sizeof(current))
            s_dm_file.read((uint8_t *)&current, sizeof(current));
        s_dm_file.close();
    }

    if(memcmp(&current, &rec, sizeof(rec)) == 0)
        return;

    InternalFS.remove(kDmSettingsFileName);
    if(s_dm_file.open(kDmSettingsFileName, FILE_O_WRITE))
    {
        s_dm_file.write((uint8_t *)&rec, sizeof(rec));
        s_dm_file.flush();
        s_dm_file.close();
    }
}

#else // neither ESP32 nor NRF52_SERIES -- no board in this tree, but leaves
      // the module linkable rather than failing the build.

void dmSettingsLoad(void) {}
void dmSettingsSave(void) {}

#endif // ESP32 / NRF52_SERIES

#else // NATIVE_BUILD -- no platform storage; Load/Save are no-ops so
      // env:native still compiles and links this module.

void dmSettingsLoad(void) {}
void dmSettingsSave(void) {}

#endif // !NATIVE_BUILD
