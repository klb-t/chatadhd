# Preserved first W3 runtime-preset fixture compile failure

Archive only; do not integrate this evidence into main. Source parent is
8fb24f4268c02e4caa2066f06e59439e6d90fd4c (product base 0a81480).
The original actual W11/W12 fixture failed to compile at
`CHECK(selected.hash == authoritative["hash"])`: doctest expression decomposition
cannot compare that std::string with a Json value. The selected source uses an
explicit std::string conversion. This was a fixture error, not a runtime finding.

Original complete commands/logs, pinned actual W11/W12 implementation and
all 18 recorded checkout sources are preserved. No object/executable is required
for reconstruction. `snapshot_reconstruction.json` identifies the initial live
source drift and the frozen snapshots reconstructed against recorded hashes.
Utility implementation files are preserved as evidence only, not edited or selected.

From a checkout of this archive or any descendant retaining its base headers:

```sh
python3 docs/reports/chat-selector-2-2026-10-05-negative/helper-compile/replay_compile.py --repo . --output /tmp/w3-negative-replay
```

The script verifies frozen source hashes, compiles the exact failing fixture
against frozen W11/W12 headers and the base public dependencies, retains full
diagnostics and passes only if compilation reproduces the same failing check.
It does not invoke a provider or paid operation. Historical absolute command
paths remain in the original manifest; replay records its relocated command.

## Do wątku N

- **9:** retain this complete negative on archive; select only the corrected
  fixture and final positive receipts from gpt/chat-selector-2-2026-10-05.
