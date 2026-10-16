#!/usr/bin/env python3
"""Synthetic exports for the Android smoke (no real data): a Claude.ai ZIP, a ChatGPT conversations.json and a long
conversation. Usage: make-exports.py OUTDIR  -> OUTDIR/claude-export.zip, chatgpt-conversations.json, long-claude.json"""
import json, sys, zipfile, os, zlib

out = sys.argv[1]
os.makedirs(out, exist_ok=True)

def claude_chat(uuid, name, turns):
    msgs = []
    for i, (who, text) in enumerate(turns):
        msgs.append({"uuid": f"{uuid}-m{i}", "sender": who, "text": text,
                     "created_at": f"2025-03-01T10:{i // 60:02d}:{i % 60:02d}Z", "content": [{"type": "text", "text": text}]})
    return {"uuid": uuid, "name": name, "created_at": "2025-03-01T10:00:00Z", "updated_at": "2025-03-01T11:00:00Z", "chat_messages": msgs}

claude = [
    claude_chat("aaaaaaaa-0000-4000-8000-000000000001", "Smoke Claude ZIP",
                [("human", "Zespol Orion planuje premiere w marcu."), ("assistant", "Premiera Orion w marcu, zanotowane."),
                 ("human", "Kto odpowiada za testy?"), ("assistant", "Za testy odpowiada Anna Nowak.")]),
    claude_chat("aaaaaaaa-0000-4000-8000-000000000002", "Smoke Claude ZIP second",
                [("human", "Drugi watek eksportu."), ("assistant", "Drugi watek odebrany.")]),
]
with zipfile.ZipFile(os.path.join(out, "claude-export.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("conversations.json", json.dumps(claude, ensure_ascii=False))
    z.writestr("users.json", json.dumps([{"uuid": "bbbbbbbb-0000-4000-8000-000000000001", "full_name": "Smoke User", "email_address": "smoke@example.invalid"}]))
    z.writestr("projects.json", "[]")

def gpt_chat(cid, title, turns):
    mapping = {"root": {"id": "root", "parent": None, "children": [f"n0"], "message": None}}
    prev = "root"
    for i, (who, text) in enumerate(turns):
        nid = f"n{i}"
        mapping[prev]["children"] = [nid]
        mapping[nid] = {"id": nid, "parent": prev, "children": [],
                        "message": {"id": nid, "author": {"role": who}, "create_time": 1740823200 + i,
                                    "content": {"content_type": "text", "parts": [text]}}}
        prev = nid
    return {"id": cid, "conversation_id": cid, "title": title, "create_time": 1740823200, "update_time": 1740823300,
            "mapping": mapping, "current_node": prev}

gpt = [
    gpt_chat("cccccccc-0000-4000-8000-000000000001", "Smoke ChatGPT JSON",
             [("user", "Projekt Helios startuje w maju."), ("assistant", "Helios w maju, rozumiem."),
              ("user", "Kto prowadzi projekt?"), ("assistant", "Projekt prowadzi Jan Kowalski.")]),
]
with open(os.path.join(out, "chatgpt-conversations.json"), "w", encoding="utf-8") as f:
    json.dump(gpt, f, ensure_ascii=False)

long_turns = []
for i in range(260):
    long_turns.append(("human", f"Pytanie numer {i}: co z zadaniem {i}?"))
    long_turns.append(("assistant", f"Odpowiedz numer {i}: zadanie {i} jest w toku."))
with open(os.path.join(out, "long-claude.json"), "w", encoding="utf-8") as f:
    json.dump([claude_chat("dddddddd-0000-4000-8000-000000000001", "Smoke Long 520", long_turns)], f, ensure_ascii=False)
# files for the system-picker steps (a different conversation each, so a store never meets the same bytes twice)
def one(path, name, who_text):
    with open(path, "w", encoding="utf-8") as f:
        json.dump([claude_chat("eeeeeeee-0000-4000-8000-" + format(zlib.crc32(name.encode()), "012d"), name,
                               [("human", who_text), ("assistant", "Odebrano: " + who_text)])], f, ensure_ascii=False)
one(os.path.join(out, "saf-local.json"), "Smoke SAF local", "Plik wybrany systemowym pickerem na telefonie.")
one(os.path.join(out, "saf-server.json"), "Smoke SAF server", "Plik wybrany przez WebView file chooser.")
one(os.path.join(out, "ui-local.json"), "Smoke UI local", "Plik z panelu importu w trybie lokalnym.")
os.makedirs(os.path.join(out, "saf-folder"), exist_ok=True)
one(os.path.join(out, "saf-folder", "one.json"), "Smoke SAF folder one", "Pierwszy plik z folderu.")
one(os.path.join(out, "saf-folder", "two.json"), "Smoke SAF folder two", "Drugi plik z folderu.")
with open(os.path.join(out, "saf-folder", "ignored.bin"), "wb") as f:
    f.write(b"\x00\x01\x02 not an export")
print("ok", out)
