# Native packet continuation receipt

Implementation: `935c569886e7c37cdd79eb80b13b455b549b3714`, on main base
`161cc22`. `manifest.json` binds tested source/data, binaries and every retained
text receipt. The initial increment's receipt remains in the sibling
`2026-10-04/` directory; these files do not replace it.

Final unchanged full CTest: **110/110**, serial, **395.91 seconds**.
Separate real-policy overlay: **14/14**, pinned W2 `03b0c4e`, compiler warnings
as errors. Native packet: **4 cases / 24 assertions**; packet FFI **16/16**;
store **23/23**; HTTP **11/11**; audit **9/9 rejected**; open method-schema
checks **14/14**. Five original pinned compiler outputs remain exact.
All inputs are synthetic. No provider calls, paid calls or Actions runs.

Reproduction commands are in `../../README.md`. The method construction test
uses existing native packet/store APIs. W3 production method-registry adoption
and the joint execution regression remain open; a data-pattern fixture is not
evidence that that adapter already exists.

Failed attempts are retained with their original outcomes: memory/disk build
failures, research timeouts, the stale HTTP receipt-marker assertion and the
first method-test helper error. Original build/CTest text is unedited, including
its emitted trailing spaces; receipt hashes cover those original bytes.
The standalone research diagnostics passed all
859 structure and 209 contract cases; only `ctest.txt` is the full gate receipt.
The supplemental unfiltered doctest invocation ran 663 cases in one process and
failed the import absolute-RSS assertion with inherited VmHWM 187292 kB and no
increase. CTest's unchanged isolated import group passed in 36.26 seconds.

`audit-before-transcript.json` contains saved pre-fix observations, not a rerun
of the final suite against the old library. The previous commit `3caa6b4` and
portable audit runner remain available for a separate-worktree reproduction.
`unresolved-replay-before/` preserves the independent actual W2/W4 overlay,
original result, exact source/artifact hashes and runner. Its old C API dispatch
is pinned; the linked local core provenance is recorded separately rather than
claimed to be a fully rebased old build. W9's full negative archive also remains
at `archive/2026-10-04/integrator-packet-unresolved-replay`.

`thin-archive.json` records the same 129 compiled objects and the pre-conversion
regular archive hash. After the successful build, only the local static archive
cache changed to GNU thin format to release disk space. Tested shared/server
binaries and repository build presets were unchanged.
