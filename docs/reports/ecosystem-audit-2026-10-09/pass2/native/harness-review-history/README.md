# Superseded audit assumptions — not current product verdicts

The v1 audit treated an arbitrary new key in `onboarding.settings` as unsupported and required its rejection. Peer review found this assumption too broad: canonical data explicitly sets `additionalProperties=true`; effective layer resolution does not promise that every extension controls `ProfileSession`.

A discriminating real-native probe changed the runtime definition, through `OnboardingStore.update_pack`, to `additionalProperties=false`. The same key then produces explicit `runtime_profile.available=false` with `unknown executable setting`, and the actual `model_request` consumer returns that error. This is a valid preserve-data-but-disable-execution alternative. V1's reject-only test would falsely fail it.

The current finding and runner therefore reject the original suspected violation in this scoped case. No product code was changed and no product gate was weakened. The synthetic control was also improved: `preference_mode=candidate` now feeds an actual `propose` action and its presentation is checked; v1 only checked the persisted value.

These receipts remain solely to expose the audit correction. They must not contribute current finding counts or acceptance-failure counts. Four B-POLICY001 baseline failures and four independent B fixes were unaffected.
