# Runner review — full negative evidence

Archive only. Do not merge this tree into main.

The ZIP retains complete before/after sources, fabricated request/response and
billing fixtures, SQLite state, first failed results and corrected reproductions.
No real credential, account metadata or actual private ledger is included.

ZIP SHA256: `8140073a069632c850355ab74d30a7e4a24dc599c319a3e8062bdbb55bf42e6c`.
623 payload files are covered by the internal SHA manifest.

Extract into an empty directory, run `python3 verify.py`, then
`python3 reproduce.py`. POSIX Python standard library only; fake transports,
zero provider calls. Captured late-generation resolution preserves every
original first response and 404 and never repeats its POST. Missing or changed
resolution witnesses, wrong late generation/cost/model and BYOK proofs block.

The reviewed runner SHA256 is
`81a0b3581b56234ae7a944c51a46df1dfa89c31a7911c1db7d9d5e59969af075`.

The supplementary ZIP preserves endpoint-estimator negatives and the first
partial live output. Its README explicitly records missing v1 producer source;
full producer regeneration is not claimed.

The optional-endpoint unit-floor proof preserves 143 complete fabricated files,
including the old explicit-zero request charge escape and corrected zero-POST
preflight rejection. ZIP SHA256:
`3f795f00b87618b8c1be08ff5fd40c61c2758bb9b02de8214a77facfc39cffd4`.
Its own manifest and reproduction script run without provider calls.

The stage-end final review ZIP preserves 611 complete fabricated payload files
and exact before/final sources. SHA256:
`7d5fcf20cd0975d1e56f4b7b9292340ab02064a6fd9815c059799fdf1ed51d03`.
Its portable verifier and reproduction cover deleted contradiction witnesses,
partial paused-history loss, bootstrap, resume and cross-stage negatives.
Final runner SHA256:
`dd74b3d4197c3d274f780a1b324b56d9d579b8df905c512282e7f33cd25f6b97`.
All transports are fabricated; no real account or paid evidence is included.
