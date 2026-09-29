#!/usr/bin/env python3
"""Deterministic, synthetic OpenAI (ChatGPT) and Anthropic (Claude) data-export
fixtures for loom/tests/test_import_exports.cpp.

Every structure follows docs/exports/OPENAI_ANTHROPIC_EXPORT_FORMATS.md (fields
marked V there come from working third-party parsers; U fields are invented but
plausible and are kept OPEN by the importer). No real personal data: every name,
e-mail, id and text below is made up.

Outputs (loom/tests/fixtures/exports/):
    openai_legacy.zip              2023-era: bare-array conversations.json, user.json,
                                   message_feedback.json, shared_conversations.json,
                                   chat.html, model_comparisons.json
    openai_2026_sharded.zip        conversations-000.json ({"conversations":[..]}) +
                                   conversations-001.json (bare array), every content
                                   type/part/metadata variant, assets in every layout
                                   (root file-*, dalle-generations/, user-*/, .dat),
                                   textdocs/, group_chats/shopping/sora/... and one
                                   unknown future member
    openai_single_conversations.json   bare minified file, unicode-heavy
    anthropic_legacy.zip           2024-era: linear chat_messages with text only
    anthropic_2026_full.zip        content blocks (text/thinking/voice_note/tool_use/
                                   tool_result/image/document/unknown), branches via
                                   parent_message_uuid, attachments/files/files_v2,
                                   artifacts, projects, memories, users
    anthropic_single_chat.json     single-chat export shape (IndividualChatSchema)
    nested_container.zip           zip of zips (+ a note)
    malformed_export.zip           truncated / invalid-UTF-8 / zip-slip / lone surrogate
    unknown_provider.zip           an export no registry entry knows (inference test)
    EXPECTED.json                  oracle counts computed HERE, independently of the
                                   C++ importer, straight from the generated data

Run:  python3 loom/tools/gen_export_fixtures.py     (idempotent, byte-identical)
"""
import hashlib
import io
import json
import pathlib
import zipfile

OUT = pathlib.Path(__file__).resolve().parents[1] / "tests" / "fixtures" / "exports"

T0 = 1760000000.0  # 2025-10-09T08:53:20Z
PUA_A, PUA_B, PUA_C = "", "", ""  # invisible citation delimiters


def cite(turn, idx):
    return f"{PUA_A}cite{PUA_B}turn{turn}search{idx}{PUA_C}"


def dumps(obj, indent=None, ascii_=False):
    return json.dumps(obj, ensure_ascii=ascii_, separators=(",", ":") if indent is None else None,
                      indent=indent)


# Tiny deterministic binaries -------------------------------------------------
PNG_A = bytes.fromhex("89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c489") + b"A" * 24
PNG_B = bytes.fromhex("89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c489") + b"B" * 24
PNG_C2PA = PNG_A + b"c2pa.manifest"
WEBP = b"RIFF\x24\x00\x00\x00WEBPVP8 " + b"W" * 20
WAV = b"RIFF\x24\x00\x00\x00WAVEfmt " + b"V" * 20
PDF = b"%PDF-1.4\n% synthetic fixture\n%%EOF\n"


def write_zip(path, members):
    """members: list of (name, bytes|str). Deterministic order/timestamps."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for name, data in members:
            zi = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = 0o644 << 16
            zf.writestr(zi, data if isinstance(data, bytes) else data.encode("utf-8"))
    path.write_bytes(buf.getvalue())


# ═══════════════════════════ OpenAI builders ══════════════════════════════
def msg(id_, role, content, t, *, name=None, recipient="all", meta=None, status="finished_successfully",
        end_turn=None, weight=1.0, channel=None, omit=()):
    m = {"id": id_, "author": {"role": role, "name": name, "metadata": {}}, "create_time": t,
         "update_time": None, "content": content, "status": status, "end_turn": end_turn,
         "weight": weight, "metadata": meta if meta is not None else {}, "recipient": recipient,
         "channel": channel}
    for k in omit:
        m.pop(k, None)
    return m


def text(*parts):
    return {"content_type": "text", "parts": list(parts)}


def build_mapping(spec):
    """spec: list of (id, parent, message|None). children derived in order."""
    mapping = {i: {"id": i, "message": m, "parent": p, "children": []} for i, p, m in spec}
    for i, p, _ in spec:
        if p is not None and p in mapping:
            mapping[p]["children"].append(i)
    return mapping


def oa_conv(cid, title, t, spec, current, **extra):
    d = {"title": title, "create_time": t, "update_time": t + 600.0, "mapping": build_mapping(spec),
         "moderation_results": [], "current_node": current, "plugin_ids": None, "conversation_id": cid,
         "conversation_template_id": None, "gizmo_id": None, "gizmo_type": None, "is_archived": False,
         "is_starred": None, "safe_urls": [], "default_model_slug": "gpt-5-t-mini", "id": cid}
    d.update(extra)
    return d


# ── C1: forks (edit + regenerate), thoughts, code interpreter, canvas, citations ──
def conv_c1():
    t = T0
    ctx = {"model_slug": "gpt-5-t-mini", "parent_id": "c1-root"}
    spec = [
        ("c1-root", None, None),
        ("c1-sys", "c1-root", msg("c1-sys", "system", text(""), None, weight=0.0,
                                  meta={"is_visually_hidden_from_conversation": True}, recipient="all")),
        ("c1-u1", "c1-sys", msg("c1-u1", "user", text("Jak zaplanować migrację bazy SQLite do PostgreSQL?"), t + 1,
                                meta={"request_id": "req-0001", "message_source": None,
                                      "serialization_metadata": {"custom_symbol_offsets": []}})),
        ("c1-th1", "c1-u1", msg("c1-th1", "assistant",
                                {"content_type": "thoughts", "thoughts": [
                                    {"summary": "Porównuję ryzyka migracji", "content": "Najpierw schemat, potem dane.",
                                     "chunks": ["Najpierw schemat,", " potem dane."], "finished": True},
                                    {"summary": "Wybieram narzędzia", "content": "pgloader albo własny skrypt.",
                                     "chunks": [], "finished": True}]},
                                t + 2, channel="analysis", recipient="all",
                                meta={"is_thinking_preamble_message": False, "model_slug": "gpt-5-t-mini"})),
        ("c1-rr1", "c1-th1", msg("c1-rr1", "assistant",
                                 {"content_type": "reasoning_recap", "content": "Thought for 12s"}, t + 3,
                                 meta={"finished_duration_sec": 12, "reasoning_title": "Plan migracji"})),
        ("c1-a1", "c1-rr1", msg("c1-a1", "assistant",
                                text("Zacznij od inwentaryzacji schematu " + cite(0, 1) + ", potem migruj dane w partiach."),
                                t + 4, end_turn=True,
                                meta={"model_slug": "gpt-5-t-mini", "finish_details": {"type": "stop", "stop_tokens": [200002]},
                                      "citations": [{"start_ix": 34, "end_ix": 40, "citation_format_type": "tether_og",
                                                     "metadata": {"title": "PostgreSQL docs", "url": "https://example.invalid/pg",
                                                                  "text": "COPY is fast", "type": "webpage",
                                                                  "extra": {"cited_message_idx": 0, "evidence_text": "COPY"}}}],
                                      "content_references": [
                                          {"matched_text": cite(0, 1), "start_idx": 34, "end_idx": 40, "type": "grouped_webpages",
                                           "items": [{"title": "PostgreSQL docs", "url": "https://example.invalid/pg",
                                                      "attribution": "example.invalid"}],
                                           "safe_urls": ["https://example.invalid/pg"], "refs": [{"turn_index": 0, "ref_type": "search", "ref_index": 1}],
                                           "alt": "([PostgreSQL docs](https://example.invalid/pg))"},
                                          {"matched_text": "", "start_idx": 0, "end_idx": 0, "type": "sources_footnote",
                                           "sources": [{"title": "PostgreSQL docs", "url": "https://example.invalid/pg"}]}],
                                      "search_result_groups": [{"type": "search_result_group", "domain": "example.invalid",
                                                                "entries": [{"type": "search_result", "url": "https://example.invalid/pg",
                                                                             "title": "PostgreSQL docs", "snippet": "COPY is fast",
                                                                             "ref_id": {"turn_index": 0, "ref_type": "search", "ref_index": 1},
                                                                             "pub_date": None, "attribution": "example.invalid"}]}],
                                      "_cite_metadata": {"citation_format": {"name": "tether_og"},
                                                         "metadata_list": [{"title": "PostgreSQL docs", "url": "https://example.invalid/pg",
                                                                            "text": "COPY is fast"}]}})),
        # edit fork: two user children of a1
        ("c1-u2", "c1-a1", msg("c1-u2", "user", text("A co z indeksami i sekwencjami?"), t + 10)),
        ("c1-u2b", "c1-a1", msg("c1-u2b", "user", text("A co z triggerami?"), t + 11)),
        # branch u2: code interpreter round trip + canvas
        ("c1-a2", "c1-u2", msg("c1-a2", "assistant",
                               {"content_type": "code", "language": "python", "text": "print(len(tables))",
                                "response_format_name": None}, t + 12, recipient="python")),
        ("c1-t1", "c1-a2", msg("c1-t1", "tool", {"content_type": "execution_output", "text": "42\n"}, t + 13, name="python",
                               meta={"aggregate_result": {"status": "success", "run_id": "run-1", "start_time": t + 12.1,
                                                          "update_time": t + 12.9, "end_time": t + 13.0,
                                                          "code": "print(len(tables))", "final_expression_output": "42",
                                                          "jupyter_messages": [{"msg_type": "status", "parent_header": {}}],
                                                          "messages": [{"message_type": "image", "time": t + 12.8, "sender": "server",
                                                                        "image_url": "file-service://file-Plot0001", "width": 640,
                                                                        "height": 480}]},
                                     "is_complete": True})),
        ("c1-a3", "c1-t1", msg("c1-a3", "assistant", text("W bazie jest 42 tabel."), t + 14, end_turn=True)),
        ("c1-a4", "c1-a3", msg("c1-a4", "assistant",
                               {"content_type": "code", "language": "json", "text": dumps(
                                   {"name": "plan-migracji", "type": "document", "content": "# Plan\n1. Schemat\n2. Dane\n3. Weryfikacja"})},
                               t + 15, recipient="canmore.create_textdoc")),
        # branch u2b: regenerate x3 (three assistant siblings)
        ("c1-a5", "c1-u2b", msg("c1-a5", "assistant", text("Triggery przenieś ręcznie."), t + 20, end_turn=True)),
        ("c1-a5b", "c1-u2b", msg("c1-a5b", "assistant", text("Triggery: użyj pg_dump --schema-only."), t + 21, end_turn=True)),
        ("c1-a5c", "c1-u2b", msg("c1-a5c", "assistant", text("Triggery: przepisz na PL/pgSQL."), t + 22, end_turn=True)),
    ]
    return oa_conv("c1-0000-aaaa", "Migracja bazy danych", t, spec, "c1-a4", is_starred=True,
                   voice=None, model_slug="gpt-5-t-mini")


# ── C2: browsing/search tools, sonic_webpage, tether_*, system_error ──
def conv_c2():
    t = T0 + 5000
    spec = [
        ("c2-root", None, None),
        ("c2-u1", "c2-root", msg("c2-u1", "user", text("Sprawdź w sieci, czym jest CRDT."), t + 1)),
        ("c2-a1", "c2-u1", msg("c2-a1", "assistant", {"content_type": "code", "language": "unknown",
                                                     "text": 'search("CRDT explained")'}, t + 2, recipient="browser",
                               meta={"command": "search", "args": ["CRDT explained"]})),
        ("c2-t1", "c2-a1", msg("c2-t1", "tool", {"content_type": "tether_browsing_display",
                                                 "result": "# 【0†CRDT†example.invalid】\nConflict-free replicated data types.",
                                                 "summary": "Search results"}, t + 3, name="browser",
                               meta={"_cite_metadata": {"citation_format": {"name": "tether_og"}, "metadata_list": []}})),
        ("c2-t2", "c2-t1", msg("c2-t2", "tool", {"content_type": "tether_quote", "domain": "example.invalid",
                                                 "text": "A CRDT is a data structure replicated across nodes.",
                                                 "title": "CRDT explained", "url": "https://example.invalid/crdt"}, t + 4,
                               name="browser")),
        ("c2-t3", "c2-t2", msg("c2-t3", "tool", {"content_type": "tether_browsing_code"}, t + 5, name="browser")),
        ("c2-a2", "c2-t3", msg("c2-a2", "assistant", {"content_type": "code", "language": "unknown", "text": "{\"search_query\": [{\"q\": \"CRDT\"}]}"},
                               t + 6, recipient="web.run")),
        ("c2-t4", "c2-a2", msg("c2-t4", "tool", {"content_type": "sonic_webpage", "url": "https://example.invalid/crdt",
                                                 "domain": "example.invalid", "title": "CRDT explained",
                                                 "text": "Long scraped page text about CRDTs...", "snippet": "CRDTs...",
                                                 "pub_timestamp": T0 - 86400.0}, t + 7, name="web.run")),
        ("c2-t5", "c2-t4", msg("c2-t5", "tool", {"content_type": "multimodal_text", "parts": [
            {"type": "search_result_group", "domain": "example.invalid", "entries": [
                {"type": "search_result", "url": "https://example.invalid/crdt", "title": "CRDT explained",
                 "snippet": "Conflict-free...", "ref_id": {"turn_index": 1, "ref_type": "search", "ref_index": 0},
                 "pub_date": None, "attribution": "example.invalid"},
                {"type": "search_result", "url": "https://example.invalid/ot", "title": "OT vs CRDT",
                 "snippet": "Operational transformation...", "ref_id": {"turn_index": 1, "ref_type": "search", "ref_index": 1},
                 "pub_date": None, "attribution": "example.invalid"}]}]}, t + 8, name="web.run")),
        ("c2-t6", "c2-t5", msg("c2-t6", "tool", {"content_type": "system_error", "name": "Error", "text": "robots.txt denied"},
                               t + 9, name="web.run")),
        ("c2-a3", "c2-t6", msg("c2-a3", "assistant", text("CRDT to typ danych replikowany bez koordynacji " + cite(1, 0) + "."),
                               t + 10, end_turn=True,
                               meta={"content_references": [{"matched_text": cite(1, 0), "start_idx": 44, "end_idx": 50,
                                                             "type": "webpage", "title": "CRDT explained",
                                                             "url": "https://example.invalid/crdt", "attribution": "example.invalid"}]})),
    ]
    return oa_conv("c2-0000-bbbb", "Wyszukiwanie w sieci", t, spec, "c2-a3", is_archived=True)


# ── C3: images, audio, attachments in every naming layout, project ref, voice object ──
def conv_c3():
    t = T0 + 9000
    spec = [
        ("c3-root", None, None),
        ("c3-u1", "c3-root", msg("c3-u1", "user", {"content_type": "multimodal_text", "parts": [
            {"content_type": "image_asset_pointer", "asset_pointer": "file-service://file-A1b2C3d4E5f6G7h8",
             "size_bytes": len(PNG_A), "width": 1, "height": 1, "fovea": 512, "metadata": {"dalle": None, "sanitized": True}},
            "Co jest na tym schemacie?"]}, t + 1,
                                 meta={"attachments": [{"id": "file-A1b2C3d4E5f6G7h8", "name": "schemat.png",
                                                        "mime_type": "image/png", "size": len(PNG_A)}]})),
        ("c3-a1", "c3-u1", msg("c3-a1", "assistant", text("To schemat bazy."), t + 2, end_turn=True)),
        ("c3-u2", "c3-a1", msg("c3-u2", "user", text("Zapisz obraz do szkicu i porównaj raport."), t + 3,
                               meta={"attachments": [
                                   {"id": "file-NoSuchId0", "name": "raport-q3.pdf", "mime_type": "application/pdf", "size": len(PDF)},
                                   {"id": "file-Boundary1", "name": "granica.png", "mime_type": "image/png", "size": len(PNG_A)},
                                   {"id": "file-Dup1", "name": "kopia1.png", "mime_type": "image/png", "size": len(PNG_B)},
                                   {"id": "file-Dup2", "name": "kopia2.png", "mime_type": "image/png", "size": len(PNG_B)},
                                   {"id": "file-Zz9Dat", "name": "wykres.png", "mime_type": "image/png", "size": len(PNG_C2PA)}]})),
        ("c3-a2", "c3-u2", msg("c3-a2", "assistant", {"content_type": "code", "language": "json",
                                                     "text": '{"prompt": "szkic domu", "size": "1024x1024"}'}, t + 4,
                               recipient="dalle.text2im")),
        ("c3-t1", "c3-a2", msg("c3-t1", "tool", {"content_type": "multimodal_text", "parts": [
            {"content_type": "image_asset_pointer", "asset_pointer": "file-service://file-Dq3Zx9", "size_bytes": len(WEBP),
             "width": 1024, "height": 1024, "fovea": 512,
             "metadata": {"dalle": {"gen_id": "gen-0001", "prompt": "szkic domu", "seed": 12345, "serialization_title": "DALL-E generation metadata"}}},
            "DALL·E displayed 1 images."]}, t + 5, name="dalle.text2im")),
        ("c3-a3", "c3-t1", msg("c3-a3", "assistant", {"content_type": "multimodal_text", "parts": [
            "Oto wersja nowsza:",
            {"content_type": "image_asset_pointer", "asset_pointer": "sediment://file_00000000c0ffee01", "size_bytes": len(PNG_A),
             "width": 1, "height": 1, "fovea": None, "metadata": {"generation": {"gen_id": "gen-0002"}}},
            {"content_type": "image_asset_pointer", "asset_pointer": "sediment://file_00000000feed0002", "size_bytes": len(PNG_B),
             "width": 1, "height": 1, "fovea": None, "metadata": {}},
            {"content_type": "image_asset_pointer", "asset_pointer": "sediment://file_00000000dead0000", "size_bytes": 10,
             "width": 1, "height": 1, "fovea": None, "metadata": {}}]}, t + 6, end_turn=True)),
        ("c3-u3", "c3-a3", msg("c3-u3", "user", {"content_type": "multimodal_text", "parts": [
            {"content_type": "real_time_user_audio_video_asset_pointer", "expiry_datetime": "2026-01-01T00:00:00Z",
             "frames_asset_pointers": [], "video_container_asset_pointer": None,
             "audio_asset_pointer": {"expiry_datetime": "2026-01-01T00:00:00Z", "content_type": "audio_asset_pointer",
                                     "asset_pointer": "sediment://file_00000000a0d10001", "size_bytes": len(WAV), "format": "wav",
                                     "metadata": {"start_timestamp": None, "end_timestamp": None, "pretokenized_vq": None,
                                                  "interruptions": None, "original_audio_source": None, "transcription": None,
                                                  "word_transcription": None, "start": 0.0, "end": 1.5}},
             "audio_start_timestamp": 0.5},
            {"content_type": "audio_transcription", "decoding_id": None, "direction": "in", "text": "Cześć, słychać mnie?"}]},
                               t + 7)),
        ("c3-a4", "c3-u3", msg("c3-a4", "assistant", {"content_type": "multimodal_text", "parts": [
            {"content_type": "audio_transcription", "decoding_id": None, "direction": "out", "text": "Tak, słychać dobrze."},
            {"content_type": "audio_asset_pointer", "audio_asset_pointer": None, "asset_pointer": "sediment://file_00000000a0d10001",
             "expiry_datetime": "2026-01-01T00:00:00Z", "format": "wav", "size_bytes": len(WAV),
             "metadata": {"start_timestamp": 0.0, "end_timestamp": 1.0, "pretokenized_vq": None}}]}, t + 8, end_turn=True)),
    ]
    return oa_conv("c3-0000-cccc", "Obrazy i dźwięk", t, spec, "c3-a4", gizmo_id="g-p-0123456789abcdef-zenit",
                   gizmo_type="gpt", voice={"name": "cove", "engine": "advanced"}, conversation_origin=None)


# ── C4: custom instructions, memory, unknown content type, July-2026 omissions, unicode ──
def conv_c4():
    t = T0 + 14000
    spec = [
        ("c4-root", None, None),
        ("c4-ci", "c4-root", msg("c4-ci", "system", {"content_type": "user_editable_context",
                                                     "user_profile": "Lubię krótkie odpowiedzi. Mieszkam w Krakowie (fikcyjnie).",
                                                     "user_instructions": "Odpowiadaj po polsku."}, None,
                                 meta={"is_user_system_message": True, "is_visually_hidden_from_conversation": True,
                                       "user_context_message_data": {"about_user_message": "Lubię krótkie odpowiedzi.",
                                                                     "about_model_message": "Odpowiadaj po polsku."}})),
        ("c4-mem", "c4-ci", msg("c4-mem", "system", {"content_type": "model_editable_context",
                                                     "model_set_context": "1. [2025-09-01]. Użytkownik pracuje nad projektem Zenit."},
                                None, meta={"is_visually_hidden_from_conversation": True})),
        ("c4-u1", "c4-mem", msg("c4-u1", "user", text("Zapamiętaj: preferuję SQLite. Zażółć gęślą jaźń 😀 שלום עולם مرحبا 👨‍👩‍👧 é"),
                                t + 1, omit=("status", "weight"))),
        ("c4-a1", "c4-u1", msg("c4-a1", "assistant", {"content_type": "code", "language": "unknown",
                                                     "text": '{"content": "Prefers SQLite"}'}, t + 2, recipient="bio",
                               omit=("status", "weight"))),
        ("c4-t1", "c4-a1", msg("c4-t1", "tool", text("Model set context updated."), t + 3, name="bio", omit=("status", "weight"))),
        ("c4-a2", "c4-t1", msg("c4-a2", "assistant", text("Zapamiętane \ud83d (samotny surogat) i emoji \U0001F600."), t + 4,
                               omit=("status", "weight"), end_turn=True)),
        ("c4-a3", "c4-a2", msg("c4-a3", "assistant", {"content_type": "future_widget", "widget": {"kind": "chart", "series": [1, 2, 3]},
                                                     "caption": "Nowy typ treści"}, t + 5,
                               meta={"future_metadata_key": {"a": [1, 2, {"b": None}]}, "message_type": "next"})),
    ]
    c = oa_conv("c4-0000-dddd", None, t, spec, "c4-a3", voice="cove", is_starred=False,
                future_conversation_field={"flag": True, "list": [], "obj": {}}, is_study_mode=False, memory_scope="project_enabled",
                context_scopes=None, pinned_time=T0 + 20000, is_do_not_remember=False, blocked_urls=[], disabled_tool_ids=[],
                async_status=None, sugar_item_id=None, sugar_item_visible=False, owner=None)
    return c


# ── C5: pathologies ──
def conv_c5():
    t = T0 + 20000
    spec = [
        ("c5-root", None, None),
        ("c5-u1", "c5-root", msg("c5-u1", "user", text("Pathological tree"), t + 1)),
        ("c5-a1", "c5-u1", msg("c5-a1", "assistant", text("Reply before dangling"), t + 2)),
        ("c5-orphan", "c5-MISSING", msg("c5-orphan", "assistant", text("Orphan: parent absent from mapping"), t + 3)),
        ("c5-null", "c5-a1", msg("c5-null", "assistant", None, t + 4)),  # content null
        ("c5-cyc1", "c5-cyc2", msg("c5-cyc1", "user", text("cycle one"), t + 5)),
        ("c5-cyc2", "c5-cyc1", msg("c5-cyc2", "assistant", text("cycle two"), t + 6)),
    ]
    mapping = build_mapping(spec)
    mapping["c5-a1"]["children"].append("c5-ghost")  # child id absent from mapping
    d = oa_conv("c5-0000-eeee", "Patologie", t, spec, "c5-does-not-exist")
    d["mapping"] = mapping
    return d


def conv_empty():
    return {"title": "Pusta", "create_time": T0 + 30000, "update_time": T0 + 30001, "mapping": {}, "current_node": None,
            "conversation_id": "c6-0000-ffff", "id": "c6-0000-ffff"}


def conv_no_mapping():
    return {"title": "Bez mapping", "create_time": T0 + 31000, "update_time": T0 + 31001, "current_node": None,
            "conversation_id": "c7-0000-0000", "id": "c7-0000-0000", "mapping": None}


def legacy_conv(cid, title, t, texts):
    spec = [(cid + "-root", None, None)]
    prev = cid + "-root"
    for i, (role, txt) in enumerate(texts):
        nid = f"{cid}-{i}"
        spec.append((nid, prev, {"id": nid, "author": {"role": role, "metadata": {}}, "create_time": t + i,
                                 "content": {"content_type": "text", "parts": [txt]}, "status": "finished_successfully",
                                 "end_turn": role == "assistant", "weight": 1.0, "metadata": {"timestamp_": "absolute"},
                                 "recipient": "all"}))
        prev = nid
    return {"title": title, "create_time": t, "update_time": t + len(texts), "mapping": build_mapping(spec),
            "moderation_results": [], "current_node": prev, "plugin_ids": None, "conversation_id": cid, "id": cid}


# ═══════════════════════════ Anthropic builders ═══════════════════════════
def cl_msg(uuid, sender, text_, t, content=None, **extra):
    m = {"uuid": uuid, "text": text_, "content": content if content is not None else
         [{"start_timestamp": t, "stop_timestamp": t, "type": "text", "text": text_, "citations": []}],
         "sender": sender, "created_at": t, "updated_at": t, "attachments": [], "files": []}
    m.update(extra)
    return m


def claude_conv_full():
    t = "2025-11-02T10:00:00.000000Z"
    msgs = [
        cl_msg("m-u1", "human", "Zaprojektuj widok listy z artefaktem.", "2025-11-02T10:00:01.000000Z",
               parent_message_uuid="00000000-0000-4000-8000-000000000000", index=0, truncated=False,
               attachments=[{"id": "att-1", "file_name": "notatki.txt", "file_size": 22, "file_type": "text/plain",
                             "extracted_content": "Notatki: lista, filtr.", "created_at": "2025-11-02T10:00:00.500000Z"}],
               files=[{"file_kind": "image", "file_uuid": "f-uuid-1", "file_name": "makieta.png",
                       "created_at": "2025-11-02T10:00:00.400000Z", "thumbnail_url": "/api/f-uuid-1/thumbnail",
                       "preview_url": "/api/f-uuid-1/preview",
                       "thumbnail_asset": {"url": "/t.png", "file_variant": "thumbnail", "primary_color": "ffffff", "image_width": 10, "image_height": 10},
                       "preview_asset": {"url": "/p.png", "file_variant": "preview", "primary_color": "ffffff", "image_width": 100, "image_height": 100}}],
               files_v2=[{"file_kind": "image", "file_uuid": "f-uuid-1", "file_name": "makieta.png",
                          "created_at": "2025-11-02T10:00:00.400000Z"}],
               sync_sources=[]),
        cl_msg("m-a1", "assistant", "Zacznę od myślenia i narzędzi.", "2025-11-02T10:00:05.000000Z",
               parent_message_uuid="m-u1", index=1, truncated=False,
               content=[
                   {"start_timestamp": "2025-11-02T10:00:02.000000Z", "stop_timestamp": "2025-11-02T10:00:03.000000Z", "type": "thinking",
                    "thinking": "Użytkownik chce widok listy; artefakt HTML.", "summaries": [{"summary": "Plan widoku"}, {"summary": "Wybór artefaktu"}],
                    "cut_off": False},
                   {"start_timestamp": "2025-11-02T10:00:03.000000Z", "stop_timestamp": "2025-11-02T10:00:03.500000Z", "type": "text",
                    "text": "Zacznę od myślenia i narzędzi.", "citations": [
                        {"uuid": "cit-1", "start_index": 0, "end_index": 6, "details": {"type": "web_search_citation", "url": "https://example.invalid/list"}}]},
                   {"start_timestamp": "2025-11-02T10:00:03.600000Z", "stop_timestamp": "2025-11-02T10:00:03.700000Z", "type": "tool_use",
                    "name": "web_search", "input": {"query": "list view UX"}, "message": "Searching", "integration_name": None,
                    "integration_icon_url": None, "context": None, "display_content": None, "approval_options": None, "approval_key": None},
                   {"start_timestamp": "2025-11-02T10:00:03.800000Z", "stop_timestamp": "2025-11-02T10:00:03.900000Z", "type": "tool_result",
                    "name": "web_search", "content": [
                        {"type": "knowledge", "title": "List UX", "url": "https://example.invalid/list", "text": "Lists are ...", "uuid": "k-1",
                         "metadata": {"site_domain": "example.invalid"}},
                        {"type": "rag_reference", "doc_uuid": "d-1", "extras": {"page": 3}},
                        {"type": "text", "text": "Found 2 results.", "uuid": "t-1"}],
                    "is_error": False, "message": None, "integration_name": None, "integration_icon_url": None, "display_content": None},
                   {"start_timestamp": "2025-11-02T10:00:04.000000Z", "stop_timestamp": "2025-11-02T10:00:04.100000Z", "type": "tool_use",
                    "name": "artifacts", "input": {"id": "list-view", "type": "text/html", "title": "Widok listy", "command": "create",
                                                   "content": "<ul><li>Zażółć</li></ul>", "language": None, "version_uuid": "ver-1"},
                    "message": None, "integration_name": None, "integration_icon_url": None, "context": None, "display_content": None,
                    "approval_options": None, "approval_key": None},
                   {"start_timestamp": "2025-11-02T10:00:04.200000Z", "stop_timestamp": "2025-11-02T10:00:04.300000Z", "type": "tool_result",
                    "name": "artifacts", "content": [{"type": "text", "text": "OK", "uuid": "t-2"}], "is_error": False, "message": None,
                    "integration_name": None, "integration_icon_url": None, "display_content": None},
                   {"start_timestamp": "2025-11-02T10:00:04.400000Z", "stop_timestamp": "2025-11-02T10:00:04.500000Z", "type": "voice_note",
                    "title": "Notatka głosowa", "text": "To jest transkrypcja notatki głosowej."},
                   {"type": "image", "source": {"type": "base64", "media_type": "image/png", "data": "iVBORw0KGgo="}},
                   {"type": "document", "title": "Specyfikacja", "source": {"type": "text", "media_type": "text/plain", "data": "Spec."}},
                   {"type": "web_widget", "payload": {"x": [1, 2]}},
               ]),
        # branch: two assistant continuations after m-a1 (edit of the human turn -> m-u2 / m-u2b)
        cl_msg("m-u2", "human", "Dodaj filtr po tagach.", "2025-11-02T10:01:00.000000Z", parent_message_uuid="m-a1", index=2),
        cl_msg("m-u2b", "human", "Dodaj sortowanie.", "2025-11-02T10:01:30.000000Z", parent_message_uuid="m-a1", index=3),
        cl_msg("m-a2", "assistant", "Filtr dodany.\n<antArtifact identifier=\"list-view\" type=\"text/html\" title=\"Widok listy v2\">"
                                    "<ul></ul></antArtifact>", "2025-11-02T10:01:10.000000Z", parent_message_uuid="m-u2", index=4),
        cl_msg("m-a3", "assistant", "Sortowanie dodane.", "2025-11-02T10:01:40.000000Z", parent_message_uuid="m-u2b", index=5),
    ]
    return {"uuid": "cl-conv-0001", "name": "Widok listy", "summary": "Rozmowa o widoku listy i artefakcie.",
            "created_at": t, "updated_at": "2025-11-02T10:02:00.000000Z",
            "settings": {"preview_feature_uses_artifacts": True, "preview_feature_uses_latex": False,
                         "preview_feature_uses_citations": True, "enabled_artifacts_attachments": True, "enabled_turmeric": False},
            "is_starred": True, "current_leaf_message_uuid": "m-a2", "account": {"uuid": "acct-0001"},
            "model": "claude-sonnet-4", "project_uuid": None, "chat_messages": msgs,
            "future_field": {"nested": [1, {"a": None}]}}


def claude_conv_linear(uuid, name, texts, t0="2024-05-01T08:00:00Z"):
    msgs = []
    for i, (sender, txt) in enumerate(texts):
        m = {"uuid": f"{uuid}-m{i}", "text": txt, "sender": sender, "created_at": f"2024-05-01T08:0{i}:00Z",
             "updated_at": f"2024-05-01T08:0{i}:00Z", "attachments": [], "files": []}
        msgs.append(m)
    return {"uuid": uuid, "name": name, "created_at": t0, "updated_at": t0, "account": {"uuid": "acct-legacy"}, "chat_messages": msgs}


# ═══════════════════════════ assemble ═════════════════════════════════════
def openai_zip_members_2026():
    c0 = {"conversations": [conv_c1(), conv_c2(), conv_c3()]}
    c1 = [conv_c4(), conv_c5(), conv_empty(), conv_no_mapping()]
    members = [
        ("conversations-000.json", dumps(c0, ascii_=True)),  # ASCII escapes: surrogate pair + lone surrogate as \uXXXX
        ("conversations-001.json", dumps(c1, ascii_=True)),
        ("user.json", dumps({"id": "user-FAKE0001", "email": "user@example.invalid", "chatgpt_plus_user": True,
                            "phone_number": "+48000000000", "birth_year": 1990})),
        ("message_feedback.json", dumps([{"id": "fb-1", "conversation_id": "c1-0000-aaaa", "message_id": "c1-a1", "user_id": "user-FAKE0001",
                                          "rating": "thumbs_up", "create_time": "2025-10-09T09:00:00.000000Z", "update_time": None,
                                          "content": dumps({"tags": ["helpful"], "text": "Dobra odpowiedź"}), "evaluation_name": None,
                                          "evaluation_treatment": None, "workspace_id": None},
                                         {"id": "fb-2", "conversation_id": "c2-0000-bbbb", "message_id": "c2-MISSING", "user_id": "user-FAKE0001",
                                          "rating": "thumbs_down", "create_time": "2025-10-09T10:00:00.000000Z", "content": "{}"}])),
        ("shared_conversations.json", dumps([{"id": "share-1", "conversation_id": "c1-0000-aaaa", "title": "Migracja bazy danych",
                                              "is_anonymous": True}])),
        ("model_comparisons.json", dumps([])),
        ("group_chats.json", dumps({"chats": []})),
        ("shopping.json", dumps([])),
        ("sora.json", dumps({})),
        ("export_manifest.json", dumps({"hypothetical": True, "note": "not a confirmed OpenAI file; tests unknown-member handling",
                                        "generated": "2026-01-01T00:00:00Z", "files": 3})),
        ("chat.html", "<!doctype html><html><head><title>ChatGPT Data Export</title></head><body><script>var jsonData = "
                      + dumps(c0["conversations"][0], ascii_=True) + ";</script><div id=\"root\"></div></body></html>"),
        ("textdocs/7f3c9a10-0000-4000-8000-000000000001.json",
         dumps({"id": "7f3c9a10-0000-4000-8000-000000000001", "name": "plan-migracji", "type": "document",
                "content": "# Plan\n1. Schemat\n2. Dane\n3. Weryfikacja", "conversation_id": "c1-0000-aaaa", "future": {"rev": 2}})),
        ("something_new_2027.json", dumps({"version": 9, "items": [{"id": "x1", "title": "Nowa rzecz", "created_at": "2027-01-01T00:00:00Z"}]})),
        # assets
        ("file-A1b2C3d4E5f6G7h8-schemat.png", PNG_A),
        ("file-Boundary1-a.png", PNG_A + b"1"),
        ("file-Boundary12-b.png", PNG_A + b"2"),
        ("file-Dup1-a.png", PNG_B),
        ("file-Dup2-b.png", PNG_B),
        ("file-RealId77-raport-q3.pdf", PDF),
        ("file-Zz9Dat.dat", PNG_C2PA),
        ("file-Orphan9-old.png", PNG_A + b"orphan"),
        ("file_00000000feed0002-sanitized.png", PNG_B + b"s"),
        ("file_00000000a0d10001-voice.wav", WAV),
        ("dalle-generations/file-Dq3Zx9-11111111-2222-4333-8444-555555555555.webp", WEBP),
        ("user-FAKEUSER0001/file_00000000c0ffee01-66666666-7777-4888-9999-000000000000.png", PNG_A + b"user"),
    ]
    return members


def openai_zip_members_legacy():
    convs = [legacy_conv("L1", "Pierwsza rozmowa", T0 - 9e7, [("user", "Cześć!"), ("assistant", "Witaj, w czym mogę pomóc?"),
                                                                ("user", "Napisz haiku o zimie."), ("assistant", "Śnieg cicho pada\nna dachy uśpionych miast\nzima oddycha.")]),
             legacy_conv("L2", "Second conversation", T0 - 8e7, [("user", "What is WAL?"), ("assistant", "Write-ahead logging.")])]
    # legacy quirks: system root message, weight 0 hidden node, browsing plugin
    convs[0]["mapping"]["L1-0"]["message"]["metadata"]["is_visually_hidden_from_conversation"] = True
    return [
        ("conversations.json", dumps(convs, ascii_=True)),
        ("user.json", dumps({"id": "user-LEGACY01", "email": "legacy@example.invalid", "chatgpt_plus_user": False, "phone_number": None})),
        ("message_feedback.json", dumps([{"id": "fbl", "conversation_id": "L1", "message_id": "L1-1", "rating": "thumbs_up"}])),
        ("shared_conversations.json", dumps([])),
        ("model_comparisons.json", dumps([])),
        ("chat.html", "<html><body>legacy viewer</body></html>"),
    ]


def anthropic_members_2026():
    convs = [claude_conv_full(),
             claude_conv_linear("cl-conv-0002", "Krótka", [("human", "Hej"), ("assistant", "Cześć! 👋")], "2025-11-03T09:00:00Z")]
    projects = [{"uuid": "proj-0001", "name": "Zenit", "description": "Projekt fikcyjny.", "is_private": True, "is_starred": False,
                 "prompt_template": "Odpowiadaj zwięźle.", "created_at": "2025-10-01T08:00:00Z", "updated_at": "2025-10-02T08:00:00Z",
                 "creator": {"uuid": "acct-0001", "full_name": "Test User"},
                 "docs": [{"uuid": "doc-1", "filename": "zasady.md", "content": "# Zasady\nMałe kroki.", "created_at": "2025-10-01T08:10:00Z"},
                          {"uuid": "doc-2", "filename": "notatki.txt", "content": "Notatki projektu."}]},
                {"uuid": "proj-0002", "name": "Pusty", "description": "", "prompt_template": "", "created_at": "2025-10-05T08:00:00Z", "docs": []}]
    users = [{"uuid": "acct-0001", "full_name": "Test User", "email_address": "test@example.invalid", "verified_phone_number": None}]
    memories = {"conversations_memory": "Użytkownik pracuje nad projektem Zenit i woli małe kroki.",
                "project_memories": {"proj-0001": "Zenit: rdzeń w C++, dane w SQLite."}, "updated_at": "2025-11-01T00:00:00Z"}
    return [("conversations.json", dumps(convs, ascii_=False)), ("projects.json", dumps(projects)),
            ("users.json", dumps(users)), ("memories.json", dumps(memories))]


def anthropic_members_legacy():
    convs = [claude_conv_linear("cl-legacy-1", "Stara rozmowa", [("human", "Ile to 2+2?"), ("assistant", "4."), ("human", "Dzięki"), ("assistant", "Proszę.")]),
             claude_conv_linear("cl-legacy-2", "Attachments", [("human", "Zobacz plik"), ("assistant", "Widzę.")])]
    convs[1]["chat_messages"][0]["attachments"] = [{"file_name": "a.txt", "file_size": 3, "file_type": "text/plain", "extracted_content": "abc"}]
    projects = [{"uuid": "proj-old", "name": "Stary projekt", "created_at": "2024-04-01T00:00:00Z", "description": "d", "prompt_template": "p",
                 "docs": [{"filename": "f.md", "content": "c"}]}]
    users = [{"uuid": "acct-legacy", "full_name": "Legacy User", "email_address": "legacy@example.invalid"}]
    return [("conversations.json", dumps(convs)), ("projects.json", dumps(projects)), ("users.json", dumps(users))]


def write_nested(path):
    inner_a = io.BytesIO()
    write_zip_to(inner_a, anthropic_members_legacy())
    inner_b = io.BytesIO()
    write_zip_to(inner_b, openai_zip_members_legacy())
    write_zip(path, [("README.txt", "Two export parts.\n"), ("part1-claude.zip", inner_a.getvalue()), ("part2-chatgpt.zip", inner_b.getvalue())])


def write_zip_to(buf, members):
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for name, data in members:
            zi = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = 0o644 << 16
            zf.writestr(zi, data if isinstance(data, bytes) else data.encode("utf-8"))


def malformed_members():
    """Never-crash corpus: valid, NUL char, raw invalid UTF-8, truncated tail, zip-slip names, empty, too deep."""
    good = dumps(legacy_conv("M1", "Dobra", T0, [("user", "ok"), ("assistant", "ok too")]), ascii_=True)
    with_nul = dumps(legacy_conv("M2", "Z zerem", T0 + 1, [("user", "PLACEHOLDER"), ("assistant", "fine")]), ascii_=True)
    with_nul = with_nul.replace("PLACEHOLDER", "przed\\u0000po")
    bad_utf = (b'{"title":"Bajty","conversation_id":"M3","mapping":{"n1":{"id":"n1","parent":null,"children":[],'
               b'"message":{"id":"n1","author":{"role":"user"},"create_time":1.0,"content":{"content_type":"text",'
               b'"parts":["A\xff\xfeB"]},"status":"finished_successfully","metadata":{}}}},"current_node":"n1"}')
    truncated = (b'{"title":"Ucieta","conversation_id":"M4","mapping":{"a":{"id":"a","parent":null,"children":[],'
                 b'"message":{"id":"a","author":{"role":"user"},"content":{"content_type":"text","parts":["uci\xc4\x99ta')
    body = b"[" + good.encode() + b"," + with_nul.encode() + b"," + bad_utf + b"," + truncated  # never closed
    return [("conversations.json", body), ("../evil.json", b'{"escape": true}'), ("/abs/evil2.json", b'{"escape": 2}'),
            ("empty.json", b""), ("deep.json", b"[" * 2000 + b"]" * 2000), ("notes.txt", b"plain note\n")]


def unknown_provider_members():
    data = {"format": "acme-chat-export", "version": "3.1", "threads": [
        {"id": "th-1", "name": "Wątek pierwszy", "started": "2026-02-01T10:00:00Z",
         "messages": [{"author": "user", "body": "Cześć, jak zacząć?", "ts": "2026-02-01T10:00:01Z", "attachments": [{"file": "plan.pdf"}]},
                      {"author": "bot", "body": "Zacznij od planu.", "ts": "2026-02-01T10:00:05Z"}]},
        {"id": "th-2", "name": "Wątek drugi", "started": "2026-02-02T10:00:00Z",
         "messages": [{"author": "user", "body": "Drugi.", "ts": "2026-02-02T10:00:01Z"}]}]}
    return [("export.json", dumps(data)), ("files/plan.pdf", PDF), ("readme.md", "# Acme export\n")]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    write_zip(OUT / "openai_legacy.zip", openai_zip_members_legacy())
    write_zip(OUT / "openai_2026_sharded.zip", openai_zip_members_2026())
    (OUT / "openai_single_conversations.json").write_text(
        dumps([legacy_conv("S1", "Pojedynczy plik 😀", T0, [("user", "Zażółć gęślą jaźń 😀 שלום مرحبا"), ("assistant", "Odpowiedź")])], ascii_=False),
        encoding="utf-8")
    write_zip(OUT / "anthropic_legacy.zip", anthropic_members_legacy())
    write_zip(OUT / "anthropic_2026_full.zip", anthropic_members_2026())
    (OUT / "anthropic_single_chat.json").write_text(dumps(dict(claude_conv_full(), conversation_id="cl-conv-0001", workspace_id="ws-1")), encoding="utf-8")
    write_nested(OUT / "nested_container.zip")
    write_zip(OUT / "malformed_export.zip", malformed_members())
    write_zip(OUT / "unknown_provider.zip", unknown_provider_members())
    expected = build_expected()
    (OUT / "EXPECTED.json").write_text(json.dumps(expected, indent=1, ensure_ascii=False, sort_keys=True) + "\n", encoding="utf-8")
    print("wrote", len(list(OUT.iterdir())), "files to", OUT)


# ═══════════════════════════ independent oracle ═══════════════════════════
def leaves(node, ptr=""):
    """All JSON leaves (scalars and empty containers) as (pointer, value)."""
    if isinstance(node, dict):
        if not node:
            yield ptr, node
        for k, v in node.items():
            yield from leaves(v, ptr + "/" + str(k).replace("~", "~0").replace("/", "~1"))
    elif isinstance(node, list):
        if not node:
            yield ptr, node
        for i, v in enumerate(node):
            yield from leaves(v, ptr + "/" + str(i))
    else:
        yield ptr, node


OA_TEXTY = {"text", "multimodal_text"}
OA_SINGLE_KIND = {"code": "code", "execution_output": "tool_result", "tether_quote": "quote", "tether_browsing_display": "browse",
                  "tether_browsing_code": "browse", "sonic_webpage": "browse", "system_error": "error",
                  "model_editable_context": "context", "reasoning_recap": "reasoning"}
OA_PART_KIND = {"image_asset_pointer": "media", "audio_asset_pointer": "media",
                "real_time_user_audio_video_asset_pointer": "media", "audio_transcription": "transcript",
                "search_result_group": "search_results", "text": "text"}


def oa_blocks(content):
    """[(kind, part_or_None)] as the registry describes them."""
    if not isinstance(content, dict):
        return []
    ct = content.get("content_type")
    out = []
    if ct in OA_TEXTY:
        for p in content.get("parts") or []:
            if isinstance(p, str):
                out.append("text")
            elif isinstance(p, dict):
                key = p.get("content_type", p.get("type"))
                out.append(OA_PART_KIND.get(key, "unknown"))
            else:
                out.append("unknown")
    elif ct in OA_SINGLE_KIND:
        out.append(OA_SINGLE_KIND[ct])
    elif ct == "user_editable_context":
        for k in ("user_profile", "user_instructions"):
            if isinstance(content.get(k), str):
                out.append("context")
    elif ct == "thoughts":
        out.extend("reasoning" for _ in content.get("thoughts") or [])
    else:
        out.append("unknown")
    return out


def oa_conv_oracle(c, acc):
    acc["conversation"] += 1
    mapping = c.get("mapping") or {}
    if not isinstance(mapping, dict):
        mapping = {}
    for nid, node in mapping.items():
        m = node.get("message")
        kids = [k for k in node.get("children", []) if k in mapping]
        if len(kids) >= 2:
            acc["fork_points"] += 1
        if not kids and m is not None:
            acc["branch"] += 1
        if not isinstance(m, dict):
            continue
        acc["message"] += 1
        for kind in oa_blocks(m.get("content")):
            acc["block"] += 1
            acc["block_kind"][kind] = acc["block_kind"].get(kind, 0) + 1
        md = m.get("metadata") or {}
        acc["attachment"] += len(md.get("attachments") or [])
        acc["attachment"] += len((md.get("aggregate_result") or {}).get("messages") or [])
        acc["citation"] += len(md.get("citations") or []) + len(md.get("content_references") or [])
        acc["citation_group"] += len(md.get("search_result_groups") or [])
        for g in md.get("search_result_groups") or []:
            acc["citation"] += len(g.get("entries") or [])
        ct = (m.get("content") or {}).get("content_type") if isinstance(m.get("content"), dict) else None
        if ct in OA_TEXTY:
            for p in (m["content"].get("parts") or []):
                if isinstance(p, dict) and p.get("type") == "search_result_group":
                    acc["citation"] += len(p.get("entries") or [])
        if ct == "user_editable_context":
            acc["custom_instruction"] += sum(1 for k in ("user_profile", "user_instructions") if isinstance(m["content"].get(k), str))
        if ct == "model_editable_context":
            acc["memory"] += 1
        if m.get("recipient") == "canmore.create_textdoc":
            acc["artifact"] += 1
    # current path length
    seen, cur, n = set(), c.get("current_node"), 0
    while cur in mapping and cur not in seen:
        seen.add(cur)
        if isinstance(mapping[cur].get("message"), dict):
            n += 1
        cur = mapping[cur].get("parent")
    acc["current_path_messages"] += n


def new_acc():
    return {"conversation": 0, "message": 0, "block": 0, "block_kind": {}, "attachment": 0, "citation": 0, "citation_group": 0,
            "branch": 0, "fork_points": 0, "custom_instruction": 0, "memory": 0, "artifact": 0, "current_path_messages": 0,
            "account": 0, "feedback": 0, "shared_link": 0, "project": 0, "project_doc": 0, "record": 0}


def cl_blocks(m):
    kinds = {"text": "text", "thinking": "reasoning", "voice_note": "transcript", "tool_use": "tool_call", "tool_result": "tool_result",
             "image": "media", "document": "document"}
    content = m.get("content")
    if isinstance(content, list) and content:
        return [kinds.get(b.get("type"), "unknown") if isinstance(b, dict) else "unknown" for b in content]
    if isinstance(m.get("text"), str):
        return ["text"]
    return []


def cl_conv_oracle(c, acc):
    acc["conversation"] += 1
    msgs = c.get("chat_messages") or []
    ids = {m["uuid"] for m in msgs}
    children = {}
    for m in msgs:
        p = m.get("parent_message_uuid")
        if p in ids:
            children.setdefault(p, []).append(m["uuid"])
    has_parents = any(m.get("parent_message_uuid") in ids for m in msgs)
    for m in msgs:
        acc["message"] += 1
        for kind in cl_blocks(m):
            acc["block"] += 1
            acc["block_kind"][kind] = acc["block_kind"].get(kind, 0) + 1
        for key in ("attachments", "files", "files_v2"):
            acc["attachment"] += len(m.get(key) or [])
        for b in m.get("content") or []:
            if isinstance(b, dict):
                acc["citation"] += len(b.get("citations") or []) if b.get("type") == "text" else 0
                if b.get("type") == "tool_use" and isinstance(b.get("input"), dict) and isinstance(b["input"].get("content"), str) \
                        and "command" in b["input"]:
                    acc["artifact"] += 1
    if has_parents:
        acc["fork_points"] += sum(1 for k, v in children.items() if len(v) >= 2)
        acc["branch"] += sum(1 for m in msgs if m["uuid"] not in children)
        parent_of = {m["uuid"]: m.get("parent_message_uuid") for m in msgs}
        cur, seen, n = c.get("current_leaf_message_uuid"), set(), 0
        while cur in parent_of and cur not in seen:
            seen.add(cur)
            n += 1
            cur = parent_of[cur]
        acc["current_path_messages"] += n
    else:
        acc["branch"] += 1 if msgs else 0
        acc["current_path_messages"] += len(msgs)


def leaf_count_of(members, names):
    total = 0
    for name, data in members:
        if name in names:
            total += sum(1 for _ in leaves(json.loads(data if isinstance(data, str) else data.decode("utf-8"))))
    return total


def build_expected():
    exp = {}
    # -- openai legacy
    acc = new_acc()
    mem = openai_zip_members_legacy()
    for c in json.loads(dict(mem)["conversations.json"]):
        oa_conv_oracle(c, acc)
    acc["account"] = 1
    acc["feedback"] = 1
    exp["openai_legacy.zip"] = {"provider": "openai", "counts": acc, "members": len(mem),
                                "json_leaves": leaf_count_of(mem, {"conversations.json"})}
    # -- openai 2026
    acc = new_acc()
    mem = openai_zip_members_2026()
    md = dict(mem)
    for name in ("conversations-000.json", "conversations-001.json"):
        data = json.loads(md[name])
        for c in (data["conversations"] if isinstance(data, dict) else data):
            oa_conv_oracle(c, acc)
    acc["account"] = 1
    acc["feedback"] = 2
    acc["shared_link"] = 1
    acc["artifact"] += 1  # textdocs/ member
    asset_members = [n for n, _ in mem if n.startswith(("file-", "file_", "dalle-generations/", "user-FAKEUSER0001/"))]
    hashes = {}
    for n, d in mem:
        if n in asset_members:
            hashes.setdefault(hashlib.sha256(d).hexdigest(), []).append(n)
    dups = sorted(sorted(v) for v in hashes.values() if len(v) > 1)
    exp["openai_2026_sharded.zip"] = {
        "provider": "openai", "counts": acc, "members": len(mem), "asset_members": sorted(asset_members),
        "duplicate_groups": dups,
        "pointer_links": {  # asset key -> expected member and method (observed = id evidence, inferred = name only)
            "file-A1b2C3d4E5f6G7h8": ["file-A1b2C3d4E5f6G7h8-schemat.png", "id_prefix"],
            "file-Dq3Zx9": ["dalle-generations/file-Dq3Zx9-11111111-2222-4333-8444-555555555555.webp", "id_prefix"],
            "file_00000000c0ffee01": ["user-FAKEUSER0001/file_00000000c0ffee01-66666666-7777-4888-9999-000000000000.png", "id_prefix"],
            "file_00000000feed0002": ["file_00000000feed0002-sanitized.png", "id_prefix"],
            "file_00000000a0d10001": ["file_00000000a0d10001-voice.wav", "id_prefix"],
            "file-Zz9Dat": ["file-Zz9Dat.dat", "id_prefix"],
            "file-Boundary1": ["file-Boundary1-a.png", "id_prefix"],
            "file-Dup1": ["file-Dup1-a.png", "id_prefix"],
            "file-Dup2": ["file-Dup2-b.png", "id_prefix"],
            "file-NoSuchId0": ["file-RealId77-raport-q3.pdf", "name"],
        },
        "unresolved_keys": ["file_00000000dead0000", "file-Plot0001"],
        "unreferenced_members": ["file-Boundary12-b.png", "file-Orphan9-old.png"],
    }
    # -- anthropic legacy
    acc = new_acc()
    mem = anthropic_members_legacy()
    for c in json.loads(dict(mem)["conversations.json"]):
        cl_conv_oracle(c, acc)
    acc["account"] = 1
    acc["project"] = 1
    acc["project_doc"] = 1
    exp["anthropic_legacy.zip"] = {"provider": "anthropic", "counts": acc, "members": len(mem)}
    # -- anthropic 2026
    acc = new_acc()
    mem = anthropic_members_2026()
    for c in json.loads(dict(mem)["conversations.json"]):
        cl_conv_oracle(c, acc)
    acc["account"] = 1
    acc["project"] = 2
    acc["project_doc"] = 2
    acc["memory"] = 2  # conversations_memory + one project memory (shape inferred)
    exp["anthropic_2026_full.zip"] = {"provider": "anthropic", "counts": acc, "members": len(mem)}
    # -- singles
    acc = new_acc()
    oa_conv_oracle(json.loads((OUT / "openai_single_conversations.json").read_text(encoding="utf-8"))[0], acc)
    exp["openai_single_conversations.json"] = {"provider": "openai", "counts": acc}
    acc = new_acc()
    cl_conv_oracle(json.loads((OUT / "anthropic_single_chat.json").read_text(encoding="utf-8")), acc)
    exp["anthropic_single_chat.json"] = {"provider": "anthropic", "counts": acc}
    return exp


if __name__ == "__main__":
    main()
