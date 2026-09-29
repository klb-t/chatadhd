# Jev recipes v1 — T3

[P] 32 freshly authored PL/EN cases, separate reference labels, 188 prepared
request bodies; **zero model calls**. Same author as implementation, not a blind
or human-independent evaluation. The former 64 texts are not reused as new cases.

Read `docs/research/JEV_RECIPES_PLAN_2026-09-29.md` for preregistered comparisons,
limitations and analysis. `manifest.json` freezes inputs, plan, generator and
prepared request identities. `examples.json` contains three ready body examples.
These envelopes include management metadata: send only their `body` to the API.

```sh
python -B loom/tools/eval/jev_recipes.py
python -B -m unittest discover -s loom/tools/eval -p test_jev_recipes.py -v
python -B loom/tools/eval/jev_recipes.py --export /tmp/jev_recipes_v1_requests
```

Export requires a new directory. It emits individual plain request JSONs and an
INDEX with `activation=disabled`, unset budget, body hashes, conditional children
and a deterministic shuffled schedule. No execution code exists here.
A root must complete before its selected child; never run both children merely
because they have JSON files. 188 prepared bodies imply at most 180 selected
calls across the full plan, not 188 completed experiments. A future live run
requires a separately authorized budget, current model/provider validation and
an immutable execution manifest.

27 local preparation/protocol tests passed. They check leaf-preserving key
changes, question-ID negative control, cumulative detail, multiple relevant
subgraphs, actual-root (not gold) child selection, explicit missing/tie handling,
file freezing and no network. This says nothing about Jev accuracy.

The corpus has no positive `other` routing cases; routing cannot measure general
abstention. Context cases are controlled short examples, not demonstrated code
repair or complete graph retrieval. English rubrics are used for both input
languages. The semantic validity of author labels needs independent review.
