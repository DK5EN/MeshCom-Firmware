"""PlatformIO pre-script (FORMAL-01 lwIP #2): link-time wrap flags for src/esp32/lwip_mtu_guard.cpp.

The guard raises a DHCP option 26 MTU below 576 before dhcp_bind's netif_set_addr, ip4_frag
and the TCP MSS computation see it. It does so with `-Wl,--wrap=` on the prebuilt liblwip, so
the three flags below must reach the link of EVERY env that compiles src/esp32, not only the
envs that inherit [esp32] build_flags (the e-paper variants and t_deck_pro do not).

Rule: add the flags when the platform is espressif32 with the arduino framework of the 2.0.x
line (IDF 4.4; package.json version "3.200xx...", e.g. 3.20017 = 2.0.17, 3.20014 = 2.0.14).
Nothing for native, nRF52 and safeboot (Tasmota 3.3.x, IDF 5: other lwIP, other symbols).
A flag already on the link line is not added twice. One line on stderr names what was added.
The decision functions are importable without SCons.
"""

import json
import sys
from pathlib import Path

PACKAGE = "framework-arduinoespressif32"
WRAP_FLAGS = (
    "-Wl,--wrap=netif_set_addr",
    "-Wl,--wrap=ip4_frag",
    "-Wl,--wrap=tcp_eff_send_mss_netif",
)


def framework_version(root) -> str:
    """package.json "version" of the framework dir; "" if absent or unreadable."""
    # Same 4-line reader as tools/arduino_esp32_patch.py: SCons pre-scripts run without __file__, so no shared import.
    try:
        return str(json.loads((Path(root) / "package.json").read_text()).get("version", ""))
    except (OSError, ValueError, TypeError, AttributeError):
        return ""


def is_arduino_2_0_line(version: str) -> bool:
    """True for "3.200xx.*" (arduino-esp32 2.0.x on IDF 4.4); False for Tasmota "3.3.x", 5.x, junk."""
    parts = str(version).split("+")[0].split(".")
    if len(parts) < 2 or parts[0] != "3" or not parts[1].isdigit():
        return False
    return 20000 <= int(parts[1]) < 20100


def wants_lwip_wrap(platform_name, frameworks, version) -> bool:
    """True for an espressif32 + arduino env whose framework is the 2.0.x line."""
    if platform_name != "espressif32":
        return False
    if isinstance(frameworks, str):
        frameworks = [f.strip() for f in frameworks.replace(",", "\n").splitlines()]
    if "arduino" not in [str(f).strip().lower() for f in frameworks]:
        return False
    return is_arduino_2_0_line(version)


def add_wrap_flags(env) -> list:
    """Append the wrap flags not yet in LINKFLAGS; return what was added (and say so once)."""
    present = [str(f) for f in env.get("LINKFLAGS", [])]
    added = [f for f in WRAP_FLAGS if f not in present]
    if added:
        env.Append(LINKFLAGS=added)
        print("lwip_wrap: LINKFLAGS += " + " ".join(added), file=sys.stderr)
    return added


try:
    Import("env")  # noqa: F821  (SCons builtin)
except NameError:
    env = None


if env is not None:
    _platform = env.PioPlatform()
    _root = _platform.get_package_dir(PACKAGE) if _platform.name == "espressif32" else None
    if wants_lwip_wrap(
        _platform.name, env.get("PIOFRAMEWORK", []), framework_version(_root) if _root else ""
    ):
        add_wrap_flags(env)
