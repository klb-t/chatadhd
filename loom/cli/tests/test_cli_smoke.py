#!/usr/bin/env python3
"""Smoke tests for the `loom` CLI (run by ctest as cli.smoke).

Each check runs the real binary against a temporary data directory and
asserts on exit codes and JSON output. No network access is needed.
"""
import hashlib
import json
import math
import os
import shutil
import sqlite3
import subprocess
import sys
import tempfile
from pathlib import Path

LOOM = os.environ.get("LOOM_CLI")
FIXTURES = os.environ.get("LOOM_FIXTURES")
if not LOOM or not os.path.exists(LOOM):
    print("SKIP: LOOM_CLI not set")
    sys.exit(0)

tmp = tempfile.mkdtemp(prefix="loom_cli_")
data = os.path.join(tmp, "data")
failures = []


def run(*args, stdin=None, code=0):
    p = subprocess.run([LOOM, "--data-dir", data, "--quiet", *args], input=stdin, capture_output=True, text=True,
                       timeout=240)
    if p.returncode != code:
        failures.append(f"{' '.join(args)}: exit {p.returncode} (want {code})\nstdout: {p.stdout[-800:]}\nstderr: {p.stderr[-800:]}")
    return p


def js(*args, **kw):
    p = run("--json", *args, **kw)
    try:
        return json.loads(p.stdout)
    except Exception:
        failures.append(f"{' '.join(args)}: not JSON: {p.stdout[:300]!r}")
        return None


try:
    # help / usage
    p = subprocess.run([LOOM, "--help"], capture_output=True, text=True)
    assert p.returncode == 0 and "archive run" in p.stdout, "help text"
    p = subprocess.run([LOOM, "no-such-command"], capture_output=True, text=True)
    assert p.returncode == 2, "unknown command exit 2"

    info = js("init")
    assert info and info["data_dir"].startswith(tmp), info
    assert js("version")["abi"] >= 1

    # conversations + messages
    c = js("conv", "create", "Smoke test")
    cid = c["id"]
    convs = js("conv", "list")
    assert any(x["id"] == cid for x in convs)
    run("conv", "rename", cid, "Renamed")
    assert js("conv", "show", cid)["conversation"]["title"] == "Renamed"

    # import + search + export + edit/versions
    md = os.path.join(tmp, "chat.md")
    with open(md, "w") as f:
        f.write("## User\nWe decided to use SQLite for the knowledge graph.\n\n## Assistant\n"
                "Good: SQLite keeps it local-first and the graph store simple.\n")
    imp = js("import", md, "--title", "Imported")
    assert imp["messages"] == 2, imp
    icid = imp["conversations"][0]["id"]
    again = js("import", md)
    assert again["already_imported"] is True
    forced = js("import", md, "--force")
    assert forced["already_imported"] is False
    hits = js("search", "SQLite")
    assert len(hits["results"]) >= 2, hits
    out = os.path.join(tmp, "export.md")
    run("export", icid, "--format", "markdown", "--out", out)
    assert "SQLite" in open(out).read()
    msgs = js("conv", "show", icid)["messages"]
    edited = js("msg", "edit", msgs[0]["id"], "We decided to use Postgres instead.")
    versions = js("msg", "versions", edited["id"])
    assert len(versions) == 2, versions
    run("msg", "restore", msgs[0]["id"])
    run("msg", "status", msgs[1]["id"], "excluded")

    # Real import callers consume complete preset files, then explicit flags.
    preset_dir = Path(__file__).resolve().parents[2] / "data" / "presets"
    import_pack = json.loads((preset_dir / "import.pack").read_text(encoding="utf-8"))
    audit_pack = json.loads((preset_dir / "import_audit.pack").read_text(encoding="utf-8"))
    import_pack["values"].update(stream_threshold_bytes=0, json_read_chunk_bytes=3,
                               json_max_depth=0, json_inline_threshold_bytes=0,
                               generic_inference_max_bytes=0)
    audit_pack["values"].update(native_active_only=True, chars_per_token_low=8.0,
                              chars_per_token_high=4.0, output_ratio=2.0)
    import_preset = os.path.join(tmp, "import_custom.pack")
    audit_preset = os.path.join(tmp, "audit_custom.pack")
    for path, document in ((import_preset, import_pack), (audit_preset, audit_pack)):
        with open(path, "w", encoding="utf-8") as f:
            json.dump(document, f)
    preset_source = os.path.join(tmp, "preset_chat.json")
    preset_document = [{"uuid": "synthetic-preset-conversation", "name": "Preset regression",
                        "chat_messages": [{"uuid": "user", "sender": "human", "text": "abcdefghij"},
                                          {"uuid": "assistant", "sender": "assistant", "text": "klmnop"}]}]
    with open(preset_source, "w", encoding="utf-8") as f:
        json.dump(preset_document, f)

    def test_import_wholefile_presets():
        report = js("import", preset_source, "--export-mode", "on", "--import-preset", import_preset,
                    "--audit-preset", audit_preset, "--audit", "--audit-input-price", "2", "--audit-output-price", "5")
        assert report and report["messages"] == 2 and len(report["conversations"]) == 1, report
        effective = report["usage_policy"]["estimate"]["import_options"]
        assert effective["preset_values"] == import_pack["values"], effective
        canonical = json.dumps(import_pack["values"], sort_keys=True, separators=(",", ":")).encode()
        assert effective["preset_values_sha256"] == hashlib.sha256(canonical).hexdigest(), effective
        audit = report["audit"]
        assert audit["raw"]["characters"] == 16 and audit["projected"]["scope"] == "active", audit
        assert audit["assumptions"]["chars_per_token_low"] == 8 and audit["assumptions"]["output_ratio"] == 2, audit
        assert audit["estimates"]["low"]["input_tokens_estimate"] == 2, audit
        assert audit["estimates"]["high"]["output_tokens_estimate"] == 8, audit
        assert math.isclose(audit["estimates"]["high"]["model_cost_usd_estimate"], 48e-6), audit
        imported = js("conv", "show", report["conversations"][0]["id"])["messages"]
        assistant = next(message for message in imported if message["role"] == "assistant")
        run("msg", "status", assistant["id"], "excluded")
        cached = js("import", preset_source, "--export-mode", "on", "--import-preset", import_preset,
                    "--audit-preset", audit_preset, "--audit")
        assert cached["already_imported"] and cached["source_id"] == report["source_id"], cached
        assert cached["audit"]["raw"]["characters"] == 16 and cached["audit"]["projected"]["characters"] == 10, cached
        assert cached["audit"]["estimates"]["high"]["input_tokens_estimate"] == 2.5, cached
        assert cached["audit"]["estimates"]["high"]["model_cost_usd_estimate"] is None, cached

    def test_import_preset_explicit_overrides():
        override_source = os.path.join(tmp, "preset_override_chat.json")
        document = json.loads(json.dumps(preset_document))
        document[0]["uuid"] = "synthetic-override-conversation"
        with open(override_source, "w", encoding="utf-8") as f:
            json.dump(document, f)
        report = js("import", override_source, "--export-mode", "on", "--import-preset", import_preset,
                    "--audit-preset", audit_preset, "--json-read-chunk-bytes", "7", "--json-max-depth", "64",
                    "--json-inline-threshold-bytes", "256", "--generic-inference-max-bytes", "2048",
                    "--audit", "--audit-scope", "all", "--audit-chars-per-token-low", "10",
                    "--audit-chars-per-token-high", "5", "--audit-output-ratio", "0.5",
                    "--audit-input-price", "2", "--audit-output-price", "5")
        assert report and report["messages"] == 2 and not report["already_imported"], report
        expected = {**import_pack["values"], "json_read_chunk_bytes": 7, "json_max_depth": 64,
                    "json_inline_threshold_bytes": 256, "generic_inference_max_bytes": 2048}
        effective = report["usage_policy"]["estimate"]["import_options"]
        assert effective["preset_values"] == expected, effective
        canonical = json.dumps(expected, sort_keys=True, separators=(",", ":")).encode()
        assert effective["preset_values_sha256"] == hashlib.sha256(canonical).hexdigest(), effective
        audit = report["audit"]
        assert audit["projected"]["scope"] == "all" and audit["projected"]["characters"] == 16, audit
        assert audit["assumptions"]["chars_per_token_low"] == 10 and audit["assumptions"]["chars_per_token_high"] == 5, audit
        assert audit["assumptions"]["output_ratio"] == .5, audit
        assert math.isclose(audit["estimates"]["low"]["input_tokens_estimate"], 1.6), audit
        assert math.isclose(audit["estimates"]["high"]["output_tokens_estimate"], 1.6), audit
        assert math.isclose(audit["estimates"]["high"]["model_cost_usd_estimate"], 14.4e-6), audit

    def test_invalid_import_presets_leave_import_state_unchanged():
        def snapshot():
            with sqlite3.connect(Path(info["paths"]["db"]).resolve().as_uri() + "?mode=ro", uri=True) as db:
                rows = {table: db.execute("SELECT * FROM " + table + " ORDER BY rowid").fetchall()
                        for table in ("messages", "conversations", "loom_sources", "loom_blobs",
                                      "loom_provenance", "loom_import_checkpoints")}
            blob_root = os.path.join(data, "blobs")
            blobs = {}
            for directory, _, names in os.walk(blob_root):
                for name in names:
                    path = os.path.join(directory, name)
                    with open(path, "rb") as f:
                        blobs[os.path.relpath(path, blob_root)] = hashlib.sha256(f.read()).hexdigest()
            return rows, blobs

        source = os.path.join(tmp, "must_not_be_imported.md")
        with open(source, "w", encoding="utf-8") as f:
            f.write("## User\nSynthetic input that invalid presets must not capture.\n")
        cases = []
        for flag, document, field in (("import-preset", import_pack, "json_read_chunk_bytes"),
                                     ("audit-preset", audit_pack, "output_ratio")):
            missing = json.loads(json.dumps(document))
            del missing["values"][field]
            null = json.loads(json.dumps(document))
            null["values"][field] = None
            for label, bad, code in (("missing-field", missing, 1), ("null-field", null, 1),
                                     ("empty-values", {**document, "values": {}}, 1),
                                     ("null-values", {**document, "values": None}, 2),
                                     ("wrong-schema", {**document, "schema": "unsupported"}, 2)):
                path = os.path.join(tmp, flag + "-" + label + ".pack")
                with open(path, "w", encoding="utf-8") as f:
                    json.dump(bad, f)
                cases.append((flag, path, code))
            cases.append((flag, os.path.join(tmp, flag + "-absent.pack"), 1))
        before = snapshot()
        for flag, path, code in cases:
            result = run("--json", "import", source, "--" + flag, path, "--audit", code=code)
            assert result.returncode == code, (flag, path, result.stdout, result.stderr)
            assert snapshot() == before, ("invalid preset mutated import rows/blob files", flag, path)
        print("cli import preset rejection: 12 variants; imported rows and blob bytes unchanged")

    test_import_wholefile_presets()
    test_import_preset_explicit_overrides()
    test_invalid_import_presets_leave_import_state_unchanged()
    print("cli import presets: 3 real-binary scenarios passed")

    # graph / semantic / context / memory
    js("semantic", "run")
    stats = js("graph", "stats")
    assert stats["messages"] >= 2
    js("graph", "nodes", "--limit", "5")
    js("context", "SQLite graph")
    mem = js("memory", "add", "User prefers local-first storage")
    assert any(n["id"] == mem["id"] for n in js("memory", "list"))

    # config + secrets (value from stdin, never argv)
    run("config", "set", "temperature", "0.3")
    assert js("config", "get", "temperature") == 0.3
    run("secret", "set", "api_key", "extra", code=2)
    run("secret", "set", "github_token", stdin="tok-123\n")
    assert js("secret", "has", "github_token")["set"] is True
    assert "tok-123" not in run("--json", "secret", "list").stdout
    run("secret", "delete", "github_token")
    run("secret", "has", "github_token", code=3)

    # provenance / sources / tasks
    srcs = js("sources")
    assert srcs and srcs[0]["kind"] == "file"
    prov = js("provenance", msgs[1]["id"])
    assert prov["records"], prov
    js("tasks", "list")

    # archive over a fixture corpus
    corpus = os.path.join(FIXTURES, "archive") if FIXTURES else None
    if corpus and os.path.isdir(corpus):
        out_dir = os.path.join(tmp, "archive_out")
        r = js("archive", "run", "--source", os.path.join(corpus, "exports"), "--source",
               os.path.join(corpus, "docs"), "--repo", os.path.join(corpus, "repo"), "--no-git", "--out", out_dir,
               "--seed", "ChatADHD", "--seed", "graph")
        assert r["status"] == "done", r
        for name in ["MASTER.md", "gap_report.md", "source_map.csv", "timeline.json", "items.jsonl", "graph.json",
                     "project_manifest.json", "task_log.jsonl"]:
            assert os.path.exists(os.path.join(out_dir, name)), name
        st = js("archive", "status")
        assert st["run"]["status"] == "done" and len(st["artifacts"]) == 8, st
        arts = js("artifacts", "list")
        assert any(a["title"] == "MASTER.md" for a in arts)
        r2 = js("archive", "run", "--source", os.path.join(corpus, "exports"), "--source",
                os.path.join(corpus, "docs"), "--repo", os.path.join(corpus, "repo"), "--no-git", "--out", out_dir,
                "--seed", "ChatADHD", "--seed", "graph")
        hits = [s for s in r2["stages"] if s["stage"] != "materialize"]
        assert all(s["cache_hit"] for s in hits), r2["stages"]

    # Knowledge is a required part of --knowledge, including shell status.
    # Archive materialization succeeds, then the knowledge export cannot
    # create a directory because the caller placed a regular file there.
    blocked = os.path.join(tmp, "blocked_knowledge")
    os.makedirs(blocked)
    with open(os.path.join(blocked, "knowledge"), "w") as f:
        f.write("preserve this file")
    bad = js("archive", "run", "--source", md, "--out", blocked,
             "--knowledge", code=4)
    assert bad["status"] == "done" and "error" in bad["knowledge"], bad
    run("archive", "run", "--source", md, "--out", blocked,
        "--knowledge", code=4)

    knowledge_out = os.path.join(tmp, "knowledge_out")
    knowledge = js("knowledge", "run", "--source", md, "--out", knowledge_out)
    assert knowledge["status"] == "done", knowledge
    assert len(knowledge["stages"]) == 6, knowledge
    assert os.path.isfile(os.path.join(knowledge_out, "SELF.md"))

    # crypto (password from stdin)
    run("crypto", "setup", stdin="pw-123\n")
    assert js("crypto", "status")["configured"] is True
except AssertionError as e:
    failures.append(f"assertion: {e}")
finally:
    shutil.rmtree(tmp, ignore_errors=True)

if failures:
    print("\n\n".join(failures))
    sys.exit(1)
print("cli smoke: ok")
