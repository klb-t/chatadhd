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
Action arguments are supplied by the host; this first contract does not bind
earlier action results into later parameters. More complex workflows need that
explicit dataflow in addition to state transitions.

The web UI can import a profile JSON and open any number of simultaneous profile
views over the selected conversation. Each retains its model and context controls;
message edits and completed sends notify sibling views. Changing the UI profile
does not select a provider, edit privacy/permission settings or compile a different
context. The shared conversation sidebar uses the primary view's geometry;
secondary profiles do not receive independent sidebars in this slice.

Local persistence saves custom definitions, view identities and successful
workflow transitions without message text or API secrets. It does not provide
cross-device synchronization, native graph persistence or execution recovery.
Invalid stored data is reported and preserved. Closing a view stops its active
subscription; it does not delete conversation data.

## Included data profiles and reference projects

All supplied graphics/CSS are our own implementation; third-party source code
has not been incorporated. The examples deliberately carry evidence gaps.

| Example / reference | Use and version evidence | Limit |
|---|---|---|
| Native Loom example | Current development view | Existing features, not a released consumer application |
| ChatGPT-inspired example / [LibreChat](https://github.com/LibreChat-AI/LibreChat) | Reference release `v0.8.8`, commit `e8f3be08623663d4ad7f7241e693c94469b63bb0`; branching and adapter architecture | LibreChat describes inspiration from ChatGPT; its release does not identify a ChatGPT version |
| Claude-inspired example / LibreChat | Multi-provider interface reference; a separate editable composer example | No pinned original Claude UI evidence; shortcut and colours are implementation choices |
| Gemini-inspired example / [Gemini-Clone](https://github.com/GourangaDasSamrat/Gemini-Clone) | Educational visual reference | Reference commit unpinned; Gemini model name does not identify UI version |
| [NextChat](https://github.com/ChatGPTNextWeb/NextChat) | Reference `v2.16.1`, commit `557a2cce357749c6fb3176d42e03ff6f7de4d355`; lightweight interface/provider composition | Prompt masks are not general app workflow profiles |
| [Chatbot UI](https://github.com/mckaywrigley/chatbot-ui) | README preserves `1.0` on `legacy` alongside `2.0` | Versions are Chatbot UI releases, not ChatGPT releases |

The inspected repositories establish useful examples. They do not establish
that clones of all applications, or arbitrary versions of originals, exist.
An original version must be documented separately by source artifacts, dated
captures, user observations or actual behavioral comparison.

## Adding an application or historical version

Start with one of the [JSON examples](../loom/web/src/profiles/data/). Give the
target a separate ID/version/platform, retain the source references and describe
what is unverified. Import the JSON through **Import profile**. No code branch
based on an application name is needed for existing renderer/operation vocabulary.
Current rendering supports chat, plain/bubble messages, colour tokens, content
width, shared sidebar geometry, composer shortcuts and the workflow controls.
An arbitrary application's specialized screens still need renderer components;
an unsupported operation still needs an adapter. A manifest cannot manufacture
those capabilities. Trusted code extends the registry explicitly.

Separate the following changes when translating a source application:

| Transformation | Preserved | Unknown/lost or added |
|---|---|---|
| Reference UI → profile data | Declared geometry/interaction choices, target and reference versions | Pixel/animation fidelity, accessibility and undisclosed state are unverified; Loom adds its own inspection controls |
| Source workflow → canonical actions | Declared order and capability guards | Source service's hidden state, request assembly and tool policy remain unknown; local adapters have their own semantics |
| Conversation → multiple views | Existing canonical messages, versions and model/context selections | This slice renders active text messages, not all preserved tool/media/raw-export blocks |
| Action → saved navigation trace | Action/operation identity and successful local transition sequence | Request payloads/results omitted; trace is not an exact provider transmission record |

## Claude handoff and remaining work

The contract is a narrow implementation of the existing detachable `interface`
and interaction axes (§11.10 of the conceptual model). It does not replace
workspace/model/provider profiles or silently apply their other components.

Next additions should demonstrate a specific target version against evidence:

1. Extend renderer components for typed tool/media/artifact blocks and source
   chronology/branches, retaining raw observations and unknown metadata.
2. Add actual project/artifact/browser/voice adapters with per-operation
   `native/equivalent/limited/unavailable` evidence. Native message version
   restore handles siblings, not complete descendant-branch navigation.
3. Persist profile entities/capability links through the existing canonical
   graph boundary (R15) and add server-backed profile/workflow state. Browser
   storage here is a client prototype, not a parallel knowledge authority.
4. Bind durable execution checkpoints, idempotency and result references to
   existing TaskEngine for remote/replayable workflows. Current local workflow
   persistence cannot guarantee recovery after a successful remote side effect
   followed by a browser crash.
5. Extend geometry from one shared primary sidebar to independent composed
   views; preserve existing unlimited graph panes and detachable couplings.

Build/test commands and measured results belong in
[the dated verification](verification/application-profiles-2026-10-04/RESULTS.md).
The prior 108/108 CTest report remains tied to its original source snapshot.
