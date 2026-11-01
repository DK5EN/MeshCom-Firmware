"""webflash.py web login (WEB-AUTH-01): the password comes from .env, never from the operator.

Fake HTTP layer only; no network. A protected node serves the login page on "/"
and refuses /callfunction without the session cookie (401), like the firmware
(src/web_functions/web_functions.cpp, work_webpage).
"""
from __future__ import annotations

import hashlib
import hmac
import sys
import urllib.error
from pathlib import Path

import pytest

TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))

import webflash  # noqa: E402

OLD_BUILD = "build: "
NEW_BUILD = "build: NEW "
NONCE = "0123456789abcdef0123456789abcdef"
COOKIE = "f" * 32
LOGIN_PAGE = ('<html><title>HB9TST-9 - Meshcom</title><div id="head_layer">Meshcom 4.40a</div>'
              '<form><input type="password" id="nodepassword" name="nodepassword"></form></html>')


class FakeNode:
    def __init__(self, protected: bool, password: str = "s3cret", call: str = "HB9TST-9") -> None:
        self.protected, self.password, self.call = protected, password, call
        self.requests: list[tuple[str, str | None]] = []  # (url path, cookie)
        self.mode = "app"
        self.relogin_ok = True  # False: the login after the reboot is answered 403
        self.logins = 0
        self.trigger_401 = False  # /callfunction answers 401 although "/" is readable
        self.start_page_fails = False  # "/" with a valid session cookie answers 500
        self.login_page = LOGIN_PAGE

    def paths(self) -> list[str]:
        return [u.split("//", 1)[1].split("/", 1)[1] for u, _ in self.requests]

    def _app_page(self) -> str:
        return webflash._FIXTURE_HELTEC_V3

    def raw(self, url: str, timeout: float, cookie: str | None) -> tuple[int, str, str | None]:
        self.requests.append((url, cookie))
        path = "/" + url.split("//", 1)[1].split("/", 1)[1]
        if path == "/?loginnonce":
            self.logins += 1
            if self.logins > 1 and not self.relogin_ok:
                return 403, '{"ok":false,"err":"throttled"}', None
            return 200, NONCE, None
        if path.startswith("/?login="):
            n, _, mac = path[len("/?login="):].partition(".")
            want = hmac.new(self.password.encode(), n.encode(), hashlib.sha256).hexdigest()
            if n == NONCE and hmac.compare_digest(mac, want):
                return 200, '{"ok":true}', f"mcs={COOKIE}; Path=/; HttpOnly; SameSite=Strict"
            return 403, '{"ok":false,"err":"badpw"}', None
        return self.get(url, timeout, cookie, record=False) + (None,)

    def get(self, url: str, timeout: float = 5.0, cookie: str | None = None,
            record: bool = True) -> tuple[int, str]:
        if record:
            self.requests.append((url, cookie))
        path = "/" + url.split("//", 1)[1].split("/", 1)[1]
        if self.mode == "safeboot":
            if path == "/":
                return 200, webflash._FIXTURE_SAFEBOOT
            if path == "/ota/start" or path.startswith("/ota/start?"):
                self.mode = "flashed"
            return 200, "ok"
        if self.mode == "flashed":
            self.mode = "app_after"
        if path == "/" and self.mode in ("app", "app_after"):
            if self.protected and cookie != COOKIE:
                return 200, self.login_page
            if self.start_page_fails and cookie == COOKIE:
                return 500, ""
            page = self._app_page()
            return 200, page.replace(OLD_BUILD, NEW_BUILD) if self.mode == "app_after" else page
        if path == "/update":
            return 404, "not found"  # only the safeboot serves it
        if path == "/callfunction/?otaupdate":
            if self.trigger_401 or (self.protected and cookie != COOKIE):
                raise urllib.error.HTTPError(url, 401, "Unauthorized", {}, None)  # type: ignore[arg-type]
            self.mode = "safeboot"
            raise urllib.error.URLError("reset")
        return 200, "ok"

    def plain_get(self, url: str, timeout: float = 5.0) -> tuple[int, str]:
        return self.get(url, timeout, None)

    def post(self, url: str, path: Path) -> tuple[int, str]:
        self.requests.append((url, None))
        return 200, "ok"


FW_BYTES = b"\xe9firmware"


def run(node: FakeNode, tmp_path: Path, **kw):
    fw = tmp_path / "fw.bin"
    fw.write_bytes(FW_BYTES)
    return webflash.flash("node.local", fw, expect_hw="HELTEC_V3", poll_interval=0.0,
                          get=node.plain_get, poster=node.post, raw=node.raw, **kw)


def env_file(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, text: str, mode: int = 0o600) -> None:
    p = tmp_path / ".env"
    p.write_text(text)
    p.chmod(mode)
    monkeypatch.setenv("MC_ENV_FILE", str(p))
    for k in ("MC_CONSOLE_PW", "MC_WEBPW_HB9TST_9", "MC_PW_HB9TST_9"):
        monkeypatch.delenv(k, raising=False)


def test_unprotected_node_no_login_requests(tmp_path, monkeypatch):
    env_file(tmp_path, monkeypatch, "MC_WEBPW_HB9TST_9=s3cret\n")
    node = FakeNode(protected=False)
    res = run(node, tmp_path, call="HB9TST-9")
    assert res.ok, res.error
    assert not any("login" in p for p in node.paths())
    assert all(c is None for _, c in node.requests)


def test_protected_node_logs_in_with_correct_hmac_and_cookie(tmp_path, monkeypatch):
    env_file(tmp_path, monkeypatch, "MC_WEBPW_HB9TST_9=s3cret\n")
    node = FakeNode(protected=True)
    res = run(node, tmp_path, call="HB9TST-9")
    assert res.ok, res.error
    mac = hmac.new(b"s3cret", NONCE.encode(), hashlib.sha256).hexdigest()
    assert node.paths().count(f"?login={NONCE}.{mac}") == 2  # before and after the reboot
    assert node.paths().count("?loginnonce") == 2
    trig = [c for u, c in node.requests if u.endswith("/callfunction/?otaupdate")]
    assert trig == [COOKIE]


def test_call_taken_from_start_page_title(tmp_path, monkeypatch):
    env_file(tmp_path, monkeypatch, "MC_WEBPW_HB9TST_9=s3cret\n")
    node = FakeNode(protected=True)
    res = run(node, tmp_path)  # no call arg, host "node.local" is no callsign
    assert res.ok, res.error


def test_webpw_preferred_over_pw(tmp_path, monkeypatch):
    env_file(tmp_path, monkeypatch, "MC_PW_HB9TST_9=console\nMC_WEBPW_HB9TST_9=s3cret\n")
    node = FakeNode(protected=True, password="s3cret")
    assert run(node, tmp_path, call="HB9TST-9").ok


def test_fallback_to_pw(tmp_path, monkeypatch):
    env_file(tmp_path, monkeypatch, "MC_PW_HB9TST_9=s3cret\n")
    node = FakeNode(protected=True, password="s3cret")
    assert run(node, tmp_path, call="HB9TST-9").ok


def test_missing_key_errors_before_trigger(tmp_path, monkeypatch):
    env_file(tmp_path, monkeypatch, "MC_PW_OTHER_1=x\n")
    node = FakeNode(protected=True)
    res = run(node, tmp_path, call="HB9TST-9")
    assert not res.ok
    assert "MC_WEBPW_HB9TST_9" in (res.error or "")
    assert not any("otaupdate" in p or "login" in p for p in node.paths())


def test_wrong_password_one_attempt(tmp_path, monkeypatch):
    env_file(tmp_path, monkeypatch, "MC_WEBPW_HB9TST_9=wrong\n")
    node = FakeNode(protected=True, password="s3cret")
    res = run(node, tmp_path, call="HB9TST-9")
    assert not res.ok
    assert "badpw" in (res.error or "")
    assert sum(p.startswith("?login=") for p in node.paths()) == 1
    assert not any("otaupdate" in p for p in node.paths())
    assert "wrong" not in (res.error or "")


def test_password_never_in_result(tmp_path, monkeypatch):
    env_file(tmp_path, monkeypatch, "MC_WEBPW_HB9TST_9=s3cret\n")
    node = FakeNode(protected=True)
    res = run(node, tmp_path, call="HB9TST-9")
    assert "s3cret" not in repr(res)


def test_post_reboot_login_verifies_build(tmp_path, monkeypatch):
    env_file(tmp_path, monkeypatch, "MC_WEBPW_HB9TST_9=s3cret\n")
    node = FakeNode(protected=True)
    res = run(node, tmp_path, call="HB9TST-9")
    assert res.ok and res.stage == "done", res.error
    assert (res.after or {}).get("build", "").startswith("NEW ")
    assert node.paths().count("?loginnonce") == 2  # one before, one after the reboot


def test_post_reboot_login_refused_is_done_but_unverified(tmp_path, monkeypatch):
    env_file(tmp_path, monkeypatch, "MC_WEBPW_HB9TST_9=s3cret\n")
    node = FakeNode(protected=True)
    node.relogin_ok = False
    res = run(node, tmp_path, call="HB9TST-9")
    assert res.ok and res.stage == "done_unverified"
    assert "unverified" in (res.note or "")
    assert node.paths().count("?loginnonce") == 2  # no retry
    assert not any(p.startswith("?login=") for p in node.paths()[-1:])
    assert webflash.exit_code(res) == 1


# --- rework: resume path, 401 on the trigger, unreadable start page, .env, CLI, pinned sequence ---


def test_resume_protected_with_key_verifies_build(tmp_path, monkeypatch):
    env_file(tmp_path, monkeypatch, "MC_WEBPW_HB9TST_9=s3cret\n")
    node = FakeNode(protected=True)
    node.mode = "safeboot"
    res = run(node, tmp_path, call="HB9TST-9", reboot_poll_s=0.3)
    assert res.ok and res.stage == "done", (res.stage, res.error)
    assert (res.after or {}).get("build", "").startswith("NEW ")
    assert node.paths().count("?loginnonce") == 1  # only the login that verifies the build
    assert not any("otaupdate" in p for p in node.paths())


def test_resume_protected_call_from_login_page_title(tmp_path, monkeypatch):
    env_file(tmp_path, monkeypatch, "MC_WEBPW_HB9TST_9=s3cret\n")
    node = FakeNode(protected=True)
    node.mode = "safeboot"
    res = run(node, tmp_path, reboot_poll_s=0.3)  # no --call, host is no callsign
    assert res.ok and res.stage == "done", (res.stage, res.error)


def test_resume_protected_without_key_is_done_unverified(tmp_path, monkeypatch):
    env_file(tmp_path, monkeypatch, "MC_PW_OTHER_1=x\n")
    node = FakeNode(protected=True)
    node.mode = "safeboot"
    res = run(node, tmp_path, call="HB9TST-9", reboot_poll_s=0.3)
    assert res.ok and res.stage == "done_unverified", (res.stage, res.error)
    assert "MC_WEBPW_HB9TST_9" in (res.note or "")
    assert not any("login" in p for p in node.paths())  # no login without a key
    assert webflash.exit_code(res) == 1


def test_trigger_401_is_login_failed_without_safeboot_wait(tmp_path, monkeypatch):
    env_file(tmp_path, monkeypatch, "MC_WEBPW_HB9TST_9=s3cret\n")
    node = FakeNode(protected=False)
    node.trigger_401 = True
    res = run(node, tmp_path, call="HB9TST-9", safeboot_poll_s=0.3)
    assert not res.ok and res.stage == "login_failed", (res.stage, res.error)
    assert "MC_WEBPW_HB9TST_9" in (res.error or "")
    assert "update" not in node.paths()  # never polled for the safeboot server


def test_start_page_unreadable_after_login_is_login_failed(tmp_path, monkeypatch):
    env_file(tmp_path, monkeypatch, "MC_WEBPW_HB9TST_9=s3cret\n")
    node = FakeNode(protected=True)
    node.start_page_fails = True
    res = run(node, tmp_path, call="HB9TST-9", safeboot_poll_s=0.3)
    assert not res.ok and res.stage == "login_failed", (res.stage, res.error)
    assert "start page" in (res.error or "")
    assert not any("otaupdate" in p for p in node.paths())


def test_env_file_mode_644_aborts_before_trigger(tmp_path, monkeypatch):
    env_file(tmp_path, monkeypatch, "MC_WEBPW_HB9TST_9=s3cret\n", mode=0o644)
    node = FakeNode(protected=True)
    res = run(node, tmp_path, call="HB9TST-9")
    assert not res.ok
    assert "chmod 600" in (res.error or "")
    assert "s3cret" not in (res.error or "")
    assert not any("otaupdate" in p or "login" in p for p in node.paths())


def test_no_callsign_anywhere_errors_without_login_requests(tmp_path, monkeypatch):
    env_file(tmp_path, monkeypatch, "MC_WEBPW_HB9TST_9=s3cret\n")
    node = FakeNode(protected=True)
    node.login_page = LOGIN_PAGE.replace("<title>HB9TST-9 - Meshcom</title>", "")
    res = run(node, tmp_path)
    assert not res.ok and res.stage == "login_key_missing"
    assert "--call" in (res.error or "")
    assert not any("login" in p or "otaupdate" in p for p in node.paths())


def test_unprotected_request_sequence_is_pinned(tmp_path, monkeypatch):
    env_file(tmp_path, monkeypatch, "MC_WEBPW_HB9TST_9=s3cret\n")
    node = FakeNode(protected=False)
    res = run(node, tmp_path, call="HB9TST-9")
    assert res.ok and res.stage == "done", res.error
    md5 = hashlib.md5(FW_BYTES).hexdigest()
    assert node.paths() == ["", "callfunction/?otaupdate", "update",
                            f"ota/start?mode=fr&hash={md5}", "ota/upload", ""]
    assert all(c is None for _, c in node.requests)


@pytest.fixture
def cli(tmp_path, monkeypatch):
    """webflash.main() against a FakeNode; returns (node, argv)."""
    def make(node: FakeNode) -> list[str]:
        fw = tmp_path / "fw.bin"
        fw.write_bytes(FW_BYTES)
        real = webflash.flash
        monkeypatch.setattr(webflash, "flash", lambda host, f, **kw: real(
            host, f, get=node.plain_get, poster=node.post, raw=node.raw, poll_interval=0.0, **kw))
        return ["node.local", "--bin", str(fw), "--call", "HB9TST-9"]
    return make


def _secrets_absent(text: str, password: str) -> None:
    mac = hmac.new(password.encode(), NONCE.encode(), hashlib.sha256).hexdigest()
    for secret in (password, mac, "?login=", COOKIE):
        assert secret not in text


def test_cli_done_exit_0_no_secrets(tmp_path, monkeypatch, capsys, cli):
    env_file(tmp_path, monkeypatch, "MC_WEBPW_HB9TST_9=s3cret\n")
    assert webflash.main(cli(FakeNode(protected=True))) == 0
    out = capsys.readouterr()
    assert "Done:" in out.out
    _secrets_absent(out.out + out.err, "s3cret")


def test_cli_done_unverified_exit_1_no_secrets(tmp_path, monkeypatch, capsys, cli):
    env_file(tmp_path, monkeypatch, "MC_WEBPW_HB9TST_9=s3cret\n")
    node = FakeNode(protected=True)
    node.relogin_ok = False
    assert webflash.main(cli(node)) == 1
    out = capsys.readouterr()
    assert "NOT verified" in out.out
    _secrets_absent(out.out + out.err, "s3cret")


def test_cli_failure_exit_1_no_secrets(tmp_path, monkeypatch, capsys, cli):
    env_file(tmp_path, monkeypatch, "MC_WEBPW_HB9TST_9=wrong\n")
    assert webflash.main(cli(FakeNode(protected=True, password="s3cret"))) == 1
    out = capsys.readouterr()
    assert "error:" in out.out
    _secrets_absent(out.out + out.err, "wrong")
    _secrets_absent(out.out + out.err, "s3cret")


def test_result_line_says_unverified():
    res = webflash.OtaResult(ok=True, stage="done_unverified", fw_path="x", fw_size=1, md5="m",
                             after={"version": "4.40a", "build": "b", "hardware": "H"})
    assert res.line().startswith("OTA done, build unverified")
