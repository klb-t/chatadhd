# Full negative evidence, W3 second increment

Public synthetic inputs only. These attempts are deliberately absent from the selected branch.

* `chat-profile-order`: actual 41/42 checks against core b4e9b627; the failed expectation
  used the opposite JSON object insertion order. Full fixture, six consumed inputs,
  source attestation, options, commands and output are retained. Replay by building
  c0e6d0ba on base 66da570d (Debug -O0 -g0, WERROR, vendored SQLite), substituting the
  frozen negative fixture/inputs, then running its recorded compile/link/run commands.
  No production correction was needed. Its old provider_calls=0 is the runner's declared
  offline flag, not a measured count of mock HTTP callbacks. Later positive fixtures measure two.
* `guard-layout`: full original 115-entry JUnit/manifest and unmodified W8 checker,
  policy and schema. Running the flattened checker with --policy in this directory
  fails because its sibling-schema lookup expects the .github/scripts layout.
  No CTest failure or policy relaxation occurred. Recreate that layout to run the
  unchanged guard successfully. The archived receipt retains the original missing path.
* `build-race`: complete retained logs, receipts and exact binary dependency-ledger
  snapshot (base64). Old -j1/-g and new -j2/-g0 writers overlapped in one directory.
  The recorded kill attempt did not terminate the old process due to PID namespaces;
  it finished naturally. The source overlay is pinned to c0e6d0ba; its base is 66da570d.
  The scheduling race is nondeterministic. Its ledger corruption has an independent
  deterministic parser replay in `isolated-ledger-replay`: decode original base64 into
  a fresh directory as .ninja_deps, create the supplied minimal build.ninja, then run
  ninja -t deps. This directory has no relationship to the active product build.

The earlier OOM attempt's complete raw log was overwritten before preservation;
we do not claim this bundle reproduces that lost attempt. No private data or paid calls.
