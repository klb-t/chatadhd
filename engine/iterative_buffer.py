from dataclasses import dataclass
from typing import List
from datetime import datetime
import uuid

@dataclass
class Stage:
    id: str
    text: str
    created: str

class IterativePromptBuffer:
    def __init__(self): self.stages: List[Stage] = []
    def add_stage(self, text):
        sid = 's_' + uuid.uuid4().hex[:8]
        self.stages.append(Stage(sid, text, datetime.utcnow().isoformat()))
        return sid
    def list_stages(self): return [(s.id, s.created, s.text) for s in self.stages]
    def aggregate(self): return "\n\n".join(f"--- Stage {s.id} ---\n{s.text}" for s in self.stages)
    def mark_realized(self, sid): self.stages = [s for s in self.stages if s.id != sid]
    def clear(self): self.stages = []
