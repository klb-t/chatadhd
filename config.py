"""Configuration - stored in DATA_DIR (persistent across versions)"""
import json
import logging
from pathlib import Path

log = logging.getLogger('config')

DEFAULTS = {
    "base_url": "https://openrouter.ai/api/v1",
    "default_model": "anthropic/claude-sonnet-4",
    "temperature": 0.7,
    "max_tokens": 4096,
    "memory_k": 8,
    "sync_enabled": False,
    "sync_provider": None  # 'google_drive', 'custom_server'
}

class Config:
    def __init__(self, data_dir: Path):
        self.path = data_dir / "config.json"
        self.data = {**DEFAULTS}
        self._load()
    
    def _load(self):
        if self.path.exists():
            try:
                self.data.update(json.load(open(self.path)))
                log.info(f"Config loaded from {self.path}")
            except Exception as e:
                log.warning(f"Config load failed: {e}")
    
    def get(self, key, default=None):
        return self.data.get(key, default)
    
    def set(self, key, value):
        self.data[key] = value
    
    def save(self):
        json.dump(self.data, open(self.path, 'w'), indent=2)
        log.info("Config saved")

class Secrets:
    """Sensitive data - API keys, tokens. NEVER in repo, NEVER in cloud without encryption"""
    def __init__(self, data_dir: Path):
        self.path = data_dir / "secrets.json"
        self.data = {}
        self._load()
    
    def _load(self):
        if self.path.exists():
            try:
                self.data = json.load(open(self.path))
            except:
                pass
    
    def get(self, key, default=None):
        return self.data.get(key, default)
    
    def set(self, key, value):
        self.data[key] = value
        self.save()
    
    def save(self):
        json.dump(self.data, open(self.path, 'w'), indent=2)

class Models:
    def __init__(self, data_dir: Path, config, secrets):
        self.path = data_dir / "models.json"
        self.config, self.secrets = config, secrets
        self.data = {"models": {}}
        if self.path.exists():
            try:
                self.data = json.load(open(self.path))
            except:
                pass
    
    def all(self):
        return self.data.get("models", {})
    
    def name(self, mid):
        return self.data.get("models", {}).get(mid, {}).get("name", mid.split("/")[-1] if mid else "AI")
    
    def save(self):
        json.dump(self.data, open(self.path, 'w'), indent=2)
    
    def update_from_api(self):
        key = self.secrets.get("api_key")
        if not key:
            return False
        try:
            import requests
            from datetime import datetime
            r = requests.get(f"{self.config.get('base_url')}/models",
                           headers={"Authorization": f"Bearer {key}"}, timeout=15)
            r.raise_for_status()
            for m in r.json().get("data", []):
                mid = m.get("id", "")
                if mid:
                    self.data.setdefault("models", {})[mid] = {
                        "name": m.get("name", mid.split("/")[-1])
                    }
            self.data["_updated"] = datetime.now().isoformat()
            self.save()
            return True
        except Exception as e:
            log.error(f"Model update failed: {e}")
            return False
