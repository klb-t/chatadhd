# Cross-encoder DEV contrast

See `docs/research/retrieval_exploration_v1/CROSS_ENCODER_PROTOCOL.md` and `CROSS_ENCODER_FIRST_RESULTS.md`. New5 ranking recipes,34 comparators,zero API. Raw first outputs/scores/results are archived byte-for-byte in `first_evidence.zip`; restore using this folder’s `first_archive` module. The public91MB ONNX cache stays outside Git.

```sh
TMPDIR=/var/tmp python3 -m unittest loom.tools.structure.retrieval_exploration_v1.cross_encoder_v1.test_cross_encoder
python3 -m loom.tools.structure.retrieval_exploration_v1.cross_encoder_v1.first_archive verify
```

Do not re-execute first exclusive-output commands in-place. Offline audit can use recorded raw logits without downloading a model or contacting a provider.
