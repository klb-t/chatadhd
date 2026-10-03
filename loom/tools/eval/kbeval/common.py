"""Shared helpers of the knowledge-layer evaluation harness.

Everything here talks to Loom only through its public surfaces: the `loom`
command line (`loom --json knowledge run --config ...`) and the data
directory it writes (the loom_kb_* / loom_cat_* tables, read-only).
"""
from __future__ import annotations

import json
import os
import re
import sqlite3
import subprocess
import tempfile
import unicodedata
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable


def new_workspace(parent: Path, prefix: str = "run-") -> Path:
    """Create a private run directory without clearing any caller-owned data."""
    parent = parent.expanduser().resolve()
    parent.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix=prefix, dir=parent))


def run_knowledge(loom: str, data_dir: Path, config: dict[str, Any], *, timeout: int = 3600) -> dict[str, Any]:
    """Runs `loom knowledge run` with `config` and returns its JSON result."""
    data_dir.mkdir(parents=True, exist_ok=True)
    cfg_path = data_dir / "knowledge_config.json"
    cfg_path.write_text(json.dumps(config, ensure_ascii=False, indent=1), encoding="utf-8")
    cmd = [loom, "--json", "--quiet", "--data-dir", str(data_dir), "knowledge", "run", "--config", str(cfg_path)]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env={**os.environ, "CHATADHD_DATA": str(data_dir)})
    try:
        out = json.loads(p.stdout)
    except json.JSONDecodeError as e:
        raise RuntimeError(f"loom knowledge run: no JSON (exit {p.returncode})\nstdout: {p.stdout[-2000:]}\nstderr: {p.stderr[-4000:]}") from e
    if not isinstance(out, dict):
        raise RuntimeError("loom knowledge run: expected a JSON object")
    if p.returncode:
        raise RuntimeError(f"loom knowledge run exited {p.returncode}: {out.get('error')}\nstderr: {p.stderr[-4000:]}")
    if "error" in out and out.get("error") is not None and not isinstance(out.get("error"), str):
        raise RuntimeError(f"loom knowledge run failed: {out['error']}")
    if out.get("status") != "done":
        raise RuntimeError(f"loom knowledge run: status {out.get('status')}: {out.get('error')}\nstderr: {p.stderr[-4000:]}")
    return out


def fold(s: str) -> str:
    """Case/diacritics-insensitive match key (PL + EN), whitespace collapsed."""
    s = unicodedata.normalize("NFKD", s.replace("ł", "l").replace("Ł", "L"))
    s = "".join(c for c in s if not unicodedata.combining(c)).lower()
    return re.sub(r"\s+", " ", re.sub(r"[^\w.+#-]+", " ", s)).strip()


def overlap(a: str, b: str) -> float:
    """Token Jaccard of two texts (folded, tokens >= 3 chars)."""
    ta = {t for t in fold(a).split() if len(t) >= 3}
    tb = {t for t in fold(b).split() if len(t) >= 3}
    if not ta or not tb:
        return 0.0
    return len(ta & tb) / len(ta | tb)


@dataclass
class KB:
    """Read-only view of one knowledge run in a data directory."""

    db_path: Path
    run: str
    con: sqlite3.Connection = field(init=False)

    def __post_init__(self) -> None:
        self.con = sqlite3.connect(f"file:{self.db_path}?mode=ro", uri=True)

    def bodies(self, table: str, where: str = "", args: Iterable[Any] = ()) -> list[dict[str, Any]]:
        q = f"SELECT body FROM {table} WHERE run_id = ?" + (f" AND {where}" if where else "") + " ORDER BY id"
        return [json.loads(r[0]) for r in self.con.execute(q, (self.run, *args))]

    def entities(self) -> list[dict[str, Any]]:
        return self.bodies("loom_kb_entities")

    def claims(self) -> list[dict[str, Any]]:
        return self.bodies("loom_kb_claims")

    def observations(self) -> list[dict[str, Any]]:
        return self.bodies("loom_kb_observations")

    def table(self, name: str) -> list[dict[str, Any]]:
        return self.bodies(f"loom_kb_{name}")

    def unit_ext_ids(self) -> dict[str, str]:
        """catalog unit id -> ext_id (the export's own id)."""
        try:
            return {r[0]: r[1] for r in self.con.execute("SELECT id, ext_id FROM loom_cat_units")}
        except sqlite3.OperationalError:
            return {}

    def selection(self) -> dict[str, bool]:
        """ext_id -> selected, for the latest catalog score run."""
        try:
            row = self.con.execute("SELECT run_id FROM loom_cat_decisions ORDER BY rowid DESC LIMIT 1").fetchone()
        except sqlite3.OperationalError:
            return {}
        if not row:
            return {}
        out: dict[str, bool] = {}
        for ext, sel in self.con.execute(
            "SELECT u.ext_id, d.selected FROM loom_cat_units u JOIN loom_cat_decisions d ON d.unit_id = u.id AND d.run_id = ?",
            (row[0],),
        ):
            out[ext] = out.get(ext, False) or bool(sel)
        return out


def ratio(n: int, d: int) -> float | None:
    return round(n / d, 4) if d else None


def claim_obs(c: dict[str, Any]) -> list[str]:
    sup = (((c.get("assessment") or {}).get("basis") or {}).get("support")) or []
    return [s.get("observation", "") for s in sup]


def claim_quotes(c: dict[str, Any]) -> list[str]:
    sup = (((c.get("assessment") or {}).get("basis") or {}).get("support")) or []
    return [s.get("quote", "") for s in sup]


def evidence_class(c: dict[str, Any]) -> str:
    return (c.get("assessment") or {}).get("evidence_class", "")

