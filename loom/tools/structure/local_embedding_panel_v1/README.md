# Frozen local learned-embedding arm

This is a **learned** sentence-embedding baseline, not a model-free graph
method and not an equivalence oracle. It uses the publisher's multilingual
MiniLM model through local CPU ONNXRuntime. No pair labels, earlier outcome
reports, API keys or remote inference were used by this arm.

The protocol was written before model measurements. Exact package versions,
code hash and platform/thread settings were frozen in
`instrument_environment.json` before the synthetic mechanism self-test and
before the 48-pair run. The self-test checks shape, finite normalization,
identity and token truncation; it is **not** a semantic quality evaluation.

## Instrument and source

- Publisher/model: `sentence-transformers/paraphrase-multilingual-MiniLM-L12-v2`.
- Pinned revision: `e8f8c211226b894fcb81acc59f3b34ba3efd5f42`.
- [Official model card](https://huggingface.co/sentence-transformers/paraphrase-multilingual-MiniLM-L12-v2)
  specifies 384 dimensions, attention-mask mean pooling and a 128-token maximum.
- Public ONNX and tokenizer files total 479,383,128 bytes. All downloaded
  files are hash-bound by `model_manifest.json`; the two large files also
  match the publisher API's advertised LFS SHA256 values.
- Raw left/right strings are encoded independently, with no input text
  normalization or prompt prefixes. Float32 vectors are L2-normalized;
  pair score is their dot product accumulated in float64. Tiny roundoff can
  make an identity score marginally exceed 1; scores are not probabilities.
- ONNXRuntime 1.23.2; tokenizers 0.22.1; NumPy 2.5.3; CPU, two inference
  threads. Remaining package versions are in the frozen environment.

## First run, 2026-09-30 UTC

Verified input file: `loom/tests/fixtures/eval/jev_structure_pairs_v1/inputs.json`.
Input SHA256:
`3474eef3ee9060f790aae21ee30fd962569ae902f2352dda46deea576e44c545`.
Verification clearance came from the integrating agent before this program
opened the pair input. The program checks the exact SHA256 again before use.

Coverage: **48/48 pairs**, **64 unique texts**, **384 dimensions**, **0/64
truncated texts**. Inference for the unique texts took 0.967 seconds, excluding
model/session initialization and artifact integrity checks. No quality metric
or decision threshold was computed. `prediction` is null in every score row;
there is no threshold fitting on these inherited validation pairs.

`scores.json` is the first complete cosine output, `vectors.npy` contains the
64 float32 vectors, and each score row links text/vector indices and hashes.
`run_manifest.json` binds the score/vector files, input, protocol and code.
The program refuses to overwrite an existing first score file.

These inherited validation pairs support exploratory comparison only. A
strong cosine score does not establish equivalent argument structure;
negation, quantifier scope and swapped roles can remain close in an embedding.
Gold-based comparison belongs to the independent panel after this arm freezes.

## Reproduction

Create a fresh output directory. First prepare the public model, then install
runtime dependencies into that directory. The preparation manifest's package
snapshot describes its preparation environment; the later frozen instrument
environment describes the actual inference environment.

```sh
python loom/tools/structure/local_embedding_panel.py prepare --output NEW_OUTPUT
python -m pip install --no-cache-dir --target NEW_OUTPUT/runtime_packages \
  -r loom/tools/structure/local_embedding_panel_v1/requirements-lock.txt
python loom/tools/structure/local_embedding_panel.py self-test --output NEW_OUTPUT --threads 2
python loom/tools/structure/local_embedding_panel.py run --output NEW_OUTPUT \
  --inputs loom/tests/fixtures/eval/jev_structure_pairs_v1/inputs.json \
  --inputs-sha256 3474eef3ee9060f790aae21ee30fd962569ae902f2352dda46deea576e44c545 \
  --pairs 48 --threads 2
```

Preparation downloads only the pinned public artifacts. All subsequent
commands use the local model/tokenizer and CPU backend. Use a fresh output
directory for a replay; do not rerun preparation into the frozen measurement
directory or overwrite the first artifacts. Public weights, installed
dependencies and temporary installer files are intentionally ignored by Git;
the manifests provide their identities and versions.
