# W2 — Retrieval, scope and detail

## In-progress checkpoint

- Owner: independent W2 conversation, requested directly on 2026-09-30.
- base_sha: `b118c80e981c08ec6d7f9ab6aacc177979186cf2`.
- Remote integration ref checked through GitHub API and matched the available local source.
- Branch: `gpt/w2-retrieval-2026-09-30`.
- Scope: `loom/src/context/`, dedicated context tests, new context headers, additive request/channel declarations in `context_engine.h`, this receipt.
- No cross-conversation integrator messaging channel is available. Header additions are isolated for integration review. W1 owns chat; no chat/web/model-ledger/shared STATE/index edits.
- W1 checkpoint `dfa28a7d95e18c35a480081c53503f970e20ad6e` inspected locally: W1 changes ChatOptions and chat files only. W2 consumes a caller-supplied retrieval projection; it does not compile or replace ActiveTaskSpec.
- Native C ABI `loom_context_build` is the first consumer. Existing chat option whitelist requires a separate small W1/ROOT integration change for new request keys.
- Parallel lanes: selection diagnostics and dependency identity; per-thesis selection; explicit vector channel and lexical shadow; independent native verification; coordinator integration/review.
- No sealed validation or private exports read. No model calls or paid spending.

## Planned verification (before implementation)

Retain old context/controls/chat/C ABI gates. Add adversarial native cases for per-thesis scope versus detail, required counter evidence and missing support, global item-budget accounting, duplicate sources/dependency union, query failure/cap visibility, lexical-shadow omissions, unavailable versus zero vector score, and retrieval outside graph reach. Compare graph, TF-IDF, lexical and their union on labelled synthetic DEV; evidence retrieval, precision/ranking and answer correctness are separate. No semantic generalization claim from a deterministic fixture.

Verification and implementation SHA pending. This checkpoint reports work in progress, not completed behavior.
