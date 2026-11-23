"""PlatformIO pre-script: patch the RAK13800-W5100S library in place (ETH-06, ETH-07).

Patches apply in this order on socket.cpp: ETH-06, then ETH-07. Their anchors do not
overlap (ETH-06 sits inside the socketSendUDP() body, ETH-07 right after its opening
brace), so either order gives the same text.

ETH-06 -- socketSendUDP() wait
What: the wait for SEND_OK / TIMEOUT after Sock_SEND gets a 3 s deadline. When neither
flag comes, the interrupt bits are cleared and the send fails (returns false), exactly
like the library's own TIMEOUT branch.
Why: the loop had no bound of its own. After a W5100S hardware reset the chip no longer
knows the socket, so a SEND on it raises neither flag and the loop never ends. On
DB0AIS-12 (RAK4631, 2026-11-14 05:43) this froze the main loop for 52 h: no LoRa TX, no
gateway keepalive, RX still running (docs/log-reports/2026-11-16-db0ais-14d/). The
firmware side (extudp_functions.cpp externUdpChipReset()) removes that trigger; this
bound turns any other stuck socket into a 3 s stall instead of a dead node. The chip's
own UDP timeout (ARP retries) measured 1.6 s on DB0AIS, so 3 s never cuts a live send.
Remove when RAK13800-W5100S bounds socketSendUDP() itself.

ETH-07 -- socket index bounds guards (upstream RAKWireless/RAK13800-W5100S ebea549, 1.0.3)
What: every socket*() entry point in socket.cpp that indexes state[] or the chip returns
early (neutral value) when s >= MAX_SOCK_NUM; EthernetUDP (beginPacket, endPacket, write,
parsePacket, read x2) and the single-byte EthernetClient::read() do the same on sockindex.
Why: socketBegin()/socketBeginMulticast() return MAX_SOCK_NUM when no socket is free (the
W5100S has 4 usable sockets, MAX_SOCK_NUM is 8). The callers forwarded that index unchecked:
out-of-bounds access on state[MAX_SOCK_NUM], corrupted registers and buffers of other
sockets, and a possible endless loop in execCmdSn() (while (readSnCR(s));) that ends in a
watchdog reset. The icssw-org fork pinned in platformio.ini predates the fix; the other
RAKWireless commits since are an SPI-driver refactor (1e2a3f1), a Zephyr ifdef and
metadata, not fixes, and are not ported. Guards are inserted right after the function's
opening brace (upstream places a few after local declarations; same effect).
https://github.com/RAKWireless/RAK13800-W5100S/commit/ebea549
Remove when the RAK13800-W5100S pin in platformio.ini carries ebea549.

Idempotent (marker, no write when patched); keeps the file's CRLF line endings; fails
the build if the library text changed; no-op where the library is absent (ESP32,
native). The text transform is importable without SCons.
"""

import sys
from pathlib import Path

MARKER = "/* MESHCOM-PATCH ETH-06 */"
REL = "RAK13800-W5100S/src/socket.cpp"
DEADLINE_MS = 3000

ORIGINAL = """\t/* +2008.01 bj */
\twhile ( (W5100.readSnIR(s) & SnIR::SEND_OK) != SnIR::SEND_OK ) {
\t\tif (W5100.readSnIR(s) & SnIR::TIMEOUT) {"""

PATCHED = (
    """\t/* +2008.01 bj */
\tuint32_t meshcom_send_t0 = millis(); """
    + MARKER
    + """
\twhile ( (W5100.readSnIR(s) & SnIR::SEND_OK) != SnIR::SEND_OK ) {
\t\tif ((uint32_t)(millis() - meshcom_send_t0) >= """
    + str(DEADLINE_MS)
    + """) { """
    + MARKER
    + """
\t\t\tW5100.writeSnIR(s, (SnIR::SEND_OK|SnIR::TIMEOUT));
\t\t\tSPI.endTransaction();
\t\t\treturn false;
\t\t}
\t\tif (W5100.readSnIR(s) & SnIR::TIMEOUT) {"""
)


def patch_text(text: str) -> str:
    """Return patched text; unchanged if already patched; RuntimeError if unknown."""
    if MARKER in text:
        return text
    eol = "\r\n" if "\r\n" in text else "\n"
    orig = ORIGINAL.replace("\n", eol)
    if text.count(orig) != 1:
        raise RuntimeError(
            "w5100s_patch ETH-06: socketSendUDP() wait loop not found in the expected form; "
            "RAK13800-W5100S changed, review the patch"
        )
    return text.replace(orig, PATCHED.replace("\n", eol))


MARKER07 = "/* MESHCOM-PATCH ETH-07 */"
_S = "if (s >= MAX_SOCK_NUM) return"
_I = "if (sockindex >= MAX_SOCK_NUM) return"

# file -> [(signature line, guard statement)]; the anchor is signature + EOL + "{".
ETH07 = {
    "socket.cpp": [
        ("uint8_t EthernetClass::socketStatus(uint8_t s)", _S + " SnSR::CLOSED;"),
        ("void EthernetClass::socketClose(uint8_t s)", _S + ";"),
        ("uint8_t EthernetClass::socketListen(uint8_t s)", _S + " 0;"),
        ("void EthernetClass::socketConnect(uint8_t s, uint8_t * addr, uint16_t port)", _S + ";"),
        ("void EthernetClass::socketDisconnect(uint8_t s)", _S + ";"),
        ("int EthernetClass::socketRecv(uint8_t s, uint8_t *buf, int16_t len)", _S + " 0;"),
        ("uint16_t EthernetClass::socketRecvAvailable(uint8_t s)", _S + " 0;"),
        ("uint8_t EthernetClass::socketPeek(uint8_t s)", _S + " 0;"),
        ("uint16_t EthernetClass::socketSend(uint8_t s, const uint8_t * buf, uint16_t len)", _S + " 0;"),
        ("uint16_t EthernetClass::socketSendAvailable(uint8_t s)", _S + " 0;"),
        (
            "uint16_t EthernetClass::socketBufferData(uint8_t s, uint16_t offset, const uint8_t* buf, uint16_t len)",
            _S + " 0;",
        ),
        ("bool EthernetClass::socketStartUDP(uint8_t s, uint8_t* addr, uint16_t port)", _S + " false;"),
        ("bool EthernetClass::socketSendUDP(uint8_t s)", _S + " false;"),
    ],
    "EthernetUdp.cpp": [
        ("int EthernetUDP::beginPacket(IPAddress ip, uint16_t port)", _I + " 0;"),
        ("int EthernetUDP::endPacket()", _I + " 0;"),
        ("size_t EthernetUDP::write(const uint8_t *buffer, size_t size)", _I + " 0;"),
        ("int EthernetUDP::parsePacket()", _I + " 0;"),
        ("int EthernetUDP::read()", _I + " -1;"),
        ("int EthernetUDP::read(unsigned char *buffer, size_t len)", _I + " -1;"),
    ],
    "EthernetClient.cpp": [
        ("int EthernetClient::read()", _I + " -1;"),
    ],
}


def eth07_edits(name: str, eol: str = "\n"):
    """[(ORIGINAL, PATCHED)] for one library file; ORIGINAL is signature + '{', PATCHED adds the guard."""
    out = []
    for sig, guard in ETH07[name]:
        orig = sig + eol + "{"
        out.append((orig, orig + eol + "\t" + guard + " " + MARKER07))
    return out


def patch_text07(name: str, text: str) -> str:
    """Apply ETH-07 to one library file (socket.cpp, EthernetUdp.cpp, EthernetClient.cpp).

    Per guard: already there -> skip; anchor found exactly once -> insert; else RuntimeError.
    """
    eol = "\r\n" if "\r\n" in text else "\n"
    for orig, patched in eth07_edits(name, eol):
        if patched in text:
            continue
        if text.count(orig) != 1:
            raise RuntimeError(
                f"w5100s_patch ETH-07: {name}: anchor not found exactly once ({orig.splitlines()[0]!r}); "
                "RAK13800-W5100S changed, review the patch"
            )
        text = text.replace(orig, patched)
    return text


def patch_file(path: Path, fn=None) -> bool:
    """Patch file in place with fn (default: ETH-06); True if written. Missing file is a no-op."""
    if not path.is_file():
        return False
    text = path.read_bytes().decode("utf-8")
    out = (fn or patch_text)(text)
    if out == text:
        return False
    path.write_bytes(out.encode("utf-8"))
    return True


def wants_w5100s(lib_deps) -> bool:
    """True if a lib_deps list (strings or one newline-joined string) names RAK13800-W5100S."""
    if isinstance(lib_deps, str):
        lib_deps = lib_deps.splitlines()
    return any("rak13800-w5100s" in str(d).lower() for d in lib_deps)


try:
    Import("env")  # noqa: F821  (SCons builtin)
except NameError:
    env = None


if env is not None:
    _p = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env["PIOENV"] / REL
    # Dependencies are installed before pre-scripts run, so an env that pulls the
    # library and has no patch target here means the layout moved: fail, never
    # build an unpatched image silently.
    if not _p.is_file() and wants_w5100s(env.GetProjectOption("lib_deps", [])):
        raise RuntimeError(f"w5100s_patch ETH-06: RAK13800-W5100S in lib_deps but {_p} is missing")
    if patch_file(_p):
        print("w5100s_patch: ETH-06 applied to", _p, file=sys.stderr)
    for _name in ETH07:
        _q = _p.parent / _name
        if _p.is_file() and not _q.is_file():
            raise RuntimeError(f"w5100s_patch ETH-07: {_q} is missing next to {_p}")
        if _q.is_file() and patch_file(_q, lambda t, n=_name: patch_text07(n, t)):
            print("w5100s_patch: ETH-07 applied to", _q, file=sys.stderr)
