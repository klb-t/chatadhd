# N2 TF-IDF cache: matched ablation, 2026-10-02

All 18 paired cells have identical seed, request and output bytes. All 54 calls
per arm completed; each cell retains one cold and two identical warm outputs.
No provider was called. This is computational reuse, not a model-quality result.

The protocol was published before measurement at `9626969bce806b2270d764ebdea3a8e139453de3`.
The baseline is the same recovered source with only the production changes of
`f899559` reversed, not the originally proposed older integration revision.
Both arms use GCC 13, Debug `-O0 -g0`, bundled SQLite and warnings as errors.
The unchanged instrument is `loom/tools/benchmarks/context_candidate_cost_v2.py`.
Baseline ran first; candidate second. No build/test ran concurrently.

| Claims | Theses | Channel | Cold wall ms, base → cache | Warm wall ms, base → cache | Warm CPU ms, base → cache | Warm wall ratio |
|---:|---:|---|---:|---:|---:|---:|
| 64 | 1 | tfidf | 127.99 → 133.31 | 131.28 → 134.24 | 131.28 → 134.23 | 0.98× |
| 64 | 1 | uninstalled-control | 83.98 → 39.38 | 75.44 → 37.11 | 75.43 → 37.09 | 2.03× |
| 64 | 4 | tfidf | 531.61 → 234.62 | 529.67 → 231.65 | 529.66 → 231.34 | 2.29× |
| 64 | 4 | uninstalled-control | 155.81 → 122.41 | 153.11 → 123.08 | 153.09 → 123.07 | 1.24× |
| 64 | 16 | tfidf | 2161.76 → 622.43 | 2225.70 → 648.99 | 2225.17 → 647.96 | 3.43× |
| 64 | 16 | uninstalled-control | 523.49 → 476.40 | 503.26 → 482.98 | 503.24 → 482.80 | 1.04× |
| 256 | 1 | tfidf | 565.88 → 448.90 | 587.33 → 449.65 | 587.22 → 449.63 | 1.31× |
| 256 | 1 | uninstalled-control | 81.49 → 80.41 | 83.84 → 86.54 | 83.84 → 86.52 | 0.97× |
| 256 | 4 | tfidf | 1823.86 → 715.72 | 1826.32 → 717.89 | 1826.28 → 717.87 | 2.54× |
| 256 | 4 | uninstalled-control | 322.34 → 317.74 | 312.54 → 418.50 | 312.03 → 417.81 | 0.75× |
| 256 | 16 | tfidf | 7628.26 → 1847.39 | 7497.48 → 1812.30 | 7497.13 → 1812.02 | 4.14× |
| 256 | 16 | uninstalled-control | 1283.31 → 1226.21 | 1293.11 → 1239.57 | 1292.37 → 1239.29 | 1.04× |
| 1024 | 1 | tfidf | 1766.18 → 1751.35 | 1817.08 → 1750.66 | 1816.76 → 1750.57 | 1.04× |
| 1024 | 1 | uninstalled-control | 290.18 → 272.14 | 277.91 → 271.95 | 277.90 → 271.62 | 1.02× |
| 1024 | 4 | tfidf | 7203.84 → 2781.78 | 7202.53 → 2731.85 | 7202.04 → 2728.65 | 2.64× |
| 1024 | 4 | uninstalled-control | 1141.34 → 1151.67 | 1236.03 → 1149.09 | 1236.00 → 1148.93 | 1.08× |
| 1024 | 16 | tfidf | 33106.58 → 6678.06 | 28846.07 → 6740.47 | 28836.88 → 6740.00 | 4.28× |
| 1024 | 16 | uninstalled-control | 4270.06 → 4423.28 | 4322.91 → 4431.44 | 4321.31 → 4430.79 | 0.98× |

Warm values are medians of two observations. The ratio is baseline/cache,
so above 1 means the cache arm was faster in this run. Missing-channel controls
measure unrelated path variability; their ratios are not cache benefits.
The JSON summary records CPU, cold/warm RSS deltas and process peaks for every
cell. Full per-call measurements, fixture bytes, first diagnostics and both
output sets are in the recovery archive. RSS is a process snapshot, not exact
cache-owned memory. This single fixed-order Debug run cannot establish release
throughput, confidence intervals, broad workload gains or model accuracy.
The original interrupted diagnostic remains separate and was not relabelled.
