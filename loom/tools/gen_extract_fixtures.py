#!/usr/bin/env python3
"""Synthetic fixtures for loom/tools/extract (X-Port) — every new source format plus
the edge cases the slice-A tests need: Claude Code sessions (tool calls, thinking,
sidechain, compact summary, housekeeping and unknown lines, an invalid line),
Gemini Takeout (JSON and HTML), Grok (grok.com and the X archive), our graph
packet and loom.export conversation, a mixed directory of text-like files with
mixed time stamps (epoch, ISO with zone, ISO without zone, none), two ChatGPT
exports of ONE account from different dates (unchanged / changed / removed / new
conversation, a resolved and a missing attachment), an exact duplicate archive,
and a ChatGPT→Claude continuation inside the timeline window.

No real personal data: names, ids and texts are invented.  ``EXPECTED.json`` is
an independent oracle computed from the generator's own data (never through the
readers); the tests compare the readers' measurements against it.

Regenerate: python3 loom/tools/gen_extract_fixtures.py
"""
import hashlib
import io
import json
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from gen_export_fixtures import claude_conv_linear, dumps, msg, oa_conv, text, write_zip  # noqa: E402

OUT = HERE.parent / "tests" / "fixtures" / "extract"
PNG = bytes.fromhex("89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c489") + b"X" * 24

# strings a privacy test may assert never appear in aggregate output
FORBIDDEN = ["Naprawa importera", "pamięć hierarchiczna", "Grok o ChatADHD", "Eksport Loom", "Analizator archiwów",
             "schemat.png", "brak.pdf", "Migracja pamięci", "Rekurencja po katalogach"]
# … and what the token-economics report must not carry in aggregate mode (titles, ids, paths, file names)
ECON_FORBIDDEN = ["Sesja testowa jeden", "Sesja bez transkryptu", "session_TEST1", "session_TEST2", "session_TEST3", "sess-E1", "sess-E2",
                  "agent-s1", "agent-w1", "run-1", "src/a.py", "src/b.py", "Dodaj licznik tokenów"]


# ═════════════════════════ Claude Code sessions ═════════════════════════
def cc_line(uuid, parent, typ, ts, content, *, session="sess-0001", **extra):
    d = {"uuid": uuid, "parentUuid": parent, "type": typ, "timestamp": ts, "sessionId": session,
         "cwd": "/home/user/proj", "version": "2.0.0", "gitBranch": "main", "isSidechain": False,
         "userType": "external", "message": {"role": typ, "content": content}}
    if typ == "assistant":
        d["message"]["model"] = "claude-sonnet-4-5"
    d.update(extra)
    return d


def claude_code_session_1():
    T = "2026-10-10T10:{:02d}:00+02:00"  # zone +02:00 on purpose
    lines = [
        {"type": "summary", "summary": "Naprawa importera ChatADHD", "leafUuid": "u8"},
        {"type": "file-history-snapshot", "messageId": "u1", "snapshot": {"trackedFileBackups": {}}, "isSnapshotUpdate": False},
        cc_line("u1", None, "user", T.format(0), "Napraw importer w engine/importer.py — gubi gałęzie"),
        cc_line("u2", "u1", "assistant", T.format(1), [
            {"type": "thinking", "thinking": "Trzeba sprawdzić _import_chatgpt_mapping i current_node.", "signature": "x"},
            {"type": "text", "text": "Sprawdzam importer."},
            {"type": "tool_use", "id": "toolu_1", "name": "Read", "input": {"file_path": "engine/importer.py"}}]),
        cc_line("u3", "u2", "user", T.format(2), [
            {"type": "tool_result", "tool_use_id": "toolu_1", "content": [{"type": "text", "text": "def _import_chatgpt_mapping(...): ..."}]}]),
        cc_line("u4", "u3", "assistant", T.format(3), [
            {"type": "tool_use", "id": "toolu_2", "name": "Edit", "input": {"file_path": "engine/importer.py", "old_string": "a", "new_string": "b"}},
            {"type": "text", "text": "Poprawiam przejście drzewa."}]),
        cc_line("u5", "u4", "user", T.format(4), [
            {"type": "tool_result", "tool_use_id": "toolu_2", "content": "old_string not found", "is_error": True}]),
        cc_line("u6", "u5", "assistant", T.format(5), [{"type": "text", "text": "Gotowe: importer zachowuje gałęzie."}]),
        # a sidechain (sub-agent) hanging off u2 → fork at u2
        cc_line("u7", "u2", "user", T.format(6), "Sprawdź testy", isSidechain=True, agentId="agent-1"),
        # a compact summary continuing the main path
        cc_line("u8", "u6", "user", T.format(7), "This session is being continued from a previous conversation…", isCompactSummary=True),
        {"type": "system", "uuid": "s1", "parentUuid": "u8", "timestamp": T.format(8), "sessionId": "sess-0001",
         "content": "Context compacted", "subtype": "compact_boundary", "level": "info"},
        {"type": "queue-operation", "operation": "dequeue", "timestamp": T.format(8), "sessionId": "sess-0001"},
        {"type": "future-type", "sessionId": "sess-0001", "foo": 1},
    ]
    out = "\n".join(json.dumps(x, ensure_ascii=False) for x in lines)
    out += "\n{not json\n\n"
    return out


def claude_code_session_2():
    lines = [
        cc_line("v1", None, "user", "2026-10-11T09:00:00Z", "Co to jest rekurencja po katalogach?", session="sess-0002"),
        cc_line("v2", "v1", "assistant", "2026-10-11T09:00:30Z", [{"type": "text", "text": "Rekurencja po katalogach: funkcja woła siebie dla podkatalogów."}], session="sess-0002"),
    ]
    return "\n".join(json.dumps(x, ensure_ascii=False) for x in lines) + "\n"


# ═════════════════════════ token economics (Claude Code with usage, subagents, workflows, session list) ═════════════════════════
ECON_TURNS = [
    # (file, message id, model, timestamp, usage {in, out, cache_creation, cache_read}, lines the response is written in)
    ("sess-E1", "msg_A1", "claude-x-large", "2026-10-10T08:00:10Z", (10, 120, 1000, 0), 3),
    ("sess-E1", "msg_A2", "claude-x-large", "2026-10-10T08:02:00Z", (5, 40, 200, 1000), 1),
    ("sess-E1", "msg_A3", "claude-x-large", "2026-10-11T09:00:00Z", (4, 60, 100, 1200), 1),
    ("subagents/agent-s1", "msg_S1", "claude-x-small", "2026-10-10T08:05:00Z", (2, 30, 500, 0), 1),
    ("subagents/workflows/run-1/agent-w1", "msg_W1", "claude-x-small", "2026-10-10T08:06:00Z", (1, 10, 50, 300), 1),
    ("subagents/workflows/run-1/agent-w2", "msg_W2", "claude-x-small", "2026-10-10T08:07:00Z", (1, 20, 60, 400), 1),
    ("sess-E2", "msg_B1", "claude-x-small", "2026-10-10T12:00:30Z", (4, 25, 0, 800), 1),
]


def _usage(u, partial=False):
    i, o, cc, cr = u
    return {"input_tokens": i, "output_tokens": (o // 4 if partial else o), "cache_creation_input_tokens": cc,
            "cache_read_input_tokens": cr, "service_tier": "standard",
            "cache_creation": {"ephemeral_5m_input_tokens": cc, "ephemeral_1h_input_tokens": 0}}


def econ_line(uuid, parent, typ, ts, content, *, session, model=None, mid=None, usage=None, sidechain=False, **extra):
    d = {"uuid": uuid, "parentUuid": parent, "type": typ, "timestamp": ts, "sessionId": session, "cwd": "/home/user/econ",
         "version": "2.0.0", "gitBranch": "main", "isSidechain": sidechain, "userType": "external",
         "message": {"role": typ, "content": content}}
    if typ == "assistant":
        d["message"].update({"model": model, "id": mid, "type": "message", "usage": usage})
        d["requestId"] = "req_" + (mid or uuid)
    d.update(extra)
    return json.dumps(d, ensure_ascii=False)


def economics_files():
    T = {t[1]: t for t in ECON_TURNS}
    S = "sess-E1"
    commit_cmd = "git add -A && git commit -m \"feat: licznik tokenów\n\nClaude-Session: https://claude.ai/code/session_TEST1\""
    main = [
        econ_line("e1", None, "user", "2026-10-10T08:00:00Z", "Dodaj licznik tokenów do raportu", session=S),
        # one API response written in three lines (thinking, text, tool_use), the same message.id; output grows
        econ_line("e2", "e1", "assistant", "2026-10-10T08:00:10Z", [{"type": "thinking", "thinking": "Plan: licznik w raporcie.", "signature": "x"}],
                  session=S, model="claude-x-large", mid="msg_A1", usage=_usage(T["msg_A1"][4], partial=True)),
        econ_line("e3", "e2", "assistant", "2026-10-10T08:00:11Z", [{"type": "text", "text": "Dodaję licznik."}],
                  session=S, model="claude-x-large", mid="msg_A1", usage=_usage(T["msg_A1"][4])),
        econ_line("e4", "e3", "assistant", "2026-10-10T08:00:12Z",
                  [{"type": "tool_use", "id": "toolu_e1", "name": "Edit", "input": {"file_path": "src/a.py", "old_string": "x", "new_string": "y"}}],
                  session=S, model="claude-x-large", mid="msg_A1", usage=_usage(T["msg_A1"][4])),
        econ_line("e5", "e4", "user", "2026-10-10T08:01:00Z", [{"type": "tool_result", "tool_use_id": "toolu_e1", "content": "ok"}], session=S),
        econ_line("e6", "e5", "assistant", "2026-10-10T08:02:00Z",
                  [{"type": "tool_use", "id": "toolu_b1", "name": "Bash", "input": {"command": commit_cmd, "description": "commit"}}],
                  session=S, model="claude-x-large", mid="msg_A2", usage=_usage(T["msg_A2"][4])),
        econ_line("e7", "e6", "user", "2026-10-10T08:02:30Z", [{"type": "tool_result", "tool_use_id": "toolu_b1", "content": "[main abc] feat"}], session=S),
        econ_line("e8", "e7", "assistant", "2026-10-11T09:00:00Z",
                  [{"type": "tool_use", "id": "toolu_w1", "name": "Write", "input": {"file_path": "src/b.py", "content": "print(1)\n"}},
                   {"type": "tool_use", "id": "toolu_e2", "name": "Edit", "input": {"file_path": "src/a.py", "old_string": "q", "new_string": "r"}}],
                  session=S, model="claude-x-large", mid="msg_A3",
                  usage=dict(_usage(T["msg_A3"][4]), server_tool_use={"web_search_requests": 1, "web_fetch_requests": 0},
                             output_tokens_details={"thinking_tokens": 0}, future_counter=7)),
        econ_line("e9", "e8", "user", "2026-10-11T09:00:05Z", [{"type": "tool_result", "tool_use_id": "toolu_w1", "content": "ok"},
                                                              {"type": "tool_result", "tool_use_id": "toolu_e2", "content": "old_string not found",
                                                               "is_error": True}], session=S),
        # a client-inserted message (no API call) and an assistant line without counters
        econ_line("e10", "e9", "assistant", "2026-10-11T09:00:06Z", [{"type": "text", "text": "No response requested."}],
                  session=S, model="<synthetic>", mid="msg_SYN", usage={"input_tokens": 0, "output_tokens": 0,
                                                                         "cache_creation_input_tokens": 0, "cache_read_input_tokens": 0}),
        json.dumps({"uuid": "e11", "parentUuid": "e10", "type": "assistant", "timestamp": "2026-10-11T09:00:07Z", "sessionId": S,
                    "isSidechain": False, "message": {"role": "assistant", "model": "claude-x-large", "content": [{"type": "text", "text": "API Error"}]}},
                   ensure_ascii=False),
    ]
    sub = [econ_line("s1", None, "user", "2026-10-10T08:04:00Z", "Przeczytaj raport", session=S, sidechain=True, agentId="s1"),
           econ_line("s2", "s1", "assistant", "2026-10-10T08:05:00Z",
                     [{"type": "tool_use", "id": "toolu_r1", "name": "Read", "input": {"file_path": "src/report.py"}}],
                     session=S, model="claude-x-small", mid="msg_S1", usage=_usage(T["msg_S1"][4]), sidechain=True, agentId="s1")]

    def wf(agent, mid, text):
        return [econ_line(agent + "u", None, "user", "2026-10-10T08:05:30Z", "Zadanie przepływu", session=S, sidechain=True, agentId=agent),
                econ_line(agent + "a", agent + "u", "assistant", T[mid][3], [{"type": "text", "text": text}],
                          session=S, model="claude-x-small", mid=mid, usage=_usage(T[mid][4]), sidechain=True, agentId=agent)]
    e2 = [econ_line("b1", None, "user", "2026-10-10T12:00:00Z", "Krótkie pytanie o testy", session="sess-E2"),
          econ_line("b2", "b1", "assistant", "2026-10-10T12:00:30Z", [{"type": "text", "text": "Testy są w tests/."}],
                    session="sess-E2", model="claude-x-small", mid="msg_B1", usage=_usage(T["msg_B1"][4]))]
    files = {"sess-E1.jsonl": main, "sess-E1/subagents/agent-s1.jsonl": sub,
             "sess-E1/subagents/workflows/run-1/agent-w1.jsonl": wf("w1", "msg_W1", "Wynik w1"),
             "sess-E1/subagents/workflows/run-1/agent-w2.jsonl": wf("w2", "msg_W2", "Wynik w2 dłuższy"),
             "sess-E2.jsonl": e2}
    sessions = {"ccr": {"data": [
        {"id": "session_TEST1", "title": "Sesja testowa jeden", "created_at": "2026-10-10T07:59:00Z", "updated_at": "2026-10-11T09:10:00Z",
         "configured_model": "claude-x-large", "status": "idle",
         "external_metadata": {"usage": {"cache_read_tokens": 3000, "cache_write_tokens": 2000, "input_tokens": 30, "output_tokens": 300,
                                         "cost_usd": 1.5}}},
        {"id": "session_TEST2", "title": "Sesja bez transkryptu", "created_at": "2026-10-09T10:00:00Z", "updated_at": "2026-10-09T11:00:00Z",
         "configured_model": "claude-x-small",
         "external_metadata": {"usage": {"cache_read_tokens": 100, "cache_write_tokens": 50, "input_tokens": 5, "output_tokens": 20,
                                         "cost_usd": 0.25}}, "parent_session_id": "session_TEST1"}],
        "has_more": False, "first_id": "session_TEST1", "last_id": "session_TEST2"}}  # the shape list_sessions returns
    return files, sessions


def economics_expected():
    fam = {"sess-E1": [t for t in ECON_TURNS if t[0] != "sess-E2"], "sess-E2": [t for t in ECON_TURNS if t[0] == "sess-E2"]}

    def sums(ts):
        return {k: sum(t[4][i] for t in ts) for i, k in enumerate(("input", "output", "cache_creation", "cache_read"))}
    main_e1 = sums([t for t in ECON_TURNS if t[0] == "sess-E1"])
    return {"turns": len(ECON_TURNS), "duplicate_lines": sum(t[5] - 1 for t in ECON_TURNS),
            "totals": sums(ECON_TURNS), "family_E1": sums(fam["sess-E1"]), "main_E1": main_e1, "family_E2": sums(fam["sess-E2"]),
            "transcripts_E1": 4, "agents_E1": 3, "workflow_agents": 2, "files_touched_E1": ["src/a.py", "src/b.py"],
            "edit_calls_E1": 3, "edits_failed_E1": 1, "tool_calls": {"Edit": 2, "Write": 1, "Bash": 1, "Read": 1}, "tool_failed": {"Edit": 1},
            "ignored_synthetic": 1, "assistant_without_counters": 1, "unmapped_usage_fields": ["future_counter"], "web_search_requests": 1,
            "link_E1": "session_TEST1", "api_cost_TEST1": 1.5, "api_cache_read_TEST1": 3000,
            "days": {"2026-10-10": len([t for t in ECON_TURNS if t[3].startswith("2026-10-10")]),
                     "2026-10-11": len([t for t in ECON_TURNS if t[3].startswith("2026-10-11")])},
            "models": {"claude-x-large": 3, "claude-x-small": 4},
            "git_note": "the test builds the repository: 2 commits with session_TEST1 on main, 1 with session_TEST3 on a side branch, 1 without a trailer"}


# ═════════════════════════ Gemini Takeout ═════════════════════════
def gemini_entries():
    def e(t, prompt, html, **extra):
        d = {"header": "Gemini Apps", "title": "Prompted " + prompt, "time": t, "products": ["Gemini Apps"],
             "activityControls": ["Gemini Apps Activity"], "safeHtmlItem": [{"html": html}]}
        d.update(extra)
        return d
    return [
        e("2026-03-01T09:00:00.123Z", "Jak działa pamięć hierarchiczna w ChatADHD?", "<p>Pamięć <b>hierarchiczna</b> to drzewo węzłów.</p>"),
        e("2026-03-01T09:08:00.000Z", "A graf wiedzy?", "<p>Graf wiedzy łączy encje.</p>", attachments=[{"name": "schemat.png", "mimeType": "image/png"}]),
        e("2026-03-01T15:00:00.000Z", "Napisz test importera", "<p>Oto test.</p>", futureField={"x": 1}),
        e("2026-03-01T15:02:00.000Z", "Dodaj przypadek brzegowy", "<p>Dodano.</p>"),
    ]


def gemini_html():
    cells = [("Prompted Migracja pamięci do grafu", "Migracja: węzły stają się encjami.", "Mar 2, 2026, 10:00:00 AM UTC"),
             ("Prompted Co z duplikatami?", "Duplikaty scalamy po hashu.", "Mar 2, 2026, 10:05:00 AM UTC")]
    body = "".join(f'<div class="outer-cell"><div class="content-cell mdl-cell">{p}<br>{r}<br>{t}</div></div>' for p, r, t in cells)
    return f"<!DOCTYPE html><html><head><title>Gemini Apps</title></head><body><h1>Gemini Apps</h1>{body}</body></html>"


# ═════════════════════════ Grok ═════════════════════════
def grok_com():
    def r(rid, parent, sender, message, t, model=None):
        d = {"response_id": rid, "sender": sender, "message": message, "create_time": t, "query": "", "partial": False, "shared": False}
        if parent:
            d["parent_response_id"] = parent
        if model:
            d["model"] = model
        return {"response": d}
    return {"conversations": [
        {"conversation": {"conversation_id": "g1", "title": "Grok o ChatADHD", "create_time": "2026-02-02T12:00:00Z",
                          "modify_time": "2026-02-02T12:10:00Z"},
         "responses": [r("r1", None, "human", "Co sądzisz o ChatADHD?", "2026-02-02T12:00:00Z"),
                       r("r2", "r1", "assistant", "Mobilny klient czatu z grafem wiedzy.", "2026-02-02T12:01:00Z", "grok-3"),
                       r("r3", "r1", "assistant", "Regeneracja: klient Kivy z pamięcią.", "2026-02-02T12:02:00Z", "grok-3")]},
        {"conversation": {"conversation_id": "g2", "title": "Rekurencja po katalogach", "create_time": "2026-02-03T08:00:00Z"},
         "responses": [r("q1", None, "human", "Jak przejść katalog rekurencyjnie?", "2026-02-03T08:00:00Z"),
                       r("q2", None, "assistant", "os.walk albo rglob.", "2026-02-03T08:00:20Z", "grok-3")]},
    ]}


def grok_x_archive():
    items = [{"grokChatItem": {"chatId": "x1", "sender": "USER", "message": "Hej Grok, co to X-Port?", "createdAt": "2026-01-15T08:00:00.000Z"}},
             {"grokChatItem": {"chatId": "x1", "sender": "ASSISTANT", "message": "Narzędzie do ekstraktu archiwów.", "createdAt": "2026-01-15T08:00:10.000Z"}},
             {"grokChatItem": {"chatId": "x2", "sender": "USER", "message": "Druga rozmowa", "createdAt": "2026-01-16T08:00:00.000Z"}}]
    return "window.YTD.grok_chat_item.part0 = " + json.dumps(items, ensure_ascii=False)


# ═════════════════════════ our formats ═════════════════════════
def graph_packet():
    return {"schema": "loom.graph_packet/1", "packet_id": "pk-0001", "task": {"title": "Graf z rozmów o ChatADHD"},
            "definitions": [{"id": "d1", "term": "pamięć hierarchiczna"}],
            "entities": [{"id": "e1", "label": "ChatADHD"}, {"id": "e2", "label": "Loom"}],
            "claims": [{"id": "c1", "text": "ChatADHD ma pamięć hierarchiczną.", "subject": "e1", "known_at": "2026-04-02T10:00:00Z"},
                       {"id": "c2", "text": "Loom jest portem C++.", "subject": "e2", "known_at": "2026-04-02T10:01:00Z"},
                       {"id": "c3", "statement": "Graf buduje się w czasie rzeczywistym.", "known_at": "2026-04-02T10:02:00Z"}],
            "sources": [{"id": "s1", "locator": {"json_pointer": "/0"}}],
            "extras": {"future": True}}


def loom_export_conversation():
    return {"id": "lx1", "title": "Eksport Loom", "created": "2026-04-01T10:00:00Z",
            "messages": [
                {"id": "m1", "role": "user", "content": "Jak eksportować rozmowę?", "created": "2026-04-01T10:00:00Z"},
                {"id": "m2", "parent_id": "m1", "role": "assistant", "content": "Przez loom.export.v1.", "created": "2026-04-01T10:00:30Z",
                 "metadata": {"export": {"schema": "loom.export.v1", "blocks": [
                     {"kind": "thinking", "text": "Użytkownik pyta o eksport."}, {"kind": "text", "text": "Przez loom.export.v1."},
                     {"kind": "tool_use", "name": "python", "input": {"code": "print(1)"}}]}},
                 "attachments": ["attachments/notatka.txt"]}],
            "metadata": {"export": {"schema": "loom.export.v1", "provider": "openai", "locator": {"json_pointer": "/3"}}}}


# ═════════════════════════ mixed text-like directory ═════════════════════════
MIXED = {
    "notes.md": "# Notatki\n\n## User\nJak działa selektor trzypoziomowy?\n\n## Assistant\nEmbeddingi, TF-IDF, słowa kluczowe.\n",
    "chat.txt": "User: Co to Loom?\nAssistant: Jądro C++ dla ChatADHD.\nUser: A Kivy?\nAssistant: Klient mobilny.\n",
    "log.jsonl": "\n".join(json.dumps(x) for x in [
        {"role": "user", "content": "Pierwszy wiersz", "created": 1700000000},
        {"role": "assistant", "content": "Odpowiedź w JSONL", "created": "2023-11-14T22:13:30+00:00"}]) + "\n",
    "api_messages.json": json.dumps({"messages": [
        {"role": "user", "content": "Epoka", "created": 1700000000},
        {"role": "assistant", "content": "ISO ze strefą", "created": "2026-05-05T10:00:00+02:00"},
        {"role": "user", "content": "ISO bez strefy", "created": "2026-05-05T10:30:00"},
        {"role": "assistant", "content": "Bez czasu"}]}, ensure_ascii=False),
}


# ═════════════════════════ two exports of one account ═════════════════════════
TA = 1750000000.0  # 2025-06-15T15:06:40Z


def oa_linear(cid, title, t, turns, **extra):
    spec = [("root", None, None)]
    prev = "root"
    for i, (role, txt) in enumerate(turns):
        mid = f"{cid}-m{i}"
        spec.append((mid, prev, msg(mid, role, text(txt), t + 60 * i)))
        prev = mid
    return oa_conv(cid, title, t, spec, prev, **extra)


def export_a():
    c1 = oa_linear("c1", "Analizator archiwów — plan", TA, [("user", "Zaplanuj analizator archiwów"), ("assistant", "Rekurencja po katalogach, potem inwentarz.")])
    c2 = oa_linear("c2", "Importer ChatGPT", TA + 3600, [("user", "Jak czytać mapping?"), ("assistant", "Drzewo z current_node.")])
    c3 = oa_linear("c3", "Stara rozmowa", TA + 7200, [("user", "Usunięta później"), ("assistant", "Tak.")])
    return [("conversations.json", dumps([c1, c2, c3])),
            ("user.json", dumps({"id": "user-FAKE0002", "email": "user@example.invalid"}))]


def export_b():
    c1 = oa_linear("c1", "Analizator archiwów — plan", TA, [("user", "Zaplanuj analizator archiwów"), ("assistant", "Rekurencja po katalogach, potem inwentarz.")])
    c2 = oa_linear("c2", "Importer ChatGPT i Claude", TA + 3600,
                   [("user", "Jak czytać mapping?"), ("assistant", "Drzewo z current_node."),
                    ("user", "A chat_messages Claude?"), ("assistant", "parent_message_uuid.")])
    c2["update_time"] = TA + 90000
    c2["mapping"]["c2-m2"]["message"]["metadata"]["attachments"] = [{"id": "file-MISSING1", "name": "brak.pdf", "size": 10}]
    spec = [("root", None, None),
            ("c4-m0", "root", msg("c4-m0", "user", {"content_type": "multimodal_text", "parts": [
                {"content_type": "image_asset_pointer", "asset_pointer": "file-service://file-RESOLVED1", "size_bytes": 57, "width": 1, "height": 1},
                "Co jest na obrazku?"]}, TA + 86400)),
            ("c4-m1", "c4-m0", msg("c4-m1", "assistant", text("Schemat pamięci."), TA + 86460))]
    c4 = oa_conv("c4", "Nowa rozmowa z obrazkiem", TA + 86400, spec, "c4-m1")
    return [("conversations.json", dumps([c1, c2, c4])),
            ("user.json", dumps({"id": "user-FAKE0002", "email": "user@example.invalid"})),
            ("file-RESOLVED1-schemat.png", PNG)]


# ═════════════════════════ continuation ChatGPT → Claude ═════════════════════════
TC = 1782900000.0  # 2026-07-01T...


def continuation_chatgpt():
    c = oa_linear("cont1", "Analizator archiwów — rekurencja", TC,
                  [("user", "Analizator archiwów: rekurencja po katalogach i importer"), ("assistant", "Rekurencja po katalogach, importer, inwentarz.")])
    return [("conversations.json", dumps([c]))]


def continuation_claude():
    conv = claude_conv_linear("cl-cont", "Analizator archiwów — ciąg dalszy",
                              [("human", "Kontynuuję: analizator archiwów, rekurencja po katalogach, importer."),
                               ("assistant", "Dalej: inwentarz i rekurencja.")], t0="2026-07-01T10:26:40Z")
    for i, m in enumerate(conv["chat_messages"]):
        m["created_at"] = m["updated_at"] = f"2026-07-01T10:{26 + i}:40Z"
    return [("conversations.json", dumps([conv]))]


def write_damaged_zip(path, members, broken):
    """A zip whose member ``broken`` is stored with one flipped byte: reading it fails the CRC check (a partly
    downloaded or damaged export), every other member is intact."""
    import zipfile
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        for name, data in members:
            zi = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            zi.compress_type = zipfile.ZIP_STORED if name == broken else zipfile.ZIP_DEFLATED
            zi.external_attr = 0o644 << 16
            zf.writestr(zi, data if isinstance(data, bytes) else data.encode("utf-8"))
    raw = bytearray(buf.getvalue())
    with zipfile.ZipFile(io.BytesIO(bytes(raw))) as zf:
        info = zf.getinfo(broken)
    start = info.header_offset + 30 + len(info.filename.encode("utf-8")) + len(info.extra)
    raw[start + info.compress_size // 2] ^= 0xFF
    path.write_bytes(bytes(raw))


# ═════════════════════════ assemble + oracle ═════════════════════════
def sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    cc = OUT / "claude_code" / "projects" / "-home-user-proj"
    cc.mkdir(parents=True, exist_ok=True)
    (cc / "sess-0001.jsonl").write_text(claude_code_session_1(), encoding="utf-8")
    (cc / "sess-0002.jsonl").write_text(claude_code_session_2(), encoding="utf-8")

    gem = OUT / "gemini"
    gem.mkdir(exist_ok=True)
    write_zip(gem / "takeout.zip", [("Takeout/My Activity/Gemini Apps/MyActivity.json", json.dumps(gemini_entries(), ensure_ascii=False))])
    (gem / "MyActivity.html").write_text(gemini_html(), encoding="utf-8")

    gk = OUT / "grok"
    gk.mkdir(exist_ok=True)
    (gk / "grok_com.json").write_text(json.dumps(grok_com(), ensure_ascii=False, indent=1), encoding="utf-8")
    write_zip(gk / "x_archive.zip", [("data/grok-chat-item.js", grok_x_archive()), ("data/manifest.js", "window.__THAR_CONFIG = {}")])

    ours = OUT / "ours"
    ours.mkdir(exist_ok=True)
    (ours / "packet.json").write_text(json.dumps(graph_packet(), ensure_ascii=False, indent=1), encoding="utf-8")
    (ours / "conversation.json").write_text(json.dumps(loom_export_conversation(), ensure_ascii=False, indent=1), encoding="utf-8")

    mixed = OUT / "mixed_dir"
    mixed.mkdir(exist_ok=True)
    for name, body in MIXED.items():
        (mixed / name).write_text(body, encoding="utf-8")
    (mixed / "unknown.bin").write_bytes(bytes(range(256)) * 4)

    ver = OUT / "versions"
    ver.mkdir(exist_ok=True)
    write_zip(ver / "chatgpt_2025-06-20.zip", export_a())
    write_zip(ver / "chatgpt_2025-10-01.zip", export_b())
    dup = OUT / "duplicate"
    dup.mkdir(exist_ok=True)
    (dup / "chatgpt_2025-06-20_copy.zip").write_bytes((ver / "chatgpt_2025-06-20.zip").read_bytes())

    dmg = OUT / "damaged"
    dmg.mkdir(exist_ok=True)
    write_damaged_zip(dmg / "chatgpt_damaged.zip", export_a() + [("file-Broken1-uszkodzony.png", PNG)], "file-Broken1-uszkodzony.png")

    econ = OUT / "economics"
    files, sessions = economics_files()
    for rel, lines in files.items():
        f = econ / "projects" / "-home-user-econ" / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text("\n".join(lines) + "\n", encoding="utf-8")
    (econ / "sessions.json").write_text(json.dumps(sessions, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")

    cont = OUT / "continuation"
    cont.mkdir(exist_ok=True)
    write_zip(cont / "chatgpt_part.zip", continuation_chatgpt())
    write_zip(cont / "claude_part.zip", continuation_claude())

    expected = {
        "claude_code/projects/-home-user-proj": {
            "format": "anthropic.claude_code.session", "units": 2, "kind": "session",
            "sess-0001": {"messages": 9, "user": 5, "assistant": 3, "system": 1, "compacted": 1, "sidechain": 1,
                          "tool_use": 2, "tool_result": 2, "thinking": 1, "text": 6, "system_blocks": 1, "forks": 1, "leaves": 2,
                          "unknown_entries": 2, "internal_lines": {"file-history-snapshot": 1, "queue-operation": 1}, "summaries": 1,
                          "tool_mentions": {"Read": 2, "Edit": 2}, "tool_mentions_note": "tool_use + its bound tool_result",
                          "created_utc": "2026-10-10T08:00:00Z", "created_src_zone": "+02:00"},
            "sess-0002": {"messages": 2, "tool_use": 0, "forks": 0}},
        "gemini/takeout.zip": {"format": "google.gemini.takeout", "conversations": 2, "messages": 8, "attachments": 1,
                               "attachments_missing": 1, "unknown_entries": 1, "hypothesis": True, "grouping_inferred": True,
                               "created_utc_first": "2026-03-01T09:00:00.123000Z"},
        "gemini/MyActivity.html": {"format": "google.gemini.takeout", "conversations": 1, "messages": 4, "created_utc": "2026-03-02T10:00:00Z"},
        "grok/grok_com.json": {"format": "xai.grok.export", "conversations": 2, "messages": 5, "forks": 1, "models": {"grok-3": 3}},
        "grok/x_archive.zip": {"format": "xai.grok.export", "conversations": 2, "messages": 3, "members_unknown": 1, "units": 3},
        "ours/packet.json": {"format": "loom.graph_packet", "units": 1, "kind": "graph", "messages": 3, "unknown_entries": 1},
        "ours/conversation.json": {"format": "loom.export", "conversations": 1, "messages": 2, "service": "openai",
                                   "blocks": {"text": 2, "thinking": 1, "tool_use": 1}, "attachments": 1},
        "mixed_dir": {"members": 5, "members_unknown": 1, "conversations": 4, "messages": 12,
                      "formats": {"notes.md": "textlike.markdown", "chat.txt": "textlike.text", "log.jsonl": "textlike.jsonl",
                                  "api_messages.json": "textlike.json_messages", "unknown.bin": None},
                      "api_messages_times": [{"utc": "2023-11-14T22:13:20Z", "how": "epoch"}, {"utc": "2026-05-05T08:00:00Z", "how": "iso"},
                                             {"utc": "2026-05-05T10:30:00Z", "how": "iso_nozone"}, {"utc": None, "how": "conversation"}]},
        "versions": {"old": "chatgpt_2025-06-20.zip", "new": "chatgpt_2025-10-01.zip",
                     "diff": {"new": 1, "changed": 1, "removed": 1, "unchanged": 1, "messages_added": 2, "messages_changed": 0,
                              "messages_removed": 0, "title_changed": ["c2"], "new_attachments": {"c2": 1}},
                     "merge_both": {"identities": 5, "duplicates": 2, "versions": 1, "note": "4 conversations + 1 account unit (user.json in both)"},
                     "attachments_new": {"referenced": 2, "resolved": 1, "missing": 1}},
        "duplicate": {"with": "versions/chatgpt_2025-06-20.zip", "duplicates": 4, "identities": 4, "note": "3 conversations + account"},
        "continuation": {"from_title": "Analizator archiwów — rekurencja", "to_title": "Analizator archiwów — ciąg dalszy",
                         "from_service": "openai", "to_service": "anthropic", "gap_minutes_max": 240,
                         "shared_terms_min": 2},
        "damaged/chatgpt_damaged.zip": {"unreadable": ["file-Broken1-uszkodzony.png"], "conversations": 3,
                                         "note": "one stored member with a flipped byte (CRC error); the rest reads"},
        "forbidden_in_aggregate": FORBIDDEN,
        "economics": dict(economics_expected(), forbidden_in_aggregate=ECON_FORBIDDEN),
    }
    (OUT / "EXPECTED.json").write_text(json.dumps(expected, ensure_ascii=False, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    print("wrote fixtures to", OUT)


if __name__ == "__main__":
    main()
