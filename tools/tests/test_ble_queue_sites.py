"""Structural pins for the BLE phone frame queue (BLE-QDROP-01, BLE-STALE-01; specs/tla/bletask.tla).

- Both producers (ESP32 onWrite, nRF52 bleuart_rx_callback) check the xQueueSend result
  (bleQueueOnSend) and stamp the session generation (bleItemStamp).
- Both loop drains call bleQueueAccept before readPhoneCommand.
- The connect/disconnect callbacks (ESP32: bleSessionResetAll) bump the generation.
- BleQueueItem is defined once, in src/ble_session.h.

BLE_QUEUE_SRC_ROOT points the test at a mutated copy of src/ (mutation proof).
"""
import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = Path(os.environ.get("BLE_QUEUE_SRC_ROOT", ROOT / "src"))


def _read(rel):
    return (SRC / rel).read_text(errors="replace")


def _body(text, anchor, end_tok="\n}"):
    """Function body: from the anchor (signature up to the opening brace) to the next column-0 '}'."""
    start = text.index(anchor)
    end = text.find(end_tok, start)
    assert end > 0, anchor
    return text[start:end]


def _no_ignored_send(text, rel):
    for ln in text.splitlines():
        s = ln.strip()
        assert not s.startswith("xQueueSend(bleQueue"), f"{rel}: xQueueSend result ignored: {s}"


def test_esp32_producer_checks_send_and_stamps():
    t = _read("esp32/esp32_main.cpp")
    _no_ignored_send(t, "esp32_main.cpp")
    body = _body(t, "void onWrite(NimBLECharacteristic* pCharacteristic", "\n    }")
    assert "bleItemStamp(item, g_bleQ)" in body
    assert re.search(r"bleQueueOnSend\(g_bleQ,\s*bleQueue != NULL && xQueueSend\(bleQueue", body)


def test_esp32_onwrite_uses_one_copy_and_the_seam():
    t = _read("esp32/esp32_main.cpp")
    body = _body(t, "void onWrite(NimBLECharacteristic* pCharacteristic", "\n    }")
    assert "bleItemFromWrite(item, v.data(), v.size())" in body
    assert "const NimBLEAttValue v = pCharacteristic->getValue()" in body
    assert "memcpy" not in body, "onWrite must not copy by hand (length and data from different sources, F1)"
    assert "bleQueueOnNoCopy(g_bleQ)" in body


def test_esp32_queue_create_is_checked_and_drain_skips_missing_queue():
    t = _read("esp32/esp32_main.cpp")
    i = t.index("bleQueue = xQueueCreate(")
    assert "if(bleQueue == NULL)" in t[i:i + 300]
    assert "while (bleQueue != NULL && xQueueReceive(bleQueue" in t


def test_nrf52_overflow_callback_registered_and_counts_only():
    t = _read("nrf52/nrf52_ble.cpp")
    assert "g_ble_uart.setRxOverflowCallback(bleuart_rx_overflow_callback)" in t
    body = _body(t, "void bleuart_rx_overflow_callback(uint16_t conn_handle, uint16_t leftover)\n{")
    assert "bleQueueOnOverflow(g_bleQ)" in body
    assert "Serial" not in body and "printf" not in body


def test_drains_log_nocopy_and_overflow():
    assert "bleQueueTakeNoCopy(g_bleQ)" in _read("esp32/esp32_main.cpp")
    assert "no data from the stack" in _read("esp32/esp32_main.cpp")
    assert "bleQueueTakeOverflow(g_bleQ)" in _read("nrf52/nrf52_main.cpp")


def test_nrf52_producer_checks_send_and_stamps():
    t = _read("nrf52/nrf52_ble.cpp")
    _no_ignored_send(t, "nrf52_ble.cpp")
    body = _body(t, "void bleuart_rx_callback(uint16_t conn_handle)\n{")
    assert "bleItemStamp(item, g_bleQ)" in body
    assert re.search(r"bleQueueOnSend\(g_bleQ,\s*bleQueue != NULL && xQueueSend\(bleQueue", body)


def _drain_checks_before_apply(t, rel):
    i = t.index("xQueueReceive(bleQueue")
    j = t.index("readPhoneCommand(bleItem.data", i)
    seg = t[i:j]
    assert "bleQueueAccept(g_bleQ, bleItem)" in seg, f"{rel}: drain applies without the generation check"
    assert "continue" in seg, f"{rel}: a stale item must be skipped"


def test_esp32_drain_checks_generation_before_apply():
    _drain_checks_before_apply(_read("esp32/esp32_main.cpp"), "esp32_main.cpp")


def test_nrf52_drain_checks_generation_before_apply():
    _drain_checks_before_apply(_read("nrf52/nrf52_main.cpp"), "nrf52_main.cpp")


def test_drains_log_the_counters():
    for rel in ("esp32/esp32_main.cpp", "nrf52/nrf52_main.cpp"):
        t = _read(rel)
        assert "bleQueueTakeDropped(g_bleQ)" in t and "phone frame(s) dropped: queue full" in t, rel
        assert "bleQueueTakeStale(g_bleQ)" in t, rel


def test_generation_bumped_in_connect_and_disconnect():
    esp = _read("esp32/esp32_main.cpp")
    reset = _body(esp, "static inline void bleSessionResetAll()")
    assert "bleQueueNewSession(g_bleQ)" in reset
    # both NimBLE callbacks go through bleSessionResetAll
    assert _body(esp, "void onConnect(NimBLEServer*", "\n    }").count("bleSessionResetAll()") == 1
    assert _body(esp, "void onDisconnect(NimBLEServer*", "\n    }").count("bleSessionResetAll()") == 1
    nrf = _read("nrf52/nrf52_ble.cpp")
    assert "bleQueueNewSession(g_bleQ)" in _body(nrf, "void connect_callback(uint16_t conn_handle)\n{")
    assert "bleQueueNewSession(g_bleQ)" in _body(nrf, "void disconnect_callback(uint16_t conn_handle, uint8_t reason)\n{")


def test_queue_item_defined_once_in_ble_session_h():
    defs = []
    for p in sorted(SRC.rglob("*")):
        if p.suffix in (".cpp", ".h", ".c", ".hpp", ".ino") and p.is_file():
            if re.search(r"^\s*struct\s+BleQueueItem\s*\{", p.read_text(errors="replace"), re.M):
                defs.append(p.relative_to(SRC).as_posix())
    assert defs == ["ble_session.h"], f"BleQueueItem must be defined once in ble_session.h: {defs}"


def test_esp32_tx_characteristic_has_no_write_property():
    # F6 (FORMAL-02): the phone only subscribes to TX (6E400003); a WRITE property there was an unused entry point.
    t = _read("esp32/esp32_main.cpp")
    start = t.index("createCharacteristic(\n                        CHARACTERISTIC_UUID_TX")
    end = t.index(");", start)
    props = "\n".join(ln.split("//")[0] for ln in t[start:end].splitlines())
    assert "NIMBLE_PROPERTY::WRITE" not in props
    assert "NIMBLE_PROPERTY::NOTIFY" in props
