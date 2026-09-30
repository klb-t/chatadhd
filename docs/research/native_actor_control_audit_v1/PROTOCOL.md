# Independent actor transport controls

Author: frontier_matrix, independent of the actor projection implementation.
These twelve tiny source wrappers are new mechanism controls, not DEV relation
cases, source-graph quality labels, native extraction runs or model experiments.
No API, key, gold, validation or old holdout is used. The fixture builder invokes
no native extractor or actor projection.

The task contract supplies the expected behavior: bind exact raw source bytes and
an observation quote at `/mapping/{turn}/message/content/parts/0`; preserve a
literal source-recorded actor label from `author.name` and
`metadata.loom_source_speaker`. Equal labels or one present label resolve; missing
labels stay unknown; conflicts abstain. User transport role is no actor fallback.
Source IDs and turn IDs must agree across their source fields. Known-at metadata
must be explicit and agree with epoch create_time and the native observation date.
Missing/null metadata stays unknown. The raw label never establishes world
identity, and identical names/turn IDs across raw hashes never imply `same_as`.

Expectations are frozen before any projection output. The fixture contains equal,
conflicting, absent and single actor labels, conflicting source IDs, two separate
raw documents with colliding literal turn/observation IDs, a missing locator,
an unsupported parts/1 pointer, an exact narrow UTF-8 quote, a missing timestamp,
an explicitly null timestamp, a date conflict and a mismatched source hash.

The parts/1 control deliberately distinguishes an existing second text part from
the declared parts/0-only support. It must be unavailable under this contract even
when its byte quote and hash are otherwise valid. Preserve the first outcome
before considering a parser correction or a separately versioned policy change.

`controls.json` uses the receiving harness's `cases` and `cross_case_checks` shape.
Per-field expectations are separate from top-level source binding state. The
fixture builder records canonical input fingerprints as construction receipts;
the projection harness computes locator hashes from its own exact serialized raw
bytes. It must preserve those actual bytes and the final observation before
execution. No graph mutation or global actor association is authorized by the
fixture. Unknown/conflicting field values must not be filled from native speaker,
transport role, observation date or another source's namespace.
