# Branches and recoverable history

The [dated inventory](branch-inventory-2026-10-02.json) records all **41**
ChatADHD branches and **3** standalone Loom branches advertised at the audit.
Exact original histories remain recoverable from complete private Git bundles.

The final privacy finding concerns **author/committer contact metadata in 26
original-main ChatADHD commits**. Those commits are shared ancestors of the
historical ChatADHD refs. No private source-document content was confirmed
by the final content audit; early filename/substrings were not sufficient
evidence. The private contact value is deliberately not repeated here.
Standalone Loom has a separate ancestry and requires its own metadata audit.

The owner explicitly keeps `klb-t/chatadhd` public. This integration preserves
the original main ancestry. Moving files or refs out of the default view does
not hide historical metadata from repository visitors or remove previous clones
and cached commit pages. The contact value is not repeated here.

## Current publication boundary

The selected product tree is published on the existing public `main`, as a
single-parent successor of original main `b5f7eac`. Historical experiments are
not merged wholesale into the product line. Current reports and demonstrations
are at the front; the dated inventory and archive retain reconstruction paths.

**The 41 hosted branch labels have not been physically retired.** The current
connector can publish trees, commits and branch tips, but has no tag, rename
or branch-deletion operation. Direct Git push is not authenticated. The separate
local metadata-corrected bundle has four heads and 35 archive tags; those counts
do not describe GitHub's current ref list.

| Work | Role in the existing repository |
|---|---|
| Main product | Selected tree on original main ancestry, current reports and measured status. |
| Complete historical studies | Preserved original refs and complete backups; reconstruction links in the archive index. |
| Graph-reply prototype | Separate current research tree, retaining qualified synthetic findings. |
| Ecosystem-agent prototype | Separate current research, retaining its independent review boundary. |
| Night-development baseline | Supporting baseline for draft PR #7. |
| Sealed holdout/blind evaluation | Preserve isolated original tips; do not inspect or promote their contents. |
| Completed old lane/WIP labels | Retire redundant labels after exact backup and recovery mapping verification. |
| Standalone Loom | Compatibility baseline; authoritative current implementation is embedded in ChatADHD. |

The inventory also records mapped tips for the separate local derivative;
sealed tips remain unmapped. Its [404-commit mapping](public-history-map.json)
and [validation](metadata-sanitization.json) establish source-tree and topology
preservation. That mapping does not mean public ancestry was rewritten. The
public integration retains the contact finding in 26 original-main commits.

## Restore exact original histories privately

The utility defaults to a read-only ref-metadata plan:

```bash
python3 tools/archive_branches.py plan
```

To restore a downloaded complete private backup, use a new destination under
a private parent directory:

```bash
python3 tools/archive_branches.py restore-private --bundle /private/backup.bundle --bundle-sha256 <verified-sha256> --destination /private/chatadhd-originals.git
```

The bundle must be self-contained, match its SHA-256 and list every inventoried
original tip as a direct named head. The tool verifies it, creates a bare
mirror with owner-only directory permissions, restores exact original branch
names and verifies all tip identities. The new mirror has no fetch/push
remote and a private-history marker. No network calls, public refs or tags are
created; no existing checkout is overwritten.

The restoration check uses commit/ref metadata without inspecting evaluation
contents. Preserve their isolated roles after restoration. Run historical
commands from detached worktrees of this private mirror. Do not mirror-push
it publicly. Missing toolchains or originally unavailable external inputs
remain explicit reproducibility limits.

For standalone Loom, invoke the same tool with `--repository klb-t/loom`,
explicit inventory/tool paths and that repository's own complete private
bundle. This restores original bytes; it does not certify public privacy.

## Independent research and pending reviews

The graph-reply prototype is exactly two original commits after accepted
source `af3c81a5ddce70bdf4346a2c8c808ce6cc97b4d0`:
`a9110e1561697d50826b03bccd7a578575d1b560` and
`d49e05fd484e1a0af8432a5512762107ff633c1e`.
These are original identities on the independent research branch.
The prototype adds 33 paths, 37 contract tests and synthetic first-response
evidence, and is not part of this production selection.

Its report records zero external API calls. Eight synthetic cases yielded
23/24 semantic criteria with both graph and flat input; no graph-input quality
advantage was demonstrated. V1→V2 byte-size comparisons do not measure actual
tokens, API cost or latency.

At the read-only audit, [PR #6](https://github.com/klb-t/chatadhd/pull/6) was
an open draft for the superseded September 28 handoff. Its work is superseded
by the recovered main integration; closing it is a separate repository action.
[PR #7](https://github.com/klb-t/chatadhd/pull/7) was an open ecosystem-agent
draft based on night development. Preserve its independent review boundary
and supporting original identities.

No recipient-directed comments or reviews were sent by this audit.
Historical conclusions remain qualified; selected regression infrastructure
does not turn a failed study into a positive product result.
