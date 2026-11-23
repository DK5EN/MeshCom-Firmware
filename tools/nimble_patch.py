"""PlatformIO pre-script (AU-19, AU-20, NB-01..NB-07): patch NimBLE-Arduino 2.2.3 in place.

AU-19, ble_hs.c:

What: in ble_hs_timer_reset(), the "host not enabled" branch no longer calls
ble_npl_callout_deinit(&ble_hs_timer); it only stops the timer. A timer event
that is already queued keeps its callback and runs harmlessly (it reschedules,
which now just stops). No ble_npl_eventq_remove(): its drain-and-requeue holds a
function-local portMUX, which does not lock against the host task on the other core.
Why: while a stop begins, the deinit zeroes the callout (including the embedded
event's callback) although the esp_timer may have just queued that event on the
host queue; the host task then calls NULL (classic ESP32 panic InstrFetchProhibited,
PC 0x0). ble_hs_deinit() deinits the callout anyway, so the early deinit is redundant.
Remove when NimBLE-Arduino fixes ble_hs_timer_reset().
Upstream fixed the same race in 2.5.2 with the identical change (drop the deinit,
https://github.com/h2zero/NimBLE-Arduino/commit/823624df); ours stays because the
library is pinned at 2.2.3 (no bump, FORMAL-01 wave 3a). Remove with the pin bump.

AU-20, ble_hs_flow.c:
What: ble_hs_flow_startup() calls ble_npl_callout_deinit(&ble_hs_flow_timer) right
before its ble_npl_callout_init(&ble_hs_flow_timer, ...).
Why: ble_hs_flow_init() and ble_hs_flow_startup() both init the same callout, and
the live npl_freertos_callout_init() always esp_timer_create()s; the second init
orphans the first esp_timer (~50 B per BLE pause/resume); ble_hs_flow_deinit() deletes
only the second. The deinit runs in the host task at sync; on a pause/resume the timer
was just created and is not armed. After a host reset it may be armed: the deinit
stops and deletes it, and a timer event already queued is harmless (the init restores
its callback in the same host task before the event can be dequeued).
Remove when NimBLE-Arduino fixes the double init. (Checked against 2.5.2: ble_hs_flow.c
still inits the callout in both ble_hs_flow_startup() and ble_hs_flow_init(); not fixed.)

NB-01, NimBLEAttValue.cpp (upstream 2b2a56c8, 2.3.0):
What: NimBLEAttValue::setValue() also zeroes the first value byte when it resets the length.
Why: a zero-length write (empty ATT write from the phone) set the length to 0 and appended
nothing, so the old content stayed readable as c_str()/data(); the write callback then saw the
previous frame again. Our onWrite returns on length 0 (esp32_main.cpp), so the stale read
does not reach the mesh today; the patch keeps the getter contract for every other caller.
Unlike upstream 2b2a56c8 the store is NULL-guarded (m_attr_value is NULL after a failed
initial calloc) and memcmp runs only on a successful, non-empty append (FORMAL-01 NimBLE #5/#6).
Remove when the NimBLE-Arduino pin includes upstream 2b2a56c8 (2.3.0 or later).

NB-02, NimBLEAttValue.cpp (upstream 7d0a3202, 2.5.1):
What: NimBLEAttValue::append() records the new capacity only after realloc() succeeded.
Why: on a failed realloc m_capacity was already raised although the buffer kept its old
size; a later append within that fake capacity wrote past the heap block (heap corruption on
low memory; the characteristic values grow on every phone write).
Remove when the pin includes upstream 7d0a3202 (2.5.1 or later).

NB-03, esp_nimble_hci.c (upstream 83d43378, 2.3.7):
What: the retry loop in ble_hci_rx_acl() waits pdMS_TO_TICKS(10) instead of vTaskDelay(1)
while the host has no free ACL mbuf.
Why: the loop runs in the controller callback; a one tick wait is 1 ms at a 1000 Hz tick and
spins the VHCI task while the host drains the 4-block msys pool (MSYS1_BLOCK_COUNT=4).
Remove when the pin includes upstream 83d43378 (2.3.7 or later).

NB-04, NimBLEDevice.cpp, NimBLEDevice::deinit(clearAll) (FORMAL-01 NimBLE review #2), three anchors:
What: the clearAll block (delete server / advertising / scan, delete clients) runs only when the
host stop succeeded (`if (clearAll && stopRc == 0)`). stopRc is a separate variable holding the
nimble_port_stop() result: rc is overwritten later by esp_nimble_hci_and_controller_deinit()
(IDF < 5), so gating on rc would mix in the controller result. The return value stays rc == 0.
Why: after a failed nimble_port_stop() the host task is still running and the GATT table still
references the characteristic objects; deleting them anyway is a use-after-free on the next
phone write (classic ESP32 forced pause). The caller sees false and keeps valid objects. The
library then keeps m_pServer: a caller that nulled its own pointers must pick the server up again
with getServer()/createServer() and reuse its services (esp32_main.cpp bleStackStart()).
Remove when NimBLE-Arduino guards the clearAll block itself.

NB-05, NimBLEDevice.cpp, NimBLEDevice::init() (review #4), four anchors:
What: (a) a failed esp_bt_controller_enable() deinits the controller, (b) a failed
esp_nimble_hci_init() disables and deinits it, (c) the wait for host sync is bounded by
NIMBLE_SYNC_TIMEOUT_MS = 5000; on timeout init() returns false and, only when THIS call did the
bring-up (local flag bringUp, set inside `if (!m_initialized)`), the host and the port are
unwound exactly as deinit() does: nimble_port_stop(); nimble_port_deinit() only if the stop
returned 0 or BLE_HS_EALREADY (host already stopped; any other error leaves the port allocated
and logs it, a leak instead of a use-after-free of the event queue); on IDF < 5 the
hci/controller deinit; then m_synced = false and m_initialized = false so a late sync callback
cannot leave a stale flag. When init() is called on an already-up stack (m_initialized true,
e.g. resume after a failed deinit) a timeout only returns false: the stack is not ours to tear
down. A host task that failed to start (xTaskCreatePinnedToCore result is not checked in
esp_nimble_enable) is covered by (c): the sync never comes, nimble_port_stop() returns
BLE_HS_EALREADY because the host was never started, the unwind runs. (d) is the bringUp flag.
Why: a partial failure left the controller enabled, so every retry failed in
esp_bt_controller_init() until reboot; the unbounded loop hung the caller (OTA job holding the
BLE lifetime lock) when the host never synced. Bound: normal sync takes well under 1 s after the
controller starts; 5 s is more than 10x that and still short of any watchdog the callers run.
Remove when NimBLE-Arduino unwinds init() failures and bounds the sync wait.

NB-06, NimBLEDevice.cpp, NimBLEDevice::host_task() (review #3):
What: the host task ends with vTaskDelete(NULL) instead of nimble_port_freertos_deinit().
Why: nimble_port_freertos_deinit() deletes the task stored in the library-global host_task_h.
nimble_port_stop() returns as soon as the stop event ran (semaphore), so deinit()/init() may
already have created the next host task and stored its handle there; the old task then deleted
the new one (S3 panic signature of AU-19). deinit() does not wait for the task end today either
(it learns "stopped" from the semaphore), so nothing else changes. host_task_h is left holding
the dead handle until the next init overwrites it; its only reader,
nimble_port_freertos_get_hs_hwm(), is not called by this firmware.
Remove when NimBLE-Arduino stops deleting the shared handle from the task itself.

NB-07, ble_hs_stop.c, ble_hs_stop_terminate_timeout_cb() (review #8):
What: the timeout callback resets ble_hs_stop_conn_cnt to 0 before ble_hs_stop_done(0).
Why: after a graceful-disconnect timeout the counter stayed above 0, so every later stop (even
without connections) took the "wait for disconnects" branch and ran into the full
BLE_HS_STOP_ON_SHUTDOWN_TIMEOUT again (about 2 s extra per pause). done() unregisters the GAP
listener first, so no late event decrements the reset counter.
Remove when NimBLE-Arduino resets the counter in ble_hs_stop_done().

Evaluated and not ported (2.2.3 -> 2.5.2): see the FORMAL-01 wave 3a report; the code is
either absent from 2.2.3 (ble_gap_rx_rd_rem_ver_info_complete, 2.5.2 ticks-to-ms
recursion), or compiled out (central/observer roles), or an API we never call.

Idempotent (marker, no write when patched); fails the build if the library text
changed; no-op where the library is absent (nRF52, native). The text transform
is importable without SCons.
"""

import sys
from pathlib import Path

MARKER = "/* MESHCOM-PATCH AU-19 */"
REL = "NimBLE-Arduino/src/nimble/nimble/host/src/ble_hs.c"

ORIGINAL = """    if (!ble_hs_is_enabled()) {
        ble_npl_callout_stop(&ble_hs_timer);
        ble_npl_callout_deinit(&ble_hs_timer);
    } else {"""

PATCHED = (
    """    if (!ble_hs_is_enabled()) {
        """
    + MARKER
    + """
        ble_npl_callout_stop(&ble_hs_timer);
    } else {"""
)


def patch_text(text: str) -> str:
    """Return patched text; unchanged if already patched; RuntimeError if unknown."""
    if MARKER in text:
        return text
    if text.count(ORIGINAL) != 1:
        raise RuntimeError(
            "nimble_patch AU-19: ble_hs_timer_reset() not found in the expected form; NimBLE-Arduino changed, review the patch"
        )
    return text.replace(ORIGINAL, PATCHED)


FLOW_MARKER = "/* MESHCOM-PATCH AU-20 */"
FLOW_REL = "NimBLE-Arduino/src/nimble/nimble/host/src/ble_hs_flow.c"

FLOW_ORIGINAL = """#endif
    ble_npl_callout_init(&ble_hs_flow_timer, ble_hs_evq_get(),
                         ble_hs_flow_event_cb, NULL);
#endif

    return 0;"""

FLOW_PATCHED = (
    """#endif
    ble_npl_callout_deinit(&ble_hs_flow_timer); """
    + FLOW_MARKER
    + """
    ble_npl_callout_init(&ble_hs_flow_timer, ble_hs_evq_get(),
                         ble_hs_flow_event_cb, NULL);
#endif

    return 0;"""
)


def patch_flow_text(text: str) -> str:
    """AU-20: return patched text; unchanged if already patched; RuntimeError if unknown."""
    if FLOW_MARKER in text:
        return text
    if text.count(FLOW_ORIGINAL) != 1:
        raise RuntimeError(
            "nimble_patch AU-20: ble_hs_flow_startup() not found in the expected form; NimBLE-Arduino changed, review the patch"
        )
    return text.replace(FLOW_ORIGINAL, FLOW_PATCHED)


def _apply(text: str, marker: str, original: str, patched: str, pid: str, where: str) -> str:
    """Generic anchored patch: patched text; unchanged if marker present; RuntimeError if unknown."""
    if marker in text:
        return text
    if text.count(original) != 1:
        raise RuntimeError(
            f"nimble_patch {pid}: {where} not found in the expected form; NimBLE-Arduino changed, review the patch"
        )
    return text.replace(original, patched)


# --- NB-01: NimBLEAttValue::setValue() clears the first byte (upstream 2b2a56c8) ---

SET_MARKER = "/* MESHCOM-PATCH NB-01 */"
SET_REL = "NimBLE-Arduino/src/NimBLEAttValue.cpp"

SET_ORIGINAL = """    m_attr_len = 0; // Just set the value length to 0 and append instead of repeating code.
    append(value, len);
    return memcmp(m_attr_value, value, len) == 0 && m_attr_len == len;"""

# First form of NB-01 (never committed, may sit in an existing .pio/libdeps tree): upgraded in place.
SET_PATCHED_V1 = (
    """    m_attr_len = 0; // Just set the value length to 0 and append instead of repeating code.
    m_attr_value[0] = '\\0'; """
    + SET_MARKER
    + """
    append(value, len);
    return memcmp(m_attr_value, value, len) == 0 && m_attr_len == len;"""
)

SET_PATCHED = (
    """    m_attr_len = 0; // Just set the value length to 0 and append instead of repeating code.
    if (m_attr_value != nullptr) m_attr_value[0] = '\\0'; """
    + SET_MARKER
    + """
    append(value, len);
    return m_attr_len == len && (len == 0 || memcmp(m_attr_value, value, len) == 0);"""
)


def patch_set_text(text: str) -> str:
    """NB-01: return patched text; unchanged if already patched; RuntimeError if unknown."""
    if text.count(SET_PATCHED_V1) == 1:
        return text.replace(SET_PATCHED_V1, SET_PATCHED)
    return _apply(
        text, SET_MARKER, SET_ORIGINAL, SET_PATCHED, "NB-01", "NimBLEAttValue::setValue()"
    )


# --- NB-02: NimBLEAttValue::append() raises m_capacity only after realloc (upstream 7d0a3202) ---

APPEND_MARKER = "/* MESHCOM-PATCH NB-02 */"
APPEND_REL = SET_REL

APPEND_ORIGINAL = """    if (new_len > m_capacity) {
        res        = static_cast<uint8_t*>(realloc(m_attr_value, (new_len + 1)));
        m_capacity = new_len;
    }
    NIMBLE_CPP_DEBUG_ASSERT(res);
    if (res == nullptr) {
        NIMBLE_LOGE(LOG_TAG, "Failed to realloc append");
        return *this;
    }
"""

APPEND_PATCHED = (
    """    bool grow = new_len > m_capacity; """
    + APPEND_MARKER
    + """
    if (grow) {
        res = static_cast<uint8_t*>(realloc(m_attr_value, (new_len + 1)));
    }
    NIMBLE_CPP_DEBUG_ASSERT(res);
    if (res == nullptr) {
        NIMBLE_LOGE(LOG_TAG, "Failed to realloc append");
        return *this;
    }
    if (grow) {
        m_capacity = new_len;
    }
"""
)


def patch_append_text(text: str) -> str:
    """NB-02: return patched text; unchanged if already patched; RuntimeError if unknown."""
    return _apply(
        text, APPEND_MARKER, APPEND_ORIGINAL, APPEND_PATCHED, "NB-02", "NimBLEAttValue::append()"
    )


# --- NB-03: ble_hci_rx_acl() retry wait of 10 ms (upstream 83d43378) ---

HCI_MARKER = "/* MESHCOM-PATCH NB-03 */"
HCI_REL = "NimBLE-Arduino/src/nimble/esp_port/esp-hci/src/esp_nimble_hci.c"

HCI_ORIGINAL = """            /* Give some time to free buffer and try again */
            vTaskDelay(1);
"""

HCI_PATCHED = (
    """            /* Give some time to free buffer and try again */
            vTaskDelay(pdMS_TO_TICKS(10)); """
    + HCI_MARKER
    + "\n"
)


def patch_hci_text(text: str) -> str:
    """NB-03: return patched text; unchanged if already patched; RuntimeError if unknown."""
    return _apply(
        text, HCI_MARKER, HCI_ORIGINAL, HCI_PATCHED, "NB-03", "ble_hci_rx_acl() retry wait"
    )


# --- NB-04..NB-07: NimBLE lifecycle fixes (FORMAL-01 NimBLE review #2, #4, #3, #8) ---

DEVICE_REL = "NimBLE-Arduino/src/NimBLEDevice.cpp"
STOP_REL = "NimBLE-Arduino/src/nimble/nimble/host/src/ble_hs_stop.c"

DEINIT_MARKER = "/* MESHCOM-PATCH NB-04 */"
DEINIT_VAR_MARKER = "/* MESHCOM-PATCH NB-04a */"
DEINIT_CAP_MARKER = "/* MESHCOM-PATCH NB-04b */"
DEINIT_ORIGINAL = """    if (clearAll) {
# if defined(CONFIG_BT_NIMBLE_ROLE_PERIPHERAL)
"""
DEINIT_PATCHED = (
    "    if (clearAll && stopRc == 0) { "
    + DEINIT_MARKER
    + "\n# if defined(CONFIG_BT_NIMBLE_ROLE_PERIPHERAL)\n"
)
# First form of NB-04 (gated on rc, which the controller deinit overwrites; may sit in an existing
# .pio/libdeps tree): upgraded in place.
DEINIT_PATCHED_V1 = (
    "    if (clearAll && rc == 0) { "
    + DEINIT_MARKER
    + "\n# if defined(CONFIG_BT_NIMBLE_ROLE_PERIPHERAL)\n"
)
DEINIT_VAR_ORIGINAL = """bool NimBLEDevice::deinit(bool clearAll) {
    int rc = 0;
"""
DEINIT_VAR_PATCHED = (
    """bool NimBLEDevice::deinit(bool clearAll) {
    int rc = 0;
    int stopRc = 0; """
    + DEINIT_VAR_MARKER
    + "\n"
)
DEINIT_CAP_ORIGINAL = """        rc = nimble_port_stop();
        if (rc == 0) {
"""
DEINIT_CAP_PATCHED = (
    """        rc = nimble_port_stop();
        stopRc = rc; """
    + DEINIT_CAP_MARKER
    + """
        if (rc == 0) {
"""
)

INIT_MARKER = "/* MESHCOM-PATCH NB-05 */"
INIT_EN_MARKER = "/* MESHCOM-PATCH NB-05a */"
INIT_HCI_MARKER = "/* MESHCOM-PATCH NB-05b */"
INIT_EN_ORIGINAL = """            NIMBLE_LOGE(LOG_TAG, "esp_bt_controller_enable() failed; err=%d", err);
            return false;
"""
INIT_EN_PATCHED = (
    """            NIMBLE_LOGE(LOG_TAG, "esp_bt_controller_enable() failed; err=%d", err);
            esp_bt_controller_deinit(); """
    + INIT_EN_MARKER
    + """
            return false;
"""
)
INIT_HCI_ORIGINAL = """            NIMBLE_LOGE(LOG_TAG, "esp_nimble_hci_init() failed; err=%d", err);
            return false;
"""
INIT_HCI_PATCHED = (
    """            NIMBLE_LOGE(LOG_TAG, "esp_nimble_hci_init() failed; err=%d", err);
            esp_bt_controller_disable(); """
    + INIT_HCI_MARKER
    + """
            esp_bt_controller_deinit();
            return false;
"""
)
INIT_UP_MARKER = "/* MESHCOM-PATCH NB-05c */"
INIT_UP_ORIGINAL = """bool NimBLEDevice::init(const std::string& deviceName) {
    if (!m_initialized) {
"""
INIT_UP_PATCHED = (
    """bool NimBLEDevice::init(const std::string& deviceName) {
    bool bringUp = false; """
    + INIT_UP_MARKER
    + """
    if (!m_initialized) {
        bringUp = true;
"""
)
INIT_WAIT_ORIGINAL = """    while (!m_synced) {
        ble_npl_time_delay(1);
    }
"""
INIT_WAIT_PATCHED = (
    """    const uint32_t NIMBLE_SYNC_TIMEOUT_MS = 5000; """
    + INIT_MARKER
    + """
    const ble_npl_time_t syncStart = ble_npl_time_get();
    while (!m_synced) {
        if ((ble_npl_time_t)(ble_npl_time_get() - syncStart) > ble_npl_time_ms_to_ticks32(NIMBLE_SYNC_TIMEOUT_MS)) {
            NIMBLE_LOGE(LOG_TAG, "host sync timeout");
            if (!bringUp) {
                return false; // stack was already up: not ours to unwind
            }
            NIMBLE_LOGE(LOG_TAG, "unwinding init");
            int stopRc = nimble_port_stop(); // BLE_HS_EALREADY if the host never started
            if (stopRc == 0 || stopRc == BLE_HS_EALREADY) {
                nimble_port_deinit();
# if defined(ESP_PLATFORM) && defined(CONFIG_NIMBLE_CPP_IDF) && ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
                esp_nimble_hci_and_controller_deinit();
# endif
            } else {
                NIMBLE_LOGE(LOG_TAG, "nimble_port_stop() failed; err=%d; port left allocated", stopRc);
            }
            m_synced      = false;
            m_initialized = false;
            return false;
        }
        ble_npl_time_delay(1);
    }
"""
)
# First form of the NB-05 wait (unwind unconditionally, stop result unchecked, flags untouched; may sit
# in an existing .pio/libdeps tree): upgraded in place.
INIT_WAIT_PATCHED_V1 = (
    """    const uint32_t NIMBLE_SYNC_TIMEOUT_MS = 5000; """
    + INIT_MARKER
    + """
    const ble_npl_time_t syncStart = ble_npl_time_get();
    while (!m_synced) {
        if ((ble_npl_time_t)(ble_npl_time_get() - syncStart) > ble_npl_time_ms_to_ticks32(NIMBLE_SYNC_TIMEOUT_MS)) {
            NIMBLE_LOGE(LOG_TAG, "host sync timeout; unwinding init");
            nimble_port_stop(); // returns an error at once if the host never started
            nimble_port_deinit();
# if defined(ESP_PLATFORM) && defined(CONFIG_NIMBLE_CPP_IDF) && ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
            esp_nimble_hci_and_controller_deinit();
# endif
            return false;
        }
        ble_npl_time_delay(1);
    }
"""
)

HOST_TASK_MARKER = "/* MESHCOM-PATCH NB-06 */"
HOST_TASK_ORIGINAL = """    nimble_port_run(); // This function will return only when nimble_port_stop() is executed
    nimble_port_freertos_deinit();
"""
HOST_TASK_PATCHED = (
    """    nimble_port_run(); // This function will return only when nimble_port_stop() is executed
    vTaskDelete(NULL); """
    + HOST_TASK_MARKER
    + "\n"
)

STOP_MARKER = "/* MESHCOM-PATCH NB-07 */"
STOP_ORIGINAL = """    /* TODO: Shall we send error here? */
    ble_hs_stop_done(0);
"""
STOP_PATCHED = (
    """    /* TODO: Shall we send error here? */
    ble_hs_stop_conn_cnt = 0; """
    + STOP_MARKER
    + """
    ble_hs_stop_done(0);
"""
)


def patch_deinit_text(text: str) -> str:
    """NB-04: return patched text; unchanged if already patched; RuntimeError if unknown."""
    if text.count(DEINIT_PATCHED_V1) == 1:
        text = text.replace(DEINIT_PATCHED_V1, DEINIT_PATCHED)
    text = _apply(
        text,
        DEINIT_VAR_MARKER,
        DEINIT_VAR_ORIGINAL,
        DEINIT_VAR_PATCHED,
        "NB-04",
        "NimBLEDevice::deinit() head",
    )
    text = _apply(
        text,
        DEINIT_CAP_MARKER,
        DEINIT_CAP_ORIGINAL,
        DEINIT_CAP_PATCHED,
        "NB-04",
        "NimBLEDevice::deinit() stop call",
    )
    return _apply(
        text,
        DEINIT_MARKER,
        DEINIT_ORIGINAL,
        DEINIT_PATCHED,
        "NB-04",
        "NimBLEDevice::deinit() clearAll",
    )


def patch_init_text(text: str) -> str:
    """NB-05: return patched text; unchanged if already patched; RuntimeError if unknown."""
    if text.count(INIT_WAIT_PATCHED_V1) == 1:
        text = text.replace(INIT_WAIT_PATCHED_V1, INIT_WAIT_PATCHED)
    text = _apply(
        text,
        INIT_EN_MARKER,
        INIT_EN_ORIGINAL,
        INIT_EN_PATCHED,
        "NB-05",
        "controller enable failure",
    )
    text = _apply(
        text, INIT_HCI_MARKER, INIT_HCI_ORIGINAL, INIT_HCI_PATCHED, "NB-05", "hci init failure"
    )
    text = _apply(
        text, INIT_UP_MARKER, INIT_UP_ORIGINAL, INIT_UP_PATCHED, "NB-05", "init() bring-up block"
    )
    return _apply(
        text,
        INIT_MARKER,
        INIT_WAIT_ORIGINAL,
        INIT_WAIT_PATCHED,
        "NB-05",
        "host sync wait",
    )


def patch_host_task_text(text: str) -> str:
    """NB-06: return patched text; unchanged if already patched; RuntimeError if unknown."""
    return _apply(
        text,
        HOST_TASK_MARKER,
        HOST_TASK_ORIGINAL,
        HOST_TASK_PATCHED,
        "NB-06",
        "NimBLEDevice::host_task()",
    )


def patch_stop_text(text: str) -> str:
    """NB-07: return patched text; unchanged if already patched; RuntimeError if unknown."""
    return _apply(
        text,
        STOP_MARKER,
        STOP_ORIGINAL,
        STOP_PATCHED,
        "NB-07",
        "ble_hs_stop_terminate_timeout_cb()",
    )


def _patch_path(path: Path, transform) -> bool:
    if not path.is_file():
        return False
    text = path.read_text()
    out = transform(text)
    if out == text:
        return False
    path.write_text(out)
    return True


def patch_flow_file(path: Path) -> bool:
    """AU-20: patch file in place; True if written. Missing file is a no-op."""
    return _patch_path(path, patch_flow_text)


def patch_file(path: Path) -> bool:
    """Patch file in place; True if written. Missing file is a no-op."""
    return _patch_path(path, patch_text)


def patch_set_file(path: Path) -> bool:
    """NB-01: patch file in place; True if written. Missing file is a no-op."""
    return _patch_path(path, patch_set_text)


def patch_append_file(path: Path) -> bool:
    """NB-02: patch file in place; True if written. Missing file is a no-op."""
    return _patch_path(path, patch_append_text)


def patch_hci_file(path: Path) -> bool:
    """NB-03: patch file in place; True if written. Missing file is a no-op."""
    return _patch_path(path, patch_hci_text)


def patch_deinit_file(path: Path) -> bool:
    """NB-04: patch file in place; True if written. Missing file is a no-op."""
    return _patch_path(path, patch_deinit_text)


def patch_init_file(path: Path) -> bool:
    """NB-05: patch file in place; True if written. Missing file is a no-op."""
    return _patch_path(path, patch_init_text)


def patch_host_task_file(path: Path) -> bool:
    """NB-06: patch file in place; True if written. Missing file is a no-op."""
    return _patch_path(path, patch_host_task_text)


def patch_stop_file(path: Path) -> bool:
    """NB-07: patch file in place; True if written. Missing file is a no-op."""
    return _patch_path(path, patch_stop_text)


# (id, path relative to the env libdeps dir, file patcher); NB-01/02 and NB-04/05/06 share a file each.
PATCHES = (
    ("AU-19", REL, patch_file),
    ("AU-20", FLOW_REL, patch_flow_file),
    ("NB-01", SET_REL, patch_set_file),
    ("NB-02", APPEND_REL, patch_append_file),
    ("NB-03", HCI_REL, patch_hci_file),
    ("NB-04", DEVICE_REL, patch_deinit_file),
    ("NB-05", DEVICE_REL, patch_init_file),
    ("NB-06", DEVICE_REL, patch_host_task_file),
    ("NB-07", STOP_REL, patch_stop_file),
)


try:
    Import("env")  # noqa: F821  (SCons builtin)
except NameError:
    env = None


def wants_nimble(lib_deps) -> bool:
    """True if a lib_deps list (strings or one newline-joined string) names NimBLE-Arduino."""
    if isinstance(lib_deps, str):
        lib_deps = lib_deps.splitlines()
    return any("nimble-arduino" in str(d).lower() for d in lib_deps)


if env is not None:
    _libdeps = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env["PIOENV"]
    # Dependencies are installed before pre-scripts run, so an env that pulls
    # NimBLE-Arduino and has no patch target here means the layout moved: fail,
    # never build an unpatched image silently.
    for _id, _rel, _patch in PATCHES:
        _p = _libdeps / _rel
        if not _p.is_file() and wants_nimble(env.GetProjectOption("lib_deps", [])):
            raise RuntimeError(
                f"nimble_patch {_id}: NimBLE-Arduino in lib_deps but {_p} is missing"
            )
        if _patch(_p):
            print(f"nimble_patch: {_id} applied to", _p, file=sys.stderr)
