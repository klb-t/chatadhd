#!/usr/bin/env python3
"""Smoke tests for the `loom` CLI (run by ctest as cli.smoke).

Each check runs the real binary against a temporary data directory and
asserts on exit codes and JSON output. No network access is needed.
"""
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

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
