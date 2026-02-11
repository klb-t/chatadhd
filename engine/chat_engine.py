"""Chat Engine with streaming support"""
import requests
import json
import base64
from pathlib import Path

class ChatEngine:
    def __init__(self, config, secrets, db, memory=None):
        self.config = config
        self.secrets = secrets
        self.db = db
        self.memory = memory
        self.conv = None
        
        # Load last conversation or create new
        convs = self.db.list_convs()
        if convs:
            self.conv = convs[0]
    
    def new_conv(self, title="New Chat"):
        self.conv = self.db.create_conv(title)
        return self.conv
    
    def load_conv(self, conv_id):
        self.conv = self.db.get_conv(conv_id)
        return self.conv
    
    def send(self, text, attachments=None, on_chunk=None):
        """Send message and get response. on_chunk callback enables streaming."""
        if not self.conv:
            self.new_conv()
        
        # Save user message
        user_mid = self.db.create_msg(self.conv['id'], text, 'user', attachments=attachments)
        
        # Build messages for API
        messages = self._build_messages(text, attachments)
        
        # Get response
        model = self.config.get('default_model')
        response_text = self._call_api(messages, model, on_chunk)
        
        # Save assistant message
        self.db.create_msg(self.conv['id'], response_text, 'assistant', model=model)
        
        # Update conversation title if first message
        msgs = self.db.get_msgs(self.conv['id'])
        if len(msgs) <= 2:
            title = text[:30] + ('...' if len(text) > 30 else '')
            self.db.update_conv(self.conv['id'], title=title)
            self.conv['title'] = title
        
        return response_text
    
    def _build_messages(self, current_text, attachments=None):
        messages = []
        
        # Memory context
        if self.memory:
            mem_ctx = self.memory.get_active_context()
            if mem_ctx:
                messages.append({
                    "role": "system",
                    "content": f"User's memory/context:\n{mem_ctx}"
                })
        
        # Conversation history
        if self.conv:
            for m in self.db.get_msgs(self.conv['id']):
                if m['status'] == 'active':
                    # Apply weight
                    content = m['text']
                    weight = m.get('weight', 1.0)
                    if weight > 1.5:
                        content = f"[IMPORTANT] {content}"
                    elif weight < 0.5:
                        content = f"[low priority] {content}"
                    
                    messages.append({"role": m['role'], "content": content})
        
        # Current message with attachments
        content = self._build_content(current_text, attachments)
        messages.append({"role": "user", "content": content})
        
        return messages
    
    def _build_content(self, text, attachments):
        """Build message content, handling file attachments."""
        if not attachments:
            return text
        
        content = [{"type": "text", "text": text}]
        
        for path in attachments:
            p = Path(path)
            if not p.exists():
                continue
            
            # Images
            if p.suffix.lower() in ('.png', '.jpg', '.jpeg', '.gif', '.webp'):
                try:
                    data = base64.b64encode(p.read_bytes()).decode()
                    mime = f"image/{p.suffix[1:].lower()}"
                    if mime == 'image/jpg':
                        mime = 'image/jpeg'
                    content.append({
                        "type": "image_url",
                        "image_url": {"url": f"data:{mime};base64,{data}"}
                    })
                except: pass
            
            # Text files
            elif p.suffix.lower() in ('.txt', '.py', '.md', '.json', '.csv', '.xml', '.html', '.css', '.js'):
                try:
                    file_text = p.read_text(errors='replace')[:10000]
                    content.append({
                        "type": "text",
                        "text": f"\n--- FILE: {p.name} ---\n{file_text}\n--- END FILE ---"
                    })
                except: pass
        
        return content
    
    def _call_api(self, messages, model, on_chunk=None):
        """Call OpenRouter API with optional streaming."""
        key = self.secrets.get('api_key')
        if not key:
            raise ValueError("No API key configured")
        
        url = self.config.get('base_url', '').rstrip('/') + '/chat/completions'
        headers = {
            'Authorization': f'Bearer {key}',
            'Content-Type': 'application/json',
        }
        
        stream = on_chunk is not None
        
        payload = {
            'model': model,
            'messages': messages,
            'temperature': self.config.get('temperature', 0.7),
            'max_tokens': self.config.get('max_tokens', 4096),
            'stream': stream,
        }
        
        resp = requests.post(url, headers=headers, json=payload, stream=stream, timeout=120)
        
        if resp.status_code != 200:
            raise ValueError(f"API error {resp.status_code}: {resp.text[:200]}")
        
        if stream:
            full_text = ""
            for line in resp.iter_lines():
                if line:
                    line = line.decode('utf-8')
                    if line.startswith('data: '):
                        data_str = line[6:]
                        if data_str.strip() == '[DONE]':
                            break
                        try:
                            data = json.loads(data_str)
                            delta = data.get('choices', [{}])[0].get('delta', {})
                            chunk = delta.get('content', '')
                            if chunk:
                                full_text += chunk
                                on_chunk(chunk)
                        except: pass
            return full_text
        else:
            data = resp.json()
            return data['choices'][0]['message']['content']
