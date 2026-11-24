"""AE-01..AE-03: tools/arduino_esp32_patch.py text transforms (arduino-esp32 2.0.17)."""

import importlib.util
import json
from pathlib import Path

import pytest

TOOLS = Path(__file__).resolve().parents[1]
FIXTURES = Path(__file__).resolve().parent / "fixtures" / "au_http"
FRAMEWORK = Path.home() / ".platformio/packages/framework-arduinoespressif32"


def _load():
    spec = importlib.util.spec_from_file_location(
        "arduino_esp32_patch", TOOLS / "arduino_esp32_patch.py"
    )
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


AP = _load()


def _synthetic(pairs):
    """A file made only of the anchors (in order), separated by filler."""
    return "// head\n" + "\n// mid\n".join(o for o, _ in pairs) + "\n// tail\n"


# (id, transform, anchor pairs)
# patch id -> marker text inside the patched file (AE-01 is versioned, AU-28)
MARKER = {
    "AE-01": AP.HTTP_MARKER,
    "AE-02": "/* MESHCOM-PATCH AE-02 */",
    "AE-03": "/* MESHCOM-PATCH AE-03 */",
}

CASES = [
    ("AE-01", AP.patch_http_text, AP.HTTP_PAIRS),
    ("AE-02", AP.patch_udp_text, AP.UDP_PAIRS),
    ("AE-02", AP.patch_client_text, AP.CLIENT_PAIRS),
    ("AE-03", AP.patch_dns_text, AP.DNS_PAIRS),
]
IDS = ["AE-01-http", "AE-02-udp", "AE-02-client", "AE-03-dns"]


@pytest.mark.parametrize("pid,fn,pairs", CASES, ids=IDS)
def test_applies_once_and_is_idempotent(pid, fn, pairs):
    src = _synthetic(pairs)
    out = fn(src)
    assert out != src
    assert MARKER[pid] in out
    for orig, _ in pairs:
        assert orig not in out
    assert fn(out) == out


@pytest.mark.parametrize("pid,fn,pairs", CASES, ids=IDS)
def test_changed_text_raises_naming_the_id(pid, fn, pairs):
    with pytest.raises(RuntimeError, match=pid):
        fn("// some other framework version\n")
    with pytest.raises(RuntimeError, match=pid):
        fn(_synthetic(pairs) + _synthetic(pairs))  # anchor twice


@pytest.mark.parametrize("pid,fn,pairs", CASES, ids=IDS)
def test_stale_or_edited_patch_raises_never_skips(pid, fn, pairs):
    """A marker without the exact current patched block must fail loud (AU-28)."""
    patched = fn(_synthetic(pairs))
    stale = patched.replace(pairs[0][1], pairs[0][1].replace("\n", "\n// edited\n", 1))
    assert stale != patched
    with pytest.raises(RuntimeError, match=pid):
        fn(stale)
    with pytest.raises(RuntimeError, match=pid):
        fn(patched + patched)  # block twice: not the current single patch


def _fixture_headers(name):
    raw = (FIXTURES / name).read_bytes().split(b"\r\n")
    return [ln for ln in raw if ln]


def _longest_used_header_len():
    """Longest Location / Content-Length line of the captured AU responses (fixture-derived)."""
    used = (b"location:", b"content-length:", b"transfer-encoding:", b"connection:")
    return max(
        len(ln)
        for f in sorted(FIXTURES.glob("*.hdr"))
        for ln in _fixture_headers(f.name)
        if ln.lower().startswith(used)
    )


def test_http_behavior():
    out = AP.patch_http_text(_synthetic(AP.HTTP_PAIRS))
    assert "readStringUntil" not in out
    assert f"headerLine.length() < {AP.HTTP_MAX_LINE}" in out
    assert f"> {AP.HTTP_HEADER_DEADLINE_MS}UL" in out
    assert f"++headerBytes > {AP.HTTP_MAX_HEADER}" in out
    assert "HTTPC_ERROR_TOO_LESS_RAM" in out
    assert "HTTPC_ERROR_NO_HTTP_SERVER" not in out  # v2 never reports an over-long line as -7
    assert "HTTPC_ERROR_READ_TIMEOUT" in out
    assert AP.HTTP_HEADER_DEADLINE_MS > 5000
    # AU-28: the cap derives from the captured responses, not from a guess. A used header
    # (Location) may grow well past today's value, and the real 302 carries an unused line > 2 KiB.
    assert AP.HTTP_MAX_LINE >= 2 * _longest_used_header_len()
    longest = max(len(ln) for ln in _fixture_headers("github_302.hdr"))
    assert longest > 2048
    assert AP.HTTP_MAX_HEADER >= 4 * sum(len(ln) + 2 for ln in _fixture_headers("github_302.hdr"))


def test_http_v1_store_is_upgraded_to_v2():
    """T3: a store patched with the frozen v1 block ends byte-identical to stock -> v2."""
    stock = _synthetic(AP.HTTP_PAIRS)
    v1 = stock.replace(AP.HTTP_ORIGINAL, AP.HTTP_PATCHED_V1)
    assert AP.HTTP_MARKER_V1 in v1 and AP.HTTP_MARKER not in v1
    assert AP.patch_http_text(v1) == AP.patch_http_text(stock)
    assert "length() >= 2048" not in AP.patch_http_text(v1)


def test_http_v1_marker_with_edited_body_raises():
    """T4: the v1 marker with a body that is neither v1 nor v2 must not be skipped or guessed."""
    edited = AP.HTTP_PATCHED_V1.replace("2048", "8192")
    assert edited != AP.HTTP_PATCHED_V1
    with pytest.raises(RuntimeError, match="AE-01"):
        AP.patch_http_text("// head\n" + edited + "// tail\n")
    with pytest.raises(RuntimeError, match="AE-01"):
        AP.patch_http_text("// head\n" + AP.HTTP_MARKER_V1 + "\n// tail\n")


def test_udp_behavior():
    out = AP.patch_udp_text(_synthetic(AP.UDP_PAIRS))
    assert "malloc(len + 1)" in out  # probe before cbuf's throwing new char[]
    assert "if (rx_buffer)" in out
    assert "rx_buffer->write(buf, len);" in out


def test_client_behavior():
    out = AP.patch_client_text(_synthetic(AP.CLIENT_PAIRS))
    assert "new(std::nothrow) WiFiClientSocketHandle" in out
    assert "new(std::nothrow) WiFiClientRxBuffer" in out
    assert "reset(new WiFiClient" not in out
    assert out.count("close(fd);") == 1 and out.count("close(sockfd);") == 1
    assert "return 0;" in out


def test_dns_behavior():
    out = AP.patch_dns_text(_synthetic(AP.DNS_PAIRS))
    assert "reinterpret_cast<IPAddress*>" not in out
    assert "&aResult" not in out  # the caller's stack is no longer handed to lwIP
    assert "s_dns_id" in out and "s_dns_addr" in out
    assert "malloc" not in out and "new " not in out  # no heap involved


REAL = [
    ("AE-01", "libraries/HTTPClient/src/HTTPClient.cpp", AP.HTTP_PAIRS),
    ("AE-02", "libraries/WiFi/src/WiFiUdp.cpp", AP.UDP_PAIRS),
    ("AE-02", "libraries/WiFi/src/WiFiClient.cpp", AP.CLIENT_PAIRS),
    ("AE-03", "libraries/WiFi/src/WiFiGeneric.cpp", AP.DNS_PAIRS),
]


@pytest.mark.parametrize("pid,rel,pairs", REAL, ids=[r[1] for r in REAL])
def test_anchors_match_real_framework_once(pid, rel, pairs):
    path = FRAMEWORK / rel
    if not path.is_file():
        pytest.skip("framework-arduinoespressif32 not installed")
    version = AP.framework_version(FRAMEWORK)
    if not AP.is_anchor_version(version):
        pytest.skip(f"global package is framework {version or 'unknown'}, anchors are 2.0.17")
    text = path.read_text()
    stale_v1 = pid == "AE-01" and AP.HTTP_MARKER_V1 in text
    if MARKER[pid] in text or stale_v1:
        # T5 (AU-28): a patched store must hold the CURRENT block, never just a marker. A store
        # still on AE-01 v1 fails here until a build (the pre-script) upgrades it; that is the point.
        for _, patched in pairs:
            assert text.count(patched) == 1, (
                f"{rel}: patched with a stale {pid} block; run a build to upgrade the store"
            )
    else:
        for orig, _ in pairs:
            assert text.count(orig) == 1


def test_patch_list_and_paths():
    assert [p[0] for p in AP.PATCHES] == ["AE-01", "AE-02", "AE-02", "AE-03"]
    assert all(p[1].startswith("libraries/") for p in AP.PATCHES)
    assert AP.PACKAGE == "framework-arduinoespressif32"


@pytest.mark.parametrize(
    "version,expected",
    [
        ("3.20017.241212+sha.dcc1105b", True),
        ("3.20017.0", True),
        ("3.20014.231204", False),  # 2.0.14 (espressif32@6.5.0): anchors not verified
        ("3.20018.0", False),
        ("3.3.7+sha.b3b492ff", False),  # Tasmota (safeboot)
        ("3.200170.0", False),
        ("", False),
    ],
)
def test_is_anchor_version(version, expected):
    assert AP.is_anchor_version(version) is expected


def _fake_framework(root, version):
    (root / "package.json").write_text(json.dumps({"version": version}))
    for _pid, rel, _fn in AP.PATCHES:
        f = root / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        if not f.exists():
            f.write_text("// stock\n")
    return {rel: (root / rel).read_text() for _pid, rel, _fn in AP.PATCHES}


def test_patch_if_supported_skips_other_version_loudly(tmp_path, capsys):
    before = _fake_framework(tmp_path, "3.3.7+sha.b3b492ff")
    assert AP.patch_if_supported(tmp_path) is None  # no RuntimeError on unknown text
    assert capsys.readouterr().err.strip() == (
        "arduino_esp32_patch: AE patches NOT applied: framework 3.3.7+sha.b3b492ff"
    )
    assert {rel: (tmp_path / rel).read_text() for rel in before} == before


def test_patch_if_supported_warns_on_unreadable_version(tmp_path, capsys):
    assert AP.patch_if_supported(tmp_path) is None
    assert "AE patches NOT applied: framework unknown" in capsys.readouterr().err


def test_patch_if_supported_patches_the_anchor_version(tmp_path, capsys):
    _fake_framework(tmp_path, "3.20017.241212+sha.dcc1105b")
    for _pid, rel, _fn in AP.PATCHES:
        pairs = {
            AP.HTTP_REL: AP.HTTP_PAIRS,
            AP.UDP_REL: AP.UDP_PAIRS,
            AP.CLIENT_REL: AP.CLIENT_PAIRS,
            AP.DNS_REL: AP.DNS_PAIRS,
        }[rel]
        (tmp_path / rel).write_text(_synthetic(pairs))
    done = AP.patch_if_supported(tmp_path)
    assert [pid for pid, _ in done] == ["AE-01", "AE-02", "AE-02", "AE-03"]
    assert "NOT applied" not in capsys.readouterr().err
    assert AP.patch_if_supported(tmp_path) == []  # idempotent


def test_markers_present_lists_patched_targets(tmp_path):
    ae = _load()
    pid, rel, _ = ae.PATCHES[0]
    f = tmp_path / rel
    f.parent.mkdir(parents=True)
    f.write_text("x /* MESHCOM-PATCH %s */ y" % ae.HTTP_MARKER_ID)
    assert ae.markers_present(tmp_path) == [f"AE-01v2@{Path(rel).name}"]  # versioned id
    f.write_text("x /* MESHCOM-PATCH AE-01 */ y")  # a stale v1 store is not reported as patched
    assert ae.markers_present(tmp_path) == []
