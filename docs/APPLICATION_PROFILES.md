# Versioned application interface and workflow profiles

Owner requirements R17 and R21 in
[the requirements log](architecture/OWNER_REQUIREMENTS_2026-09-26.md) explicitly
include provider-inspired interfaces, archival application versions, data-defined
interaction logic and reproduction of functionality where possible. The owner
renewed that instruction on 2026-10-04. This increment implements a first web
slice over Loom's existing operations; it is not an exact replica of every app.

## Implemented boundary

`loom.application_profile/1` is a JSON contract for an application identity,
target release/platform, a separately versioned profile, reference provenance,
declared evidence gaps, presentation, composer interactions, actions and finite
workflow transitions. A target release may be `null`: unknown is explicit, never
replaced with a clone's version or a model's name. Different target releases use
different profile IDs; profile revisions refine the same target. Saved workflow
state includes the canonical profile definition, so reusing an ID/revision for
different data cannot silently continue an earlier workflow after reload.

The [JSON Schema](contracts/application_profile.schema.json) documents the data.
The web [runtime](../loom/web/src/profiles/runtime.ts) also validates references,
deterministic transitions, registered renderers/operations and capability guards.
An imported profile cannot install code or an adapter. A required missing adapter
blocks activation; optional gaps remain visible and their controls do not run.
Supported means a registered Loom operation exists, not that credentials,
networks, provider quality or source-application equivalence have been verified.

The [Loom adapter](../loom/web/src/profiles/loom-adapter.ts) forwards create,
rename, send, cancel, edit, version restore and exclude to the existing
API. Selection uses the installed host callback and existing conversation reads.
UI opening adapters expose Knowledge/Memory/Import/Settings views; opening
a panel is not tool execution. Chat forwards the constructed request unchanged.
It completes only on the native terminal result; errors, incomplete streams and
cancellation do not advance a workflow. Workflow failure leaves its local state
unchanged, but remote effects already performed by an adapter are not rolled back.
Stored workflow traces are local navigation history, not authoritative receipts
proving server execution, idempotency or delivery.
Transitions can now bind a declarative `payload` from `inputs`, explicit host
`context`, or workflow-local `vars`, and select `save` values from a successful
result. Expressions are JSON literals, objects, arrays and RFC 6901 pointers;
there are no scripts. The supplied revision-2 workflows retain only a created
conversation's `/id` and use that ID for return, independently of later sidebar
selection. The host exposes `{conversation_id}` as its context and an editable
JSON input envelope. Old profiles without bindings use that JSON as their
explicit action payload.

Saved variables and variable-update receipts are checked when replaying a local
session. Inputs, complete results, requests and stream callbacks are never
captured automatically. A profile can explicitly select JSON data to retain;
its selected values are stored in browser storage and should be reviewed before
running a custom workflow. Missing inputs and capability guards fail before the
adapter runs. An invalid selected result fails after the adapter returns: local
state does not advance, but its external effects cannot be undone.

The web UI can import a profile JSON and open any number of simultaneous profile
views over the selected conversation. Each retains its model and context controls;
message edits and completed sends notify sibling views. Changing the UI profile
does not select a provider, edit privacy/permission settings or compile a different
context. The global conversation sidebar follows the primary view. Each composed view
can also open its own sidebar using its profile geometry, and can couple its
conversation selection to the host or choose independently. Model, context,
channel and plan controls persist independently by view identity.

Local persistence saves custom definitions, their exact accepted UTF-8 JSON
source text, view identities, successful transitions and explicitly selected
variables. A UTF-8 BOM is retained in the source and removed only for JSON
parsing. Invalid UTF-8 is rejected rather than repaired. All original revision-1
builtins remain available; revision-2 data is separate, so an old session cannot
silently change its definition. Async import/load retains new source records
without overwriting a more recent view choice or restoring a closed view.
Invalid stored data is reported and preserved. Closing a view stops its active
subscription; it does not delete conversation data.

**Save profile to graph** prepares and explicitly accepts a GraphPacket through
`POST /api/graph/packets/store`, using the existing C ABI and KnowledgeStore.
It writes an `application_profile` entity and an Observation containing the
exact source JSON, its hash, source locator and declared actor. The packet also
records the canonical definition hash and application/profile version identity.
This is not a second profile database; immutable receipts, selection closure,
owner judgement authority, CAS and drift checks remain native operations.
The new HTTP route preserves the original request body before calling the ABI.
Source authenticity and original-app parity are not established by acceptance.
A saved profile creates a completed knowledge run. Existing automatic
latest-run queries can therefore select that run; a caller can pin an explicit
run in context controls. Applying/loading a UI profile does not itself write to
the graph or change the caller's context selection.

**Saved profiles** can list recent native profile receipts or load a known
receipt ID. Loading checks the native current-row drift report and the packet's
source/identity before registering the profile. Older clients which did not
retain original bytes use a visibly labelled JSON derivative when explicitly
saved. Definitions can survive browser storage loss through native receipts;
an explicitly saved workflow snapshot can restore view layout, definitions,
exact source JSON, local transitions and selected variables from server config.
Remote execution outcomes still require inspection and reconciliation. Android's current JNI dispatch does not expose this
store operation, so the UI marks it unavailable there.

## Included data profiles and reference projects

All supplied graphics/CSS are our own implementation; third-party source code
has not been incorporated. The examples deliberately carry evidence gaps.

| Example / reference | Use and version evidence | Limit |
|---|---|---|
| [LibreChat 0.8.8 profile](../loom/web/src/profiles/data/librechat-0.8.8.json) | Clone itself, pinned source; Enter, user-only bubbles, 360px sidebar | Partial source mapping, not original ChatGPT fidelity |
| [NextChat 2.16.1 profile](../loom/web/src/profiles/data/nextchat-2.16.1.json) | Clone itself, pinned source; unmodified Enter, bubbles, 300px sidebar | Source Retry/edit/context semantics differ explicitly |
| Native Loom example | Current development view | Existing features, not a released consumer application |
| ChatGPT-inspired example / [LibreChat](https://github.com/LibreChat-AI/LibreChat) | Reference release `v0.8.8`, commit `e8f3be08623663d4ad7f7241e693c94469b63bb0`; branching and adapter architecture | LibreChat describes inspiration from ChatGPT; its release does not identify a ChatGPT version |
| Claude-inspired example / LibreChat | Multi-provider interface reference; a separate editable composer example | No pinned original Claude UI evidence; shortcut and colours are implementation choices |
| Gemini-inspired example / [Gemini-Clone](https://github.com/GourangaDasSamrat/Gemini-Clone) | Educational visual reference | Reference commit unpinned; Gemini model name does not identify UI version |
| [NextChat](https://github.com/ChatGPTNextWeb/NextChat) | Reference `v2.16.1`, commit `557a2cce357749c6fb3176d42e03ff6f7de4d355`; lightweight interface/provider composition | Prompt masks are not general app workflow profiles |
| [Chatbot UI](https://github.com/mckaywrigley/chatbot-ui) | README preserves `1.0` on `legacy` alongside `2.0` | Versions are Chatbot UI releases, not ChatGPT releases |

[The pinned source audit](APPLICATION_PROFILE_SOURCES.md) records exact files,
observed behavior, local projection choices and licenses. The inspected
repositories establish useful examples. They do not establish
that clones of all applications, or arbitrary versions of originals, exist.
An original version must be documented separately by source artifacts, dated
captures, user observations or actual behavioral comparison.

## Adding an application or historical version

Start with one of the [JSON examples](../loom/web/src/profiles/data/). Give the
target a separate ID/version/platform, retain the source references and describe
what is unverified. Import the JSON through **Import profile**. No code branch
based on an application name is needed for existing renderer/operation vocabulary.
Current rendering supports chat, plain/bubble/user-bubble messages, colour
tokens, content width, per-view sidebar geometry, composer shortcuts and workflow
controls with explicit JSON inputs.
An arbitrary application's specialized screens still need renderer components;
an unsupported operation still needs an adapter. A manifest cannot manufacture
those capabilities. Trusted code extends the registry explicitly.

Separate the following changes when translating a source application:

| Transformation | Preserved | Unknown/lost or added |
|---|---|---|
| Reference UI → profile data | Declared geometry/interaction choices, target and reference versions | Pixel/animation fidelity, accessibility and undisclosed state are unverified; Loom adds its own inspection controls |
| Source workflow → canonical actions | Declared order and capability guards | Source service's hidden state, request assembly and tool policy remain unknown; local adapters have their own semantics |
| Conversation → multiple views | Canonical current text, versions, model/context selections, original import blocks and unknown JSON | Inline retained media can be previewed explicitly; code/tools remain source inspection; descendant paths are navigable projections, not selectable native send branches |
| Action → saved navigation trace | Action/operation identity, successful transitions, explicitly selected variables | Unselected request/result fields omitted; trace is not an exact provider transmission record |

## Claude handoff and remaining work

The contract is a narrow implementation of the existing detachable `interface`
and interaction axes (§11.10 of the conceptual model). It does not replace
workspace/model/provider profiles or silently apply their other components.

The thread-10 increment on `gpt/interface-2-2026-10-04` supplies the following
projections and adapters. Each remains separate from a claim of parity with a
source application.

| Handoff item | Implemented | Remaining native boundary |
|---|---|---|
| Specialized source renderers | Inert artifact/code/document inspection, explicitly opened retained inline media, complete descendant index and ancestor navigation with cycle/orphan diagnostics | Referenced blob retrieval and selecting an alternative branch as native chat history have no public adapter |
| Project/artifact/browser/voice operations | Operations panel, native artifact listing/read, media status and explicit file-transcription route; per-operation `native/equivalent/limited/unavailable` evidence | Artifact editing, project execution, browser execution, microphone and realtime voice need actual adapters |
| Queryable capability links | Explicit GraphPacket projection with separate profile/action/capability entities and native acceptance/readback | Evidence is the installed-adapter snapshot; acceptance does not establish original-service equivalence |
| Server-backed workflow sessions | Explicit config-backed snapshots preserve definitions, exact sources, view selection, session traces, selected variables and recovery flags; restore performs no adapter dispatch | This config key has readback conflict detection rather than compare-and-swap. Concurrent writers can overwrite a snapshot |
| Execution recovery | Atomic local pre-dispatch checkpoint records unknown outcomes; unknown/abandoned sessions block continuation. Restoring is blocked during native operations; closed views ignore late results | The public TaskEngine API lacks generic submit/checkpoint/result binding. Server-side exactly-once execution and reconciliation of successful remote effects remain core work |
| Independent composed views | Per-view sidebar placement/width, independent/coupled conversation selection, stable adapter registrations, independent persistent model/context settings | Specialized screens still require trusted renderer registrations |

**Workflow recovery** never marks an unknown remote outcome as successful.
An owner can reconcile only a verified-not-applied outcome before continuing,
or abandon the session and keep it blocked. A browser crash after dispatch can
therefore leave a visible unknown marker. Snapshot storage does not replace
TaskEngine authority or undo a remote side effect. Legacy successful local
sessions remain readable; malformed stored bytes are preserved and reported.

## Available cross-thread controls

- **Context** exposes native channel IDs, per-channel limits and thresholds,
  candidate scan limit, lexical shadow and recorded counter-evidence links.
  Defaults preserve the current request. Unknown method IDs are sent through
  the open native channel contract and retain unavailable diagnostics. Arbitrary
  weights, method combinations and capability discovery await the W3 registry;
  the UI does not silently drop unsupported weight fields.
- **Settings / Usage policy** reads presets, effective settings and overrides
  from W2, edits the complete policy JSON and inspects exact receipt decisions.
  Approve/decline use receipt and operation identities; stale confirmations need
  inspection rather than an implicit new dispatch. The HTTP route calls the
  existing W2 dispatcher when compiled with that dependency, otherwise returns
  explicit unavailable. These ledger controls do not guard unrelated legacy
  chat, transcription or import operations by themselves.
- **Graph reply workbench** compiles a captured response through W4, preserves
  its raw bytes and native error envelope, shows model-origin unverified nodes
  and host-computed spans, validates compilation and explicitly applies a
  candidate/automatic policy to a returned packet. Expand/correct puts a
  fragment address into the composer for review. Application is a packet
  projection, separate from a canonical knowledge-store write. W3 still owns
  automatic chat reply modes and graph-context request assembly. Integration of
  W4 also waits for its held replay-accounting regression to be resolved.
- **Expert controls** show/edit the exact client ChatRequest for one subsequent
  send and accept generic config patches. A client preview is not the fully
  assembled provider prompt; native context traces remain separate. Credentials
  use the existing secret endpoints. Analysis prompt/schema/preset editing and
  full provider-query preview need W1/W11 descriptors and overlay APIs.

The [thread-10 report](reports/interface-2-2026-10-04.md) records fresh checks,
before/after counts, negative evidence and concrete requests to other lanes.
The earlier [extended verification](verification/application-profiles-extended-2026-10-04/RESULTS.md)
remains historical evidence for its pinned increment, rather than a receipt
for these new changes.
