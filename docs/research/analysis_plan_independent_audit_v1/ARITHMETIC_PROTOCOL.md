# Exact resource arithmetic control

Registered before results. Compare the repaired exact-sum helper with an
independent rational-number oracle, and compare ordinary Decimal accumulation as
the baseline. Generate 128 deterministic nonnegative decimal tuples using seed
20260930, with 1–6 operands, integer coefficients 0–999999999999 and decimal
exponents -80 through 80. Reuse the same tuples at ambient precision 2, 7, 28
and 81. Preserve every input/result, both error denominators and source digest.

Criterion: repaired arithmetic has zero differences from the Fraction oracle
under every ambient context; baseline errors are diagnostic. This is numerical
mechanism correctness, not a model evaluation or a distributional cost estimate.
No tuning, network, credentials, graph writes or sealed data is involved.
