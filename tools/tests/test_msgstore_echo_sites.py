"""Structural pins for the store node's own delivery coming back (field case 2026-11-06).

A hop-0 delivery carries a fresh msg_id and returns through another gateway. Two call sites that
need the firmware stay off the host, so the source is pinned here; the behaviour was proven on the
bench with --injectraw.

MSGSTORE_ECHO_SRC_ROOT points the test at a mutated copy of src/ (mutation proof).
"""
import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = Path(os.environ.get("MSGSTORE_ECHO_SRC_ROOT", ROOT / "src"))


def _function_body(text: str, signature: str) -> str:
    start = text.index(signature)
    open_brace = text.index("{", start)
    # Braces inside string literals ("{" in the {NNN suffix) do not nest.
    code = re.sub(r'"(?:\\.|[^"\\])*"', lambda m: " " * len(m.group(0)), text)
    depth = 0
    for pos in range(open_brace, len(code)):
        if code[pos] == "{":
            depth += 1
        elif code[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace : pos + 1]
    raise AssertionError(f"unbalanced braces after {signature}")


def test_delivery_id_enters_the_dedup_ring_after_a_successful_enqueue() -> None:
    glue = (SRC / "msgstore_glue.cpp").read_text(encoding="utf-8")
    body = _function_body(glue, "static bool glueDeliver(")
    enqueue = body.index('addTxRingEntryOnce(buf, len, "mbox")')
    refused = body.index("return false", enqueue)
    registered = body.index("addLoraRxBuffer(m.msg_id, false)")
    assert enqueue < refused < registered


def test_delivery_stays_out_of_own_tx_and_upload() -> None:
    glue = (SRC / "msgstore_glue.cpp").read_text(encoding="utf-8")
    body = _function_body(glue, "static bool glueDeliver(")
    assert "insertOwnTx" not in body
    assert "addNodeData" not in body


def test_rf_store_decision_checks_the_path_for_the_own_call() -> None:
    lora = (SRC / "lora_functions.cpp").read_text(encoding="utf-8")
    call = re.search(r"mboxClassify\(aprsmsg\.msg_source_call.*?\);", lora, re.S)
    assert call is not None
    code = re.sub(r"//[^\n]*", "", call.group(0))
    assert re.search(
        r"bMboxPeerDelivery\s*\|\|\s*mboxPathHasCall\(aprsmsg\.msg_source_path,\s*meshcom_settings\.node_call\)",
        code,
    )
