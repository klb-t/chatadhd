from typing import Dict, Any, Optional, List
import logging
from .api_client import APIClient
from .memory_engine import MemoryEngine
from .prompt_engine import build_system_prompt, nodes_to_prompt_text
from .selector import SelectorEngine
from .iterative_buffer import IterativePromptBuffer

log = logging.getLogger('engine')

class ChatEngine:
    def __init__(self, config=None, secrets=None, db=None):
        self.config = config or {}
        self.secrets = secrets or {}
        self.db = db
        base_url = self.config.get('base_url', 'https://openrouter.ai/api/v1') if isinstance(self.config, dict) else self.config.get('base_url')
        api_key = self.secrets.get('api_key') if isinstance(self.secrets, dict) else self.secrets.get('api_key')
        self.api = APIClient(base_url, api_key)
        self.memory = MemoryEngine(db=self.db)
        self.selector = SelectorEngine()
        self.buffer = IterativePromptBuffer()
        self.history: List[Dict] = []
        self.conv = None
    
    def new_conv(self, title=None, preset='default'):
        self.history = []
        model = self.config.get('default_model', 'local-echo') if isinstance(self.config, dict) else self.config.get('default_model')
        if self.db:
            self.conv = self.db.create_conv(title or 'New Chat', model)
        else:
            self.conv = {'id': 'local', 'title': title or 'New Chat', 'model': model}
        return self.conv
    
    def load_conv(self, id):
        if self.db:
            self.conv = self.db.get_conv(id)
            if self.conv:
                self.history = [{'role': m['role'], 'content': m['text']} for m in self.db.get_msgs(id)]
        return self.conv
    
    def add_memory(self, content, parent=None, node_type='text', metadata=None):
        return self.memory.add_node(content, parent, node_type, metadata)
    
    def link(self, src, dst, rel='related'):
        self.memory.add_link(src, dst, rel)
    
    def get_relevant_memory(self, query, top_k=8):
        return self.selector.select_relevant(list(self.memory.all_nodes()), query, top_k)
    
    def prepare_system_prompt(self, query, top_k=8, instructions=None):
        selected = self.get_relevant_memory(query, top_k)
        mem_text = nodes_to_prompt_text(selected)
        agg = self.buffer.aggregate()
        if agg: mem_text = agg + "\n\n=== Memory ===\n" + mem_text
        return build_system_prompt(mem_text, instructions)
    
    def send(self, text, model=None):
        if not self.conv: self.new_conv()
        
        k = self.config.get('memory_k', 8) if isinstance(self.config, dict) else self.config.get('memory_k', 8)
        system = self.prepare_system_prompt(text, top_k=k)
        messages = [{'role': 'system', 'content': system}] + self.history + [{'role': 'user', 'content': text}]
        
        mdl = model or (self.config.get('default_model') if isinstance(self.config, dict) else self.config.get('default_model'))
        resp = self.api.chat(messages, mdl)
        
        self.history.append({'role': 'user', 'content': text})
        self.history.append({'role': 'assistant', 'content': resp})
        
        if self.db and self.conv:
            self.db.create_msg(self.conv['id'], 'user', text)
            self.db.create_msg(self.conv['id'], 'assistant', resp, model=mdl)
        
        return resp
