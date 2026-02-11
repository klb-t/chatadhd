# ChatADHD v0.06.00 - Major UI Overhaul

## New Features

### 📦 Collapsible Messages
- Long messages show first 4 lines
- Click **▼ więcej** to expand
- Click **▲ mniej** to collapse

### 🎨 Artifact Detection
- Auto-detects code blocks (```...```)
- Floating bar with: Copy | →Mem
- One-click save to Memory

### ⚡ Quick API Panel
- Click **⚙️** for fast settings
- **Favorite models** (6 quick buttons)
- **Presets**: Creative, Balanced, Precise, Code, Long
- **Temperature** slider (0.0-1.5)
- **Max tokens** slider (1k-32k)

### 📁 Full Directory Import
- +Dir imports ALL file contents
- Warning if >50 files or >500KB
- Supports: .py, .md, .txt, .json, .js, .html, .css
- Skips: __pycache__, .git, node_modules

### 📊 API Preview (long-press Send)
- Shows request structure
- Token count estimate
- Copy to clipboard

### 🔧 Fixed Issues
- ✅ NodeEditorPopup import error in Graph
- ✅ Keyboard visibility (softinput_mode)
- ✅ Voice button (uses system keyboard mic)

## UI Layout

```
[Model▾] [⚙️] [Ref] [New] [Cfg] [Log]
[🌐Web] [🔬Deep] [🧠Auto] [💭    ]
┌─────────────────────────────────────┐
│ Messages (collapsible)              │
│ ┌─────────────────────────────────┐ │
│ │ You: message preview...         │ │
│ │ ▼ więcej                        │ │
│ └─────────────────────────────────┘ │
│ ┌─────────────────────────────────┐ │
│ │ AI: response with artifact      │ │
│ │ [📄 python] [Copy] [→Mem]       │ │
│ └─────────────────────────────────┘ │
└─────────────────────────────────────┘
┌─────────────────────────────────────┐
│ [Message input................] [Send] │
│ [📎 Attach                    ] [🎤]   │
└─────────────────────────────────────┘
[Ready                                  ]
```

## Quick API Presets

| Preset | Temperature | Max Tokens |
|--------|-------------|------------|
| Creative | 0.9 | 4096 |
| Balanced | 0.7 | 4096 |
| Precise | 0.3 | 4096 |
| Code | 0.2 | 8192 |
| Long | 0.7 | 16384 |

## Version Scheme

Switching to x.yy.zz for future versions:
- **0.6.0** = current
- Next: **0.06.01**, **0.06.02**, etc.

## Installation

```bash
pip install kivy requests
```

Copy to `/storage/emulated/0/Download/chatadhd_v0.06.00/`
Run `main.py` in Pydroid 3

---
ChatADHD v0.06.00 - Artifacts, Presets, Full Control
