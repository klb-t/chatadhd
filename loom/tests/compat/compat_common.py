"""Shared helpers for the Python <-> Loom differential tests.

The C++ side is `loom_compat_tool` (path from $LOOM_COMPAT_TOOL, set by
ctest). The Python side is the real ChatADHD engine imported from the repo
root, so every comparison is against the reference implementation itself.
"""
import json
import os
import pathlib
import re
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[3]
if str(REPO) not in sys.path:
    sys.path.insert(0, str(REPO))

import logging  # noqa: E402

logging.basicConfig(level=logging.CRITICAL)


def _find_tool() -> str:
    env = os.environ.get("LOOM_COMPAT_TOOL")
    if env:
        return env
    for preset in ("dev", "asan", "tsan", "vendored", "release"):
        p = REPO / "loom" / "build" / preset / "loom_compat_tool"
        if p.exists():
            return str(p)
    raise unittest.SkipTest("loom_compat_tool not found (set LOOM_COMPAT_TOOL)")


TOOL = None


def tool(*args, env=None):
    """Runs loom_compat_tool and returns its parsed JSON stdout."""
    global TOOL
    if TOOL is None:
        TOOL = _find_tool()
    proc = subprocess.run([TOOL, *map(str, args)], capture_output=True, text=True, env=env, timeout=300)
    if proc.returncode != 0:
        raise AssertionError(f"loom_compat_tool {' '.join(map(str, args))} failed ({proc.returncode}):\n"
                             f"{proc.stderr}\n{proc.stdout[:2000]}")
    return json.loads(proc.stdout)


def tool_popen(*args):
    global TOOL
    if TOOL is None:
        TOOL = _find_tool()
    return subprocess.Popen([TOOL, *map(str, args)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


class TempDir:
    def __init__(self):
        self.path = pathlib.Path(tempfile.mkdtemp(prefix="loom_compat_"))

    def file_json(self, name, obj):
        p = self.path / name
        p.write_text(json.dumps(obj, ensure_ascii=False), encoding="utf-8")
        return "@" + str(p)

    def cleanup(self):
        shutil.rmtree(self.path, ignore_errors=True)


# ── Python executor mirroring tool_db.cpp:run_op ───────────────────────────

_KW_FIELDS = {"update_conv", "update_msg", "update_node"}


def _resolve(v, vars_):
    if isinstance(v, str) and len(v) > 1 and v[0] == "$":
        parts = v[1:].split(".")
        cur = vars_[parts[0]]
        for p in parts[1:]:
            cur = cur[int(p)] if isinstance(cur, list) else cur[p]
        return cur
    if isinstance(v, list):
        return [_resolve(x, vars_) for x in v]
    if isinstance(v, dict):
        return {k: _resolve(x, vars_) for k, x in v.items()}
    return v


def run_ops_py(db, ops, vars_=None):
    vars_ = dict(vars_ or {})
    results = []
    for op in ops:
        name = op["op"]
        args = _resolve(op.get("args", {}), vars_)
        try:
            fn = getattr(db, name)
            if name in _KW_FIELDS:
                res = fn(args["id"], **args.get("fields", {}))
            else:
                res = fn(**args)
        except Exception:  # noqa: BLE001 - mirrors {"error": true} in C++
            res = {"error": True}
        # Plain JSON types, and JSON-text columns parsed (Python returns some
        # of them raw: list_convs, get_versions, get_links, get_unanalysed_msgs).
        res = pyshape(json.loads(json.dumps(res)))
        if "save" in op:
            vars_[op["save"]] = res
        results.append(res)
    return {"results": results, "vars": vars_}


# ── Canonical dumps ────────────────────────────────────────────────────────

JSON_KEYS = {"metadata", "attachments", "tags"}


def pyshape(obj):
    """Parse JSON-text columns that Python returns raw (metadata, ...)."""
    if isinstance(obj, dict):
        out = {}
        for k, v in obj.items():
            if k in JSON_KEYS and isinstance(v, str):
                try:
                    v = json.loads(v or ("[]" if k != "metadata" else "{}"))
                except json.JSONDecodeError:
                    pass
            out[k] = pyshape(v)
        return out
    if isinstance(obj, list):
        return [pyshape(x) for x in obj]
    return obj


def api_dump_py(db):
    big = 10 ** 9
    convs = db.list_convs(limit=big)
    msgs = []
    for c in convs:
        msgs.extend(db.get_msgs(c["id"], include_all=True))
    return pyshape({
        "conversations": convs,
        "messages": msgs,
        "nodes": db.list_nodes(limit=big),
        "links": db.get_links(),
        "graph": db.get_graph_data(),
        "pending": db.count_pending_semantic(),
        "unanalysed": [m["id"] for m in db.get_unanalysed_msgs(limit=big)],
        "schema_version": db._get_meta("schema_version"),
    })


def canonical_api_dump(d):
    """Order-insensitive form of an API dump (lists sorted by id)."""
    d = json.loads(json.dumps(d))
    for k in ("conversations", "messages", "nodes", "links"):
        d[k] = sorted(d[k], key=lambda r: r["id"])
    d["unanalysed"] = sorted(d["unanalysed"])
    if d.get("graph"):
        d["graph"]["nodes"] = sorted(d["graph"]["nodes"], key=lambda r: r["id"])
        d["graph"]["edges"] = sorted(d["graph"]["edges"], key=lambda r: json.dumps(r, sort_keys=True))
    return d


def normalized_dump(normalizer, dump):
    """Normalise ids/timestamps of an API dump, then re-sort by placeholder so
    two executions with different random ids line up."""
    return canonical_api_dump(normalizer(dump))


def raw_dump_py(path):
    conn = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    out = {}
    for table in ("_meta", "conversations", "messages", "nodes", "links"):
        if not conn.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", (table,)).fetchone():
            continue
        cols = [r[1] for r in conn.execute(f"PRAGMA table_info({table})")]
        sel = ", ".join(f'typeof("{c}"), "{c}"' for c in cols)
        rows = []
        for r in conn.execute(f"SELECT {sel} FROM {table} ORDER BY rowid"):
            row = {}
            for i, c in enumerate(cols):
                t, v = r[2 * i], r[2 * i + 1]
                if isinstance(v, bytes):
                    v = "<blob>"
                row[c] = [t, v]
            rows.append(row)
        out[table] = {"columns": cols, "rows": rows}
    conn.close()
    return out


def schema_py(path):
    conn = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    rows = conn.execute("SELECT type, name, tbl_name, sql FROM sqlite_master "
                        "WHERE name NOT LIKE 'sqlite_%' ORDER BY type, name").fetchall()
    conn.close()
    return [list(r) for r in rows]


def core_schema(entries):
    """Drop Loom-only objects (loom_* tables and their indexes)."""
    return [e for e in entries if not e[1].startswith("loom_") and not e[1].startswith("idx_loom_")]


def core_raw(raw):
    raw = json.loads(json.dumps(raw))
    if "_meta" in raw:
        raw["_meta"]["rows"] = [r for r in raw["_meta"]["rows"] if not r["key"][1].startswith("loom_")]
    return raw


# ── Normalisation for comparing two different executions ──────────────────

ID_RE = re.compile(r"\b(c|m|vg|n|l)_[0-9a-f]{12}\b")
TS_RE = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d{6})?Z$")


class Normalizer:
    """Maps random IDs to stable placeholders (by first appearance) and
    timestamps to "<ts>" after checking their exact Python format."""

    def __init__(self):
        self.ids = {}
        self.counts = {}

    def _id(self, m):
        real = m.group(0)
        if real not in self.ids:
            prefix = m.group(1)
            self.counts[prefix] = self.counts.get(prefix, 0) + 1
            self.ids[real] = f"{prefix}_<{self.counts[prefix]}>"
        return self.ids[real]

    def __call__(self, obj):
        if isinstance(obj, dict):
            return {k: self(obj[k]) for k in sorted(obj)}
        if isinstance(obj, list):
            return [self(x) for x in obj]
        if isinstance(obj, str):
            if TS_RE.match(obj):
                return "<ts>"
            return ID_RE.sub(self._id, obj)
        if isinstance(obj, float) and obj.is_integer():
            return int(obj)
        return obj


def diff_hint(a, b, path="$"):
    """First differing path between two JSON values (for assertion messages)."""
    if type(a) != type(b) and not (isinstance(a, (int, float)) and isinstance(b, (int, float))):
        return f"{path}: {type(a).__name__} {str(a)[:200]!r} != {type(b).__name__} {str(b)[:200]!r}"
    if isinstance(a, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a or k not in b:
                return f"{path}.{k}: missing on {'left' if k not in a else 'right'}"
            h = diff_hint(a[k], b[k], f"{path}.{k}")
            if h:
                return h
        return None
    if isinstance(a, list):
        if len(a) != len(b):
            return f"{path}: length {len(a)} != {len(b)}"
        for i, (x, y) in enumerate(zip(a, b)):
            h = diff_hint(x, y, f"{path}[{i}]")
            if h:
                return h
        return None
    return None if a == b else f"{path}: {str(a)[:200]!r} != {str(b)[:200]!r}"


class CompatTestCase(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        self.tmp = TempDir()

    def tearDown(self):
        self.tmp.cleanup()

    def assertSameJson(self, a, b, msg=""):
        h = diff_hint(a, b)
        if h:
            self.fail(f"{msg} first difference at {h}")
