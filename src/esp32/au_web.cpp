#if defined(ESP32)

#include "au_web.h"
#include <Arduino.h>
#include <string.h>
#include <configuration.h>
#include <meshcom_settings.h>
#include "fw_update_net.h"   // FwNetStatus/fwNetGetStatus, fwJsonAppendUp/Img
#include "../web_functions/au_ui_state.h"   // AuUiIn/auStatusJson, auEtaS
#include <esp_timer.h>       // "up" (seconds since boot)
#include <esp_idf_version.h>
#if ESP_IDF_VERSION_MAJOR >= 5
#include <esp_app_desc.h>    // "img" (running image ELF SHA-256)
#define MC_RUNNING_APP_DESC() esp_app_get_description()
#else
#include <esp_ota_ops.h>     // "img": IDF 4.4 name
#define MC_RUNNING_APP_DESC() esp_ota_get_app_description()
#endif
#ifndef MC_BUILD_TAG
#define MC_BUILD_TAG ""
#endif

// GET /austatus body: the JSON of the Info page row (spec 5.1), n bytes in buf (0 = nothing written / buffer too small).
size_t auWebStatusJson(char *buf, size_t cap)
{
    FwNetStatus au;
    fwNetGetStatus(au);
    AuUiIn in;
    memset(&in, 0, sizeof(in));
    const char *stn = "idle";
    if (au.state == FWS_BUSY)
        stn = "busy";
    else if (au.state == FWS_DONE_OK)
        stn = "ok";
    else if (au.state == FWS_DONE_FAIL)
        stn = "fail";
    const char *jobn = "none";
    if (au.job == FWJ_CHECK)
        jobn = "check";
    else if (au.job == FWJ_DOWNLOAD)
        jobn = "download";
    else if (au.job == FWJ_STAGELAN)
        jobn = "stagelan";
    snprintf(in.st, sizeof(in.st), "%s", stn);
    snprintf(in.job, sizeof(in.job), "%s", jobn);
    snprintf(in.run, sizeof(in.run), "%.23s", MC_BUILD_TAG[0] ? MC_BUILD_TAG : "local");
    snprintf(in.avail, sizeof(in.avail), "%.23s", au.availTag);
    snprintf(in.staged, sizeof(in.staged), "%.23s", au.staged ? au.stagedTag : "");
    snprintf(in.err, sizeof(in.err), "%.23s", au.state == FWS_BUSY ? "" : au.lastErr);
    snprintf(in.mode, sizeof(in.mode), "%s", meshcom_settings.node_autoupd == 2 ? "auto" : "notify"); // never "off" (D1)
    snprintf(in.chan, sizeof(in.chan), "%s", meshcom_settings.node_updchan ? "dev" : "prod");
    in.newer = (au.availNewer && au.availTag[0]) ? 1 : 0;
    in.inst = au.installable ? 1 : 0;
    // pct: compressed bytes written / the length the running job stages (progLen), only while a download runs
    const bool dl = au.state == FWS_BUSY && (au.job == FWJ_DOWNLOAD || au.job == FWJ_STAGELAN);
    if (dl)
        in.pct = fwProgressPct(au.bytesDone, au.progLen);
    in.phone = fwNetPhoneHeld() ? 1 : 0; // same expression as the lowheap_phone token
    in.guard = auBleGuardActive() ? 1 : 0; // BLE guard (campaign-au-webgui-ux section 7)
    in.sbok = fwNetSafebootCapable() ? 1 : 0;
    snprintf(in.sbwhy, sizeof(in.sbwhy), "%s", fwNetSafebootWhy()); // ok|old|dio, tells the page why the install is blocked
    in.etaS = auEtaS(au.stagedIlen);
    fwNetHeapNow(&in.ifree, &in.iblk);

    size_t n = auStatusJson(in, buf, cap);
    { // install schedule: the countdown notice rides outside auStatusJson (its golden strings stay as they are)
        FwSchedView sv;
        fwNetSchedGet(sv);
        n = auAppendSched(buf, cap, n, sv.text);
    }
    // AU-30 "vpend": the staged record's dry run was deferred to the install (the page says "checked at install").
    // One NVS read, only while a record is staged.
    n = auAppendVpend(buf, cap, n, (au.staged && fwNetVerifyPending(au.stagedTag)) ? 1 : 0);
    // "up": seconds since boot, appended outside auStatusJson. The page compares it across the install restart
    // when the build tag cannot be compared (an image built without MC_BUILD_TAG reports run "local").
    n = fwJsonAppendUp(buf, cap, n, (uint32_t)(esp_timer_get_time() / 1000000));
    // "img": first 8 hex of the running image's ELF SHA-256. With run "local" the page counts the install as done
    // only when it changed: a Safeboot fallback restarts the node too (uptime drops) but keeps the old image.
    const esp_app_desc_t *d = MC_RUNNING_APP_DESC();
    if (d != NULL)
        n = fwJsonAppendImg(buf, cap, n, d->app_elf_sha256, sizeof(d->app_elf_sha256));
    return n;
}

#endif
