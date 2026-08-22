#!/usr/bin/env python3
"""Minimal MCP (2026-07-28 stateless) HTTP client for mcmap-prod research."""
import json, subprocess, sys

BASE = "https://meshmap.oevsv.at/api/mcp"
TOKEN = json.load(open("/Users/martinwerner/WebDev/mcmap/.claude/settings.local.json"))["env"]["MCMAP_PROD_MCP_TOKEN"]

def rpc(method, params, name=None):
    body = {"jsonrpc": "2.0", "id": 1, "method": method, "params": params}
    cmd = ["curl", "-sS", "-m", "60", "-X", "POST", BASE,
           "-H", f"Authorization: Bearer {TOKEN}",
           "-H", "Content-Type: application/json",
           "-H", "Accept: application/json, text/event-stream",
           "-H", f"Mcp-Method: {method}"]
    if name:
        cmd += ["-H", f"Mcp-Name: {name}"]
    cmd += ["--data-binary", "@-"]
    raw = subprocess.run(cmd, input=json.dumps(body), capture_output=True,
                         text=True, check=True).stdout
    # streamable http may answer SSE; extract data lines
    if raw.startswith("event:") or raw.startswith("data:"):
        chunks = [l[5:].strip() for l in raw.splitlines() if l.startswith("data:")]
        raw = chunks[-1] if chunks else raw
    return json.loads(raw)

def call(tool, args=None):
    resp = rpc("tools/call", {"name": tool, "arguments": args or {}}, name=tool)
    if "error" in resp:
        return {"_rpc_error": resp["error"]}
    result = resp.get("result", {})
    if result.get("isError"):
        return {"_tool_error": result.get("content")}
    sc = result.get("structuredContent")
    if sc is not None:
        return sc
    content = result.get("content", [])
    for c in content:
        if c.get("type") == "text":
            try:
                return json.loads(c["text"])
            except Exception:
                return {"_text": c["text"]}
    return result

if __name__ == "__main__":
    mode = sys.argv[1]
    if mode == "list":
        resp = rpc("tools/list", {}, name=None)
        tools = resp.get("result", {}).get("tools", [])
        for t in tools:
            print(t["name"], "-", (t.get("description") or "")[:100].replace("\n", " "))
    elif mode == "schema":
        resp = rpc("tools/list", {}, name=None)
        for t in resp.get("result", {}).get("tools", []):
            if t["name"] == sys.argv[2]:
                print(json.dumps(t, indent=1))
    elif mode == "call":
        tool = sys.argv[2]
        args = json.loads(sys.argv[3]) if len(sys.argv) > 3 else {}
        out = call(tool, args)
        print(json.dumps(out, indent=1, ensure_ascii=False))
    else:
        print("usage: mcp.py list | schema <tool> | call <tool> [json-args]")
