# DIC-0301 offline replay

The seven historical generalization/principle cue classes are default pack data.
`dic0301_legacy_classes.json.fixture` is a test oracle copied exactly from public
W1 commit `50e6bb9f80b0cd855e4dd1efedaf3399cac5e4e6`; production code does not
read it. All 120 phrase/weight pairs and phrase order remain unchanged.

After coordinating compilation with the shared-resource owner:

```sh
python3 loom/src/generalize/tests/dic0301_replay.py \
  --core-build loom/build/dev \
  --output /tmp/dic0301-replay
```

The output directory must be new. The runner compiles serially with no debug
data, links exact legacy and current `common.cpp`/`principles.cpp` modules before
the same prebuilt core archive, and loads explicit old/current cues through the
real `Pack::from_documents`. Its Git-pinned baseline and raw JSON stay in the
output directory. It does not invoke model providers or use blind/sealed data.
No CMake registration or existing test gates are changed.

Checks cover exact seven effective lookups, 120 phrase score/type samples plus
two controls per class (134 samples), actual principle discovery over the
public fictional `synthetic_dev` conversations, 16 public legacy paradigm cases
and the same 16 with accepted anchor settings, seven deletion overrides, and
seven complete replacement overrides. Raw default outputs must agree across
the two source/data pairs; the legacy variant is a preservation control rather
than a claim that every legacy case is correct.

Removing a class disables its cue matches. Replacing a class uses only the
supplied phrases. There is no C++ semantic fallback or hidden merge. The current
foundation-owned Pack validator rejects `phrases: []`; this fixture records that
rejection and does not call it a successful empty-class disable. The generic
empty `PreparedCues` matcher still scores zero. A universal empty-list allowance
in the foundation validator is a separate ownership task. Profile tombstones
and permanent exclusions (R40) are not implemented by this local migration.

This is focused native preservation/configuration evidence. It does not prove
that the supplied prebuilt archive already embeds newly generated cue data;
the full build and existing `test_kb_pack` verify that separately. It makes no
new graph-precision gain claim. The public `loom/include/loom/generalize.h`
comment about C++ fallbacks requires its owner's documentation update.
