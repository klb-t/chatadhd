// Generic, non-secret prompt data and request preparation. No transport calls.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::extract::prompts {

struct Contract {
  Json definition;
  std::string hash;  // SHA-256 of canonical effective definition
};

// Objects overlay recursively; arrays and scalar values replace entirely.
Json overlay(const Json& base, const Json& patch);

// Exact JSON-content .prompt data are compiled into the binary. An optional
// overlay directory may supply replacements/patches by contract id. No cwd
// lookup and no network; omitted files retain their compiled default.
Result<Contract> resolve(std::string_view id, const std::filesystem::path& overlay_dir = {},
                         const Json& patch = Json::object());
Result<Contract> from_snapshot(const Json& definition);
std::vector<std::string> builtin_ids();
Json builtin_catalog();

// bindings maps named bindings to JSON: strings are exact text, other values
// render as canonical JSON. input_json always renders canonical JSON. An
// optional max_codepoints clips rendered text on UTF-8 codepoint boundaries.
// request_patch recursively overlays the final provider request body.
// Optional transport.body_field_order lists first fields in wire order;
// unlisted fields follow in their insertion order, without being removed.
// Result includes method/url/headers/body_json/body_bytes/transport and
// contract_hash/output_schema_hash/request_hash; never adds Authorization.
Result<Json> prepare_request(const Contract& contract, std::string_view model,
                             std::string_view provider, const Json& bindings,
                             const Json& request_patch = Json::object());

// Generic supported JSON Schema validation. strict rejects violations;
// lenient retains the input and reports warnings; off does not run the schema.
// Unsupported constraints are explicit issues, never successful checks.
// Validation is immutable: additionalProperties rejects/reports unknown
// fields according to the mode and never removes them from the raw output.
// Native grounding/truth checks are separate from this schema report.
Json validate_output(const Contract& contract, const Json& output);

}  // namespace loom::extract::prompts
