# Test fixtures

`import/` holds small synthetic inputs, at least one for every format
`engine/importer.py` supports: ChatGPT mapping trees, Claude exports,
OpenAI-style message lists, conversation objects, JSONL, HTML (class/data-role
and label layouts), MHT, Markdown, plain text, Claude-layout and generic SQLite
files, a sniffed file with no extension, and a ZIP bundle. They contain no real
conversations.

Regenerate them with `python3 loom/tools/gen_import_fixtures.py`. The importer's
differential test feeds each file to both `engine.importer.ConversationImporter`
and Loom's `ConversationImporter`, then compares the resulting databases the same
way `tests/compat/test_db_compat.py` does. Tests reach this directory through the
`LOOM_TEST_FIXTURES` compile definition.
