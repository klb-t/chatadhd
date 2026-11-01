# Focused DOCX source-parts acceptance

Integrator-only runtime gate after a fresh heavy-slot admission. This target links
an already rebuilt own `loom_core`; it does not compile monolithic `loom_tests`.
The core build must use system SQLite, as this focused link configuration does.

```sh
cmake --build "$CORE_BUILD" --target loom_core loom-server loom -j2
cmake --install "$CORE_BUILD" --prefix "$NEW_STAGE" --component runtime
cmake -S loom/tests/docx_source_parts -B "$NEW_FOCUSED" \
  -DLOOM_SOURCE="$REPO/loom" -DLOOM_CORE_BUILD="$CORE_BUILD"
cmake --build "$NEW_FOCUSED" -j2
LOOM_DOCX_TEST_PYTHON=/usr/bin/python3 \
LOOM_DOCX_TEST_READER="$NEW_STAGE/share/loom/docx_basic/reader.py" \
ctest --test-dir "$NEW_FOCUSED" -j1 --output-on-failure -V
```

Two suites: `docx_source_parts` (9 cases) and the existing `text_source_parts`
(11 cases). The latter is a regression justified by shared source_parts/text-pack
changes. Each CTest filter excludes the other suite; both execute separately.
Only synthetic fixtures and ScriptedTransport. No provider network or UI.

HTTP gate: `test_server_docx_source_parts.DocxSourceParts` (2 scenarios), using a
fresh own staged `LOOM_SERVER_BIN`. The test deliberately retains the shipped
`package:docx_basic` default for both helper and interpreter, rather than resolving
a script from the checkout. The staged runtime must contain the matching new
reader and runtime manifest. Existing harness makes only synthetic accounts and
loopback provider calls. Use a new own CWD/evidence directory and filesystem
isolation; explicit data/accounts/static paths; stop all owned processes.

The scoped UTF-8 HTTP regressions are
`GraphSourceParts.test_occurrence_context_and_reopen` and
`GraphSourceParts.test_code_version_and_independent_source`. Do not automatically
run full P0, DOCX-parser, external-share, accounts or backup suites.

Acceptance 2026-11-01: native9/9 (172 assertions), G1native11/11 (76), HTTP4/4.
[Integrator report](../../../docs/reports/DOCX_SOURCE_PARTS_INTEGRATION_2026-11-01.md).
No GUI acceptance, no byte offsets in original DOCX, no pagination, and no
claim that database-only context selection verifies BlobStore integrity.
