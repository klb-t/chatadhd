# Archived negative prompt experiments

These are complete rejected variants, intended only for the negative archive
branch. They are not production registry code or accepted evaluation oracles.
All responses and text are synthetic; no real API key, private archive or live
HTTP is used. The `.cpp.fixture` suffix keeps these separate `main()` programs
and the alternate registry out of the normal core source glob.

| Variant | Original result | Interpretation |
| --- | --- | --- |
| Ordered-JSON equality oracle | 15/16 cases, 240/242 assertions; exit 1 | JSON object insertion order caused two false inequality assertions despite identical fields/values. The corrected accepted fixture compares canonical content and also checks original raw bytes. |
| Legacy analysis request order | Exact wire bytes 0/3; body semantics 3/3; content bytes 3/3 | Missing explicit body-field order produced different serialized request bytes. This variant was rejected for default-wire compatibility. |

The recovered legacy definition has exact canonical contract hash
`fb31e66cf09406d0f29ab1bad19290507c3df67b59e63785824788fde849dac2`.
Its full historical first receipt preserves old/generated body hashes for all
three inputs, including long Unicode clipped to 3000 codepoints. The archived
helper reads that definition through `argv` and `from_snapshot`; its static
old body construction and linked `kAnalysisPrompt` remain unchanged.

The complete registry copy differs from the production source only by accepting
the historical metadata key `preserve_unknown_fields` **when exactly true**.
That key never controlled output retention; it is an archived dead switch,
not a production setting. `registry_delta.patch` shows the entire deliberate
change. The alternate object is linked **before** the normal core archive.
The recovered snapshot, rather than any current builtin, determines the probe
contract and wire output. This reconstructs the negative instrument; it does
not claim the copied modern implementation is the vanished original binary.

Use the matching pre-W2 W1 source/build context recorded by the final archive
receipt. In particular, the original 16-case oracle includes the missing-W2
case, so a build with W2 integrated has a different fixture population. Keep
historical runs and later context variants separate.

After a normal matching `loom/build/dev` build, run from any directory:

```sh
python3 /path/to/archive/2026-10-04/replay_negative_prompts.py \
  --repo /path/to/chatadhd \
  --output /tmp/loom-prompt-negative-replay-new
```

The output directory must be new. `--case legacy-wire` or `--case ordered-json`
can replay one variant. The helper compiles only standalone fixture/registry
objects, reads prebuilt libraries, changes no production files, and retains
complete compile/run stdout and stderr plus source/library hashes. Its exit 0
means the **expected negative outcomes were reproduced**, not that either
variant passed the accepted quality gate. The original legacy helper also
returns 0 for content/semantic equality despite its wire gate being 0/3; the
runner verifies the complete negative historical receipt, not that exit alone.

`manifest.json` records the source hashes, expectations and actual reproduced
negative outcomes. The observed replay receipt and complete compile/run streams
are in `replay-evidence/`; no passing quality result is inferred from successful
reproduction of rejected outcomes. Root infrastructure logs are stored separately from these evaluated
negative outcomes.
