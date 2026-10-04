# Integrator baseline execution failures — 2026-10-04

Native code and tests: public main `161cc22dfb84fe863389d6b90323bd44516a68dc`.
The archive parent adds review documents only. No private source or paid calls.

The first complete serial CTest run returned **106/108 passed**, 907.42 seconds.
`research.structure` and `research.contracts` each exceeded their original
60-second timeout. A separate two-entry retry with an owned tmpfs temporary
directory also exceeded 60 seconds for both. Timeouts are not passing coverage
or a demonstrated assertion defect. They have not been waived.

[Full evidence.zip](integrator-baseline-timeouts-2026-10-04/evidence.zip) retains
both full JUnit files, complete first CTest output and LastTest.log, discovery
manifest, failed executed-case guard output, source/binary hashes, web output
and all captured build attempts. Its SHA256.json hashes each original file.
The complete guard correctly rejects both failed entries.

Build attempts include disk exhaustion and a linker killed by signal9. The
successful baseline build uses bundled SQLite, server/CLI/shared enabled,
`-O0 -g0` and low-memory GNU linker options. No source, assertion, threshold
or timeout was changed. This is unsuccessful execution evidence, not an
accepted product experiment. Other lanes' files/processes were not modified.

## Replay unchanged gates

Substitute installed CMake/Ninja/Python paths. Python needs the existing
structure/contracts requirements. Run from the repository root:

```sh
cmake --preset dev -S loom -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_BUILD_SERVER=ON \
  -DCMAKE_CXX_FLAGS_DEBUG='-O0 -g0' -DCMAKE_C_FLAGS_DEBUG='-O0 -g0' \
  -DCMAKE_EXE_LINKER_FLAGS='-Wl,--no-keep-memory -Wl,--reduce-memory-overheads'
cmake --build loom/build/dev -j1
env -u PYTHONPATH -u TMPDIR ctest --test-dir loom/build/dev -j1 \
  --output-on-failure --no-tests=error --test-output-size-passed 10485760 \
  --test-output-size-failed 10485760 --output-junit /absolute/path/ctest.xml
```

The timeouts depend on host conditions; reproduction commands preserve the
same gate without promising identical timing. During diagnosis, the shared
8-core cgroup was heavily throttled and memory approached its8GiB ceiling;
multiple other compiler/test processes were visible. That observation does
not assign individual OOM events or prove a unique cause. A request to raise
only an owned process's priority was denied by the OS; no escalation followed.

## Do wątku 8

Preserve original limits and internal-case checks. Distinguish environmental
execution failures from assertion failures and accepted coverage. A later
successful full run needs its own pinned source/binary receipt; this archive
does not substitute for it.
