// Native method provenance proposals. No provider calls or canonical writes.
#pragma once

#include <filesystem>

#include "extract/prompt_contract.h"

namespace loom::extract::prompts::method_graph {

Json builtin_method_data();
// Independent JSON-content analysis_methods.pack; objects overlay recursively.
// No cwd lookup. An exact options.method_data_snapshot bypasses these overlays.
Result<Json> resolve_method_data(const std::filesystem::path& overlay_dir = {},
                                 const Json& patch = Json::object());

// Returns complete native profile arrays plus bindings/definition_hashes/records.
// options: method_data_snapshot (exact), method_data (recursive patch),
// preset_snapshot (exact original prompt definition), known_at (string or null),
// prompt_messages_snapshot (exact caller replacement of declarative messages).
// Unregistered custom prompt IDs require caller method data; no implicit method.
// Prompt text is canonical original declarative messages JSON, including roles
// and bindings. Actual input/body bytes are separate immutable captures.
Result<Json> method_profile(const Contract& contract, const Json& prepared,
                            const Json& effective_parameters, const Json& user_overrides,
                            const Json& options = Json::object());

// Registers an opaque historical recipe as native method/recipe/preset/parameter
// versions. No prompt, model, run, result, execution or evaluation is fabricated.
// options.declared_parameters is optional metadata; omitted means null/unbound.
Result<Json> recipe_profile(std::string_view id, const Json& patch = Json::object(),
                            const Json& options = Json::object());

// run_context requires run_id, input_sha256 (SHA256 or null), known_at
// (string or null). Optional measurements are measured numbers or null;
// measurement_scope and knowledge_run_id retain their caller-declared scope.
// Returns {schema,profile,manifest,trace,result_entity_ids,
// definition_capture_source_id,trace_capture_source_id,canonical_store_written}.
// These are candidate rows; canonical_store_written is always false.
Result<Json> prepare_graph(const Json& profile, const Json& run_context);

// Validates retained native DTOs, exact captures, definition hashes, closure,
// trace identity and real result provenance edges before checkpoint reuse.
Status validate_graph(const Json& graph);

// candidates is [{id:<existing queue id>,payload:<complete proposal>}].
// provenance requires raw_model_content (exact string), known_at (string/null).
// Optional response_provenance, from_cache, model, measurements,
// measurement_scope and instrumentation describe the actual invocation.
// Result native Entity IDs are distinct projections of queue IDs; contents
// remain model_knowledge/unverified. Compiler transform is explicitly null.
Result<Json> bind_candidates(const Json& graph, const Json& candidates,
                             const Json& provenance);

}  // namespace loom::extract::prompts::method_graph
