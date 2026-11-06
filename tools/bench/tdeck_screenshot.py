#!/usr/bin/env python3
"""Screenshot of a T-Deck over HTTP, as a PNG (INSTRUMENT build).

The panel does not drive MISO, so the firmware keeps a shadow copy of every LVGL flush in PSRAM and
serves it on GET /screen.rgb565 (RGB565 big endian). The first request switches the copy on, forces a
full redraw and answers 503; this tool retries until the frame arrives. ImageMagick writes the PNG.

    tools/bench/tdeck_screenshot.py dk5en-14.local shot.png
    tools/bench/tdeck_screenshot.py --self-test
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
import urllib.error
import urllib.request

WIDTH = 320
HEIGHT = 240
FRAME_BYTES = WIDTH * HEIGHT * 2


def rgb565be_to_rgb888(raw: bytes) -> bytes:
    """Expand RGB565 big endian to RGB888; the top bits repeat so 0x1F becomes 0xFF, not 0xF8."""
    if len(raw) % 2:
        raise ValueError("odd byte count")
    out = bytearray(len(raw) // 2 * 3)
    o = 0
    for i in range(0, len(raw), 2):
        v = (raw[i] << 8) | raw[i + 1]
        r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        out[o] = (r << 3) | (r >> 2)
        out[o + 1] = (g << 2) | (g >> 4)
        out[o + 2] = (b << 3) | (b >> 2)
        o += 3
    return bytes(out)


def fetch_frame(host: str, tries: int, delay_s: float) -> bytes:
    url = f"http://{host}/screen.rgb565"
    last = "no attempt"
    for _ in range(tries):
        try:
            with urllib.request.urlopen(url, timeout=15) as resp:
                raw = resp.read()
            if len(raw) == FRAME_BYTES:
                return raw
            last = f"short frame: {len(raw)} of {FRAME_BYTES} bytes"
        except urllib.error.HTTPError as err:
            last = f"HTTP {err.code}"
            if err.code != 503:
                break
        time.sleep(delay_s)
    raise RuntimeError(f"{url}: {last}")


def write_png(rgb: bytes, path: str) -> None:
    subprocess.run(
        ["magick", "-size", f"{WIDTH}x{HEIGHT}", "-depth", "8", "rgb:-", path],
        input=rgb,
        check=True,
    )


def self_test() -> int:
    px = rgb565be_to_rgb888(bytes([0xF8, 0x00, 0x07, 0xE0, 0x00, 0x1F, 0xFF, 0xFF, 0x00, 0x00]))
    want = bytes([255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0])
    if px != want:
        print(f"FAIL conversion: {px.hex()}", file=sys.stderr)
        return 1
    try:
        rgb565be_to_rgb888(b"\x00")
    except ValueError:
        print("self-test ok")
        return 0
    print("FAIL odd length accepted", file=sys.stderr)
    return 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("host", nargs="?", help="node host name or IP")
    ap.add_argument("png", nargs="?", help="output file")
    ap.add_argument("--tries", type=int, default=8)
    ap.add_argument("--delay", type=float, default=1.0, help="seconds between tries")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if not args.host or not args.png:
        ap.error("host and png are required")
    write_png(rgb565be_to_rgb888(fetch_frame(args.host, args.tries, args.delay)), args.png)
    print(args.png)
    return 0


if __name__ == "__main__":
    sys.exit(main())
