# Publication privacy review — 2026-10-02

The reviewed source snapshot has **no confirmed owner medical records, private
conversation exports, real contact datasets, or live credentials detected**.
The existing remote Git history has a separate, confirmed metadata issue:
**26 commits expose the owner's personal email address** in author/committer
fields. This includes the original `main` ancestry and its first commit.
Publishing another commit with that ancestry retains that exposure.

**Publication update, 2026-10-03:** the owner explicitly keeps the existing
`klb-t/chatadhd` repository public. The integration is published directly on
its original main ancestry. No repository visibility change or replacement
repository is part of this publication. The historical metadata finding is
unchanged; the local correction described below is a separate derivative.

## What was checked

- The staged ChatADHD/Loom source, research JSON and archive members, including
  nested test ZIPs and SQLite fixtures, plus the separate `klb-t/loom` source.
- History reachable from 42 allowed local/remote ChatADHD refs: 404 unique
  commits, blob content, commit messages and author/committer metadata.
- The 11 checked UI screenshots, nine older historical screenshot versions,
  and the workbench concept image. Their visible chats, addresses and budgets
  are synthetic demonstration data; model replies identify the mock model.
- Reconstructed text from the two historical research DOCX inputs, rather than
  treating raw XML keyword matches as personal facts.

Synthetic fixtures, vendor attribution, product architecture and owner design
requirements are distinct from autobiographical medical or financial records.
No confirmed personal-data leak was found in those reviewed source files.
Source-keyword matches were reviewed rather than reported as proven leaks.
No sensitive values are reproduced in this report.

## Historical metadata and the publication boundary

The owner-email finding concerns historical **Git commit metadata**, rather
than an application configuration field. Every one of the 42 inspected refs
reaches the affected commits. The separate Loom repository had no equivalent
owner-email finding.

Complete original histories are also preserved in verified backups. A local
history with a noreply identity was prepared and verified separately; it is
not the ancestry published on the existing repository. Moving files into
`docs/archive/`, renaming branches, or adding a cleanup commit does not hide
reachable history from people who can access the repository.

## Scope limits

`eval/real-holdout-key` and the sealed blind worktree branch were deliberately
not inspected. Their contents and private full conversation exports are outside
this source review. The review detects common credentials and owner identifiers;
it is not a proof that arbitrary personal information cannot exist.

Updating public refs cannot by itself guarantee removal from previous clones,
forks, cached pages or already-addressable historical commit URLs. Any statement
that historical public exposure has been fully erased requires separate
verification of the hosting service's retention behavior.

## Local metadata correction

The local sanitized mirror maps 404 commits while preserving every source
tree, commit date/message and ordered parent topology. It replaces 52
author/committer identity headers in the 26 affected commits. Recomputed
ancestry invalidated 196 signatures, which were removed from the derivative;
the exact originally signed objects remain in the private backup.
The target private address is absent from all advertised derivative commit
metadata. The two sealed refs are not advertised by this public derivative.
See [mapping](archive/public-history-map.json) and
[validation](archive/metadata-sanitization.json). This is a local
correction, not a remote visibility or cache-removal confirmation.

## Local sanitized history verification

The local mirror was independently checked: 404 mapped commits and
42 allowed refs; the target personal email is absent from all reachable commit
payloads. All 404 corresponding source trees and mapped parent relationships
match the preservation map. Identity headers changed in 26 commits; all 404
commit hashes changed through the reconstructed ancestry.

An exact target-email search also covered 6,124 historical source/archive
entries. It found no match. The embedded incremental W3 and W5 Git bundles were
unpacked using the private original object database: their seven packed commits
and remaining packed objects contain no target-email match. Their old
prerequisite commit IDs still require the preserved original history for replay.

This verifies the separate local derivative. The public integration retains
the original parent history; it does not claim removal of that contact metadata
from remote refs, previous clones or cached commits.
