"""AU-19: tools/nimble_patch.py text transform on ble_hs_timer_reset()."""

import importlib.util
from pathlib import Path

import pytest

TOOLS = Path(__file__).resolve().parents[1]
FIXTURE = TOOLS / "tests/fixtures/nimble_ble_hs_timer_reset.c"


def _load():
    spec = importlib.util.spec_from_file_location("nimble_patch", TOOLS / "nimble_patch.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _disabled_branch(text):
    start = text.index("if (!ble_hs_is_enabled())")
    return text[start : text.index("} else {", start)]


def test_original_is_patched():
    np = _load()
    src = FIXTURE.read_text()
    assert "ble_npl_callout_deinit(&ble_hs_timer)" in src
    out = np.patch_text(src)
    branch = _disabled_branch(out)
    assert np.MARKER in out
    assert "ble_npl_callout_deinit" not in branch
    assert "ble_npl_callout_stop(&ble_hs_timer);" in branch
    assert "ble_npl_eventq_remove" not in branch
    assert "ble_npl_callout_reset(&ble_hs_timer, ticks)" in out  # enabled branch kept


def test_patched_is_idempotent():
    np = _load()
    once = np.patch_text(FIXTURE.read_text())
    assert np.patch_text(once) == once


def test_unknown_text_raises():
    np = _load()
    with pytest.raises(RuntimeError):
        np.patch_text("static void ble_hs_timer_reset(uint32_t ticks) { }\n")


def test_original_twice_raises():
    np = _load()
    src = FIXTURE.read_text()
    with pytest.raises(RuntimeError):
        np.patch_text(src + src)


def test_rel_path_and_lib_deps_detection():
    np = _load()
    assert np.REL == "NimBLE-Arduino/src/nimble/nimble/host/src/ble_hs.c"
    assert np.wants_nimble(["h2zero/NimBLE-Arduino@2.2.3", "bblanchon/ArduinoJson"])
    assert np.wants_nimble("\nh2zero/NimBLE-Arduino@2.2.3\nRadioLib")
    assert not np.wants_nimble(["jgromes/RadioLib"])
