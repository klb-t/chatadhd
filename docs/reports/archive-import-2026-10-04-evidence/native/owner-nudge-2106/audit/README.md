# Regular audit suite through the existing CTest adapter

Actual focused execution of `loom/tests/compat/test_archive_cost.py -v` passed
**19/19 unittest cases**: all 11 existing estimate sentinels plus the 8 retained
raw-audit regressions now in `loom/tools/eval/test_archive_cost.py`.
The ordinary adapter's `load_tests` loads that suite without duplicated tests;
existing CMake compatibility discovery registers the adapter.

[receipt.json](receipt.json) records the exact command, Python version, source
hashes, elapsed time and stdout/stderr identities. The [full case log](unittest.stderr.txt)
records every executed case. All fixtures were synthetic and offline, with no
provider call. This direct adapter execution is **not a full CTest claim**;
the root agent's full native receipt remains separate.

From the repository root:

```bash
python3 -B loom/tests/compat/test_archive_cost.py -v
```

[SHA256.json](SHA256.json) binds the retained receipt and exact logs. The source
revision reference is `4e8c3de`; actual input hashes bind the execution rather
than assuming that a Git label covers every working file.
