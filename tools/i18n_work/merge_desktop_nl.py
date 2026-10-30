#!/usr/bin/env python3
"""Add the nl texts of desktop_nl.json to loom/web/src/shell/data/desktop.json (idempotent; touches only `nl` keys)."""
import json, pathlib
here = pathlib.Path(__file__).resolve().parent
src = json.load(open(here / "desktop_nl.json", encoding="utf8"))
p = here.parent.parent / "loom/web/src/shell/data/desktop.json"
raw = p.read_text(encoding="utf8")
d = json.loads(raw)
n = 0
def put(o, key, val):
    global n
    if o.get(key) != val: o[key] = val; n += 1
for k, v in src["words"].items():
    if k in d["words"]: put(d["words"][k], "nl", v)
for k, v in src["declarations"].items():
    if k in d["declarations"] and isinstance(d["declarations"][k].get("label"), dict): put(d["declarations"][k]["label"], "nl", v)
for k, v in src["address_labels"].items():
    if k in d["address_labels"]: put(d["address_labels"][k], "nl", v)
for k, v in src["declaration_words"].items():
    if k in d["declaration_words"]: put(d["declaration_words"][k], "nl", v)
for k, v in src["arrange_words"].items():
    if k in d["arrangement"]["arrange_words"]: put(d["arrangement"]["arrange_words"][k], "nl", v)
indent = 1
p.write_text(json.dumps(d, indent=indent, ensure_ascii=False) + ("\n" if raw.endswith("\n") else ""), encoding="utf8")
print("nl values set:", n)
