# Explicit gold container selection

After collection of the 192 preregistered first responses, but before scoring, the coordinator found an input-layout mismatch: the hash-bound public gold file is an object containing a `cases` array; the original scorer accepted only a root array. The original source, policy, prepared manifest and request capsule remain preserved. No response, label, query, recipe, threshold, denominator or cost was changed, and no provider call was repeated.

The universal reader now accepts an explicit JSON pointer supplied at scoring time. The empty pointer retains the original root-array behaviour. This run uses `/cases`; it first verifies the exact original raw-file SHA, then records that pointer and the selected array SHA. Missing, ambiguous or non-array selections fail without fallback. This format correction was chosen without examining scores.

Frozen revised scorer: `9aec8378f5e51b78cf7a9bfcb02fbcb1a3d027e3471a0ebdef6ea27d5e8ea560`; revised tests: `7928da45ba50e8c486f2cf09ffa0d31fa842dce385985d6a231a2d356bc430bc`. Two independent fake-only reviews and 52 focused tests passed. The revised full gate passed 108/108 in 134.63 seconds, with 2347 unit cases plus two smoke suites and no skips. Complete source-bound gate evidence is published separately.

The prepaid policy `2a66aa4d…`, manifest `f92c1094…`, request source `1c5ef93c…` and replay capsule `cdc11ca5…` remain byte-identical. The original prepaid freeze continues to identify the original scorer. This separate post-collection note does not rewrite it. Full invented before/fixed failures are retained in the archive.
