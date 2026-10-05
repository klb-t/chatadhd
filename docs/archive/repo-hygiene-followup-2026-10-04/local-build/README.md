# Incomplete local setup/build attempts — W8 follow-up

These logs record failed attempts, not CTest execution. The first configure
needed an explicit Ninja path; the second lacked system SQLite development
files. The subsequent GCC/vendored build found four empty object files. Their
paths, sizes and hashes are retained; only those generated empty objects were
removed before recompiling them serially. No product source, warning or test
threshold changed. Positive verification, if completed, has a separate receipt
in `docs/verification/repo-hygiene-followup-2026-10-04/local-vendored-gcc/`.
