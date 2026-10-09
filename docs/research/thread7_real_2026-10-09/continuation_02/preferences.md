# Source preference annotations — continuation 02

The existing 12-family panel was reviewed in two passes in this session. The
inventory covers 770 user messages. There are 258 candidate records: 174 accepted
as scoped historical instructions/preferences, 59 rejected and 25 uncertain,
with 61 explicit relations between records. These are annotation records,
including revisions and repeated statements, not 174 independent or permanent
preferences. Ten families have accepted evidence; two have none accepted.

Pass 1 selected literal evidence from source turns. Pass 2 checked attribution,
interpretation and scope. The two session agents cross-reviewed the candidate
records; this is not independent adjudication, owner confirmation, or a recall
measurement. All nonquoted user text was read. One long external podcast
transcript was excluded as quotation after checking its boundaries and speaker
context; its internal claims were not semantically adjudicated in full. Nonuser
turns are surrounding context, never declared owner preferences. The historical
family split remains unchanged and researcher exposure is explicit.

Private records retain the exact quote, literal field pointer and codepoint
range, quote/message/source hashes, speaker, native timestamp, interpretation,
scope, tentative/declaration/correction/revocation/quotation status, relations and
individual uncertainty reasons. No global profile is inferred from a one-message
instruction. Quoted third-party restrictions and assistant suggestions are not
promoted. Factual corrections remain evidence but are not automatically preferences.

The new validator in `experiment_preference_annotations_v1.py` checks both
passes against the actual native messages, complete family/user-node inventory,
relation references and the reviewed decision set. Accepted nonuser statements,
unendorsed quotations, hypotheses, stale hashes and unreviewed records fail.
This is annotation validation, not a second profile engine or semantic judge.

Preparation producer version 3 adds a review-bound source-evidence path. An empty
known-evidence result is explicit; it does not assert that no preference exists.
Uncertain candidates keep individual private reasons. Only a literal annotation
whose node is in the actual context view can enter a request. The request excludes
full-family interpretations, later supersession times, hidden relations and
semantic scope labels; it retains a source-utterance anchor. Consequently these
requests test source-evidence inclusion, **not active personalization**. Current
applicability and temporal resolution remain unknown. The complete reviewed scope
and relations remain in the private dossier.

Only the 324 changed `source_explicit` combinations were regenerated. The 648
`none`/`selected_profile` rows from the preceding release are inherited unchanged.
The composite still has 972 source/task/variant intentions: 648 complete
preparations and 324 with a primary request ready but a separate extraction call
pending. All 972 primary bodies exist. The previous 297 missing-annotation
intentions now have reviewed evidence or an explicit empty known-evidence result;
no preference was fabricated to fill them. Of the 324 changed rows, 126 have an
empty visible accepted-evidence set, 72 retain visible uncertainty, and 162 omit
annotations because their source nodes are outside the view.

No model was called. New API cost is USD 0. Model answer quality, the benefit of
personalization, and independent validation remain unmeasured. Previous freezes,
request bodies and results remain unchanged. The first new release attempt was
interrupted when a posterior-metadata leakage risk was identified; its partial
files, producer snapshots and status are preserved privately, never dispatched.
The final `expanded-v3` and `COMPOSITION-v3.json` are the resume inputs.

Verification: **59/59**, zero skipped (23 new annotation mechanics tests plus
36 existing context tests). Coverage includes exact quote/codepoint binding,
wrong speaker, quotation/hypothesis rejection, missing review, inventory drift,
explicit reviewed-empty/unknown, source-view filtering, posterior/future-scope
leak prevention and private release manifests. The first release-fixture attempt
used the wrong list key (`items` instead of the existing `values` contract); its
failed log is preserved and the fixture was corrected. No quality gate changed.

Recheck from repository root:

```bash
TMPDIR=/var/tmp PYTHONPATH=.:loom/tools/structure python3 -m unittest \
  loom.tools.structure.test_experiment_preference_annotations_v1 \
  loom.tools.structure.test_experiment_context_preparation_v1 -v \
  > /var/tmp/thread7-preferences-recheck.log 2>&1
```

Rebuild private changed preparations with the public CLI and checkpoint inputs:

```bash
python3 -m loom.tools.structure.experiment_preference_annotations_v1 \
  --sources "$PREVIOUS/sources.json" \
  --evidence "$PRIVATE/evidence-pass1-v2.json" \
  --review "$PRIVATE/interpretation-pass2-v2.json" \
  --tasks "$PREVIOUS/tasks.json" \
  --plan "$PRIVATE/plan-source-explicit-v3.json" \
  --profiles "$PREVIOUS/profiles.json" \
  --output "$NEW_PRIVATE_OUTPUT"
```

`$NEW_PRIVATE_OUTPUT` must be a new private directory outside a Git tree. Version 2
remains reproducible from its original `producer.py` and manifest; the new producer
retains the unreviewed-source pending behavior. The source-explicit study's
interpretation changed and is versioned rather than silently assigned to old rows.
A separate mechanics-only comparison against the pinned version-2 producer
verified identical legacy request bodies and lifecycle status for all 81
representation/resolution/response/preference combinations. Versioned provenance
IDs intentionally differ; no real source preparation was repeated for this check.
Machine receipts and protocol are in `preferences-receipt.json` and
`preferences-protocol.json`. Raw quotes, node identities and source titles are
only in the private checkpoint.
