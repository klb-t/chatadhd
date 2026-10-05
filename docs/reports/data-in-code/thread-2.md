# Data in code — thread 2

Source: `161cc22dfb84fe863389d6b90323bd44516a68dc`. Inventory recorded before source edits. 5 reviewed groups; a group may contain an ordered table or several related literal sites. Full coverage/literal triage is in [inventory.json](inventory.json). Existing data-backed profiles and protocol/representation invariants are distinct from adjustable policy.

| ID / source | What | Destination | Generic mechanism | Status |
|---|---|---|---|---|
| DIC-0325 — `loom/src/core/config.cpp:36` | Android/Linux default data-directory candidate paths embed product names. | loom/data/profiles/runtime-paths.json + loom/data/presets/config.json | Read canonical startup defaults/path recipes from generated embedded data plus user overrides, preserving serialized legacy defaults. | hardcoded |
| DIC-0326 — `loom/src/core/config.cpp:48` | Subdirectory init list attachments/exports/logs and sentinel product label. | loom/data/profiles/runtime-paths.json + loom/data/presets/config.json | Read canonical startup defaults/path recipes from generated embedded data plus user overrides, preserving serialized legacy defaults. | hardcoded |
| DIC-0327 — `loom/src/core/config.cpp:87` | DataPaths file names chatadhd.db/chatadhd.fts.db/config/secrets/memory/models/github_sync etc. | loom/data/profiles/runtime-paths.json + loom/data/presets/config.json | Read canonical startup defaults/path recipes from generated embedded data plus user overrides, preserving serialized legacy defaults. | hardcoded |
| DIC-0328 — `loom/src/core/config.cpp:197` | 12-key kDefaults embeds provider URL, model ID, sampling/token/UI/system prompt/graph presets. | loom/data/profiles/runtime-paths.json + loom/data/presets/config.json | Read canonical startup defaults/path recipes from generated embedded data plus user overrides, preserving serialized legacy defaults. | hardcoded |
| DIC-0329 — `loom/src/core/config.cpp:215` | kLoomDefaults embeds event log type list and task worker count. | loom/data/profiles/runtime-paths.json + loom/data/presets/config.json | Read canonical startup defaults/path recipes from generated embedded data plus user overrides, preserving serialized legacy defaults. | hardcoded |

## Do wątku 2

Use the listed tasks only within your assigned edit scope. Preserve the exact current defaults, expose overrides, verify against the pinned baseline, and keep changes to evidence/transaction meaning explicit.
