# ChatADHD v0.07.09

Mobile-first AI chat client with hierarchical memory, branching conversations,
and zero-knowledge encryption.

## Architecture

```
KOD (versioned, replaceable)          DANE (persistent, user-owned)
───────────────────────               ──────────────────────────────
chatadhd_v0.07.09/                    ~/Documents/ChatADHD/
├── main.py                           ├── .chatadhd_data   (sentinel)
├── core/                             ├── config.json
│   ├── crypto.py      AES-256-GCM    ├── secrets.json     (600 perms)
│   ├── semantic.py    NER + topics    ├── chatadhd.db      (SQLite WAL)
│   └── selector.py   3-tier search   ├── memory.json
├── engine/                           ├── models.json
│   ├── paths.py       KOD≠DANE       ├── attachments/
│   ├── config.py      JSON store     ├── exports/
│   ├── db.py          thread-safe     └── logs/
│   ├── events.py      pub/sub bus
│   ├── chat_engine.py API + stream
│   ├── memory_engine.py  tree + tags
│   ├── models.py      registry
│   ├── providers.py   OCR/ASR
│   ├── importer.py    7 formats + streaming
│   ├── graph_engine.py  realtime graph
│   ├── graph_memory.py  graph-based search
│   ├── semantic_llm.py  LLM analysis
│   ├── semantic_worker.py  bg daemon
│   ├── batch_api.py   Anthropic batch
│   └── github_sync.py bidir sync
├── gui/
│   ├── base.py        themes, widgets
│   ├── chat_panel.py  main chat UI
│   ├── conv_panel.py  conversation list
│   ├── memory_panel.py  memory tree
│   ├── dialogs.py     settings, picker
│   ├── import_panel.py  threaded import
│   ├── voice_panel.py
│   ├── github_panel.py
│   └── graph_viz.py   force-directed LOD
└── requirements.txt
```

## Quick Start

```bash
pip install kivy requests
python main.py
```

## Optional Dependencies

```bash
pip install cryptography          # Zero-knowledge encryption
pip install scikit-learn          # TF-IDF semantic search (tier 2)
pip install sentence-transformers # Embedding search (tier 1)
```

## Features

- Multi-model (OpenRouter: Claude, GPT, Gemini, DeepSeek, etc.)
- Streaming responses with reasoning/thinking display
- Branching conversations (edit → new version, old preserved)
- Hierarchical memory tree with active/inactive toggle
- Semantic auto-tagging, entity extraction, topic detection
- 3-tier search: embeddings → TF-IDF → keyword
- Optional AES-256-GCM encryption (PBKDF2, 600K iterations)
- Import: SQLite, JSON, HTML, MHT, Markdown, text, screenshot (OCR)
- Force-directed graph visualisation
- Voice input (Groq Whisper / Google Speech)
- Bidirectional GitHub sync
- Themes: Dark / AMOLED

## Security

- secrets.json: owner-only file permissions
- Encryption: optional, local-only, server never sees plaintext
- LLM providers see plaintext (by design — unavoidable)
- No telemetry, no analytics, no ads
