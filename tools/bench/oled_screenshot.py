#!/usr/bin/env python3
"""Screenshot of the OLED of a Heltec V3 / T-Beam over HTTP, as a PNG (INSTRUMENT build).

GET /screen.oled serves the u8g2 full-frame buffer unchanged (X-Screen-Format: oled1-<w>x<h>): 8
pages of w bytes, byte (y // 8) * w + x, bit y % 8 is pixel (x, y), LSB at the top, 1 = lit (see
src/oled_shot.h). This tool unpacks it and ImageMagick writes the PNG, scaled up with nearest
neighbour so every glyph pixel can be counted. A 503 carries X-Reason (no-display, display-off,
not-full-frame): that is reported, never turned into a blank picture.

    tools/bench/oled_screenshot.py dk5en-1.local shot.png [--scale 6]
    tools/bench/oled_screenshot.py --self-test

Text for the shot comes from the INSTRUMENT console command: --oledtext [-10] [-t<s>] <text>.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import urllib.error
import urllib.request

FORMAT_RE = re.compile(r"^oled1-(\d+)x(\d+)$")


def unpack(buf: bytes, width: int, height: int) -> bytes:
    """Row-major 8 bpp (0 dark, 255 lit) from the u8g2 vertical-byte buffer."""
    if height % 8 or len(buf) != width * height // 8:
        raise ValueError(f"{len(buf)} bytes do not fit {width}x{height}")
    out = bytearray(width * height)
    for y in range(height):
        page = (y >> 3) * width
        bit = y & 7
        row = y * width
        for x in range(width):
            if (buf[page + x] >> bit) & 1:
                out[row + x] = 255
    return bytes(out)


def fetch(host: str) -> tuple[bytes, int, int]:
    url = f"http://{host}/screen.oled"
    try:
        with urllib.request.urlopen(url, timeout=15) as resp:
            raw = resp.read()
            fmt = resp.headers.get("X-Screen-Format", "")
    except urllib.error.HTTPError as err:
        reason = err.headers.get("X-Reason", "") if err.headers else ""
        raise RuntimeError(f"{url}: HTTP {err.code} {reason}".strip()) from err
    m = FORMAT_RE.match(fmt)
    if not m:
        raise RuntimeError(f"{url}: unexpected X-Screen-Format {fmt!r}")
    w, h = int(m.group(1)), int(m.group(2))
    if len(raw) != w * h // 8:
        raise RuntimeError(f"{url}: short frame, {len(raw)} of {w * h // 8} bytes")
    return raw, w, h


def write_png(gray: bytes, width: int, height: int, scale: int, path: str) -> None:
    cmd = ["magick", "-size", f"{width}x{height}", "-depth", "8", "gray:-"]
    if scale > 1:
        cmd += ["-filter", "point", "-resize", f"{scale * 100}%"]
    subprocess.run(cmd + [path], input=gray, check=True)


def self_test() -> int:
    # Same pattern as test/test_oled_shot: corners, page boundary, LSB at the top.
    buf = bytearray(1024)
    buf[0] = 0x01
    buf[127] = 0x80
    buf[7 * 128] = 0x01
    buf[7 * 128 + 127] = 0x80
    buf[128 + 5] = 0x01
    buf[5] = 0x80
    buf[3 * 128 + 10] = 0x0F
    img = unpack(bytes(buf), 128, 64)

    def px(x: int, y: int) -> int:
        return img[y * 128 + x]

    lit = {(0, 0), (127, 7), (0, 56), (127, 63), (5, 8), (5, 7), (10, 24), (10, 25), (10, 26), (10, 27)}
    got = {(x, y) for y in range(64) for x in range(128) if px(x, y)}
    if got != lit or any(px(x, y) != 255 for x, y in lit):
        print(f"FAIL unpack: {sorted(got ^ lit)}", file=sys.stderr)
        return 1
    try:
        unpack(b"\x00", 128, 64)
    except ValueError:
        print("self-test ok")
        return 0
    print("FAIL short buffer accepted", file=sys.stderr)
    return 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("host", nargs="?", help="node host name or IP")
    ap.add_argument("png", nargs="?", help="output file")
    ap.add_argument("--scale", type=int, default=6, help="integer upscale, default 6 (768x384)")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if not args.host or not args.png:
        ap.error("host and png are required")
    if args.scale < 1:
        ap.error("--scale must be >= 1")
    try:
        raw, w, h = fetch(args.host)
    except RuntimeError as err:
        print(err, file=sys.stderr)
        return 2
    write_png(unpack(raw, w, h), w, h, args.scale, args.png)
    print(args.png)
    return 0


if __name__ == "__main__":
    sys.exit(main())
