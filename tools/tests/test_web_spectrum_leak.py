"""WEB-SPECTRUM-LEAK-01: every caller of sx126x_spectral_scan_freq() frees the new[] array it returns.

The spectrum page leaked one 33-entry array per sweep step (about 5 KB per page view). WEB_SRC_ROOT points the test
at a mutated copy of src/ (mutation proof).
"""
import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = Path(os.environ.get("WEB_SRC_ROOT", ROOT / "src"))


def test_spectral_scan_result_is_freed_by_every_caller():
    callers = 0
    for p in sorted(SRC.rglob("*.cpp")):
        if p.name == "spectral_scan.cpp":
            continue
        text = p.read_text(errors="replace")
        for m in re.finditer(r"(\w+)\s*=\s*sx126x_spectral_scan_freq\(", text):
            callers += 1
            var = m.group(1)
            tail = text[m.end():m.end() + 4000]
            assert re.search(rf"delete\s*\[\]\s*{var}\s*;", tail), f"{p.relative_to(SRC)}: {var} is never freed"
    assert callers >= 1


def test_spectral_scan_result_starts_zeroed():
    # The array is returned unchanged when setFrequency or spectralScanStart fails, and the spectrum page then
    # draws its bins: a plain new[] hands out uninitialised heap. Value-initialise it with ().
    text = (SRC / "spectral_scan.cpp").read_text(errors="replace")
    allocs = re.findall(r"new\s+uint16_t\s*\[\s*RADIOLIB_SX126X_SPECTRAL_SCAN_RES_SIZE\s*\]\s*(\(\s*\))?", text)
    assert allocs, "allocation of the scan result not found"
    assert all(a for a in allocs), "scan result array is not value-initialised (new uint16_t[...]())"
