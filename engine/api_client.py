"""API client for LLM providers"""
import json
import logging
import base64
import mimetypes
from pathlib import Path

log = logging.getLogger('api')

class APIClient:
    def __init__(self, config, secrets):
        self.config = config
        self.secrets = secrets
    
    def chat(self, messages, model=None, attachments=None):
        """Send chat request with optional attachments"""
        import requests
        
        api_key = self.secrets.get("api_key")
        if not api_key:
            raise ValueError("API key not set")
        
        base_url = self.config.get("base_url", "https://openrouter.ai/api/v1")
        model = model or self.config.get("default_model", "anthropic/claude-sonnet-4")
        
        # Build messages with attachments
        api_messages = []
        for msg in messages:
            if msg['role'] == 'user' and msg.get('attachments'):
                # Multi-modal message
                content = []
                content.append({"type": "text", "text": msg['text']})
                
                for att_path in msg['attachments']:
                    file_content = self._encode_file(att_path)
                    if file_content:
                        content.append(file_content)
                
                api_messages.append({"role": "user", "content": content})
            else:
                api_messages.append({"role": msg['role'], "content": msg['text']})
        
        log.info(f"API call: {model}, {len(api_messages)} messages")
        
        resp = requests.post(
            f"{base_url}/chat/completions",
            headers={
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json",
                "HTTP-Referer": "https://chatadhd.app",
                "X-Title": "ChatADHD"
            },
            json={
                "model": model,
                "messages": api_messages,
                "temperature": self.config.get("temperature", 0.7),
                "max_tokens": self.config.get("max_tokens", 4096)
            },
            timeout=120
        )
        
        resp.raise_for_status()
        data = resp.json()
        
        return data['choices'][0]['message']['content']
    
    def _encode_file(self, path):
        """Encode file for API (images as base64)"""
        path = Path(path)
        if not path.exists():
            log.warning(f"File not found: {path}")
            return None
        
        mime, _ = mimetypes.guess_type(str(path))
        
        # Images - encode as base64
        if mime and mime.startswith('image/'):
            with open(path, 'rb') as f:
                data = base64.b64encode(f.read()).decode('utf-8')
            return {
                "type": "image_url",
                "image_url": {
                    "url": f"data:{mime};base64,{data}"
                }
            }
        
        # Text files - read content
        if mime and (mime.startswith('text/') or mime in ['application/json', 'application/xml']):
            try:
                with open(path, 'r', encoding='utf-8') as f:
                    content = f.read()
                return {
                    "type": "text",
                    "text": f"\n--- File: {path.name} ---\n{content}\n--- End of {path.name} ---\n"
                }
            except:
                pass
        
        # Other files - just mention them
        return {
            "type": "text",
            "text": f"\n[Attached file: {path.name} ({mime or 'unknown type'})]\n"
        }
