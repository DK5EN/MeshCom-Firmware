"""ota_lowheap_trigger.py: heap and uptime from --info on plain images (no --heaplog)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import ota_lowheap_trigger as t  # noqa: E402


def test_info_heap_reads_free_min_blk_and_uptime():
    lines = ["2026-11-24 15:34:02 ...TIME 236881 ms",
             "2026-11-24 15:34:02 ...HEAP         : free 103120 min 89744 blk 90100"]
    assert t.info_heap(lines) == {"up": "236", "free": "103120", "blk": "90100", "min": "89744"}


def test_info_heap_without_lines_is_unknown():
    assert t.info_heap(["...wrong command --heaplog 1"]) == {"up": "?", "free": "?", "blk": "?", "min": "?"}


def test_unstage_is_allowed_apply_is_not():
    assert "--update unstage" in t.ALLOWED
    assert not any("apply" in c for c in t.ALLOWED)
