#pragma once

#include <functional>
#include <string>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Database;
namespace context {

using MethodPacketOperation = std::function<Result<Json>(const Json&)>;

// Source-private adapter for W4's loom.method_graph/1 client data pattern.
// No catalogue, preset, precedence order or supported algorithm list lives here.
// Caller profile shape (arrays contain complete native DTOs):
// {vocabulary:{kinds:{method,method_version,parameter_set_version,run,...},
//    predicates:{version_of,uses_parameter_set,uses_combination,
//    requests_method_version,produced_in_run,produced_by_method_version,...}},
//  entities:[Entity],claims:[Claim],sources:[{observation,known_at,text_sha256}],
//  selection:{members:[{method_version_id|combination_version_id,weight?,parameters?}],
//    parameter_layers:["preset","recipe","method","combination","member",
//                      "selection","user",...],
//    parameter_sources:{<additional layer>:<object>},parameters:{...},
//    user_overrides:{...},preset_version_id?,fusion:<string|{operation,...}>}}
// Members can select any registered preset_version_id (nearest occurrence wins).
// selection.fusion:null explicitly disables an inherited root fusion descriptor.
// Layers merge objects recursively; arrays/scalars/null replace. The order is
// required caller data. Combination attrs.definition.members use the same member
// shape, with real includes_method edges. Paths retain repeated DAG occurrences.
// All weights are finite signed values; there is no size/depth policy cap.
// Capabilities: {execution:{<capability>:{available:bool,...}},
//                fusion:{<operation>:{available:bool,...}}}. Metadata is not code.
// Each method-version attrs.definition.execution_capability selects a capability
// advertised by the actual host; unavailable mechanisms remain visible leaves.
// Receipt IDs identify actual native graph rows, point-read under a coherent lock;
// drift, modified immutable content and rejected owner rows block dispatch.
// Snapshots are values, not a second database. prepare rechecks accepted receipts.
//
// run_context requires run_id, origin (W4 instrument-origin DTO), known_at;
// input_sha256 is a SHA256 string or explicit null. effective_parameters may
// replace the resolved values only when it is the actual consumed configuration.
// base_packet is optional; measurements contain measured numbers or null.
// Optional model_identity_id/compiler_transform_id reference real snapshot rows.
// Recipe definition.parameter_bindings is [{target:[effective_parameter_path],
// source:[run_context_path]}]; exact values bind BEFORE version hashes. This
// makes consumed per-call controls part of the concrete method/recipe identity.
// prepare creates an immutable parameter-set version with
// attrs.definition={effective_parameters,user_overrides} and canonical hash;
// method version and run use actual uses_parameter_set Claims to that Entity.
// Applicable combinations have actual run -> uses_combination Claims. Missing
// kind/predicate roles are unavailable, never supplied by a C++ fallback.
// Recipe definition.request_bindings is [{target:[key/index,...],
// source:[key/index,...]}]: exact value substitution from run_context. Bindings
// run AFTER graph registration, with packet and base_packet_sha256 authoritative.
// Immutable recipe hash covers definition, effective params and binding spec;
// recipe_request_sha256 identifies the bound recipe request/patch. The actual
// sent payload is captured separately by bind_results as request_sha256 plus
// request_bytes_sha256; optional expected_request_sha256 verifies a prepared
// complete body. This avoids packet self-hash cycles and partial-payload stamps.
// prepare returns {packet,manifest,effective_recipe}, never canonical acceptance.
// manifest.definition_records captures each bound definition/descriptor's exact
// native attrs under its binding name (excluding mutable run_id), alongside
// immutable Observation bytes. Binding verifies these records and graph edges;
// changed model descriptor bindings refresh their corresponding captures.
// bind_results consumes actual node_ids/result_entity_ids plus provenance/hashes
// and measured instrumentation. It returns {packet,manifest} with real native
// result-to-run/version/compiler Claims and a separate captured completed trace.
// accept is the existing explicit closed-selection/CAS graph store boundary.
class MethodRegistry {
 public:
  explicit MethodRegistry(Database& db) : db_(db) {}
  Result<Json> load(const Json& profile, const std::vector<std::string>& receipt_ids = {});
  Result<Json> resolve(const Json& snapshot, const Json& selection_overlay,
                       const Json& native_capabilities);
  Result<Json> prepare(const Json& snapshot, const Json& resolved_leaf,
                       const Json& run_context, const MethodPacketOperation& operation);
  Result<Json> bind_results(const Json& candidate_packet, const Json& manifest,
                           const Json& result_bindings, const MethodPacketOperation& operation);
  Result<Json> accept(const Json& graph_store_request);

 private:
  Database& db_;
};

}  // namespace context
}  // namespace loom
