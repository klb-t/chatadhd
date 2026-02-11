# ChatADHD v0.5.0 - Non-Linear Context Editor

Mobile-first AI chat with graph visualization, message versioning, and full context control.

## What's New in v0.5.0

### 🌐 Web Search
- Toggle **Web** button to enable real-time web search
- Uses OpenRouter's web plugin with Exa/native search

### 🔬 Deep Research
- Toggle **Deep** for extensive search (10 results, high context)
- Automatically enables Web Search

### 🧠 Reasoning/Thinking
- Cycle through: **Auto** → Low → Med → High → MAX
- Auto = adaptive thinking (Claude 4.6)
- Shows 💭 indicator when model is thinking
- Reasoning tokens displayed in stream

### 📊 Graph Explorer
- Force-directed visualization of conversation + memory
- **Bigger nodes** with readable labels
- Drag to reposition, click to edit
- Zoom +/- and Center controls

### 📝 Message Versioning
- Edit creates new version (never lose original)
- Restore any previous version
- Weight slider (0.1-2.0) for context priority

### 📁 Memory Features
- Attach files and directories
- ZIP export for folders
- Auto-import app code to memory

## Installation (Pydroid 3)

```bash
pip install kivy requests
```

Copy `chatadhd_v0.5.0/` to `/storage/emulated/0/Download/`
Open `main.py` in Pydroid 3 and run.

## First Run

1. Tap **Cfg** → Enter OpenRouter API key
2. Tap **Ref** to load models
3. Select model by tapping model name
4. Chat!

## UI Guide

### Top Bar
`[Model] [Ref] [New] [Cfg] [Log]`

### Feature Toggles
`[Web] [Deep] [Auto] [💭]`

- **Web** - Enable web search (blue when ON)
- **Deep** - Deep research mode (orange when ON)
- **Auto** - Reasoning effort (cycles through levels)
- **💭** - Thinking indicator (shows when reasoning)

### Side Panels
- **Chats** - Conversation list
- **Mem** - Memory tree with +Text/+Folder/+File/+Dir
- **Graph** - Knowledge graph visualization

## API Features (OpenRouter)

```json
{
  "model": "anthropic/claude-4.6-opus",
  "plugins": [{"id": "web", "max_results": 10}],
  "reasoning": {"enabled": true},
  "verbosity": "max",
  "web_search_options": {"search_context_size": "high"}
}
```

## Data Location

`/storage/emulated/0/Download/chatadhd_pydroid_v0.4.6/` (uses existing data)

Or creates new in app folder.

## Themes

Settings → Theme: Dark / AMOLED

---
**ChatADHD** - Your thoughts, your context, your control.
