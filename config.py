"""Configuration management"""
import json, logging
from pathlib import Path
from typing import Any, Dict

log = logging.getLogger('config')

DEFAULTS = {
    "base_url": "https://openrouter.ai/api/v1",
    "default_model": "anthropic/claude-sonnet-4",
    "temperature": 0.7,
    "max_tokens": 4096,
    "memory_k": 8
}

DEFAULT_PRESETS = {
    "default": {"name": "Default", "prompt": "You are a helpful assistant."},
    "epistemic": {"name": "Epistemic", "prompt": "Challenge assumptions, point out flaws, explore perspectives."},
    "concise": {"name": "Concise", "prompt": "Be brief and direct."}
}

class Config:
    def __init__(self, base_dir: Path):
        self.path = base_dir / "config.json"
        self.data = {**DEFAULTS}
        if self.path.exists():
            try: self.data.update(json.load(open(self.path)))
            except: pass
    
    def get(self, key, default=None): return self.data.get(key, default)
    def set(self, key, value): self.data[key] = value
    def save(self): json.dump(self.data, open(self.path, 'w'), indent=2)

class Secrets:
    def __init__(self, base_dir: Path):
        self.path = base_dir / "secrets.json"
        self.data = {}
        if self.path.exists():
            try: self.data = json.load(open(self.path))
            except: pass
    
    def get(self, key, default=None): return self.data.get(key, default)
    def set(self, key, value): self.data[key] = value; self.save()
    def save(self): json.dump(self.data, open(self.path, 'w'), indent=2)

class Presets:
    def __init__(self, base_dir: Path):
        self.path = base_dir / "presets.json"
        self.data = {**DEFAULT_PRESETS}
        for p in [self.path, base_dir / "_presets.json"]:
            if p.exists():
                try: self.data.update(json.load(open(p))); break
                except: pass
    
    def get(self, key): return self.data.get(key, {"name": key, "prompt": ""})
    def all(self): return self.data

class Models:
    def __init__(self, base_dir: Path, config, secrets):
        self.path = base_dir / "models.json"
        self.config, self.secrets = config, secrets
        self.data = {"models": {}, "_meta": {}}
        if self.path.exists():
            try: self.data = json.load(open(self.path))
            except: pass
    
    def all(self): return self.data.get("models", {})
    def name(self, mid): return self.data.get("models", {}).get(mid, {}).get("name", mid or "AI")
    def save(self): json.dump(self.data, open(self.path, 'w'), indent=2)
    
    def update_from_api(self):
        key = self.secrets.get("api_key")
        if not key: return False
        try:
            import requests
            from datetime import datetime
            r = requests.get(f"{self.config.get('base_url')}/models", headers={"Authorization": f"Bearer {key}"}, timeout=15)
            r.raise_for_status()
            for m in r.json().get("data", []):
                mid = m.get("id", "")
                if mid:
                    pr = m.get("pricing", {})
                    self.data.setdefault("models", {})[mid] = {
                        "name": m.get("name", mid.split("/")[-1]),
                        "pricing": {"prompt": round(float(pr.get("prompt", 0))*1e6, 2), "completion": round(float(pr.get("completion", 0))*1e6, 2)}
                    }
            self.data["_meta"] = {"updated": datetime.now().isoformat()}
            self.save()
            return True
        except Exception as e:
            log.error(f"Model update failed: {e}")
            return False
