from typing import Dict, Any, List
import logging
from .api_client import APIClient
from .memory_engine import MemoryEngine
from .selector import SelectorEngine

log = logging.getLogger('engine')

class ChatEngine:
    def __init__(self, config, secrets, db=None):
        self.config = config
        self.secrets = secrets
        self.db = db
        self.api = APIClient(config.get('base_url'), secrets.get('api_key'))
        self.memory = MemoryEngine(db=db)
        self.selector = SelectorEngine()
        self.history: List[Dict] = []
        self.conv = None
    
    def new_conv(self, title=None):
        self.history = []
        model = self.config.get('default_model')
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
    
    def build_system_prompt(self, query, top_k=8):
        selected = self.selector.select_relevant(list(self.memory.all_nodes()), query, top_k)
        if not selected:
            return "You are a helpful assistant."
        mem_text = "\n".join([f"- [{n.node_type}] {n.content}" for n in selected])
        return f"You are an assistant with access to user's memory:\n\n{mem_text}\n\nUse relevant items to help."
    
    def send(self, text, model=None):
        if not self.conv: self.new_conv()
        system = self.build_system_prompt(text, self.config.get('memory_k', 8))
        messages = [{'role': 'system', 'content': system}] + self.history + [{'role': 'user', 'content': text}]
        mdl = model or self.config.get('default_model')
        self.api.api_key = self.secrets.get('api_key')
        resp = self.api.chat(messages, mdl, self.config.get('temperature', 0.7), self.config.get('max_tokens', 4096))
        self.history.append({'role': 'user', 'content': text})
        self.history.append({'role': 'assistant', 'content': resp})
        if self.db and self.conv:
            self.db.create_msg(self.conv['id'], 'user', text)
            self.db.create_msg(self.conv['id'], 'assistant', resp, model=mdl)
            self.db.update_conv(self.conv['id'])
        return resp
