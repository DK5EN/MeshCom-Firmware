// Differential rig of the production web server: see web_rig.h.
#include "web_rig.h"

#include <stdio.h>
#include <string.h>

#include "hmac_sha256.h"
#include "web_functions/web_auth.h"

// tools/web_corpus.py SESSION_PLACEHOLDER
static const char kSessionPlaceholder[] = "aaaaaaaabbbbbbbbccccccccdddddddd";

SerialStub Serial;
MeshcomSettingsStub meshcom_settings;
bool bDEBUG = false;
CommonWebClient web_client;

WebRigState &webRig()
{
    static WebRigState s; // function-local: web_functions.cpp-style globals call millis() during static init
    return s;
}

unsigned long webRigMillis() { return webRig().now; }
bool webRigAuSession() { return webRig().auSession; }

// ---- fake client
size_t CommonWebClient::write(uint8_t b)
{
    webRig().out += (char)b;
    return 1;
}
size_t CommonWebClient::write(const uint8_t *buf, size_t n)
{
    webRig().out.append((const char *)buf, n);
    return n;
}
int CommonWebClient::available()
{
    WebRigState &r = webRig();
    if (r.stopped)
        return 0;
    const size_t left = r.in.size() - r.pos;
    if (left == 0)
    {
        r.now += 1; // an empty poll costs one fake millisecond: a stalled peer reaches the deadlines
        r.idlePolls++;
    }
    return (int)left;
}
int CommonWebClient::read()
{
    WebRigState &r = webRig();
    if (r.stopped || r.pos >= r.in.size())
        return -1;
    return (unsigned char)r.in[r.pos++];
}
int CommonWebClient::peek()
{
    WebRigState &r = webRig();
    return (r.stopped || r.pos >= r.in.size()) ? -1 : (unsigned char)r.in[r.pos];
}
bool CommonWebClient::connected()
{
    WebRigState &r = webRig();
    if (r.stopped)
        return false;
    return r.pos < r.in.size() || !r.peerCloses; // unread data keeps a socket "connected", as lwIP does
}
void CommonWebClient::stop()
{
    WebRigState &r = webRig();
    if (r.stopCalls == 0)
        r.closeAt = r.out.size();
    r.stopCalls++;
    r.stopped = true;
}
void CommonWebClient::flush() { webRig().flushCalls++; }

// ---- recorders
void webRigTrace(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    webRig().trace.push_back(buf);
}

std::string webRigEsc(const std::string &s)
{
    std::string o;
    for (unsigned char c : s)
    {
        char t[8];
        if (c == '\\')
            o += "\\\\";
        else if (c == '\r')
            o += "\\r";
        else if (c == '\n')
            o += "\\n";
        else if (c == '\t')
            o += "\\t";
        else if (c >= 0x20 && c <= 0x7e)
            o += (char)c;
        else
        {
            snprintf(t, sizeof(t), "\\x%02x", c);
            o += t;
        }
    }
    return o;
}

static uint8_t g_rngCounter = 0;
bool webRigRng(uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++)
        out[i] = (uint8_t)(g_rngCounter++ * 37u + 11u);
    return true;
}

static std::string hex(const uint8_t *p, size_t n)
{
    static const char d[] = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++)
    {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

void webRigBegin(const std::string &request, bool peerCloses, const WebProfile &p)
{
    WebRigState &r = webRig();
    r = WebRigState();
    g_rngCounter = 0;
    Serial.clear();
    bDEBUG = false;
    memset(&meshcom_settings, 0, sizeof(meshcom_settings));
    snprintf(meshcom_settings.node_webpwd, sizeof(meshcom_settings.node_webpwd), "%s", p.webpwd);
    snprintf(meshcom_settings.node_webhost, sizeof(meshcom_settings.node_webhost), "%s", p.webhost);
    r.auSession = p.auSession;
    r.peerCloses = peerCloses;

    webAuthReset();
    std::string req = request;
    if (p.loggedIn)
    {
        // a real login through the real web_auth.cpp: nonce, HMAC with the profile's password, token
        char nonce[33];
        char token[33] = {0};
        webAuthIssueNonce((uint32_t)r.now, webRigRng, nonce);
        uint8_t mac[32];
        hmacSha256((const uint8_t *)p.webpwd, strlen(p.webpwd), (const uint8_t *)nonce, 32, mac);
        const std::string arg = std::string(nonce) + "." + hex(mac, 32);
        const WebAuthResult wr = webAuthLogin(arg.c_str(), arg.size(), p.webpwd, (uint32_t)r.now, webRigRng, token);
        if (wr != WA_OK)
            webRigTrace("RIG-ERROR login failed");
        for (size_t at = req.find(kSessionPlaceholder); at != std::string::npos; at = req.find(kSessionPlaceholder, at + 32))
            req.replace(at, 32, token, 32);
        g_rngCounter = 0; // the handler under test starts from the same RNG state in every profile
    }
    r.in = req;
}

std::string webRigRecord()
{
    WebRigState &r = webRig();
    std::string s;
    char b[96];
    s += "trace:\n";
    for (const std::string &t : r.trace)
        s += "  " + webRigEsc(t) + "\n";
    s += "serial: " + webRigEsc(Serial.captured()) + "\n";
    snprintf(b, sizeof(b), "out: %zu bytes\n", r.out.size());
    s += b;
    size_t i = 0;
    while (i < r.out.size())
    {
        size_t nl = r.out.find('\n', i);
        size_t end = (nl == std::string::npos) ? r.out.size() : nl + 1;
        s += "| " + webRigEsc(r.out.substr(i, end - i)) + "\n";
        i = end;
    }
    snprintf(b, sizeof(b), "close: calls=%d at_byte=%zu flush_calls=%d\n", r.stopCalls, r.closeAt, r.flushCalls);
    s += b;
    snprintf(b, sizeof(b), "clock: elapsed_ms=%lu idle_polls=%lu\n", r.now - r.t0, r.idlePolls);
    s += b;
    snprintf(b, sizeof(b), "input: consumed=%zu of %zu\n", r.pos, r.in.size());
    s += b;
    return s;
}
