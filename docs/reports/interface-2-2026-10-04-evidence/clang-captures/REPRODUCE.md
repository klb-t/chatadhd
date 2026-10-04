# Clang capture check

Clang 18.1.3 compiled the real `app.cpp` twice with the CMake vendored-SQLite
flags, `-Werror -Wunused-lambda-capture -fsyntax-only`. Both exit codes were 0
and both stderr files are empty. Macro output proves which header branch ran.
This is one translation unit, not the full Clang matrix owned by thread 8.

`attempt-002/RESULTS.json` records exact commands, input/output hashes and the
compiler version. Large macro outputs and compile_commands are compressed
losslessly as `.gz`; `gzip -dc` recovers their original bytes. The header copy
omits only `loom/usage_policy.h` for the unavailable-policy build.

Run `check_app_clang.py --help` for the reproducible checker. Use a fresh output
directory, the current repository and a Clang 18 compiler. Local extracted
compiler files were reclaimed after verification; exact package versions and
hashes, library-byte restoration and the failed first attempt remain in
`../negative/clang-local-attempt-001.tar.gz`. Source and thresholds were not
changed for compiler setup failures.
