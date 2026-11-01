# EPUB synthetic acceptance

Author checks (stdlib only):

```sh
python3 -B -m unittest discover -s loom/tests/epub_basic -p 'test_*.py' -v
```

`fixtures.py` generates a deterministic mini-EPUB with ZIP order opposite to
spine order, Polish UTF-8, repeated/empty/nested paragraphs, headings, and a
non-linear spine item. It also generates a ZIP traversal attack. All bytes are
synthetic; no production documents, secrets, holdouts or downloads are needed.
The 31 unittest cases cover parser and real isolated helper subprocess behavior.
They do not establish native or HTTP integration acceptance.

Sole integrator `01a192f5-2814-7e52-bf0c-32f3428151ed`:

1. Verify the stable manifest and patch SHA-256s. Check that product paths are
   only the two assigned EPUB directories and contain no checkpoint/receipts.
2. Apply-check both patches against the next accepted base. Review conflicts in
   shared hooks; do not overwrite another format's reader/pack additions.
3. Apply product then integration. The latter already contains freshly rendered
   pack embeddings. Run the repository's pack generator **check** commands;
   regenerate once if adaptation changed canonical packs. Run no protected
   holdout or provider tests.
4. Generate fixtures in a new own temporary directory:

   ```sh
   python3 -B loom/tests/epub_basic/fixtures.py /tmp/epub-native-fixtures
   export LOOM_EPUB_TEST_FIXTURES=/tmp/epub-native-fixtures
   export LOOM_EPUB_TEST_PYTHON=/usr/bin/python3
   ```

   Select the actual existing absolute stdlib interpreter. No install/fallback.
5. Through the authorized runner and resource reservation, build only the own
   needed core/test target and run:

   ```sh
   <own-build>/loom_tests --test-suite=epub_basic --no-intro=true
   <own-build>/loom_tests --test-suite=docx_source_parts --no-intro=true
   <own-build>/loom_tests --test-suite=text_source_parts --no-intro=true
   ```

   `test_hook.cpp` adds six native cases: real document dispatch; actual
   attachment/provenance hook; upload/materialize/read/reopen/idempotence and
   chapter edges through the shared action path; rejected hostile ZIP without
   graph writes; tampered hashes/ranges/XML locators; and original-source
   descriptor corruption. `ScriptedTransport` must record zero requests.
6. If required by the integrator's gate, run one scoped synthetic upload/read
   HTTP test and relocated runtime package smoke using the own runner. The
   optional runtime package must include both reader.py and runtime.json; no
   source-checkout fallback or host install. This author supplies no HTTP PASS.
7. Record actual counts, accept into the next base, and update canonical state
   from the integrator workspace. The author starts no next loop.

Native checks are intentionally pending: the author's sparse checkout lacks
vendored nlohmann/doctest headers, and backend build/integration is reserved for
the sole integrator. A missing dependency is a blocker, not a skipped PASS.
