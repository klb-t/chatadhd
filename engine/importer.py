"""
ChatADHD v0.06.01 - Conversation Importer
Imports conversations from various formats:
- SQLite DB (Claude.ai, ChatGPT exports)
- JSON (OpenAI, Anthropic API logs)
- HTML (web exports)
- MHT (single-file web archives)
- Screenshot (OCR via basic pattern matching)
- Markdown
- Plain text
"""
import os
import re
import json
import sqlite3
import base64
from pathlib import Path
from datetime import datetime
from html.parser import HTMLParser
import logging

log = logging.getLogger('importer')


class ConversationImporter:
    """Import conversations from various sources."""
    
    def __init__(self, db, engine):
        self.db = db
        self.engine = engine
    
    def detect_format(self, path):
        """Auto-detect file format."""
        p = Path(path)
        ext = p.suffix.lower()
        
        if ext == '.db':
            return 'sqlite'
        elif ext == '.json':
            return 'json'
        elif ext == '.html' or ext == '.htm':
            return 'html'
        elif ext == '.mht' or ext == '.mhtml':
            return 'mht'
        elif ext in ('.png', '.jpg', '.jpeg', '.webp'):
            return 'screenshot'
        elif ext == '.md':
            return 'markdown'
        elif ext == '.txt':
            return 'text'
        else:
            # Try to detect by content
            try:
                with open(path, 'rb') as f:
                    header = f.read(100)
                if b'SQLite format' in header:
                    return 'sqlite'
                if header.startswith(b'{') or header.startswith(b'['):
                    return 'json'
                if b'<html' in header.lower() or b'<!doctype' in header.lower():
                    return 'html'
            except:
                pass
        
        return 'unknown'
    
    def import_file(self, path, title=None):
        """Import conversation from file, auto-detecting format."""
        fmt = self.detect_format(path)
        
        if fmt == 'sqlite':
            return self.import_sqlite(path, title)
        elif fmt == 'json':
            return self.import_json(path, title)
        elif fmt == 'html':
            return self.import_html(path, title)
        elif fmt == 'mht':
            return self.import_mht(path, title)
        elif fmt == 'screenshot':
            return self.import_screenshot(path, title)
        elif fmt == 'markdown':
            return self.import_markdown(path, title)
        elif fmt == 'text':
            return self.import_text(path, title)
        else:
            raise ValueError(f"Unknown format: {path}")
    
    # === SQLITE ===
    def import_sqlite(self, path, title=None):
        """Import from SQLite database (Claude.ai, ChatGPT, etc)."""
        results = []
        
        conn = sqlite3.connect(path)
        conn.row_factory = sqlite3.Row
        cur = conn.cursor()
        
        # Try to find conversation tables
        cur.execute("SELECT name FROM sqlite_master WHERE type='table'")
        tables = [r[0] for r in cur.fetchall()]
        
        # Claude.ai format
        if 'conversations' in tables and 'messages' in tables:
            results.extend(self._import_claude_db(cur, title))
        
        # ChatGPT format
        elif 'conversation' in tables or 'message' in tables:
            results.extend(self._import_chatgpt_db(cur, title))
        
        # Generic: look for any table with 'role' and 'content' columns
        else:
            for table in tables:
                try:
                    cur.execute(f"PRAGMA table_info({table})")
                    cols = [r[1] for r in cur.fetchall()]
                    if 'role' in cols and ('content' in cols or 'text' in cols):
                        results.extend(self._import_generic_db(cur, table, title))
                except:
                    pass
        
        conn.close()
        return results
    
    def _import_claude_db(self, cur, title):
        """Import Claude.ai database format."""
        results = []
        
        cur.execute("SELECT id, title, created_at FROM conversations ORDER BY created_at DESC")
        convs = cur.fetchall()
        
        for conv_row in convs:
            conv_id = conv_row['id']
            conv_title = title or conv_row['title'] or f"Imported {datetime.now():%Y-%m-%d}"
            
            # Create new conversation
            new_conv = self.db.create_conv(f"[Import] {conv_title}")
            
            # Get messages
            cur.execute("""
                SELECT role, content, created_at 
                FROM messages 
                WHERE conversation_id = ? 
                ORDER BY created_at
            """, (conv_id,))
            
            for msg in cur.fetchall():
                role = 'user' if msg['role'] in ('user', 'human') else 'assistant'
                self.db.create_msg(new_conv['id'], msg['content'], role)
            
            results.append(new_conv)
        
        return results
    
    def _import_chatgpt_db(self, cur, title):
        """Import ChatGPT database format."""
        results = []
        # Similar structure, adapt as needed
        return results
    
    def _import_generic_db(self, cur, table, title):
        """Import from generic table with role/content columns."""
        results = []
        
        cur.execute(f"PRAGMA table_info({table})")
        cols = [r[1] for r in cur.fetchall()]
        
        content_col = 'content' if 'content' in cols else 'text'
        
        conv_title = title or f"Import from {table}"
        new_conv = self.db.create_conv(f"[Import] {conv_title}")
        
        cur.execute(f"SELECT role, {content_col} FROM {table}")
        for row in cur.fetchall():
            role = 'user' if row[0] in ('user', 'human') else 'assistant'
            self.db.create_msg(new_conv['id'], row[1], role)
        
        results.append(new_conv)
        return results
    
    # === JSON ===
    def import_json(self, path, title=None):
        """Import from JSON file (API logs, exports)."""
        with open(path, 'r', encoding='utf-8') as f:
            data = json.load(f)
        
        results = []
        
        # Handle different JSON structures
        if isinstance(data, list):
            # List of messages or conversations
            if data and isinstance(data[0], dict):
                if 'role' in data[0]:
                    # Direct message list
                    results.append(self._import_message_list(data, title))
                elif 'messages' in data[0]:
                    # List of conversations
                    for conv_data in data:
                        results.append(self._import_conversation_obj(conv_data, title))
        
        elif isinstance(data, dict):
            if 'messages' in data:
                # Single conversation
                results.append(self._import_conversation_obj(data, title))
            elif 'conversations' in data:
                # Export with multiple conversations
                for conv_data in data['conversations']:
                    results.append(self._import_conversation_obj(conv_data, title))
            elif 'role' in data:
                # Single message? Wrap in list
                results.append(self._import_message_list([data], title))
        
        return results
    
    def _import_message_list(self, messages, title):
        """Import list of message objects."""
        conv_title = title or f"Import {datetime.now():%Y-%m-%d %H:%M}"
        new_conv = self.db.create_conv(f"[Import] {conv_title}")
        
        for msg in messages:
            role = msg.get('role', 'user')
            if role in ('system',):
                continue  # Skip system messages or import as metadata
            
            role = 'user' if role in ('user', 'human') else 'assistant'
            content = msg.get('content', msg.get('text', ''))
            
            # Handle content that might be a list (OpenAI format)
            if isinstance(content, list):
                text_parts = []
                for part in content:
                    if isinstance(part, dict) and part.get('type') == 'text':
                        text_parts.append(part.get('text', ''))
                    elif isinstance(part, str):
                        text_parts.append(part)
                content = '\n'.join(text_parts)
            
            if content:
                self.db.create_msg(new_conv['id'], content, role)
        
        return new_conv
    
    def _import_conversation_obj(self, conv_data, title):
        """Import conversation object with messages."""
        conv_title = title or conv_data.get('title', conv_data.get('name', f"Import {datetime.now():%H:%M}"))
        messages = conv_data.get('messages', conv_data.get('mapping', {}).values())
        
        # Handle ChatGPT's nested mapping format
        if isinstance(messages, dict):
            messages = list(messages)
        
        return self._import_message_list(list(messages), conv_title)
    
    # === HTML ===
    def import_html(self, path, title=None):
        """Import from HTML file (web page save)."""
        with open(path, 'r', encoding='utf-8', errors='replace') as f:
            html = f.read()
        
        return self._parse_html_conversation(html, title, path)
    
    def _parse_html_conversation(self, html, title, source_path=None):
        """Parse conversation from HTML content."""
        messages = []
        
        # Try different patterns for different chat UIs
        
        # Pattern 1: Claude.ai - look for data attributes or specific classes
        claude_pattern = r'<div[^>]*(?:data-role|class)[^>]*(?:human|user|assistant)[^>]*>(.*?)</div>'
        
        # Pattern 2: ChatGPT - markdown-body or message classes
        chatgpt_user = r'<div[^>]*class="[^"]*(?:user-message|human)[^"]*"[^>]*>(.*?)</div>'
        chatgpt_ai = r'<div[^>]*class="[^"]*(?:assistant|ai-message|markdown)[^"]*"[^>]*>(.*?)</div>'
        
        # Pattern 3: Generic alternating pattern
        # Look for repeated structures
        
        # Simple approach: extract all text blocks and try to identify speakers
        parser = ConversationHTMLParser()
        parser.feed(html)
        
        if parser.messages:
            messages = parser.messages
        else:
            # Fallback: regex-based extraction
            messages = self._extract_messages_regex(html)
        
        if not messages:
            # Last resort: treat as single block
            text = self._strip_html(html)
            if text:
                messages = [{'role': 'assistant', 'content': text[:50000]}]
        
        if messages:
            return self._import_message_list(messages, title or Path(source_path).stem if source_path else None)
        
        return None
    
    def _extract_messages_regex(self, html):
        """Extract messages using regex patterns."""
        messages = []
        
        # Remove scripts and styles
        html = re.sub(r'<script[^>]*>.*?</script>', '', html, flags=re.DOTALL | re.IGNORECASE)
        html = re.sub(r'<style[^>]*>.*?</style>', '', html, flags=re.DOTALL | re.IGNORECASE)
        
        # Look for common patterns
        patterns = [
            # Human/Assistant labels
            (r'(?:Human|User|You):\s*(.*?)(?=(?:Assistant|AI|Claude|ChatGPT):|$)', 'user'),
            (r'(?:Assistant|AI|Claude|ChatGPT):\s*(.*?)(?=(?:Human|User|You):|$)', 'assistant'),
        ]
        
        for pattern, role in patterns:
            for match in re.finditer(pattern, html, re.DOTALL | re.IGNORECASE):
                text = self._strip_html(match.group(1))
                if text and len(text) > 10:
                    messages.append({'role': role, 'content': text})
        
        # Sort by position in original text
        return messages
    
    def _strip_html(self, html):
        """Remove HTML tags and clean up text."""
        # Remove tags
        text = re.sub(r'<[^>]+>', ' ', html)
        # Decode entities
        text = text.replace('&nbsp;', ' ')
        text = text.replace('&lt;', '<')
        text = text.replace('&gt;', '>')
        text = text.replace('&amp;', '&')
        text = text.replace('&quot;', '"')
        # Clean whitespace
        text = re.sub(r'\s+', ' ', text)
        return text.strip()
    
    # === MHT ===
    def import_mht(self, path, title=None):
        """Import from MHT/MHTML file (single-file web archive)."""
        with open(path, 'rb') as f:
            content = f.read()
        
        # MHT is MIME multipart - extract HTML part
        html = None
        
        # Simple extraction - find HTML content
        parts = content.split(b'------=_')
        for part in parts:
            if b'text/html' in part[:500]:
                # Find the actual HTML
                html_start = part.find(b'<html')
                if html_start == -1:
                    html_start = part.find(b'<HTML')
                if html_start == -1:
                    html_start = part.find(b'<!DOCTYPE')
                
                if html_start != -1:
                    html = part[html_start:].decode('utf-8', errors='replace')
                    break
                
                # Try base64 decoded
                if b'base64' in part[:200]:
                    try:
                        b64_start = part.find(b'\r\n\r\n')
                        if b64_start != -1:
                            b64_data = part[b64_start:].strip()
                            html = base64.b64decode(b64_data).decode('utf-8', errors='replace')
                            break
                    except:
                        pass
        
        if html:
            return self._parse_html_conversation(html, title, path)
        
        return None
    
    # === SCREENSHOT ===
    def import_screenshot(self, path, title=None):
        """Import from screenshot using OCR."""
        text = self._ocr_image(path)
        
        if not text or len(text.strip()) < 10:
            raise ValueError("Could not extract text from image. Try a clearer screenshot.")
        
        # Parse extracted text into conversation
        messages = self._parse_chat_text(text)
        
        if not messages:
            # If no conversation pattern found, import as single block
            messages = [{'role': 'assistant', 'content': text}]
        
        conv_title = title or f"Screenshot {datetime.now():%Y-%m-%d %H:%M}"
        return self._import_message_list(messages, conv_title)
    
    def _ocr_image(self, path):
        """Extract text from image using available OCR method."""
        
        # Try EasyOCR first (best quality, pure Python)
        try:
            import easyocr
            reader = easyocr.Reader(['en', 'pl'], gpu=False)
            results = reader.readtext(path)
            
            # Sort by vertical position (top to bottom)
            results.sort(key=lambda x: x[0][0][1])
            
            lines = []
            current_y = -100
            current_line = []
            
            for bbox, text, conf in results:
                y = bbox[0][1]  # Top-left Y coordinate
                
                # New line if Y changed significantly
                if y - current_y > 20:
                    if current_line:
                        lines.append(' '.join(current_line))
                    current_line = [text]
                    current_y = y
                else:
                    current_line.append(text)
            
            if current_line:
                lines.append(' '.join(current_line))
            
            return '\n'.join(lines)
            
        except ImportError:
            pass
        
        # Try pytesseract
        try:
            import pytesseract
            from PIL import Image
            
            img = Image.open(path)
            
            # Preprocess for better OCR
            img = img.convert('L')  # Grayscale
            
            text = pytesseract.image_to_string(img, lang='eng+pol')
            return text
            
        except ImportError:
            pass
        except Exception as e:
            log.warning(f"pytesseract failed: {e}")
        
        # Fallback: Try with PIL only for basic info
        try:
            from PIL import Image
            img = Image.open(path)
            
            # Can't do real OCR without libraries
            # Raise helpful error
            raise ImportError(
                "No OCR library available. Install one:\n"
                "pip install easyocr  (recommended)\n"
                "or: pip install pytesseract (requires tesseract-ocr)"
            )
            
        except ImportError as e:
            raise ValueError(str(e))
    
    def _parse_chat_text(self, text):
        """Parse OCR'd text into conversation messages."""
        messages = []
        lines = text.split('\n')
        
        current_role = None
        current_text = []
        
        # Patterns for different chat apps
        user_patterns = [
            r'^(You|Human|User|Ja|Ty)[\s:]+',
            r'^[►▶→>]\s*',  # Arrow indicators
            r'^\d{1,2}:\d{2}\s*(AM|PM)?\s*$',  # Time only (often user msg)
        ]
        
        ai_patterns = [
            r'^(Claude|Assistant|AI|ChatGPT|GPT|Gemini|Bot)[\s:]+',
            r'^[◄◀←<]\s*',  # Arrow indicators
        ]
        
        # Claude.ai specific patterns
        claude_human = r'H\s*$|Human\s*$'
        claude_ai = r'A\s*$|Assistant\s*$|Claude\s*$'
        
        for line in lines:
            line = line.strip()
            if not line:
                continue
            
            # Check for role indicators
            is_user = any(re.match(p, line, re.IGNORECASE) for p in user_patterns)
            is_ai = any(re.match(p, line, re.IGNORECASE) for p in ai_patterns)
            
            # Claude.ai sidebar detection
            if re.match(claude_human, line):
                is_user = True
            elif re.match(claude_ai, line):
                is_ai = True
            
            if is_user or is_ai:
                # Save previous message
                if current_role and current_text:
                    content = '\n'.join(current_text).strip()
                    if content and len(content) > 3:
                        messages.append({'role': current_role, 'content': content})
                
                current_role = 'user' if is_user else 'assistant'
                current_text = []
                
                # Remove the role prefix from line
                for p in user_patterns + ai_patterns:
                    line = re.sub(p, '', line, flags=re.IGNORECASE).strip()
                
                if line:
                    current_text.append(line)
            else:
                if current_role:
                    current_text.append(line)
                else:
                    # No role yet, try to guess based on content
                    # Questions often from user, long explanations from AI
                    if line.endswith('?') or len(line) < 50:
                        current_role = 'user'
                    else:
                        current_role = 'assistant'
                    current_text.append(line)
        
        # Don't forget last message
        if current_role and current_text:
            content = '\n'.join(current_text).strip()
            if content and len(content) > 3:
                messages.append({'role': current_role, 'content': content})
        
        # Post-process: merge consecutive same-role messages
        if messages:
            merged = [messages[0]]
            for msg in messages[1:]:
                if msg['role'] == merged[-1]['role']:
                    merged[-1]['content'] += '\n\n' + msg['content']
                else:
                    merged.append(msg)
            messages = merged
        
        return messages
    
    # === MARKDOWN ===
    def import_markdown(self, path, title=None):
        """Import from Markdown file."""
        with open(path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        messages = []
        current_role = None
        current_text = []
        
        lines = content.split('\n')
        
        for line in lines:
            # Check for role headers
            lower = line.lower().strip()
            
            if lower.startswith('## human') or lower.startswith('## user') or lower.startswith('**human**') or lower.startswith('**user**'):
                if current_role and current_text:
                    messages.append({'role': current_role, 'content': '\n'.join(current_text).strip()})
                current_role = 'user'
                current_text = []
            
            elif lower.startswith('## assistant') or lower.startswith('## claude') or lower.startswith('**assistant**') or lower.startswith('**claude**'):
                if current_role and current_text:
                    messages.append({'role': current_role, 'content': '\n'.join(current_text).strip()})
                current_role = 'assistant'
                current_text = []
            
            elif lower.startswith('---') or lower.startswith('***'):
                # Separator - might indicate message boundary
                if current_role and current_text:
                    messages.append({'role': current_role, 'content': '\n'.join(current_text).strip()})
                    current_text = []
            
            else:
                if current_role:
                    current_text.append(line)
        
        # Don't forget last message
        if current_role and current_text:
            messages.append({'role': current_role, 'content': '\n'.join(current_text).strip()})
        
        # If no structure found, treat as single assistant message
        if not messages:
            messages = [{'role': 'assistant', 'content': content}]
        
        return self._import_message_list(messages, title or Path(path).stem)
    
    # === PLAIN TEXT ===
    def import_text(self, path, title=None):
        """Import from plain text file."""
        with open(path, 'r', encoding='utf-8', errors='replace') as f:
            content = f.read()
        
        messages = []
        
        # Try to detect conversation patterns
        patterns = [
            r'(?:^|\n)(Human|User|You):\s*(.*?)(?=(?:\n(?:Human|User|You|Assistant|AI|Claude):)|$)',
            r'(?:^|\n)(Assistant|AI|Claude):\s*(.*?)(?=(?:\n(?:Human|User|You|Assistant|AI|Claude):)|$)',
        ]
        
        found = False
        for pattern in patterns:
            for match in re.finditer(pattern, content, re.DOTALL | re.IGNORECASE):
                role_text = match.group(1).lower()
                role = 'user' if role_text in ('human', 'user', 'you') else 'assistant'
                text = match.group(2).strip()
                if text:
                    messages.append({'role': role, 'content': text})
                    found = True
        
        if not found:
            # No pattern found - import as single message
            messages = [{'role': 'assistant', 'content': content}]
        
        return self._import_message_list(messages, title or Path(path).stem)


class ConversationHTMLParser(HTMLParser):
    """HTML parser to extract conversation messages."""
    
    def __init__(self):
        super().__init__()
        self.messages = []
        self.current_role = None
        self.current_text = []
        self.in_message = False
        self.depth = 0
    
    def handle_starttag(self, tag, attrs):
        attrs_dict = dict(attrs)
        classes = attrs_dict.get('class', '').lower()
        data_role = attrs_dict.get('data-role', '').lower()
        
        # Detect message containers
        if 'human' in classes or 'user' in classes or data_role in ('human', 'user'):
            self._finish_message()
            self.current_role = 'user'
            self.in_message = True
            self.depth = 0
        
        elif 'assistant' in classes or 'ai' in classes or data_role in ('assistant', 'ai'):
            self._finish_message()
            self.current_role = 'assistant'
            self.in_message = True
            self.depth = 0
        
        if self.in_message:
            self.depth += 1
    
    def handle_endtag(self, tag):
        if self.in_message:
            self.depth -= 1
            if self.depth <= 0:
                self._finish_message()
    
    def handle_data(self, data):
        if self.in_message and self.current_role:
            text = data.strip()
            if text:
                self.current_text.append(text)
    
    def _finish_message(self):
        if self.current_role and self.current_text:
            content = ' '.join(self.current_text).strip()
            if content and len(content) > 5:
                self.messages.append({
                    'role': self.current_role,
                    'content': content
                })
        self.current_role = None
        self.current_text = []
        self.in_message = False
