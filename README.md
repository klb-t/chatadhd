# ChatADHD v0.4.0-dev

Multi-model AI chat client with hierarchical graph memory.

## Features

- **Graph Memory**: Tree structure with cross-links between nodes
- **Semantic Search**: sentence-transformers → TF-IDF → keywords fallback
- **Iterative Buffer**: Multi-step prompt aggregation
- **File Parsing**: OCR (Tesseract) and ASR (Whisper) support
- **Multi-model**: OpenRouter API with model switching

## Install

```bash
pip install kivy requests openai plyer
python main.py
```

## CLI Demo

```bash
python cli.py
```

Commands: `/mem-add`, `/mem-tree`, `/link`, `/select`, `/buffer-add`

## Structure

```
chatadhd/
├── main.py           # Kivy GUI entry point
├── cli.py            # CLI for testing
├── db.py             # SQLite database
├── config.py         # Configuration management
├── version.py        # Version info
├── engine/           # Core logic
│   ├── memory_engine.py   # Graph memory
│   ├── selector.py        # Semantic search
│   ├── api_client.py      # LLM API
│   ├── prompt_engine.py   # Prompt building
│   ├── iterative_buffer.py # Multi-step workflows
│   ├── file_parser.py     # OCR/ASR
│   └── chat_engine.py     # Main engine
└── gui/              # Kivy UI
    ├── colors.py
    ├── widgets.py
    └── panels.py
```

## Git Setup (from phone)

```bash
# Install git (Termux)
pkg install git

# Configure
git config --global user.email "you@example.com"
git config --global user.name "Your Name"

# Init repo
cd /path/to/chatadhd
git init
git add .
git commit -m "Initial commit v0.4.0-dev"

# Push to GitHub
# 1. Create repo on github.com
# 2. Add remote:
git remote add origin https://github.com/USERNAME/chatadhd.git
git branch -M main
git push -u origin main
```

For authentication, use GitHub Personal Access Token as password.
