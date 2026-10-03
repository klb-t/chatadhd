# ChatADHD spec (2025-03-01)

## Components

- Graph store
- Conversation importer
- Voice input (ASR)
- Compose renderer
- Execution environment

Every adapter implements `IExecutionEnvironment`; storage goes through `IGraphStore`.

## Invariants

- Raw source bytes are never modified after import, so every derived graph can be rebuilt.

## Open questions

- Which sync transport should be the default for the knowledge graph?
