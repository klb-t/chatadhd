#!/usr/bin/env python3
"""PH29 P1: the browser's SQLite store uses the kernel's own table layout.

The kernel's DDL is spread over schema_sql.inc (the five Python-compatible tables), forward-only migrations and the
annotation/provenance tables; the only complete source is a database the kernel itself created. This tool runs
`loom --data-dir <tmp> init` (the built CLI), reads sqlite_master of the fresh database and writes the CREATE TABLE and
CREATE INDEX statements of the bundle's tables (the kernel's preset allow-list, read from working_copy.cpp) as data:
loom/web/src/context/kernel-schema.json. Triggers and virtual tables (FTS) are left out: P1 does not need them.

Usage: python3 loom/tools/gen_web_kernel_schema.py [--loom loom/build/dev/cli/loom] [--check]
"""
import argparse
import json
import re
import sqlite3
import subprocess
import sys
import tempfile
from pathlib import Path

LOOM = Path(__file__).resolve().parents[1]
OUT = LOOM / "web/src/context/kernel-schema.json"


def bundle_tables() -> list[str]:
    src = (LOOM / "src/working_copy/working_copy.cpp").read_text(encoding="utf8")
    m = re.search(r"default_tables\(\)\s*\{\s*static const std::vector<std::string> t = \{(.*?)\};", src, re.S)
    if not m:
        sys.exit("gen_web_kernel_schema: default_tables() not found in working_copy.cpp")
    return re.findall(r'"([^"]+)"', m.group(1))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--loom", default=str(LOOM / "build/dev/cli/loom"))
    ap.add_argument("--check", action="store_true", help="fail when the file on disk differs")
    a = ap.parse_args()
    tables = bundle_tables()
    with tempfile.TemporaryDirectory() as d:
        subprocess.run([a.loom, "--data-dir", d, "--quiet", "init"], check=True, stdout=subprocess.DEVNULL)
        db = sqlite3.connect(Path(d) / "chatadhd.db")
        rows = db.execute("SELECT type, name, tbl_name, sql FROM sqlite_master WHERE sql IS NOT NULL ORDER BY rowid").fetchall()
        db.close()
    out = {
        "schema": "loom.web_kernel_schema/1",
        "generated_by": "loom/tools/gen_web_kernel_schema.py (from a database created by `loom init`)",
        "bundle_tables": tables,
        "tables": {},
        "indexes": {},
    }
    for typ, name, tbl, sql in rows:
        if tbl not in tables:
            continue
        if typ == "table" and not sql.upper().startswith("CREATE VIRTUAL"):
            out["tables"][name] = " ".join(sql.split())
        elif typ == "index":
            out["indexes"].setdefault(tbl, []).append(" ".join(sql.split()))
    text = json.dumps(out, indent=2, ensure_ascii=False) + "\n"
    if a.check:
        same = OUT.exists() and OUT.read_text(encoding="utf8") == text
        print("kernel-schema.json up to date" if same else "kernel-schema.json differs: regenerate")
        return 0 if same else 1
    OUT.write_text(text, encoding="utf8")
    print(f"wrote {OUT} ({len(out['tables'])} tables)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
