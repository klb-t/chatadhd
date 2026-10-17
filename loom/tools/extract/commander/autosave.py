"""Autosave: the recipe (steps, sources, settings, views, bookmarks, tags) and the panes, written
atomically (``.tmp`` → ``replace``) after every N recorded steps (preset ``commander.autosave.every``,
``0`` = off).  A start that finds the file offers to resume it: the recipe is replayed (without its
``write`` steps — writing is what the user does again) and the panes come back."""
import json
import logging
import os
import time
from pathlib import Path
from typing import Any, Optional

log = logging.getLogger(__name__)
SCHEMA = "loom.extract.autosave/1"


class Autosave:
    def __init__(self, path: Path, every: int = 1) -> None:
        self.path = Path(path)
        self.every = int(every)
        self.saved_steps = -1
        self.writes = 0

    def due(self, steps: int) -> bool:
        return self.every > 0 and (self.saved_steps < 0 or abs(steps - self.saved_steps) >= self.every)

    def write(self, recipe: dict[str, Any], panes: list[dict[str, Any]], extra: Optional[dict[str, Any]] = None) -> Path:
        doc = {"schema": SCHEMA, "saved_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "recipe": recipe, "panes": panes,
               **(extra or {})}
        self.path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_name(self.path.name + ".tmp")
        with open(tmp, "w", encoding="utf-8") as fh:
            json.dump(doc, fh, ensure_ascii=False, indent=1)
            fh.write("\n")
            fh.flush()
            os.fsync(fh.fileno())
        tmp.replace(self.path)
        self.saved_steps = len(recipe.get("steps") or [])
        self.writes += 1
        return self.path

    def load(self) -> Optional[dict[str, Any]]:
        if not self.path.exists():
            return None
        try:
            doc = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            log.warning("autosave %s unreadable; not offered for resume", self.path, exc_info=True)
            return None
        return doc if doc.get("schema") == SCHEMA else None

    def discard(self) -> None:
        try:
            self.path.unlink()
        except FileNotFoundError:
            log.debug("autosave already gone: %s", self.path)


def resume_recipe(doc: dict[str, Any]) -> dict[str, Any]:
    rec = dict(doc.get("recipe") or {})
    rec["steps"] = [s for s in rec.get("steps") or [] if s.get("op") != "write"]
    return rec
