"""Tests for tools/remote_cmd.py (RM1 remote-management wire format, issue #1189)."""

from __future__ import annotations

import hashlib
import hmac
import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import remote_cmd as rc  # noqa: E402

VECTORS = json.loads((Path(__file__).with_name("remote_cmd_vectors.json")).read_text())
DST, SRC, PW = "DK5EN-90", "DK5EN-1", "secret"


def test_rfc4231_case2_through_module_hmac() -> None:
    mac = rc.hmac_sha256(b"Jefe", b"what do ya want for nothing?")
    assert mac.hex() == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"


def test_vector_counts() -> None:
    assert len(VECTORS["commands"]) >= 12
    assert len(VECTORS["replies"]) >= 4


@pytest.mark.parametrize("v", VECTORS["commands"], ids=lambda v: v["dm_text"])
def test_command_vector(v: dict) -> None:
    assert rc.derive_key(v["passwd"]).hex() == v["key_hex"]
    assert rc.canonical(v["dst"], v["src"], v["ctr"], v["cmd"], v["args"]) == v["canonical"]
    assert rc.tag(bytes.fromhex(v["key_hex"]), v["canonical"]) == v["tag"]
    dm = rc.build_command(v["dst"], v["src"], v["ctr"], v["cmd"], v["args"], v["passwd"])
    assert dm == v["dm_text"]
    assert dm.endswith(" " + v["tag"])
    # independent recomputation with plain hashlib/hmac
    key = hashlib.sha256(v["passwd"].rstrip(" ").encode()).digest()
    assert hmac.new(key, v["canonical"].encode(), hashlib.sha256).hexdigest()[:16] == v["tag"]


@pytest.mark.parametrize("v", VECTORS["replies"], ids=lambda v: v["reply_text"])
def test_reply_vector(v: dict) -> None:
    assert rc.reply_canonical(v["dst"], v["src"], v["ctr"], v["result"]) == v["reply_canonical"]
    assert rc.tag(bytes.fromhex(v["key_hex"]), v["reply_canonical"]) == v["reply_tag"]
    assert rc.build_reply(v["dst"], v["src"], v["ctr"], v["result"], v["passwd"]) == v["reply_text"]
    r = rc.verify_reply(v["reply_text"], v["dst"], v["src"], v["passwd"])
    assert r is not None and r["ctr"] == v["ctr"] and r["result"] == v["result"]


def test_vector_file_is_current() -> None:
    assert rc.generate_vectors() == VECTORS


def test_allowlist_command_count() -> None:
    assert len(rc.ALLOWLIST) == 22
    assert rc.ALLOWLIST["led"] == ("on", "off")


def test_vector_coverage() -> None:
    cmds = VECTORS["commands"]
    assert any(v["passwd"] != v["passwd"].rstrip(" ") for v in cmds)  # trailing spaces
    assert any(len(v["passwd"]) == 14 for v in cmds)
    assert any(v["cmd"] == "sync" and v["ctr"] == 0 for v in cmds)
    assert any(v["ctr"] == 4294967295 for v in cmds)
    assert any(v["cmd"] == "setout" and v["args"] == "a2 on" for v in cmds)
    assert any(v["cmd"] == "setout" and v["args"] == "b7 off" for v in cmds)
    assert any(v["cmd"] == "led" and v["args"] == "on" for v in cmds)
    assert any(v["cmd"] == "led" and v["args"] == "off" for v in cmds)
    # trailing spaces do not change the key
    assert rc.derive_key("secret   ") == rc.derive_key("secret")


def test_dst_and_src_are_bound() -> None:
    base = rc.build_command(DST, SRC, 1, "reboot", "", PW)
    assert rc.build_command("DK5EN-92", SRC, 1, "reboot", "", PW) != base
    assert rc.build_command(DST, "DK5EN-14", 1, "reboot", "", PW) != base


@pytest.mark.parametrize(
    "cmd,args",
    [
        ("reboot", ""), ("status", ""), ("sendpos", ""), ("sendtrack", ""),
        ("gps", "on"), ("gps", "off"), ("track", "on"), ("display", "off"),
        ("gateway", "on"), ("mesh", "off"), ("txpower", "2"), ("txpower", "22"),
        ("setout", "a2 on"), ("setout", "a0 off"), ("setout", "b7 off"), ("setout", "b0 on"),
        ("led", "on"), ("led", "off"),
    ],
)
def test_allowlisted_commands_build(cmd: str, args: str) -> None:
    text = rc.build_command(DST, SRC, 5, cmd, args, PW)
    assert text.startswith("RM1 5 " + cmd)


@pytest.mark.parametrize(
    "cmd,args",
    [
        ("cleanflash", ""), ("ota-update", ""), ("dfu", ""), ("deepsleep", ""),
        ("setcall", "DK5EN-9"), ("passwd", "x"), ("webpwd", "x"), ("btcode", "1"),
        ("setssid", "x"), ("setpwd", "x"), ("wifiset", "x"), ("updrepo", "x"),
        ("updchan", "x"), ("autoupdate", "on"), ("rm", "on"), ("remotemgmt", "on"), ("stor", ""),
        ("reboot", "--cleanflash"), ("reboot", "; reboot"), ("status", "{x"),
        ("status", "%41"), ("gps", "on --reboot"), ("txpower", "2;3"),
        ("unknown", ""), ("REBOOT", ""), ("gps", "ON"),
        ("gps", ""), ("gps", "maybe"), ("reboot", "now"), ("txpower", ""),
        ("txpower", "-1"), ("txpower", "x"), ("setout", "2"), ("setout", "2 1"), ("setout", "0 0"),
        ("setout", "a8 on"), ("setout", "c0 on"), ("setout", "b8 off"), ("setout", "a2 on1"),
        ("setout", "a2 1"), ("setout", "a2"), ("setout", "A2 on"), ("setout", "a2 ON"),
        ("setout", "a2  on"), ("setout", "a22 on"), ("setout", "a2 onoff"), ("gps", " on"), ("gps", "on "), ("", ""),
        ("reboot", "ä"), ("status", "\t"),
        ("led", ""), ("led", "blink"), ("led", "1"), ("led", "ON"), ("led", "on off"), ("led", " on"),
    ],
)
def test_blocked_or_malformed_refused(cmd: str, args: str) -> None:
    with pytest.raises(rc.RmError):
        rc.build_command(DST, SRC, 5, cmd, args, PW)


@pytest.mark.parametrize("ctr", [-1, 4294967296, 2**40, True, 1.5, "1"])
def test_ctr_out_of_range(ctr: object) -> None:
    with pytest.raises(rc.RmError):
        rc.build_command(DST, SRC, ctr, "reboot", "", PW)  # type: ignore[arg-type]


def test_ctr_bounds() -> None:
    assert rc.build_command(DST, SRC, 1, "reboot", "", PW).startswith("RM1 1 ")
    assert rc.build_command(DST, SRC, 4294967295, "reboot", "", PW).startswith("RM1 4294967295 ")
    with pytest.raises(rc.RmError):
        rc.build_command(DST, SRC, 0, "reboot", "", PW)  # 0 only for sync
    with pytest.raises(rc.RmError):
        rc.build_command(DST, SRC, 1, "sync", "", PW)  # sync is ctr 0 only
    assert rc.build_command(DST, SRC, 0, "sync", "", PW).startswith("RM1 0 sync ")


def test_passwd_limits() -> None:
    with pytest.raises(rc.RmError):
        rc.derive_key("")
    with pytest.raises(rc.RmError):
        rc.derive_key("   ")
    with pytest.raises(rc.RmError):
        rc.derive_key("x" * 15)
    assert len(rc.derive_key("x" * 14)) == 32


@pytest.mark.parametrize("call", ["", "A|B-1", "DK5EN 1", "{9}", "*", "DK5EN-90\n"])
def test_bad_calls_refused(call: str) -> None:
    with pytest.raises(rc.RmError):
        rc.build_command(call, SRC, 1, "reboot", "", PW)
    with pytest.raises(rc.RmError):
        rc.build_command(DST, call, 1, "reboot", "", PW)


def test_dm_text_is_firmware_safe() -> None:
    for v in VECTORS["commands"] + VECTORS["replies"]:
        text = v.get("dm_text") or v["reply_text"]
        assert text.startswith("RM1 ")
        assert not any(c in text for c in "{%;:\n\r")
        assert len(text) <= (140 if "reply_text" in v else 100)


GOOD = rc.build_reply(DST, SRC, 42, "ok rebooting", PW)


def test_verify_reply_accepts_good() -> None:
    r = rc.verify_reply(GOOD, DST, SRC, PW)
    assert r == {"ctr": 42, "status": "ok", "text": "rebooting", "result": "ok rebooting"}
    assert rc.verify_reply(GOOD, DST, SRC, PW, ctr=42) is not None
    err = rc.build_reply(DST, SRC, 7, "err range", PW)
    r2 = rc.verify_reply(err, DST, SRC, PW)
    assert r2 is not None and r2["status"] == "err" and r2["text"] == "range"


def test_verify_reply_rejects_tampering() -> None:
    assert rc.verify_reply(GOOD.replace("ok rebooting", "ok sent"), DST, SRC, PW) is None
    assert rc.verify_reply(GOOD.replace("ok ", "err "), DST, SRC, PW) is None
    assert rc.verify_reply(GOOD.replace("RM1 42 ", "RM1 43 "), DST, SRC, PW) is None
    assert rc.verify_reply(GOOD, DST, SRC, PW, ctr=43) is None  # wrong expected ctr
    assert rc.verify_reply(GOOD, "DK5EN-92", SRC, PW) is None  # wrong dst
    assert rc.verify_reply(GOOD, DST, "DK5EN-14", PW) is None  # wrong src
    assert rc.verify_reply(GOOD, DST, SRC, "other") is None  # wrong key
    assert rc.verify_reply(GOOD.upper(), DST, SRC, PW) is None
    assert rc.verify_reply(GOOD[:-1], DST, SRC, PW) is None
    assert rc.verify_reply(GOOD[:-1] + "0", DST, SRC, PW) is None


def test_verify_reply_rejects_garbage() -> None:
    for t in ["", "RM1", "RM1 42 ok", "hello world", "RM2 42 ok x " + "0" * 16,
              "RM1 x ok y " + "0" * 16, "RM1 42 maybe y " + "0" * 16,
              "RM1 4294967296 ok y " + "0" * 16]:
        assert rc.verify_reply(t, DST, SRC, PW) is None


def test_command_is_not_a_reply() -> None:
    cmd = rc.build_command(DST, SRC, 1, "status", "", PW)
    assert rc.verify_reply(cmd, DST, SRC, PW) is None


def test_cli_print_only(capsys: pytest.CaptureFixture[str]) -> None:
    assert rc.main(["--dst", DST, "--src", SRC, "--passwd", PW, "--ctr", "1", "reboot"]) == 0
    assert capsys.readouterr().out.strip() == VECTORS["commands"][0]["dm_text"]
    assert rc.main(["--dst", DST, "--src", SRC, "--passwd", PW, "sync"]) == 0
    assert rc.main(["--dst", DST, "--src", SRC, "--passwd", PW, "--ctr", "1", "dfu"]) == 2
    assert rc.main(["--dst", DST, "--src", SRC, "--passwd", PW, "--verify", GOOD]) == 0
    assert rc.main(["--dst", DST, "--src", SRC, "--passwd", PW, "--verify", GOOD + "0"]) == 1


def test_send_refuses_non_call_destination() -> None:
    for dst in ["TEST", "9", "*", "{9}"]:
        with pytest.raises(rc.RmError):
            rc.send_via_console("127.0.0.1", dst, "RM1 1 reboot 0000000000000000", "")


def test_lower_case_call_refused() -> None:
    """Advisor RM W1 N4: the node binds its configured (upper-case) call, so a
    lower-case dst/src would only produce a tag the node rejects."""
    for dst, src in (("dk5en-90", "DK5EN-1"), ("DK5EN-90", "dk5en-1")):
        with pytest.raises(rc.RmError):
            rc.build_command(dst, src, 5, "status", "", "secret")


EXT_CASES = [
    ("radio", "", True), ("radio", "x", False), ("sens", "", True), ("txq", "x", False), ("mbox", "", True),
    ("maxhop", "", True), ("name", "", True), ("name", "Martin", True), ("name", "1234567890123456789", True),
    ("name", "12345678901234567890", False), ("name", "a{b", False), ("name", "a|b", False), ("name", "a:b", False),
    ("atxt", "MeshCom Garten", True), ("atxt", "a" * 39, True), ("atxt", "a" * 40, False), ("atxt", "a|b", False),
    ("pos", "", True), ("pos", "48.40812 11.73812 492", True), ("pos", "1 2", False), ("pos", "a b c", False),
    ("mh", "0", True), ("mh", "999", True), ("mh", "1000", False), ("mh", "DK5EN-98", True),
    ("mh", "dk5en-98", True), ("mh", "DK5EN-98 x", False), ("mh", "", False),
    ("gps", "ON", False), ("setout", "A0 on", False), ("Radio", "", False),
]


def test_extended_shapes_match_the_firmware_table() -> None:
    for cmd, args, ok in EXT_CASES:
        try:
            rc.validate_command(cmd, args, 5)
            got = True
        except rc.RmError:
            got = False
        assert got is ok, (cmd, args)
