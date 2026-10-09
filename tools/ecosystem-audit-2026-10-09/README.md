# Ecosystem philosophy audit candidate scanner

This is a read-only audit aid, not a product guard or a verdict engine. Python
3.10+ and Git are its only dependencies. It never imports or runs product code,
calls a model, contacts a remote, or starts CI.

## Reproduce

```sh
python3 tools/ecosystem-audit-2026-10-09/scan.py \
  --repo /absolute/path/to/repo \
  --repo-name klb-t/chatadhd \
  --base-branch main \
  --revision 9e20f99ab27e7cd45e1892f83bf60fbe60db3de9 \
  --visibility public \
  --output /absolute/path/to/audit-output
python3 -m unittest discover -s tools/ecosystem-audit-2026-10-09/tests -v
```

Use each repository's independently recorded base SHA. `--revision` defaults to
HEAD only for convenience; pinned SHA is required for reproducible published
evidence. Git mode reads the immutable tree and blobs; working-tree changes and
untracked files cannot alter the scan. Scan outputs do not belong to product code.

`rules.json` contains path classification, scanned categories, source suffixes,
maximum size, lexical rules and the six R42 exceptions. `--rules` permits a
different recorded rules file. `--categories product,tooling,research,build,tests`
explicitly expands scope. Every summary records the rules hash and scanner hash.
No path classification is claimed to be semantically definitive.

## Outputs and denominators

- `inventory.jsonl`: **every recursive tracked tree entry** at the recorded SHA,
  including gitlinks and symlinks. Each has one category and explicit scan status.
- `candidates.jsonl`: possible policy/data/control points, with repository, SHA,
  path, git blob OID, exact span, rule, extraction method and source-fragment hash.
- `summary.json`: complete inventory denominator; per-category counts; eligible,
  actually scanned and skipped counts, including `coverage_by_category`;
  AST/lexical status; candidate counts.

The primary denominator is tracked entries, not all files that have ever existed.
Git history and submodule contents are not recursively traversed. Embedded tests
such as `loom/src/**/tests/` are tests; active `core/engine/gui/main.py` is product.
Recognized executable source under a directory named `data`, such as Kotlin
`core/data/KeyboardRepository.kt`, remains product code rather than data.
Generated/vendor/history/test/data/docs/assets entries remain visible in inventory,
even when their content is outside the configured scan. Default source scanning
covers product, tooling, research and build code with recognized suffixes, not
all tracked file contents. `semantic_reviewed_entries` is always **0**: semantic
findings must be supplied by a human/agent tracing real execution paths.

Files with blind/holdout/heldout markers and conventional secret/credential paths
are inventoried but **never read**. Symlinks are not followed. Oversized, binary
and non-UTF-8 files yield explicit gaps. Research branches/corpora not in the
selected tree are neither opened nor inferred. Output contains no source text,
literal value or variable identifier. Paths can themselves reveal information;
this distribution accepts only explicitly public repositories. Private-repo
reports belong in a separate private workflow.

## What signals mean

Python AST detects defaults, branches, exceptions, boolean guards/fallbacks,
slices, comprehensions and direct call ordering. A lexical pass identifies
literal strings/numbers and possible prompts, models, endpoints, UI text,
regexes, thresholds/weights/limits; control rules look for missing-value behavior,
fallbacks, filtering, sorting, deduplication, truncation, network calls,
inference acceptance and setting inheritance. Rules are candidates, never proof.
Python parse failures retain a lexical scan and explicit parse status.
Non-Python code uses an approximate string/comment lexer and regex rules.

Spans are 1-based Unicode-code-point line/column coordinates; end column is
exclusive. Multiline spans retain end line. IDs use repository, path, rule,
fragment SHA-256 and occurrence among identical fragments. They repeat exactly
for the same inputs and survive simple line shifts; editing the fragment,
renaming its file or reordering identical occurrences can change its identity.
Manual findings require their own stable IDs and source evidence.

R42 asks: **could someone want to change it without changing the algorithm?**
The six exceptions are contract names, external-standard constants, mechanism
vocabulary, contract serialization, minimal bootstrap and developer diagnostics.
The scanner does **not** exempt every `schema`, `GET`, `error` or log string.
An exception requires contextual proof. Optional `exception_assessments` entries
must contain `candidate_id`, `exception_id` (R42.1–R42.6), `reason`, `reviewer`;
they are attached as external assessments without removing candidates. Empty is
the truthful default. Tests/fixtures are separated by scope, not called R42
product violations. Changing a rules file changes its reported hash.

## Exported sources without Git

`--manifest manifest.json --repo /source/root` accepts:

```json
{"repo":"owner/name","sha":"asserted source SHA","base_branch":"main","files":[{"path":"src/main.py","content_sha256":"optional expected SHA256"}]}
```

Paths must be unique and relative, cannot escape root, and symlinks are rejected.
Optional hashes are verified for scanned content. This mode labels provenance
`manifest_assertion_plus_local_files`: completeness and revision identity are
asserted by the manifest, not independently established. Arbitrary manifest
fields are not copied into output.

## Limits requiring semantic audit

This scanner proves neither reachability nor consumption. It cannot establish
data → graph → runtime → UI wiring, distinguish an arbitrary constant from a
necessary algorithm constant, verify consent, or show that every alternative is
representable. Interprocedural call ordering, reflection, generated code and
dynamic configuration need manual tracing. Lexical matching can overcount local
variable names and undercount embedded expressions, raw strings, templates,
HTML text nodes, domain regex literals and unusual languages. A missing signal
is not evidence that a problem is absent. Numeric matching is intentionally
conservative and can miss language-specific suffixes (`0.5f`, `1UL`) or leading-dot
floats (`.5`); Python default AST nodes still expose their containing defaults.
Classifications and limits are data;
review the inventory before claiming a product/module denominator.

## Report integrity and behavioral probes

Use `validate_reports.py --repos-root /clones --reports-root /audit/reports --output /audit/results` for the public report gate. Each clone is named after the repository and contains its recorded SHA. This validator checks declared evidence locations and fields; it is not a semantic verdict engine. See `docs/reports/ecosystem-audit-2026-10-09/REPRODUCE.md` for the executed offline product probes, scope, dependencies and receipts. No model or CI calls are necessary.
