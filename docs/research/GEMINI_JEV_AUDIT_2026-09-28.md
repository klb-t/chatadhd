# Gemini report audit: Jev and the two ADHD repositories

Checked 2026-09-28 against primary sources. Input: the complete attached report
“Wykorzystanie modelu klasyfikacyjnego jev i repozy....docx”, locally extracted as
`gemini_reports/report_2.txt`. This is a source audit and experiment proposal,
not a measured Jev integration. No provider inference, credentials, account
changes or paid calls were used. The protected owner holdout was not read.

**Useful direction:** add an optional, provider-neutral decision scorer for
retrieval and routing. Keep graph extraction, evidence validation, canonical
authority and user interface policy separate. The report identifies a real
product and useful patterns, but repeatedly upgrades limited evidence into
claims of semantic infallibility and cognitive understanding.

## Verified API and deployment facts

| Report claim | Verified correction and implication |
| --- | --- |
| Jev is a TypeSafe AI decision model | Confirmed. The documented endpoint is `POST https://api.typesafe.ai/v1/systemone`; this is not the existing OpenAI-compatible chat-completions protocol. [API](https://docs.typesafe.ai/api) |
| Noul returns a Boolean plus confidence | It returns a number from 0 to 1 representing the yes probability. It has no separate `confidence` field. Thresholding is application policy. [API](https://docs.typesafe.ai/api) |
| Choice classifies a document into a project | Choice returns one winning option and a distribution, with up to 255 options. A single Choice is unsuitable for overlapping project membership. [API](https://docs.typesafe.ai/api) |
| Score supplies an arbitrary continuous judgment | Score averages probability mass over 2–10 ordered rubric levels. It is a rubric score, not a physical measurement or proof. [API](https://docs.typesafe.ai/api) |
| Confidence is the probability that the answer is correct | For Choice/Score it summarizes distribution concentration; measure empirical correctness separately. [Confidence](https://docs.typesafe.ai/confidence) |
| State can contain 64k tokens and questions another 32k | Incorrect. The documented budget is 64k for state plus all questions, and 32k for state plus the longest question. [Models](https://docs.typesafe.ai/models) |
| $0.042 per million input tokens; output free | Confirmed current published price, equivalent to $42/billion. State and questions consume input; repeated calls still cost. [Models](https://docs.typesafe.ai/models) |
| Arbitrarily many questions in one pass | Questions share state evaluation and can run in parallel, but context/rate limits apply. The documented model is `jev-1.13.0`; record the returned version and pin it for an evaluation. [Models](https://docs.typesafe.ai/models) |
| It understands any uploaded material | Inputs are text or structured text, not raw images/audio/video. English is its strongest documented language; Polish quality requires separate measurement. [Models](https://docs.typesafe.ai/models) |

The provider's RLCD description is its training-method claim. It does not make
every classification logically true. The official limitation page explicitly
documents difficulties with numerical accuracy, indirect reasoning, irrelevant
long context and adversarial input. It also warns that independent questions
need not obey expected probability identities: Noul and equivalent Choice can
disagree; a proposition and its negation need not sum to one. Therefore graph
invariants, byte offsets, arithmetic and permission checks stay in code.
[Official Jev 1.13 limitations](https://docs.typesafe.ai/model-jaggedness/jev-1.13)

## What the numerical claims actually measure

The **193.6× faster / 444.6× cheaper** figures come from TypeSafe's own workflow
evaluation. Its launch post explicitly calls these high-end gains, describes
capabilities-team-authored workflows, and uses other models' reference
probabilities rather than independently established classification truth.
The post also states that its 0% figure is a schema guarantee, not an empirical
semantic-error rate. Neither figure predicts Loom latency or accuracy.
The report's per-example $0.06 / $0.12 / $0.0007 comparison was not verified in
the primary sources examined; exclude it from our cost case.
[Launch post](https://typesafe.ai/blog/introducing-system-one-models-and-jev),
[vendor evaluation site](https://evals.typesafe.ai/)

The **81.0% Choice / 72.2% Noul / 45.4% Score** figures describe interface usage
in a study of 2,170 public GitHub projects. These are neither accuracy nor
reliability estimates. Its 52% action-selection figure concerns purpose labels
within Simulation & Control, not all successful decisions.
[Jev in the Wild, arXiv:2609.30216v1](https://arxiv.org/html/2609.30216v1)

A newly available independent preprint supplies a relevant counterexample:
rebinding semantically loaded option names to fixed rubrics degraded its hosted
model's AUROC from **0.8146 to 0.5806**, while outputs remained type-correct.
This is a reported English benchmark at one point in time, not a Loom result
or a permanent measurement of the currently served Jev version. It motivates
tests of option naming/order and test–retest stability; it does not establish
that arbitrary opaque IDs universally fix classification.
[Type-Safe Is Not Error-Free, arXiv:2609.26758v1](https://arxiv.org/html/2609.26758v1)

## ADHD repositories: useful ideas, bounded evidence

**UditAkhourii/adhd exists.** It separates independent idea-generation branches
from criticism, clustering and expansion. Its author-run evaluation uses six
engineering problems, randomized answer order and a judge from the generator's
model family. Five wins out of six and +7.67 trap-detection points are reported;
**+0.83 is builder usefulness**, not objective accuracy. The documentation
acknowledges the small set, same-family judging and unproven human applicability.
Isolation prevents branches from reading each other's intermediate answers; it
does not eliminate shared training biases, framing effects or within-branch
anchoring. Different research branches plus independent evaluation remain a
reasonable Loom method, but these six results cannot validate our system.
[Repository](https://github.com/UditAkhourii/adhd),
[evaluation methodology](https://github.com/UditAkhourii/adhd/blob/main/documentation/evals.md)

**zgbrenner/adhd-and-47-tabs also exists.** It is a response-design skill: choose
an answer, action, artifact or project-update contract; preserve useful state;
make progress and resumption easier. Its instructions explicitly disclaim
diagnosis and put the user's requested depth ahead of low-friction defaults.
It is not a validated cognitive-state detector. Its bounded working-set idea
can be an optional focus view, not a global cap on Loom's simultaneous graph
views or the user's chosen parallel work.
[Repository](https://github.com/zgbrenner/adhd-and-47-tabs),
[author instructions](https://github.com/zgbrenner/adhd-and-47-tabs/blob/main/chatgpt-custom-gpt/INSTRUCTIONS.md)

The report's proposed inference of a user's mental state from three messages is
unsupported by these sources. Classify observable requests if useful—explicit
request to resume, change topic, summarize or inspect a result—and retain
uncertainty. Do not turn speculative psychology into hidden UI restrictions.
Neither repository was installed or executed during this audit.

## Proposed Loom experiment

The known catalog recall problem is missing links to unnamed project
continuations, while the existing source bytes remain present. See
[the local diagnosis](CATALOG_RECALL_DIAGNOSIS_2026-09-28.md). A scorer can help
rank a supplied antecedent candidate, but cannot retrieve an antecedent omitted
from its packet. Separate candidate-retrieval coverage from classification.

1. **Freeze an independent pilot before inference.** Author 60 new bilingual
   windows, 30 PL / 30 EN, grouped by conversation and semantic family; use
   20 for development and 40 for validation with no family overlap. Include
   explicit identity, unnamed continuation, shared vocabulary across projects,
   return after a topic change, two simultaneous projects, and genuinely
   unresolved antecedents. Record exact source spans, allowed context and
   acceptable multiple labels. Do not derive examples or thresholds from the
   existing recall gate's labels or the protected owner holdout.
2. **Hold candidate inputs constant.** Retrieve a bounded shortlist using
   existing lexical/graph signals, then evaluate four arms: unchanged baseline;
   available local scorer; the configured inexpensive generative model; Jev if
   separately configured. Use at most eight candidates per window. A missing
   correct candidate is a retrieval failure, not a classification rejection.
3. **Ask narrow, independent questions.** Score relevance to each candidate
   project; whether an explicit continuation refers to each prior observation;
   and whether a proposed context slice is needed for the stated task. Preserve
   multiple supported memberships. Abstain on unresolved reference, insufficient
   evidence, out-of-set content, transport failure or invalid output. Do not
   normalize independent memberships into a forced one-project distribution.
4. **Record replayable decisions.** A provider-neutral record should include
   packet hash, candidate IDs, question/rubric version, provider/model version,
   raw scores, decision, reason for abstention, selected evidence IDs, duration,
   token usage, error category and request ID. If a scorer cannot produce
   source-grounded reasons, preserve that limitation; do not invent them.
5. **Measure the practical trade-off.** Report retrieval recall first, then
   multilabel precision/recall, false inclusion of unrelated context,
   coverage-versus-error under abstention, Brier score where genuine binary gold
   exists, ranking quality, retained tokens, and end-to-end p50/p95 latency.
   Show PL and EN separately. Report topic boundary/return errors separately
   from project affiliation. Keep throughput, cost and quality on separate axes.
6. **Stress the contract.** Use a predeclared subset for candidate-order changes,
   neutral option-ID renaming, irrelevant context, quoted injected instructions,
   and repeat requests. Compare labels after mapping back to semantic IDs.
   Such perturbations are robustness checks, not extra independent examples.

For a first live pilot, propose an explicit ceiling of **160 requests and two
million billed input tokens per provider**, with a separately configured money
ceiling, maximum two concurrent requests, request timeout and at most one
transport retry. If the budget prevents an arm finishing, record incomplete
coverage. At Jev's checked input price, two million input tokens would cost
$0.084 before any tax or additional charges; other providers need their own
current prices. This is a budget calculation, not a measured bill. Cache by
full request identity, save every completed batch, and never automatically
retry semantic disagreement until an attractive result appears.

Candidate-scoring output remains a proposal. It must not mutate canonical
claims, erase source bytes, replace existing assessments, set user preferences,
or silently exclude a project forever. Favor additive retrieval suggestions
until held-out evidence supports a stronger role. A provider outage should
leave the existing baseline available, with the unavailable scorer visible in
diagnostics. No claim of a complete universal thought representation follows
from this routing experiment.

## Decision for this increment

Keep the useful architectural hypothesis: cheap bounded decisions can reduce
the expensive extractor's irrelevant context. Implement the experiment through
a provider-neutral interface before committing the system to Jev. Preserve
the native graph validator as the mechanical authority for admissible
structure, and measure semantic correctness against separately authored source
interpretations. The current audit supplies no live-model quality result.
