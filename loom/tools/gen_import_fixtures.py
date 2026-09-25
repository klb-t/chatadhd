#!/usr/bin/env python3
"""Generate deterministic import fixtures (one or more per supported format)
into loom/tests/fixtures/import/ for the importer's differential tests.

Every file is synthetic (no real conversations). The same files are fed to
engine.importer.ConversationImporter and to Loom's ConversationImporter and
the resulting databases are compared.

Usage: python3 loom/tools/gen_import_fixtures.py
"""
import base64
import json
import pathlib
import sqlite3
import zipfile

OUT = pathlib.Path(__file__).resolve().parents[1] / "tests" / "fixtures" / "import"

CHATGPT = [
    {
        "title": "Loom design ✓",
        "mapping": {
            "root": {"id": "root", "parent": None, "children": ["a"], "message": None},
            "a": {"id": "a", "parent": "root", "children": ["b"],
                  "message": {"author": {"role": "system"}, "content": {"content_type": "text", "parts": ["sys"]}}},
            "b": {"id": "b", "parent": "a", "children": ["c", "c2"],
                  "message": {"author": {"role": "user"},
                              "content": {"content_type": "text", "parts": ["How should the kernel store blobs?"]}}},
            "c": {"id": "c", "parent": "b", "children": [],
                  "message": {"author": {"role": "assistant"},
                              "content": {"content_type": "text",
                                          "parts": ["Content-addressed by SHA-256.", {"content_type": "text",
                                                                                      "text": "Never rewrite."}]}}},
            "c2": {"id": "c2", "parent": "b", "children": [],
                   "message": {"author": {"role": "assistant"},
                               "content": {"content_type": "text", "parts": ["An alternative branch (fork)."]}}},
        },
    },
    {"title": "Empty mapping", "mapping": {}},
    {
        "title": "Zażółć 😀",
        "mapping": {
            "x": {"id": "x", "parent": "missing-parent", "children": ["y"],
                  "message": {"author": {"role": "user"}, "content": {"parts": ["Polish: gęślą jaźń"]}}},
            "y": {"id": "y", "parent": "x", "children": [],
                  "message": {"author": {"role": "tool"}, "content": {"parts": ["tool output is skipped"]}}},
        },
    },
]

CLAUDE = [
    {
        "uuid": "11111111-1111-1111-1111-111111111111",
        "name": "Archive intelligence",
        "chat_messages": [
            {"sender": "human", "text": "Find every fork of the Loom idea."},
            {"sender": "assistant", "text": "",
             "content": [{"type": "text", "text": "Pass 1: lexical."}, {"type": "tool_use", "name": "x"},
                         {"type": "text", "text": "Pass 2: expanded vocabulary."}]},
            {"sender": "human", "text": "   "},
        ],
    },
    {"name": "No messages", "chat_messages": []},
]

OPENAI_MESSAGES = [
    {"role": "system", "content": "You are helpful."},
    {"role": "user", "content": [{"type": "text", "text": "Describe this"},
                                 {"type": "image_url", "image_url": {"url": "data:image/png;base64,AAAA"}}]},
    {"role": "assistant", "content": "A tiny image."},
    {"role": "user", "content": 42},
]

CONV_OBJECTS = {"conversations": [
    {"title": "Obj A", "messages": [{"role": "human", "text": "hi"}, {"role": "ai", "text": "hello"}]},
    {"name": "Obj B", "items": [{"sender": "user", "content": "items key"}]},
]}

JSONL = [
    {"role": "user", "content": "line one"},
    {"role": "assistant", "content": "line two"},
    "not an object",
    {"messages": [{"role": "user", "content": "nested conversation"}], "title": "Nested"},
]

HTML = """<!DOCTYPE html>
<html><head><title>Saved chat</title><style>.x{}</style></head><body>
<div class="message user">What is the MEGA MASTER document?</div>
<div class="message assistant"><p>It is the <b>architecture constitution</b> &amp; roadmap.</p></div>
<div data-role="user">Short</div>
<div data-role="assistant">Another assistant answer with enough text.</div>
</body></html>
"""

HTML_LABELS = """<html><body><script>var x = "Human: not this";</script>
Human: first question about kernels and graphs
Assistant: first answer, fairly detailed and long
Human: second question please
Claude: second answer from Claude here
</body></html>"""

MARKDOWN = """# Exported chat

## Human
How do tasks resume after a crash?

## Assistant
From the last checkpoint.

---

**User**
And provenance?

**Claude**
Every import records source bytes, parser and transforms.
"""

TEXT = """Human: plain text export, question one
Assistant: answer one
spanning two lines
User: question two
AI: answer two"""

TEXT_NO_ROLES = "Just a note without any speaker labels.\nSecond line."

# ChatGPT export with an edited message: "b" has two children ("edit-1" then
# "edit-2", i.e. the user edited their message and ChatGPT re-answered),
# matching how ChatGPT's own export represents edits (siblings under the
# same parent, not a special "edit" node type).
CHATGPT_EDITS = [{
    "title": "Edited question",
    "mapping": {
        "root": {"id": "root", "parent": None, "children": ["u1"], "message": None},
        "u1": {"id": "u1", "parent": "root", "children": ["a1"],
               "message": {"author": {"role": "user"}, "content": {"parts": ["Original question"]}}},
        "a1": {"id": "a1", "parent": "u1", "children": ["u2edit"],
               "message": {"author": {"role": "assistant"}, "content": {"parts": ["First answer"]}}},
        "u2edit": {"id": "u2edit", "parent": "a1", "children": ["a2"],
                   "message": {"author": {"role": "user"}, "content": {"parts": ["Edited follow-up question"]}}},
        "a2": {"id": "a2", "parent": "u2edit", "children": [],
               "message": {"author": {"role": "assistant"}, "content": {"parts": ["Second, deeper answer"]}}},
    },
}]

# Claude export with attachment/project metadata Python's importer ignores
# (attachments, project block) plus heavy unicode content.
CLAUDE_UNICODE = [{
    "uuid": "22222222-2222-2222-2222-222222222222",
    "name": "Unicode — 日本語 नमस्ते 😀",
    "project": {"uuid": "proj-1", "name": "Ignored project"},
    "chat_messages": [
        {"sender": "human", "text": "Emoji test 🚀🎉 and RTL: שלום",
         "attachments": [{"file_name": "notes.txt", "extracted_content": "ignored by Python"}]},
        {"sender": "assistant", "text": "Reply with combining marks: é and CJK: 你好世界"},
    ],
}]

# Odd/edge-case message shapes the shared normaliser must handle exactly
# like engine/importer.py's `_import_message_list`.
ODD_MESSAGES = [
    {"role": "user", "content": None},                    # falsy content -> dropped
    {"role": "user", "content": ""},                       # blank -> dropped
    {"role": "user", "content": "   \n\t  "},               # whitespace-only -> dropped
    {"role": "SYSTEM", "content": "not actually skipped"},  # exact-match only; uppercase is NOT 'system'
    {"content": "no role key at all"},                      # role defaults to 'user'
    {"role": "user", "content": True},                      # non-list, non-str, truthy -> str(True) = "True"
    {"role": "assistant", "content": 3.5},                  # str(3.5) = "3.5"
    {"role": "user", "content": ["plain string part", {"type": "text", "text": "typed part"},
                                  {"type": "image_url", "image_url": {"url": "x"}}, {"type": "other"}]},
    {"role": "assistant", "content": "  padded on both sides  "},
]

# Deliberately truncated JSON: both sides must fail the import.
MALFORMED_JSON = '[{"chat_messages": [{"sender": "human", "text": "cut off'


def mht(html: str) -> bytes:
    body = base64.b64encode(html.encode("utf-8")).decode()
    return ("From: <Saved by Blink>\r\nMIME-Version: 1.0\r\n"
            "Content-Type: multipart/related; type=\"text/html\"; boundary=\"----=_NextPart_000\"\r\n\r\n"
            "------=_NextPart_000\r\nContent-Type: text/html\r\nContent-Transfer-Encoding: base64\r\n\r\n"
            f"{body}\r\n------=_NextPart_000--\r\n").encode()


def claude_sqlite(path: pathlib.Path):
    if path.exists():
        path.unlink()
    conn = sqlite3.connect(path)
    conn.executescript("""
        CREATE TABLE conversations (id TEXT PRIMARY KEY, title TEXT, created_at TEXT);
        CREATE TABLE messages (id TEXT PRIMARY KEY, conversation_id TEXT, role TEXT, content TEXT, created_at TEXT);
        INSERT INTO conversations VALUES ('k1', 'SQLite conv one', '2024-01-01T00:00:00Z');
        INSERT INTO conversations VALUES ('k2', NULL, '2024-02-01T00:00:00Z');
        INSERT INTO messages VALUES ('a', 'k1', 'human', 'db question', '2024-01-01T00:00:01Z');
        INSERT INTO messages VALUES ('b', 'k1', 'assistant', 'db answer', '2024-01-01T00:00:02Z');
        INSERT INTO messages VALUES ('c', 'k2', 'user', 'second conv', '2024-02-01T00:00:01Z');
    """)
    conn.commit()
    conn.close()


def generic_sqlite(path: pathlib.Path):
    if path.exists():
        path.unlink()
    conn = sqlite3.connect(path)
    conn.executescript("""
        CREATE TABLE chat_log (role TEXT, text TEXT);
        INSERT INTO chat_log VALUES ('user', 'generic table row');
        INSERT INTO chat_log VALUES ('bot', 'generic reply');
    """)
    conn.commit()
    conn.close()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    w = lambda name, data: (OUT / name).write_text(data, encoding="utf-8")  # noqa: E731
    w("chatgpt_conversations.json", json.dumps(CHATGPT, ensure_ascii=False, indent=1))
    w("claude_export.json", json.dumps(CLAUDE, ensure_ascii=False, indent=1))
    w("openai_messages.json", json.dumps(OPENAI_MESSAGES, indent=1))
    w("conversation_objects.json", json.dumps(CONV_OBJECTS, indent=1))
    w("wrapped_data.json", json.dumps({"data": CLAUDE[:1]}, ensure_ascii=False))
    w("messages.jsonl", "\n".join(json.dumps(x) for x in JSONL) + "\n")
    w("chat.html", HTML)
    w("chat_labels.htm", HTML_LABELS)
    (OUT / "chat.mht").write_bytes(mht(HTML))
    w("chat.md", MARKDOWN)
    w("chat.txt", TEXT)
    w("note.log", TEXT_NO_ROLES)
    claude_sqlite(OUT / "claude.db")
    generic_sqlite(OUT / "generic.db")
    # Unknown extension, sniffed by content.
    w("export_without_extension", json.dumps(CLAUDE[:1]))
    w("chatgpt_edits.json", json.dumps(CHATGPT_EDITS, ensure_ascii=False, indent=1))
    w("claude_unicode.json", json.dumps(CLAUDE_UNICODE, ensure_ascii=False, indent=1))
    w("odd_messages.json", json.dumps(ODD_MESSAGES, indent=1))
    w("malformed.json", MALFORMED_JSON)
    # ZIP bundle mixing formats (deterministic timestamps).
    zpath = OUT / "bundle.zip"
    with zipfile.ZipFile(zpath, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for name in ("claude_export.json", "chat.md", "messages.jsonl"):
            info = zipfile.ZipInfo("export/" + name, date_time=(2024, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(info, (OUT / name).read_bytes())
        info = zipfile.ZipInfo("export/ignored.bin", date_time=(2024, 1, 1, 0, 0, 0))
        zf.writestr(info, b"\x00\x01binary")

    # Nested ZIP: an inner .zip (itself containing an importable file) plus a
    # sibling text file, both inside the outer archive.
    inner_path = OUT / "_inner_tmp.zip"
    with zipfile.ZipFile(inner_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        info = zipfile.ZipInfo("inner_chat.txt", date_time=(2024, 1, 1, 0, 0, 0))
        zf.writestr(info, TEXT)
    with zipfile.ZipFile(OUT / "nested.zip", "w", compression=zipfile.ZIP_DEFLATED) as zf:
        info = zipfile.ZipInfo("outer/inner.zip", date_time=(2024, 1, 1, 0, 0, 0))
        zf.writestr(info, inner_path.read_bytes())
        info = zipfile.ZipInfo("outer/note.log", date_time=(2024, 1, 1, 0, 0, 0))
        zf.writestr(info, TEXT_NO_ROLES)
    inner_path.unlink()

    print(f"wrote fixtures to {OUT}")


if __name__ == "__main__":
    main()
