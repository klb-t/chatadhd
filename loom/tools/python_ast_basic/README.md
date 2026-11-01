# Python AST declaration adapter

Pure stdlib Python (3.11+) over caller-supplied **UTF-8 bytes**. No imports,
execution, evaluation, subprocesses or network access for the source being read.
The CLI reads one explicit local file; the Python API reads bytes already held
by its caller. Synthetic tests run without a native library:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -B -m unittest discover -s loom/tests/python_ast_basic -v
python3 -B loom/tools/python_ast_basic/reader.py \
  loom/tests/python_ast_basic/fixtures/zagniezdzenia.py \
  --source-key synthetic:PL --name żółw.py --source-parts-request
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

## Integration status — 2026-11-01

**Parser-only admission. Native graph/HTTP/context acceptance is blocked.**
The separately handed-off `integration.patch` was reviewed and deliberately
NOT applied: `client.blob_text` receives client-controlled JSON and the proposed
hook does not reparse it with the pinned Python reader. Running `validate` in a
client before sending a request cannot secure the native boundary.

`native_projection.h` is an unused candidate byte/record consistency checker,
not an AST attestation verifier. The integrator fixed two string/JSON comparisons
with explicit string extraction so the header compiles. The focused diagnostic
`loom/tests/python_ast_basic/verify_trust_boundary.py` demonstrates coherent
name/kind/method forgeries accepted by that byte checker and refused by the
pinned Python reparse. It must not be treated as permission to expose the hook.

The CLI and Python parser API are usable locally. `source_parts_request` only
constructs a candidate transport envelope; the accepted runtime does not support
its `python_declaration` level. No new public API, package helper, UI registry,
Source/Unit persistence or selected-context path is admitted here.

## Candidate hook (not applied)


The separate root `integration.patch` is a candidate change to
`loom/src/text/source_parts.cpp`, built against base
`e2f6873c7ca24b41957b36b0127439b926068f41`. It is not applied by the author.
`native_projection.h` is the small native receipt-contract adapter included by
that patch. No ABI, UI, context selector, CMake or default format pack changes.
The request carries a scoped `python_ast` format overlay. Existing plain `code`
behavior is preserved, and calling `python_ast` without a receipt fails closed.

The host first runs this parser/`source_parts_request` on the exact immutable
bytes, uploads those bytes through the existing client-blob API, and passes the
resulting request to `client.blob_text`. No document-supplied executable paths
are used. The native boundary validates Source/Unit/Observation identities,
quotes, hashes, containment and parent order, then reuses the existing Source,
provenance, genesis, occurrence and read machinery. It saves the complete receipt
as a method-pinned blob. Nested declaration occurrences intentionally overlap;
this is one-version genesis, not cross-version alignment or a byte partition.
Parent occurrences get ordinary `part_of` links in addition to version links.
Selected occurrences use the existing schema and context path.

**Trust boundary:** the native receipt checker does not itself run Python or
prove an arbitrary submitted receipt was produced by AST. Structural labels are
explicitly derived. For authoritative AST import the host must produce and
revalidate the receipt with this adapter, rather than trust external JSON.
This delta does not install a native subprocess launcher or a UI reader registry.

Remaining acceptance (through the authorized runner):

1. Add a trusted native host path which derives/revalidates the receipt from
   immutable blob bytes using a pinned parser. Do not take executable paths or
   trust flags from the client envelope. Reuse the bounded, shell-free DOCX
   subprocess/package patterns, including interpreter isolation, input/output
   limits, watchdog, cleanup and refusal when the helper is absent or changed.
2. Keep byte consistency checks after host generation. Pin parser/profile/
   interpreter and save replay provenance. Client JSON cannot establish this
   invariant, even when all byte hashes match.
3. Only then adapt the separate shared hook, build incrementally and run
   `verify_native` plus affected G1/DOCX sentinels. The integrator ran the supplied
   harness on the unchanged accepted library: it fails at materialization with
   `text pack: no level "python_declaration"`; this is NOT native acceptance.
4. Verify forged receipt/hash/source refusal and actual selected-context request
   after reopen through the safe host/API. Preserve original BOM/CRLF bytes,
   distinguish occurrence identity from text hashes, label syntax as derived.

Boundary diagnostic (no runtime rebuild or API registration): compile
`loom/tests/python_ast_basic/check_receipt.cpp` with the repository C++20/JSON
flags and the matching own `libloom_core.a`, then run:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -B -m loom.tests.python_ast_basic.verify_trust_boundary \
  --byte-checker /absolute/path/to/check_receipt --scratch-root /own/synthetic/scratch
```

A successful diagnostic proves that byte validation is insufficient; it does
not prove native AST import. See
`docs/reports/FLEET_PYTHON_AST_INTEGRATION_2026-11-01.md` for the actual receipt.
