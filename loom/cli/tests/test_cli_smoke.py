#!/usr/bin/env python3
"""Smoke tests for the `loom` CLI (run by ctest as cli.smoke).

Each check runs the real binary against a temporary data directory and
asserts on exit codes and JSON output. No network access is needed.
"""
import json
import os
import shutil
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
