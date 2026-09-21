#!/usr/bin/env python3
"""Generate and publish the MeshCom web flasher for GitHub Pages.

Design and rationale: docs/meshcom-web-flasher-plan.md

The generator never carries a hand-maintained list of flash offsets or chip
families. Both are derived from the tree:

  * offsets and the per-board part list come from the effective
    ``upload_command`` of the environment (``platformio.ini`` and the variant
    files), i.e. from the exact esptool invocation the project itself uses;
  * the chip family comes from ``build.mcu`` in the board JSON.

Only the human-facing display names are a table, and they live here, once.

Usage:
    uv run tools/pages_flasher.py stage   --version v4.35t.09.21-neo
    uv run tools/pages_flasher.py publish --version v4.35t.09.21-neo --keep 3
    uv run tools/pages_flasher.py check   --version v4.35t.09.21-neo
"""

from __future__ import annotations

import argparse
import configparser
import datetime as _dt
import glob
import hashlib
import json
import os
import re
import shutil
import ssl
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# The 32 release environments, minus the two safeboot helper envs, which are
# inputs rather than boards. Mirrors .claude/commands/release-firmware.md
# step 4; t5_epaper and esp32-external-radio are deliberately absent.
RELEASE_ENVS = [
    "E22-DevKitC",
    "E22_1262-DevKitC",
    "E22_1262_S3-DevKitC-1-N16R8",
    "E22_1268_S3-DevKitC-1-N16R8",
    "E22_XML-DevKitC",
    "esp32-loraprs-e22",
    "esp32-loraprs-ra01",
    "heltec_wifi_lora_32_V2",
    "heltec_wifi_lora_32_V3",
    "heltec_wifi_lora_32_V4",
    "heltec_wireless_stick",
    "heltec_wireless_tracker",
    "LilyGo_T-Beam-1W",
    "T-ETH-ELITE_1262",
    "LilyGo_T3_S3_V1_3",
    "ttgo-lora32-v21",
    "ttgo_tbeam",
    "ttgo_tbeam_supreme",
    "ttgo_tbeam_SX1262",
    "ttgo_tbeam_SX1268",
    "LilyGo_T_Connect_Pro",
    "t_deck",
    "t_deck_plus",
    "t_deck_pro",
    "vision-master-e213",
    "vision-master-e290",
    "wireless-paper",
    "heltec_t114",
    "t_echo",
    "wiscore_rak4631",
]

# env -> (vendor group, display name). The only hand-maintained table here.
DISPLAY = {
    "heltec_wifi_lora_32_V2": ("Heltec", "WiFi LoRa 32 V2"),
    "heltec_wifi_lora_32_V3": ("Heltec", "WiFi LoRa 32 V3"),
    "heltec_wifi_lora_32_V4": ("Heltec", "WiFi LoRa 32 V4"),
    "heltec_wireless_stick": ("Heltec", "Wireless Stick V3"),
    "heltec_wireless_tracker": ("Heltec", "Wireless Tracker"),
    "vision-master-e213": ("Heltec", "Vision Master E213"),
    "vision-master-e290": ("Heltec", "Vision Master E290"),
    "wireless-paper": ("Heltec", "Wireless Paper"),
    "heltec_t114": ("Heltec", "Mesh Node T114"),
    "ttgo_tbeam": ("LilyGo", "T-Beam v1.1 (SX1276)"),
    "ttgo_tbeam_SX1262": ("LilyGo", "T-Beam v1.2 (SX1262)"),
    "ttgo_tbeam_SX1268": ("LilyGo", "T-Beam (SX1268)"),
    "ttgo_tbeam_supreme": ("LilyGo", "T-Beam Supreme"),
    "LilyGo_T-Beam-1W": ("LilyGo", "T-Beam 1 W"),
    "LilyGo_T3_S3_V1_3": ("LilyGo", "T3-S3 V1.3"),
    "LilyGo_T_Connect_Pro": ("LilyGo", "T-Connect Pro"),
    "T-ETH-ELITE_1262": ("LilyGo", "T-ETH-Elite (SX1262)"),
    "ttgo-lora32-v21": ("LilyGo", "TTGO LoRa32 V2.1"),
    "t_deck": ("LilyGo", "T-Deck"),
    "t_deck_plus": ("LilyGo", "T-Deck Plus"),
    "t_deck_pro": ("LilyGo", "T-Deck Pro"),
    "t_echo": ("LilyGo", "T-Echo"),
    "wiscore_rak4631": ("RAKwireless", "WisBlock RAK4631"),
    "E22-DevKitC": ("DevKit", "ESP32 DevKitC + E22 (SX1262)"),
    "E22_1262-DevKitC": ("DevKit", "ESP32 DevKitC + E22-900M22S"),
    "E22_XML-DevKitC": ("DevKit", "ESP32 DevKitC + E22 (XML)"),
    "E22_1262_S3-DevKitC-1-N16R8": ("DevKit", "ESP32-S3 DevKitC-1 + E22 (SX1262)"),
    "E22_1268_S3-DevKitC-1-N16R8": ("DevKit", "ESP32-S3 DevKitC-1 + E22 (SX1268)"),
    "esp32-loraprs-e22": ("DevKit", "LoRa-APRS ESP32 + E22"),
    "esp32-loraprs-ra01": ("DevKit", "LoRa-APRS ESP32 + RA-01"),
}

MCU_FAMILY = {
    "esp32": "ESP32",
    "esp32s2": "ESP32-S2",
    "esp32s3": "ESP32-S3",
    "esp32c3": "ESP32-C3",
    "esp32c6": "ESP32-C6",
    "nrf52840": "NRF52",
    "nrf52832": "NRF52",
}

UF2_FAMILY_ID = "0xADA52840"


# --------------------------------------------------------------------------
# platformio.ini
# --------------------------------------------------------------------------


def load_config(repo: Path = REPO) -> configparser.RawConfigParser:
    """Read platformio.ini plus every variant file, the way PlatformIO does."""
    cfg = configparser.RawConfigParser(strict=False)
    files = [repo / "platformio.ini"] + sorted(
        Path(p) for p in glob.glob(str(repo / "variants" / "*" / "platformio.ini"))
    )
    for f in files:
        with open(f, encoding="utf-8") as fh:
            cfg.read_file(fh, source=str(f))
    return cfg


def resolve(cfg: configparser.RawConfigParser, env: str, key: str) -> str | None:
    """Value of `key` for `env:<env>`, following the `extends` chain."""
    section = f"env:{env}"
    seen: set[str] = set()
    while section and section not in seen:
        seen.add(section)
        if not cfg.has_section(section):
            return None
        if cfg.has_option(section, key):
            return cfg.get(section, key)
        parent = cfg.get(section, "extends", fallback=None)
        if not parent:
            return None
        section = parent.strip()
    return None


def board_json(board: str, repo: Path = REPO) -> dict:
    local = repo / "boards" / f"{board}.json"
    if local.is_file():
        return json.loads(local.read_text())
    home = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    for cand in sorted(home.glob(f"platforms/*/boards/{board}.json")):
        return json.loads(cand.read_text())
    raise SystemExit(f"board JSON not found for {board!r}")


def chip_family(cfg, env: str, repo: Path = REPO) -> str:
    board = resolve(cfg, env, "board")
    if not board:
        raise SystemExit(f"{env}: no board in platformio.ini")
    mcu = board_json(board.strip(), repo).get("build", {}).get("mcu", "")
    if mcu not in MCU_FAMILY:
        raise SystemExit(f"{env}: unknown mcu {mcu!r} (board {board})")
    return MCU_FAMILY[mcu]


# --------------------------------------------------------------------------
# flash layout
# --------------------------------------------------------------------------

_WRITE_FLASH = re.compile(r"write_flash\s+(.*)$")


def parse_upload_command(cmd: str) -> list[tuple[int, str]]:
    """The (offset, file token) pairs of an esptool `write_flash` invocation."""
    m = _WRITE_FLASH.search(cmd.replace("\n", " "))
    if not m:
        raise ValueError("no write_flash in upload_command")
    tokens = m.group(1).split()
    pairs = []
    i = 0
    while i + 1 < len(tokens):
        off, path = tokens[i], tokens[i + 1]
        if not off.lower().startswith("0x"):
            raise ValueError(f"expected an offset, got {off!r}")
        pairs.append((int(off, 16), path))
        i += 2
    if i != len(tokens):
        raise ValueError("odd number of write_flash arguments")
    return pairs


def resolve_token(token: str, env: str, repo: Path, build_dir: Path) -> Path:
    """Map an upload_command file token onto a file in the tree."""
    t = token.replace("${platformio.build_dir}", str(build_dir))
    t = t.replace("${this.__env__}", env)
    t = t.replace("$BUILD_DIR", str(build_dir / env))
    p = Path(t)
    return p if p.is_absolute() else repo / p


def esp_parts(cfg, env: str, family: str, repo: Path, build_dir: Path):
    """Ordered (offset, source path, published name) for one ESP32 board."""
    cmd = resolve(cfg, env, "upload_command")
    if not cmd:
        # t_deck_pro keeps its upload_command commented out. Fall back to the
        # family default, which is what the commented line says anyway.
        boot = "0x0000" if family != "ESP32" else "0x1000"
        sb = "safeboot-s3.bin" if family != "ESP32" else "safeboot.bin"
        cmd = (
            f"write_flash {boot} $BUILD_DIR/bootloader.bin 0xE000 otadata.bin "
            f"0x8000 $BUILD_DIR/partitions.bin 0x10000 {sb} "
            f"0xC0000 $BUILD_DIR/firmware.bin"
        )
    out = []
    for offset, token in parse_upload_command(cmd):
        src = resolve_token(token, env, repo, build_dir)
        out.append((offset, src, src.name))
    # esptool takes the pairs in any order; the manifest lists them the way the
    # image is laid out, so a reader can check it against the partition table.
    out.sort(key=lambda t: t[0])
    return out


# --------------------------------------------------------------------------
# staging
# --------------------------------------------------------------------------


def uf2_from_hex(hex_path: Path, out: Path) -> None:
    conv = (
        Path.home()
        / ".platformio/packages/framework-arduinoadafruitnrf52/tools/uf2conv/uf2conv.py"
    )
    if not conv.is_file():
        raise SystemExit(f"uf2conv.py missing: {conv}")
    subprocess.run(
        [sys.executable, str(conv), str(hex_path), "-c", "-f", UF2_FAMILY_ID, "-o", str(out)],
        check=True,
        capture_output=True,
    )


def stage_board(cfg, env: str, version: str, dest: Path, repo: Path, build_dir: Path) -> dict:
    """Write one board folder. Returns its releases.json entry."""
    family = chip_family(cfg, env, repo)
    group, name = DISPLAY[env]
    dest.mkdir(parents=True, exist_ok=True)

    if family == "NRF52":
        hexf = build_dir / env / "firmware.hex"
        if not hexf.is_file():
            raise SystemExit(f"{env}: {hexf} missing -- build it, never ship a partial board")
        uf2_from_hex(hexf, dest / "firmware.uf2")
        builds = [{"chipFamily": "NRF52", "parts": [{"path": "firmware.uf2"}]}]
    else:
        parts = []
        for offset, src, name_out in esp_parts(cfg, env, family, repo, build_dir):
            if not src.is_file():
                raise SystemExit(f"{env}: {src} missing -- build it, never ship a partial board")
            shutil.copy2(src, dest / name_out)
            parts.append({"path": name_out, "offset": offset})
        builds = [{"chipFamily": family, "parts": parts}]

    manifest = {
        "name": f"MeshCom {group} {name}",
        "version": version,
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 0,
        "builds": builds,
    }
    (dest / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return {"env": env, "group": group, "name": name, "chipFamily": family}


def stage(version: str, out: Path, repo: Path = REPO, envs=None) -> list[dict]:
    build_dir = repo / ".pio" / "build"
    envs = envs or RELEASE_ENVS
    cfg = load_config(repo)
    boards = []
    for env in envs:
        boards.append(stage_board(cfg, env, version, out / version / env, repo, build_dir))
    return boards


def update_releases(pages_root: Path, version: str, boards: list[dict], keep: int) -> dict:
    rj = pages_root / "releases.json"
    data = json.loads(rj.read_text()) if rj.is_file() else {"releases": []}
    data["releases"] = [r for r in data["releases"] if r["version"] != version]
    data["releases"].insert(
        0,
        {
            "version": version,
            "date": _dt.date.today().isoformat(),
            "notes": f"https://github.com/DK5EN/MeshCom-Firmware/releases/tag/{version}",
            "boards": boards,
        },
    )
    pruned = [r["version"] for r in data["releases"][keep:]]
    data["releases"] = data["releases"][:keep]
    rj.write_text(json.dumps(data, indent=2) + "\n")
    return {"pruned": pruned, "kept": [r["version"] for r in data["releases"]]}


def copy_page(pages_root: Path, repo: Path = REPO) -> None:
    src = repo / "pages" / "flash"
    for item in ("index.html", "flasher.js", "VERSION"):
        if (src / item).is_file():
            shutil.copy2(src / item, pages_root / item)
    ewt = pages_root / "esp-web-tools"
    if ewt.exists():
        shutil.rmtree(ewt)
    shutil.copytree(src / "esp-web-tools", ewt)


def du(path: Path) -> str:
    total = sum(f.stat().st_size for f in path.rglob("*") if f.is_file())
    return f"{total / 1e6:.1f} MB"


# --------------------------------------------------------------------------
# commands
# --------------------------------------------------------------------------


def cmd_stage(args) -> int:
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    boards = stage(args.version, out, REPO, args.envs)
    copy_page(out)
    info = update_releases(out, args.version, boards, args.keep)
    print(f"staged {len(boards)} boards into {out}/{args.version} ({du(out)})")
    print(f"releases kept: {', '.join(info['kept'])}")
    return 0


def cmd_publish(args) -> int:
    with tempfile.TemporaryDirectory() as tmp:
        wt = Path(tmp) / "gh-pages"
        subprocess.run(
            ["git", "worktree", "add", "--detach", str(wt), "origin/gh-pages"],
            cwd=REPO, check=True, capture_output=True,
        )
        try:
            subprocess.run(["git", "checkout", "-B", "gh-pages"], cwd=wt, check=True,
                           capture_output=True)
            root = wt / "flash"
            root.mkdir(exist_ok=True)
            target = root / args.version
            if target.exists():
                shutil.rmtree(target)
            boards = stage(args.version, root, REPO, args.envs)
            copy_page(root)
            info = update_releases(root, args.version, boards, args.keep)
            for old in info["pruned"]:
                if (root / old).is_dir():
                    shutil.rmtree(root / old)
            subprocess.run(["git", "add", "--", "flash"], cwd=wt, check=True)
            msg = f"flash: web flasher {args.version} ({len(boards)} boards)"
            subprocess.run(["git", "commit", "-m", msg], cwd=wt, check=True)
            sha = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=wt,
                                 check=True, capture_output=True, text=True).stdout.strip()
            print(f"committed {sha} on gh-pages: {msg}")
            print(f"flash/ is {du(root)}, releases: {', '.join(info['kept'])}")
            if info["pruned"]:
                print(f"pruned: {', '.join(info['pruned'])}")
            if args.push:
                subprocess.run(["git", "push", "origin", "gh-pages"], cwd=wt, check=True)
                print("pushed")
            else:
                # `checkout -B gh-pages` above put the worktree ON the branch, so
                # the commit already moved it. Forcing it again from REPO would
                # fail: the branch is checked out in the worktree.
                print("not pushed (--push withheld); local gh-pages holds the commit")
            print("https://dk5en.github.io/MeshCom-Firmware/flash/")
        finally:
            subprocess.run(["git", "worktree", "remove", "--force", str(wt)],
                           cwd=REPO, capture_output=True)
    return 0


def _https_context() -> ssl.SSLContext:
    """A context with a usable CA store.

    PlatformIO's bundled interpreter is first on PATH in this checkout and
    ships no root certificates, so the plain default context fails every
    HTTPS fetch with CERTIFICATE_VERIFY_FAILED. certifi is the fallback.
    """
    ctx = ssl.create_default_context()
    if ctx.cert_store_stats()["x509_ca"]:
        return ctx
    try:
        import certifi
    except ImportError:
        raise SystemExit(
            "no CA certificates: run this with a different interpreter "
            "(/usr/bin/python3) or install certifi"
        ) from None
    return ssl.create_default_context(cafile=certifi.where())


def _get(url: str) -> bytes:
    with urllib.request.urlopen(url, context=_https_context()) as r:
        return r.read()


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def cmd_check(args) -> int:
    base = args.base_url.rstrip("/")
    build_dir = REPO / ".pio" / "build"
    cfg = load_config(REPO)
    bad = 0
    releases = json.loads(_get(f"{base}/releases.json"))["releases"]
    rel = next((x for x in releases if x["version"] == args.version), None)
    if rel is None:
        print(f"FAIL releases.json has no {args.version}")
        return 1
    for b in rel["boards"]:
        env = b["env"]
        url = f"{base}/{args.version}/{env}"
        manifest = json.loads(_get(f"{url}/manifest.json"))
        for part in manifest["builds"][0]["parts"]:
            remote = _get(f"{url}/{part['path']}")
            if part["path"] == "firmware.uf2":
                with tempfile.TemporaryDirectory() as t:
                    local_p = Path(t) / "firmware.uf2"
                    uf2_from_hex(build_dir / env / "firmware.hex", local_p)
                    local = local_p.read_bytes()
            else:
                fam = manifest["builds"][0]["chipFamily"]
                srcs = {n: s for _, s, n in esp_parts(cfg, env, fam, REPO, build_dir)}
                local = srcs[part["path"]].read_bytes()
            if sha256(remote) != sha256(local):
                print(f"FAIL {env}/{part['path']} sha mismatch")
                bad += 1
    print(f"checked {len(rel['boards'])} boards, {bad} mismatches")
    return 1 if bad else 0


def main(argv=None) -> int:
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--version", required=True)
    common.add_argument("--keep", type=int, default=3)
    common.add_argument("--envs", nargs="*", default=None)

    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("stage", parents=[common])
    s.add_argument("--out", required=True)
    s.set_defaults(fn=cmd_stage)
    p = sub.add_parser("publish", parents=[common])
    p.add_argument("--push", action="store_true")
    p.set_defaults(fn=cmd_publish)
    c = sub.add_parser("check", parents=[common])
    c.add_argument("--base-url", default="https://dk5en.github.io/MeshCom-Firmware/flash")
    c.set_defaults(fn=cmd_check)
    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    raise SystemExit(main())
