# Project saved programme results with the generic exporter

The API is `method_graph_export_v1.export(configuration, root=artifact_root)`. It accepts caller-declared methods, runs, metrics and events; it has no fixed study/stage list. The historical `method_graph_build_v1` builder deliberately declares the accepted 60-event scripted freeze and should not be reused as a live-result dispatcher. Live output belongs in a new programme directory, with a new dated configuration and GraphPacket. The accepted offline freeze remains unchanged.

## Inputs and identities

The caller supplies public saved artifacts, not the private ledger/key directory:

1. A methods JSON array declares each exact configuration: stable method ID, version, exact recipe material/hash, exact prompt byte hashes, model/provider and all effective sent generation/format/runtime parameters, declared preset and ordered applicable compiler/adaptation components. The configuration binds each descriptor to its exact array pointer and file SHA-256. A reused label with changed bytes/parameters receives a distinct native version/configuration identity.
2. A normalized receipt records every durable attempt, including the failed first attempt. It preserves operation ID, state, exact request/response hashes, requested model/provider, observed model/provider or null, available cost/token/latency evidence and its measurement time. Unknown cost or usage stays null. It carries no credential or account/key fingerprint.
3. Optional existing scorer JSON preserves its exact inputs, scorer version/settings/hash and scored output. Accuracy is a scorer measurement, not HTTP success. Missing/invalid output remains explicitly unavailable. No live result is relabelled as scripted, and no authored control is relabelled as provider output.
4. The programme manifest supplies all planned operation IDs and exact request-file hashes. Response and scorer capture indexes are separate public JSON artifacts when available. A failed attempt can have only a request binding; it still has an event and run. A missing response must not prevent exporting the known failed attempt.

Use `measurement_kind: "historical_actual_model_response_replay"` for saved actual provider captures, including newly captured results dated today. This is the existing exporter's permitted model-quality receipt class: export is offline replay of those saved bytes. An artifact without actual provider output may still record its attempt/resource state, but must not declare measured semantic quality. `public_artifacts_only` is an explicit caller declaration after sanitization, not an instruction to read private files.

The source-level exporter does not dispatch, retrieve billing or run a scorer. It uses native Entity/Claim/Observation fields and `produced_by` relations to the exact configured method Entity, whose parent is the exact method-version Entity. These are report-projection provenance Claims. Provider execution and production native persistence keep their separate proofs under the canonical W3/W4 contract. `model_profiles` is omitted; dated metric Claims do not require making a new ModelProfile.

## Normalized receipt shape

This is a shape example, not a measured result. Arrays contain actual saved attempts only. `saved_row_count` equals the actual array length; planned IDs remain separate. The caller copies each known field from preserved evidence and supplies explicit nulls for unknown fields.

```json
{
  "schema": "caller.programme_saved_results/1",
  "sample_origin": "actual_provider_capture",
  "model_quality_measured": false,
  "saved_row_count": 1,
  "planned_operation_ids": ["operation-1", "operation-2"],
  "rows": [
    {
      "operation_id": "operation-1",
      "method_ref": "exact-configuration-id",
      "state": "uncertain",
      "request_sha256": "<exact-sent-byte-sha256>",
      "response_sha256": null,
      "requested_model": "<prepared-model>",
      "requested_provider": "<prepared-provider>",
      "observed_model": null,
      "observed_provider": null,
      "completed": false,
      "semantic_accuracy": null,
      "actual_cost_usd_decimal": null,
      "metrics": {
        "cost_usd": {
          "value": null, "numerator": null, "denominator": null,
          "method": "unavailable", "unit": "USD",
          "evidence": {"source_id": "rebound-by-exporter", "location": "/rows/0"},
          "note": "No verified billing result is available for this saved attempt."
        }
      }
    }
  ]
}
```

The shared metric shape permits a numeric `value` or null; the runner's `actual_cost_usd` is a decimal string. Preserve that exact decimal text separately and project a numeric scalar only after verifying its evidence. Cost must not be treated as zero when unavailable or replaced by reservation. Input/output tokens and measured latency use separate metrics. A scalar's note records whether it is provider-reported, independently billing-verified or host-measured, plus the exact normalization applied.

Existing scorer JSON can be used directly through `metric_pointer` if it already contains the shared metric shape. Otherwise use `value_pointer` for a numeric/null scalar or explicit numerator/denominator pointers for a counted ratio; preserve the scorer report itself as the source. Metric `axis` identifies the measured dimension (`cost`, `tokens`, `latency`, `model_quality`, `mechanism` or `unavailable`) as data. Semantic accuracy remains unavailable until an actual scorer result exists. Population records planned/available/missing counts and correlated-input limits.

## Exact generic configuration

Supply one run per exact configuration/input population or one run per attempt. The following data fragment shows a group run; all paths/hashes/pointers/IDs must be filled from actual saved artifacts. `methods` is the fully bound descriptor array. `events` contains only saved attempts; `selected_request_ids` must exactly match those event IDs. It can be extended to all 432 prepared study operations and every Stage 2/3/4 configuration without exporter code branches.

```json
{
  "public_artifacts_only": true,
  "exported_on": "2026-10-05",
  "methods": ["<fully-bound-descriptor-objects>"],
  "runs": [
    {
      "id": "programme:exact-configuration:saved-attempts",
      "method_ref": "exact-configuration-id",
      "observed_on": "2026-10-05",
      "measurement_kind": "historical_actual_model_response_replay",
      "source": {"path": "public/normalized.json", "sha256": "<normalized-file-sha256>"},
      "population": {
        "planned": 2, "available": 1, "missing": 1, "unit": "planned operations",
        "dependent_observations": "Declare matched/reused inputs and repeated attempts explicitly."
      },
      "selected_request_ids": ["operation-1"],
      "events": [
        {
          "id": "operation-1", "location": "/rows/0", "identity_pointer": "/operation_id",
          "binding": {
            "prepared_request": {
              "source": {"path": "public/manifest.json", "sha256": "<manifest-sha256>"},
              "location": "/operations/0",
              "matches": [
                {"pointer": "/operation_id", "equals": "operation-1"},
                {"pointer": "/request_sha256", "equals": "<exact-sent-byte-sha256>"}
              ]
            }
          }
        }
      ],
      "metrics": [
        {
          "id": "completion_coverage", "axis": "mechanism", "location": "/rows",
          "method": "counted_ratio", "records_pointer": "/rows",
          "complete_count_pointer": "/saved_row_count", "identity_pointer": "/operation_id",
          "match": {"pointer": "/operation_id", "one_of": ["operation-1", "operation-2"]},
          "success_pointer": "/completed", "unit": "fraction",
          "note": "All planned IDs remain denominator; failed and missing attempts are not removed."
        },
        {
          "id": "attempt1_cost_usd", "axis": "cost", "location": "/rows/0/metrics/cost_usd",
          "metric_pointer": "/rows/0/metrics/cost_usd"
        }
      ]
    }
  ]
}
```

The completion metric above is 0/2, while saved population availability is 1/2; semantic accuracy remains null. A model-quality metric must use the scorer's declared population and policy. If unanswered requests count as failures under a preregistered operational accuracy metric, state that policy; otherwise retain unscored availability separately instead of silently converting unknown semantic scores to false. Per-attempt cost/token/latency claims keep uncertain attempts visible even when no valid answer exists.

Write the configuration and export with the existing API or CLI:

```python
from loom.tools.structure import method_graph_export_v1 as graph
packet = graph.export(graph.read_json(configuration_path), root=artifact_root)
output_path.write_bytes(graph.codec.encode_packet(packet))
```

Each study must publish its own dated graph, exact configuration, saved source hashes and verification receipt after the attempt stops or finishes. Never overwrite the first failed input/capture with a corrected run; later attempts have their own identities. The caller may also retain an incomplete study graph before final scoring and publish a new graph version with added dated evaluations once those real measurements exist.
