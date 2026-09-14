// dm_settings.cpp -- STUB by the orchestrator; owner C replaces the
// persistence bodies (docs/dm-stage1-plan-20260914.md section 2).
#include "dm_settings.h"
#include <string.h>

static enum DmRetryMode s_mode = DM_RETRY_OFF;

void dmSettingsLoad(void) {}
void dmSettingsSave(void) {}
enum DmRetryMode dmRetryMode(void) { return s_mode; }
void dmRetrySet(enum DmRetryMode m) { s_mode = m; }
const char *dmRetryModeName(enum DmRetryMode m)
{
    switch(m) { case DM_RETRY_3: return "3"; case DM_RETRY_9: return "9"; default: return "off"; }
}
bool dmRetryModeParse(const char *text, enum DmRetryMode *out)
{
    if(text == NULL || out == NULL) return false;
    if(strcmp(text, "off") == 0) { *out = DM_RETRY_OFF; return true; }
    if(strcmp(text, "3") == 0)   { *out = DM_RETRY_3;   return true; }
    if(strcmp(text, "9") == 0)   { *out = DM_RETRY_9;   return true; }
    return false;
}
