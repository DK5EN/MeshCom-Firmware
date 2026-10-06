"""RM-GWRELAY (2026-10-06): remote-management commands are accepted from either path.

The former LoRa-only rule keyed on ``aprsmsg.msg_server``, which a gateway also sets on every frame it
relays over RF: a node behind a gateway dropped every command silently (bench 2026-10-06). The decision
now lives in ``src/rm_rx_gate.h`` and must be called from OnRxDone and from both server-ingress handlers;
none of them may gate it on the server flag again.
"""

from __future__ import annotations

import re
from pathlib import Path

SRC = Path(__file__).resolve().parents[2] / "src"


def _body(text: str, signature: str) -> str:
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[brace : i + 1]
    raise AssertionError("unbalanced braces after " + signature)


def _code(text: str) -> str:
    """Source without // and /* */ comments."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def test_gate_has_no_server_flag() -> None:
    gate = _code((SRC / "rm_rx_gate.h").read_text())
    assert "msg_server" not in gate
    assert "rmQueuePush" in gate and "rmReplyPush" in gate


def test_lora_hook_does_not_gate_on_the_server_flag() -> None:
    lora = (SRC / "lora_functions.cpp").read_text()
    body = _code(_body(lora, "static bool rmTryQueue("))
    assert "msg_server" not in body
    assert "rmRxTryQueue(" in body


def test_both_server_ingress_handlers_call_the_gate() -> None:
    for rel in ("esp32/udp_frame_esp32.cpp", "nrf52/udp_frame_nrf52.cpp"):
        code = _code((SRC / rel).read_text())
        assert code.count("rmRxTryQueue(") == 1, rel
        # the command must stay off the display and the phone
        assert "!bStoConsumed && !bRmConsumed)" in code, rel
        assert code.count("!bRmConsumed") == 2, rel
