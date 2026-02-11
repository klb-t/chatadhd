
import logging, json
from typing import List, Dict, Any, Optional
log = logging.getLogger('api_client')

class APIClient:
    def __init__(self, base_url: str = 'https://openrouter.ai/api/v1', api_key: Optional[str] = None):
        self.base_url = base_url
        self.api_key = api_key

    def chat(self, system_prompt: str, messages: List[Dict[str,str]], model: str = 'local-echo', temperature: float = 0.7, max_tokens: int = 4096) -> str:
        # Minimal fallback: echo recent user message and include system prompt summary
        last_user = next((m for m in reversed(messages) if m.get('role')=='user'), None)
        return (last_user.get('content') if last_user else '') + "\n\n[system-summary]\n" + (system_prompt[:1000] if system_prompt else '[no-system]')
