# Configurable frontier graph methods: offline instrument round

This round implements instruments and tests their mechanisms. **It has made zero
frontier inference calls.** The owner deferred a paid frontier pilot; this does
not prohibit the previously authorized cheap/Jev research under its existing
session budget. Frontier methods remain available to future users with their
chosen model, scope, reasoning, payload and application policy. The USD 2 global
and USD .10 batch values below are editable research presets, not product limits.

## Evidence and current instrument identities

Unauthenticated public OpenRouter catalog and endpoint GETs were saved byte for
byte, with retrieval times, URLs and SHA256 hashes in `public_preflight/`. The
primary family presets are GPT-6.1 Sol, Claude Sonnet 5.5 and Gemini 3.1 Pro
Preview. Gemini 3.8 Flash and GPT-6 Luna are additional lower-cost variants. This
is a choice of instruments, not a claim that these are universally the best models.
Direct account access, latency, quality and billing have not been tested here.

| Preset | Requested model | Pinned provider | Observed dated alias |
|---|---|---|---|
| gpt61_sol | openai/gpt-6.1-sol | openai | openai/gpt-6.1-sol-20260929 |
| sonnet55 | anthropic/claude-sonnet-5.5 | anthropic | anthropic/claude-sonnet-5.5-20260928 |
| gemini31_pro | google/gemini-3.1-pro-preview | google-ai-studio | google/gemini-3.1-pro-preview-20260219 |
| gemini38_flash | google/gemini-3.8-flash | google-ai-studio | google/gemini-3.8-flash-20260902 |
| gpt6_luna | openai/gpt-6-luna | openai | openai/gpt-6-luna-20260922 |

Every listed endpoint exposes an observed dated alias. The date is copied from
the public snapshot, not inferred from the preset name. Exact provider, active status,
endpoint file hash, alias set and token prices are checked before planning/replay.
A made-up future alias is rejected even if it matches an equally fabricated ledger.

Official public references consulted on 2026-09-30:

- https://openrouter.ai/docs/guides/best-practices/reasoning-tokens
- https://openrouter.ai/docs/guides/routing/provider-selection
- https://openrouter.ai/docs/guides/features/tool-calling
- https://developers.openai.com/api/docs/guides/responses-multi-agent
- https://developers.openai.com/api/docs/guides/agents

The catalog advertises mandatory reasoning for the Sol/Sonnet/Gemini presets and
optional reasoning for Luna. The instrument validates the selected effort against
the observed model metadata. `low` is the default here; supported deeper effort
and disabled reasoning are configurable. A model rejecting disabled reasoning is
an external capability constraint, not an application policy. Google effort
levels do not establish a precise internal token budget.

## Methods and input separation

`recipes.json` records three controlled DEV recipes for each existing track:

1. exact existing baseline prompt;
2. an added generic output-grammar reminder;
3. a separate epistemic decomposition reminder.

The assisted extractor receives unchanged turns and supplied proposition nodes,
without judgment queries or gold. The free extractor receives only id, source_id
and raw turns. Its node/edge scorer reuses the existing frozen surface-alignment
lower bound, including unmapped/invalid outputs in planned denominators. It does
not equate representation mismatch with a false world fact. The supplied-edge
judge receives the unchanged, physically truncated `as_of` source prefix. Source
attribution, direction, negation and temporal scope remain required.

Extraction over a whole conversation is retrospective. It is not a causal
prefix-extraction experiment and is not a matched ranking against prefix judges.
The DEV case selections are deterministic IDs in data: screen6, balanced8,
balanced12 and all24. The latter three contain equal PL/EN counts; balanced8
selects two cases from each six-case family block. No validation file is opened.
New validation use needs a separate root release after every finalist is frozen.

The generic `graph_packet` track uses **the same** `agentic_graph_v1.packet`
codec and rich native `Entity.to_json`/`Claim.to_json`/`Observation.to_json` records.
No alternate graph store or narrow triple projection is introduced. Actions in
recipe data include complete graph, discover patterns, propose definitions,
merge/split intent, critique, gaps and whole-archive reasoning. Models may propose
new user-defined structures. Before hashes, full replacement records and explicit
edits preserve previous values; merge/split intent never silently relinks a graph.

The original proposed diff is retained unchanged. A separate derived diff binds
only its top-level instrument origin and availability time to the verified model
response, raw response hash, prompt hash and measured `finished_at`. Native source
known_at and Claim qualifiers are unchanged. Preview is reversible and writes no
canonical store. The packet codec's application policy can select preview or auto
acceptance; this client does not override it with an invented universal policy.
Acceptance does not establish the truth of the content.

## Transport, accounting and reproducibility

The old OpenRouter runner and old first-result scorers are unchanged. A private
FunctionType scope preserves their write-ahead, no-retry, key-cap, response
retention and BYOK tripwire mechanisms while the **new** planner validates
configurable reasoning and provider capabilities. No imported module is patched.
Its planner uses actual request hashes and reservations, not projected body hashes.
Envelope byte/deadline limits and batch size are editable data. Global finite
budgets above USD 1 billion are accepted and tested with scripted key metadata;
this test spends no money.

Reservations use a conservative byte-based input token allowance, the largest
public prompt/cache-write/tier rate, and the selected full output budget. The
Google preset adds a completion multiplier to hedge internal reasoning. These
are allowances, **not billing guarantees or token counts**. The provider key cap
and first-attempt accounting remain required for a real bounded execution.
Public price decimals are retained exactly, including values beyond 18 decimal
places. Unsupported additional pricing charges require a new explicit policy.

Known costs belong to attempts even when semantics fail. Missing usage is unknown
cost, not zero. BYOK or unclear billing stops the original bounded credit runner.
Replay checks raw artifact hashes, raw/ledger costs, model/provider identity and
frozen source/recipe bytes before any gold is read. Configured large response
limits and unlimited finite accounting are applied consistently in execution
and replay; independent first counterexamples exposed and corrected two inherited
replay guards. Their original artifacts are retained in the philosophy audit.
Terminal or uncertain attempts are never automatically reissued. A changed
prompt is a new recipe/experiment, not a hidden retry of the earlier response.

`OFFLINE_COST_MATRIX.json` covers 45 planned model/track/recipe cells, 720 request
bodies and USD 31.172386 of conservative reservations on balanced8. This is
planning only, not a proposal to execute a full factorial or an allocation of
USD 31.17. Some Gemini Pro extraction rows exceed the editable .10 batch preset.
Report that capability honestly; do not silently change the selected limit.
Cheap screen/then finalist selection should depend on informative outcomes when
paid frontier data eventually become authorized.
The original matrix remains unchanged with its original code hash;
`OFFLINE_COST_MATRIX2.json` is a recount against the checkpoint sources, with
exact equality of all 45 rows checked separately from its new provenance.

## Runtime axis and limitations

`runtime.py` has executable scripted adapters for:

- local functions over the same packet;
- a client tool loop with standard OpenRouter function calls;
- OpenAI Responses hosted tooling (`managed_agent`);
- beta Responses hosted same-model subagents (`managed_swarm`).

Tools and reasoning items are retained for subsequent turns; tool calls must be
declared, registered and have unique call IDs. Hosted `multi_agent_call` items
are not local tools and are never dispatched by the client. The hosted swarm
shares a model/tools across its tree; mixed model families require Loom
orchestration using per-node instruments. Concurrent subagent count is
configurable without a made-up API ceiling. Unsupported current beta parameters
are reported as capability errors. Root final answers are separated from child
answers and intermediate output.

Every scripted stage persists an exact request, write-ahead attempt, first raw
response and parsed result. Each authorized tool start is persisted before
dispatch; each returned result is persisted and hashed before the next tool can
run. If a later tool fails, previous results remain available and the failing
tool's outcome remains uncertain. Ambiguity stops without a retry. Read-only
replay recomputes the initial request content hash, checks every request against
the recorded continuation, and checks tool result hashes and billing. Responses
usage without an invoice amount remains unknown
cost and consumes its declared reservation. The runtime has no credential loader
or network implementation. A scripted run does **not** establish that an account
has access to an API. The separate persistent Agents API/session service remains
an extension capability; the Responses adapter does not pretend to implement it.

The runtime registry accepts new backend builders/parsers without branching the
canonical graph. Product multiaxis analysis plans are owned by the generic
contracts layer, not by this experiment module.

## Validation and decisions

55 targeted mechanism tests pass (32 frontier core + 23 runtime). They test
protocol/transport/codec behavior and adversarial counterexamples, not AI quality.
First failures remain in the local research files and independent audit archive:
a duplicate reservation keyword during initial preparation; a test helper that
JSON-quoted a deliberately large raw content string; two original replay limit
counterexamples; an incorrect assumption about provider decimal precision; and
two runtime counterexamples where a later tool failure lost an earlier returned
result and a changed initial request retained an unchecked stale hash.
No quality gate or first-result artifact was weakened or overwritten.

Keep the new generic mechanisms as an offline research surface. Investigate
actual archive quality, cost, managed API account access and model-conditioned
error profiles only after the required sources and authorization exist. No
native production wiring, native ABI extension, live frontier quality claim,
natural-language population accuracy or canonical graph update follows here.

## Ecosystem contracts and permission boundary

`ECOSYSTEM.md` describes possible cooperation, not ready integrations. A shared
packet does not make ChatADHD the required interface or give an executor access to
AGEDS/WatchDog/iOmatrix/LEM/PixelSpace data. Runtime configuration carries the
semantic-contract reference and resource scope separately. Tool capability,
registered executor and explicit per-tool execution permission are independent.
The executor receives that scope/contract/grant context and must enforce its own
domain permissions. Merely declaring a tool in a model request does not dispatch
it. Different domain graphs can retain their semantics; this transport does not
force a global ontology or automatically discover another project's resources.
