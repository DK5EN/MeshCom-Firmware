"""PlatformIO pre-script (AU-19): patch NimBLE-Arduino ble_hs.c in place.

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


def patch_file(path: Path) -> bool:
    """Patch file in place; True if written. Missing file is a no-op."""
    if not path.is_file():
        return False
    text = path.read_text()
    out = patch_text(text)
    if out == text:
        return False
    path.write_text(out)
    return True


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
    _p = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env["PIOENV"] / REL
    # Dependencies are installed before pre-scripts run, so an env that pulls
    # NimBLE-Arduino and has no ble_hs.c here means the layout moved: fail, never
    # build an unpatched image silently.
    if not _p.is_file() and wants_nimble(env.GetProjectOption("lib_deps", [])):
        raise RuntimeError(f"nimble_patch AU-19: NimBLE-Arduino in lib_deps but {_p} is missing")
    if patch_file(_p):
        print("nimble_patch: AU-19 applied to", _p, file=sys.stderr)
