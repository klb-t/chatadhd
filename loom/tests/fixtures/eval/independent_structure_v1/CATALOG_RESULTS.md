# Production catalog baseline on independent source text

The original native implementation at `33fb083` was measured through its
public catalog C ABI: scan, score, select, query and preview. Each split used
a fresh disposable runtime and only its generated `conversations.json`.
There was no import, model call, network call, policy change or threshold
adjustment. All 100 conversations received decisions.

The task labels are independent axes, while the production policy is a
single broader self-discovery policy. The table measures direct project
membership only and must not be read as an apples-to-apples replacement for
the existing 65-conversation development benchmark.

| Direct-self goal | Development | Validation |
| --- | ---: | ---: |
| True positives | 5 | 5 |
| False positives | 2 | 2 |
| False negatives | 3 | 4 |
| True negatives | 36 | 37 |
| Gold ambiguous, excluded and counted | 4 | 2 |
| Recall | 5/8 = 0.625 | 5/9 = 0.556 |
| Precision | 5/7 = 0.714 | 5/7 = 0.714 |

Genuine inflected aliases and unnamed continuations are still missed.
False-positive cases include quoted unrelated namesakes, alternate short-name
meanings and late unrelated technical context authorizing an earlier alias.
The topic-local experiment's success on some local distractors does not fix
the production catalog: that code was not integrated or changed in this run.

For the narrower labeled self-discovery union, recall is 5/9 development and
5/10 validation, with 36 and 34 abstained labels respectively. Generic
philosophy questions and other conceptual material remain useful without
implying membership in the named project. All 100 conversations were designed
to be useful for some structural analysis, yet the production selector
chooses 14. This demonstrates a consumer-goal mismatch; it is not evidence
that the remaining source material should be discarded or that the existing
catalog should be made indiscriminate.

`production_catalog_report.json` records every decision, score feature,
selection reason, split/category failure, binary SHA256, source export hashes,
and the 41-file data-pack manifest. It also reports the four separate goals,
without tuning a policy for any of them. Philosophical generalization from
specific project choices is a distinct inference task, so those goal-specific
counts should not be promoted to an overall "relevance accuracy" number.

Reproduce from `loom/` with the desired native baseline:

```sh
python tools/eval/independent_cases.py run-catalog --library build/dev/libloom.so.0.1.0 --output /tmp/independent-catalog-report.json
```

The original development recall gate and the unread real holdout remain
separate and unchanged. This report uses no examples or answer labels from
either dataset.

## Local-context patch comparison

The same frozen source suite was rerun after the native local-context/title/
version/provenance patch. **All 100 selection decisions and all 100 scores are
unchanged.** The table above therefore also describes this patched run.
This suite demonstrates no selection-quality improvement from that patch;
passing mechanism regressions must be reported separately.

- Baseline library SHA256:
  `f4cb30c74444a59ead452448d7bf135678091c39b6e9af2ca6d32a6a4c11c77f`.
- Patched library SHA256:
  `a9c9368d39496ffbffeaeb7062511f858e16fcb78d57662552e248da668d0cba`.
- The 41-file data-pack manifest is unchanged.
- `after_local_context_catalog_report.json` includes the baseline reference
  and an empty score/selection change list. No threshold was tuned.
