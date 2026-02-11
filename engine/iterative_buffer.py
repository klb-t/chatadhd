
"""Aggregates multi-step edits into a single prompt payload.
Stages can be marked 'realized' so they won't be sent further once applied."""
from dataclasses import dataclass, field
from typing import List, Dict, Any
import datetime, uuid

@dataclass
class Stage:
    id: str
    text: str
    created: str

class IterativePromptBuffer:
    def __init__(self):
        self.stages: List[Stage] = []

    def add_stage(self, text: str) -> str:
        sid = 's_' + uuid.uuid4().hex[:8]
        self.stages.append(Stage(sid, text, datetime.datetime.utcnow().isoformat()))
        return sid

    def list_stages(self):
        return [(s.id, s.created, s.text) for s in self.stages]

    def aggregate(self, compress: bool = False) -> str:
        # naive aggregation: join stages with separators;
        # compress flag can be used later to run a small summarizer
        parts = []
        for s in self.stages:
            parts.append(f"--- Stage {s.id} ({s.created}) ---\n" + s.text)
        return "\n\n".join(parts)

    def mark_realized(self, sid: str):
        # remove stage with id sid (assume applied)
        self.stages = [s for s in self.stages if s.id != sid]
