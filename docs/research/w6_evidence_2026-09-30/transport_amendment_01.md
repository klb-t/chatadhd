# Transport protocol amendment 01 (before attempt 02)

Attempt 01 reports 68/68 mechanical checks, but manual trace review finds the
knowledge prompt empty (`targets:[]`, zero selected items). This is a harness
coverage defect; 68 passes must not be interpreted as a nonempty knowledge
context delivery result. Initial files and the exact harness are preserved in
`transport_attempt_01/`. No product edits or fixture wording changes.

Attempt 02 will explicitly supply entity IDs obtained through native knowledge
query as selection targets, allow one graph hop, and add four checks: nonempty
items, nonempty prompt, exact synthetic source requirement in prompt, and exact
prompt in dispatched system messages. This exercises the existing documented
target-based selection API; it does not test automatic free-text resolution.

The attempt-01 `environment.base_sha` is the harness checkout HEAD (a concurrent
documentation commit), not proof of the binary's source. Amendment separates
`harness_checkout_sha` and `native_source_checkout_sha`, records native source
status, and retains binary/library hashes. Native source is the clean b118c80
baseline; previously compiled binaries are reused read-only.
