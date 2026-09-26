"""
ChatADHD v0.07.01 - Universal Conversation Importer

Imports conversations from virtually any chatbot export format:
- ZIP archives (bulk import — walks contents recursively)
- SQLite DB (Claude.ai, ChatGPT exports)
- JSON (OpenAI conversations.json, Anthropic, generic API logs,
        Claude shared links, single or multi-conversation)
- HTML (web exports, saved pages)
- MHT/MHTML (single-file web archives)
- Markdown (.md chat logs)
- Plain text (.txt)
- Screenshot (OCR via basic pattern matching)

Design: maximally universal.  Every format handler tries multiple
heuristics and falls back gracefully.  ZIP ingestion unpacks and
recurses into each file.
"""
import os
import re
import io
import json
import sqlite3
import base64
import zipfile
import tempfile
import shutil
from pathlib import Path
from datetime import datetime
from html.parser import HTMLParser
import logging

from engine.events import bus, MSG_CREATED, IMPORT_DONE

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
        
        if ext == '.zip':
            return 'zip'
        elif ext == '.db':
            return 'sqlite'
        elif ext == '.json':
            return 'json'
        elif ext in ('.jsonl', '.ndjson'):
            return 'jsonl'
        elif ext in ('.html', '.htm'):
            return 'html'
        elif ext in ('.mht', '.mhtml'):
            return 'mht'
        elif ext in ('.png', '.jpg', '.jpeg', '.webp'):
            return 'screenshot'
        elif ext == '.md':
            return 'markdown'
        elif ext in ('.txt', '.log'):
            return 'text'
        else:
            # Try to detect by content
            try:
                with open(path, 'rb') as f:
                    header = f.read(200)
                if header[:4] == b'PK\x03\x04':
                    return 'zip'
                if b'SQLite format' in header:
                    return 'sqlite'
                if header.lstrip()[:1] in (b'{', b'['):
                    return 'json'
                if b'<html' in header.lower() or b'<!doctype' in header.lower():
                    return 'html'
            except Exception:
                log.debug("Format detection failed for %s", path, exc_info=True)
        
        return 'unknown'
    
    def import_file(self, path, title=None):
        """Import conversation from file, auto-detecting format.
        Returns list of created conversation dicts."""
        fmt = self.detect_format(path)
        log.info("Importing %s (detected format: %s)", path, fmt)
        
        results = []
        if fmt == 'zip':
            results = self.import_zip(path, title)
        elif fmt == 'sqlite':
            results = self.import_sqlite(path, title)
        elif fmt == 'json':
            results = self.import_json(path, title)
        elif fmt == 'jsonl':
            results = self.import_jsonl(path, title)
        elif fmt == 'html':
            results = self.import_html(path, title)
        elif fmt == 'mht':
            results = self.import_mht(path, title)
        elif fmt == 'screenshot':
            results = self.import_screenshot(path, title)
        elif fmt == 'markdown':
            results = self.import_markdown(path, title)
        elif fmt == 'text':
            results = self.import_text(path, title)
        else:
            raise ValueError(f"Unknown format: {path}")
        
        # Flatten if needed (some handlers return single conv, some return list).
        if isinstance(results, dict):
            results = [results]
        results = [r for r in (results or []) if r is not None]
        
        if results:
            bus.emit(IMPORT_DONE, {
                "count": len(results),
                "source": str(path),
                "format": fmt,
            })
        
        return results
    
    # === ZIP ===
    def import_zip(self, path, title=None):
        """Extract ZIP and import every recognized file inside."""
        results = []
        tmpdir = tempfile.mkdtemp(prefix="chatadhd_import_")
        try:
            with zipfile.ZipFile(path, 'r') as zf:
                zf.extractall(tmpdir)
            
            # Walk extracted tree, sort for determinism.
            files = sorted(Path(tmpdir).rglob("*"))
            importable = [f for f in files if f.is_file()
                          and self.detect_format(str(f)) != 'unknown']
            
            log.info("ZIP contains %d importable files out of %d total",
                     len(importable), len(files))
            
            for fpath in importable:
                try:
                    # Use relative path inside ZIP as title hint.
                    rel = fpath.relative_to(tmpdir)
                    file_title = title or str(rel)
                    sub_results = self.import_file(str(fpath), file_title)
                    results.extend(sub_results)
                except Exception:
                    log.warning("Failed to import %s from ZIP", fpath, exc_info=True)
        finally:
            shutil.rmtree(tmpdir, ignore_errors=True)
        
        return results
    
    # === JSONL / NDJSON ===
    def import_jsonl(self, path, title=None):
        """Import newline-delimited JSON (one object per line)."""
        results = []
        with open(path, 'r', encoding='utf-8') as f:
            lines = [line.strip() for line in f if line.strip()]
        
        # Heuristic: if lines look like messages, batch into one conv.
        messages = []
        for line in lines:
            try:
                obj = json.loads(line)
                if isinstance(obj, dict) and ('role' in obj or 'content' in obj):
                    messages.append(obj)
                elif isinstance(obj, dict) and 'messages' in obj:
                    results.append(self._import_conversation_obj(obj, title))
            except json.JSONDecodeError:
                log.debug("Skipped non-JSON line in JSONL")
        
        if messages:
            results.append(self._import_message_list(messages, title))
        return results
    
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
                except Exception:
                    log.debug("Format detection failed for %s", path, exc_info=True)
        
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
        """Import from JSON file — streams large files to avoid OOM."""
        file_size = os.path.getsize(path)

        if file_size > 5_000_000:  # > 5MB → stream
            log.info("Large JSON (%d MB) — streaming import", file_size // 1_000_000)
            return self._stream_json_array(path, title)

        # Small file — load normally.
        with open(path, 'r', encoding='utf-8') as f:
            data = json.load(f)
        return self._import_json_data(data, title, source_path=path)

    def _stream_json_array(self, path, title=None):
        """Stream-parse a JSON array of conversations without loading all into RAM.
        Reads character by character, extracting one top-level element at a time.
        Handles ChatGPT conversations.json (500MB+) and Claude exports.
        """
        results = []
        conv_count = 0

        with open(path, 'r', encoding='utf-8', buffering=8192) as f:
            # Skip to first '['.
            ch = ''
            while ch != '[':
                ch = f.read(1)
                if not ch:
                    # Not an array — fall back to full load.
                    log.warning("Stream: not a JSON array, falling back")
                    f.seek(0)
                    data = json.load(f)
                    return self._import_json_data(data, title, source_path=path)

            # Parse elements one by one.
            for element in self._iter_json_elements(f):
                try:
                    r = self._import_single_element(element, title)
                    if r:
                        results.append(r)
                        conv_count += 1
                        if conv_count % 50 == 0:
                            log.info("Stream import: %d conversations processed", conv_count)
                except Exception:
                    log.debug("Stream: skipped one element", exc_info=True)

        log.info("Stream import complete: %d conversations from %s",
                 conv_count, os.path.basename(path))
        return results

    def _iter_json_elements(self, f):
        """Yield individual JSON objects from an open file positioned after '['.
        Tracks bracket/brace depth to find element boundaries.
        """
        buf = []
        depth = 0
        in_string = False
        escape = False

        while True:
            ch = f.read(1)
            if not ch:
                break

            if escape:
                buf.append(ch)
                escape = False
                continue

            if ch == '\\' and in_string:
                buf.append(ch)
                escape = True
                continue

            if ch == '"':
                in_string = not in_string
                buf.append(ch)
                continue

            if in_string:
                buf.append(ch)
                continue

            # Outside string.
            if ch in ('{', '['):
                depth += 1
                buf.append(ch)
            elif ch in ('}', ']'):
                if depth == 0:
                    # End of top-level array.
                    break
                depth -= 1
                buf.append(ch)
                if depth == 0:
                    # Complete element.
                    raw = ''.join(buf).strip()
                    buf.clear()
                    if raw:
                        try:
                            yield json.loads(raw)
                        except json.JSONDecodeError:
                            log.debug("Stream: invalid JSON element (len=%d)", len(raw))
            elif ch == ',' and depth == 0:
                # Separator between elements — skip.
                raw = ''.join(buf).strip()
                buf.clear()
                if raw:
                    try:
                        yield json.loads(raw)
                    except json.JSONDecodeError:
                        pass
            else:
                if depth > 0 or ch.strip():
                    buf.append(ch)

    def _import_single_element(self, element, title=None):
        """Import a single conversation element (from streaming or normal)."""
        if not isinstance(element, dict):
            return None
        if 'mapping' in element:
            return self._import_chatgpt_mapping(element, title)
        elif 'chat_messages' in element:
            return self._import_claude_export(element, title)
        elif 'messages' in element:
            return self._import_conversation_obj(element, title)
        else:
            return self._import_conversation_obj(element, title)
    
    def _import_json_data(self, data, title=None, source_path=None):
        """Route parsed JSON to the right handler."""
        results = []
        
        if isinstance(data, list):
            if not data:
                return results
            first = data[0]
            if isinstance(first, dict):
                if 'mapping' in first:
                    # ChatGPT conversations.json (list of convs with mapping trees)
                    for conv_data in data:
                        r = self._import_chatgpt_mapping(conv_data, title)
                        if r:
                            results.append(r)
                elif 'role' in first or 'content' in first:
                    # Direct message list
                    results.append(self._import_message_list(data, title))
                elif 'messages' in first:
                    # List of conversation objects
                    for conv_data in data:
                        results.append(self._import_conversation_obj(conv_data, title))
                elif 'chat_messages' in first:
                    # Claude.ai export format
                    for conv_data in data:
                        r = self._import_claude_export(conv_data, title)
                        if r:
                            results.append(r)
                else:
                    # Unknown list format — try each as conversation
                    for item in data:
                        try:
                            r = self._import_conversation_obj(item, title)
                            if r:
                                results.append(r)
                        except Exception:
                            log.debug("Skipped unrecognized list item")
        
        elif isinstance(data, dict):
            if 'mapping' in data:
                # Single ChatGPT conversation with mapping
                r = self._import_chatgpt_mapping(data, title)
                if r:
                    results.append(r)
            elif 'chat_messages' in data:
                # Single Claude.ai conversation
                r = self._import_claude_export(data, title)
                if r:
                    results.append(r)
            elif 'messages' in data:
                results.append(self._import_conversation_obj(data, title))
            elif 'conversations' in data:
                for conv_data in data['conversations']:
                    results.append(self._import_conversation_obj(conv_data, title))
            elif 'role' in data:
                results.append(self._import_message_list([data], title))
            elif 'data' in data and isinstance(data['data'], list):
                # Wrapped export: {"data": [...conversations...]}
                return self._import_json_data(data['data'], title, source_path)
            else:
                # Last resort: try as conversation
                try:
                    results.append(self._import_conversation_obj(data, title))
                except Exception:
                    log.warning("Unrecognized JSON structure in %s", source_path)
        
        return results
    
    def _import_chatgpt_mapping(self, conv_data, title=None):
        """Import ChatGPT conversation with tree/mapping structure.
        ChatGPT exports conversations as a tree where each node has
        an ID, parent, and message content."""
        conv_title = title or conv_data.get('title', f"ChatGPT Import {datetime.now():%H:%M}")
        mapping = conv_data.get('mapping', {})
        
        if not mapping:
            return None
        
        # Build ordered message list by walking the tree.
        # Find root (node with no parent or parent not in mapping).
        children_of = {}
        for node_id, node in mapping.items():
            parent = node.get('parent')
            children_of.setdefault(parent, []).append(node_id)
        
        # Walk from root down the "active" path.
        messages = []
        
        def walk(node_id):
            node = mapping.get(node_id, {})
            msg = node.get('message')
            if msg:
                author = msg.get('author', {}).get('role', 'unknown')
                content = msg.get('content', {})
                
                # Content can be: {"parts": ["text"]} or {"content_type": "text", "parts": [...]}
                text = ""
                if isinstance(content, dict):
                    parts = content.get('parts', [])
                    text_parts = []
                    for part in parts:
                        if isinstance(part, str):
                            text_parts.append(part)
                        elif isinstance(part, dict) and part.get('content_type') == 'text':
                            text_parts.append(part.get('text', ''))
                    text = '\n'.join(text_parts)
                elif isinstance(content, str):
                    text = content
                
                if text.strip() and author in ('user', 'assistant'):
                    messages.append({'role': author, 'content': text})
            
            for child_id in children_of.get(node_id, []):
                walk(child_id)
        
        # Find roots (nodes whose parent is None or not in mapping).
        roots = [nid for nid, node in mapping.items()
                 if node.get('parent') is None or node.get('parent') not in mapping]
        for root in roots:
            walk(root)
        
        if not messages:
            return None
        
        return self._import_message_list(messages, conv_title)
    
    def _import_claude_export(self, conv_data, title=None):
        """Import Claude.ai export format (chat_messages array)."""
        conv_title = title or conv_data.get('name',
                         conv_data.get('title', f"Claude Import {datetime.now():%H:%M}"))
        messages = []
        for msg in conv_data.get('chat_messages', []):
            sender = msg.get('sender', 'human')
            role = 'user' if sender == 'human' else 'assistant'
            text = msg.get('text', '')
            
            # Claude exports can have content blocks.
            if not text and 'content' in msg:
                content = msg['content']
                if isinstance(content, list):
                    text = '\n'.join(
                        c.get('text', '') for c in content
                        if isinstance(c, dict) and c.get('type') == 'text'
                    )
                elif isinstance(content, str):
                    text = content
            
            if text.strip():
                messages.append({'role': role, 'content': text})
        
        if not messages:
            return None
        return self._import_message_list(messages, conv_title)
    
    def _import_message_list(self, messages, title):
        """Import list of message objects using batch DB inserts.
        Core method — all importers converge here.
        """
        conv_title = title or f"Import {datetime.now():%Y-%m-%d %H:%M}"
        new_conv = self.db.create_conv(f"[Import] {conv_title}")

        # Normalise messages into batch format.
        batch = []
        for msg in messages:
            role = msg.get('role', msg.get('sender', 'user'))
            if role in ('system',):
                continue

            role = 'user' if role in ('user', 'human') else 'assistant'
            content = msg.get('content', msg.get('text', ''))

            # Handle content that might be a list (OpenAI format).
            if isinstance(content, list):
                text_parts = []
                for part in content:
                    if isinstance(part, dict):
                        if part.get('type') == 'text':
                            text_parts.append(part.get('text', ''))
                        elif part.get('type') == 'image_url':
                            text_parts.append('[image]')
                    elif isinstance(part, str):
                        text_parts.append(part)
                content = '\n'.join(text_parts)

            if not isinstance(content, str):
                content = str(content) if content else ''

            if content.strip():
                batch.append({"role": role, "text": content})

        # Batch insert (1000 per transaction).
        count = self.db.batch_create_msgs(new_conv['id'], batch)

        # Single event for the whole import (not per message).
        bus.emit(IMPORT_DONE, {
            "conv_id": new_conv['id'],
            "count": count,
            "title": conv_title,
        })

        log.info("Imported %d messages into '%s'", count, conv_title)
        return new_conv
    
    def _import_conversation_obj(self, conv_data, title):
        """Import conversation object with messages. Handles multiple key names."""
        if not isinstance(conv_data, dict):
            return None
        
        conv_title = title or conv_data.get('title',
                         conv_data.get('name',
                         conv_data.get('conversation_name',
                         f"Import {datetime.now():%H:%M}")))
        
        # Try multiple message keys.
        messages = (conv_data.get('messages')
                    or conv_data.get('chat_messages')
                    or conv_data.get('items')
                    or conv_data.get('data')
                    or [])
        
        # Handle ChatGPT's nested mapping format.
        if isinstance(messages, dict):
            messages = list(messages.values())
        
        if not messages:
            return None
        
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
            return self._import_message_list(messages, title or (Path(source_path).stem if source_path else None))
        
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
                    except Exception:
                        log.debug("Format detection failed for %s", path, exc_info=True)
        
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
        
        # Fallback: Try ocr.space API (via providers)
        try:
            from engine.providers import ProviderManager
            providers = ProviderManager(self.engine.secrets if hasattr(self.engine, 'secrets') else None)
            
            if providers.get_ocr_providers():
                result = providers.ocr(path)
                if result.text:
                    return result.text
        except Exception as e:
            log.warning(f"ocr.space failed: {e}")
        
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
