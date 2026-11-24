"""AU-28: HTTPClient::handleHeaderResponse() after tools/arduino_esp32_patch.py (AE-01), on real wire bytes.

The text tests in test_arduino_esp32_patch.py cannot see a behavioural bug: AE-01 v1 passed all of
them while it failed every Auto Update download (github.com answers the release asset with a 302
whose Content-Security-Policy line is 4204 bytes; v1 refused lines >= 2048 bytes, "conn -7").
Here the vendored stock function (au_http_harness/hhr_stock.inc) is run through patch_http_text(),
compiled on the host with the system C++ compiler against String / client stubs, and fed the
captured response heads in fixtures/au_http/ (sig=, jwt= values and request ids redacted with
same-length runs, so every line keeps its measured length) plus generated hostile heads.
Skipped only when no C++ compiler exists; the skip names the reason.
"""

import importlib.util
import re
import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
FIXTURES = HERE / "fixtures" / "au_http"
HARNESS = HERE / "au_http_harness"


def _load():
    spec = importlib.util.spec_from_file_location(
        "arduino_esp32_patch", TOOLS / "arduino_esp32_patch.py"
    )
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


AP = _load()
STOCK = (HARNESS / "hhr_stock.inc").read_text()
CXX = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")

pytestmark = pytest.mark.skipif(
    CXX is None,
    reason="NO C++ COMPILER (c++/clang++/g++) on PATH: the AU-28 header-loop behaviour "
    "tests (T1/T2) did NOT run; install a compiler, this is a hole in the gate",
)

RESULT = re.compile(r"^(\S+) code=(-?\d+) size=(-?\d+) loclen=(\d+) consumed=(\d+)/(\d+)$")


def _variants() -> dict:
    assert STOCK.count(AP.HTTP_ORIGINAL) == 1  # the vendored stock text carries the anchor
    return {
        "stock": STOCK,
        "v1": STOCK.replace(AP.HTTP_ORIGINAL, AP.HTTP_PATCHED_V1),
        "v2": AP.patch_http_text(STOCK),
    }


@pytest.fixture(scope="module")
def sims(tmp_path_factory):
    out = tmp_path_factory.mktemp("au_http")
    bins = {}
    for name, text in _variants().items():
        inc = out / f"hhr_{name}.inc"
        inc.write_text(text)
        exe = out / f"sim_{name}"
        subprocess.run(
            [
                CXX,
                "-std=c++17",
                "-O1",
                f'-DVARIANT="{inc}"',
                "-o",
                str(exe),
                str(HARNESS / "harness.cpp"),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        bins[name] = exe
    return bins


def _run(exe: Path, head: bytes, tmp: Path, ms_per_byte: int = 0) -> dict:
    f = tmp / "head.hdr"
    f.write_bytes(head)
    args = [str(exe)] + (["-t", str(ms_per_byte)] if ms_per_byte else []) + [str(f)]
    line = subprocess.run(
        args, check=True, capture_output=True, text=True, timeout=60
    ).stdout.strip()
    m = RESULT.match(line)
    assert m, line
    return {
        "code": int(m[2]),
        "size": int(m[3]),
        "loclen": int(m[4]),
        "consumed": int(m[5]),
        "total": int(m[6]),
    }


def _fixture(name: str) -> bytes:
    return (FIXTURES / name).read_bytes()


def _location_len(head: bytes) -> int:
    m = re.search(rb"(?im)^Location:[ ]*([^\r\n]*)\r\n", head)
    return len(m[1].strip()) if m else 0


def _head(status: bytes = b"HTTP/1.1 200 OK", *lines: bytes) -> bytes:
    return b"\r\n".join([status, *lines]) + b"\r\n\r\n"


def _long(name: bytes, n: int) -> bytes:
    return name + b": " + b"a" * (n - len(name) - 2)


# --- T1: the captured AU responses ---


@pytest.mark.parametrize("variant", ["stock", "v2"])
def test_captured_responses_parse(sims, tmp_path, variant):
    r = _run(sims[variant], _fixture("github_302.hdr"), tmp_path)
    assert r["code"] == 302
    assert r["loclen"] == _location_len(_fixture("github_302.hdr")) > 900  # full Location kept
    assert max(len(ln) for ln in _fixture("github_302.hdr").split(b"\r\n")) > 4096 - 100
    assert _run(sims[variant], _fixture("api_latest.hdr"), tmp_path)["code"] == 200
    r = _run(sims[variant], _fixture("release_assets_206.hdr"), tmp_path)
    assert (r["code"], r["size"]) == (206, 100)


@pytest.mark.parametrize("variant", ["stock", "v2"])
def test_captured_head_consumed_to_the_blank_line(sims, tmp_path, variant):
    """The over-long line is drained, not left in the stream (the body starts right after)."""
    head = _fixture("github_302.hdr")
    r = _run(sims[variant], head, tmp_path)
    assert r["consumed"] == len(head)


def test_frozen_v1_reproduces_au28(sims, tmp_path):
    """The bug itself: v1 turns the real github.com 302 into -7 (stays true, v1 is frozen text)."""
    assert _run(sims["v1"], _fixture("github_302.hdr"), tmp_path)["code"] == -7


# --- T2: hostile and odd heads (v2) ---


def test_unused_long_line_is_dropped_and_stream_stays_in_sync(sims, tmp_path):
    head = _head(
        b"HTTP/1.1 302 Found",
        _long(b"X-Unused", 4300),
        b"Location: https://example.invalid/a",
        _long(b"X-Unused2", 9000),
        b"Content-Length: 7",
    )
    r = _run(sims["v2"], head, tmp_path)
    assert (r["code"], r["size"], r["loclen"]) == (302, 7, len("https://example.invalid/a"))
    assert r["consumed"] == len(head)


def test_line_at_the_cap_is_stored_whole(sims, tmp_path):
    # the stored line includes the CR, so the cap fits MAX_LINE - 1 payload bytes
    loc = b"Location: " + b"l" * (AP.HTTP_MAX_LINE - 1 - len("Location: "))
    r = _run(sims["v2"], _head(b"HTTP/1.1 302 Found", loc), tmp_path)
    assert (r["code"], r["loclen"]) == (302, AP.HTTP_MAX_LINE - 1 - len("Location: "))
    over = b"Location: " + b"l" * (AP.HTTP_MAX_LINE - len("Location: "))
    assert _run(sims["v2"], _head(b"HTTP/1.1 302 Found", over), tmp_path)["code"] == -8


@pytest.mark.parametrize(
    "name",
    [
        b"Location",
        b"Content-Length",
        b"Transfer-Encoding",
        b"Content-Encoding",  # a key in _currentHeaders (the AU asks for it)
        b"Connection",
        b"location",
    ],
)
def test_over_long_used_header_fails_too_less_ram(sims, tmp_path, name):
    head = _head(b"HTTP/1.1 302 Found", _long(name, AP.HTTP_MAX_LINE + 904))
    assert _run(sims["v2"], head, tmp_path)["code"] == -8


def test_over_long_status_line_and_nameless_line_fail(sims, tmp_path):
    assert _run(sims["v2"], _head(b"HTTP/1.1 200 " + b"x" * 5000), tmp_path)["code"] == -8
    nameless = _head(b"HTTP/1.1 200 OK", b"x" * 5000)
    assert _run(sims["v2"], nameless, tmp_path)["code"] == -8


def test_header_budget_fails(sims, tmp_path):
    head = _head(b"HTTP/1.1 200 OK", *[_long(b"X-Pad%d" % i, 2000) for i in range(20)])
    assert len(head) > AP.HTTP_MAX_HEADER
    assert _run(sims["v2"], head, tmp_path)["code"] == -8


def test_trickling_server_hits_the_deadline(sims, tmp_path):
    head = _head(b"HTTP/1.1 200 OK", b"X-A: " + b"a" * 100)
    # 1 s per byte stays under the 15 s idle timeout, so only the 30 s deadline can fire
    assert _run(sims["v2"], head, tmp_path, ms_per_byte=1000)["code"] == -11
    assert _run(sims["v2"], head, tmp_path)["code"] == 200


def test_unbounded_unused_line_is_still_bounded_by_the_budget(sims, tmp_path):
    r = _run(sims["v2"], _head(b"HTTP/1.1 200 OK", _long(b"X-Huge", 60000)), tmp_path)
    assert r["code"] == -8
    assert r["consumed"] <= AP.HTTP_MAX_HEADER + 1  # stops reading at the budget
