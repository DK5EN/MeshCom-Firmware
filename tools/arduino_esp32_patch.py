"""PlatformIO pre-script (AE-01..AE-03): patch arduino-esp32 2.0.17 in place.

NOTE: this patches the GLOBAL package store (~/.platformio/packages/
framework-arduinoespressif32), which every project on the machine shares. The patch is
idempotent (marker, no write when patched) and refuses unknown text (RuntimeError naming
the id). The folder is swapped between arduino-esp32 2.0.17 (package.json "3.20017..."),
Tasmota 3.3.x (safeboot) and 2.0.14 (espressif32@6.5.0 envs), so the build step patches only
the 2.0.17 line the anchors were taken from; on any other version it prints one loud line
("AE patches NOT applied: framework <ver>") and goes on, never patching blind and never
failing the build of an env that cannot use the patches.
Origin: IDF 4.4 review, FORMAL-01 wave 3b (docs/campaign-formal-01-20261123.md, rows 4, 5, 8).

AE-01, libraries/HTTPClient/src/HTTPClient.cpp (handleHeaderResponse):
What: the response header lines are read byte by byte with a cap of 2048 bytes per line and a
30 s deadline for the whole header phase (plus the existing idle timeout _tcpTimeout between
bytes). A longer line fails with HTTPC_ERROR_NO_HTTP_SERVER, an expired deadline or idle gap
with HTTPC_ERROR_READ_TIMEOUT, a lost connection with HTTPC_ERROR_CONNECTION_LOST; the
connection is then not reused.
Why: readStringUntil('\\n') grows a String without limit and each byte restarts the timeout, so
a hostile or MITM server can exhaust the heap or hold the caller for as long as it trickles.
Limits: real header lines (Location, Set-Cookie, Content-Security-Policy) stay far below
2 KiB and a 2 KiB String is an allocation a fragmented heap still serves. 30 s is six
times the default tcp timeout (5 s) and covers a slow but honest server.
Remove when arduino-esp32 caps header lines (not in 2.0.17 or the 3.x tree we checked).

AE-02, libraries/WiFi/src/WiFiUdp.cpp (parsePacket) and WiFiClient.cpp (WiFiClient(int fd),
connect):
What: UDP: cbuf's constructor does new char[len + 1], which aborts when it fails (nothrow on
the cbuf object does not help), and the result was dereferenced unchecked. Now a malloc probe
of the same size runs first; on failure the datagram is dropped (return 0). The client:
WiFiClientSocketHandle and WiFiClientRxBuffer are allocated with new(std::nothrow); on
failure the socket is closed and the client stays disconnected (connect() returns 0).
Why: at low heap a LAN datagram or a connect would abort() the node instead of failing.
Residual: the shared_ptr control block (a few bytes) still uses throwing new; the objects
allocated before it are larger, so they fail first. The probe is not atomic with the cbuf
allocation; the window is one task switch.
Remove when arduino-esp32 allocates these with nothrow and checks the result.

AE-03, libraries/WiFi/src/WiFiGeneric.cpp (hostByName):
What: the DNS callback no longer gets &aResult (the caller's stack). It writes a static
word s_dns_addr, guarded by a request id (s_dns_id) passed as callback_arg; hostByName copies
the word after the wait. A stale callback of a timed-out request carries an old id and
changes nothing (it does not set the done bit either). No heap.
Why: after the 15 s wait times out hostByName returns, and a late lwIP callback then wrote
the freed stack slot of the caller (stack corruption).
Remove when arduino-esp32 stops passing the caller's stack to dns_gethostbyname().
Concurrency limit (deliberately not widened): s_dns_id/s_dns_addr are single global slots.
Stock hostByName() serialises callers with WIFI_DNS_IDLE_BIT (waitStatusBits(IDLE, 16000),
then clearStatusBits(IDLE | DONE) before dns_gethostbyname, setStatusBits(IDLE) at the end),
so ++s_dns_id and s_dns_addr are written by one caller at a time. That wait does not claim
the bit atomically: two tasks can both pass it before either clears it (a window of one task
switch); then the earlier caller's callback carries an outdated id, is ignored, and that caller
waits its 15 s and gets no address (it fails, it does not corrupt). An atomic claim
(xEventGroupWaitBits with clear-on-exit) would change a stock line outside the anchor and could
not be re-checked against the 2.0.17 text here, so it is not done. Callers today: the
McTlsClient update task and instrument_probe, which never resolve concurrently. A third
caller that may resolve in parallel must serialise itself (mutex) before hostByName().

Out of scope here: DNS outside tcpip_thread (review row 3) is handled in our own code.

No-op where the framework is absent or not Arduino/espressif32 (nRF52, native). Safeboot envs
set their own extra_scripts and do not load this script. The text transforms are importable
without SCons.
"""

import json
import re
import sys
from pathlib import Path

PACKAGE = "framework-arduinoespressif32"

HTTP_MAX_LINE = 2048
HTTP_HEADER_DEADLINE_MS = 30000


def _apply(text: str, marker: str, pairs, pid: str, where: str) -> str:
    """Apply every (original, patched) pair once; unchanged if marker present; RuntimeError if unknown."""
    if marker in text:
        return text
    for original, _ in pairs:
        if text.count(original) != 1:
            raise RuntimeError(
                f"arduino_esp32_patch {pid}: {where} not found in the expected form; "
                "arduino-esp32 changed, review the patch"
            )
    for original, patched in pairs:
        text = text.replace(original, patched)
    return text


# --- AE-01: HTTPClient header lines ---

HTTP_MARKER = "/* MESHCOM-PATCH AE-01 */"
HTTP_REL = "libraries/HTTPClient/src/HTTPClient.cpp"

HTTP_ORIGINAL = """    while(connected()) {
        size_t len = _client->available();
        if(len > 0) {
            String headerLine = _client->readStringUntil('\\n');
            headerLine.trim(); // remove \\r
"""

HTTP_PATCHED = (
    """    """
    + HTTP_MARKER
    + """
    const unsigned long headerStart = millis();
    while(connected()) {
        if((millis() - headerStart) > """
    + str(HTTP_HEADER_DEADLINE_MS)
    + """UL) {
            _canReuse = false;
            return HTTPC_ERROR_READ_TIMEOUT;
        }
        size_t len = _client->available();
        if(len > 0) {
            String headerLine;
            bool lineDone = false;
            while(!lineDone) {
                if((millis() - headerStart) > """
    + str(HTTP_HEADER_DEADLINE_MS)
    + """UL || (millis() - lastDataTime) > _tcpTimeout) {
                    _canReuse = false;
                    return HTTPC_ERROR_READ_TIMEOUT;
                }
                int c = _client->available() ? _client->read() : -1;
                if(c < 0) {
                    if(!connected()) {
                        _canReuse = false;
                        return HTTPC_ERROR_CONNECTION_LOST;
                    }
                    delay(1);
                    continue;
                }
                lastDataTime = millis();
                if(c == '\\n') {
                    lineDone = true;
                } else if(headerLine.length() >= """
    + str(HTTP_MAX_LINE)
    + """) {
                    _canReuse = false;
                    return HTTPC_ERROR_NO_HTTP_SERVER;
                } else {
                    headerLine += (char)c;
                }
            }
            headerLine.trim(); // remove \\r
"""
)

HTTP_PAIRS = ((HTTP_ORIGINAL, HTTP_PATCHED),)


def patch_http_text(text: str) -> str:
    """AE-01: return patched text; unchanged if already patched; RuntimeError if unknown."""
    return _apply(text, HTTP_MARKER, HTTP_PAIRS, "AE-01", "handleHeaderResponse() header loop")


# --- AE-02: WiFiUDP / WiFiClient allocation failure ---

AE02_MARKER = "/* MESHCOM-PATCH AE-02 */"
UDP_REL = "libraries/WiFi/src/WiFiUdp.cpp"
CLIENT_REL = "libraries/WiFi/src/WiFiClient.cpp"

UDP_ORIGINAL = """  if (len > 0) {
    rx_buffer = new(std::nothrow) cbuf(len);
    rx_buffer->write(buf, len);
  }
  free(buf);
  return len;"""

UDP_PATCHED = (
    """  if (len > 0) {
    """
    + AE02_MARKER
    + """
    void *probe = malloc(len + 1);  // cbuf(len) does new char[len + 1], which aborts on failure
    if (!probe) {
      free(buf);
      return 0;
    }
    free(probe);
    rx_buffer = new(std::nothrow) cbuf(len);
    if (rx_buffer) {
      rx_buffer->write(buf, len);
    } else {
      free(buf);
      return 0;
    }
  }
  free(buf);
  return len;"""
)

UDP_PAIRS = ((UDP_ORIGINAL, UDP_PATCHED),)

CLIENT_CTOR_ORIGINAL = """    clientSocketHandle.reset(new WiFiClientSocketHandle(fd));
    _rxBuffer.reset(new WiFiClientRxBuffer(fd));
"""

CLIENT_CTOR_PATCHED = (
    """    """
    + AE02_MARKER
    + """
    WiFiClientSocketHandle *h = new(std::nothrow) WiFiClientSocketHandle(fd);
    if(!h) {
        close(fd);
        _connected = false;
        return;
    }
    clientSocketHandle.reset(h);
    WiFiClientRxBuffer *r = new(std::nothrow) WiFiClientRxBuffer(fd);
    if(!r) {
        clientSocketHandle = NULL;  // closes fd
        _connected = false;
        return;
    }
    _rxBuffer.reset(r);
"""
)

CLIENT_CONNECT_ORIGINAL = """    clientSocketHandle.reset(new WiFiClientSocketHandle(sockfd));
    _rxBuffer.reset(new WiFiClientRxBuffer(sockfd));
"""

CLIENT_CONNECT_PATCHED = """    WiFiClientSocketHandle *h = new(std::nothrow) WiFiClientSocketHandle(sockfd);
    if(!h) {
        close(sockfd);
        return 0;
    }
    clientSocketHandle.reset(h);
    WiFiClientRxBuffer *r = new(std::nothrow) WiFiClientRxBuffer(sockfd);
    if(!r) {
        clientSocketHandle = NULL;  // closes sockfd
        return 0;
    }
    _rxBuffer.reset(r);
"""

CLIENT_PAIRS = (
    (CLIENT_CTOR_ORIGINAL, CLIENT_CTOR_PATCHED),
    (CLIENT_CONNECT_ORIGINAL, CLIENT_CONNECT_PATCHED),
)


def patch_udp_text(text: str) -> str:
    """AE-02: return patched text; unchanged if already patched; RuntimeError if unknown."""
    return _apply(text, AE02_MARKER, UDP_PAIRS, "AE-02", "WiFiUDP::parsePacket() rx_buffer")


def patch_client_text(text: str) -> str:
    """AE-02: return patched text; unchanged if already patched; RuntimeError if unknown."""
    return _apply(
        text, AE02_MARKER, CLIENT_PAIRS, "AE-02", "WiFiClient socket handle / rx buffer allocation"
    )


# --- AE-03: hostByName callback must not write the caller's stack ---

DNS_MARKER = "/* MESHCOM-PATCH AE-03 */"
DNS_REL = "libraries/WiFi/src/WiFiGeneric.cpp"

DNS_CB_ORIGINAL = """static void wifi_dns_found_callback(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    if(ipaddr) {
        (*reinterpret_cast<IPAddress*>(callback_arg)) = ipaddr->u_addr.ip4.addr;
    }
    xEventGroupSetBits(_arduino_event_group, WIFI_DNS_DONE_BIT);
}
"""

DNS_CB_PATCHED = (
    """"""
    + DNS_MARKER
    + """
static volatile uint32_t s_dns_id = 0;    // id of the request hostByName() waits for
static volatile uint32_t s_dns_addr = 0;  // its result (0 = none)

static void wifi_dns_found_callback(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    // A callback that outlives its request (caller timed out) carries an old id: ignore it.
    if((uint32_t)(uintptr_t)callback_arg != s_dns_id) {
        return;
    }
    if(ipaddr) {
        s_dns_addr = ipaddr->u_addr.ip4.addr;
    }
    xEventGroupSetBits(_arduino_event_group, WIFI_DNS_DONE_BIT);
}
"""
)

DNS_CALL_ORIGINAL = """        err_t err = dns_gethostbyname(aHostname, &addr, &wifi_dns_found_callback, &aResult);
        if(err == ERR_OK && addr.u_addr.ip4.addr) {
            aResult = addr.u_addr.ip4.addr;
        } else if(err == ERR_INPROGRESS) {
            waitStatusBits(WIFI_DNS_DONE_BIT, 15000);  //real internal timeout in lwip library is 14[s]
            clearStatusBits(WIFI_DNS_DONE_BIT);
        }
"""

DNS_CALL_PATCHED = """        s_dns_addr = 0;
        uint32_t dns_id = ++s_dns_id;
        err_t err = dns_gethostbyname(aHostname, &addr, &wifi_dns_found_callback, (void *)(uintptr_t)dns_id);
        if(err == ERR_OK && addr.u_addr.ip4.addr) {
            aResult = addr.u_addr.ip4.addr;
        } else if(err == ERR_INPROGRESS) {
            waitStatusBits(WIFI_DNS_DONE_BIT, 15000);  //real internal timeout in lwip library is 14[s]
            clearStatusBits(WIFI_DNS_DONE_BIT);
            if(s_dns_addr) {
                aResult = (uint32_t)s_dns_addr;
            }
        }
"""

DNS_PAIRS = (
    (DNS_CB_ORIGINAL, DNS_CB_PATCHED),
    (DNS_CALL_ORIGINAL, DNS_CALL_PATCHED),
)


def patch_dns_text(text: str) -> str:
    """AE-03: return patched text; unchanged if already patched; RuntimeError if unknown."""
    return _apply(text, DNS_MARKER, DNS_PAIRS, "AE-03", "wifi_dns_found_callback()/hostByName()")


def _patch_path(path: Path, transform) -> bool:
    if not path.is_file():
        return False
    text = path.read_text()
    out = transform(text)
    if out == text:
        return False
    path.write_text(out)
    return True


# (id, path relative to the framework dir, text transform)
PATCHES = (
    ("AE-01", HTTP_REL, patch_http_text),
    ("AE-02", UDP_REL, patch_udp_text),
    ("AE-02", CLIENT_REL, patch_client_text),
    ("AE-03", DNS_REL, patch_dns_text),
)


def patch_framework(root: Path) -> list:
    """Patch every target under root; return the list of (id, path) written.

    A missing target file is an error: the framework layout moved, never build unpatched.
    """
    done = []
    for pid, rel, transform in PATCHES:
        path = root / rel
        if not path.is_file():
            raise RuntimeError(f"arduino_esp32_patch {pid}: {path} is missing")
        if _patch_path(path, transform):
            done.append((pid, path))
    return done


def markers_present(root: Path) -> list:
    """Return "<id>@<file>" for every patch target whose marker is in the file (build-log evidence)."""
    out = []
    for pid, rel, _transform in PATCHES:
        path = root / rel
        if path.is_file() and f"/* MESHCOM-PATCH {pid} */" in path.read_text(errors="replace"):
            out.append(f"{pid}@{Path(rel).name}")
    return out


def framework_version(root) -> str:
    """package.json "version" of the framework dir; "" if absent or unreadable."""
    # Same 4-line reader as tools/lwip_wrap.py: SCons pre-scripts run without __file__, so no shared import.
    try:
        return str(json.loads((Path(root) / "package.json").read_text()).get("version", ""))
    except (OSError, ValueError, TypeError, AttributeError):
        return ""


def is_anchor_version(version: str) -> bool:
    """True only for arduino-esp32 2.0.17 ("3.20017.<date>+sha...") the anchors were taken from."""
    return re.fullmatch(r"3\.20017\.[^+]*(\+.*)?", str(version)) is not None


def patch_if_supported(root: Path) -> list | None:
    """patch_framework(root) on the 2.0.17 line; otherwise warn loudly, touch nothing, return None."""
    version = framework_version(root)
    if not is_anchor_version(version):
        print(
            f"arduino_esp32_patch: AE patches NOT applied: framework {version or 'unknown'}",
            file=sys.stderr,
        )
        return None
    return patch_framework(root)


try:
    Import("env")  # noqa: F821  (SCons builtin)
except NameError:
    env = None


def wants_arduino_esp32(platform_name, frameworks) -> bool:
    """True for an espressif32 platform env that uses the Arduino framework."""
    if platform_name != "espressif32":
        return False
    if isinstance(frameworks, str):
        frameworks = [f.strip() for f in frameworks.replace(",", "\n").splitlines()]
    return "arduino" in [str(f).strip().lower() for f in frameworks]


if env is not None:
    _platform = env.PioPlatform()
    if wants_arduino_esp32(_platform.name, env.get("PIOFRAMEWORK", [])):
        _root = _platform.get_package_dir(PACKAGE)
        if _root:
            _done = patch_if_supported(Path(_root))
            for _id, _p in _done or []:
                print(f"arduino_esp32_patch: {_id} applied to", _p, file=sys.stderr)
            if _done is not None:  # positive evidence in every build log, also when already patched
                print(
                    "arduino_esp32_patch: present:",
                    " ".join(markers_present(Path(_root))),
                    file=sys.stderr,
                )
