# ChatADHD v0.07.10

Local-first, mobile-oriented AI chat client built around branching conversations, hierarchical memory, semantic retrieval and user-owned persistent data.

**Status:** functional Python/Kivy prototype. The repository contains the working application and storage/engine layers; it is not yet presented as a packaged consumer release.

## Core ideas

- **Code and data are separate.** Application code can be replaced or versioned without moving the persistent user store.
- **Conversation history is a graph, not a flat transcript.** Editing or branching preserves earlier versions.
- **Memory is inspectable.** Hierarchical and graph memory can be searched and toggled rather than hidden behind an opaque prompt.
- **Provider choice is runtime data.** The model registry includes provider/model metadata, pricing and context information.
- **Local privacy controls matter.** Secrets are stored outside the repository and optional AES-256-GCM encryption is supported for local data.

## Features

- multi-model chat through OpenRouter-compatible providers;
- streaming responses and reasoning/thinking display;
- branching/versioned conversations;
- hierarchical memory tree;
- real-time knowledge graph and graph-based retrieval;
- semantic analysis, entity extraction and topic detection;
- tiered search: embeddings → TF-IDF → keyword fallback;
- import from SQLite, JSON, HTML, MHT, Markdown, text and screenshots/OCR paths;
- voice input;
- bidirectional GitHub sync;
- model filtering, descriptions, context lengths, pricing and cost estimation;
- dark and AMOLED-oriented Kivy UI.

## Architecture

```text
core/      crypto, semantic analysis, selection/retrieval primitives
engine/    persistence, chat, memory, providers, import, graph and GitHub sync
gui/       Kivy panels, dialogs and graph visualisation
main.py    application entry point
```

Persistent user data defaults outside the source tree and includes configuration, SQLite state, memory, attachments, exports and logs. See `CLAUDE.md` for the detailed code/data contract and extension notes.

## Quick start

```bash
python -m pip install -r requirements.txt
python main.py
```

Optional semantic backends can be installed when needed:

```bash
python -m pip install scikit-learn
python -m pip install sentence-transformers
```

## Security notes

- API secrets belong in the user data/config path, not in the repository.
- Local encryption is optional; remote model providers necessarily receive the plaintext sent to them.
- The project does not add telemetry, analytics or advertising.

## Licensing

This project is **source-available**, not OSI open-source. Noncommercial use is licensed under the PolyForm Noncommercial License 1.0.0; see `LICENSE`.

Commercial use requires a separate written license; see `COMMERCIAL_LICENSE.md`.
