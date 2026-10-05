// fw_update_net.cpp -- network and staging layer of the firmware auto update (#1187, AU-04)
// See fw_update_net.h for the contract (STAGELAN, the bench-LAN staging job, exists only with
// INSTRUMENT_ENABLED and shares stagePrepare()/stageFinish() with DOWNLOAD). Decisions: AU-D11 (compressed staging at the
// end of ota_0), AU-D12 (trimmed roots), AU-D13 (small heap), AU-D15/16/17.

#if defined(ESP32)

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_rom_crc.h>
#include <esp_system.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../configuration_global.h"
#include "../fw_update.h"
#include "../fw_update_assets.h"
#include "../fw_update_http.h"
#include "../fw_update_roots.h"
#include "../hmac_sha256.h"
#include "../instrument.h" // INSTRUMENT_ENABLED: the bench-only STAGELAN job
#include "../safeboot/safeboot_ver.h" // SafebootVerScan, AU_SAFEBOOT_MIN
#include "fw_update_net.h"

// Measurement stub (-DFWNET_STUB): the five entry points as no-ops, so the flash cost of
// the real layer (TLS, HTTPClient, mbedtls) is the difference between the two builds.
#ifdef FWNET_STUB
bool fwNetStart(FwNetJob, uint8_t) { return false; }
#if INSTRUMENT_ENABLED
bool fwNetStartLan(const char *, uint32_t, const char *) { return false; }
#endif
void fwNetGetStatus(FwNetStatus &out) { memset(&out, 0, sizeof(out)); }
bool fwNetStagedPending(void) { return false; }
bool fwNetLoadRecord(FwStageRecord &) { return false; }
void fwNetClearRecord(void) {}
bool fwNetMarkHandover(const char *) { return false; }
void fwNetUnmarkHandover(void) {}
bool fwNetSafebootOld(void) { return false; }
int fwNetSafebootVersion(void) { return -1; }
bool fwNetSafebootCapable(void) { return false; }
void fwNetLastInstalledTag(char *out, size_t n)
{
    if (out != nullptr && n > 0)
        out[0] = '\0';
}
#else

#ifndef MC_BUILD_TAG
#define MC_BUILD_TAG ""
#endif
#ifndef MC_ENV_NAME
#define MC_ENV_NAME ""
#endif

// Largest free block needed before a TLS session starts (mbedtls allocates the 16 KB
// record buffers on the heap). Below it the job is refused instead of risking an
// allocation failure in the middle of the handshake (classic ESP32 with NimBLE).
#ifndef FWNET_MIN_BLOCK
#define FWNET_MIN_BLOCK 17000u
#endif
#define FWNET_BLK 4096u                  // staging block, heap-allocated per job
#define FWNET_JSON_MAX (300u * 1024u)    // releases/latest is about 65 KB; hard cap
#define FWNET_IDLE_MS 20000u             // no byte for this long = stall
#define FWNET_HANDSHAKE_S 20u            // WiFiClientSecure::setHandshakeTimeout() takes SECONDS
#define FWNET_CHECK_DEADLINE_MS (5u * 60u * 1000u)     // whole CHECK body
#define FWNET_DL_DEADLINE_MS (30u * 60u * 1000u)       // whole DOWNLOAD body
#define FWNET_TASK_STACK 12288u

#if CONFIG_FREERTOS_UNICORE
#define FWNET_CORE 0
#else
#define FWNET_CORE 1
#endif

namespace
{

// ---------------------------------------------------------------------------
// shared state
// ---------------------------------------------------------------------------

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
FwNetStatus s_st; // zero-initialised: IDLE, NONE
bool s_busy = false;
bool s_stagedKnown = false;
FwNetJob s_argJob = FWJ_NONE;
uint8_t s_argCh = 0;

// Result of the last CHECK that DOWNLOAD works from (the status holds tag/zlen/ilen).
struct Avail
{
    bool checked;    // a CHECK completed
    bool found;      // the .bin.zz asset was found with a usable url
    bool haveDigest; // ... and carries a well-formed sha256 digest
    uint8_t sha[32];
    char url[256]; // never printed: it is a plain download URL, the redirect carries the token
};
Avail s_av;

char s_err[48]; // dynamic error text of the running job (one job at a time)

#if INSTRUMENT_ENABLED
// Arguments of the running STAGELAN job, copied in under s_mux by startJob().
struct LanArg
{
    char url[200];
    uint32_t ilen;
    char tag[24];
};
LanArg s_lan;
#endif

// ---------------------------------------------------------------------------
// markers: formatted into a stack buffer, written in one call (Print::printf
// mallocs above 64 B, which starves NimBLE on the classic ESP32)
// ---------------------------------------------------------------------------

void auLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void auLog(const char *fmt, ...)
{
    char b[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof(b))
        n = (int)sizeof(b) - 1;
    Serial.write((const uint8_t *)b, (size_t)n);
}

void heapMark(const char *when)
{
    auLog("[AU];heap;%s;free;%u;min;%u;blk;%u;stk;%u\n", when,
          (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
          (unsigned)uxTaskGetStackHighWaterMark(nullptr)); // bytes of stack never used
}

// Failed heap allocations while a job runs (heap_caps failed-alloc hook). mbedtls maps an
// allocation failure inside the signature check of the chain verification to the same
// -9984 (X509 verify failed) as a genuinely untrusted chain, and WiFiClientSecure frees the
// TLS context (and with it the verify flags) before lastError() can be asked. A non-zero
// count at the failure is the tell for "ran out of heap", zero means "untrusted". The hook
// runs in the allocating context: counters only, no printing, no locking.
volatile uint32_t s_afN, s_afFirstSize, s_afFirstCaps, s_afMaxSize;
const char *volatile s_afFirstFn;

void onAllocFail(size_t size, uint32_t caps, const char *fn)
{
    if (__atomic_fetch_add(&s_afN, 1u, __ATOMIC_RELAXED) == 0u)
    {
        s_afFirstSize = (uint32_t)size;
        s_afFirstCaps = caps;
        s_afFirstFn = fn;
    }
    if ((uint32_t)size > s_afMaxSize)
        s_afMaxSize = (uint32_t)size;
}

void allocFailArm()
{
    s_afN = 0;
    s_afFirstSize = 0;
    s_afFirstCaps = 0;
    s_afMaxSize = 0;
    s_afFirstFn = nullptr;
    heap_caps_register_failed_alloc_callback(onAllocFail); // one slot, idempotent
}

void allocFailMark(const char *when)
{
    const uint32_t n = s_afN;
    const char *fn = s_afFirstFn;
    auLog("[AU];alloc;%s;fails;%u;first;%u;caps;%x;max;%u;fn;%s\n", when, (unsigned)n,
          (unsigned)s_afFirstSize, (unsigned)s_afFirstCaps, (unsigned)s_afMaxSize,
          fn != nullptr ? fn : "-");
}

void copyStr(char *dst, size_t n, const char *src)
{
    size_t i = 0;
    if (n == 0)
        return;
    if (src != nullptr)
        for (; i + 1 < n && src[i] != '\0'; i++)
            dst[i] = src[i];
    dst[i] = '\0';
}

bool joinName(char *out, size_t n, const char *base, const char *suffix)
{
    out[0] = '\0';
    if (base == nullptr || base[0] == '\0')
        return false;
    size_t bl = strlen(base), sl = strlen(suffix);
    if (bl + sl + 1 > n)
        return false;
    memcpy(out, base, bl);
    memcpy(out + bl, suffix, sl + 1);
    return true;
}

const char *runningVersion()
{
    static const char kTag[] = MC_BUILD_TAG;
    static const char kSrc[] = SOURCE_VERSION SOURCE_VERSION_SUB;
    return kTag[0] != '\0' ? kTag : kSrc;
}

// ---------------------------------------------------------------------------
// NVS
// ---------------------------------------------------------------------------

#define FWNET_NS "fwstage"
#define FWNET_KEY_REC "rec"
#define FWNET_KEY_LAST "last"
#define FWNET_KEY_TRIES "tries" // Safeboot's apply-attempt counter (src/safeboot/main.cpp)
// Old-Safeboot detection (AU-08): the app writes "hand" (= the staged tag) before every handover;
// a NEW Safeboot removes "hand" and "nocap" first thing at boot. If the app boots with "hand" set
// and the record still there, Safeboot did not process it: it is an old one that ignores FWS2.
// "nocap" = 1 then suppresses the automatic handover (a reboot loop every update window).
// This is only the backstop: fwNetSafebootVersion() reads the Safeboot image up front (AU-12) and
// fwNetSafebootCapable() combines both verdicts.
#define FWNET_KEY_HAND "hand"
#define FWNET_KEY_NOCAP "nocap"

// Removes the record AND Safeboot's attempt counter: a new tag must not inherit the failed
// attempts of an old one (AU-08 R4), and the handover marker. Neither key present is success.
bool nvsClearRecord()
{
    Preferences p;
    if (!p.begin(FWNET_NS, false))
        return false;
    bool ok = true;
    if (p.isKey(FWNET_KEY_REC))
        ok = p.remove(FWNET_KEY_REC);
    if (p.isKey(FWNET_KEY_TRIES))
        ok = p.remove(FWNET_KEY_TRIES) && ok;
    if (p.isKey(FWNET_KEY_HAND))
        ok = p.remove(FWNET_KEY_HAND) && ok;
    p.end();
    return ok;
}

// ---------------------------------------------------------------------------
// HTTP response body pump: identity (Content-Length or close-delimited) and
// HTTP/1.1 chunked (FwChunkDec, src/fw_update_http.h, host-tested), read straight off
// the TLS client with an idle timeout and a total deadline, so a stalled or trickling
// peer cannot hang the task (HTTPClient::writeToStream has neither).
// Decoded bytes are collected into a 4 KB block and handed to feed() full blocks
// at a time (the last one short).
// ---------------------------------------------------------------------------

struct Pump
{
    uint8_t *blk;
    size_t fill;
    FwSinkFn feed;
    void *ctx;
};

bool pumpFlush(Pump &p)
{
    if (p.fill == 0)
        return true;
    const bool ok = p.feed(p.ctx, p.blk, p.fill);
    p.fill = 0;
    vTaskDelay(1); // let the loop task, WiFi and the idle task run between blocks
    return ok;
}

bool pumpPush(void *vp, const uint8_t *d, size_t n)
{
    Pump &p = *(Pump *)vp;
    while (n > 0)
    {
        size_t room = FWNET_BLK - p.fill;
        size_t k = n < room ? n : room;
        memcpy(p.blk + p.fill, d, k);
        p.fill += k;
        d += k;
        n -= k;
        if (p.fill == FWNET_BLK && !pumpFlush(p))
            return false;
    }
    return true;
}

// clen < 0: unknown (chunked, or read until the peer closes). maxBytes bounds the
// decoded total ("toobig"), deadlineMs the whole body ("timeout"), FWNET_IDLE_MS the gap
// between two bytes ("stall"). Returns nullptr on success, else a short reason.
const char *pumpBody(WiFiClient &c, Pump &p, bool chunked, int32_t clen, uint32_t maxBytes,
                     uint32_t deadlineMs)
{
    uint8_t raw[1024];
    FwChunkDec dec;
    uint32_t rawTotal = 0;
    const uint32_t t0 = millis();
    uint32_t last = t0;

    // counts decoded bytes in front of the block collector and flags a cap trip
    struct Cnt
    {
        Pump *p;
        uint32_t n;
        uint32_t max;
        bool over;
    } cnt = {&p, 0, maxBytes, false};
    FwSinkFn counted = [](void *v, const uint8_t *d, size_t n) -> bool {
        Cnt &k = *(Cnt *)v;
        if ((size_t)(k.max - k.n) < n)
        {
            k.over = true;
            return false;
        }
        k.n += (uint32_t)n;
        return pumpPush(k.p, d, n);
    };

    for (;;)
    {
        if (!chunked && clen >= 0 && rawTotal >= (uint32_t)clen)
            break;
        if (chunked && dec.st == FwChunkDec::DONE)
            break;
        if ((uint32_t)(millis() - t0) > deadlineMs)
            return "timeout";

        int avail = c.available();
        if (avail <= 0)
        {
            if (!c.connected() && c.available() <= 0)
            {
                if (!chunked && clen < 0)
                    break; // close-delimited body ends with the connection
                return "short";
            }
            if ((uint32_t)(millis() - last) > FWNET_IDLE_MS)
                return "stall";
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        size_t want = (size_t)avail < sizeof(raw) ? (size_t)avail : sizeof(raw);
        if (!chunked && clen >= 0 && want > (size_t)clen - rawTotal)
            want = (size_t)clen - rawTotal;
        int n = c.read(raw, want);
        if (n <= 0)
        {
            if ((uint32_t)(millis() - last) > FWNET_IDLE_MS)
                return "stall";
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        last = millis();
        if (!chunked)
        {
            rawTotal += (uint32_t)n;
            if (!counted(&cnt, raw, (size_t)n))
                return cnt.over ? "toobig" : "feed";
        }
        else if (dec.feed(raw, (size_t)n, counted, &cnt) < 0)
            return cnt.over ? "toobig" : "chunk";
    }
    return pumpFlush(p) ? nullptr : "feed";
}

// ---------------------------------------------------------------------------
// HTTP GET with redirects; returns nullptr and leaves the response header read
// ---------------------------------------------------------------------------

const char *httpFail(int code, WiFiClientSecure &c)
{
    if (code > 0)
    {
        snprintf(s_err, sizeof(s_err), "http %d", code);
        return s_err;
    }
    char eb[96];
    eb[0] = '\0';
    const int le = c.lastError(eb, sizeof(eb));
    if (le != 0)
    {
        auLog("[AU];tls;%d;%s\n", le, eb);
        // -9984 alone cannot tell "alloc failed" from "untrusted": the fail counter and the
        // heap low-water mark can (fails > 0 and min near 0 = heap, else the chain)
        allocFailMark("tls");
        heapMark("tlsfail");
        snprintf(s_err, sizeof(s_err), "tls %d", le);
    }
    else
        snprintf(s_err, sizeof(s_err), "conn %d", code);
    return s_err;
}

struct Resp
{
    bool chunked;
    int32_t clen; // -1 unknown
    bool encoded; // Content-Encoding other than identity
};

// Sends the GET, follows redirects (strict: GET only), checks for 200 and reads the
// framing headers. The body is left unread on `c`.
const char *httpGet(HTTPClient &http, WiFiClientSecure &c, const String &url, bool wantJson,
                    Resp &out)
{
    static const char *kHdr[] = {"Transfer-Encoding", "Content-Encoding"};
    http.setReuse(false); // "Connection: close"
    http.setTimeout(15000);
    http.setConnectTimeout(10000);
    http.setRedirectLimit(3);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.collectHeaders(kHdr, 2);
    if (!http.begin(c, url))
        return "begin";
    char ua[40];
    snprintf(ua, sizeof(ua), "MeshCom-AU/%s%s", SOURCE_VERSION, SOURCE_VERSION_SUB);
    http.setUserAgent(ua);
    if (wantJson)
        http.addHeader("Accept", "application/vnd.github+json");
    const int code = http.GET();
    if (code != HTTP_CODE_OK)
        return httpFail(code, c);
    out.chunked = http.header("Transfer-Encoding").equalsIgnoreCase("chunked");
    out.clen = http.getSize();
    String ce = http.header("Content-Encoding");
    out.encoded = ce.length() > 0 && !ce.equalsIgnoreCase("identity");
    return nullptr;
}

// ---------------------------------------------------------------------------
// CHECK
// ---------------------------------------------------------------------------

struct CheckCtx
{
    FwJsonScan *z; // the .bin.zz asset
    FwJsonScan *r; // the raw .bin asset (its size = ilen)
};

bool checkFeed(void *v, const uint8_t *d, size_t n)
{
    CheckCtx &k = *(CheckCtx *)v;
    fwScanFeed(*k.z, (const char *)d, n);
    fwScanFeed(*k.r, (const char *)d, n);
    return !k.z->err && !k.r->err;
}

const char *runCheck(FwChannel ch)
{
    char zname[40], rname[40];
    if (!fwAssetName(MC_ENV_NAME, zname, sizeof(zname)) ||
        !joinName(rname, sizeof(rname), fwAssetBase(MC_ENV_NAME), ".bin"))
        return "noenv";

    FwVersion run;
    if (!fwParseTag(runningVersion(), run))
        return "badver";

    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < FWNET_MIN_BLOCK)
    {
        auLog("[AU];refuse;lowheap\n");
        return "lowheap";
    }

    uint8_t *blk = (uint8_t *)heap_caps_malloc(FWNET_BLK, MALLOC_CAP_8BIT);
    FwJsonScan *sz = (FwJsonScan *)heap_caps_malloc(sizeof(FwJsonScan), MALLOC_CAP_8BIT);
    FwJsonScan *sr = (FwJsonScan *)heap_caps_malloc(sizeof(FwJsonScan), MALLOC_CAP_8BIT);
    if (blk == nullptr || sz == nullptr || sr == nullptr)
    {
        heap_caps_free(blk);
        heap_caps_free(sz);
        heap_caps_free(sr);
        return "nomem";
    }
    fwScanInit(*sz, zname);
    fwScanInit(*sr, rname);

    const char *err = nullptr;
    {
        String url = String("https://api.github.com/repos/") + fwChannelRepo(ch) + "/releases/latest";
        WiFiClientSecure client;
        client.setCACert(FW_UPDATE_ROOTS_PEM);
        client.setHandshakeTimeout(FWNET_HANDSHAKE_S);
        HTTPClient http;
        Resp rs = {false, -1, false};
        err = httpGet(http, client, url, true, rs);
        if (err == nullptr && rs.encoded)
            err = "enc";
        if (err == nullptr)
        {
            CheckCtx cx = {sz, sr};
            Pump pump = {blk, 0, checkFeed, &cx};
            err = pumpBody(client, pump, rs.chunked, rs.clen, FWNET_JSON_MAX, FWNET_CHECK_DEADLINE_MS);
        }
        http.end();
    }
    heap_caps_free(blk);
    blk = nullptr;

    FwReleaseInfo iz, ir;
    bool okz = false;
    if (err == nullptr)
    {
        okz = fwScanResult(*sz, iz);
        fwScanResult(*sr, ir);
        if (!okz)
            err = "json";
    }
    heap_caps_free(sz);
    heap_caps_free(sr);
    if (err != nullptr)
        return err;

    char last[24];
    fwNetLastInstalledTag(last, sizeof(last));
    FwVersion cand;
    fwParseTag(iz.tag, cand);
    const bool newer = !iz.draft && !iz.prerelease && fwShouldInstall(run, cand, iz.tag, last);
    // installable = asset and digest both present; availNewer stays true without them so
    // notify mode can still report the release, but zlen is then 0 (nothing to stage).
    const bool installable = iz.found && iz.size > 0 && iz.haveDigest;
    const uint32_t zlen = installable ? iz.size : 0;
    const uint32_t ilen = ir.found ? ir.size : 0;
    if (newer && !iz.found)
        auLog("[AU];refuse;noasset\n");
    else if (newer && !installable)
        auLog("[AU];refuse;nodigest\n");

    const time_t now = time(nullptr);
    portENTER_CRITICAL(&s_mux);
    copyStr(s_st.availTag, sizeof(s_st.availTag), iz.tag);
    s_st.availNewer = newer;
    s_st.installable = installable;
    s_st.zlen = zlen;
    s_st.ilen = ilen;
    s_st.lastCheckEpoch = now > 0 ? (uint32_t)now : 0;
    s_av.checked = true;
    s_av.found = iz.found && iz.size > 0;
    s_av.haveDigest = iz.haveDigest;
    memcpy(s_av.sha, iz.sha256, sizeof(s_av.sha));
    memcpy(s_av.url, iz.url, sizeof(s_av.url));
    portEXIT_CRITICAL(&s_mux);
    auLog("[AU];check;tag;%s;newer;%u\n", iz.tag, newer ? 1u : 0u);
    return nullptr;
}

// ---------------------------------------------------------------------------
// DOWNLOAD
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Stage-area flash access. THE ONLY code in the firmware that writes ota_0 while running
// from it.
//
// Why not esp_partition_erase_range()/esp_partition_write(): every erase/write on the
// default flash chip goes through CHECK_WRITE_ADDRESS -> os_func->region_protected(), which
// is esp_partition_main_flash_region_safe(). It marks the WHOLE running app partition (and
// the bootloader / partition table) as protected, and with
// CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS that is abort(). The rule is a conservative
// guard against overwriting code that is executing; it cannot know that we only touch the
// tail of the slot.
//
// Why our range is safe: stagePrepare() has fwStageLayout() prove that the running image
// ends below stageOff (the image is mapped and executed only from [0, image_len); nothing
// at or above stageOff is mapped, cached as code or read by the running app), and that the
// inflated image also fits below stageOff. So an erase/write at or above ota0->address +
// stageOff cannot touch a byte that executes or that Safeboot will later write its head
// into.
//
// How: a private esp_flash_t, copied from *esp_flash_default_chip, with a private
// esp_flash_os_functions_t copied from the default one and region_protected = NULL (the
// check is skipped when the hook is NULL). Everything else (start/end flash lock, yield,
// temp buffers, os_func_data, host, chip driver) is the default chip's, so locking and the
// cache handling stay the IDF's. The private chip is used by stageFlash() and nowhere else,
// and stageFlash() re-checks the range on EVERY call: inside ota_0 and at or above the
// armed stage start. Public API only (esp_flash.h).
// ---------------------------------------------------------------------------

enum StageOp : uint8_t
{
    STAGE_ERASE,
    STAGE_WRITE,
    STAGE_READ
};

struct StageFlash
{
    bool armed;
    uint32_t base;  // absolute flash address of ota_0
    uint32_t size;  // size of ota_0
    uint32_t lo;    // stage offset inside ota_0 (fwStageLayout), the lowest offset any call may use
    esp_flash_t chip;
    esp_flash_os_functions_t os;
};
StageFlash s_sf;

// Arms the private chip for [ota0->address + stageOff, ota0 end). False if the default chip
// is missing or the offset is outside the partition.
bool stageFlashArm(const esp_partition_t *ota0, uint32_t stageOff)
{
    s_sf.armed = false;
    if (ota0 == nullptr || esp_flash_default_chip == nullptr || esp_flash_default_chip->os_func == nullptr ||
        stageOff == 0 || stageOff >= ota0->size)
        return false;
    s_sf.os = *esp_flash_default_chip->os_func;
    s_sf.os.region_protected = nullptr;
    s_sf.chip = *esp_flash_default_chip;
    s_sf.chip.os_func = &s_sf.os;
    s_sf.base = ota0->address;
    s_sf.size = ota0->size;
    s_sf.lo = stageOff;
    s_sf.armed = true;
    return true;
}

void stageFlashDisarm() { s_sf.armed = false; }

// rel is an offset inside ota_0. The range [rel, rel+len) must lie inside ota_0 and at or
// above the armed stage start, else nothing is touched and ESP_ERR_INVALID_ARG comes back.
esp_err_t stageFlash(StageOp op, uint32_t rel, void *buf, uint32_t len)
{
    if (!s_sf.armed || len == 0 || rel < s_sf.lo || rel > s_sf.size || len > s_sf.size - rel)
        return ESP_ERR_INVALID_ARG;
    const uint32_t addr = s_sf.base + rel;
    switch (op)
    {
    case STAGE_ERASE:
        return esp_flash_erase_region(&s_sf.chip, addr, len);
    case STAGE_WRITE:
        return buf != nullptr ? esp_flash_write(&s_sf.chip, buf, addr, len) : ESP_ERR_INVALID_ARG;
    case STAGE_READ:
        return buf != nullptr ? esp_flash_read(&s_sf.chip, buf, addr, len) : ESP_ERR_INVALID_ARG;
    }
    return ESP_ERR_INVALID_ARG;
}

struct DlCtx
{
    const esp_partition_t *part;
    uint32_t base;    // stage offset in the partition
    uint32_t zlen;
    uint32_t written;
    uint32_t crc;     // zlib CRC-32 of what was handed to flash
    Sha256Ctx sha;
};

bool dlFeed(void *v, const uint8_t *d, size_t n)
{
    DlCtx &k = *(DlCtx *)v;
    if (k.written + n > k.zlen)
        return false;
    sha256Update(k.sha, d, n);
    k.crc = esp_rom_crc32_le(k.crc, d, (uint32_t)n);
    if (stageFlash(STAGE_WRITE, k.base + k.written, (void *)d, (uint32_t)n) != ESP_OK)
        return false;
    k.written += (uint32_t)n;
    portENTER_CRITICAL(&s_mux);
    s_st.bytesDone = k.written;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

const char *layoutReason(FwRefuse r)
{
    switch (r)
    {
    case FW_REF_SIZE:
        return "size";
    case FW_REF_OVERLAP:
        return "overlap";
    case FW_REF_SMALL:
        return "small";
    case FW_REF_INFLATE:
        return "inflate";
    default:
        return "layout";
    }
}

const char *refuse(const char *why)
{
    auLog("[AU];refuse;%s\n", why);
    return why;
}

// Everything DOWNLOAD and STAGELAN have in common, in two steps around the transfer:
//   stagePrepare  layout (running image below the stage, inflated image below it), heap, the
//                 stage record removed (the area is about to be destroyed), erase
//   stageFinish   SHA-256 (against `expectSha` if given, else just computed), read-back CRC,
//                 the FWS2 record, the reinstall guard, the status
struct Stage
{
    const esp_partition_t *ota0;
    uint32_t off;
    uint8_t *blk;
    DlCtx dl;
};

// Returns nullptr, or the reason. On a reason nothing is left allocated and no record
// survives; refusals before the first destructive step were printed by refuse().
const char *stagePrepare(Stage &sg, uint32_t zlen, uint32_t ilen, uint32_t minBlock)
{
    memset(&sg, 0, sizeof(sg));

    // layout: the stage area is the end of ota_0, the running image must stay below it
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_t *ota0 =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    if (run == nullptr || ota0 == nullptr || run->address != ota0->address)
        return refuse("notota0");
    esp_partition_pos_t pos;
    pos.offset = run->address;
    pos.size = run->size;
    esp_image_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    if (esp_image_get_metadata(&pos, &meta) != ESP_OK || meta.image_len == 0)
        return refuse("imglen");
    uint32_t off = 0;
    const FwRefuse lr = fwStageLayout(ota0->size, meta.image_len, zlen, ilen, off);
    if (lr != FW_OK)
        return refuse(layoutReason(lr));

    if (!stageFlashArm(ota0, off))
        return refuse("noflash");

    if (minBlock != 0 && heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < minBlock)
    {
        stageFlashDisarm();
        return refuse("lowheap");
    }
    uint8_t *blk = (uint8_t *)heap_caps_malloc(FWNET_BLK, MALLOC_CAP_8BIT);
    if (blk == nullptr)
    {
        stageFlashDisarm();
        return "nomem";
    }

    // from here on the stage area is destroyed: no record may survive
    if (!nvsClearRecord())
    {
        heap_caps_free(blk);
        stageFlashDisarm();
        return refuse("nvs");
    }
    portENTER_CRITICAL(&s_mux);
    s_st.staged = false;
    s_st.stagedTag[0] = '\0';
    s_st.bytesDone = 0;
    portEXIT_CRITICAL(&s_mux);

    sg.ota0 = ota0;
    sg.off = off;
    sg.blk = blk;
    sg.dl.part = ota0;
    sg.dl.base = off;
    sg.dl.zlen = zlen;
    sha256Init(sg.dl.sha);

    // erase in 64 KB steps with a yield between them
    const char *err = nullptr;
    const uint32_t eraseLen = (zlen + 0xFFFu) & ~0xFFFu;
    for (uint32_t e = 0; e < eraseLen && err == nullptr; e += 0x10000u)
    {
        uint32_t n = eraseLen - e < 0x10000u ? eraseLen - e : 0x10000u;
        if (stageFlash(STAGE_ERASE, off + e, nullptr, n) != ESP_OK)
            err = "erase";
        vTaskDelay(1);
    }
    if (err != nullptr)
    {
        heap_caps_free(blk);
        sg.blk = nullptr;
        stageFlashDisarm();
        nvsClearRecord();
        return err;
    }
    return nullptr;
}

// `err` is the outcome of the transfer (nullptr = the body arrived). Frees sg.blk.
const char *stageFinish(Stage &sg, const char *err, const char *tag, uint32_t ilen,
                        const uint8_t *expectSha)
{
    const uint32_t zlen = sg.dl.zlen;
    if (err != nullptr && strcmp(err, "feed") == 0)
        err = "write"; // flash write failed or the peer sent more than zlen
    if (err == nullptr && sg.dl.written != zlen)
        err = "short";

    // digest: compared if the caller has one (DOWNLOAD: mandatory), else only recorded
    uint8_t dig[32];
    memset(dig, 0, sizeof(dig));
    if (err == nullptr)
    {
        sha256Final(sg.dl.sha, dig);
        if (expectSha != nullptr && memcmp(dig, expectSha, sizeof(dig)) != 0)
            err = "digest";
    }

    // read-back CRC over what is really in flash
    uint32_t crc = 0;
    if (err == nullptr)
    {
        for (uint32_t o = 0; o < zlen && err == nullptr;)
        {
            size_t n = zlen - o < FWNET_BLK ? zlen - o : FWNET_BLK;
            if (stageFlash(STAGE_READ, sg.off + o, sg.blk, (uint32_t)n) != ESP_OK)
                err = "readback";
            else
                crc = esp_rom_crc32_le(crc, sg.blk, (uint32_t)n);
            o += (uint32_t)n;
            vTaskDelay(1);
        }
        if (err == nullptr && crc != sg.dl.crc)
            err = "crc";
    }
    heap_caps_free(sg.blk);
    sg.blk = nullptr;
    stageFlashDisarm(); // the private chip is not used past this point

    // record (the commit), then the reinstall guard
    if (err == nullptr)
    {
        FwStageRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.magic = FW_STAGE_MAGIC;
        copyStr(rec.tag, sizeof(rec.tag), tag);
        copyStr(rec.env, sizeof(rec.env), MC_ENV_NAME);
        rec.off = sg.off;
        rec.zlen = zlen;
        rec.ilen = ilen;
        rec.crc32 = crc;
        memcpy(rec.sha256, dig, sizeof(rec.sha256));
        const time_t now = time(nullptr);
        rec.ts = now > 0 ? (uint32_t)now : 0;
        uint8_t enc[FW_STAGE_ENC_LEN];
        const size_t el = fwRecordEncode(rec, enc, sizeof(enc));
        Preferences p;
        if (el != FW_STAGE_ENC_LEN || !p.begin(FWNET_NS, false))
            err = "nvs";
        else
        {
            if (p.putBytes(FWNET_KEY_REC, enc, el) != el)
                err = "nvs";
            else if (p.putString(FWNET_KEY_LAST, tag) == 0)
            {
                p.remove(FWNET_KEY_REC);
                err = "nvs";
            }
            p.end();
        }
    }

    if (err != nullptr)
    {
        nvsClearRecord();
        return err;
    }

    portENTER_CRITICAL(&s_mux);
    s_st.staged = true;
    copyStr(s_st.stagedTag, sizeof(s_st.stagedTag), tag);
    s_stagedKnown = true;
    portEXIT_CRITICAL(&s_mux);
    auLog("[AU];stage;ok;tag;%s;zlen;%u\n", tag, (unsigned)zlen);
    return nullptr;
}

const char *runDownload()
{
    // snapshot of the CHECK result
    Avail av;
    char tag[24];
    uint32_t zlen, ilen;
    bool newer;
    portENTER_CRITICAL(&s_mux);
    av = s_av;
    copyStr(tag, sizeof(tag), s_st.availTag);
    zlen = s_st.zlen;
    ilen = s_st.ilen;
    newer = s_st.availNewer;
    portEXIT_CRITICAL(&s_mux);

    // refusals below are not download attempts: nothing was touched, the caller must not
    // count them (it keys Install on FwNetStatus::installable)
    if (!av.checked || !newer || tag[0] == '\0')
        return refuse("nocheck");
    if (!av.found || av.url[0] == '\0')
        return refuse("noasset");
    if (!av.haveDigest)
        return refuse("nodigest");
    if (zlen == 0)
        return refuse("noasset");

    Stage sg;
    const char *err = stagePrepare(sg, zlen, ilen, FWNET_MIN_BLOCK);
    if (err != nullptr)
        return err;

    uint32_t t0 = millis();
    {
        WiFiClientSecure client;
        client.setCACert(FW_UPDATE_ROOTS_PEM);
        client.setHandshakeTimeout(FWNET_HANDSHAKE_S);
        HTTPClient http;
        Resp rs = {false, -1, false};
        err = httpGet(http, client, String(av.url), false, rs);
        if (err == nullptr && (rs.encoded || rs.chunked))
            err = "enc";
        if (err == nullptr && rs.clen != (int32_t)zlen)
            err = "size";
        if (err == nullptr)
        {
            Pump pump = {sg.blk, 0, dlFeed, &sg.dl};
            err = pumpBody(client, pump, false, (int32_t)zlen, zlen, FWNET_DL_DEADLINE_MS);
        }
        http.end();
    }
    if (err == nullptr && sg.dl.written == zlen)
        auLog("[AU];dl;bytes;%u;ms;%u\n", (unsigned)sg.dl.written, (unsigned)(millis() - t0));
    return stageFinish(sg, err, tag, ilen, av.sha);
}

#if INSTRUMENT_ENABLED
// STAGELAN: DOWNLOAD's stage path fed from the bench LAN over plain http (no TLS, no redirect).
// The compressed length is the Content-Length; the SHA-256 is computed and recorded.
const char *runStageLan()
{
    const LanArg a = s_lan;
    FwLanUrl u;
    if (!fwLanUrlParse(a.url, u))
        return refuse("url");

    static const char *kHdr[] = {"Transfer-Encoding", "Content-Encoding"};
    WiFiClient client;
    HTTPClient http;
    http.setReuse(false); // "Connection: close"
    http.setTimeout(15000);
    http.setConnectTimeout(10000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.collectHeaders(kHdr, 2);
    if (!http.begin(client, String(u.host), u.port, String(u.path)))
        return "begin";
    const int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        if (code > 0)
            snprintf(s_err, sizeof(s_err), "http %d", code);
        else
            snprintf(s_err, sizeof(s_err), "conn %d", code);
        http.end();
        return s_err;
    }
    const bool chunked = http.header("Transfer-Encoding").equalsIgnoreCase("chunked");
    String ce = http.header("Content-Encoding");
    const bool encoded = ce.length() > 0 && !ce.equalsIgnoreCase("identity");
    const int32_t clen = http.getSize();
    if (chunked || encoded)
    {
        http.end();
        return "enc";
    }
    if (clen <= 0)
    {
        http.end();
        return "size";
    }
    const uint32_t zlen = (uint32_t)clen;

    Stage sg;
    const char *err = stagePrepare(sg, zlen, a.ilen, 0);
    if (err != nullptr)
    {
        http.end();
        return err;
    }
    const uint32_t t0 = millis();
    Pump pump = {sg.blk, 0, dlFeed, &sg.dl};
    err = pumpBody(client, pump, false, clen, zlen, FWNET_DL_DEADLINE_MS);
    http.end();
    if (err == nullptr && sg.dl.written == zlen)
        auLog("[AU];dl;bytes;%u;ms;%u\n", (unsigned)sg.dl.written, (unsigned)(millis() - t0));
    return stageFinish(sg, err, a.tag, a.ilen, nullptr);
}
#endif

// ---------------------------------------------------------------------------
// task
// ---------------------------------------------------------------------------

void fwNetTask(void *)
{
    s_err[0] = '\0';
    heapMark("start");
    allocFailArm();
#if AU_BLE_PAUSE
    // AU-D18: free the NimBLE stack's heap for the TLS handshake. Only CHECK and DOWNLOAD talk
    // TLS; esp32BlePause() declines (false) while a phone is connected or ready.
    const bool blePaused = (s_argJob == FWJ_CHECK || s_argJob == FWJ_DOWNLOAD) && esp32BlePause();
#endif
    const char *err = (s_argJob == FWJ_CHECK) ? runCheck((FwChannel)s_argCh)
#if INSTRUMENT_ENABLED
                      : (s_argJob == FWJ_STAGELAN) ? runStageLan()
#endif
                                                   : runDownload();
    heapMark("end"); // after every client object is gone
    allocFailMark("end");
    if (err != nullptr)
        auLog("[AU];fail;%s\n", err);

#if AU_BLE_PAUSE
    // BLE comes back BEFORE the status leaves BUSY: nothing else starts a job meanwhile, and the
    // loop treats BLE as down for the whole pause (esp32_main.cpp guards every BLE path).
    if (blePaused)
    {
        bool up = false;
        for (int i = 0; i < 3 && !up; i++)
        {
            if (i > 0)
                vTaskDelay(pdMS_TO_TICKS(1000));
            up = esp32BleResume();
        }
        heapMark("resumed"); // a failed resume is retried by the loop (esp32_main.cpp)
    }
#endif

    portENTER_CRITICAL(&s_mux);
    if (err != nullptr)
    {
        s_st.state = FWS_DONE_FAIL;
        copyStr(s_st.lastErr, sizeof(s_st.lastErr), err);
    }
    else
    {
        s_st.state = FWS_DONE_OK;
        s_st.lastErr[0] = '\0';
    }
    s_busy = false;
    portEXIT_CRITICAL(&s_mux);
    vTaskDelete(nullptr);
}

// Once per boot (first status read / first tick / first web read). "hand" present means the last
// boot was a handover: record still valid = Safeboot did not touch it = old Safeboot -> "nocap";
// record gone = it was applied (or went stale). "hand" is consumed either way.
bool s_hcClaimed = false;
bool s_safebootOld = false;

// AU-12: "nocap" stores the Safeboot version it was earned under (+1, so it is never 0). Safeboot's own
// setup() removes the key; a reflash through the web flasher boots ota_0 directly and never runs it, so
// the tag is what lets the scan retire a verdict that belongs to the image that was replaced.
uint8_t nocapTag()
{
    const int v = fwNetSafebootVersion();
    return (uint8_t)((v < 0 ? 0 : (v > 250 ? 250 : v)) + 1);
}

void bootCheckOnce()
{
    portENTER_CRITICAL(&s_mux);
    const bool mine = !s_hcClaimed;
    s_hcClaimed = true;
    portEXIT_CRITICAL(&s_mux);
    if (!mine)
        return;

    bool hand = false;
    {
        Preferences p;
        if (p.begin(FWNET_NS, false))
        {
            hand = p.isKey(FWNET_KEY_HAND);
            p.end();
        }
    }
    bool markOld = false;
    if (hand)
    {
        FwStageRecord r;
        markOld = fwNetLoadRecord(r); // drops a stale record (e.g. the version just applied)
        Preferences p;
        if (p.begin(FWNET_NS, false))
        {
            if (markOld)
                p.putUChar(FWNET_KEY_NOCAP, nocapTag());
            p.remove(FWNET_KEY_HAND);
            p.end();
        }
        if (markOld)
            auLog("[AU];refuse;old_safeboot\n");
    }
    bool old = markOld;
    if (!old)
    {
        Preferences p;
        if (p.begin(FWNET_NS, false))
        {
            const uint8_t nc = p.getUChar(FWNET_KEY_NOCAP, 0);
            old = nc != 0 && nc == nocapTag();
            if (nc != 0 && !old)
            {
                // AU-12: earned under another Safeboot (a web-flasher reflash does not run Safeboot's
                // setup(), so nothing else clears it): the verdict no longer applies
                p.remove(FWNET_KEY_NOCAP);
                auLog("[AU];safeboot;nocap;cleared;ver;%d\n", fwNetSafebootVersion());
            }
            p.end();
        }
    }
    portENTER_CRITICAL(&s_mux);
    s_safebootOld = old;
    portEXIT_CRITICAL(&s_mux);
}

void ensureStagedLoaded()
{
    bootCheckOnce();
    bool known;
    portENTER_CRITICAL(&s_mux);
    known = s_stagedKnown;
    portEXIT_CRITICAL(&s_mux);
    if (known)
        return;
    FwStageRecord r;
    memset(&r, 0, sizeof(r));
    const bool ok = fwNetLoadRecord(r);
    portENTER_CRITICAL(&s_mux);
    if (!s_stagedKnown) // a DOWNLOAD may have published meanwhile
    {
        s_st.staged = ok;
        copyStr(s_st.stagedTag, sizeof(s_st.stagedTag), ok ? r.tag : "");
        s_stagedKnown = true;
    }
    portEXIT_CRITICAL(&s_mux);
}

} // namespace

// ---------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------

namespace
{
// Shared start: marks the job running, records its arguments (inside the critical section,
// so a refused start never touches the running job's), creates the task.
#if INSTRUMENT_ENABLED
bool startJob(FwNetJob job, uint8_t channel, const LanArg *lan)
#else
bool startJob(FwNetJob job, uint8_t channel)
#endif
{
    if (WiFi.status() != WL_CONNECTED)
        return false;

    FwNetStatus prev;
    memset(&prev, 0, sizeof(prev));
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    if (!s_busy)
    {
        s_busy = true;
        prev = s_st;
        s_st.state = FWS_BUSY;
        s_st.job = job;
        s_st.bytesDone = 0;
        s_st.lastErr[0] = '\0';
        s_argJob = job;
        s_argCh = channel;
#if INSTRUMENT_ENABLED
        if (lan != nullptr)
            s_lan = *lan;
#endif
        ok = true;
    }
    portEXIT_CRITICAL(&s_mux);
    if (!ok)
        return false;

    if (xTaskCreatePinnedToCore(fwNetTask, "fwnet", FWNET_TASK_STACK, nullptr, 1, nullptr,
                                FWNET_CORE) != pdPASS)
    {
        portENTER_CRITICAL(&s_mux);
        s_st = prev;
        s_busy = false;
        portEXIT_CRITICAL(&s_mux);
        return false;
    }
    return true;
}
} // namespace

bool fwNetStart(FwNetJob job, uint8_t channel)
{
    if (job != FWJ_CHECK && job != FWJ_DOWNLOAD)
        return false;
#if INSTRUMENT_ENABLED
    return startJob(job, channel, nullptr);
#else
    return startJob(job, channel);
#endif
}

#if INSTRUMENT_ENABLED
bool fwNetStartLan(const char *url, uint32_t ilen, const char *tag)
{
    FwLanUrl u;
    FwVersion v;
    LanArg a;
    if (url == nullptr || tag == nullptr || strlen(url) >= sizeof(a.url) || strlen(tag) >= sizeof(a.tag) ||
        !fwLanUrlParse(url, u) || ilen == 0 || ilen > 16u * 1024u * 1024u || !fwParseTag(tag, v))
        return false;
    memset(&a, 0, sizeof(a));
    copyStr(a.url, sizeof(a.url), url);
    a.ilen = ilen;
    copyStr(a.tag, sizeof(a.tag), tag);
    return startJob(FWJ_STAGELAN, 0, &a);
}
#endif

void fwNetGetStatus(FwNetStatus &out)
{
    ensureStagedLoaded();
    portENTER_CRITICAL(&s_mux);
    out = s_st;
    portEXIT_CRITICAL(&s_mux);
}

bool fwNetStagedPending(void)
{
    ensureStagedLoaded();
    bool v;
    portENTER_CRITICAL(&s_mux);
    v = s_st.staged;
    portEXIT_CRITICAL(&s_mux);
    return v;
}

bool fwNetLoadRecord(FwStageRecord &out)
{
    uint8_t buf[FW_STAGE_ENC_LEN];
    Preferences p;
    if (!p.begin(FWNET_NS, false))
        return false;
    bool ok = false;
    bool have = false;
    if (p.getBytesLength(FWNET_KEY_REC) == FW_STAGE_ENC_LEN)
    {
        have = true;
        if (p.getBytes(FWNET_KEY_REC, buf, sizeof(buf)) == FW_STAGE_ENC_LEN)
            ok = fwRecordDecode(buf, sizeof(buf), out);
    }
    // A record that no longer applies is dropped, not just hidden: it is for another
    // board env, or its version is not newer than what runs now (the operator flashed
    // the staged version or a newer one by USB or OTA). Safeboot would otherwise apply
    // an old image over the newer one.
    if (ok)
    {
        FwVersion run, cand;
        const bool runOk = fwParseTag(runningVersion(), run);
        const bool candOk = fwParseTag(out.tag, cand);
        if (strncmp(out.env, MC_ENV_NAME, sizeof(out.env) - 1) != 0 || (runOk && (!candOk || fwCompare(run, cand) <= 0)))
            ok = false;
    }
    if (have && !ok && p.isKey(FWNET_KEY_REC))
    {
        p.remove(FWNET_KEY_REC);
        p.remove(FWNET_KEY_TRIES);
        auLog("[AU];refuse;stalerec\n");
        // the cached "staged" flag must follow, or fwTick would hand over every second
        portENTER_CRITICAL(&s_mux);
        s_st.staged = false;
        s_st.stagedTag[0] = '\0';
        portEXIT_CRITICAL(&s_mux);
    }
    p.end();
    return ok;
}

bool fwNetMarkHandover(const char *tag)
{
    if (tag == nullptr || tag[0] == '\0')
        return false;
    Preferences p;
    if (!p.begin(FWNET_NS, false))
        return false;
    const bool ok = p.putString(FWNET_KEY_HAND, tag) != 0;
    p.end();
    return ok;
}

void fwNetUnmarkHandover(void)
{
    Preferences p;
    if (!p.begin(FWNET_NS, false))
        return;
    if (p.isKey(FWNET_KEY_HAND))
        p.remove(FWNET_KEY_HAND);
    p.end();
}

bool fwNetSafebootOld(void)
{
    bootCheckOnce();
    bool v;
    portENTER_CRITICAL(&s_mux);
    v = s_safebootOld;
    portEXIT_CRITICAL(&s_mux);
    return v;
}

// Safeboot capability version (AU-12): one scan of the "safeboot" partition per boot, cached.
// State 0 = unclaimed, 1 = a task is scanning, 2 = s_sbVer is final. Never holds s_mux over flash reads.
static uint8_t s_sbState = 0;
static int s_sbVer = -1;

int fwNetSafebootVersion(void)
{
    portENTER_CRITICAL(&s_mux);
    const bool mine = (s_sbState == 0);
    if (mine)
        s_sbState = 1;
    portEXIT_CRITICAL(&s_mux);

    if (!mine)
    {
        for (;;) // another task is scanning: wait for its verdict
        {
            uint8_t st;
            int v;
            portENTER_CRITICAL(&s_mux);
            st = s_sbState;
            v = s_sbVer;
            portEXIT_CRITICAL(&s_mux);
            if (st == 2)
                return v;
            vTaskDelay(1);
        }
    }

    int ver = -1;
    const esp_partition_t *p =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "safeboot");
    if (p != nullptr)
    {
        SafebootVerScan sc;
        uint8_t buf[256];
        bool ok = true;
        for (size_t off = 0; off < p->size; off += sizeof(buf))
        {
            const size_t n = (p->size - off) < sizeof(buf) ? (p->size - off) : sizeof(buf);
            if (esp_partition_read(p, off, buf, n) != ESP_OK)
            {
                ok = false;
                break;
            }
            sc.feed(buf, n);
        }
        if (ok)
            ver = sc.version();
    }
    portENTER_CRITICAL(&s_mux);
    s_sbVer = ver;
    s_sbState = 2;
    portEXIT_CRITICAL(&s_mux);
    auLog("[AU];safeboot;ver;%d;need;%d\n", ver, (int)AU_SAFEBOOT_MIN);
    return ver;
}

bool fwNetSafebootCapable(void)
{
    const int v = fwNetSafebootVersion();
    return v >= AU_SAFEBOOT_MIN && !fwNetSafebootOld();
}

void fwNetClearRecord(void)
{
    nvsClearRecord();
    portENTER_CRITICAL(&s_mux);
    s_st.staged = false;
    s_st.stagedTag[0] = '\0';
    s_stagedKnown = true;
    portEXIT_CRITICAL(&s_mux);
}

void fwNetLastInstalledTag(char *out, size_t n)
{
    if (out == nullptr || n == 0)
        return;
    out[0] = '\0';
    Preferences p;
    if (!p.begin(FWNET_NS, false))
        return;
    char tmp[24];
    tmp[0] = '\0';
    p.getString(FWNET_KEY_LAST, tmp, sizeof(tmp));
    p.end();
    tmp[sizeof(tmp) - 1] = '\0';
    copyStr(out, n, tmp);
}

// Link probe for flash-delta measurements only (-DFWNET_LINK_PROBE): the layer is
// linked only when something references it. Inert in every normal build.
#ifdef FWNET_LINK_PROBE
namespace
{
struct FwNetProbe
{
    FwNetProbe()
    {
        static volatile void *keep[] = {(void *)fwNetStart, (void *)fwNetGetStatus,
                                        (void *)fwNetStagedPending, (void *)fwNetLoadRecord,
                                        (void *)fwNetLastInstalledTag};
        (void)keep;
    }
};
FwNetProbe s_probe;
} // namespace
#endif

#endif // FWNET_STUB

#endif // ESP32
