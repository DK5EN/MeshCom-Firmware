"""PlatformIO pre-script (AU-19, AU-20, NB-01..NB-03): patch NimBLE-Arduino 2.2.3 in place.

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


# (id, path relative to the env libdeps dir, file patcher); NB-01 and NB-02 share a file.
PATCHES = (
    ("AU-19", REL, patch_file),
    ("AU-20", FLOW_REL, patch_flow_file),
    ("NB-01", SET_REL, patch_set_file),
    ("NB-02", APPEND_REL, patch_append_file),
    ("NB-03", HCI_REL, patch_hci_file),
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
