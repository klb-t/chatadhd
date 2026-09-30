# Cycle fix: independent retest 10

2026-09-30. Preserve protocol 07, its immutable source and first nonreturning
result. Run the identical self-referential dict and shared acyclic reference on
a newly captured producer source. The same entered-marker and 0.2s timeout are
retained. A finite explicit rejection of the cyclic object and acceptance of the
shared acyclic object are the required outcomes. No network or production edit.

This tests JSON validity. It establishes neither semantic graph correctness nor
an archive performance guarantee. Freeze source/script/protocol before results.

Setup attempt 08 stopped before any freeze or cycle probe because its protocol
filename substitution pointed to a nonexistent file. Its copied source and
script remain preserved. This corrected setup starts a separately named audit.
