"""
ChatADHD v0.5.0 - Chat Engine with all OpenRouter features
- Adaptive thinking (Claude 4.6)
- Reasoning tokens display
- Web search (:online)
- Extended thinking
- Verbosity control
"""
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
        self.last_reasoning = None  # Store reasoning tokens
        
        # Load last conversation
        convs = self.db.list_convs()
        if convs:
            self.conv = convs[0]
    
    def new_conv(self, title="New Chat"):
        self.conv = self.db.create_conv(title)
        return self.conv
    
    def load_conv(self, conv_id):
        self.conv = self.db.get_conv(conv_id)
        return self.conv
    
    def send(self, text, attachments=None, on_chunk=None, on_reasoning=None,
             web_search=False, deep_research=False, reasoning_effort=None):
        """
        Send message with all OpenRouter features.
        
        Args:
            text: User message
            attachments: List of file paths
            on_chunk: Callback for streaming response chunks
            on_reasoning: Callback for reasoning/thinking tokens
            web_search: Enable web search (:online)
            deep_research: Enable deep research mode (high context search)
            reasoning_effort: None (adaptive), 'low', 'medium', 'high', 'max'
        """
        if not self.conv:
            self.new_conv()
        
        # Save user message
        user_mid = self.db.create_msg(self.conv['id'], text, 'user', attachments=attachments)
        
        # Build API request
        messages = self._build_messages(text, attachments)
        model = self.config.get('default_model')
        
        # Call API
        response_text, reasoning = self._call_api(
            messages, model, on_chunk, on_reasoning,
            web_search, deep_research, reasoning_effort
        )
        
        # Store reasoning if present
        self.last_reasoning = reasoning
        
        # Save assistant message with reasoning in metadata
        metadata = {}
        if reasoning:
            metadata['reasoning'] = reasoning[:2000]  # Truncate for storage
        
        self.db.create_msg(self.conv['id'], response_text, 'assistant', 
                          model=model, metadata=metadata)
        
        # Update conversation title
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
        if not attachments:
            return text
        
        content = [{"type": "text", "text": text}]
        
        for path in attachments:
            p = Path(path)
            if not p.exists():
                continue
            
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
            
            elif p.suffix.lower() in ('.txt', '.py', '.md', '.json', '.csv', '.xml', '.html', '.css', '.js'):
                try:
                    file_text = p.read_text(errors='replace')[:15000]
                    content.append({
                        "type": "text",
                        "text": f"\n--- FILE: {p.name} ---\n{file_text}\n--- END FILE ---"
                    })
                except: pass
        
        return content
    
    def _call_api(self, messages, model, on_chunk=None, on_reasoning=None,
                  web_search=False, deep_research=False, reasoning_effort=None):
        """Call OpenRouter API with all features."""
        key = self.secrets.get('api_key')
        if not key:
            raise ValueError("No API key configured")
        
        url = self.config.get('base_url', '').rstrip('/') + '/chat/completions'
        headers = {
            'Authorization': f'Bearer {key}',
            'Content-Type': 'application/json',
            'HTTP-Referer': 'https://github.com/chatadhd',
            'X-Title': 'ChatADHD',
        }
        
        stream = on_chunk is not None
        
        # Build payload
        payload = {
            'model': model,
            'messages': messages,
            'temperature': self.config.get('temperature', 0.7),
            'max_tokens': self.config.get('max_tokens', 4096),
            'stream': stream,
        }
        
        # === WEB SEARCH ===
        if web_search or deep_research:
            plugins = [{"id": "web"}]
            if deep_research:
                plugins[0]["max_results"] = 10
                payload["web_search_options"] = {"search_context_size": "high"}
            payload["plugins"] = plugins
        
        # === REASONING / EXTENDED THINKING ===
        # Check if model supports reasoning
        is_claude_46 = '4.6' in model or '4-6' in model
        is_thinking_model = any(x in model.lower() for x in ['opus', 'sonnet', 'o1', 'o3', 'r1', 'thinking', 'deepseek'])
        
        if is_thinking_model:
            reasoning_config = {"enabled": True}
            
            if is_claude_46:
                # Claude 4.6: Use adaptive thinking by default
                # Only set max_tokens if user wants budget-based
                if reasoning_effort == 'max':
                    payload["verbosity"] = "max"
                elif reasoning_effort and reasoning_effort != 'adaptive':
                    # Force budget-based for specific effort
                    effort_map = {'low': 5000, 'medium': 15000, 'high': 30000}
                    reasoning_config["max_tokens"] = effort_map.get(reasoning_effort, 15000)
            else:
                # Other models: use effort parameter
                if reasoning_effort:
                    reasoning_config["effort"] = reasoning_effort
            
            payload["reasoning"] = reasoning_config
        
        # Request reasoning in response
        payload["include_reasoning"] = True
        
        resp = requests.post(url, headers=headers, json=payload, stream=stream, timeout=180)
        
        if resp.status_code != 200:
            raise ValueError(f"API error {resp.status_code}: {resp.text[:300]}")
        
        full_text = ""
        reasoning_text = ""
        
        if stream:
            for line in resp.iter_lines():
                if line:
                    line = line.decode('utf-8')
                    if line.startswith('data: '):
                        data_str = line[6:]
                        if data_str.strip() == '[DONE]':
                            break
                        try:
                            data = json.loads(data_str)
                            choice = data.get('choices', [{}])[0]
                            delta = choice.get('delta', {})
                            
                            # Regular content
                            chunk = delta.get('content', '')
                            if chunk:
                                full_text += chunk
                                if on_chunk:
                                    on_chunk(chunk)
                            
                            # Reasoning tokens
                            reasoning = delta.get('reasoning', '')
                            if reasoning:
                                reasoning_text += reasoning
                                if on_reasoning:
                                    on_reasoning(reasoning)
                            
                            # Reasoning details (for some models)
                            if 'reasoning_details' in choice:
                                for detail in choice['reasoning_details']:
                                    if detail.get('type') == 'text':
                                        r = detail.get('text', '')
                                        reasoning_text += r
                                        if on_reasoning:
                                            on_reasoning(r)
                        except: pass
        else:
            data = resp.json()
            choice = data['choices'][0]
            full_text = choice['message']['content']
            
            # Extract reasoning
            if 'reasoning' in choice['message']:
                reasoning_text = choice['message']['reasoning']
            if 'reasoning_details' in choice:
                for detail in choice['reasoning_details']:
                    if detail.get('type') == 'text':
                        reasoning_text += detail.get('text', '')
        
        return full_text, reasoning_text if reasoning_text else None
    
    def get_last_reasoning(self):
        """Get reasoning tokens from last response."""
        return self.last_reasoning
