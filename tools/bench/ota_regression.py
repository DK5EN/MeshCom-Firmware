#!/usr/bin/env python3
"""TM-40 OTA flashing regression: prove a WiFi node re-flashes itself over the
air, unattended, and comes back unchanged.

Holds one USB serial session open for the whole run (the node resets on
open -- that is the run's own first, expected boot) and drives the WiFi OTA
flow through `tools/webflash.py`'s `flash()` against the node's HTTP server,
while watching the serial log for the safeboot reboot and the app's own
reboot. Before and after the flash it records a settings snapshot purely over
serial (`--info`, `--maxhop`, `--lora`, `--wifistat`) -- a channel the OTA
itself cannot touch -- and compares the two.

    python3 tools/bench/ota_regression.py \\
        --port /dev/cu.usbserial-573C0005841 --env ttgo_tbeam \\
        --ip 192.168.1.90

    python3 tools/bench/ota_regression.py --parse-only tools/bench/runs/ota_20260830-190700/serial.log

Asserted (see `run()`):
  * the node is reachable and reports the expected hardware before the OTA
    starts (`--expect-hw`, from `webflash.ENV_HARDWARE[env]`)
  * safeboot prints "OTA update finished successfully!" within the reboot
    watch window
  * the app prints `[BOOT];ready` after the OTA (a real reboot into the new
    image, not just an HTTP server that happened to answer)
  * `--wifistat` reports a WLAN IP again after the reboot
  * post-flash `--info` reports the same hardware string as before
  * post-flash `--info`/`--maxhop`/`--lora`/`--wifistat` settings match the
    pre-flash snapshot (webserver flag, callsign, maxhop, LoRa RF params,
    WLAN SSID) -- with `--settings-check` a mismatch fails the run, without
    it a mismatch is a warning only
  * no more than 2 `rst:0x` resets after the OTA trigger (app->safeboot,
    safeboot->app; a 3rd is a crash loop)

Because a bench run typically re-flashes the exact bytes already running (no
other firmware build to hand -- see the module docstring in `webflash.py`),
"the version reads the same" is expected and is not treated as a failure by
itself: the proof is the full safeboot->upload->reboot round trip completing
and the node coming back in the same state it left, not a version bump. The
md5 sent to `/ota/start` (recorded in the summary as `ota_md5`) is what the
firmware's own `Update.setMD5()` verifies the upload against
(src/safeboot/ElegantOTA.cpp) -- a mismatched upload is rejected before it
ever reaches the flash, so an `ok` result already proves that specific image
was written.

Markers consumed, all raw Serial.printf (not printfdeb -- see
docs/../DEBUG_MSG note in bench memory), so they survive --debug off:
  rst:0x..                                    ESP32 ROM, a reset after boot #1
  [BOOT];ready;ms;N;ip;X                      esp32_main.cpp
  [WIFI];event;got_ip;ms;N                    udp_functions.cpp
  [WIFI];stat;ssid;S;localip;IP;...           udp_functions.cpp (--wifistat)
  [MAXHOP];text;N;pos;M                       command_functions.cpp (--maxhop)
Safeboot prints plain text, no markers (src/safeboot/main.cpp, ElegantOTA.cpp):
  "Connected to WiFi", "OTA update started!", "OTA update finished successfully!"
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import threading
import time
from pathlib import Path
from typing import Any, Callable, Optional

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover - parse-only works without it
    serial = None  # type: ignore[assignment]

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import webflash  # noqa: E402

BAUD = 115200
WALL_FMT = "%Y-%m-%d %H:%M:%S"
RUN_PREFIX = "ota"

# --- regexes ----------------------------------------------------------------

RE_RESET = re.compile(r"rst:0x[0-9a-f]+")
RE_BOOT_READY = re.compile(r"\[BOOT\];ready;ms;(?P<ms>\d+);ip;(?P<ip>\d+)")
RE_WIFI_GOT_IP = re.compile(r"\[WIFI\];event;got_ip;ms;(?P<ms>\d+)")
RE_CLIENT_STARTED = re.compile(r"CLIENT STARTED")

RE_SAFEBOOT_OTA_OK = re.compile(r"OTA update finished successfully!")
RE_SAFEBOOT_OTA_ERR = re.compile(r"There was an error during OTA update!")

RE_INFO_HEAD = re.compile(r"--MeshCom\s*(?P<ver>[^\s(]+)\s*\(build:\s*(?P<build>[^)]+)\)")
RE_INFO_CALL = re.compile(
    r"\.\.\.Call:\s*<(?P<call>[^>]*)>\s*\.\.\.ID\s*(?P<id>[0-9A-Fa-f]+)\s*"
    r"\.\.\.NODE\s*(?P<node>\d+)\s*<(?P<hw>[^>]*)>")
RE_INFO_WEBSERVER = re.compile(r"\.\.\.Webserver\s+(?P<val>on|off)")
RE_MAXHOP = re.compile(r"\[MAXHOP\];text;(?P<text>\d+);pos;(?P<pos>\d+)")
RE_LORA = re.compile(
    r"LoRa RF-Frequ:\s*<(?P<freq>[\d.]+)\s*MHz>.*?"
    r"LoRa RF-Power:\s*<(?P<power>-?\d+)\s*dBm>.*?"
    r"LoRa RF-BW:\s*<(?P<bw>[\d.]+)\s*kHz>.*?"
    r"LoRa RF-SF:\s*<(?P<sf>\d+)>.*?"
    r"LoRa RF-CR:\s*<4/(?P<cr>\d+)>", re.S)
RE_WIFISTAT = re.compile(r"\[WIFI\];stat;ssid;(?P<ssid>[^;]*);localip;(?P<ip>[\d.]+)")

# settings fields compared before/after (see `diff_settings`)
SETTINGS_KEYS = ["call", "hw_idx", "hardware", "webserver", "maxhop_text", "maxhop_pos",
                  "lora_freq", "lora_power", "lora_bw", "lora_sf", "lora_cr", "wifi_ssid"]


def wall_now(ts: Optional[float] = None) -> str:
    return time.strftime(WALL_FMT, time.localtime(ts if ts is not None else time.time()))


def strip_wall_prefix(line: str) -> str:
    """Undo the `f"{wall_now(ts)} {line}"` (or "## >> cmd") the log writer used."""
    return line[20:] if len(line) > 20 and line[19] == " " else line


# --- serial session -----------------------------------------------------------


def real_opener(port: str) -> Any:  # pragma: no cover - needs hardware
    if serial is None:
        raise RuntimeError("pyserial is required to run against hardware "
                            "(parse-only works without it)")
    s = serial.Serial()
    s.port = port
    s.baudrate = BAUD
    s.timeout = 0.2
    # T-Beam v1.2 (CH9102) and the other ESP32 bench nodes reset on open when
    # DTR/RTS both stay low -- that reset is this run's own expected boot #1.
    s.dtr = False
    s.rts = False
    s.open()
    return s


class SerialSession:
    """Held-open USB session: one background reader, a timestamped line log,
    and a paced `send()` (the firmware consumes one char per loop() pass)."""

    def __init__(self, port: str, log_path: Path, opener: Callable[[str], Any] = real_opener):
        self.port = port
        self.opener = opener
        self.lines: list[tuple[float, str]] = []
        self.lock = threading.Lock()
        self.stop_evt = threading.Event()
        self.thread: Optional[threading.Thread] = None
        self.ser: Any = None
        self.error: Optional[str] = None
        self.log_fh = open(log_path, "a", buffering=1, encoding="utf-8")

    def start(self) -> None:
        try:
            self.ser = self.opener(self.port)
        except Exception as e:  # noqa: BLE001
            self.error = str(e)
            return
        self.thread = threading.Thread(target=self._run, daemon=True, name="ota-serial")
        self.thread.start()

    def _run(self) -> None:
        buf = b""
        while not self.stop_evt.is_set():
            try:
                chunk = self.ser.read(4096)
            except Exception as e:  # noqa: BLE001
                with self.lock:
                    self.error = str(e)
                return
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.decode("utf-8", errors="replace").rstrip("\r")
                ts = time.time()
                self.log_fh.write(f"{wall_now(ts)} {line}\n")
                with self.lock:
                    self.lines.append((ts, line))

    def close(self) -> None:
        self.stop_evt.set()
        if self.thread:
            self.thread.join(timeout=3)
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:  # noqa: BLE001
                pass
        self.log_fh.close()

    def send(self, cmd: str) -> None:
        if self.ser is None:
            raise RuntimeError("serial not open")
        for ch in cmd:
            self.ser.write(ch.encode())
            self.ser.flush()
            time.sleep(0.02)
        self.ser.write(b"\n")
        self.ser.flush()
        ts = time.time()
        self.log_fh.write(f"{wall_now(ts)} ## >> {cmd}\n")
        with self.lock:
            self.lines.append((ts, f"## >> {cmd}"))

    def lines_since(self, ts: float) -> list[str]:
        with self.lock:
            return [line for (t, line) in self.lines if t >= ts]

    def wait_for(self, pattern: "re.Pattern[str]", timeout: float,
                 since_ts: float = 0.0) -> Optional["re.Match[str]"]:
        deadline = time.time() + timeout
        while True:
            with self.lock:
                for t, line in self.lines:
                    if t >= since_ts:
                        m = pattern.search(line)
                        if m:
                            return m
            if time.time() >= deadline:
                return None
            time.sleep(0.1)


# --- settings snapshot ---------------------------------------------------------


def extract_snapshot(blob: str) -> tuple[dict[str, str], list[str]]:
    """Pull the settings fields out of a text blob -- live-collected command
    answers or a slice of a raw serial.log; both are plain text either way."""
    snap: dict[str, str] = {}
    problems: list[str] = []

    m = RE_INFO_HEAD.search(blob)
    if m:
        snap["version"] = m.group("ver")
        snap["build"] = m.group("build").strip()
    else:
        problems.append("--info: could not parse the --MeshCom header line")

    m = RE_INFO_CALL.search(blob)
    if m:
        snap["call"] = m.group("call")
        snap["node_id"] = m.group("id")
        snap["hw_idx"] = m.group("node")
        snap["hardware"] = m.group("hw")
    else:
        problems.append("--info: could not parse the Call/NODE/hardware line")

    m = RE_INFO_WEBSERVER.search(blob)
    if m:
        snap["webserver"] = m.group("val")
    else:
        problems.append("--info: no '...Webserver on/off' line")

    m = RE_MAXHOP.search(blob)
    if m:
        snap["maxhop_text"] = m.group("text")
        snap["maxhop_pos"] = m.group("pos")
    else:
        problems.append("--maxhop: no [MAXHOP] answer")

    m = RE_LORA.search(blob)
    if m:
        snap["lora_freq"] = m.group("freq")
        snap["lora_power"] = m.group("power")
        snap["lora_bw"] = m.group("bw")
        snap["lora_sf"] = m.group("sf")
        snap["lora_cr"] = m.group("cr")
    else:
        problems.append("--lora: could not parse the LoRa RF-* lines")

    m = RE_WIFISTAT.search(blob)
    if m:
        snap["wifi_ssid"] = m.group("ssid")
        snap["wifi_localip"] = m.group("ip")
    else:
        problems.append("--wifistat: no [WIFI];stat answer")

    return snap, problems


def send_and_collect(sess: SerialSession, cmd: str, terminator: "re.Pattern[str]",
                      timeout: float, settle: float = 0.3) -> str:
    """Send `cmd`, wait for `terminator` to show up in the reply, return
    everything received since the send (joined lines)."""
    since = time.time()
    sess.send(cmd)
    deadline = since + timeout
    while time.time() < deadline:
        blob = "\n".join(sess.lines_since(since))
        if terminator.search(blob):
            time.sleep(settle)  # let trailing bytes of the same block land
            break
        time.sleep(0.1)
    return "\n".join(sess.lines_since(since))


def capture_snapshot(sess: SerialSession, timeout: float = 8.0) -> tuple[dict[str, str], list[str]]:
    """Send --info/--maxhop/--lora/--wifistat over the live session and parse
    the combined answer."""
    blobs = [
        send_and_collect(sess, "--info", RE_INFO_WEBSERVER, timeout),
        send_and_collect(sess, "--maxhop", RE_MAXHOP, timeout),
        send_and_collect(sess, "--lora", re.compile(r"LoRa RF-CR"), timeout),
        send_and_collect(sess, "--wifistat", RE_WIFISTAT, timeout),
    ]
    return extract_snapshot("\n\n".join(blobs))


def diff_settings(before: dict[str, str], after: dict[str, str]) -> list[str]:
    diffs = []
    for k in SETTINGS_KEYS:
        bv, av = before.get(k), after.get(k)
        if bv is not None and av is not None and bv != av:
            diffs.append(f"{k}: {bv!r} -> {av!r}")
    return diffs


# --- reporting -----------------------------------------------------------------


def render_summary(result: dict[str, Any]) -> str:
    out: list[str] = []
    out.append(f"ota_regression  {result.get('rundir') or result.get('source', '')}")
    if result.get("started"):
        out.append(f"started            {result['started']}")
    if result.get("port"):
        out.append(f"port               {result['port']}")
    if result.get("env"):
        out.append(f"env                {result['env']}  expect_hw {result.get('expect_hardware', '?')}")
    if result.get("target"):
        out.append(f"target             http://{result['target']}/")
    if result.get("bin"):
        out.append(f"firmware           {result['bin']}  md5 {result.get('ota_md5', '?')}")

    ota = result.get("ota")
    if ota:
        line = "ok" if ota.get("ok") else f"FAILED at {ota.get('stage')}"
        out.append(f"ota                {line}" + (f"  ({ota['error']})" if ota.get("error") else ""))
        t = ota.get("timings") or {}
        if t:
            out.append("  timings          " + "  ".join(f"{k}={v:.1f}s" for k, v in t.items()))

    b = result.get("before") or {}
    a = result.get("after") or {}
    if b or a:
        out.append(f"{'field':<14}{'before':<26}after")
        for k in sorted(set(b) | set(a)):
            out.append(f"{k:<14}{str(b.get(k, '-')):<26}{a.get(k, '-')}")

    if result.get("resets_after_ota") is not None:
        out.append(f"resets after OTA   {result['resets_after_ota']}")
    if "boot_ready" in result:
        out.append(f"[BOOT];ready seen  {result['boot_ready']}")
    if "wifi_rejoined" in result:
        out.append(f"WLAN rejoined      {result['wifi_rejoined']}")

    for w in result.get("warnings", []):
        out.append(f"  WARN {w}")
    out.append(f"verdict            {result.get('verdict', '?')}")
    for f in result.get("fails", []):
        out.append(f"  !! {f}")
    return "\n".join(out)


def write_result(rundir: Path, result: dict[str, Any]) -> None:
    (rundir / "summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    (rundir / "summary.txt").write_text(render_summary(result) + "\n", encoding="utf-8")


def default_runs_dir() -> Path:
    return Path(__file__).resolve().parent / "runs"


# --- the run ---------------------------------------------------------------


def run(args: argparse.Namespace, opener: Callable[[str], Any] = real_opener,
        get: webflash.GetFn = webflash.http_get,
        poster: webflash.PostFn = webflash.upload_multipart) -> int:
    """Drive one OTA regression run. Returns the process exit code (0/1)."""
    runs_dir = Path(args.runs_dir) if args.runs_dir else default_runs_dir()
    runs_dir.mkdir(parents=True, exist_ok=True)
    rundir = runs_dir / f"{RUN_PREFIX}_{time.strftime('%Y%m%d-%H%M%S')}"
    rundir.mkdir(parents=True, exist_ok=True)

    target = args.ip or args.host
    fw = webflash.resolve_firmware(args.env, args.bin)
    expect_hw = webflash.ENV_HARDWARE.get(args.env, args.env.upper())

    fails: list[str] = []
    warnings: list[str] = []
    result: dict[str, Any] = dict(
        port=args.port, env=args.env, host=args.host, ip=args.ip, target=target,
        bin=str(fw), expect_hardware=expect_hw, started=wall_now(), rundir=str(rundir),
    )

    sess = SerialSession(args.port, rundir / "serial.log", opener=opener)
    sess.start()
    if sess.error:
        result.update(verdict="FAIL", fails=[f"serial open failed: {sess.error}"], warnings=[])
        write_result(rundir, result)
        print(render_summary(result))
        return 1

    t_open = time.time()
    # ESP32 bench nodes reset on open: wait for the boot marker before typing.
    if not sess.wait_for(RE_CLIENT_STARTED, args.boot_wait_s, since_ts=t_open):
        warnings.append(f"did not see CLIENT STARTED after opening the port within "
                         f"{args.boot_wait_s:.0f}s -- continuing anyway")
    time.sleep(1.0)

    print("Reading pre-flash settings snapshot over serial ...")
    before, problems = capture_snapshot(sess, timeout=args.snapshot_timeout_s)
    for p in problems:
        warnings.append(f"pre-flash snapshot: {p}")
    result["before"] = before

    if not fw.is_file():
        fails.append(f"firmware not found: {fw}")
        result.update(verdict="FAIL", fails=fails, warnings=warnings)
        sess.close()
        write_result(rundir, result)
        print(render_summary(result))
        return 1

    md5 = hashlib.md5(fw.read_bytes()).hexdigest()
    result["ota_md5"] = md5

    def on_phase(name: str, detail: str) -> None:
        print(f"[ota] {name}" + (f": {detail}" if detail else ""))

    def on_wait(msg: str) -> None:
        print(f"  ... {msg}")

    print(f"Starting OTA against http://{target}/ (env {args.env}, expect hw {expect_hw}) ...")
    t_ota = time.time()
    ota = webflash.flash(target, fw, expect_hw=expect_hw, force=args.force,
                          safeboot_poll_s=args.safeboot_poll_s, reboot_poll_s=args.reboot_poll_s,
                          get=get, poster=poster, on_phase=on_phase, on_wait=on_wait)
    result["ota"] = dict(ok=ota.ok, stage=ota.stage, error=ota.error, timings=ota.timings,
                          before=ota.before, after=ota.after, md5=ota.md5, fw_size=ota.fw_size)
    if not ota.ok:
        fails.append(f"OTA failed at stage {ota.stage}: {ota.error}")

    # The serial log is the witness of a real reboot -- an HTTP 200 alone
    # could in principle be a cached/proxy answer, so this is checked even
    # when webflash already reports failure (to see how far it actually got).
    print("Watching serial for the safeboot -> app reboot ...")
    safeboot_ok = sess.wait_for(RE_SAFEBOOT_OTA_OK, args.post_ota_timeout_s, since_ts=t_ota)
    if safeboot_ok is None:
        msg = ('safeboot did not print "OTA update finished successfully!" within '
               f"{args.post_ota_timeout_s:.0f}s")
        (fails if ota.ok else warnings).append(msg)

    boot_ready = sess.wait_for(RE_BOOT_READY, args.post_ota_timeout_s, since_ts=t_ota)
    result["boot_ready"] = boot_ready is not None
    if boot_ready is None:
        fails.append(f"no [BOOT];ready seen after the OTA within {args.post_ota_timeout_s:.0f}s "
                      "-- the app may not have come back up")

    got_ip = sess.wait_for(RE_WIFI_GOT_IP, args.post_ota_timeout_s, since_ts=t_ota)
    result["wifi_rejoined"] = got_ip is not None
    if got_ip is None:
        warnings.append(f"no [WIFI];event;got_ip seen after the OTA within "
                         f"{args.post_ota_timeout_s:.0f}s (checked again via --wifistat below)")

    resets_after = len(RE_RESET.findall("\n".join(sess.lines_since(t_ota))))
    result["resets_after_ota"] = resets_after
    if resets_after > 2:
        fails.append(f"{resets_after} rst:0x resets after the OTA trigger (2 expected: "
                      "app->safeboot, safeboot->app) -- possible crash loop")

    print("Reading post-flash settings snapshot over serial ...")
    after, problems2 = capture_snapshot(sess, timeout=args.snapshot_timeout_s)
    for p in problems2:
        warnings.append(f"post-flash snapshot: {p}")
    result["after"] = after

    if expect_hw and after.get("hardware") and not webflash.hardware_matches(after["hardware"], expect_hw):
        fails.append(f"post-flash hardware mismatch: {after.get('hardware')!r} != {expect_hw!r}")

    if not after.get("wifi_localip") or after.get("wifi_localip") == "0.0.0.0":
        fails.append("post-flash --wifistat reports no WLAN IP")

    diffs = diff_settings(before, after) if before and after else []
    result["settings_diffs"] = diffs
    if diffs:
        note = "settings changed across the OTA: " + "; ".join(diffs)
        (fails if args.settings_check else warnings).append(note)

    sess.close()

    result["warnings"] = warnings
    result["fails"] = fails
    result["verdict"] = "PASS" if not fails else "FAIL"
    result["total_s"] = time.time() - t_open
    write_result(rundir, result)
    print(render_summary(result))
    print(f"{rundir / 'summary.txt'}")
    return 0 if result["verdict"] == "PASS" else 1


# --- --parse-only ---------------------------------------------------------


def cmd_parse_only(args: argparse.Namespace) -> int:
    path = Path(args.parse_only)
    raw_lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    lines = [strip_wall_prefix(l) for l in raw_lines]

    split_idx: Optional[int] = None
    for i, line in enumerate(lines):
        if RE_SAFEBOOT_OTA_OK.search(line):
            split_idx = i  # last match wins if the log has more than one OTA
    if split_idx is None:
        print(f'error: no "OTA update finished successfully!" line in {path} '
              "-- cannot split it into a before/after snapshot")
        return 1

    before_blob = "\n".join(lines[:split_idx])
    after_blob = "\n".join(lines[split_idx:])
    before, problems1 = extract_snapshot(before_blob)
    after, problems2 = extract_snapshot(after_blob)
    warnings = [f"pre-flash snapshot: {p}" for p in problems1]
    warnings += [f"post-flash snapshot: {p}" for p in problems2]

    fails: list[str] = []
    boot_ready = RE_BOOT_READY.search(after_blob) is not None
    if not boot_ready:
        fails.append("no [BOOT];ready seen after the OTA in the log")
    got_ip = RE_WIFI_GOT_IP.search(after_blob) is not None

    resets_after = len(RE_RESET.findall(after_blob))
    if resets_after > 2:
        fails.append(f"{resets_after} rst:0x resets after the OTA (possible crash loop)")

    expect_hw = args.expect_hw
    if expect_hw and after.get("hardware") and not webflash.hardware_matches(after["hardware"], expect_hw):
        fails.append(f"post-flash hardware mismatch: {after.get('hardware')!r} != {expect_hw!r}")
    if not after.get("wifi_localip") or after.get("wifi_localip") == "0.0.0.0":
        fails.append("post-flash --wifistat reports no WLAN IP")

    diffs = diff_settings(before, after) if before and after else []
    if diffs:
        note = "settings changed across the OTA: " + "; ".join(diffs)
        (fails if args.settings_check else warnings).append(note)

    result: dict[str, Any] = dict(
        source=str(path), before=before, after=after, settings_diffs=diffs,
        resets_after_ota=resets_after, boot_ready=boot_ready, wifi_rejoined=got_ip,
        warnings=warnings, fails=fails, verdict="PASS" if not fails else "FAIL",
    )
    print(render_summary(result))
    if not args.no_write:
        out_dir = path.parent
        (out_dir / "summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        (out_dir / "summary.txt").write_text(render_summary(result) + "\n", encoding="utf-8")
    return 0 if result["verdict"] == "PASS" else 1


# --- CLI ------------------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port, e.g. /dev/cu.usbserial-573C0005841")
    ap.add_argument("--env", help="PlatformIO env, e.g. ttgo_tbeam, heltec_wifi_lora_32_V3, t_deck_plus")
    ap.add_argument("--host", default=None, help="node hostname (mDNS), e.g. dk5en-92.local")
    ap.add_argument("--ip", default=None, help="node IP address (from --wifistat's localip)")
    ap.add_argument("--bin", type=Path, default=None,
                     help="firmware.bin override (default .pio/build/<env>/firmware.bin)")
    ap.add_argument("--settings-check", action="store_true",
                     help="fail the run when the before/after settings snapshot differs "
                          "(default: report a mismatch as a warning only)")
    ap.add_argument("--force", action="store_true", help="skip webflash's hardware check")
    ap.add_argument("--runs-dir", default="", help=f"default: {default_runs_dir()}")
    ap.add_argument("--boot-wait-s", type=float, default=25.0,
                     help="how long to wait for CLIENT STARTED after opening the port")
    ap.add_argument("--snapshot-timeout-s", type=float, default=8.0,
                     help="per-command timeout while reading the settings snapshot")
    ap.add_argument("--safeboot-poll-s", type=float, default=webflash.SAFEBOOT_POLL_S,
                     help="webflash: how long to wait for the safeboot web server")
    ap.add_argument("--reboot-poll-s", type=float, default=webflash.REBOOT_POLL_S,
                     help="webflash: how long to wait for the app to answer HTTP again")
    ap.add_argument("--post-ota-timeout-s", type=float, default=60.0,
                     help="how long to watch serial for the safeboot/app reboot markers")
    ap.add_argument("--parse-only", metavar="SERIAL_LOG", default=None,
                     help="re-derive the verdict from an existing serial.log, no hardware")
    ap.add_argument("--no-write", action="store_true",
                     help="--parse-only: print only, do not rewrite summary.json/.txt")
    ap.add_argument("--expect-hw", default=None,
                     help="--parse-only: expected hardware string (default: none, skip the check)")
    return ap


def main(argv: Optional[list[str]] = None) -> int:
    args = build_parser().parse_args(argv)

    if args.parse_only:
        return cmd_parse_only(args)

    missing = [n for n, v in (("--port", args.port), ("--env", args.env)) if not v]
    if not args.host and not args.ip:
        missing.append("--host or --ip")
    if missing:
        print(f"error: missing required argument(s): {', '.join(missing)}")
        return 2

    return run(args)


if __name__ == "__main__":
    sys.exit(main())
