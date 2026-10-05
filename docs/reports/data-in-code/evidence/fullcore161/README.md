# Full-core offline parity against 161cc22

The first three baseline captures are preserved in `../baseline161/` with their original compiler/source/library receipts. `receipt-before.json` adds exact-161 captures for net/model, utility (six environment cases), and regex, compiled against the same frozen full library and headers. Source snapshots, full JSON, stderr, compile logs, and exact original compile/run commands are preserved. Refactoring the replay runner does not rewrite those receipts or raw before files.

The actual whole static core archive is linked directly; there are no substituted objects or older-library fallback. The semantics/graph/materialize probe includes Runtime and four products, without `LOOM_PARITY_CORE_ONLY`. All inputs are synthetic. Requests use ScriptedTransport; Runtime background workers are disabled. Normalization of temp/random/time details is documented in the frozen probe sources. Parity complements full CTest.

## Replay in the original layout

After a complete current build, run from any directory:

```sh
python3 /path/to/chatadhd/docs/reports/data-in-code/evidence/fullcore161/capture_fullcore.py --stage after
```

The runner discovers the repository from its own ancestors. Defaults are siblings of the checkout: `<repo-name>-baseline`, `baseline-build`, and `current-build`. Probe executables and isolated run directories default to the repository's parent. The existing explicit `--current-build` commands remain valid. No workspace identifier is fixed in the runner.

## Replay in a relocated workspace

Use a copied checkout/evidence directory when regenerating captures, to retain the archived raw proof. Build the frozen checkout at `161cc22dfb84fe863389d6b90323bd44516a68dc` and the current checkout separately with the compiler/options recorded in their receipts. The current build must include the whole core, CLI, server and shared library before the after capture. Pass paths explicitly when directory names differ:

```sh
python3 "$PROFILE_REPLAY_REPO/docs/reports/data-in-code/evidence/fullcore161/capture_fullcore.py" \
  --stage baseline-extra \
  --repo-root "$PROFILE_REPLAY_REPO" \
  --baseline-root "$PROFILE_REPLAY_BASELINE_REPO" \
  --baseline-build "$PROFILE_REPLAY_BASELINE_BUILD" \
  --scratch-root "$PROFILE_REPLAY_SCRATCH"

python3 "$PROFILE_REPLAY_REPO/docs/reports/data-in-code/evidence/fullcore161/capture_fullcore.py" \
  --stage after \
  --repo-root "$PROFILE_REPLAY_REPO" \
  --baseline-root "$PROFILE_REPLAY_BASELINE_REPO" \
  --baseline-build "$PROFILE_REPLAY_BASELINE_BUILD" \
  --current-build "$PROFILE_REPLAY_CURRENT_BUILD" \
  --scratch-root "$PROFILE_REPLAY_SCRATCH"
```

`baseline-extra` regenerates only net/model, util and regex baseline captures. The first three immutable baseline receipts remain under `../baseline161/`; the after runner verifies their frozen probe-source hashes and reuses their full raw output. Each new receipt records its actual workspace paths, archive/source/script hashes and exact compiler/run commands. Run all six probes together for a single `receipt-after.json`, or use `--only NAME` for independent per-probe receipts. A missing full consumer artifact or a changed frozen probe is an error.

Final current captures completed: [RESULTS.md](RESULTS.md), [receipt-after.json](receipt-after.json), [summary.json](summary.json). All6 old-API probes are byte-identical against the actual whole current library (1,208,469 bytes per side); the before captures and original commands remain unchanged.
