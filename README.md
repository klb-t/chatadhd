
ChatADHD - Full Modular Prototype (graph-hierarchical memory + selector + iterative buffer)
=========================================================================================
This package implements the design we discussed: a graph-based hierarchical memory, a small-model
selector (with safe fallbacks), an iterative prompt buffer for aggregating multi-step edits, file parsers
with OCR/ASR adapters (optional), and a ChatEngine that ties these together.

Run:
  - CLI: python3 cli.py
  - GUI: python3 main.py  # requires Kivy

Key components:
  - engine/memory_engine.py: graph-hierarchical memory with Node, links, embeddings placeholder
  - engine/selector.py: SelectorEngine - small-model selection with TF-IDF / embedding fallbacks
  - engine/iterative_buffer.py: IterativePromptBuffer for aggregating edits
  - engine/prompt_engine.py: building system prompts from nodes and buffer
  - engine/chat_engine.py: integration and API calling via engine/api_client.py
  - db.py: sqlite storage for nodes and links
  - engine/file_parser.py: OCR/ASR adapter stubs (pytesseract/whisper optional)
  - cli.py: interactive console to exercise features
  - gui/: minimal Kivy placeholders

Notes:
  - Heavy deps (sentence-transformers, torch, whisper, pytesseract) are optional. The selector falls back to lightweight TF-IDF
    if sklearn is available, otherwise to keyword overlap.
  - The project is organized for easy migration to a production environment.
