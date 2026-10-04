# Catalog selection — frozen evaluation protocol (2026-10-04)

Scope: thread 6 only, based on public main
`161cc22dfb84fe863389d6b90323bd44516a68dc`.

1. Measure the existing native catalog on the public `synthetic_dev` exports:
   45 relevant conversations, 5 lexical traps and 15 generic conversations.
   Report the three auxiliary documents separately. Preserve every decision,
   source/binary hash, recall, precision, AUC, hits@45 and lexical-shadow IDs.
2. Add a source/profile-bound supplied-vector channel before selection. Evaluate
   it with explicitly synthetic vectors as a mechanism test, never as measured
   model quality. Disabled/missing input must preserve the measured default.
   Owner overrides retain precedence. Invalid input must not mutate scores or
   links. Channel/model/query/vector/configuration hashes remain inspectable.
3. Investigate a generic nearest-seed local-vector prototype on DEV only. Keep
   a negative result reproducible on the branch; do not promote an approach
   that worsens tracked precision, recall, ranking or lexical-trap rejection.
   Do not add fixture aliases, project IDs or answer-label-derived vectors to
   production policy. Local TF-IDF is labeled as such, rather than dense-model
   semantics. Quality gates remain unchanged.
4. Run the complete CTest suite without manually supplying PYTHONPATH or TMPDIR.
   Additional owned-path native regressions are executed separately because
   this thread does not own the repository's test registration files.
5. Freeze implementation and settings before opening the blind-catalog branch
   `wip/worktree-agent-a342fccb481c4116c`. Inspect/evaluate it once at the end,
   mark the retained result **pierwsze spojrzenie**, and make no subsequent
   relevance tuning. If its protocol or unavailable infrastructure prevents
   evaluation, record that boundary instead of manufacturing a result.

`eval/real-holdout-key` is never read. There are no paid provider calls,
private exports or GitHub Actions runs. Thread 9 owns main integration and
STATE updates; the concurrent application-profile changes remain intact.
