#!/usr/bin/env python3
"""A tiny MCP server for tests: newline-delimited JSON-RPC 2.0 on stdio. Tools: echo, add, wipe, env_present, fail.
Never prints what it receives in the environment: env_present answers only whether a variable is set."""
import json
import os
import sys

TOOLS = [
    {"name": "echo", "description": "Echo the text back.", "annotations": {"readOnlyHint": True},
     "inputSchema": {"type": "object", "properties": {"text": {"type": "string", "description": "What to echo"}}, "required": ["text"]}},
    {"name": "add", "title": "Add numbers", "description": "Add two numbers.",
     "inputSchema": {"type": "object", "properties": {"a": {"type": "number"}, "b": {"type": "number", "default": 0}}, "required": ["a"]}},
    {"name": "wipe", "description": "Pretend to delete something.", "annotations": {"destructiveHint": True},
     "inputSchema": {"type": "object", "properties": {"what": {"type": "string"}}, "required": ["what"]}},
    {"name": "env_present", "description": "Is an environment variable set (never its value).",
     "inputSchema": {"type": "object", "properties": {"name": {"type": "string"}}, "required": ["name"]}},
    {"name": "fail", "description": "Always fails.", "inputSchema": {"type": "object", "properties": {}}},
]


def send(m):
    sys.stdout.write(json.dumps(m) + "\n")
    sys.stdout.flush()


def result(i, r):
    send({"jsonrpc": "2.0", "id": i, "result": r})


for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    msg = json.loads(line)
    method, i = msg.get("method"), msg.get("id")
    if i is None:
        continue  # notification
    if method == "initialize":
        result(i, {"protocolVersion": msg["params"]["protocolVersion"], "capabilities": {"tools": {}},
                   "serverInfo": {"name": "fake", "version": "0"}})
    elif method == "tools/list":
        result(i, {"tools": TOOLS})
    elif method == "tools/call":
        name, args = msg["params"]["name"], msg["params"].get("arguments", {})
        if name == "echo":
            result(i, {"content": [{"type": "text", "text": "echo: " + args["text"]}]})
        elif name == "add":
            total = args["a"] + args.get("b", 0)
            result(i, {"content": [{"type": "text", "text": str(total)}], "structuredContent": {"sum": total}})
        elif name == "wipe":
            result(i, {"content": [{"type": "text", "text": "wiped " + args["what"]}]})
        elif name == "env_present":
            result(i, {"content": [{"type": "text", "text": "present" if os.environ.get(args["name"]) else "absent"}]})
        elif name == "fail":
            result(i, {"content": [{"type": "text", "text": "it broke"}], "isError": True})
        else:
            send({"jsonrpc": "2.0", "id": i, "error": {"code": -32602, "message": "unknown tool"}})
    else:
        send({"jsonrpc": "2.0", "id": i, "error": {"code": -32601, "message": "no such method"}})
