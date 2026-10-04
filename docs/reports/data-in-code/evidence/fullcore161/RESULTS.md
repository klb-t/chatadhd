# Actual whole-core parity — final capture

Frozen baseline: `161cc22dfb84fe863389d6b90323bd44516a68dc`. Actual current checkout: `3cbc4b6e16e01fdeddb970bf3b866fb70e95a5dd`. All six probe sources are unchanged old-public-API callers, compiled against the corresponding complete library/header set. No object substitutions or older-library fallback were used after the final build.

**6/6 probes compile and run successfully; all six full JSON outputs are byte-identical.** Total per side: **1,208,469 bytes**. The full semantics probe includes Runtime and four products; utility includes six separately preserved raw environment cases.

| Probe | Before bytes | After bytes | SHA-256 (both) |
|---|---:|---:|---|
| memory_selector | 59955 | 59955 | `caec4bb87a68e1f284978793feceffa8a5b42445db38776c691561e425150360` |
| semantics_graph_materialize | 11766 | 11766 | `0db70356e9c30dfb937353908f2f6c79f464215f386d1a4fad5a5f86768b787d` |
| worker_media_github | 604681 | 604681 | `d947a102fbdd04aa5d6343c8020755e1a63d22ff9ca80d80c5c05c5e50f1b7d2` |
| net_model | 264606 | 264606 | `6eb6f00d3c398870a15f350a918d7558eb20d68e31b64af82ca745d250b0b7a4` |
| util | 13295 | 13295 | `a6fd124a827744adc68e08fa771cc9fc2b4548128713c9de8ef12aa58b7c1dbf` |
| regex | 254166 | 254166 | `d54343319fe07946c4e2d023a9bb09ccc8461bcd03aa6b92548c4351b6163381` |

[Consolidated receipt](receipt-after.json) records the actual core/miniz/sqlite archive hashes, full CLI/server/shared-library artifact hashes, compiler commands, source/script hashes, four-product assertion, raw util equality, paths and per-probe git statuses. [Summary JSON](summary.json) gives the compact comparison. Exact original before receipts remain unchanged in `../baseline161/receipt.json` and `receipt-before.json`.

All inputs are synthetic. HTTP messages are captured through ScriptedTransport; Runtime workers are disabled. Normalization is limited to the frozen-source rules for random IDs, multipart boundary and time/path summaries. These compatibility measurements complement the root's full CTest gate; they do not certify model quality, price accuracy, or pending integration of W2 bootstrap/admission consumers.

## Do wątku 9

Use this actual full-library receipt as the final default-behavior comparison alongside full CTest and web/build evidence. Keep the earlier scoped, exact-baseline and negative stale-embedding receipts available; the final capture does not delete them.
