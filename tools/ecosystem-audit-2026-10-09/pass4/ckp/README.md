# PASS4 CKP: actual capture and delayed-paste consumers

Run from repository root after obtaining Kotlin 2.1.20 host dependencies:

```sh
python3 tools/ecosystem-audit-2026-10-09/pass4/ckp/run.py \
  --repo /path/to/Custom-Keyboard-Pro \
  --sha 192f820f2d8c653768237e1bff3a1a6d950d69c0 \
  --deps /path/to/kotlin-jars \
  --workdir /tmp/ckp-pass4 \
  --out /tmp/ckp-receipts/host-receipt.json --phase all
```

`--sha` resolves to an exact commit. Sources are read with `git show`; current
working-tree modifications are not used. No product files are changed. Three
capture source files are compiled whole and unchanged. Four production methods
are extracted verbatim with their original control flow into a host/platform shell:
`pasteOrConvert`, `pasteNativeClip`, `refreshBusyLamp`, and `commitText`.
The receipt records source paths, ranges and hashes; missing seams fail the gate.

Jars used: kotlin-compiler-embeddable2.1.20, kotlin-daemon-embeddable2.1.20,
kotlin-script-runtime2.1.20, kotlin-stdlib2.1.20, kotlin-reflect1.6.10,
kotlinx-coroutines-core-jvm1.8.0, trove4j1.0.20200330, annotations13.0,
json20240303. These were already installed by PASS2. Exact hashes are in the
receipt. JRE17 was used. The runner creates only a small jar and source snapshots.
Newer JREs rejecting SecurityManager need an equally effective process/network
block; silently removing the guard is not acceptable.

`--phase` can be `all`, `reproduction`, `acceptance`, or `contract`.
The baseline all-phase exit is **1**, with 6 reproduction PASS, 6 acceptance FAIL
and 11 contract PASS. Reproduction PASS confirms the old defect; it does not certify
product correctness. There is no xfail or expected-failure-to-success remapping.
A compile failure returns2 and is infrastructure failure, not an acceptance result.
Use a fresh output directory for each SHA to preserve final historical receipts.

Fixture boundaries:

- CaptureDriver/TreeNode provide safe deterministic observations and record actions.
  CaptureTreeReader, CaptureSession and ConversationArchive are actual consumers.
- PasteConversion suspends at a recording transport boundary; no provider is called.
  Settings/platform objects and coroutine host are minimal fixtures. This does not
  verify provider resolution, actual settings persistence, or Android lifecycle.
- Successful paste executes unchanged EditorController.commitText with its actual
  caller parameter `applyConventions=false`. InputConnection is a recording effect.
  Unused convention branches throw if unexpectedly reached; they do not implement
  substitute typing logic.
- Failed paste records the native pasteClip request at the platform boundary.
  Rich-content commitContent, MIME capability and OS acknowledgement remain open.
- Java socket connect/listen/accept are all denied, with an explicit guard case.

Synthetic fixture strings are not real user conversations or model-quality samples.
Map permutation tests preserve unordered map meaning, not ordered node/message lists.
Export tests target the existing inline options contract; a future versioned recipe
reference must be tested through its actual resolver before changing that oracle.
