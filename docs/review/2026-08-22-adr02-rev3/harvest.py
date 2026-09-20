#!/usr/bin/env python3
"""Harvest HEY JSON lines from mcmap-prod interlink.log via logs_grep cursor loop.
Usage: harvest.py [start_byte]   (resumes from cursor.txt if present)"""
import base64, json, os, sys, time
from mcp import call

OUT = "heys.jsonl"
CUR = "cursor.txt"
PATTERN = '"type":"hey"'
INO = 918235

def main():
    cursor = None
    if os.path.exists(CUR):
        cursor = open(CUR).read().strip() or None
        print("resuming from cursor file")
    elif len(sys.argv) > 1:
        start_byte = int(sys.argv[1])
        cursor = base64.urlsafe_b64encode(
            json.dumps({"v": 1, "byte": start_byte, "line": 1, "ino": INO}).encode()
        ).decode().rstrip("=")
    calls = 0
    kept = 0
    cutoff = time.time() - 86400
    with open(OUT, "a") as f:
        while calls < 400:
            args = {"name": "interlink", "pattern": PATTERN, "maxMatches": 1000}
            if cursor:
                args["cursor"] = cursor
            r = call("logs_grep", args)
            calls += 1
            if "_rpc_error" in r or "_tool_error" in r:
                msg = json.dumps(r)
                print("transient error, backing off:", msg[:200], flush=True)
                if "409" in msg:
                    print("cursor stale (rotation) - aborting for manual restart")
                    break
                time.sleep(30)
                continue
            for m in r.get("matches", []):
                t = m["text"].strip()
                try:
                    obj = json.loads(t)
                except Exception:
                    continue
                if obj.get("timestamp", 0) >= cutoff:
                    f.write(json.dumps(obj) + "\n")
                    kept += 1
            cursor = r.get("nextCursor")
            if cursor:
                open(CUR, "w").write(cursor)
            if not r.get("truncated"):
                print("EOF reached")
                break
            if calls % 20 == 0:
                print(f"progress: {calls} calls, kept {kept}", flush=True)
            time.sleep(1.2)
    print(f"done: {calls} calls, kept {kept} -> {OUT}")

if __name__ == "__main__":
    main()
