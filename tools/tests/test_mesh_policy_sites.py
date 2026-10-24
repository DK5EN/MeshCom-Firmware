"""Structural pins for `--mesh off` (BL-01, docs/campaign-bl01-gwrx01-nrf52-20261024.md section 1).

`--mesh off` stops only the repeat of foreign frames. The two relay gates go through
meshTxAllowed(MESH_TX_FOREIGN_RELAY, ...); the always-sent sites must not look at the mesh flag.

MESH_POLICY_SRC_ROOT points the test at a mutated copy of src/ (mutation proof).
"""
import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = Path(os.environ.get("MESH_POLICY_SRC_ROOT", ROOT / "src"))
DOC = "docs/campaign-bl01-gwrx01-nrf52-20261024.md section 1"

# Every file that mentions bMESH today, with the number of lines (declaration, assignment, settings,
# web, command and display sites included). A new read anywhere fails: route a relay decision through
# meshTxAllowed(), and bump the count here only with the operator's table in mind.
BMESH_ALLOWLIST = {
    "aprs_functions.cpp": 1,
    "command_functions.cpp": 4,
    "esp32/esp32_main.cpp": 1,
    "loop_functions.cpp": 3,
    "loop_functions.h": 1,
    "loop_functions_extern.h": 1,
    "lora_functions.cpp": 3,
    "nrf52/nrf52_main.cpp": 1,
    "rm_runtime.cpp": 1,
    "t-deck-pro/ui_deckpro.cpp": 3,
    "t-deck/lv_obj_functions.cpp": 1,
    "via_functions.cpp": 14,
    "web_functions/web_functions.cpp": 3,
    "web_functions/web_setup.cpp": 3,
}


def _read(rel):
    return (SRC / rel).read_text(errors="replace")


def test_bmesh_reads_stay_on_allowlist():
    found = {}
    for p in sorted(SRC.rglob("*")):
        if p.suffix not in (".cpp", ".h", ".c", ".hpp", ".ino") or not p.is_file():
            continue
        n = sum(1 for ln in p.read_text(errors="replace").splitlines() if re.search(r"\bbMESH\b", ln))
        if n:
            found[p.relative_to(SRC).as_posix()] = n
    assert found == BMESH_ALLOWLIST, (
        f"bMESH use changed. --mesh off may gate only foreign-frame relay, see {DOC}. "
        f"found={found}"
    )


def test_relay_gates_use_policy():
    via = _read("via_functions.cpp")
    start = via.index("bool checkMesh(")
    end = via.index("\n}\n", start)
    body = via[start:end]
    assert body.count("meshTxAllowed(MESH_TX_FOREIGN_RELAY") == 2, f"checkMesh() gate, see {DOC}"
    assert not re.search(r"^\s*return\s+bMESH\s*;", body, re.M), f"checkMesh() reads bMESH raw, see {DOC}"

    lora = _read("lora_functions.cpp")
    m = re.search(r"if\(\(print_buff\[5\] & 0x7F\) > 0x00 &&[^\n]*checkServerRx\(print_buff\+6\)\)", lora)
    assert m, f"rx_ack_fwd statement not found, see {DOC}"
    assert "meshTxAllowed(MESH_TX_FOREIGN_RELAY" in m.group(0), f"rx_ack_fwd gate, see {DOC}"
    assert not re.search(r"\bbMESH\b\s*&&", m.group(0))


MESH_TOKENS = re.compile(r"\b(bMESH|checkMesh|meshTxAllowed)\b")


def _region_before_label(text, label, opener):
    """Text from the last `opener` before `"label"` up to the label (the enclosing gateway block)."""
    pos = text.index(f'"{label}"')
    start = text.rindex(opener, 0, pos)
    return text[start:pos]


def _function_body(text, signature):
    """From the signature to the first column-0 closing brace (all functions here are top level)."""
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


def test_gateway_acks_not_mesh_gated():
    # Delimiter: from the last `if(bGATEWAY)` before the label to the label. Both labels sit in
    # the same bGATEWAY block; the block's condition and every nested condition are inside.
    lora = _read("lora_functions.cpp")
    for label in ("rx_dm_ack_gw", "rx_dm_ack_new"):
        region = _region_before_label(lora, label, "if(bGATEWAY)")
        assert not MESH_TOKENS.search(region), f"{label} must be sent regardless of mesh, see {DOC}"


def test_store_service_not_mesh_gated():
    glue = _read("msgstore_glue.cpp")
    for sig, label in (("static bool glueDeliver(", "mbox"), ("static bool glueNotify(", "sto")):
        body = _function_body(glue, sig)
        assert f'"{label}"' in body
        assert not MESH_TOKENS.search(body), f"{label} must be sent regardless of mesh, see {DOC}"


def test_udp_rx_not_mesh_gated():
    body = _function_body(_read("txring_functions.cpp"), "int addUdpRxRingEntry(")
    assert '"udp_rx"' in body
    assert not MESH_TOKENS.search(body), f"udp_rx must be sent regardless of mesh, see {DOC}"


def test_checkmesh_called_only_at_rx_relay():
    # The bMESH allowlist does not see a new checkMesh() call (e.g. at the gateway HEY upload or the
    # own-HEY echo upload); pin its call sites: one call in lora_functions.cpp, at rx_relay.
    calls = {}
    for p in sorted(SRC.rglob("*.cpp")):
        n = sum(
            1
            for ln in p.read_text(errors="replace").splitlines()
            if re.search(r"\bcheckMesh\(", ln) and not ln.lstrip().startswith("//")
            and not re.search(r"\bbool\s+checkMesh\(", ln)
        )
        if n:
            calls[p.relative_to(SRC).as_posix()] = n
    assert calls == {"lora_functions.cpp": 1}, f"checkMesh() call sites changed, see {DOC}: {calls}"
    lora = _read("lora_functions.cpp")
    pos = lora.index("checkMesh(aprsmsg)")
    assert lora.index('"rx_relay"', pos) - pos < 8000, "checkMesh() no longer guards rx_relay"
