"""Configuration management"""
import json
from pathlib import Path

class Config:
    DEFAULTS = {
        "base_url": "https://openrouter.ai/api/v1",
        "default_model": "anthropic/claude-sonnet-4-20250514",
        "temperature": 0.7,
        "max_tokens": 4096,
        "theme": "dark",
    }
    
    def __init__(self, path):
        self.path = Path(path)
        self._data = self.DEFAULTS.copy()
        self._load()
    
    def _load(self):
        if self.path.exists():
            try:
                with open(self.path, 'r') as f:
                    self._data.update(json.load(f))
            except: pass
    
    def save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with open(self.path, 'w') as f:
            json.dump(self._data, f, indent=2)
    
    def get(self, key, default=None):
        return self._data.get(key, default)
    
    def set(self, key, value):
        self._data[key] = value
        self.save()


class Secrets:
    def __init__(self, path):
        self.path = Path(path)
        self._data = {}
        self._load()
    
    def _load(self):
        if self.path.exists():
            try:
                with open(self.path, 'r') as f:
                    self._data = json.load(f)
            except: pass
    
    def save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with open(self.path, 'w') as f:
            json.dump(self._data, f)
    
    def get(self, key, default=None):
        return self._data.get(key, default)
    
    def set(self, key, value):
        self._data[key] = value
        self.save()
