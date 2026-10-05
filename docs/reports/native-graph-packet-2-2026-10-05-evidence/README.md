# W4 normalization evidence

Before/after are actual native API observations on the same public development corpus: 1236 token records and 300 phrase records. No provider calls. Source/compile/archive provenance is recorded separately from behavior; the two snapshot JSON files are byte-identical. Gzip members use mtime=0.

Reproduce using loom/src/kb/tests/run_normalizer_snapshot.py and the commands in loom/src/kb/NORMALIZER_RECIPE.md. Decompress corpus.json.gz and before.snapshot.json.gz to reuse exact inputs. The baseline used a dependency-complete subset archive of 94 compiled objects, with its manifest and build closure retained. It is not evidence of a full baseline build or CTest. The after observation used the completed core archive.

Complete baseline OOM/interruption logs and full intermediate source negatives are retained on archive/2026-10-05/native-graph-packet-2-review-negatives at 19d72fe19ee966cdfd0a88cf10d604523caf0783. They are excluded from the selected final tree. REVIEW_NEGATIVES.md on that branch distinguishes static findings from measured failures.

Full changed-tree build/CTest execution evidence will be added after those gates finish; the report remains in progress until then.
