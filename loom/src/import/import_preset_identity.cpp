#include "import/import_preset_identity.h"

#include <algorithm>
#include <stdexcept>

#include "import/import_preset.h"
#include "loom/util/sha256.h"

namespace loom {
namespace {
struct CompatibilityHashes {
  std::string values;
  std::string projection;
};

const CompatibilityHashes& compatibility_hashes() {
  static const CompatibilityHashes hashes = [] {
    const auto source = compiled_import_preset_source("import");
    if (!source) throw std::runtime_error(source.error().to_string());
    const auto document = json::parse(*source);
    if (!document) throw std::runtime_error(document.error().to_string());
    const Json* compatibility = json::find(*document, "compatibility");
    if (!compatibility || !compatibility->is_object())
      throw std::runtime_error("compiled import preset has no compatibility identity");
    auto token = [&](std::string_view field) {
      const Json* value = json::find(*compatibility, field);
      if (!value || !value->is_string())
        throw std::runtime_error("compiled import preset compatibility identity must be a SHA-256 string");
      const auto text = value->get<std::string>();
      if (text.size() != 64 || !std::all_of(text.begin(), text.end(), [](char ch) {
            return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
          }))
        throw std::runtime_error("compiled import preset compatibility identity must be lowercase SHA-256");
      return text;
    };
    return CompatibilityHashes{token("legacy_values_sha256"), token("legacy_projection_values_sha256")};
  }();
  return hashes;
}
}  // namespace

Json import_preset_values(const ImportOptions& options) {
  return Json{{"stream_threshold_bytes", options.stream_threshold_bytes},
              {"json_read_chunk_bytes", options.json_read_chunk_bytes},
              {"json_max_depth", options.json_max_depth},
              {"json_inline_threshold_bytes", options.json_inline_threshold_bytes},
              {"generic_inference_max_bytes", options.generic_inference_max_bytes}};
}

std::string import_preset_hash(const ImportOptions& options) {
  return Sha256::hex(json::canonical(import_preset_values(options)));
}

std::string import_projection_hash(const ImportOptions& options) {
  return Sha256::hex(json::canonical(Json{{"json_inline_threshold_bytes", options.json_inline_threshold_bytes},
      {"generic_inference_max_bytes", options.generic_inference_max_bytes}}));
}

bool import_preset_is_legacy(const ImportOptions& options) {
  return import_preset_hash(options) == compatibility_hashes().values;
}

bool import_projection_is_legacy(const ImportOptions& options) {
  return import_projection_hash(options) == compatibility_hashes().projection;
}

}  // namespace loom
