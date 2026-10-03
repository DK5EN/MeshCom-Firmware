#!/usr/bin/env python3
"""Wait until an HTTP server answers, for the bench driver (tools/bench/bench_suite.py).

    python3 tools/bench/wait_http.py http://192.168.68.70/ --timeout 180

Polls GET until any HTTP status arrives (a 4xx/5xx still proves the server is
up) or the timeout passes; exit 0 on success, 1 on timeout. Written after the
first stage-3 run (2026-10-03): the oled harness and the OTA step both reboot
the node, and the T-Beam's WiFi needs about a minute, so the jsdom badge test
hit ETIMEDOUT on a node that was simply not back yet.
"""
from __future__ import annotations

import argparse
import sys
import time
import urllib.error
import urllib.request
from typing import Callable, Optional


def probe(url: str, timeout_s: float = 5.0) -> bool:
    """One GET; True when the server answered at all (any status)."""
    try:
        with urllib.request.urlopen(url, timeout=timeout_s):
            return True
    except urllib.error.HTTPError:
        return True
    except (urllib.error.URLError, OSError, ValueError):
        return False


def wait_for(fetch: Callable[[], bool], timeout_s: float, interval_s: float = 3.0,
             now: Callable[[], float] = time.monotonic,
             sleep: Callable[[float], None] = time.sleep) -> Optional[float]:
    """Seconds until fetch() first returned True, or None after timeout_s."""
    t0 = now()
    while True:
        if fetch():
            return now() - t0
        if now() - t0 >= timeout_s:
            return None
        sleep(interval_s)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="wait until an HTTP server answers")
    ap.add_argument("url")
    ap.add_argument("--timeout", type=float, default=180.0)
    ap.add_argument("--interval", type=float, default=3.0)
    a = ap.parse_args(argv)
    waited = wait_for(lambda: probe(a.url), a.timeout, a.interval)
    if waited is None:
        print(f"wait_http: {a.url} did not answer within {a.timeout:.0f} s")
        return 1
    print(f"wait_http: {a.url} answered after {waited:.0f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
