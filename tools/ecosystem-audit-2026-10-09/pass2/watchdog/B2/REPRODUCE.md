# Focused B2 consumer verification

```sh
python3 tools/ecosystem-audit-2026-10-09/pass2/watchdog/B2/run.py \
  --repo /path/to/B2-checkout --sha 384c5e1686cd3a58a7d89a8a6813a18c764f697d \
  --base-repo /path/to/B-checkout --base-sha ddcaeaf7a25b70f0d4dc2e0eed2466016233eeb8 \
  --base-build /path/to/B-native-build --scratch /path/to/empty-scratch \
  --out /path/to/receipt.json
```

The baseline build must be a proven build from the pinned B source. Requires C++20, the baseline CMake compile_commands/Ninja dependency log and its three native archives. No full build or paid service is invoked. Six affected production TUs are rebuilt, a baseline executable creates historical SQLite state, and the B2 executable uses the real consumer on that same state. The runner refuses unexpected product deltas or a reused fixture database. Exit1 means an acceptance failure; a successful reproduction never counts as product acceptance.

Optional `--reuse-objects /path/to/object-manifest.json` reuses only SHA/source/object-hash-verified six-TU artifacts for changes to this independent driver. Final fixture corrections used this option; previous compile commands and artifact hashes remain in receipts. Omit it for a clean reproduction. The English diagnostic-based migration criterion is explicitly limited to the existing native diagnostic contract; a new structured/localized migration contract must get an adapter rather than be forced to use these words.
