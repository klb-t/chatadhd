# Local DOCX runtime package

The default reader uses `package:docx_basic` for both the helper and interpreter
configuration. The resolver anchors resources to the running Loom executable or
shared library, never to an upload name, the document's directory, HOME or PATH.
A relative main-program invocation uses its verified mapping and `/proc/self/exe`.
Load the shared library by an absolute path (or an absolute loader search path).
An explicit absolute `python` or `script` in the trusted reader profile still
wins; a missing override is an error, not a fallback to another helper.

The CMake `runtime` component installs a relocatable layout:

- `bin/loom-server` (when enabled), `lib/libloom.so*` (when enabled);
- `share/loom/docx_basic/reader.py`, copied from the canonical parser;
- `share/loom/docx_basic/runtime.json`, containing the configured interpreter,
  its observed version and the parser SHA256.

Python is an explicit **system runtime dependency**, not a bundled interpreter:
Python >= 3.10 with standard-library `zipfile`, `zlib` and `pyexpat`. CMake checks
that the selected `LOOM_DOCX_PYTHON` can run in isolated mode. Runtime does not
search PATH, use site packages, inherit provider credentials, install software
or download dependencies. A deployment must supply the interpreter at the
recorded absolute path, or deliberately configure another trusted interpreter.
Missing helper/interpreter yields `docx_reader_unavailable`; original bytes stay
in the existing upload store. Linux/glibc >= 2.34 is required by the adapter.

Use an **own local prefix**, never `/usr`, for inspection:

```sh
cmake --build .integration-p0-20261031/build --target loom-server loom -j2
cmake --install .integration-p0-20261031/build \
  --prefix "$PWD/.integration-local-package" --component runtime
```

The source module also mirrors runtime resources into the own CMake build for
local execution; it does not embed or search for a source-checkout path.
`LOOM_PACKAGE_DOCX_RUNTIME=OFF` omits this optional runtime package.

Independent package test, after the own build, from repository root:

```sh
python3 loom/tools/docx_basic/stage_test.py \
  --build .integration-p0-20261031/build \
  --stage .integration-package-check
```

This creates a new stage, relocates it, and uses Linux Landlock to deny the
original parser, adapter and build binary. Only the installed artifact, system
runtime dependencies and copied test support remain readable. Five serial
cases cover the default HTTP flow with PL text/reopen/original/hash, absent
helper, absent interpreter, a hostile upload filename, and the default shared
library path. There is no fallback that labels unsupported sandboxing as PASS.
No real provider, user documents, credentials, host installation or deployment.
The JSON manifest is deployment metadata, not a signature or a sandbox guarantee.
