# Physical prefix before graph structural validation: protocol 12

2026-09-30. This is an authored mechanism experiment, not another model call or
proof that a previously extracted graph was available at a past time. Preserve
the original solver and every old research score. Compare its current whole-
graph validation with an experimental wrapper that cuts timestamped supplied
rows physically before invoking the identical solver.

Cases declared before results: baseline; five future appends (cross-speaker
event, invalid premise class, dangling event, duplicate assertion ID and bad
polarity); current cross-speaker event; current invalid premise class; unknown
timestamp; and the cross-speaker event at a later cutoff where it is current.

Criterion: all five dated future appends must return the exact baseline solver
result in the prefix arm. All current invalid rows must still reject. Unknown
dates must return explicit unavailable, never be silently treated as future.
Both arms retain source objects and a receipt containing every excluded row,
their hashes and raw counts. Inputs must not change. These receipts do not turn
model assertions into source facts or source time into instrument availability.

Decision keep only as a separately named candidate if the criterion passes;
investigate how the canonical graph/source packet supplies trustworthy time
metadata and how whole-graph health diagnostics remain visible independently.
No production edit, sealed validation, credential or network access.
