# ChatADHD v0.5.0 - Non-Linear Context Editor

Mobile-first AI chat client with hierarchical memory, graph visualization, and full context control.

## What's New in v0.5.0

### Graph Explorer
- **Force-directed visualization** of conversations and memory as interactive graph
- **Pan, zoom, drag** nodes to organize your knowledge
- **Click nodes** to edit weight, content, links
- Real-time physics simulation

### Message Versioning
- **Edit creates new version** - never lose original
- **Version history** - restore any previous version
- **Branch conversations** - explore different paths

### Weighted Context
- **Adjust node importance** with weight slider (0.1 - 2.0)
- High-weight nodes marked as `[IMPORTANT]` to model
- Low-weight nodes marked as `[low priority]`
- Affects context building for API calls

### Memory Improvements  
- **Attach files and directories** to memory nodes
- **Directory support** - add entire folders with contents
- **ZIP export** - download memory folders as archives
- Types: Text, Folder, File, Directory

## Installation (Pydroid 3)

1. Install Pydroid 3 from Play Store
2. Install pip packages:
   ```
   pip install kivy requests
   ```
3. Copy `chatadhd_v0.5.0` folder to `/storage/emulated/0/Download/`
4. Open `main.py` in Pydroid 3
5. Run!

## First Run

1. Tap **Cfg** → Enter your OpenRouter API key
2. Tap **Ref** to load available models
3. Tap model name to select different model
4. Start chatting!

## UI Guide

### Top Bar
- **Chats** - Open conversations panel
- **[Model name]** - Tap to change model
- **Ref** - Refresh model list
- **New** - New conversation
- **Cfg** - Settings (API key, theme)
- **Log** - View debug logs
- **Mem** - Memory panel
- **Graph** - Graph Explorer

### Chat
- **Long-press Send** - Preview API request JSON
- **Copy** - Copy message text
- **Hide/Show** - Exclude/include from context
- **Streaming** - Real-time response display

### Memory Panel
- **+Text** - Add text note
- **+Folder** - Add logical folder (groups items)
- **+File** - Attach file from storage
- **+Dir** - Attach entire directory
- **[+]/[-]** - Toggle node active/inactive
- **[x]** - Delete node
- **ZIP** - Download folder as archive

### Graph Explorer
- **Drag nodes** - Reposition manually
- **Click node** - Edit properties
- **+/-** buttons - Zoom in/out
- **Reset** - Reset view
- **Refresh** - Reload graph data

### Node Editor (Graph)
- **Content** - View/edit text
- **Weight** - Importance slider
- **Pin** - Lock position
- **Versions** - Switch between edits
- **Edit (new ver)** - Create new version

## Architecture

```
chatadhd_v0.5.0/
├── main.py              # App entry point
├── engine/
│   ├── config.py        # Config & secrets
│   ├── db.py            # SQLite with WAL, versioning
│   ├── chat_engine.py   # API calls, streaming
│   ├── memory_engine.py # Hierarchical memory
│   └── models.py        # Model registry
└── gui/
    ├── panels.py        # UI components
    └── graph_viz.py     # Graph explorer
```

## Data Storage

All data saved to `/storage/emulated/0/Download/chatadhd_data/`:
- `config.json` - Settings
- `secrets.json` - API key (local only)
- `chatadhd.db` - Conversations & messages
- `memory.json` - Memory tree
- `models.json` - Cached model list

## Security Model

- **API key stored locally** only
- **Memory stays on device** 
- **WAL mode** prevents database corruption
- Your data never leaves your device except API calls

## Themes

Settings → Change Theme:
- **Dark** - Default, good contrast
- **AMOLED** - Pure black for OLED screens

## Known Limitations

- Graph performance may slow with 100+ nodes
- Large file attachments not sent to API (just metadata)
- No cloud sync (by design - privacy first)

## Troubleshooting

**White text on white background?**
→ Update to v0.5.0 (uses DarkInput class)

**Models not loading?**
→ Check API key in Settings, tap Ref

**App crashes?**
→ Check Log panel, look for errors

**Database corruption?**
→ v0.5.0 uses WAL mode - should be fixed

## Credits

- Kivy framework
- OpenRouter API
- Graph visualization inspired by Gemini suggestions

## License

MIT - Use freely, modify freely, share freely.

---
**ChatADHD** - Your thoughts, your context, your control.
