# ChatADHD v0.06.01 - Conversation Import

## Import Conversations 📥

Import chats from various sources into ChatADHD.

### Supported Formats

| Format | Source | Detection |
|--------|--------|-----------|
| `.db` | Claude.ai, ChatGPT SQLite | Auto-detect tables |
| `.json` | API logs, exports | messages array |
| `.html` | Browser "Save as HTML" | Class-based parsing |
| `.mht/.mhtml` | Single-file web archive | MIME extraction |
| `.md` | Markdown exports | ## Human/## Assistant |
| `.txt` | Plain text | Pattern matching |
| `.png/.jpg` | Screenshots | **OCR** (EasyOCR/pytesseract) |

### Screenshot OCR

For screenshot import, install one OCR library:

```bash
# Recommended - pure Python, works in Pydroid3
pip install easyocr

# Alternative - requires tesseract binary
pip install pytesseract
```

**Supported patterns in screenshots:**
- "Human:", "User:", "You:" → user messages
- "Claude:", "Assistant:", "AI:" → assistant messages
- Time stamps and UI indicators
- Polish: "Ja:", "Ty:"

### How to Import

1. Open **☰ Conversations** panel
2. Click **📥 Import**
3. Browse to your file
4. (Optional) Set custom title
5. Click **Import**

### JSON Format Examples

**OpenAI/Anthropic style:**
```json
{
  "messages": [
    {"role": "user", "content": "Hello"},
    {"role": "assistant", "content": "Hi!"}
  ]
}
```

**Message list:**
```json
[
  {"role": "user", "content": "Hello"},
  {"role": "assistant", "content": "Hi!"}
]
```

### Markdown Format

```markdown
## Human
Your message here

## Assistant
Response here

---

## Human
Another message
```

### Database Import

Auto-detects Claude.ai and ChatGPT SQLite schemas:
- Finds `conversations` and `messages` tables
- Falls back to any table with `role` + `content` columns

---

## Previous Features

- 📦 Collapsible messages (v0.06.00)
- 🎨 Artifact detection
- ⚡ Quick API Panel
- 📁 Full directory import
- 📊 Graph visualization

---
ChatADHD v0.06.01 - Import from anywhere
