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
