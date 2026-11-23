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


# --- NB-01..NB-03: backports from NimBLE-Arduino 2.3.0 .. 2.3.7 (exact 2.2.3 snippets) ---

ATT_ORIGINAL = """// Set the value of the attribute.
bool NimBLEAttValue::setValue(const uint8_t* value, uint16_t len) {
    m_attr_len = 0; // Just set the value length to 0 and append instead of repeating code.
    append(value, len);
    return memcmp(m_attr_value, value, len) == 0 && m_attr_len == len;
}

// Append the new data, allocate as necessary.
NimBLEAttValue& NimBLEAttValue::append(const uint8_t* value, uint16_t len) {
    if (len == 0) {
        return *this;
    }

    uint8_t* res     = m_attr_value;
    uint16_t new_len = m_attr_len + len;
    if (new_len > m_capacity) {
        res        = static_cast<uint8_t*>(realloc(m_attr_value, (new_len + 1)));
        m_capacity = new_len;
    }
    NIMBLE_CPP_DEBUG_ASSERT(res);
    if (res == nullptr) {
        NIMBLE_LOGE(LOG_TAG, "Failed to realloc append");
        return *this;
    }

    memcpy(res + m_attr_len, value, len);
}
"""

HCI_TEXT = """    do {
        m = ble_transport_alloc_acl_from_ll();

        if (!m) {
            ESP_LOGD(LOG_TAG,"Failed to allocate buffer, retrying ");
            /* Give some time to free buffer and try again */
            vTaskDelay(1);
	    }
    } while(!m);
"""

# (id, text, transform name, marker attribute, a substring the patch must add)
NB_CASES = [
    (
        "NB-01",
        ATT_ORIGINAL,
        "patch_set_text",
        "SET_MARKER",
        "if (m_attr_value != nullptr) m_attr_value[0] = '\\0';",
    ),
    (
        "NB-02",
        ATT_ORIGINAL,
        "patch_append_text",
        "APPEND_MARKER",
        "bool grow = new_len > m_capacity;",
    ),
    ("NB-03", HCI_TEXT, "patch_hci_text", "HCI_MARKER", "vTaskDelay(pdMS_TO_TICKS(10));"),
]


@pytest.mark.parametrize(("pid", "text", "fn", "marker", "added"), NB_CASES)
def test_nb_applies_once(pid, text, fn, marker, added):
    np = _load()
    m = getattr(np, marker)
    assert m == f"/* MESHCOM-PATCH {pid} */"
    out = getattr(np, fn)(text)
    assert out.count(m) == 1
    assert added in out
    assert out != text


@pytest.mark.parametrize(("pid", "text", "fn", "marker", "added"), NB_CASES)
def test_nb_idempotent(pid, text, fn, marker, added):
    np = _load()
    once = getattr(np, fn)(text)
    assert getattr(np, fn)(once) == once


@pytest.mark.parametrize(("pid", "text", "fn", "marker", "added"), NB_CASES)
def test_nb_changed_text_raises(pid, text, fn, marker, added):
    np = _load()
    with pytest.raises(RuntimeError, match=pid):
        getattr(np, fn)("int unrelated(void) { return 0; }\n")
    with pytest.raises(RuntimeError, match=pid):
        getattr(np, fn)(text + text)  # anchor twice: ambiguous, refuse


def test_nb02_capacity_set_only_after_realloc():
    np = _load()
    out = np.patch_append_text(ATT_ORIGINAL)
    assert out.index("realloc(") < out.index("m_capacity = new_len;")
    assert out.index("res == nullptr") < out.index("m_capacity = new_len;")


def test_nb01_and_nb02_compose_on_one_file():
    np = _load()
    out = np.patch_append_text(np.patch_set_text(ATT_ORIGINAL))
    assert np.SET_MARKER in out and np.APPEND_MARKER in out
    assert np.patch_set_text(np.patch_append_text(ATT_ORIGINAL)) == out


def test_nb_file_ops_and_table(tmp_path):
    np = _load()
    ids = [i for i, _, _ in np.PATCHES]
    assert ids[:5] == ["AU-19", "AU-20", "NB-01", "NB-02", "NB-03"]
    f = tmp_path / "NimBLEAttValue.cpp"
    assert np.patch_set_file(tmp_path / "missing.cpp") is False
    f.write_text(ATT_ORIGINAL)
    assert np.patch_set_file(f) is True
    assert np.patch_set_file(f) is False
    assert np.patch_append_file(f) is True
    assert np.patch_append_file(f) is False
    h = tmp_path / "esp_nimble_hci.c"
    h.write_text(HCI_TEXT)
    assert np.patch_hci_file(h) is True
    assert np.patch_hci_file(h) is False
    assert np.SET_REL == "NimBLE-Arduino/src/NimBLEAttValue.cpp"
    assert np.HCI_REL.endswith("esp_port/esp-hci/src/esp_nimble_hci.c")


def test_nb_real_library_files():
    root = TOOLS.parent
    np = _load()
    seen = 0
    for rel, fn, marker in (
        (np.SET_REL, np.patch_set_text, np.SET_MARKER),
        (np.APPEND_REL, np.patch_append_text, np.APPEND_MARKER),
        (np.HCI_REL, np.patch_hci_text, np.HCI_MARKER),
    ):
        for f in sorted(root.glob(f".pio/libdeps/*/{rel}")):
            text = f.read_text()
            # an installed copy is pristine (patches) or already patched (idempotent)
            assert fn(fn(text)) == fn(text), f
            assert marker in fn(text), f
            seen += 1
    if not seen:
        pytest.skip("NimBLE-Arduino not installed")


def test_nb01_no_unguarded_store_and_safe_compare():
    """FORMAL-01 NimBLE #6: the first form wrote m_attr_value[0] without a NULL check and ran memcmp on
    a failed append; the store must be guarded and memcmp short-circuited on length mismatch or len 0."""
    np = _load()
    out = np.patch_set_text(ATT_ORIGINAL)
    body = out[out.index("NimBLEAttValue::setValue(") : out.index("// Append the new data")]
    assert "    m_attr_value[0] = " not in body
    assert (
        "return m_attr_len == len && (len == 0 || memcmp(m_attr_value, value, len) == 0);" in body
    )


def test_nb01_upgrades_first_form():
    np = _load()
    v1 = ATT_ORIGINAL.replace(np.SET_ORIGINAL, np.SET_PATCHED_V1)
    assert v1 != ATT_ORIGINAL
    out = np.patch_set_text(v1)
    assert out == np.patch_set_text(ATT_ORIGINAL)
    assert out.count(np.SET_MARKER) == 1


# --- NB-04..NB-07: NimBLE lifecycle fixes (FORMAL-01 NimBLE review #2/#4/#3/#8, exact 2.2.3 snippets) ---

DEINIT_TEXT = """bool NimBLEDevice::deinit(bool clearAll) {
    int rc = 0;
    if (m_initialized) {
        rc = nimble_port_stop();
        if (rc == 0) {
            nimble_port_deinit();
            rc = esp_nimble_hci_and_controller_deinit();
            m_initialized = false;
            m_synced      = false;
        }
    }

    if (clearAll) {
# if defined(CONFIG_BT_NIMBLE_ROLE_PERIPHERAL)
        if (NimBLEDevice::m_pServer != nullptr) {
            delete NimBLEDevice::m_pServer;
            NimBLEDevice::m_pServer = nullptr;
        }
# endif
    }

    return rc == 0;
} // deinit
"""

INIT_TEXT = """bool NimBLEDevice::init(const std::string& deviceName) {
    if (!m_initialized) {
        err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
        if (err != ESP_OK) {
            NIMBLE_LOGE(LOG_TAG, "esp_bt_controller_enable() failed; err=%d", err);
            return false;
        }

        err = esp_nimble_hci_init();
        if (err != ESP_OK) {
            NIMBLE_LOGE(LOG_TAG, "esp_nimble_hci_init() failed; err=%d", err);
            return false;
        }
#  endif
# endif
        nimble_port_init();
        nimble_port_freertos_init(NimBLEDevice::host_task);
    }

    // Wait for host and controller to sync before returning and accepting new tasks
    while (!m_synced) {
        ble_npl_time_delay(1);
    }

    m_initialized = true; // Set the initialization flag to ensure we are only initialized once.
    return true;
} // init
"""

HOST_TASK_TEXT = """void NimBLEDevice::host_task(void* param) {
    NIMBLE_LOGI(LOG_TAG, "BLE Host Task Started");
    nimble_port_run(); // This function will return only when nimble_port_stop() is executed
    nimble_port_freertos_deinit();
} // host_task
"""

STOP_TEXT = """static void
ble_hs_stop_terminate_timeout_cb(struct ble_npl_event *ev)
{
    BLE_HS_LOG(ERROR, "ble_hs_stop_terminate_timeout_cb,"
                      "%d connection(s) still up \\n", ble_hs_stop_conn_cnt);

    /* TODO: Shall we send error here? */
    ble_hs_stop_done(0);
}
"""

NB2_CASES = [
    ("NB-04", DEINIT_TEXT, "patch_deinit_text", "DEINIT_MARKER", "if (clearAll && stopRc == 0)"),
    ("NB-05", INIT_TEXT, "patch_init_text", "INIT_MARKER", "NIMBLE_SYNC_TIMEOUT_MS"),
    ("NB-06", HOST_TASK_TEXT, "patch_host_task_text", "HOST_TASK_MARKER", "vTaskDelete(NULL);"),
    ("NB-07", STOP_TEXT, "patch_stop_text", "STOP_MARKER", "ble_hs_stop_conn_cnt = 0;"),
]


@pytest.mark.parametrize(("pid", "text", "fn", "marker", "added"), NB2_CASES)
def test_nb2_applies_once(pid, text, fn, marker, added):
    np = _load()
    m = getattr(np, marker)
    assert m == f"/* MESHCOM-PATCH {pid} */"
    out = getattr(np, fn)(text)
    assert out.count(m) == 1
    assert added in out
    assert out != text


@pytest.mark.parametrize(("pid", "text", "fn", "marker", "added"), NB2_CASES)
def test_nb2_idempotent(pid, text, fn, marker, added):
    np = _load()
    once = getattr(np, fn)(text)
    assert getattr(np, fn)(once) == once


@pytest.mark.parametrize(("pid", "text", "fn", "marker", "added"), NB2_CASES)
def test_nb2_changed_text_raises(pid, text, fn, marker, added):
    np = _load()
    with pytest.raises(RuntimeError, match=pid):
        getattr(np, fn)("int unrelated(void) { return 0; }\n")
    with pytest.raises(RuntimeError, match=pid):
        getattr(np, fn)(text + text)  # anchor twice: ambiguous, refuse


def check_nb04(out: str) -> None:
    """Behavioral assertions on NB-04 patched deinit() text (also run against the first form)."""
    assert "    if (clearAll) {" not in out
    # the stop result is captured in its own variable right after nimble_port_stop(), before
    # the controller deinit overwrites rc
    assert "int stopRc = 0;" in out
    assert out.index("rc = nimble_port_stop();") < out.index("stopRc = rc;")
    assert out.index("stopRc = rc;") < out.index("rc = esp_nimble_hci_and_controller_deinit();")
    # clearAll is gated on the stop result only, never on rc
    assert "if (clearAll && stopRc == 0)" in out
    assert "clearAll && rc == 0" not in out
    assert out.index("if (clearAll && stopRc == 0)") < out.index("delete NimBLEDevice::m_pServer;")
    # the return value is unchanged
    assert "return rc == 0;" in out


def test_nb04_free_only_after_successful_stop():
    np = _load()
    check_nb04(np.patch_deinit_text(DEINIT_TEXT))


def test_nb04_upgrades_first_form():
    np = _load()
    v1 = DEINIT_TEXT.replace(np.DEINIT_ORIGINAL, np.DEINIT_PATCHED_V1)
    assert v1 != DEINIT_TEXT
    out = np.patch_deinit_text(v1)
    check_nb04(out)
    assert out.count(np.DEINIT_MARKER) == 1
    assert np.patch_deinit_text(out) == out
    assert out == np.patch_deinit_text(DEINIT_TEXT)


def check_nb05(out: str) -> None:
    """Behavioral assertions on NB-05 patched init() text (also run against the first form)."""
    # controller enable / hci init failures release what was already taken
    en = out[out.index("esp_bt_controller_enable() failed") :]
    assert en.index("esp_bt_controller_deinit();") < en.index("return false;")
    hci = out[out.index("esp_nimble_hci_init() failed") :]
    assert hci.index("esp_bt_controller_disable();") < hci.index("return false;")
    # a local flag is set inside the bring-up block, nowhere else
    head = out[: out.index("esp_bt_controller_enable()")]
    assert "bool bringUp = false;" in head
    assert head.index("if (!m_initialized) {") < head.index("bringUp = true;")
    assert out.count("bringUp = true;") == 1
    # the sync wait is bounded
    wait = out[
        out.index("// Wait for host and controller to sync") : out.index("m_initialized = true;")
    ]
    assert "NIMBLE_SYNC_TIMEOUT_MS" in wait
    assert out.index("while (!m_synced)") < out.index("m_initialized = true;")
    # on timeout: without the flag just return false, before any unwind call
    assert "if (!bringUp)" in wait
    guard = wait.index("if (!bringUp)")
    assert guard < wait.index("nimble_port_stop();")
    assert wait.index("return false;") < wait.index("nimble_port_stop();")
    # the stop result is checked; the port is freed only for 0 / BLE_HS_EALREADY, else logged
    assert "= nimble_port_stop();" in wait
    assert "stopRc == 0 || stopRc == BLE_HS_EALREADY" in wait
    assert wait.index("stopRc == 0 || stopRc == BLE_HS_EALREADY") < wait.index(
        "nimble_port_deinit();"
    )
    assert wait.index("nimble_port_deinit();") < wait.index(
        "esp_nimble_hci_and_controller_deinit();"
    )
    assert "nimble_port_stop() failed" in wait
    assert wait.index("} else {") < wait.index("nimble_port_stop() failed")
    assert wait.index("nimble_port_stop() failed") < wait.index("m_synced      = false;")
    # the late-sync flags are cleared after the unwind, before the final return false
    assert wait.index("m_synced      = false;") < wait.index("m_initialized = false;")
    assert wait.index("m_initialized = false;") < wait.rindex("return false;")
    assert wait.rindex("return false;") < wait.index("ble_npl_time_delay(1);")


def test_nb05_unwind_and_bounded_sync_wait():
    np = _load()
    check_nb05(np.patch_init_text(INIT_TEXT))


def test_nb05_upgrades_first_form():
    np = _load()
    first = np.patch_init_text(INIT_TEXT)
    v1 = first.replace(np.INIT_WAIT_PATCHED, np.INIT_WAIT_PATCHED_V1)
    v1 = v1.replace(np.INIT_UP_PATCHED, np.INIT_UP_ORIGINAL)
    assert v1 != first
    out = np.patch_init_text(v1)
    check_nb05(out)
    assert out == first
    assert np.patch_init_text(out) == out


def test_nb06_host_task_ends_itself():
    np = _load()
    out = np.patch_host_task_text(HOST_TASK_TEXT)
    assert "nimble_port_freertos_deinit();" not in out
    assert out.index("nimble_port_run();") < out.index("vTaskDelete(NULL);")


def test_nb07_counter_reset_before_done():
    np = _load()
    out = np.patch_stop_text(STOP_TEXT)
    assert out.index("ble_hs_stop_conn_cnt = 0;") < out.index("ble_hs_stop_done(0);")


def test_nb04_to_nb07_share_one_file_and_compose():
    np = _load()
    both = INIT_TEXT + DEINIT_TEXT + HOST_TASK_TEXT
    a = np.patch_host_task_text(np.patch_deinit_text(np.patch_init_text(both)))
    b = np.patch_init_text(np.patch_host_task_text(np.patch_deinit_text(both)))
    assert a == b
    assert np.DEVICE_REL == "NimBLE-Arduino/src/NimBLEDevice.cpp"
    assert np.STOP_REL.endswith("nimble/host/src/ble_hs_stop.c")


def test_nb2_file_ops_and_table(tmp_path):
    np = _load()
    ids = [i for i, _, _ in np.PATCHES]
    assert ids[-4:] == ["NB-04", "NB-05", "NB-06", "NB-07"]
    f = tmp_path / "NimBLEDevice.cpp"
    assert np.patch_deinit_file(tmp_path / "missing.cpp") is False
    f.write_text(INIT_TEXT + DEINIT_TEXT + HOST_TASK_TEXT)
    for fn in (np.patch_deinit_file, np.patch_init_file, np.patch_host_task_file):
        assert fn(f) is True
        assert fn(f) is False
    s = tmp_path / "ble_hs_stop.c"
    s.write_text(STOP_TEXT)
    assert np.patch_stop_file(s) is True
    assert np.patch_stop_file(s) is False


def test_nb2_real_library_files():
    root = TOOLS.parent
    np = _load()
    seen = 0
    for rel, fns in (
        (np.DEVICE_REL, (np.patch_deinit_text, np.patch_init_text, np.patch_host_task_text)),
        (np.STOP_REL, (np.patch_stop_text,)),
    ):
        for f in sorted(root.glob(f".pio/libdeps/*/{rel}")):
            text = f.read_text()
            out = text
            for fn in fns:
                out = fn(out)
                assert fn(out) == out, f
            seen += 1
    if not seen:
        pytest.skip("NimBLE-Arduino not installed")
