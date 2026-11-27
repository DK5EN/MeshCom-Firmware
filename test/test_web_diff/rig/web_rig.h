// Differential rig of the production web server (FORMAL-02, web server wave A step 0;
// docs/web-server-seams-20261127.md 7.1).
//
// One fake TCP client (CommonWebClient) that is fed the bytes of one request and records everything the
// code under test does to it: every byte written, the flushes, the close, the fake clock. One recorder
// for the handler calls ("trace"). Both entry points (the frozen copy today, the live code later) run
// against the same global `web_client`, so they see the same peer and are compared on the same record.
//
// Normalisation (done here, in the recorder, never in the golden):
//   - the clock is the rig's fake clock: it starts at WEB_RIG_T0 for every run and only advances when the
//     code polls an empty socket (1 ms per available() == 0). No wall-clock value ever reaches a record.
//   - session tokens and nonces come from a counter RNG (webRigRng), so they are the same on every run.
//   - the page handlers are stubs that only leave a trace line, so no heap/uptime/time-of-day value of a
//     real page exists in the output. getTimeString() is the constant "00:00:00".
//   - the four gzip assets are stubs of 7 bytes (their real content changes with every JS edit).
#pragma once

// keep the real src/web_functions/web_commonServer.h (WiFi / Ethernet client) out: this rig defines
// CommonWebClient itself, so web_rm_util.h and the code under test see the fake
#define _COMMONWEBSERVER_H_

#include <Arduino.h>

#include <stdarg.h>
#include <string>
#include <vector>

#define WEB_RIG_T0 100000UL // fake clock at the start of every run

// ---- profiles: the node state a request is answered under (the corpus holds bytes only)
struct WebProfile
{
    const char *name;
    const char *webpwd;   // node_webpwd ("" = no web login)
    const char *webhost;  // node_webhost ("" = none)
    bool auSession;       // fwNetSessionActive(): an AU session runs
    bool loggedIn;        // a session exists; SESSION_PLACEHOLDER in the request is replaced by its token
};

struct MeshcomSettingsStub
{
    char node_webpwd[20];
    char node_webhost[64];
};
extern MeshcomSettingsStub meshcom_settings;
extern bool bDEBUG;

// ---- the fake peer
struct WebRigState
{
    std::string in;           // bytes the peer will send
    size_t pos = 0;           // next byte to read
    bool peerCloses = false;  // the peer closed its side after the last byte (connected() false when drained)
    bool stopped = false;     // stop() was called
    int stopCalls = 0;
    int flushCalls = 0;
    int wdtFeeds = 0;         // meshcom_wdt_feed() calls (stubs/watchdog_feed.h)
    size_t closeAt = 0;       // bytes written when stop() was first called
    std::string out;          // every byte written to the client
    std::vector<std::string> trace;
    unsigned long now = WEB_RIG_T0;
    unsigned long idlePolls = 0;
    unsigned long t0 = WEB_RIG_T0;
    bool auSession = false;
};
WebRigState &webRig();

class CommonWebClient : public Stream
{
public:
    using Print::write;
    size_t write(uint8_t b) override;
    size_t write(const uint8_t *buf, size_t n) override;
    int available() override;
    int read() override;
    int peek() override;
    bool connected();
    void stop();
    void flush() override;
    void txEnd() {}
};
extern CommonWebClient web_client;

void webRigWdtFeed(); // counts one meshcom_wdt_feed() call
// one trace line (printf style); the handler stubs and the body readers call it
void webRigTrace(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// escapes a byte string for a record: printable ASCII as is, \\ \r \n \t, everything else \xNN
std::string webRigEsc(const std::string &s);

// ---- leak probe (decision (c) of web_route.h: the request line leaves no trace). A real String keeps its buffer
// bytes when it is set to "" or destroyed (see stubs/Arduino.h); only an overwrite removes them.
// webRigProbeArm(path): from now on every String that dies while its buffer still begins with `path` counts as a leak
// (the request line of the run, e.g. wg_path of work_webpage). Disarm after the run; a path that is empty, one byte or made
// of 'x' only (what rm_wipe_string writes) cannot be told from a wiped buffer and is not armed (returns false).
bool webRigProbeArm(const std::string &path);
void webRigProbeDisarm();
size_t webRigProbeLeaks();
// the request line's path of a request (the bytes between the first and the second space, up to a NUL)
std::string webRigRequestPath(const std::string &request);

// prepares one run: loads the request bytes, applies the profile, resets the auth state, the settings
// and the recorders. `token` replaces SESSION_PLACEHOLDER in the request for a logged-in profile.
void webRigBegin(const std::string &request, bool peerCloses, const WebProfile &p);
// the record of the run that just ended (text, deterministic)
std::string webRigRecord();

// the counter RNG every entry point is given
bool webRigRng(uint8_t *out, size_t n);
bool webRigAuSession();

// ---- node functions the request path calls (real signatures; recording stubs in rig/web_stubs.cpp)
void msgstorePurgeAll();
bool msgstorePurge(long slot);
bool msgstoreDeliverNow(long slot);
bool fwNetSessionActive();
int sendMessage(char *text, int len);
String getTimeString();
extern bool hasMsgFromPhone;
