# Resolved packet receipt metadata: protocol 22

2026-09-30. One authored bound-job case after the metadata-only correction.
Preserve20 unchanged. Capture new source/schema hashes before results. Loader
returns a distinct-domain Unicode packet; callback confirms its snapshot already
exists, observes caller-declared-not-verified source binding status, then mutates
its private packet. Check exact original packet survives, its digest is reported
on dispatch and cached replay, no second loader/callback executes, and changing
the retained packet is rejected during replay. Raw source fingerprint semantics
remain adapter-owned; the aggregate packet hash is a resolved-input receipt.
No actual model/paid call, source discovery or production write.
