"""Model Registry - loads models from OpenRouter API"""
import json
import requests
from pathlib import Path

class ModelRegistry:
    def __init__(self, path, config, secrets):
        self.path = Path(path)
        self.config = config
        self.secrets = secrets
        self._models = {}
        self._load()
    
    def _load(self):
        if self.path.exists():
            try:
                self._models = json.loads(self.path.read_text())
            except: pass
    
    def _save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(self._models, indent=2))
    
    def update_from_api(self):
        """Fetch models from OpenRouter API."""
        try:
            key = self.secrets.get('api_key', '')
            if not key:
                return False
            
            url = self.config.get('base_url', '').rstrip('/') + '/models'
            headers = {'Authorization': f'Bearer {key}'}
            
            resp = requests.get(url, headers=headers, timeout=15)
            if resp.status_code == 200:
                data = resp.json()
                models = data.get('data', [])
                
                self._models = {}
                for m in models:
                    mid = m.get('id', '')
                    self._models[mid] = {
                        'name': m.get('name', mid),
                        'context_length': m.get('context_length', 0),
                        'pricing': m.get('pricing', {}),
                        'description': m.get('description', ''),
                    }
                
                self._save()
                return True
        except Exception as e:
            print(f"Model fetch error: {e}")
        return False
    
    def all(self):
        return self._models
    
    def get(self, model_id):
        return self._models.get(model_id, {})
    
    def name(self, model_id):
        if not model_id:
            return "No model"
        info = self._models.get(model_id, {})
        return info.get('name', model_id.split('/')[-1] if '/' in model_id else model_id)
