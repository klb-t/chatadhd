// URL/share is an additive adapter. No shared importer/API declaration changes.
#pragma once
#include <filesystem>
#include "loom/result.h"
#include "loom/util/json.h"
namespace loom {
class Database;
class BlobStore;
class ProvenanceStore;
class Runtime;
namespace url_share {
// Validates a trusted data preset, with caller overrides for limits/page role.
Result<Json> policy(const Json& overrides = Json::object());
// Embedded canonical worker, extracted privately; no source-checkout dependency.
// Process watchdog covers DNS, HTTP, parsing and serialization.
// No shell, model API, credentials, cookies or arbitrary request headers.
// `isolated_worker` is a trusted C++ test seam, never selected from HTTP/config.
Result<Json> fetch(const Json& request, const Json& policy,
                   const std::filesystem::path& isolated_worker = {});
// Transactional native persistence using existing Source/Blob/Provenance stores.
// Exposed to native tests so isolated transport results need no network.
Result<Json> store(Database&, BlobStore&, ProvenanceStore&, Json snapshot,
                   const std::string& title = "", const Json& projection = Json::object());
// Existing /api/import uploads a .loomurl descriptor; integrator hook dispatches
// it before the file-source cache. Fetch is repeated to detect changed sources.
Result<Json> import_request_file(Runtime&, const std::filesystem::path&, const std::string& title = "");
}  // namespace url_share
}  // namespace loom
