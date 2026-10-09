# Independent importer/resource probes

```sh
python3 tools/ecosystem-audit-2026-10-09/pass3-resources/importers/chat_importers.py \
  --repo /path/to/chatadhd --sha 9e20f99ab27e7cd45e1892f83bf60fbe60db3de9 \
  --out /path/to/chat-main.json

python3 tools/ecosystem-audit-2026-10-09/pass3-resources/importers/chat_importers.py \
  --repo /path/to/B2 --sha 384c5e1686cd3a58a7d89a8a6813a18c764f697d \
  --out /path/to/chat-B2.json

python3 tools/ecosystem-audit-2026-10-09/pass3-resources/importers/watchdog_run.py \
  --repo /path/to/Watchdog-JH16 --sha 58a0c93bd0135e3715dcbc4d92fb80e61bd31215 \
  --out /path/to/watchdog-main.json
```

Python needs only the standard library and actual checkout modules. Watchdog uses its already installed lockfile dependencies (`tsx`, `better-sqlite3`, React); no package downloads or external services occur. Both create isolated temporary file-backed databases, exercise real consumers and remove temporary fixtures. Checkouts are checked against their requested SHA and must have no relevant tracked edits.

Reproduction PASS confirms observed bad behavior, never product acceptance. Exit1 includes FAIL or BLOCKED_MISSING_CONTRACT. The full projection/reference contract is not fabricated by the harness. No source parser or storage engine is copied into tests. The synthetic fixtures contain no private archives or credentials. Python can restrict cases with `--phase reproduction` or `--phase acceptance`; contract blockers remain explicit.
