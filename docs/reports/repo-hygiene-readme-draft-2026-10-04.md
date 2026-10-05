# Root README draft — repository hygiene, 2026-10-04

**Integrator draft; existing READMEs are unchanged.** Prepared against source
`161cc22dfb84fe863389d6b90323bd44516a68dc`. The section between the markers is
intended for the repository root: its links are root-relative. Install it only
after the INTERFACE work is incorporated and reconcile it with the accepted
results of threads 1–8. The dated measurements below remain attached to their
original source; they are not a receipt for later integration.

<!-- BEGIN PROPOSED ROOT README -->

# ChatADHD / Loom

A local-first workbench for conversations, source evidence and inspectable
context. ChatADHD provides the interaction layer; Loom is the reusable C++20
kernel for storage, import, knowledge graphs and resumable analysis.

The project connects a conversation's source history to the claims derived from
it, the current task and the material selected for a model request. Keeping
those layers distinct makes it possible to inspect a result, correct it and
return to its evidence.

**Development preview for developers and testers.** The native kernel, CLI,
HTTP server and React workbench run today. Android has a JNI/WebView shell with
host-side tests; real-device validation is outstanding. The Python/Kivy client
remains available as a retained prototype.

![Knowledge workbench with fictional entities, claims and linked graph views](docs/images/knowledge-workbench.png)

*Synthetic demonstration against the native server, with model analysis
disabled. The image demonstrates the workflow; extraction quality needs
separate evaluation.*

## What works today

| Capability | Current behavior |
|---|---|
| Source preservation | Imports retain raw source material and provenance. Conversation editing preserves earlier versions. Imported reasoning, tool/code blocks and unknown fields can be inspected separately from current text. |
| Knowledge inspection | The workbench exposes entities, claims, evidence classes, supporting sources and graph/list views. Several views can inspect the same records with independent or linked parameters. |
| Context preparation | Native graph and TF-IDF retrieval support explicit scope/detail controls, per-thesis plans, recorded counter-evidence links and inclusion/exclusion diagnostics. Context preview is available without a model request. |
| Explicit task acceptance | A caller-supplied ActiveTaskSpec can reach native chat with durable acceptance and source checks. Automatic task extraction from arbitrary prose remains future work. |
| GraphPacket persistence | Selected entities, claims and observations can be atomically accepted into native KnowledgeStore, with immutable full-packet receipts and checked readback/replay. Full transformation/history validation remains in the Python tooling. |
| Application profiles | Versioned JSON views and declarative workflows use registered Loom adapters. Multiple views preserve their own model/context choices. Exact profile source can be saved through native receipts and restored after client storage loss. |
| Resumable analysis | Native archive/knowledge tasks carry input hashes and checkpoints. The CLI and HTTP APIs expose execution and artifacts. |

Profiles include partial ChatGPT/Claude/Gemini interface mappings and separate
source-backed LibreChat 0.8.8 and NextChat 2.16.1 presets. Original proprietary
app versions are unverified. An export records available source data; it does
not reveal a service's hidden instructions, complete memory or original API
request. [Profile contracts and evidence](docs/APPLICATION_PROFILES.md) state
the available adapters and fidelity boundaries.

Saving a profile creates a completed canonical knowledge run; an automatic
latest-run query can consequently select it. Applying a profile preserves
explicitly pinned context. Browser-local perspectives and workflow variables
are not synchronized across devices.

## Build and open a synthetic demo

Requirements: CMake 3.25+, Ninja, a C++20 compiler, Python 3.11+ and Node.js with
npm. Run these commands from the repository root. The example uses bundled
SQLite and an isolated temporary data directory.

```bash
python3 -m pip install requests cryptography -r loom/tools/contracts/requirements.txt
cmake --preset dev -S loom -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_BUILD_SERVER=ON
cmake --build loom/build/dev -j 4
ctest --test-dir loom/build/dev --output-on-failure -j 4
npm --prefix loom/web ci
npm --prefix loom/web run build

DEMO_DATA="$(mktemp -d)"
./loom/build/dev/cli/loom --data-dir "$DEMO_DATA" knowledge run \
  --source loom/tests/fixtures/eval/synthetic_dev/chatgpt_export.zip \
  --source loom/tests/fixtures/eval/synthetic_dev/claude_export.zip \
  --no-priors --llm off
./loom/build/dev/server/loom-server --host 127.0.0.1 --port 8787 \
  --data-dir "$DEMO_DATA" --static-dir loom/web/dist
```

Open [http://127.0.0.1:8787](http://127.0.0.1:8787) and select **Knowledge**.
Inspect a claim's evidence, add a second graph view and build a context preview.
The [five-minute walkthrough](docs/DEMO.md) explains each step. This demo needs
no API key. Live chat requires choosing a provider and configuring its key in
Settings.

[Loom's build guide](loom/README.md#build) covers release and sanitizer presets.
[Web instructions](loom/web/README.md) cover the browser suites; those require
Playwright Chromium. CTest supplies the Python import paths.

## Architecture

| Component | Role |
|---|---|
| [`loom/`](loom/README.md) | C++20 kernel, C ABI, SQLite stores, provenance, tasks, graph and context pipelines |
| [`loom/cli/`](loom/cli/) | Chat, import, search, source inspection, knowledge/archive runs and artifact commands |
| [`loom/server/`](loom/server/) | HTTP/REST and SSE facade over the kernel |
| [`loom/web/`](loom/web/README.md) | React/TypeScript workbench, graph views, context preview and application profiles |
| [`loom/android/`](loom/android/README.md) | JNI/WebView shell; device validation remains open |
| `core/`, `engine/`, `gui/`, `main.py` | Retained Python/Kivy prototype and compatibility regression sentinels |

Code and persistent data have separate lifecycles: application upgrades replace
code; databases, attachments, configuration and secrets use a data directory.
Raw sources remain available while derived records are rebuilt. Claims carry
assessments and provenance; observation, inference and extrapolation have
different meanings. Open domain vocabulary and policy belong in data packs.
The [conceptual model](docs/architecture/LOOM_CONCEPTUAL_MODEL.md) defines the
shared terms and the intended contract.

Native CandidateGraph and the Python GraphPacket representation remain distinct.
The [native store contract](docs/NATIVE_GRAPH_PACKET_STORE.md) documents the
implemented acceptance bridge. Python compatibility is no longer a product
requirement; existing differential tests remain regression sentinels until a
deliberate migration.

## Verification and evidence

The [2026-10-04 profile verification](docs/verification/application-profiles-extended-2026-10-04/RESULTS.md)
pins source and binary hashes for implementation `95e5049` and records:

| Check | Recorded result |
|---|---|
| CTest entries | All 108 discovered entries passed across 106 + 2 stages |
| Native GraphPacket FFI regressions | 18/18 tests |
| Python contract suite | 209/209 tests |
| Strict TypeScript and production Vite build | Passed; 85 modules |
| Profile runtime / API adapters | 19/19 and 9/9 groups |
| Profile native graph / browser integration | 11/11 and 15/15 groups |
| Native imported-source display | 4/4 groups |

The receipt preserves first failures and corrections, reproduction commands
and the boundaries of each measurement. Chat integration checks used a local
fake provider. No paid model call or GitHub Actions run was part of that
verification. These results establish engineering behavior on the recorded
snapshot; general extraction accuracy, recipe efficacy and external-model
response quality need separate measurements.

[Current state](docs/STATE.md) records accepted increments and open work.
[Research and costs](docs/RESEARCH_AND_COSTS_2026-10-01.md) separates historical
development tasks, saved-response replay and live inference. The prepared
432-request analysis study is not a completed quality experiment.

## Roadmap

The direction is to compile evolving conversations into a traceable knowledge
state and current task, select context by that task, and materialize products
under the user's preferences. This remains a research and engineering programme.

- Replace remaining resource/product ceilings with caller-configurable policy
  and implement confirmation before an expected tenfold usage increase.
- Connect provider-backed vectors and model-assisted goal typing to production
  selection, then measure context cost and quality against matched baselines.
- Extend native GraphPacket transformations/history and graph-form model replies.
- Validate large real archives, semantic selection, extraction and generated
  hypotheses with independent data and controlled evidence classes.
- Extend media/artifact views, branch navigation, durable workflow recovery and
  Android graph/profile storage; validate on real devices.

Accepted changes can move items out of this list. Proposed capability and
measured behavior stay separately documented in [STATE](docs/STATE.md) and the
[engineering inventory](docs/LIMITS_AND_WIRING_2026-10-01.md).

## Documentation, data and licensing

[Documentation index](docs/README.md) links current contracts, guides and dated
verification. [Archive index](docs/archive/README.md) explains retained
experiments, negative results and historical branches with reconstruction
instructions. Main is the accepted development line; historical experiments
retain their original provenance.

Use the fictional fixtures and a separate data directory for public demos.
Private exports, credentials and signed download URLs do not belong in this
public repository. API secrets belong in the persistent data store. Optional
local encryption does not prevent a remote provider from receiving plaintext
selected for its request; choose external transmission explicitly.

**Source-available:** noncommercial use is licensed under
[PolyForm Noncommercial 1.0.0](LICENSE). Commercial use requires a separate
written license; see [commercial licensing](COMMERCIAL_LICENSE.md). Third-party
components retain their own licenses and terms.

<!-- END PROPOSED ROOT README -->

## Integration notes (exclude from the root README)

- Scope compliance: this report is the only file edited. Existing root/docs/web
  READMEs, STATE, profile contracts, client code and sealed evaluation material
  are untouched. No model or remote CI call was made for this draft.
- Evidence update: root README still cites 108/108 plus 13/13 store tests;
  `docs/DEMO.md` cites an earlier 107-entry verification. The newer profile
  receipt records 108 entries in two stages and 18 FFI tests. These are dated
  results, not interchangeable live totals. Before publishing, add the fresh
  integrated verification without relabelling the older receipts.
- Capability wording: `loom/README.md` describes an optional model goal fallback
  in a way that can imply a production connection. STATE and the wiring audit
  explicitly identify the missing caller. This draft follows the latter and
  leaves it on the roadmap until thread 3 supplies accepted evidence.
- Preserve INTERFACE's distinctions: partial app/version mappings, source-service
  parity gaps, native receipts versus browser-local view state, and the actual
  latest-run side effect of saving a profile. Avoid a blanket claim of original
  app reproduction or cross-device recovery.
- The owner's current instruction forbids paid calls without agreement. Older
  AGENTS language about an already authorized USD 2 research programme does not
  authorize new paid calls in this lane. The draft describes recorded work and
  a prepared study only.
- Threads 2–5 can change roadmap items. Accept their code and verification
  before updating capability claims; preserve the same source/evidence boundary
  after rebase. Thread 8 does not edit their files or promote their branches.
- Build commands and local link targets are taken from the current root/native
  guides and checked for existence/preset consistency. This draft does not claim
  an additional build run; the parent hygiene lane owns fresh validation.
