
from typing import Dict, Any, Optional, List
import logging
from .api_client import APIClient
from .memory_engine import MemoryEngine
from .prompt_engine import build_system_prompt, nodes_to_prompt_text
from .selector import SelectorEngine
from .iterative_buffer import IterativePromptBuffer

log = logging.getLogger('chat_engine')

class ChatEngine:
    def __init__(self, config: Dict[str,Any]=None, secrets: Dict[str,Any]=None, db=None):
        self.config = config or {}
        self.secrets = secrets or {}
        self.db = db
        self.api = APIClient(self.config.get('base_url','https://openrouter.ai/api/v1'), self.secrets.get('api_key'))
        self.memory = MemoryEngine(db=self.db)
        self.selector = SelectorEngine()
        self.buffer = IterativePromptBuffer()
        self.history: List[Dict[str,str]] = []

    def new_conv(self, title: str=None):
        self.history = []
        return {'id': 'local-'+(title or 'conv')}

    def add_memory(self, content: str, parent: Optional[str]=None, node_type: str='text', metadata: Dict=None):
        return self.memory.add_node(content, parent, node_type, metadata)

    def link(self, src: str, dst: str, rel: str='related'):
        self.memory.add_link(src,dst,rel)

    def get_relevant_memory(self, query: str, top_k: int=8):
        nodes = list(self.memory.all_nodes())
        selected = self.selector.select_relevant(nodes, query, top_n=top_k)
        return selected

    def prepare_system_prompt(self, query: str, top_k: int=8, instructions: str=None):
        selected = self.get_relevant_memory(query, top_k)
        mem_text = nodes_to_prompt_text(selected)
        agg = self.buffer.aggregate()
        if agg:
            mem_text = agg + "\n\n=== Memory Selection ===\n" + mem_text
        return build_system_prompt(mem_text, instructions)

    def send(self, user_text: str, model: str=None):
        system_prompt = self.prepare_system_prompt(user_text, top_k=self.config.get('memory_k',8))
        messages = self.history + [{'role':'user','content': user_text}]
        resp = self.api.chat(system_prompt, messages, model or self.config.get('default_model','local-echo'))
        self.history.append({'role':'user','content': user_text})
        self.history.append({'role':'assistant','content': resp})
        return resp
