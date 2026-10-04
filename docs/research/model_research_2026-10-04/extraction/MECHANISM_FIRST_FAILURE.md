The first new mechanism run executed eight synthetic tests: seven passed and
one failed. `test_three_arms_preserve_source_without_inventory_or_gold` saved
its comparison copy before deliberately replacing the fixture's family/gold
markers. Its immutability assertion therefore compared against its own setup
changes. Moving the comparison copy after setup corrected the test; no runner,
fixture, historical gate or extraction policy was changed for this failure.

The first replay invocation rejected the frozen ZIP because the new path
validator initially permitted only the research directory. The ZIP also
contains its exact public code/DEV-fixture closure and inventory. The validator
now enumerates those eleven closure members explicitly; traversal, foreign
paths, sealed data and unexpected closure paths remain rejected. The ZIP is
still required to match its original full SHA-256 before any member is read.
