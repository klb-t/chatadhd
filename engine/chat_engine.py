"""Main chat engine"""
import logging
from .api_client import APIClient
from .memory_engine import MemoryEngine

log = logging.getLogger('chat')

class ChatEngine:
    def __init__(self, config, secrets, db):
        self.config = config
        self.secrets = secrets
        self.db = db
        self.api = APIClient(config, secrets)
        self.memory = MemoryEngine(db)
        self.conv = None
    
    def new_conv(self, title="New Chat"):
        model = self.config.get("default_model", "anthropic/claude-sonnet-4")
        self.conv = self.db.create_conv(title, model)
        log.info(f"New conversation: {self.conv['id']}")
        return self.conv
    
    def load_conv(self, conv_id):
        self.conv = self.db.get_conv(conv_id)
        if self.conv:
            log.info(f"Loaded conversation: {conv_id}")
        return self.conv
    
    def send(self, text, attachments=None):
        if not self.conv:
            self.new_conv()
        
        # Save user message
        self.db.create_msg(
            self.conv['id'], 
            'user', 
            text, 
            attachments=attachments
        )
        
        # Update title if first message
        msgs = self.db.get_msgs(self.conv['id'])
        if len(msgs) == 1:
            title = text[:40] + ('...' if len(text) > 40 else '')
            self.db.update_conv(self.conv['id'], title=title)
            self.conv['title'] = title
        
        # Build messages for API
        api_msgs = []
        
        # Add memory context
        memory_ctx = self.memory.get_active_context()
        if memory_ctx:
            api_msgs.append({
                'role': 'system',
                'text': f"User's memory/notes:\n{memory_ctx}"
            })
        
        # Add conversation history
        for m in msgs:
            if m['status'] == 'active':
                api_msgs.append({
                    'role': m['role'],
                    'text': m['text'],
                    'attachments': m.get('attachments', [])
                })
        
        # Call API
        model = self.config.get("default_model")
        response = self.api.chat(api_msgs, model)
        
        # Save assistant response
        self.db.create_msg(self.conv['id'], 'assistant', response, model=model)
        
        return response
