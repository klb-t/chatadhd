# W1 → W3: source-only consumption replay

Replay compiles the **actual** W3 `MethodRegistry::load/resolve` from commit
`03c670caa6ee3d8ac2c478186548114f8e83927f` against W1's prompt graph adapter.
Foreign `.h/.cpp` are exported with `git show` into scratch, never copied into
production source. The fixture opens a fresh scratch database directly and
does not construct a Runtime or invoke a provider.

Run from the repository root, using already built static archives:

```sh
python3 loom/src/extract/tests/method_graph_w3_consumption.py \
  --build-dir loom/build/dev \
  --evidence loom/src/extract/tests/evidence/2026-10-04/method_graph_w3_consumption
```

The pinned commit must exist in the local Git object database. The runner
records its exact Git blob IDs, exported byte hashes, the complete compile/run
commands, linked archive hashes, full stdout/stderr and every native row.
It never builds or rewrites those archives. An optional fresh `--scratch-dir`
retains exported sources, the executable and its scratch-only database.

The 29 fixture checks comprise 27 checks across the three actual prompt
profiles, one successful Jev descriptor load, and one **expected reproduction
of a foreign limitation**. All three prompt profiles pass W3's native DTO,
immutable hash and Claim/support closure checks. Their layered effective
parameters exactly match both the producer parameter node and the shape used
by semantic extraction: `request_parameters`, `analysis_parameters`,
`validation_mode`, `output_schema_hash`, `transport`, `model`, `provider_url`
and `implementation_version`. Audit-only user overrides remain separate.

For relation and occurrence graph, the fixture host explicitly advertises
`loom.extract.semantic` as available. This is a capability declaration only;
no dispatch or execution is tested. Legacy analysis remains a visible leaf
with an explicitly unavailable `loom.analysis.adapter_required` capability.

Jev's descriptor-only profile has a deliberately null execution capability.
It loads, but the pinned foreign resolver tries to read that null as a
string and returns:

```text
invalid_argument: method registry: [json.exception.type_error.302] type must be string, but is null
```

The full negative response is retained in the receipt and raw log. W1 does
not patch the W3 consumer or invent an executable Jev capability. W3 should
retain a descriptor-only unavailable leaf for null capability metadata.

The runner separately verifies that every scratch data table stays empty;
Database::open's schema metadata is expected. Pure load/resolve need not
create knowledge or packet tables. Producer sources and linked archives
must remain unchanged. These checks establish source/profile compatibility,
not model execution, canonical acceptance, method quality or a W3/W4 joint
execution gate. Provider calls and paid calls are both zero.
