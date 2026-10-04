# Native import: matched synthetic before/after measurements

All input is generated, public and offline. Each shape contains **64 conversations,
128 messages and 7,404 normalized Unicode characters**, with 524,288 bytes of
unknown metadata per message. The wrapper is 67,161,202 bytes; the array is
67,161,093 bytes. The generator and measurement JSON pin their exact SHA-256.

The baseline CLI is built from unmodified `161cc22dfb84fe863389d6b90323bd44516a68dc`.
The measured after CLI is from `f50d01653c0331eac283fc0d983775df3397052a`, before
the later terminal-receipt and parser compatibility fixes. Each receipt records
its binary hash. Both builds use `Release` with explicit **`-O0 -DNDEBUG`**, WERROR,
vendored SQLite and OpenSSL. These are comparable preparation builds, not
optimized production throughput estimates. W2 is absent from these binaries.

| Shape | Before peak RSS, KiB | After peak RSS, KiB | Before wall, s | After wall, s | After cache hit, s |
|---|---:|---:|---:|---:|---:|
| Wrapper | 155,844 | 25,556 | 109.927 | 49.847 | 4.012 |
| Bare array | 24,328 | 25,920 | 69.626 | 60.149 | 6.026 |

Wrapper RSS falls **83.6%** (6.10-fold). The already-streaming bare-array baseline
instead gains **1,592 KiB / 6.5%**; its complete negative result is retained here.
These are single runs during competing builds: wall time is confounded by host
load and is not a statistical performance claim. Kernel `wait4` measures the
CLI process, including source capture, interpretation, audit and JSON output;
filesystem page cache and the later Python verification are outside its RSS.

Both versions independently pass stored-source SHA-256/size checks, all 128 raw
metadata-payload checks, message/conversation/Unicode counts, SQLite quick check
and foreign-key check. The after runs also verify all 64 source indices,
128 message pointers, 64 checkpoints and one complete source. Repeated after
imports report `already_imported=true` without changing IDs or counts.
The underlying temporary inputs/databases were removed after these receipts;
the generator reconstructs them exactly apart from elapsed-time fields.

## Reproduce

Build each pinned source in a separate checkout and external build directory:

```bash
cmake -S loom -B /tmp/loom-import-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release '-DCMAKE_C_FLAGS_RELEASE=-O0 -DNDEBUG' \
  '-DCMAKE_CXX_FLAGS_RELEASE=-O0 -DNDEBUG' -DLOOM_WERROR=ON \
  -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_BUILD_CLI=ON
cmake --build /tmp/loom-import-build --target loom -j2
```

Use the after checkout's evidence scripts with the two independent binaries:

```bash
python3 docs/reports/archive-import-2026-10-04-evidence/import/generate_provider_export.py \
  /tmp/wrapper.json --shape wrapper --conversations 64 --messages 2 \
  --payload-bytes 524288 --payload-site metadata
python3 docs/reports/archive-import-2026-10-04-evidence/import/measure_provider_import.py \
  /path/to/baseline/loom /tmp/wrapper.json /tmp/import-before --baseline \
  --source-commit 161cc22dfb84fe863389d6b90323bd44516a68dc
python3 docs/reports/archive-import-2026-10-04-evidence/import/measure_provider_import.py \
  /path/to/after/loom /tmp/wrapper.json /tmp/import-after --repeat \
  --source-commit f50d01653c0331eac283fc0d983775df3397052a
```

Repeat with `--shape array` and fresh result directories. `--timeout` is a caller
preset, not an engine cap. Explicit example prices are offline planning inputs;
no model is called. Linux/macOS and Python with `os.wait4` are required.

## Larger fixture

`fixture-2gib.json` records generation of a **2,148,031,323-byte** wrapper:
2,047 conversations, 4,094 messages, 243,420 normalized characters. It uses
1 MiB whitespace between conversations. This tests scanner traversal, not
realistic 2 GiB of stored message content. A generation receipt alone does not
prove that import ran. Any larger runtime receipt is documented separately.

```bash
python3 docs/reports/archive-import-2026-10-04-evidence/import/generate_provider_export.py \
  /tmp/large-wrapper.json --shape wrapper --min-bytes 2147483648 \
  --conversations 0 --messages 2 --payload-bytes 1048576 --payload-site whitespace
```

Default capture requires space for the input, temporary blob copy, retained blob
and database; plan capacity before running, particularly on shared workspaces.
