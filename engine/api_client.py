import logging
from typing import List, Dict
log = logging.getLogger('api')

class APIClient:
    def __init__(self, base_url='https://openrouter.ai/api/v1', api_key=None):
        self.base_url, self.api_key = base_url, api_key
    
    def chat(self, messages: List[Dict], model: str, temperature=0.7, max_tokens=4096) -> str:
        if not self.api_key: raise ValueError("No API key")
        log.info(f"API: {model}, {len(messages)} msgs")
        try:
            from openai import OpenAI
            return OpenAI(api_key=self.api_key, base_url=self.base_url).chat.completions.create(
                model=model, messages=messages, temperature=temperature, max_tokens=max_tokens
            ).choices[0].message.content
        except ImportError:
            import requests
            r = requests.post(f"{self.base_url}/chat/completions",
                headers={"Authorization": f"Bearer {self.api_key}", "Content-Type": "application/json"},
                json={"model": model, "messages": messages, "temperature": temperature, "max_tokens": max_tokens}, timeout=120)
            r.raise_for_status()
            return r.json()['choices'][0]['message']['content']
