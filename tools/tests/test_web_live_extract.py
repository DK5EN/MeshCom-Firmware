"""FORMAL-02 web differential: the committed live entry of the request rig is current.

test/test_web_diff/live/extract_live.py cuts the production request path out of the working tree. A function
edit without a regeneration leaves web_request_live.cpp / url_decode_live.cpp stale; --check regenerates in
memory and compares with the committed files.
"""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "test" / "test_web_diff" / "live" / "extract_live.py"


def test_live_entry_is_current():
    r = subprocess.run(
        [sys.executable, str(SCRIPT.relative_to(ROOT)), "--check"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=120,
    )
    assert r.returncode == 0, "live entry stale or generator broken:\n" + r.stdout + r.stderr
