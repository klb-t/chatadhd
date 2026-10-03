# Fixture integrity check record

2026-09-30, isolated author, before any model/parser/vector predictions.

```
GRAPH_PANEL_AUTHOR_CHECK_ALL=1 python3 -m unittest discover \
  -s loom/tests/fixtures/research/graph_methods_panel_v1 \
  -p 'test_fixture_mechanical.py' -v
```

**14/14 mechanism checks passed** on both independently authored splits.
This validates fixture IDs, disjoint whole-family assignments, balance,
source-spans/UTF-8, temporal support, assertion/event/query references,
path composition and epistemic markers. It is no model-quality measurement.

Normal developer execution omits the author-only environment variable:

```
python3 -m unittest discover \
  -s loom/tests/fixtures/research/graph_methods_panel_v1 \
  -p 'test_fixture_mechanical.py' -v
```

Normal execution parses development gold only and checks sealed validation
file hashes without parsing its labels. Do not use the author-only switch as
a substitute for an explicit method/scorer freeze and validation release.

No paid API call, model inference, vector ranking or parser-quality run was
performed by the fixture author. Recipes are unexecuted instrument definitions.
