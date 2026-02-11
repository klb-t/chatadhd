# ChatADHD v0.06.02 - Full API Control + GitHub Sync

## New: API Request Editor 📝

**Full control over every aspect of the API request.**

### Tabs:
| Tab | Content |
|-----|---------|
| **System** | System prompt (editable, templates) |
| **Memory** | Memory context sent to API |
| **Msgs** | All messages with include/exclude toggle |
| **Params** | Temperature, max_tokens, top_p, penalties |

### Features:
- Edit system prompt with quick templates
- View/edit full memory context
- Toggle individual messages on/off
- Adjust message weights (0.1-2.0)
- Add new user messages
- "Last 5" quick filter
- Live token count estimate
- Copy full JSON request
- Send directly

### Access:
- Tap **📝** button in top bar
- Or long-press **Send** for quick preview

---

## New: GitHub Sync 🔄

**Bidirectional synchronization with GitHub repositories.**

### Setup:
1. **Settings** → Add `github_token` (ghp_...)
2. Tap **🔄** → **+New** to add repository
3. Enter: `owner/repo`, local path, branch

### Sync Modes:
| Mode | Description |
|------|-------------|
| **↔️ Both** | Pull new remote, push new local |
| **⬇️ Pull** | Only download from GitHub |
| **⬆️ Push** | Only upload to GitHub |

### File Status:
| Icon | Meaning |
|------|---------|
| ✓ | Synced |
| ~ | Modified (conflict) |
| +L | New local file |
| +R | New remote file |

### Actions:
- **⬇️ Pull** - Download selected files
- **⬆️ Push** - Upload selected files  
- **🔄 Sync** - Bidirectional sync
- Select/deselect individual files

---

## API Keys (Settings)

| Key | Service | Purpose |
|-----|---------|---------|
| `api_key` | OpenRouter | LLM chat |
| `groq_api_key` | Groq | Voice input (ASR) |
| `ocr_space_api_key` | OCR.space | Screenshot import |
| `github_token` | GitHub | Repository sync |

---

## File Structure

```
engine/
  github_sync.py  (411)  ← NEW: GitHub sync
  providers.py    (467)  Provider abstraction
  importer.py     (693)  Conversation import
  chat_engine.py  (272)
  db.py           (274)
gui/
  panels.py      (2751)  ← +APIRequestEditor, +GitHubSyncPopup
  graph_viz.py    (384)
main.py           (152)
─────────────────────────
Total: 5698 lines
```

---

## Top Bar

```
[Model▾] [⚙️] [📝] [🔄] [New] [Cfg]
         │    │    │
         │    │    └─ GitHub Sync
         │    └─ API Request Editor  
         └─ Quick API Panel
```

---
ChatADHD v0.06.02 - Full control over your AI conversations
