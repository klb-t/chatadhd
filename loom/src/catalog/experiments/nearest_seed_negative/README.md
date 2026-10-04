# Rejected nearest-seed catalog prototype

Archive-only evidence. This experiment is **not part of production scoring**.
It reads the stored public fictional `synthetic_dev` baseline and makes no
model, provider or native-library calls. No independent, blind, holdout or
real archive was used.

| Measurement | Relevant selected | Selected noise | Status |
|---|---:|---:|---|
| Frozen native baseline | 31/45 | 0/20 | Comparison source |
| Corrected nearest-seed prototype, reference percentile 75 | 30/45 | 0/20 | Rejected: recall regressed |
| Initial attempt, reference percentile 50 | 32/45 | 0/20 | Invalid comparison: different normalization preset |

The corrected prototype rescued `lk-03-deadline-computed` and `nf-02-storage`,
but lost `rt-03-keyframes`, `wd-02-build-decision` and `wd-05-desktop-notify`.
Its net loss of one relevant conversation fails the no-regression gate.
Three auxiliary project/memory documents remain outside the 65 labeled
conversation denominator; they participate in the stored vector corpus.

## Exact portable replay

From the repository checkout containing this archived experiment:

```bash
python3 loom/src/catalog/experiments/nearest_seed_negative/replay.py \
  --baseline loom/src/catalog/experiments/nearest_seed_negative/inputs/baseline.json \
  --output /tmp/catalog-nearest-seed-replay.json
```

The baseline argument is optional and defaults to the bundled input. Output
contains frozen source/binary/fixture hashes, the recorded catalog policy,
all prototype rows and explicit promotion gates. The runner asserts equality
with **every row and the full summary** in `evidence/corrected-first.json`.
It does not fit thresholds or provide a tuning switch. The corrected replay
must report 30/45, zero selected noise, and `promotion: rejected`.

## What was actually computed

The recipe reconstructs sublinear-TF/IDF word and character 4-gram vectors from
the baseline's stored top-K sketches. Its 20 seeds are baseline-relevant rows
with a positive combined identity/principle feature and no negative-context
feature. For each unit, it takes the largest cosine to another seed, separately
in the two lexical vector spaces. A seed's own vector is excluded.

The recorded floor percentile 10, reference percentile 75 and cap 1.5 normalize
these cosines. The recipe replaces the baseline's two semantic contributions
in the logit of its rounded final score, retaining their historical weights
1.5 and 2. It uses the historical prototype's simplified relevant/owner-candidate
selection. Native feedback, links and the complete selection-rule interpreter
are not rerun. This is a post-hoc DEV ablation, not an implemented alternative
catalog engine or a measurement of model embedding quality.

## Preserved first results

- `inputs/baseline.json` is the exact native baseline receipt, including
  sketches, profiles, input configuration and source hashes.
- `evidence/corrected-first.json` retains the original corrected output bytes.
- `evidence/corrected-replay.json` records the portable replay and failed gate.
- `evidence/initial-invalid-ref50.json` retains the initial invalid output,
  including its separate complement summary 34/45. Both used the wrong
  reference percentile and establish no valid improvement against production.
- `source/original_prototype.py` preserves the historical script bytes and
  their original workspace paths. Use `replay.py` for portable execution.
- `manifest.json` pins every bundled file by SHA-256 and byte count.

The initial invalid result is preserved as evidence of the mistake; it is not
relabelled as a corrected run. All files belong only on the archive branch.
