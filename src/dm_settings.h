// dm_settings.h -- sender-side DM transport settings, every board.
//
// docs/pn-retry-snf-port-plan.md section 4 "E1": `--dmretry off|3` ("Enhanced
// message transport protection"). Mode 9 is retired -- the outbox now uses
// the official XOR retry format (src/pn_retry.h), which needs no more than
// 4 transmissions to say the same thing 9 same/fresh-id attempts used to.
// Persisted per T13 outside struct s_meshcom_settings: ESP32 NVS key
// "dm_retry", nRF52 own file "/dm.cfg". Default off, which leaves the sender
// byte-identical to today (no outbox at all). A previously stored raw value
// of 9 (old firmware) is read back as DM_RETRY_3 -- migration, not rewritten
// unless the setting is saved again.
#pragma once

#include <stdint.h>
#include <stdbool.h>

enum DmRetryMode { DM_RETRY_OFF = 0, DM_RETRY_3 = 3 };

void            dmSettingsLoad(void);          // boot, after init_flash(); applies the RAM copy
void            dmSettingsSave(void);          // from commandAction() (loop task) only
enum DmRetryMode dmRetryMode(void);            // the RAM copy the sender consults
void            dmRetrySet(enum DmRetryMode m);
const char     *dmRetryModeName(enum DmRetryMode m);   // "off" / "3"
bool            dmRetryModeParse(const char *text, enum DmRetryMode *out);
