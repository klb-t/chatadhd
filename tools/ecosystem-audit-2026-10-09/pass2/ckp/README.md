# CKP pass 2 — independent consumer tests

Targets any accessible commit through mandatory `--repo` and `--sha`. It reads
blobs with `git show`; no product file is edited. `source-manifest.json` records
exact ranges and hashes. No host test connects to a provider or uses an API key.

```sh
python3 tools/ecosystem-audit-2026-10-09/pass2/ckp/bootstrap_deps.py --deps /tmp/ckp-audit-deps
python3 tools/ecosystem-audit-2026-10-09/pass2/ckp/run.py \
  --repo /path/to/Custom-Keyboard-Pro \
  --sha 192f820f2d8c653768237e1bff3a1a6d950d69c0 \
  --deps /tmp/ckp-audit-deps --workdir /tmp/ckp-audit-build \
  --out /tmp/ckp-audit-results/host-receipt.json --mode all
python3 tools/ecosystem-audit-2026-10-09/pass2/ckp/sql_gate.py \
  --repo /path/to/Custom-Keyboard-Pro \
  --sha 192f820f2d8c653768237e1bff3a1a6d950d69c0 \
  --out /tmp/ckp-audit-results/sql-receipt.json
```

`--mode reproduction` runs A assertions (observed bugs); their PASS proves the
bug, **not compliance**. `--mode acceptance` runs B assertions. `all` executes
both; any failing assertion gives exit 1. Compilation failure gives exit 2, never
success. Recorded base: 25 host checks = 17 PASS / 8 acceptance FAIL; 6 SQL checks
PASS. There are 10 A reproductions and 15 B checks; B = 7 PASS / 8 FAIL.

## What executes

Whole unmodified Kotlin files: AiTasks, ProviderCatalog, Capabilities, Discovery,
Model, Planner, Transforms, Command, NetLines, NetListener.
Four mechanisms are extracted unchanged from pinned source into host shells:
`CustomKeyboardIme.runAiTask`, `EditorController.replaceSelectionOrAll`,
`ConvertRunner.names`, `ConvertRunner.convert`. These are actual method bodies,
not a test author's reimplementation. If seams move incompatibly, compilation
fails and an adapter must be reviewed before comparing another SHA.

Android Context/assets and Handler queue are doubles. Settings is a minimal input
fixture, not the production codec. AiClient.complete is a suspended transport
capture; AiConfig is not under test here. InputConnection and editor primitive
operations are recording doubles. Conversion IO/encoding/execution functions
record the selected implementation instead of processing media. The real Planner
and route selection method run. JVM SecurityManager rejects socket connections.
Synthetic marker text only; no private source content or real credential is used.

Consequently this proves control-flow behavior in exact methods, not Android
lifecycle scheduling, real editor/plugin/OEM behavior, full media conversion,
request-body AiConfig wiring, encrypted persistence, nor an end-to-end phone test.
CKP-A-012 moves from hypothesis to **host reproduction confirmed**, with Android
verification still open. Network queue revocation is tested separately and passes.

Planner weights change the actual planner ranking. This does **not** prove that
Settings/UI supplies those weights to ConvertRunner. That missing binding remains
CKP-A-003; B must expose a profile input consumed by the existing runner. Unknown
cost scoring currently equates null with MEDIUM, while the original null metadata
survives; equal scores alone are not used as a failed acceptance criterion.

SQL checks extract migration statements and DAO queries, execute them with real
SQLite, then close/reopen a file. They verify additive v1→v5 migration, snapshot
clear, pin protection, durable tombstone query, operation-bound undo. They do not
execute Room/KSP, the Kotlin transaction coordinator, Android FileProvider, or
real filesystem cleanup. Existing repository tests cover those different layers;
new full Android gate outcome is recorded separately.
