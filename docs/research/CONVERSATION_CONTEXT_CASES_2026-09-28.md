# Conversation context cases — 2026-09-28

Prepared a separate, authored PL/EN pilot for the owner's requirement that late
project discussion, topic returns and graph context survive multithreaded chats.
This work does not change the already frozen OpenRouter structure pilot, its
runner or production graph. No paid or external model request was made.

The corpus is under
`loom/tests/fixtures/eval/conversation_context_pilot_v1/`. Six messages in each of
16 conversations yield 96 independent causal-prefix queries. Four whole families
are development (48 queries) and four validation (48 queries). A Polish/English
pair always belongs to the same split. Opaque prompt-visible case/message/topic/
Claim handles carry no family or gold labels. Supplied target descriptions are
part of the task: this measures tracking, not discovering arbitrary topic names.

| Family | Split | What can fail |
|---|---|---|
| Late project onset | Development | Treating an earlier homonymous book reference as project discussion |
| Return after digression | Development | Dropping the project after another topic intervenes |
| Correction and contradiction | Development | Replacing an old Claim when the speaker only records a conflict |
| Availability and unknown time | Development | Treating a Claim as prior knowledge before its support arrives, or inventing a date |
| Overlapping topics | Validation | Forcing export and privacy into exclusive buckets |
| Scope prevents false conflict | Validation | Calling test versus production, or a hypothetical scenario, a factual contradiction |
| Later name cannot backfill | Validation | Resolving an earlier ambiguous project reference from a later clarification |
| Analogy without identity | Validation | Merging two projects that share a queue rule |

The gold contains 82 topic memberships over 96 messages, 10 messages with multiple
topics, 24 with no listed topic, 28 onsets and 22 returns. It contains two
correction, four contradiction and two analogy edges; 28 old-Claim selections;
two ambiguous-reference opportunities; and eight explicit historical-availability
judgments (six unknown, two established prior). These are authored case counts,
not successful model outputs. Each bilingual pair repeats a structural scenario,
so those counts do not constitute independent evidence of generalization.

`materialize.py export CASE_ID ORDINAL` uses the existing
`context_delta.prepare_context` implementation. It presents all temporally eligible
memory candidates, including irrelevant ones, rather than preselecting the gold
answer. Old Claims retain their Assessment and exact supporting Observation spans.
Unknown timestamps stay visible and unknown. A future dependency withholds its
Claim until available. Ordered current messages are complete Observations, and
only a prefix is selected: no later conversation body or native audit/undo sidecar
is sent. This is causal at message boundaries, not within a message.

`protocol.json` specifies separate scores for message membership, UTF-8 span
coverage, onset/return boundaries, typed relations, old-Claim/source selection,
ambiguous-reference abstention and historical availability. It defines empty
denominators, failure accounting and structural unsupported-output counts. It
requires first raw outputs to remain preserved; malformed/refused/truncated
responses remain in the total denominator. It disallows silent JSON repair or
using later clarifications to regrade earlier predictions as if they were causal.
A return is operationally defined as renewed membership after at least one full
intervening message without that topic; this deliberately measures a simple,
reproducible boundary convention, not every discourse-theoretic notion of return.

Validation executed successfully through all 96 prepared source packets, checked
UTF-8 spans and Claim references, verified family-disjoint bilingual splits and
onset/return consistency, and mutated hidden future text to verify that sentinel
bytes never enter earlier exports. Unknown/prior history labels were checked
against source availability. The report is `validation.json`; these are input and
causal-projection checks, not model accuracy or independent semantic adjudication.

Frozen corpus hashes:

| File | SHA-256 |
|---|---|
| inputs.jsonl | `b0b5f65214aae612fb160c41e0591039703383584ab029c38babc90c3a4d1bc2` |
| gold.jsonl | `2682a8e23346ae32ba34d69a74b7c350b89aceb743226c42e76186ecc4cdabf5` |
| split.json | `f2a7bfd935ad23152545238731b106595351fd8050ba078dec661d1ea4fcd466` |
| protocol.json | `6b7cc4afdb2c5c56ccf55cf6dd447acb379cafc67bcddd086e6aab18ea4f011f` |
| materialize.py | `b318f535f8fd0f4a0017789afceff2a40004f10aa3501c7a78d9e46aa236e7d0` |

Next work: independently implement the protocol scorer and verify output-contract
failures, then adapt a bounded live runner to this response shape. Run development
prefixes, freeze the configuration and only then run the validation families.
Do not advertise this as a comprehensive or production-quality benchmark: texts
are short, authored and intentionally legible; labels have one author; ambiguity
and correction have very few opportunities; all topics are supplied; and no real
archive, long-context or multi-speaker attribution performance is measured here.
After validation is used to tune a method, acquire a new holdout before making a
fresh out-of-sample claim. No existing hidden holdout or earlier validation gold
was read while authoring this corpus.
