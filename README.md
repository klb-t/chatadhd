# ChatADHD v0.06.03 - Bug Fixes

## Fixes in this version

### 1. Graph Explorer
- ✅ Canvas clipping via Stencil (no more overflow)
- ✅ Proper cleanup on close (canvas.clear())
- ✅ Transform fixed to include widget position

### 2. API Request Editor
- ✅ Moved from top bar to **long-press Send** button
- Long-press (0.6s) → Full API Editor opens

### 3. Top Bar
```
[Model▾] [⚙️] [🔄] [New] [Cfg] [Log]
         │    │
         │    └─ GitHub Sync
         └─ Quick API Panel

Long-press Send → Full API Editor
```

## All Features (v0.06.00 - v0.06.03)

| Feature | Access | Added |
|---------|--------|-------|
| Collapsible messages | Auto (4 lines) | v0.06.00 |
| Artifact detection | Auto in AI responses | v0.06.00 |
| Quick API Panel | ⚙️ button | v0.06.00 |
| Directory import | Memory → +Dir | v0.06.00 |
| Conversation import | ☰ → Import | v0.06.01 |
| Voice input | 🎤 button | v0.06.01 |
| OCR (screenshots) | Import → .png/.jpg | v0.06.01 |
| Full API Editor | Long-press Send | v0.06.02 |
| GitHub Sync | 🔄 button | v0.06.02 |

## API Keys (Settings)

| Key | Provider | For |
|-----|----------|-----|
| `api_key` | OpenRouter | Chat |
| `groq_api_key` | Groq | Voice |
| `ocr_space_api_key` | ocr.space | Screenshots |
| `github_token` | GitHub | Sync |

## Structure

```
5738 lines total
├── engine/
│   ├── chat_engine.py   (272)
│   ├── db.py            (274)
│   ├── github_sync.py   (411)
│   ├── importer.py      (693)
│   ├── memory_engine.py (166)
│   ├── models.py        (65)
│   └── providers.py     (467)
└── gui/
    ├── graph_viz.py     (424)
    └── panels.py        (2751)
```

---
ChatADHD v0.06.03 - Fixes for graph, API editor location
