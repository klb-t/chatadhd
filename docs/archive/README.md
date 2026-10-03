# Reproducible project history

Current source, tests, necessary fixtures and concise reports form the selected
product tree. Historical runs, unused experiments, original branch topologies,
first failures and original W3/W5 bundles remain reconstructible separately.

## Original histories and complete backups

The 2026-10-02 audit confirmed a private owner email in the author/committer
metadata of 26 original-main commits. That ancestry reaches every examined
ChatADHD branch. Public archive paths and tags cannot hide those fields.
The owner explicitly retains the original public repository. Current hosted
refs still reach the original ancestry; correcting its contact metadata is
pending authenticated Git transport.
Full original histories were also preserved in verified, self-contained
private Git bundles. A separate local metadata-corrected derivative does not
replace the history hosted on `klb-t/chatadhd`.

| Original backup | SHA-256 |
|---|---|
| `ChatADHD_complete_history_2026-10-02.bundle` | `bd046c58f21e4bd01ce8b5287adfe0b0a286f34c7e3a3ad4dafaefed5dcad086` |
| `Loom_complete_history_2026-10-02.bundle` | `155c693b098c80ab9bf841e661477fe4a3a9b7aab2d4257a222e216abc935248` |

The backups include the original advertised branch tips and complete parent
history. The ChatADHD backup also contains the archived W3/W5 bundle bytes,
so their original commit identities remain recoverable independently of their
published tree-equivalent replacements. Private source conversation exports
are separate artifacts and are not inputs to the public repository.

With access to the original backup, use [the private restoration tool](../../tools/archive_branches.py)
and [branch lifecycle guide](BRANCH_LIFECYCLE.md). Restoration is local and
creates a separate private checkout. Keep that recovery mirror separate from
ordinary development checkouts. Existing public original commit metadata was
not rewritten by this integration. Changing branch tips alone cannot remove
GitHub cached commit pages.

## Selected product tree

| Source identity | Purpose |
|---|---|
| Original main `b5f7eacdead11101e5c2a08e1864195c74034d45` | Direct parent of the published integration; original 40-commit history preserved publicly and in the complete backup |
| Accepted source `af3c81a5ddce70bdf4346a2c8c808ce6cc97b4d0` | Recovered W1–W6, night work and compatible late corrections |
| Recovery anchor `2878a0c8f80800732f0ffcfff695a24f135748e0` | Historical full tree, first-attempt logs, verification and paired N2 evidence |
| [selection.json](selection.json) | Exact retained closure and SHA-256/size for each archived path |
| [branch inventory](branch-inventory-2026-10-02.json) | Original tips and intended lifecycle, including separately retained later research |

These SHA values identify reproducible historical inputs. The published
integration directly continues original main `b5f7eac` with one parent.
Experimental branches remain separate from the main product progression.
The optional local metadata-corrected bundle changes commit identities while
preserving file trees, dates and topology; its ancestry was not published in
place of the original main line.

All 39 paths from original main survive the selection, including byte-identical
`ECOSYSTEM.md`. Test fixtures and negative examples needed by regressions remain
in the product tree. Their retention is not a claim that the underlying
experiment succeeded. Native binaries must be rebuilt on the target toolchain.

## W3 and W5 original identities

| Original nested bundle | SHA-256 | Original tip | Published tree-equivalent tip |
|---|---|---|---|
| `W3.bundle` | `7b98bb3953d2509e2d81b0779b1c3be57f64dee7c3b070ec95fc0ef71f923899` | `bdf86d10543c2487b59fc729ee0099341ecd4dab` | `729699fe8dab6dd696b133bd277d3717c3bbd055` |
| `W5.bundle` | `6f911995f84e48419b0358e3ed1c24d155d99fb587130ab7d17b8c3c8d45f1bc` | `e03bd02d42c8235b58f035a8689e593b4d570e95` | `411b18be4d8a8906d9587b1a5761861aad089f47` |

Both nested bundles verify against original prerequisite
`b118c80e981c08ec6d7f9ab6aacc177979186cf2`, which is in the preserved original history.
The original ZIP hashes and bounded recovery gaps are recorded in
[conversation recovery](../CONVERSATION_RECOVERY_2026-10-01.md).

## Hosted branches and the separate local derivative

The selected product tree is published on the existing public `main`. The
41 ChatADHD branches are now grouped as 35 archived, four active or supporting,
and two isolated evaluation refs. [The current verified mapping](branch-inventory-2026-10-03.json)
records 33 renames with all original tips unchanged. Earlier tips and roles
remain in the dated inventory. Historical reports are outside the current front-page
navigation, while required tests and fixtures remain in the active tree.

The separately prepared local metadata-corrected bundle contains four heads
and 35 archive tags. Those counts describe that optional local artifact,
not the branch or tag list on GitHub. Its two excluded evaluation refs remain
recoverable in the complete original backup. No evaluation contents were
inspected or merged, and repository visibility remains public.
