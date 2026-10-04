# W1 — Knowledge precision — 2026-10-04

Branch: `gpt/knowledge-precision-2026-10-04`. Frozen baseline:
`9d15d2dd0733274356f768816e207e26e13845e4`; latest integration base:
`7282437b1c88933977f64b3468b9f42f7b400494` (INTERFEJS and integrator
metadata retained; native source unchanged from the tested `161cc22` base).
Precision implementation tip: `ee5c74a80ec067cc770334d1a24cece5d398cdf4` (two small commits).
Precision acceptance: **108/108 CTest**, 172.90 s, after rebase; thresholds unchanged.
Research suites executed **859 structure and 209 contracts cases**, not zero.
The later prompt-data extension and its separate final gate are recorded after
the reproducibility appendix below. Legacy consumer wiring remains assigned to
its owning threads; registering its recipe does not claim that wiring is done.

## Changes and measured coverage

Archive reviewed: `archive/2026-10-03/wip/precision`,
`de3b9203253ddfdf30e02ea26881a1bcba7077c8`. Its syntax filtering and cue-weight
approach were adapted into configurable operations and measured separately;
its unmeasured changes to user confidence and catalog policy were not taken.

Editable presets in `lexicons/cues.json` reject mined code-fragment names and
complete syntax lines while preserving source observations, valid names,
explanatory comments and technical prose. Two assignment/type-annotation patterns
were narrowed to complete lines to retain prose requirements.
`lexicons/version_patterns.json` binds actual version occurrences to nearby
entities, handles quotes/parentheses/Markdown, rejects measurement suffixes and
extracts declared code-block versions. Dependency versions no longer set the
ambient project's feature-status version.

Distinct weighted paradigm cues aggregate across source-supported subject-local
contexts; repeated phrases count once. Presets: music weight 2.5/window 2,
research weight 3.5/window 1, `dominant_context: false`,
`aggregation: distinct_across_units`. Explicit hints remain valid. Whole-unit
and per-unit alternatives remain configurable. Evidence traces retain contributors;
unknown aggregation settings fail explicitly. Code contains generic operations;
`pack_embedded.inc` is regenerated from data.

| Controlled measure | Before | After |
|---|---:|---:|
| Unwanted projects in 24 extraction cases | 5 | 0 |
| Claims / principles from negative extraction cases | 11 / 2 | 0 / 0 |
| Claims / principles from positive extraction cases | 18 / 5 | 18 / 5 |
| Existing valid-name extraction coverage | 7/9 | 7/9 |
| Original observations retained | 24/24 | 24/24 |
| Exact subject/version cases | 3/15 | 15/15 |
| Music paradigm cases; false positives / negatives | 5/12; 6 / 1 | 12/12; 0 / 0 |
| Research paradigm cases; false positives / negatives | 1/4; 1 / 2 | 4/4; 0 / 0 |
| Direct syntax-policy checks / disabled-policy checks | — | 46/46 / 3/3 |
| Aggregation input-validation cases | — | 8/8 |

Legacy-mode outputs remain byte-identical for all 16 paradigm cases and five
existing DEV fixture rows. The existing strongest-kind comparison stays 4/5;
its pipeline/film mismatch is unchanged. Per-unit aggregation misses one valid
music case (11/12); distinct aggregation restores cross-unit music evidence.
The old scratch oracle incorrectly labeled explicitly subject-linked cross-unit
evidence negative. It is corrected in the embedded fixture; no repository test
or acceptance threshold was removed or relaxed.

## Repository discovery and gates

Matched input: **1,164 files, 20,973,191 bytes, 1,413 selected units**.

| Frozen-input measure | Before | Final after |
|---|---:|---:|
| Extracted claims / mined names | 9,280 / 298 | 8,010 / 247 |
| Paradigm instances / absence claims | 650 / 6,394 | 502 / 3,307 |
| All stored claims | 18,005 | 12,863 |
| Source observations | 37,249 | 37,249 |
| Failed extraction units | 0 | 0 |
| Wall time | 489.973 s | 485.753 s |
| Full CTest after rebase | — | 108/108 |

Final `knowledge_eval.py synthetic` metrics are identical to baseline. That card includes catalog recall 0.6889,
precision 1.0, project recall 1.0, pooled version recall 0.6818,
principle precision 0.5, zero structural violations and deterministic repeats.
Both normal `knowledge_eval.py selfhost` runs completed on the same committed
baseline source; they independently reproduce 18,005→12,863 stored claims,
650→502 instances and 37,249 source observations. The matched benchmark
also retains all 37,249 stored observation rows byte-for-byte (excluding the
separate run-ID column): id, unit, kind, date and full JSON body all match. Products are 1,302→1,006.
The benchmark snapshot was staged once and reused for both executables. Wall time
is descriptive; host load was not controlled as a performance experiment.
The initial rejected bench result (12,793 stored claims) is intermediate evidence,
not the final result.

## Preserved negative work and remaining limits

Negative source: `caa2619af2594bd18872354517bf663027b3afe4`;
archive tip: `ede4c31703d6cd9951338b1762814fdd7d05d979` on
`archive/2026-10-04/knowledge-precision-local-only-negative`, containing complete
negative logs/probes/report. The local ancestor was `a1179b63e2a389efc68f20979ded15f5a5cf5647`.
Music weight 3/window 1, research 3.5/window 1, dominant false and maximum
single-unit evidence gave **104/106 CTest**: both generalize entries failed on
distributed music evidence. This variant is not merged.

An earlier overloaded parallel CTest attempt hit the existing 60 s timeout for
`research.structure` and `research.contracts`. The complete final run used `-j 2`
after stopping benchmark jobs; both suites ran their cases and passed. No timeout
or assertion setting was changed. A first baseline synthetic run also hit a
SQLite workspace I/O error; a fresh `/tmp` rerun succeeded. Neither is counted
as a quality result.

No paid calls, private data, blind corpus or sealed answer key were used. No
catalog limit, user-confidence semantics, UI or other thread scope was changed.
Existing `C++` project-overlap and `Ż` tokenization misses remain. Existing
extractor 3/4-token mined-name windows and other unrelated heuristics were not
converted here because this increment tests the selected precision changes.
They remain a documented follow-up in the same extractor scope.
New syntax, entity binding, evidence window, weight and aggregation presets are
editable in data; they impose no operation quota. No calibration,
semantic-accuracy or temporal-prediction gain is claimed. Historical 8,928 claims
on 283 files are a different corpus and include 4,104 absence claims; raw counts
are not accuracy. Real-archive import, catalogue recall, semantic calibration and
UI wiring remain outside W1.

## Reproduction

All following cases are synthetic/public. The appendix embeds their source so
scratch cleanup cannot lose them. No new CTest entry was added outside W1 scope.
Save this report at `docs/reports/knowledge-precision-2026-10-04.md`, then run the
following from the repository root. Build prerequisites are those already documented
in `CLAUDE.md`; these commands select vendored SQLite for a stable link recipe.

```bash
TASK_RUN_DIR=$(mktemp -d)
export TASK_RUN_DIR
python3 - <<'EXTRACT'
import os, re
from pathlib import Path
report = Path('docs/reports/knowledge-precision-2026-10-04.md').read_text()
out = Path(os.environ['TASK_RUN_DIR'])
for name, content in re.findall(r'<!-- replay-file: ([^>]+) -->\n```(?:cpp|json)\n(.*?)\n```', report, re.S):
    (out / name.strip()).write_text(content + '\n')
EXTRACT

cmake -S loom -B "$TASK_RUN_DIR/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DLOOM_BUILD_TESTS=ON -DLOOM_BUILD_CLI=ON \
  -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_SHARED=ON -DLOOM_WERROR=ON \
  -DLOOM_BUILD_SERVER=ON
cmake --build "$TASK_RUN_DIR/build" -j 2
ctest --test-dir "$TASK_RUN_DIR/build" --output-on-failure -j 2

compile_probe() {
  c++ -std=c++20 -Iloom/include -Iloom/src -Iloom/third_party/nlohmann \
    -Iloom/tests -Iloom/third_party/miniz -DLOOM_TEST_FIXTURES="\"$PWD/loom/tests/fixtures\"" \
    "$TASK_RUN_DIR/$1.cpp" "$TASK_RUN_DIR/build/libloom_core.a" \
    "$TASK_RUN_DIR/build/libloom_sqlite3_amalgamation.a" \
    "$TASK_RUN_DIR/build/libloom_miniz.a" \
    -pthread -ldl -lm -lssl -lcrypto -o "$TASK_RUN_DIR/$1"
}
compile_probe version-probes
compile_probe extraction_corpus_v2
compile_probe paradigm_probe
compile_probe name_policy_v2
compile_probe policy_validation_probe
compile_probe fixture_probe
"$TASK_RUN_DIR/version-probes" < "$TASK_RUN_DIR/version-probes.json" > "$TASK_RUN_DIR/versions.json"
"$TASK_RUN_DIR/extraction_corpus_v2" loom/data > "$TASK_RUN_DIR/extraction.json"
"$TASK_RUN_DIR/paradigm_probe" > "$TASK_RUN_DIR/paradigms.json"
"$TASK_RUN_DIR/name_policy_v2" loom/data > "$TASK_RUN_DIR/name-policy.txt"
"$TASK_RUN_DIR/policy_validation_probe" > "$TASK_RUN_DIR/policy-validation.json"
"$TASK_RUN_DIR/fixture_probe" > "$TASK_RUN_DIR/fixture.json"
cp -a loom/data "$TASK_RUN_DIR/disabled-pack"
python3 - <<'DISABLE'
import json, os
from pathlib import Path
path = Path(os.environ['TASK_RUN_DIR']) / 'disabled-pack/lexicons/cues.json'
data = json.loads(path.read_text())
data['name_rules']['enabled'] = False
path.write_text(json.dumps(data))
DISABLE
"$TASK_RUN_DIR/name_policy_v2" "$TASK_RUN_DIR/disabled-pack" disabled > "$TASK_RUN_DIR/name-policy-disabled.txt"

python3 - <<'CHECK'
import json, os
from pathlib import Path
root = Path(os.environ['TASK_RUN_DIR'])
load = lambda name: json.loads((root / name).read_text())
expected = {row['id']: sorted(row['expected']) for row in load('version-probes.json')}
versions = load('versions.json')
print('version cases:', sum(sorted(row['versions']) == expected[row['id']] for row in versions), '/', len(versions))
extraction = load('extraction.json')
print('unwanted projects:', extraction['unwanted_project_candidates'])
for field in ('claims', 'principles'):
    print(field, 'negative / positive:', sum(row[field] for row in extraction['rows'] if row['bad']), '/', sum(row[field] for row in extraction['rows'] if not row['bad']))
print('valid names:', extraction['valid_project_names_found'], '/', extraction['valid_project_names_total'])
print('raw observations:', sum(row['raw_preserved'] for row in extraction['rows']), '/', len(extraction['rows']))
rows = [row for row in load('paradigms.json') if row['mode'] == 'accepted']
print('accepted paradigm cases:', sum(row['correct'] for row in rows), '/', len(rows))
validation = load('policy-validation.json')
print('aggregation validation:', sum(row['correct'] for row in validation), '/', len(validation))
CHECK

python3 loom/tools/eval/knowledge_eval.py synthetic \
  --loom "$TASK_RUN_DIR/build/cli/loom" --work "$TASK_RUN_DIR/synthetic" \
  --out "$TASK_RUN_DIR/synthetic.json"
```

Full CTest retains the repository's existing gates unchanged. Both measured
normal `selfhost` runs use the committed baseline source with different binaries,
so INTERFEJS changes cannot move the comparison corpus. The implementation is
on the newer integration base. For the matched frozen-source comparison,
stage the baseline worktree once and reuse the **same** `$TASK_RUN_DIR/frozen`
path for both binaries:

```bash
git worktree add --detach "$TASK_RUN_DIR/baseline" 9d15d2dd0733274356f768816e207e26e13845e4
python3 loom/tools/eval/knowledge_eval.py bench stage \
  --repo "$TASK_RUN_DIR/baseline" --dest "$TASK_RUN_DIR/frozen"
cmake -S "$TASK_RUN_DIR/baseline/loom" -B "$TASK_RUN_DIR/baseline-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DLOOM_BUILD_TESTS=ON -DLOOM_BUILD_CLI=ON \
  -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_SHARED=ON -DLOOM_WERROR=ON
cmake --build "$TASK_RUN_DIR/baseline-build" -j 2
python3 loom/tools/eval/knowledge_eval.py bench run \
  --loom "$TASK_RUN_DIR/baseline-build/cli/loom" \
  --input "$TASK_RUN_DIR/frozen" --work "$TASK_RUN_DIR/bench" \
  --label before --out "$TASK_RUN_DIR/bench-before.json"
python3 loom/tools/eval/knowledge_eval.py bench run \
  --loom "$TASK_RUN_DIR/build/cli/loom" \
  --input "$TASK_RUN_DIR/frozen" --work "$TASK_RUN_DIR/bench" \
  --label after --out "$TASK_RUN_DIR/bench-after.json"
python3 loom/tools/eval/knowledge_eval.py selfhost \
  --loom "$TASK_RUN_DIR/baseline-build/cli/loom" --repo "$TASK_RUN_DIR/baseline" \
  --work "$TASK_RUN_DIR/selfhost-before" --out "$TASK_RUN_DIR/products-before"
python3 loom/tools/eval/knowledge_eval.py selfhost \
  --loom "$TASK_RUN_DIR/build/cli/loom" --repo "$TASK_RUN_DIR/baseline" \
  --work "$TASK_RUN_DIR/selfhost-after" --out "$TASK_RUN_DIR/products-after"
```

Replay the rejected branch separately (never move `main` to it):

```bash
git worktree add --detach "$TASK_RUN_DIR/negative" caa2619af2594bd18872354517bf663027b3afe4
cmake -S "$TASK_RUN_DIR/negative/loom" -B "$TASK_RUN_DIR/negative-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DLOOM_BUILD_TESTS=ON -DLOOM_BUILD_CLI=ON \
  -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_SHARED=ON -DLOOM_WERROR=ON
cmake --build "$TASK_RUN_DIR/negative-build" -j 2
ctest --test-dir "$TASK_RUN_DIR/negative-build" --output-on-failure -j 2
```

<details>
<summary>Complete reproducible probe sources and cases</summary>

### `version-probes.cpp`

<!-- replay-file: version-probes.cpp -->
```cpp
#include <iostream>
#include <map>
#include "loom/extract.h"
#include "loom/kb.h"
using namespace loom;
int main() {
  auto p = kb::Pack::load_builtin();
  if (!p) return 2;
  extract::Extractor extractor(p.value());
  Json inputs;
  std::cin >> inputs;
  Json outputs = Json::array();
  for (const auto& input : inputs) {
    auto unit = extract::text_unit(input.at("id").get<std::string>() + ".md", input.at("text").get<std::string>(), "2026-10-04");
    auto extraction = extractor.process(unit);
    if (!extraction) return 3;
    std::map<std::string, std::string> labels;
    for (const auto& entity : extraction.value().entities) labels[entity.id] = entity.label;
    Json versions = Json::array();
    for (const auto& claim : extraction.value().claims) {
      if (claim.predicate == "has_version") versions.push_back(Json::array({labels[claim.subject], claim.value}));
    }
    outputs.push_back(Json{{"id",input.at("id")},{"versions",versions},{"extraction",extraction.value().to_json()}});
  }
  std::cout << outputs.dump(2) << '\n';
}
```

### `version-probes.json`

<!-- replay-file: version-probes.json -->
```json
[
  {
    "id": "multi",
    "text": "ChatADHD v1.2, Loom SDK v2.3.",
    "expected": [
      [
        "ChatADHD",
        "1.2"
      ],
      [
        "Loom",
        "2.3"
      ]
    ]
  },
  {
    "id": "repeated",
    "text": "SQLite 1.2 and ChatADHD 1.2.",
    "expected": [
      [
        "SQLite",
        "1.2"
      ],
      [
        "ChatADHD",
        "1.2"
      ]
    ]
  },
  {
    "id": "dependency",
    "text": "ChatADHD v1.2 uses SQLite 3.47.2.",
    "expected": [
      [
        "ChatADHD",
        "1.2"
      ],
      [
        "SQLite",
        "3.47.2"
      ]
    ]
  },
  {
    "id": "language",
    "text": "ChatADHD v1.2 uses Python 3.11.",
    "expected": [
      [
        "ChatADHD",
        "1.2"
      ],
      [
        "Python",
        "3.11"
      ]
    ]
  },
  {
    "id": "measurement",
    "text": "ChatADHD version 1.2 latency 2.5 ms.",
    "expected": [
      [
        "ChatADHD",
        "1.2"
      ]
    ]
  },
  {
    "id": "percent",
    "text": "ChatADHD version 1.2 has a 3.5% error rate.",
    "expected": [
      [
        "ChatADHD",
        "1.2"
      ]
    ]
  },
  {
    "id": "dotted",
    "text": "ChatADHD version 1.2 uses address 1.2.3.4.",
    "expected": [
      [
        "ChatADHD",
        "1.2"
      ]
    ]
  },
  {
    "id": "wildcard",
    "text": "ChatADHD version 1.2 compares 0.6.x.",
    "expected": [
      [
        "ChatADHD",
        "1.2"
      ]
    ]
  },
  {
    "id": "python_only",
    "text": "Python 3.11.",
    "expected": [
      [
        "Python",
        "3.11"
      ]
    ]
  },
  {
    "id": "cmake",
    "text": "```cmake\nproject(ChatADHD VERSION 1.2.3)\n```",
    "expected": [
      [
        "ChatADHD",
        "1.2.3"
      ]
    ]
  },
  {
    "id": "dunder",
    "text": "```python\n__version__ = \"0.07.09\"\n```",
    "expected": [
      [
        "dunder.md",
        "0.7.9"
      ]
    ]
  },
  {
    "id": "pl",
    "text": "ChatADHD wersja 1.2 oraz Loom SDK wersja 2.3.",
    "expected": [
      [
        "ChatADHD",
        "1.2"
      ],
      [
        "Loom",
        "2.3"
      ]
    ]
  },
  {
    "id": "paren_dependency",
    "text": "ChatADHD v1.2 uses SQLite (3.47.2).",
    "expected": [
      [
        "ChatADHD",
        "1.2"
      ],
      [
        "SQLite",
        "3.47.2"
      ]
    ]
  },
  {
    "id": "quoted_dependency",
    "text": "ChatADHD v1.2 uses Python version = \"3.11\".",
    "expected": [
      [
        "ChatADHD",
        "1.2"
      ],
      [
        "Python",
        "3.11"
      ]
    ]
  },
  {
    "id": "marked_dependency",
    "text": "ChatADHD v1.2 uses **SQLite** 3.47.2.",
    "expected": [
      [
        "ChatADHD",
        "1.2"
      ],
      [
        "SQLite",
        "3.47.2"
      ]
    ]
  }
]
```

### `extraction_corpus_v2.cpp`

<!-- replay-file: extraction_corpus_v2.cpp -->
```cpp
#include <iostream>
#include <vector>
#include "loom/extract.h"
#include "loom/kb.h"
#include "loom/model.h"
using namespace loom;
int main(int argc, char** argv) {
  auto pp=kb::Pack::load_dir(argv[1]); if (!pp) { std::cerr<<pp.error().message<<'\n'; return 2; }
  const auto p=pp.value(); extract::Extractor ex(p);
  auto type=model::artifact_type(*p,"specification").value();
  type.extract=Json::array({Json{{"op","entities_lexicon"}},Json{{"op","items"}},Json{{"op","decisions"}},Json{{"op","status_cues"}},Json{{"op","normative"}}});
  struct Case { const char* text; const char* wanted; bool bad; };
  std::vector<Case> cases={
    {"project (const Json& d)","",true},
    {"project (binding=\"x\")","",true},
    {"project (graph: dict)","",true},
    {"project (getValue())","",true},
    {"project: route_chat","",true},
    {"void ProjectEngine() {","",true},
    {"class ProjectEngine:","",true},
    {"const Json& requirement = never;","",true},
    {"must = \"keep source bytes\";","",true},
    {"thing.shouldNever();","",true},
    {"project (X)","X",false},
    {"project (A3)","A3",false},
    {"project (C++)","C++",false},
    {"project (C#)","C#",false},
    {"project: Łódź","Łódź",false},
    {"project: Ż","Ż",false},
    {"project: Москва","Москва",false},
    {"project: 研究","研究",false},
    {"project (Rock & Roll)","Rock & Roll",false},
    {"Never delete source bytes; always preserve originals.","",false},
    {"We must use graph -> packet transformation.","",false},
    {"// Must not delete source bytes.","",false},
    {"format: Json must always preserve unknown fields.","",false},
    {"privacy = \"local-first\" must always be enforced.","",false},
  };
  Json rows=Json::array(); int unwanted=0,positive_found=0,positive_total=0;
  for (const auto& c:cases) {
    auto u=extract::text_unit("probe.md",c.text,"2026-10-04");
    model::Observation o; o.unit=u.unit.id; o.text=c.text; o.kind=model::ObservationKind::Sentence; o.locator=u.unit.locator;
    o.locator.byte_start=0; o.locator.byte_len=u.text.size(); o.id=model::Observation::make_id(o.unit,o.locator,o.text);
    auto out=ex.extract(type,u,{o}); if (!out) { std::cerr<<out.error().message<<'\n'; return 3; }
    const auto& e=out.value(); Json labels=Json::array(); bool found=false;
    for (const auto& v:e.entities) { if (v.kind=="project") labels.push_back(v.label); found=found || (v.kind=="project" && v.label==c.wanted); }
    if (c.bad) unwanted+=static_cast<int>(labels.size());
    if (*c.wanted) { ++positive_total; positive_found+=found; }
    rows.push_back(Json{{"text",c.text},{"bad",c.bad},{"projects",labels},{"claims",e.claims.size()},{"principles",e.principles.size()},{"observations",e.observations.size()},{"raw_preserved",e.observations.size()==1 && e.observations[0].text==c.text},{"positive_found",*c.wanted?Json(found):Json()}});
  }
  std::cout<<Json{{"unwanted_project_candidates",unwanted},{"valid_project_names_found",positive_found},{"valid_project_names_total",positive_total},{"rows",rows}}.dump(1)<<'\n';
}
```

### `paradigm_probe.cpp`

<!-- replay-file: paradigm_probe.cpp -->
```cpp
#include <iostream>
#include "loom/generalize.h"
#include "loom/kb.h"
using namespace loom;
using namespace loom::generalize;
struct Builder {
  Evidence e; int ordinal=0;
  std::string subject(const std::string& key) {
    model::Entity entity; entity.kind="project"; entity.canonical_key=key; entity.label=key;
    entity.id=model::Entity::make_id(entity.kind,key); e.entities.push_back(entity); return entity.id;
  }
  std::string obs(const std::string& text,const std::string& unit="u") {
    model::Observation o; o.unit=unit; o.text=text; o.ordinal=ordinal++; o.kind=model::ObservationKind::Sentence;
    o.locator.source="sha256:probe"; o.locator.json_pointer="/"+std::to_string(o.ordinal);
    o.id=model::Observation::make_id(unit,o.locator,text); e.observations.push_back(o); return o.id;
  }
  void support(const std::string& subject,const std::string& observation, model::EvidenceClass evidence=model::EvidenceClass::Observed,
               model::ClaimStatus status=model::ClaimStatus::Active) {
    model::Claim c; c.subject=subject; c.predicate="mentioned_in"; c.value=e.observations.back().unit;
    c.qualifiers.scope=observation; c.assessment.evidence=evidence; c.assessment.status=status; c.assessment.confidence=.8;
    c.assessment.support.push_back(model::Support{observation,{},"","test@1",1.0});
    c.id=model::Claim::make_id(c.subject,c.predicate,c.object,c.value,c.qualifiers); e.claims.push_back(c);
  }
};
std::shared_ptr<const kb::Pack> pack(const std::string& mode){
  auto base=kb::Pack::load_builtin(); if(!base){std::cerr<<base.error().message<<"\n"; exit(2);}
  std::map<std::string,Json> docs; for(const auto& path:(*base)->files()) docs[path]=(*base)->file(path);
  auto& anchors=docs["lexicons/cues.json"]["anchor_policy"];
  anchors=Json::object();
  if(mode!="legacy") {
    bool dominant=mode.find("dominant")==0;
    int window=mode.find("window2")!=std::string::npos?2:1;
    bool accepted=mode=="accepted" || mode=="accepted_per_unit";
    bool across=accepted || mode.find("across")!=std::string::npos;
    double music_weight=accepted?2.5:3.0;
    double research_weight=accepted || mode.find("35")!=std::string::npos?3.5:5.0;
    if(accepted) { dominant=false; window=2; }
    std::string aggregation=mode=="accepted_per_unit" || !across?"per_unit":"distinct_across_units";
    anchors["paradigm.music"]=Json{{"min_weight",music_weight},{"window",window},{"dominant_context",dominant},{"aggregation",aggregation}};
    anchors["paradigm.research"]=Json{{"min_weight",research_weight},{"window",accepted?1:window},{"dominant_context",dominant},{"aggregation",aggregation}};
  }
  auto p=kb::Pack::from_documents(std::move(docs)); if(!p){std::cerr<<p.error().message<<"\n"; exit(2);} return *p;
}
bool matches(const std::shared_ptr<const kb::Pack>& p,const Builder& b,const std::string& id,Json* trace,const std::string& paradigm="music"){
  auto ms=ParadigmMatcher(p).match_projects(b.e); if(!ms){std::cerr<<ms.error().message<<"\n";exit(2);}
  for(auto& m:*ms) if(m.instance.paradigm==paradigm && m.instance.subject==id){if(trace)*trace=m.reasons;return true;} return false;
}
int main(){
  Json out=Json::array();
  for(const std::string mode:{"legacy","local","dominant","local_research35","dominant_research35","local_window2_research35","accepted","accepted_per_unit"}) {
    auto p=pack(mode);
    auto run=[&](const std::string& name,Builder b,const std::string& subject,bool expected,const std::string& paradigm="music"){
      Json trace; bool result=matches(p,b,subject,&trace,paradigm);
      out.push_back(Json{{"mode",mode},{"paradigm",paradigm},{"case",name},{"expected",expected},{"actual",result},{"correct",result==expected},{"trace",trace}});
    };
    {Builder b; auto s=b.subject("Software"); for(int i=0;i<30;++i){auto o=b.obs("Software: keep track of the master branch");b.support(s,o);} run("repeated_ambiguous_tokens",b,s,false);}
    {Builder b; auto s=b.subject("Song"); auto o=b.obs("Song: chorus, verse, bpm");b.support(s,o); run("single_observation_distinct_music",b,s,true);}
    {Builder b; auto s=b.subject("Song");auto o=b.obs("Song arrangement");b.support(s,o);b.obs("chorus");b.obs("verse");run("subject_owns_unit_neighbour_cues",b,s,true);}
    {Builder b; auto s=b.subject("Software"); auto m=b.subject("Song");auto o=b.obs("Software module");b.support(s,o);for(int i=0;i<4;++i)b.obs("neutral filler");o=b.obs("Song chorus");b.support(m,o);b.obs("verse bpm");run("mixed_subject_wrong_domain",b,s,false);run("mixed_subject_true_domain",b,m,true);}
    {Builder b;auto s=b.subject("Software");for(int i=0;i<20;++i){auto o=b.obs("chorus");b.support(s,o);}run("repeated_one_strong_cue",b,s,false);}
    {Builder b;auto s=b.subject("SongAcrossUnits");auto o=b.obs("chorus","u1");b.support(s,o);o=b.obs("verse","u2");b.support(s,o);run("cross_unit_valid_subject_evidence",b,s,true);}

    {Builder b;auto s=b.subject("Software");auto o=b.obs("Software module implementation","u1");b.support(s,o);b.obs("chorus verse bpm","unrelated");b.obs("chorus verse bpm","unrelated");run("unrelated_units_not_pooled",b,s,false);}
    {Builder b;auto s=b.subject("Software");auto o=b.obs("chorus verse bpm");b.support(s,o,model::EvidenceClass::Observed,model::ClaimStatus::Rejected);b.obs("chorus verse bpm");run("rejected_support_not_an_anchor",b,s,false);}
    {Builder b;auto s=b.subject("Software");auto o=b.obs("chorus verse bpm");b.support(s,o,model::EvidenceClass::Inferred);b.obs("chorus verse bpm");run("inferred_support_not_an_anchor",b,s,false);}
    {Builder b;auto s=b.subject("OwnerMusic");b.e.entities.front().attrs=Json{{"kind_hint","music"}};auto o=b.obs("master branch");b.support(s,o);run("explicit_kind_hint_preserved",b,s,true);}
    {Builder b;auto s=b.subject("Software");auto o=b.obs("Software documentation");b.support(s,o);for(int i=0;i<4;++i)b.obs("neutral filler");b.obs("meta-model example table: music | chorus | verse");b.obs("meta-model example table: bpm");run("dominant_mixed_domain_table",b,s,false);}
    {Builder b;auto s=b.subject("Petri");auto o=b.obs("Petri hypothesis is testable");b.support(s,o);o=b.obs("Petri experiment will test the hypothesis");b.support(s,o);run("unhinted_research_two_clear_cues",b,s,true,"research");}
    {Builder b;auto s=b.subject("Petri");auto o=b.obs("Petri hypothesis; an experiment tests it");b.support(s,o);run("unhinted_research_one_observation",b,s,true,"research");}
    {Builder b;auto s=b.subject("Software");auto o=b.obs("Software function takes a dataset parameter");b.support(s,o);o=b.obs("Software has an experiment flag option");b.support(s,o);run("ambiguous_research_implementation_tokens",b,s,false,"research");}
    {Builder b;auto s=b.subject("Petri");auto o=b.obs("Petri hypothesis and experiment, using a dataset to falsify our predictions");b.support(s,o);run("unhinted_research_full_vocabulary",b,s,true,"research");}

  }
  std::cout<<out.dump(1)<<"\n";
}
```

### `name_policy_v2.cpp`

<!-- replay-file: name_policy_v2.cpp -->
```cpp
#include <iostream>
#include <vector>
#include "extract/lexicon.h"
#include "loom/kb.h"

using namespace loom;
int main(int argc, char** argv) {
  auto p = kb::Pack::load_dir(argv[1]);
  if (!p) { std::cerr << p.error().message << '\n'; return 2; }
  extract::detail::Lexicons lex(*p.value(), Json::array());
  int good = 0, total = 0;
  auto check = [&](bool result, bool want, const char* label) {
    ++total; good += result == want;
    std::cout << (result == want ? "PASS " : "FAIL ") << label << " actual=" << result << " expected=" << want << '\n';
  };
  if (argc > 2) {
    check(lex.name_ok("project", "const Json& d", "specification"), true, "policy disabled allows candidate syntax");
    check(lex.name_ok("project", "route_chat", "specification"), true, "policy disabled allows lowercase project");
    check(lex.prose_ok("x = \"must not\""), true, "policy disabled allows syntax prose");
    std::cout << "checks " << good << '/' << total << '\n';
    return good == total ? 0 : 1;
  }
  for (const auto* n : {"X", "A3", "C++", "C#", "Łódź", "Ż", "Москва", "研究", "Rock & Roll", "Hello World"})
    check(lex.name_ok("project", n, "specification"), true, n);
  for (const auto* n : {"const Json& d", "graph: dict", "route_chat", "binding=\"x\"", "thing.cpp", "Graph::Node", "getValue()"})
    check(lex.name_ok("project", n, "specification"), false, n);
  check(lex.name_ok("project", "generator reelsów", "conversation"), true, "lowercase conversational project");
  check(lex.name_ok("project", "generator dla nas", "conversation"), true, "conversational stopword edges");
  check(lex.name_ok("project", "route_chat", "specification", false), true, "explicit project snake_case");
  for (const auto* t : {"Always preserve source bytes; never delete them.", "We must use graph -> packet transformation.", "// Must not delete sources.", "class of models must be configurable", "Import data before analysis.", "From this result we should derive the policy.", "privacy = never upload raw sources", "Budget = must never exceed the user setting", "format: Json must always preserve unknown fields.", "privacy = \"local-first\" must always be enforced."})
    check(lex.prose_ok(t), true, t);
  for (const auto* t : {"const Json& d = value;", "void projectEngine() {", "graph: dict", "x = \"must not\"", "std::vector<Thing> values;", "class ProjectGraph:", "import os", "thing.shouldNever();", "count = 42;", "ratio = -1.25e-3", "enabled = true;", "value = other", "value = std::move(other);", "items = [1, 2];", "graph: dict[str, int]", "graph: Json = other;"})
    check(lex.prose_ok(t), false, t);
  std::cout << "checks " << good << '/' << total << '\n';
  return good == total ? 0 : 1;
}
```

### `policy_validation_probe.cpp`

<!-- replay-file: policy_validation_probe.cpp -->
```cpp
#include <iostream>
#include "loom/generalize.h"
#include "loom/kb.h"
using namespace loom;
int main(){
  auto builtin=kb::Pack::load_builtin();if(!builtin){std::cerr<<builtin.error().message<<"\n";return 2;}
  generalize::Evidence evidence;model::Entity entity;entity.kind="project";entity.canonical_key="song";entity.label="Song";entity.id=model::Entity::make_id(entity.kind,entity.canonical_key);evidence.entities.push_back(entity);
  model::Observation o;o.unit="unit";o.text="Song chorus verse bpm";o.ordinal=0;o.id="obs";evidence.observations.push_back(o);
  model::Claim c;c.subject=entity.id;c.predicate="mentioned_in";c.value=o.unit;c.id=model::Claim::make_id(c.subject,c.predicate,c.object,c.value,c.qualifiers);c.assessment.support.push_back(model::Support{o.id,{},"","test@1",1});evidence.claims.push_back(c);
  Json out=Json::array();bool all_ok=true;
  for(const Json strategy:Json::array({"distinct_across_units","per_unit","unsupported",false,17,Json::object(),Json::array(),nullptr})){
    std::map<std::string,Json> docs;for(auto& path:(*builtin)->files())docs[path]=(*builtin)->file(path);
    docs["lexicons/cues.json"]["anchor_policy"]["paradigm.music"]=Json{{"min_weight",2.5},{"window",2},{"dominant_context",false},{"aggregation",strategy}};
    auto p=kb::Pack::from_documents(std::move(docs));if(!p){std::cerr<<p.error().message<<"\n";return 2;}
    auto result=generalize::ParadigmMatcher(*p).match_projects(evidence);
    bool expected=strategy.is_string() && (strategy=="distinct_across_units" || strategy=="per_unit");
    bool correct=expected?result.has_value():!result && result.error().code==Errc::InvalidArgument;
    all_ok &= correct;Json r{{"aggregation",strategy},{"expected_success",expected},{"actual_success",result.has_value()},{"correct",correct}};
    if(!result){r["error_code"]=std::string(errc_name(result.error().code));r["error"]=result.error().message;}
    out.push_back(std::move(r));
  }
  std::cout<<out.dump(1)<<"\n";return all_ok?0:1;
}
```

### `fixture_probe.cpp`

<!-- replay-file: fixture_probe.cpp -->
```cpp
#include <iostream>
#include "test_generalize_fixture.h"
using namespace loom;
int main(){
  auto builtin=kb::Pack::load_builtin(); if(!builtin){std::cerr<<builtin.error().message<<"\n";return 2;}
  Json report=Json::array();
  for(const std::string mode:{"legacy","per_unit","distinct_across_units"}){
    std::map<std::string,Json> docs;for(auto& f:(*builtin)->files())docs[f]=(*builtin)->file(f);
    auto& policy=docs["lexicons/cues.json"]["anchor_policy"];policy=Json::object();
    if(mode!="legacy"){
      policy["paradigm.music"]=Json{{"min_weight",2.5},{"window",2},{"dominant_context",false},{"aggregation",mode}};
      policy["paradigm.research"]=Json{{"min_weight",3.5},{"window",1},{"dominant_context",false},{"aggregation",mode}};
    }
    auto p=kb::Pack::from_documents(std::move(docs));if(!p){std::cerr<<p.error().message<<"\n";return 2;}
    auto fixture=test::gfix::load(**p);auto matches=generalize::ParadigmMatcher(*p).match_projects(fixture.ev);
    if(!matches){std::cerr<<matches.error().message<<"\n";return 2;}
    for(const auto& [pid,entity]:fixture.project_entity){
      Json alternatives=Json::array();std::string best;double best_score=0;
      for(const auto& m:*matches)if(m.instance.subject==entity){
        if(m.score>best_score){best=m.instance.paradigm;best_score=m.score;}
        alternatives.push_back(Json{{"kind",m.instance.paradigm},{"score",m.score},{"reasons",m.reasons}});
      }
      auto expected=test::gfix::project_kind_for(fixture.kind_of[pid]);
      report.push_back(Json{{"mode",mode},{"project",pid},{"expected",expected},{"best",best},{"correct",best==expected},{"alternatives",alternatives}});
    }
  }
  std::cout<<report.dump(1)<<"\n";
}
```

</details>

<details><summary>Final complete CTest output</summary>

```text
Test project /workspace/scratch/98e6ad903811/chatadhd/loom/build/dev
        Start  99: research.structure
        Start 103: research.contracts
  1/108 Test  #99: research.structure .................................   Passed   39.74 sec
        Start  54: unit.test_import_stream
  2/108 Test #103: research.contracts .................................   Passed   41.19 sec
        Start  32: unit.test_context_diagnostics
  3/108 Test  #32: unit.test_context_diagnostics ......................   Passed   11.23 sec
        Start  88: compat.test_crypto_compat
  4/108 Test  #88: compat.test_crypto_compat ..........................   Passed   12.02 sec
        Start  55: unit.test_kb_pack
  5/108 Test  #55: unit.test_kb_pack ..................................   Passed    6.94 sec
        Start  56: unit.test_knowledge
  6/108 Test  #56: unit.test_knowledge ................................   Passed    6.57 sec
        Start  96: compat.test_semantic_compat
  7/108 Test  #54: unit.test_import_stream ............................   Passed   44.39 sec
        Start   9: unit.test_catalog_links
  8/108 Test  #96: compat.test_semantic_compat ........................   Passed    8.84 sec
        Start  18: unit.test_chat_active_task_durable_audit
  9/108 Test   #9: unit.test_catalog_links ............................   Passed    6.07 sec
        Start 104: research.seeding
 10/108 Test  #18: unit.test_chat_active_task_durable_audit ...........   Passed   10.62 sec
        Start  14: unit.test_chat_active_task_acceptance_scale
 11/108 Test #104: research.seeding ...................................   Passed    8.25 sec
        Start  40: unit.test_crypto
 12/108 Test  #14: unit.test_chat_active_task_acceptance_scale ........   Passed    4.94 sec
        Start  39: unit.test_context_tfidf_reuse
 13/108 Test  #40: unit.test_crypto ...................................   Passed    4.66 sec
        Start  61: unit.test_knowledge_semantic_graph
 14/108 Test  #61: unit.test_knowledge_semantic_graph .................   Passed    3.55 sec
        Start  74: unit.test_resolve_attribution
 15/108 Test  #39: unit.test_context_tfidf_reuse ......................   Passed    6.94 sec
        Start  43: unit.test_extract
 16/108 Test  #43: unit.test_extract ..................................   Passed    2.75 sec
        Start  30: unit.test_context_controls
 17/108 Test  #74: unit.test_resolve_attribution ......................   Passed    6.47 sec
        Start  98: compat.test_util_compat
 18/108 Test  #30: unit.test_context_controls .........................   Passed    1.99 sec
        Start  60: unit.test_knowledge_semantic
 19/108 Test  #98: compat.test_util_compat ............................   Passed    4.71 sec
        Start  26: unit.test_chat_knowledge_context
 20/108 Test  #60: unit.test_knowledge_semantic .......................   Passed    3.90 sec
        Start  86: compat.test_chat_compat
 21/108 Test  #26: unit.test_chat_knowledge_context ...................   Passed    4.60 sec
        Start  75: unit.test_resolve_lineage
 22/108 Test  #86: compat.test_chat_compat ............................   Passed    5.21 sec
        Start  97: compat.test_semantic_recovery
 23/108 Test  #75: unit.test_resolve_lineage ..........................   Passed    2.88 sec
        Start  91: compat.test_graph_compat
 24/108 Test  #97: compat.test_semantic_recovery ......................   Passed    2.79 sec
        Start  27: unit.test_chat_retrieval_plan
 25/108 Test  #27: unit.test_chat_retrieval_plan ......................   Passed    2.51 sec
        Start  36: unit.test_context_plan
 26/108 Test  #91: compat.test_graph_compat ...........................   Passed    3.16 sec
        Start 107: cli.smoke
 27/108 Test #107: cli.smoke ..........................................   Passed    3.09 sec
        Start  94: compat.test_memory_compat
 28/108 Test  #36: unit.test_context_plan .............................   Passed    3.87 sec
        Start   1: unit.test_archive
 29/108 Test   #1: unit.test_archive ..................................   Passed    1.55 sec
        Start  85: compat.test_chat_active_task_http
 30/108 Test  #94: compat.test_memory_compat ..........................   Passed    2.69 sec
        Start   6: unit.test_catalog
 31/108 Test  #85: compat.test_chat_active_task_http ..................   Passed    3.26 sec
        Start  35: unit.test_context_native_consumer
 32/108 Test   #6: unit.test_catalog ..................................   Passed    3.50 sec
        Start  52: unit.test_import_exports
 33/108 Test  #35: unit.test_context_native_consumer ..................   Passed    1.73 sec
        Start  44: unit.test_extract_eval
 34/108 Test  #52: unit.test_import_exports ...........................   Passed    2.49 sec
        Start  46: unit.test_generalize
 35/108 Test  #44: unit.test_extract_eval .............................   Passed    3.29 sec
        Start   7: unit.test_catalog_context
 36/108 Test  #46: unit.test_generalize ...............................   Passed    2.93 sec
        Start   3: unit.test_capi
 37/108 Test   #7: unit.test_catalog_context ..........................   Passed    1.91 sec
        Start  47: unit.test_generalize_eval
 38/108 Test  #47: unit.test_generalize_eval ..........................   Passed    1.30 sec
        Start  31: unit.test_context_counter
 39/108 Test  #31: unit.test_context_counter ..........................   Passed    0.83 sec
        Start  23: unit.test_chat_active_task_revisions
 40/108 Test   #3: unit.test_capi .....................................   Passed    3.08 sec
        Start  68: unit.test_pack_model
 41/108 Test  #68: unit.test_pack_model ...............................   Passed    1.30 sec
        Start 105: server.smoke
 42/108 Test  #23: unit.test_chat_active_task_revisions ...............   Passed    1.65 sec
        Start  59: unit.test_knowledge_flow
 43/108 Test  #59: unit.test_knowledge_flow ...........................   Passed    1.28 sec
        Start  37: unit.test_context_retrieval
 44/108 Test  #37: unit.test_context_retrieval ........................   Passed    0.86 sec
        Start  84: compat.test_candidate_graph_native
 45/108 Test  #84: compat.test_candidate_graph_native .................   Passed    1.94 sec
        Start  89: compat.test_db_compat
 46/108 Test #105: server.smoke .......................................   Passed    5.31 sec
        Start  10: unit.test_catalog_retention
 47/108 Test  #10: unit.test_catalog_retention ........................   Passed    2.22 sec
        Start  57: unit.test_knowledge_candidate_graph
 48/108 Test  #57: unit.test_knowledge_candidate_graph ................   Passed    2.04 sec
        Start  63: unit.test_materialize
 49/108 Test  #89: compat.test_db_compat ..............................   Passed    5.24 sec
        Start  29: unit.test_context_channel_integration
 50/108 Test  #29: unit.test_context_channel_integration ..............   Passed    1.04 sec
        Start  12: unit.test_chat_active_task
 51/108 Test  #63: unit.test_materialize ..............................   Passed    1.93 sec
        Start  83: compat.test_active_task_compiler_compat
 52/108 Test  #12: unit.test_chat_active_task .........................   Passed    1.04 sec
        Start 106: server.chat_active_task
 53/108 Test  #83: compat.test_active_task_compiler_compat ............   Passed    0.96 sec
        Start  15: unit.test_chat_active_task_acceptance_validation
 54/108 Test #106: server.chat_active_task ............................   Passed    1.43 sec
        Start 108: eval.harness
 55/108 Test  #15: unit.test_chat_active_task_acceptance_validation ...   Passed    1.23 sec
        Start  22: unit.test_chat_active_task_retention
 56/108 Test #108: eval.harness .......................................   Passed    1.01 sec
        Start   8: unit.test_catalog_eval
 57/108 Test  #22: unit.test_chat_active_task_retention ...............   Passed    0.76 sec
        Start  38: unit.test_context_retrieval_dev
 58/108 Test   #8: unit.test_catalog_eval .............................   Passed    1.15 sec
        Start  93: compat.test_import_compat
 59/108 Test  #38: unit.test_context_retrieval_dev ....................   Passed    1.78 sec
        Start  13: unit.test_chat_active_task_acceptance_audit
 60/108 Test  #93: compat.test_import_compat ..........................   Passed    1.65 sec
        Start  92: compat.test_graph_packet_store
 61/108 Test  #13: unit.test_chat_active_task_acceptance_audit ........   Passed    1.80 sec
        Start  34: unit.test_context_eval
 62/108 Test  #34: unit.test_context_eval .............................   Passed    0.96 sec
        Start 101: research.graph_native_protocol
 63/108 Test #101: research.graph_native_protocol .....................   Passed    0.52 sec
        Start  17: unit.test_chat_active_task_durability
 64/108 Test  #17: unit.test_chat_active_task_durability ..............   Passed    0.64 sec
        Start 102: research.candidate_graph_protocol
 65/108 Test  #92: compat.test_graph_packet_store .....................   Passed    3.39 sec
        Start   5: unit.test_capi_knowledge_latest_run
 66/108 Test #102: research.candidate_graph_protocol ..................   Passed    0.56 sec
        Start  16: unit.test_chat_active_task_concurrency
 67/108 Test   #5: unit.test_capi_knowledge_latest_run ................   Passed    1.04 sec
        Start   4: unit.test_capi_knowledge
 68/108 Test  #16: unit.test_chat_active_task_concurrency .............   Passed    0.66 sec
        Start  73: unit.test_resolve
 69/108 Test  #73: unit.test_resolve ..................................   Passed    0.27 sec
        Start  41: unit.test_db
 70/108 Test   #4: unit.test_capi_knowledge ...........................   Passed    0.74 sec
        Start  19: unit.test_chat_active_task_exceptional
 71/108 Test  #41: unit.test_db .......................................   Passed    0.70 sec
        Start  50: unit.test_import
 72/108 Test  #19: unit.test_chat_active_task_exceptional .............   Passed    0.27 sec
        Start  82: compat.test_abi_compat
 73/108 Test  #50: unit.test_import ...................................   Passed    0.35 sec
        Start  53: unit.test_import_source_materialization
 74/108 Test  #82: compat.test_abi_compat .............................   Passed    0.43 sec
        Start  62: unit.test_knowledge_store
 75/108 Test  #53: unit.test_import_source_materialization ............   Passed    0.34 sec
        Start 100: research.independent_protocol
 76/108 Test  #62: unit.test_knowledge_store ..........................   Passed    0.30 sec
        Start  79: unit.test_semantic_worker
 77/108 Test #100: research.independent_protocol ......................   Passed    0.16 sec
        Start  90: compat.test_github_sync_defaults
 78/108 Test  #79: unit.test_semantic_worker ..........................   Passed    0.26 sec
        Start  24: unit.test_chat_active_task_scope_identity
 79/108 Test  #90: compat.test_github_sync_defaults ...................   Passed    0.26 sec
        Start  87: compat.test_config_compat
 80/108 Test  #24: unit.test_chat_active_task_scope_identity ..........   Passed    0.61 sec
        Start  51: unit.test_import_export_fidelity
 81/108 Test  #51: unit.test_import_export_fidelity ...................   Passed    0.19 sec
        Start  95: compat.test_selector_compat
 82/108 Test  #87: compat.test_config_compat ..........................   Passed    0.76 sec
        Start  69: unit.test_provenance
 83/108 Test  #69: unit.test_provenance ...............................   Passed    0.06 sec
        Start   2: unit.test_batch_api
 84/108 Test  #95: compat.test_selector_compat ........................   Passed    0.19 sec
        Start  45: unit.test_fts
 85/108 Test   #2: unit.test_batch_api ................................   Passed    0.07 sec
        Start  49: unit.test_graph
 86/108 Test  #45: unit.test_fts ......................................   Passed    0.09 sec
        Start  78: unit.test_semantic_llm
 87/108 Test  #49: unit.test_graph ....................................   Passed    0.10 sec
        Start  80: unit.test_tasks
 88/108 Test  #78: unit.test_semantic_llm .............................   Passed    0.06 sec
        Start  25: unit.test_chat_engine
 89/108 Test  #25: unit.test_chat_engine ..............................   Passed    0.12 sec
        Start  81: unit.test_util
 90/108 Test  #81: unit.test_util .....................................   Passed    0.08 sec
        Start  42: unit.test_event_bus
 91/108 Test  #42: unit.test_event_bus ................................   Passed    0.04 sec
        Start  64: unit.test_media
 92/108 Test  #64: unit.test_media ....................................   Passed    0.01 sec
        Start  48: unit.test_github
 93/108 Test  #48: unit.test_github ...................................   Passed    0.04 sec
        Start  20: unit.test_chat_active_task_history_aba
 94/108 Test  #20: unit.test_chat_active_task_history_aba .............   Passed    0.03 sec
        Start  58: unit.test_knowledge_candidates
 95/108 Test  #58: unit.test_knowledge_candidates .....................   Passed    0.03 sec
        Start  21: unit.test_chat_active_task_reference
 96/108 Test  #21: unit.test_chat_active_task_reference ...............   Passed    0.02 sec
        Start  65: unit.test_memory
 97/108 Test  #65: unit.test_memory ...................................   Passed    0.05 sec
        Start  76: unit.test_runtime
 98/108 Test  #76: unit.test_runtime ..................................   Passed    0.05 sec
        Start  72: unit.test_relations
 99/108 Test  #72: unit.test_relations ................................   Passed    0.03 sec
        Start  66: unit.test_model
100/108 Test  #66: unit.test_model ....................................   Passed    0.03 sec
        Start  71: unit.test_regex
101/108 Test  #71: unit.test_regex ....................................   Passed    0.01 sec
        Start  70: unit.test_providers
102/108 Test  #80: unit.test_tasks ....................................   Passed    0.58 sec
        Start  11: unit.test_catalog_scale
103/108 Test  #11: unit.test_catalog_scale ............................   Passed    0.01 sec
        Start  28: unit.test_config
104/108 Test  #70: unit.test_providers ................................   Passed    0.02 sec
        Start  77: unit.test_selector
105/108 Test  #77: unit.test_selector .................................   Passed    0.01 sec
        Start  67: unit.test_net_sse
106/108 Test  #67: unit.test_net_sse ..................................   Passed    0.01 sec
        Start  33: unit.test_context_engine
107/108 Test  #33: unit.test_context_engine ...........................   Passed    0.01 sec
108/108 Test  #28: unit.test_config ...................................   Passed    0.27 sec

100% tests passed out of 108

Total Test time (real) = 172.90 sec
```

</details>


## Prompt-data extension on the same branch

The extraction runtime now loads `semantic.relation` and
`semantic.occurrence_graph` from editable JSON-content `.prompt` contracts.
`semantic.analysis` is the exact legacy recipe prepared for its owners; the
existing legacy callers outside W1 remain unchanged. Builtins are generated
independently of the strict KB Pack and work without a source checkout.
Instructions, output schemas, decoding parameters, native validation caps and
former upper-limit presets are data. Owner overlays are explicit; changing
policy changes its canonical hash.

The existing knowledge API accepts `stage_params.extract.semantic` with
`prompt_id`, `prompt_patch`, `prompt_snapshot`, `preview`, `request_patch`,
`validation_mode` and `call_overrides` keyed by preview chunk ID. Durable
settings are `analysis_prompt_dir` and `analysis_prompt_overrides`.
Source-private `prompts::prepare_request()` previews the actual method, URL,
public headers, exact body bytes, transport and hashes without sending it.
Snapshots preserve the reviewed contract; one-call overrides affect only their
named chunk. These are current native APIs, not new exported C ABI or UI routes.
See [the full API and precedence description](../../loom/data/prompts/README.md).

Validation is explicitly `strict`, `lenient` or `off`. Schema diagnostics do not
rewrite unknown fields. Native source grounding is a separate check; invalid
native candidates remain unvalidated, pending and unpromoted. First HTTP
responses, including failures, are captured before decoding/accounting; capped
captures explicitly report truncation. Contract/schema/request/response hashes,
actual wire model/provider, mode and `model` origin remain inspectable in
provenance, cache and resume checks. No model-accuracy gain is claimed here.

| Extension control | Before | After |
|---|---|---|
| Relation/graph runtime recipe | C++ literals and fixed presets | Data contracts with owner and one-call overlays |
| Default request bodies | Three historical builders | 3/3 exact body matches; legacy public transport also checked |
| Registry offline proof | — | 43/43, including headers, Unicode, snapshots, validation and limits |
| Native scripted integration | — | 16/16 cases, 244/244 assertions |
| Actual latest W2 conditional ledger integration | — | 6/6, including absent-config defaults; zero paid calls |
| Final full CTest | Precision gate above | 108/108, 180.66 s; research 859 + 209 real cases |
| Synthetic before/after prompt migration | Precision scorecard | All metrics/stats unchanged; 65 deterministic products |
| Selfhost on the same pinned source | 12,863 claims / 1,006 products | 12,863 / 1,006, unchanged |

The before/after prompt-migration comparison also checks every stored row:
all **37,249 observations and 12,863 claims are byte-identical**, excluding only
their separate run-ID column. Five stage output hashes are unchanged. Stage input hashes include the run
ID; the materialize output hashes the products including `SELF.md` with that
ID. 1,005/1,006 products are exact
byte matches, and `SELF.md` differs only in its generated run-ID sentence.
Normalizing just that sentence gives 1,006/1,006 equal products. The comparison
and complete post-migration synthetic card are saved in the evidence directory.
No source/claim content is normalized away. Metadata-dependent hashes are not
claimed byte-identical.

Published prompt-module commits: `f407a7c` (registry/data), `aed85b8` (native
wiring, full gate and five-case conditional), `c66562c` (latest six-case W2).
Rejected wire-order and ordered-JSON-oracle replays are fully preserved at
`3a4f8b6aa4ac467e9508f388b62b349066d889a8` on the negative archive named
below: exact historical 0/3 wire, 3/3 content/semantics and 15/16, 240/242 oracle
failure reproduced. No archive-only files are in the accepted branch.

Proofs and standalone offline replay sources live in
`loom/src/extract/tests/`; evidence JSON pins source and linked archive hashes.
These new fixtures are outside CTest until its owner registers them; they do
not replace the complete existing CTest gate. The W2 test links exact public
`bbc95f73672f3c9c4f2cd8153108da1513a80dc3` header/policy/config objects before
our core archive. The new case checks the actual effective-options/settings
path with no stored config override. It is not the merged W2 `Config::get`
fallback/C-ABI build or concurrent dispatch locking proof. The previous 5/5
proof remains at `aed85b8d5b90db9c3055918e4864f8273bd3d411`.

W2 is used when its header/objects are present. Missing policy is reported as
unavailable; an explicitly required policy blocks execution. Token and monetary
forecasts remain unknown unless supplied, rather than treating an output cap as
predicted use. The fixture verifies confirmation before HTTP, same-run owner
confirmation plus one dispatch/cache resume, reported actual usage and unknown
usage retention. W2's null-estimate-to-known-actual `overrun` diagnostic is not
reported as an expected tenfold increase.

W7 supplied no new measured compatible winner on the audited `68f7531` tip.
The optional `jev_active_refute_v2.recipe` preserves its historical typed
`state/questions` request, payload/version hashes and 45/48 DEV supplied-candidate
score; truth accuracy is unknown. It needs a Decisions adapter and is not an
executable chat contract or a replacement extraction default. The 432 study
remains prepared, not executed. Historical 6/60 versus 38/60 belongs to a
saved-response decoder ablation, not a new prompt/model result.

W7's current source-regex reader still works through a generated, runtime-disabled
`#if 0` export. Its 4,208 bytes have exact SHA
`7527ac8c051e53bc5d6cbd51ca92ac9ead6334381d03e4e6924582797df43a1a`.
The generator checks this export and `prompt_contract_data.inc`; canonical
policy remains in `.prompt` data. This temporary compatibility representation
must be removed after W7 migrates its reader.

No paid calls, credentials, private archives, blind corpus or sealed answer key
were used. The remaining W11 inventory is not declared closed: calibration,
other extraction windows, resolver/generalization policy and candidate-graph
presets need their own measured increments. Existing `C++`/`Ż` misses and the
old precision limitations above remain. Full negative replay and infrastructure
history are kept on `archive/2026-10-04/knowledge-precision-prompt-negatives`, outside the accepted tree.

## Do wątku 2

Integrate the actual header, objects, Config defaults and C ABI before claiming
the guard is available in a normal build. Check the conditional proof against
the merged build. Null forecasts and configurable output caps are distinct;
`overrun=true` when unknown becomes known is preserved as W2's diagnostic.

## Do wątku 3

Use the shared composer/snapshot and per-call patches for the legacy
`chat/batch_api.cpp` consumer; preserve its provider wrappers. Jev needs its
separate typed Decisions adapter. A caller retains dispatch/retry state.

## Do wątku 4

Define shared graph identities for method/version/run and edges linking results
to method versions with W3/W6. W1's provenance JSON does not yet create those
shared method nodes. Do not invent a competing packet format in W1. Coordinate
KB Pack schema/manifest integration if prompt contracts become pack entries.
The current packet algebra and store are the shared basis. Persist method,
version and run as entities: the store does not persist packet `definitions`.
`compile_reply.host.recipe_sha256` can already carry `Contract.hash`, but its
current shape rejects additional method-version/run fields; agree those fields
before wiring the graph projection.

## Do wątku 7

Migrate `live_pilot.native_prompt()` from the source regex to prompt data or the
registry, then remove the generated disabled export. Send measured recipes with
input/output adapter identities and original denominators; prepared arms do not
become quality winners by selection alone. No paid work was run by W1.

## Do wątku 9

Integrate W2 before the W1 optional usage adapter; retain INTERFEJS and run the
full merged CTest/web build. Register the new standalone fixtures through the
CMake owner without weakening existing gates. Update INDEX from this report.
The broader W11 inventory and graph-method wiring remain explicit follow-ups.

## Do wątku 10

Expose effective definitions/hashes, full prepared queries, chunk IDs, one-call
patches, snapshots, validation diagnostics and usage capability. Existing
knowledge-stage JSON supports the native flow; add catalog/editor routes in
its owning scope. Distinguish `.prompt` contracts from W11 `.pack` profiles;
`profile list/inspect` currently does not discover the prompt registry.

## Do wątku 11

Wire `semantic_llm.cpp` and `worker/semantic_worker.cpp` to the prepared legacy
contract; W3 owns the batch caller. Preserve exact default prompt/body/public
headers and provider-specific wrappers. Move the remaining input minimum 20,
failure latch 5, merge factor 0.8 and array-repair policy into configurable data;
reset the latch on effective-recipe change and reject stale completions.
Use W11's actual `create_from_data_dir`, `create_with_profile`,
`to_unified_profile` and `profile_hash` interfaces for regex fallback/merge.
`Runtime::open` and the old static conversion are not yet wired to those owner
profiles; never attach an overlay hash to results from the old instance.
