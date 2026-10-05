#include "import/import_preset.h"

#include <algorithm>
#include <array>
#include <ios>
#include <limits>
#include <stdexcept>

#include "loom/util/sha256.h"

namespace loom {
namespace {
#include "import_presets.inc"

template <std::size_t N>
std::string assemble(const std::string_view (&chunks)[N]) {
  std::string bytes;
  for (const auto chunk : chunks) bytes.append(chunk);
  return bytes;
}

const std::string& import_source_bytes() {
  static const std::string bytes = assemble(kImportPresetChunks);
  return bytes;
}

const std::string& audit_source_bytes() {
  static const std::string bytes = assemble(kImportAuditPresetChunks);
  return bytes;
}

constexpr std::array<std::string_view, 5> kImportValueFields{
    "stream_threshold_bytes", "json_read_chunk_bytes", "json_max_depth",
    "json_inline_threshold_bytes", "generic_inference_max_bytes"};

Result<std::uint64_t> unsigned_integer(const Json& values, std::string_view field) {
  const auto& value = values.at(std::string(field));
  if (!value.is_number_integer())
    return Error(Errc::InvalidArgument, std::string(field) + " must be a nonnegative JSON integer");
  if (value.is_number_unsigned()) return value.get<std::uint64_t>();
  const auto signed_value = value.get<std::int64_t>();
  if (signed_value < 0)
    return Error(Errc::InvalidArgument, std::string(field) + " must be nonnegative");
  return static_cast<std::uint64_t>(signed_value);
}

template <class T>
Result<T> represented_integer(const Json& values, std::string_view field) {
  LOOM_TRY_ASSIGN(const auto value, unsigned_integer(values, field));
  if (value > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
    return Error(Errc::InvalidArgument, std::string(field) + " exceeds its native integer representation");
  return static_cast<T>(value);
}

const Result<Json>& import_document() {
  static const Result<Json> document = []() -> Result<Json> {
    LOOM_TRY_ASSIGN(auto parsed, json::parse(import_source_bytes()));
    if (!parsed.is_object()) return Error(Errc::InvalidArgument, "import preset source must be an object");
    if (!parsed.contains("schema") || parsed["schema"] != "loom.import_preset/1")
      return Error(Errc::Unsupported, "unsupported import preset schema");
    if (!parsed.contains("id") || parsed["id"] != "loom.preset.import.default")
      return Error(Errc::InvalidArgument, "invalid import preset source identity");
    if (!parsed.contains("version")) return Error(Errc::InvalidArgument, "missing import preset version");
    LOOM_TRY_ASSIGN(const auto version, unsigned_integer(parsed, "version"));
    if (!version) return Error(Errc::InvalidArgument, "import preset version must be positive");
    if (!parsed.contains("values")) return Error(Errc::InvalidArgument, "missing import preset values");
    LOOM_TRY(import_preset_from_values(parsed["values"]));
    return parsed;
  }();
  return document;
}
}  // namespace

Result<ImportPresetValues> import_preset_from_values(const Json& values) {
  if (!values.is_object()) return Error(Errc::InvalidArgument, "import preset values must be an object");
  for (const auto field : kImportValueFields) {
    if (!values.contains(std::string(field)))
      return Error(Errc::InvalidArgument, "missing import preset field: " + std::string(field));
  }
  for (auto field = values.begin(); field != values.end(); ++field) {
    if (std::find(kImportValueFields.begin(), kImportValueFields.end(), field.key()) == kImportValueFields.end())
      return Error(Errc::InvalidArgument, "unknown import preset field: " + field.key());
  }
  LOOM_TRY_ASSIGN(const auto stream_threshold, represented_integer<std::int64_t>(values, "stream_threshold_bytes"));
  LOOM_TRY_ASSIGN(const auto chunk, represented_integer<std::size_t>(values, "json_read_chunk_bytes"));
  if (!chunk || chunk > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
    return Error(Errc::InvalidArgument, "json_read_chunk_bytes must be positive and fit std::streamsize");
  LOOM_TRY_ASSIGN(const auto depth, represented_integer<std::size_t>(values, "json_max_depth"));
  LOOM_TRY_ASSIGN(const auto inline_threshold, represented_integer<std::int64_t>(values, "json_inline_threshold_bytes"));
  LOOM_TRY_ASSIGN(const auto generic_max, represented_integer<std::int64_t>(values, "generic_inference_max_bytes"));
  return ImportPresetValues{stream_threshold, chunk, depth, inline_threshold, generic_max};
}

const ImportPresetValues& default_import_preset() {
  static const ImportPresetValues values = [] {
    const auto& document = import_document();
    if (!document) throw std::runtime_error(document.error().to_string());
    auto decoded = import_preset_from_values((*document)["values"]);
    if (!decoded) throw std::runtime_error(decoded.error().to_string());
    return *decoded;
  }();
  return values;
}

Status apply_import_preset_values(ImportOptions& options, const Json& values) {
  LOOM_TRY_ASSIGN(const auto decoded, import_preset_from_values(values));
  options.stream_threshold_bytes = decoded.stream_threshold_bytes;
  options.json_read_chunk_bytes = decoded.json_read_chunk_bytes;
  options.json_max_depth = decoded.json_max_depth;
  options.json_inline_threshold_bytes = decoded.json_inline_threshold_bytes;
  options.generic_inference_max_bytes = decoded.generic_inference_max_bytes;
  return {};
}

Result<std::string_view> compiled_import_preset_source(std::string_view resource_id) {
  if (resource_id == "import") return std::string_view(import_source_bytes());
  if (resource_id == "import_audit") return std::string_view(audit_source_bytes());
  return Error(Errc::NotFound, "unknown compiled import preset resource: " + std::string(resource_id));
}

Result<Json> inspect_import_preset() {
  const auto& document = import_document();
  if (!document) return document.error();
  return Json{{"schema", (*document)["schema"]}, {"id", (*document)["id"]},
              {"version", (*document)["version"]}, {"source_path", kImportPresetSource},
              {"source_sha256", Sha256::hex(import_source_bytes())},
              {"raw_source", import_source_bytes()}, {"values", (*document)["values"]}};
}

}  // namespace loom
