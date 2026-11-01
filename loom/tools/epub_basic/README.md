# EPUB basic local reader

Executable Python stdlib reader plus an internal bridge to the existing Loom
attachment/document reader and `client.blob_text` source projection. There is
no second importer, database, schema migration, public C ABI or model/provider
call. Python >=3.10 with `zipfile`, `zlib` and Expat is a runtime dependency.

Run the isolated helper with an explicit JSON configuration:

```sh
python3 -I -B -S loom/tools/epub_basic/reader.py book.epub \
  "$(cat loom/tools/epub_basic/preset.json)"
```

The same argv accepts `-` and reads the original ZIP from stdin, as the existing
native DOCX process transport does. Success is one JSON result on stdout with
exit 0. Failure is `{"error":{"code":"epub_..."}}` with exit 1. Diagnostics
contain mechanism keys, never source content, paths to private files or secrets.
Library API: `extract(original_bytes, limits_dict)` raises `EpubError(code)`.
Neither mode writes/extracts ZIP members or fetches resources.

The reader resolves `META-INF/container.xml` -> selected OPF -> manifest ->
spine, then reads XHTML in spine order, including `linear="no"` entries. A
chapter means a **spine item**, not an inferred narrative chapter or heading
hierarchy. The zero-based `rootfile_index` selects an OPF rendition among the
OPF rootfiles in container order; its preset is 0. Other renditions stay in the
original; the result explicitly reports partial support. ZIP member order and
filenames do not determine reading order. Missing spine members/references are
errors; there is no guessed filename fallback.

`source.sha256` and `source.size` describe the exact original ZIP. Its `member`
is the selected OPF name. Each chapter records its member, spine index, idref,
linear flag, first block/count and a derived byte range. Blocks recognize
XHTML `p`, `pre`, `li`, and `h1`…`h6`, including repeated and empty paragraphs.
Nested recognized blocks are emitted at the innermost recognized boundary;
unplaced outer text is reported as omitted. Block locators are
`member#spine/N/xhtml/I/J/…`, where the integers are zero-based element-child
indices from the XHTML root. They are **XML element selectors, never ZIP or
XML byte offsets**. ZIP member names are case sensitive and URI hrefs are
percent-decoded once before resolution. Sibling relative references are
permitted only while they remain inside the archive.

`text` is a versioned basic rendering: heading prefixes, list prefixes, and one
newline between blocks. Non-`pre` whitespace collapses to single spaces; this
is neither byte-preserving XML text nor a browser rendering. `pre` whitespace
is retained after XML line-end normalization. CSS visibility/layout, images,
math, SVG, multimedia, active content and pagination are not reconstructed.
DTD-dependent EPUB 2 XHTML is deliberately refused. `xml:base` is rejected
explicitly rather than silently reading a different relative member. ZIP64, multi-disk,
encrypted containers/members, and compression other than stored/DEFLATE are
unsupported with explicit errors. External manifest resources are reported as
omitted; an external spine item is rejected. No scripts, XInclude, schemas,
stylesheets or external entities execute or resolve.

`projection.sha256`/`size` describe only the rendered UTF-8. Its `ranges` align
with `blocks`: `[begin,end)` `byte_range` includes prefixes/separator;
`text_range` addresses the exact UTF-8 bytes of `block.text`. Chapter ranges
cover their blocks and their following separators, if present. Empty chapters
have an empty range. Original bytes stay in the existing BlobStore, while the
text, extraction result, parser source and transform/limits are separate
replay artifacts after explicit materialization.

`preset.json` contains tunable resource ceilings; zero disables an individual
ceiling. `read_chunk_bytes` stays positive and `rootfile_index` stays a
nonnegative ordinal (these two are not ceilings). Disabling ceilings never
disables unsafe-path, DTD/entity, encryption, integrity or alias checks. The
ZIP directory is counted before `ZipFile` allocates entry objects; sizes and
ratios are checked both from metadata and while decompressing complete streams.
Forged uncompressed sizes with prefix CRCs cannot conceal extra expansion.
XML depth and cumulative node budgets cover all parsed publication XML,
including unused XML members. Input reading, text size, native stdout and
native subprocess time are separately bounded by trusted configuration.

`native.cpp` is internal glue, located here to respect the author's allowlist.
It reuses the current isolated helper transport and its `Prepared` data
container; EPUB result schemas and raw sources are never relabelled DOCX.
It validates UTF-8 hashes, rendering spans, source binding, spine/chapter
partitions and XML selectors before materialization. Chapter units reference
derived byte ranges and retain original ZIP/XML selectors in attributes.
Empty chapters can share an empty byte address; distinct spine identity lives
in distinct chapter nodes. Under the preset `recognized_block` level,
paragraph occurrences link to their chapter and use existing genesis. A
caller-selected different genesis level retains the chapter locator table but
does not create block-specific chapter graph edges. Chapter nodes are
structural views; this increment exposes selected text through the existing
paragraph read action, not a separate chapter context reader.

The six native acceptance cases in `loom/tests/epub_basic/test_hook.cpp` exercise
actual registry/attachment/source_parts actions, not a Python imitation. They
require the separate integration patch, existing dependencies, synthetic fixture
generation and an integrator-owned build. The author does not run backend
builds or claim those cases passed. Pure stdlib tests, including actual isolated
subprocess I/O, run without any native dependency:

```sh
python3 -B -m unittest discover -s loom/tests/epub_basic -p 'test_*.py' -v
```

Shared hooks must be applied by the sole integrator from `integration.patch`:
registry/messages, generic transport result-schema selection and package
resolution, document dispatch, attachment provenance/warnings, source_parts
prepare/read validation, pack embeddings and explicit build/runtime wiring.
The handoff manifest records exact before/after hashes; rebase these small
hooks if another format changed the shared registry. Never overwrite that work.
