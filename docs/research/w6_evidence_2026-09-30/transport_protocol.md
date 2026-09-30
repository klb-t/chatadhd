# W6 independent native transport protocol

Frozen before first experimental execution, 2026-09-30. Author: independent
transport validation agent; no production changes. Baseline source:
`b118c80e981c08ec6d7f9ab6aacc177979186cf2`.

Instrument: new Python standard-library HTTP capture server, native `loom-server`
from the independently built continuation baseline (`verification/native-dev`),
REST client and on-disk SQLite inspection. Record binary and library SHA256,
source SHA, exact invocation, initial outcomes and full synthetic provider bodies.
Only loopback transport, fresh generated fixture/data directories and a dummy key.
No owner exports, credentials, paid calls or sealed material.

Predeclared expectations:

1. Offline knowledge run over a new synthetic source completes without provider
   requests. Explicit knowledge, memory and trace survive actual HTTP dispatch.
2. Stored and returned compiled message arrays exactly equal the independently
   captured request array; compact UTF-8 serialization reproduces trace SHA256.
   This checks array equality, not whole-body equality or provider comprehension.
3. Second turn contains each prior active message once. Explicit history/memory/
   graph exclusions remove those sources without erasing persisted messages.
4. Trace opt-out still sends the intended message but returns/persists no trace.
5. HTTP 500 after provider capture retains the compiled trace and records an
   error. Missing API key retains compilation trace but produces zero provider
   calls. Missing knowledge run produces an error and zero provider calls.
6. Explicit provider streaming is independently captured; stream result retains
   matching trace. The fake provider returns fixed content, not an LLM judgment.
7. Saved traces remain exactly equal after stopping and reopening the native
   process, then after semantic graph reindex. Reindex causes zero provider calls
   with semantic model disabled. Compare both success and failure traces.

Every check records its denominator; earlier failures remain in their original
attempt directory. Infrastructure/harness defects are labeled separately from
production failures, and amendments never replace first output. No threshold
tuning. Limits: synthetic local protocol evidence, one host, ordinary stop/start
not crash durability, and no semantic-quality or real-export claim.
