"""AU-19/AU-20: tools/nimble_patch.py text transforms (ble_hs_timer_reset, ble_hs_flow_startup)."""

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


# --- AU-20: ble_hs_flow_startup() deinit before the second callout_init ---

FLOW_ORIGINAL = """    ble_transport_register_put_acl_from_ll_cb(ble_hs_flow_acl_free);
#endif
    ble_npl_callout_init(&ble_hs_flow_timer, ble_hs_evq_get(),
                         ble_hs_flow_event_cb, NULL);
#endif

    return 0;
}

void
ble_hs_flow_stop(void)
{
#if MYNEWT_VAL(BLE_HS_FLOW_CTRL)
    ble_npl_callout_deinit(&ble_hs_flow_timer);
#endif
}

void
ble_hs_flow_init(void)
{
#if MYNEWT_VAL(BLE_HS_FLOW_CTRL)
    ble_npl_event_init(&ble_hs_flow_ev, ble_hs_flow_event_cb, NULL);
    ble_npl_callout_init(&ble_hs_flow_timer, ble_hs_evq_get(),
                         ble_hs_flow_event_cb, NULL);
#endif //MYNEWT_VAL(BLE_HS_FLOW_CTRL)
}
"""


def test_au20_original_is_patched():
    np = _load()
    out = np.patch_flow_text(FLOW_ORIGINAL)
    assert out.count(np.FLOW_MARKER) == 1
    assert np.FLOW_MARKER == "/* MESHCOM-PATCH AU-20 */"
    startup = out[: out.index("ble_hs_flow_stop")]
    deinit = startup.index("ble_npl_callout_deinit(&ble_hs_flow_timer);")
    init = startup.index("ble_npl_callout_init(&ble_hs_flow_timer")
    assert deinit < init
    assert out.count("ble_npl_callout_init(&ble_hs_flow_timer") == 2


def test_au20_flow_init_untouched():
    np = _load()
    out = np.patch_flow_text(FLOW_ORIGINAL)
    tail = out[out.index("ble_hs_flow_init(void)") :]
    assert tail == FLOW_ORIGINAL[FLOW_ORIGINAL.index("ble_hs_flow_init(void)") :]


def test_au20_idempotent():
    np = _load()
    once = np.patch_flow_text(FLOW_ORIGINAL)
    assert np.patch_flow_text(once) == once


def test_au20_unknown_text_raises():
    np = _load()
    with pytest.raises(RuntimeError, match="AU-20"):
        np.patch_flow_text("int ble_hs_flow_startup(void) { return 0; }\n")


def test_au20_original_twice_raises():
    np = _load()
    with pytest.raises(RuntimeError, match="AU-20"):
        np.patch_flow_text(FLOW_ORIGINAL + FLOW_ORIGINAL)


def test_au20_file_ops(tmp_path):
    np = _load()
    assert np.patch_flow_file(tmp_path / "missing.c") is False
    f = tmp_path / "ble_hs_flow.c"
    f.write_text(FLOW_ORIGINAL)
    assert np.patch_flow_file(f) is True
    assert np.patch_flow_file(f) is False
    assert np.FLOW_MARKER in f.read_text()


def test_au20_rel_path():
    np = _load()
    assert np.FLOW_REL == "NimBLE-Arduino/src/nimble/nimble/host/src/ble_hs_flow.c"


def test_au20_real_library_file():
    root = TOOLS.parent
    files = sorted(
        root.glob(".pio/libdeps/*/NimBLE-Arduino/src/nimble/nimble/host/src/ble_hs_flow.c")
    )
    if not files:
        pytest.skip("NimBLE-Arduino not installed")
    np = _load()
    deinit = "callout_deinit(&ble_hs_flow_timer)"
    for f in files:
        text = f.read_text()
        # every installed copy is either the original or exactly our patch of it
        orig = text.replace(np.FLOW_PATCHED, np.FLOW_ORIGINAL) if np.FLOW_MARKER in text else text
        out = np.patch_flow_text(orig)
        assert out == text or np.FLOW_MARKER not in text, f
        assert np.patch_flow_text(out) == out, f
        # the one added deinit sits inside ble_hs_flow_startup(), the rest of the file is untouched
        assert out.count(deinit) == orig.count(deinit) + 1, f
        start = out.index("ble_hs_flow_startup(void)")
        stop = out.index("ble_hs_flow_stop(void)")
        assert start < out.index(np.FLOW_MARKER) < stop, f
        assert out[stop:] == orig[orig.index("ble_hs_flow_stop(void)") :], f
