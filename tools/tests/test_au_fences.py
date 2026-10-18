"""Source pins for the Auto Update D14 fences (docs/campaign-au-webgui-ux-20261018.md, 5.3)."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "src/esp32/esp32_main.cpp"


def test_fw_update_h_has_error_fence():
    s = (ROOT / "src/fw_update.h").read_text()
    assert re.search(r"#\s*if defined\(AU_AUTO_INSTALL\) && \(AU_AUTO_INSTALL \+ 0\) != 0", s)
    assert '#error "AU_AUTO_INSTALL must stay 0' in s
    assert re.search(r"#\s*define\s+AU_AUTO_INSTALL\s+0", s)


def _fenced(lines, needle):
    """True when every line containing needle sits inside an open `#if AU_AUTO_INSTALL`; needs >= 1 hit."""
    stack, hits = [], 0
    for line in lines:
        m = re.match(r"\s*#\s*(if|ifdef|ifndef)\b(.*)", line)
        if m:
            stack.append(m.group(1) == "if" and m.group(2).strip().startswith("AU_AUTO_INSTALL"))
        elif re.match(r"\s*#\s*(else|elif)\b", line) and stack:
            stack[-1] = False
        elif re.match(r"\s*#\s*endif", line) and stack:
            stack.pop()
        elif needle in line and not line.lstrip().startswith("//"):
            hits += 1
            if not any(stack):
                return False
    return hits > 0


def test_automatic_handover_branch_is_fenced():
    assert _fenced(MAIN.read_text().splitlines(), "act == FW_HANDOVER") or _fenced(
        MAIN.read_text().splitlines(), "act==FW_HANDOVER"
    )


def test_automatic_download_start_is_fenced():
    assert _fenced(MAIN.read_text().splitlines(), "fwNetStart(FWJ_DOWNLOAD")


@pytest.mark.skipif(shutil.which("g++") is None, reason="needs a host compiler")
@pytest.mark.parametrize("flag,ok", [("-DAU_AUTO_INSTALL=0", True), ("-DAU_AUTO_INSTALL=1", False), ("-DAU_AUTO_INSTALL", False)])
def test_flag_cannot_enable_auto_install(flag, ok):
    r = subprocess.run(
        ["g++", "-std=gnu++17", "-fsyntax-only", "-DNATIVE_BUILD=1", "-I", str(ROOT / "src"), flag,
         "-x", "c++", str(ROOT / "src/fw_update.h")],
        capture_output=True, text=True,
    )
    assert (r.returncode == 0) == ok, r.stderr[-400:]
