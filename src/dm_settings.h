// dm_settings.h -- sender-side DM transport settings, every board.
//
// docs/dm-stage1-plan-20260914.md: `--dmretry off|3|9` ("Enhanced message
// protection"). Persisted per T13 outside struct s_meshcom_settings: ESP32
// NVS key "dm_retry", nRF52 own file "/dm.cfg". Default off, which leaves the
// sender byte-identical to today (no outbox at all).
#pragma once

#include <stdint.h>
#include <stdbool.h>

enum DmRetryMode { DM_RETRY_OFF = 0, DM_RETRY_3 = 3, DM_RETRY_9 = 9 };

void            dmSettingsLoad(void);          // boot, after init_flash(); applies the RAM copy
void            dmSettingsSave(void);          // from commandAction() (loop task) only
enum DmRetryMode dmRetryMode(void);            // the RAM copy the sender consults
void            dmRetrySet(enum DmRetryMode m);
const char     *dmRetryModeName(enum DmRetryMode m);   // "off" / "3" / "9"
bool            dmRetryModeParse(const char *text, enum DmRetryMode *out);
