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

## Loom (C++ core)

`loom/` is a C++20 port of the `engine/`/`core/` packages: one kernel library
(`libloom`) shared by every client — the Kivy app above, a CLI, an HTTP
server + React web UI, and an Android shell — that reads and writes the same
data directory (`chatadhd.db`, `config.json`, `memory.json`, …) as the Python
app, byte-for-byte. It is not a rewrite that replaces this app; it is the
same engine made reusable outside Kivy, kept in lockstep with `engine/` and
`core/` by differential (Python-vs-C++) compat tests.

```bash
cd loom
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

See [`loom/README.md`](loom/README.md) for the full build matrix, C ABI,
Archive Intelligence pipeline and CLI/server/web/Android layout;
[`docs/selfhost/README.md`](docs/selfhost/README.md) for the self-hosting run
where Loom compiled this repository's own history into a project
description; and
[`docs/architecture/MEGA_MASTER_2026-09-16.md`](docs/architecture/MEGA_MASTER_2026-09-16.md)
for the architecture Loom implements.

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
