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
import struct
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


def profile_only(root, *args, stdin=None, code=0, env=None, command="profile"):
    argv = [LOOM]
    if root is not None:
        argv += ["--data-dir", root]
    p = subprocess.run(argv + ["--json", "--quiet", command, *args], input=stdin, env=env,
                       capture_output=True, text=True, timeout=240)
    assert p.returncode == code, (args, p.returncode, p.stdout, p.stderr)
    return json.loads(p.stdout)


def tree_bytes(root):
    if not os.path.exists(root):
        return None
    tree = {}
    for parent, dirs, files in os.walk(root):
        for name in dirs:
            tree[os.path.relpath(os.path.join(parent, name), root)] = None
        for name in files:
            path = os.path.join(parent, name)
            with open(path, "rb") as f:
                tree[os.path.relpath(path, root)] = f.read()
    return tree


try:
    # help / usage
    p = subprocess.run([LOOM, "--help"], capture_output=True, text=True)
    assert p.returncode == 0 and "archive run" in p.stdout, "help text"
    # Early informational commands must not initialize a data directory.
    for label, args, code in [("help", ["--help"], 0), ("version", ["version"], 0), ("usage", [], 2)]:
        unopened = os.path.join(tmp, "early-" + label)
        p = subprocess.run([LOOM, "--data-dir", unopened, *args], capture_output=True, text=True, timeout=240)
        assert p.returncode == code, (args, p.stdout, p.stderr)
        assert not os.path.exists(unopened), "early command must not initialize its explicit root"
    for label in ("read-only-home", "non-directory-home"):
        home = os.path.join(tmp, label)
        if label == "read-only-home":
            os.mkdir(home)
            os.chmod(home, 0o555)
        else:
            with open(home, "w") as f:
                f.write("synthetic HOME blocker\n")
        env = dict(os.environ)
        env.pop("CHATADHD_DATA", None)
        env.pop("XDG_DATA_HOME", None)
        env["HOME"] = home
        try:
            for args, code in [(["--help"], 0), (["version"], 0), ([], 2)]:
                p = subprocess.run([LOOM, *args], env=env, capture_output=True, text=True, timeout=240)
                assert p.returncode == code, (label, args, p.stdout, p.stderr)
                assert not os.path.exists(os.path.join(home, ".chatadhd")), "early command must not initialize HOME"
        finally:
            if label == "read-only-home":
                os.chmod(home, 0o700)

    # Profile inspection/editing is independent of Runtime startup and migrations.
    profile_root = os.path.join(tmp, "profile-only")
    assert "cli" in profile_only(profile_root, "list")
    assert profile_only(profile_root) == profile_only(profile_root, "list")
    profile_builtin = profile_only(profile_root, "inspect", "cli")
    assert profile_only(profile_root, "validate", "cli", stdin="{}")["hash"] == profile_builtin["hash"]
    assert not os.path.exists(profile_root), "profile reads/validation must not initialize any directory"
    for op, domain, body, code in [("inspect", "no-such-domain", None, 1),
                                  ("save", "cli", "{broken", 1),
                                  ("save", "cli", '{"schema":"loom.runtime_profile_overlay/1","domain":"cli","patch":[]}', 1),
                                  ("save", "usage_policy", '{"growth_factor":1}', 1),
                                  ("save", "usage_policy", '{"initial_baselines":{"":{"tokens":1}}}', 1),
                                  ("save", "usage_policy", '{"initial_baselines":{"cohort":{"":1}}}', 1)]:
        assert "error" in profile_only(profile_root, op, domain, stdin=body, code=code)
        assert not os.path.exists(profile_root), "failed validation/save must not initialize a root"

    # An explicit choice equal to builtin must remain a stored choice after later saves.
    title_default = profile_builtin["values"]["creation"]["conversation_title"]
    explicit_defaults = {"creation": {"conversation_title": title_default},
                         "flags": profile_builtin["values"]["flags"]}
    saved_equal = profile_only(profile_root, "save", "cli", stdin=json.dumps(explicit_defaults))
    assert saved_equal["hash"] == profile_builtin["hash"] and saved_equal["is_builtin"]
    profile_path = os.path.join(profile_root, "profiles", "cli.pack")
    document = json.loads(open(profile_path).read())
    assert document["overrides"]["creation"]["conversation_title"] == title_default
    assert document["overrides"]["flags"] == profile_builtin["values"]["flags"]
    assert set(tree_bytes(profile_root)) == {"profiles", "profiles/cli.pack"}, "save only creates its overlay"
    # The advertised overlay contract requires overrides even with a patch.
    invalid_existing = os.path.join(profile_root, "profiles", "memory.pack")
    with open(invalid_existing, "w") as f:
        json.dump({"schema": "loom.runtime_profile_overlay/1", "domain": "memory", "patch": []}, f)
    invalid_tree = tree_bytes(profile_root)
    assert "error" in profile_only(profile_root, "inspect", "memory", code=1)
    assert "error" in profile_only(profile_root, "save", "memory", stdin="{}", code=1)
    assert tree_bytes(profile_root) == invalid_tree, "invalid existing envelope must not be rewritten"
    os.remove(invalid_existing)
    profile_only(profile_root, "save", "cli", stdin=json.dumps({"commands": {"pfixture": "profile"},
                 "subcommands": {"profile": {"peekfixture": "inspect"}}}))
    document = json.loads(open(profile_path).read())
    assert document["overrides"]["creation"]["conversation_title"] == title_default
    assert profile_only(profile_root, "peekfixture", "cli", command="pfixture")["values"]["commands"]["pfixture"] == "profile"

    # Equal-value patch replacements preserve intent too, without inserting array elements.
    profile_only(profile_root, "save", "cli", stdin=json.dumps({
        "schema": "loom.runtime_profile_overlay/1", "domain": "cli", "overrides": {},
        "patch": [{"op": "replace", "path": "/limits/conversation_list",
                   "value": profile_builtin["values"]["limits"]["conversation_list"]},
                  {"op": "replace", "path": "/flags/0", "value": profile_builtin["values"]["flags"][0]}]}))
    document = json.loads(open(profile_path).read())
    assert any(op["op"] == "replace" and op["path"] == "/limits/conversation_list" for op in document["patch"])
    assert profile_only(profile_root, "inspect", "cli")["values"]["flags"] == profile_builtin["values"]["flags"]

    # A removed array element survives a partial save; a later shorter array
    # replacement must not replay the previous remove against invalid indices.
    profile_only(profile_root, "save", "cli", stdin=json.dumps({
        "schema": "loom.runtime_profile_overlay/1", "domain": "cli", "overrides": {},
        "patch": [{"op": "remove", "path": "/flags/16"}]}))
    profile_only(profile_root, "save", "cli", stdin=json.dumps({"creation": {"conversation_title": title_default}}))
    assert "no-priors" not in profile_only(profile_root, "inspect", "cli")["values"]["flags"]
    shorter_flags = profile_builtin["values"]["flags"][:2]
    profile_only(profile_root, "save", "cli", stdin=json.dumps({"flags": shorter_flags}))
    assert profile_only(profile_root, "inspect", "cli")["values"]["flags"] == shorter_flags
    profile_only(profile_root, "save", "cli", stdin=json.dumps({"flags": profile_builtin["values"]["flags"]}))

    # Corrupt unrelated runtime files do not get read, repaired, migrated or created.
    for name, content in [("chatadhd.db", b"synthetic invalid database\x00"),
                          ("config.json", b"{synthetic invalid config"),
                          (".chatadhd_data", b"unchanged synthetic sentinel\n")]:
        with open(os.path.join(profile_root, name), "wb") as f:
            f.write(content)
    untouched = tree_bytes(profile_root)
    profile_only(profile_root, "list")
    profile_only(profile_root, "inspect", "cli")
    profile_only(profile_root, "validate", "cli", stdin="{}")
    assert tree_bytes(profile_root) == untouched, "profile reads must not touch Runtime files"
    os.chmod(profile_root, 0o555)
    try:
        assert profile_only(profile_root, "inspect", "cli")["values"]["creation"]["conversation_title"] == title_default
        assert tree_bytes(profile_root) == untouched, "read-only inspection must not initialize data"
    finally:
        os.chmod(profile_root, 0o700)

    usage_changes = {"baseline_window": None, "initial_baselines": {"open-cohort": {"tokens": None, "calls": 0}},
                     "fixture_extension": {"description": "open native contract"},
                     "overrides": {"description": "ordinary open extension, not an envelope"}}
    usage_validated = profile_only(profile_root, "validate", "usage_policy", stdin=json.dumps(usage_changes))
    assert usage_validated["values"]["baseline_window"] is None
    assert usage_validated["values"]["overrides"] == usage_changes["overrides"]
    assert tree_bytes(profile_root) == untouched, "native validation does not write/activate usage policy"
    profile_only(profile_root, "save", "usage_policy", stdin=json.dumps(usage_changes))
    usage_path = os.path.join(profile_root, "profiles", "usage_policy.pack")
    usage_bytes = open(usage_path, "rb").read()
    usage_tree = tree_bytes(profile_root)
    assert set(usage_tree) == set(untouched) | {"profiles/usage_policy.pack"}, "usage save creates only its overlay"
    assert all(usage_tree[name] == content for name, content in untouched.items()), "usage save leaves all other files unchanged"
    for invalid in [{"growth_factor": 1}, {"initial_baselines": {"": {"tokens": 1}}},
                    {"initial_baselines": {"cohort": {"": 1}}}, {"initial_baselines": {"cohort": {"tokens": -1}}}]:
        for op in ("validate", "save"):
            assert "error" in profile_only(profile_root, op, "usage_policy", stdin=json.dumps(invalid), code=1)
            assert open(usage_path, "rb").read() == usage_bytes, "native rejection must preserve saved bytes"
    usage_native_invalid = json.loads(usage_bytes)
    usage_native_invalid["overrides"]["growth_factor"] = 1
    with open(usage_path, "w") as f:
        json.dump(usage_native_invalid, f)
    assert "error" in profile_only(profile_root, "validate", "usage_policy", stdin="{}", code=1)
    with open(usage_path, "wb") as f:
        f.write(usage_bytes)
    assert open(os.path.join(profile_root, "config.json"), "rb").read() == untouched["config.json"]
    assert not os.path.exists(os.path.join(profile_root, "usage.db")), "profile editor must not open a ledger"

    # A failed atomic write preserves the original overlay and unrelated files.
    os.mkdir(os.path.join(profile_root, "profiles", "cli.tmp"))
    before_failed_write = tree_bytes(profile_root)
    assert "error" in profile_only(profile_root, "save", "cli", stdin='{"creation":{"conversation_title":"not saved"}}', code=1)
    assert tree_bytes(profile_root) == before_failed_write, "failed save must preserve the complete tree"
    os.rmdir(os.path.join(profile_root, "profiles", "cli.tmp"))

    # Discovery honors explicit > environment > sentinel candidate, without initialization.
    discover_home = os.path.join(tmp, "profile-discovery-home")
    discover_xdg = os.path.join(tmp, "profile-discovery-xdg")
    env = dict(os.environ, HOME=discover_home, XDG_DATA_HOME=discover_xdg, CHATADHD_DATA=profile_root)
    assert profile_only(None, "inspect", "cli", env=env)["values"]["commands"]["pfixture"] == "profile"
    assert profile_only(os.path.join(tmp, "profile-explicit-absent"), "inspect", "cli", env=env)["hash"] == profile_builtin["hash"]
    env.pop("CHATADHD_DATA")
    home_root = os.path.join(discover_home, ".chatadhd")
    os.makedirs(os.path.join(home_root, "profiles"))
    with open(os.path.join(home_root, ".chatadhd_data"), "w") as f:
        f.write("synthetic candidate\n")
    with open(os.path.join(home_root, "profiles", "cli.pack"), "w") as f:
        json.dump({"schema": "loom.runtime_profile_overlay/1", "domain": "cli",
                   "overrides": {"creation": {"conversation_title": "sentinel candidate"}}}, f)
    candidate_before = tree_bytes(home_root)
    assert profile_only(None, "inspect", "cli", env=env)["values"]["creation"]["conversation_title"] == "sentinel candidate"
    assert tree_bytes(home_root) == candidate_before and not os.path.exists(discover_xdg)

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

    # Execution profiles: actual CLI bootstrap, effective settings, and checked persistence.
    domains = js("profile", "list")
    assert {"cli", "selector", "materialize", "memory"}.issubset(domains), domains
    builtin_help = js("profile", "inspect", "cli")["values"]["help"]
    assert run("--help").stdout == builtin_help
    cli_before = js("profile", "inspect", "cli")
    settings = {"help": "profile help fixture\n", "commands": {"vfixture": "version", "cfixture": "conv"},
                "subcommands": {"conv": {"newfixture": "create"}, "profile": {"peekfixture": "inspect"}},
                "flags": cli_before["values"]["flags"] + ["fixture-toggle"],
                "limits": {"conversation_list": 2}, "creation": {"conversation_title": "Profile title"}}
    validated = js("profile", "validate", "cli", stdin=json.dumps(settings))
    assert validated["hash"] != cli_before["hash"]
    assert js("profile", "inspect", "cli")["hash"] == cli_before["hash"], "validate must not write"
    saved = js("profile", "save", "cli", stdin=json.dumps(settings))
    assert js("profile", "inspect", "cli")["hash"] == saved["hash"]
    assert run("--help").stdout == "profile help fixture\n"
    assert js("vfixture")["abi"] >= 1
    assert js("profile", "peekfixture", "cli")["hash"] == saved["hash"]
    assert js("cfixture", "newfixture")["title"] == "Profile title"
    assert len(js("conv", "list", "--fixture-toggle")) == 2
    assert len(js("conv", "list", "--limit", "100")) > 2, "explicit limit wins"
    stored_path = os.path.join(data, "profiles", "cli.pack")
    stored_bytes = open(stored_path, "rb").read()
    rejected = js("profile", "save", "cli", stdin=json.dumps({"limits": {"conversation_list": "two"}}), code=1)
    assert "error" in rejected
    assert open(stored_path, "rb").read() == stored_bytes, "invalid save must preserve file"
    rejected = js("profile", "save", "cli", stdin=json.dumps({"presentation": {"score_decimal_scale": 0}}), code=1)
    assert "error" in rejected
    assert open(stored_path, "rb").read() == stored_bytes, "zero decimal scale must not poison bootstrap"
    # Wider values must pass checked native size_t conversion without wrapping.
    native_size_max = (1 << (8 * struct.calcsize("P"))) - 1
    large_sizes = {"presentation": {"widths": {"default": native_size_max},
                                    "timestamps": {"message": native_size_max}, "hash_chars": native_size_max,
                                    "score_decimal_scale": float.fromhex("0x0.0000000000001p-1022")}}
    saved_sizes = js("profile", "save", "cli", stdin=json.dumps(large_sizes))
    assert saved_sizes["values"]["presentation"]["hash_chars"] == native_size_max
    assert js("profile", "inspect", "cli")["hash"] == saved_sizes["hash"]
    assert js("version")["abi"] >= 1, "bootstrap accepts positive denormal scale and native presentation maxima"
    os.remove(stored_path)
    assert run("--help").stdout == builtin_help

    # A dictionary removal must survive save, reload and a subsequent partial override.
    changed = js("profile", "save", "materialize", stdin=json.dumps({
        "schema": "loom.runtime_profile_overlay/1", "domain": "materialize", "overrides": {},
        "patch": [{"op": "remove", "path": "/languages/.cpp"}]}))
    assert ".cpp" not in changed["values"]["languages"]
    changed = js("profile", "save", "materialize", stdin=json.dumps({"language": "pl"}))
    reloaded = js("profile", "inspect", "materialize")
    assert ".cpp" not in reloaded["values"]["languages"] and reloaded["values"]["language"] == "pl"
    assert changed["hash"] == reloaded["hash"]
    os.remove(os.path.join(data, "profiles", "materialize.pack"))

    # Existing malformed overlays fail explicitly, including early help parsing.
    with open(stored_path, "w") as f:
        f.write("{broken")
    assert "error" in js("profile", "inspect", "cli", code=1)
    assert run("--help", code=1).returncode == 1
    os.remove(stored_path)

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
