# Python AST declaration adapter

Pure stdlib Python (3.11+) over caller-supplied **UTF-8 bytes**. No imports,
execution, evaluation, subprocesses or network access for the source being read.
The CLI reads one explicit local file; the Python API reads bytes already held
by its caller. Synthetic tests run without a native library:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -B -m unittest discover -s loom/tests/python_ast_basic -v
python3 -B loom/tools/python_ast_basic/reader.py \
  loom/tests/python_ast_basic/fixtures/zagniezdzenia.py \
  --source-key synthetic:PL --name żółw.py
```

`parse(raw, source_key=..., name=..., settings=..., expected_sha256=...)`
returns a Source descriptor, native-contract Units/Observations, declaration
order/parent ordinals, structural provenance and a pinned method. `validate`
reparses and compares the whole result, including JSON types. `source_parts_request`
revalidates before creating a request to the existing `client.blob_text` action.
Nothing is persisted by this adapter. Source identity uses the current native
`src_text_` rule; Unit/Observation IDs follow `model_core.cpp`, including native
canonical locator JSON. Labels, resource presets and grammar version live in
`profile.json` or a caller-supplied profile, not another registry/framework.

## Coordinates and evidence

- Module covers `[0, original_size)`, including BOM, comments and trailing bytes.
  Empty input is a module with an empty quote. Its locator line is null.
- Declarations include decorators from `@` and end at the last AST token.
  `ast_range` separately starts at `class`, `def` or `async def`. Leading
  indentation and trailing comments/newlines are outside declaration spans.
- Byte bounds are half-open and address the **original** blob. Line ranges are
  1-based and inclusive. CPython's AST columns are UTF-8 bytes; SyntaxError
  columns are converted from characters to bytes. CRLF/bare CR/BOM are retained.
- Parents are nearest enclosing module/class/function declarations, even inside
  `if`, `try` and other compound statements. These are lexical declaration
  containers, not Python runtime name resolution (class scope has special rules).
  Equal names never imply identity; module/sole-function Units remain distinct.
- Quotes are observations of raw text. Kinds, ranges and parents are derived
  syntax with a method hash. No confidence, execution, runtime dependency or
  semantic correctness is inferred. `ast.parse` does not perform all compiler
  context checks (for example a module-level `return` can have an AST).
- Syntax/encoding/hash/resource failures return no partial declaration tree.
  Diagnostics contain codes and optional locators, never source excerpts.
  Non-UTF-8 encoding cookies are refused; no silent transcoding or byte repair.

## Resource presets and replay

Every ceiling in the profile is configurable; zero disables it. Input is checked
before decoding/parsing (the CLI bounds its read). AST node/declaration/quoted
byte limits are checked **after** `ast.parse` allocated its tree; they do not
promise an interpreter memory sandbox. The hosting runner owns process ceilings.
Nested quotations overlap and can repeat source bytes, so quoted bytes have a
separate configurable ceiling. Interpreter version, reader SHA and full profile
are pinned. The saved result is derived data; the original SHA is unchanged.
Keep this adapter code/version available with the method for regeneration.

## Trusted host integration — 2026-11-01

The local parser is now invoked by the native host from the installed
`share/loom/structure_readers` package. Use the existing `client.blob_text`
action with **only** `hash`, `source_key`, `name`, `format="python_ast"` and,
optionally, `previous_source_id`. The host generates the receipt itself before
mutating the graph. Interpreter/helper/profile/limits never come from uploads,
request options or client receipts. The exact installation is pinned and input
is verified before and after execution. Read and selected context reparse with
the installed reader and compare the saved binding and occurrence.

`source_parts_request` remains a legacy candidate-envelope constructor for
local tooling/tests. **Do not send its receipt/pack to the API**: the safe host
intentionally rejects them. `native_projection.h` is still an unused diagnostic
byte checker, not AST attestation. The historical `verify_trust_boundary` showed
why it must not become an API trust boundary; it need not be repeated.

Native host acceptance is in `loom/tests/structure_readers/verify_native.py`
and `loom/server/tests/test_server_host_readers.py`, with package faults checked
by `verify_limits.py`. The old `verify_native.py` here targets the rejected
candidate envelope and is retained as historical evidence, not the new contract.

See `loom/tools/structure_readers/README.md` for packaging, replay limits and
accepted test scope. Syntax labels remain derived, quotes remain original bytes.
No Python-specific UI, semantic execution or Android helper acceptance is claimed.
