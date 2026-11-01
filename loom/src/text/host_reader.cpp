#include "host_reader.h"
#include "file_graph.h"
#include "chat/docx_basic/adapter.h"
#include "chat/docx_basic/package.h"
#include "loom/model.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace loom::text::sources::host {
namespace {
Result<Json> manifest(const std::filesystem::path& root) {
  LOOM_TRY_ASSIGN(auto raw, fsutil::read_file(root / "runtime.json"));
  LOOM_TRY_ASSIGN(auto config, json::parse(raw));
  if (json::get_string(config, "schema") != "loom.structure_runtime/1")
    return Error(Errc::Unavailable, "host_reader_manifest_invalid");
  for (const auto* file : {"worker.py", "profile.json", "csv/reader.py", "python_ast/reader.py", "python_ast/profile.json"}) {
    LOOM_TRY_ASSIGN(auto bytes, fsutil::read_file(root / file));
    if (Sha256::hex(bytes) != config.at("files").at(file).get<std::string>())
      return Error(Errc::Conflict, "host_reader_package_changed");
  }
  const auto python = config.at("python").get<std::string>();
  if (!std::filesystem::path(python).is_absolute() || python.find('\0') != std::string::npos)
    return Error(Errc::Unavailable, "host_reader_interpreter_invalid");
  LOOM_TRY_ASSIGN(auto interpreter, fsutil::read_file(python));
  if (Sha256::hex(interpreter) != config.at("python_sha256").get<std::string>())
    return Error(Errc::Conflict, "host_reader_interpreter_changed");
  return config;
}
}
std::string occurrence_id(const Json& unit, const std::string& method_hash, std::size_t index) {
  // A row and a sole cell can cover identical bytes. Role occurrences remain
  // distinct; content/location identity alone is not an occurrence identity.
  return "n_text_occ_" + Sha256::hex(unit.at("id").get<std::string>() + ":" + method_hash + ":" + std::to_string(index));
}
Result<Prepared> prepare(const std::filesystem::path& original, const std::string& hash,
    const std::string& format, const std::string& key, const std::string& name, const Json& selection) try {
  if (file_format(format)) return prepare_file_graph(original, hash, format, key, name);
  if (format != "csv" && format != "python_ast") return Error(Errc::Unsupported, "host_reader_format_unavailable");
  LOOM_TRY_ASSIGN(auto root, chat::packaged_reader_directory("structure_readers"));
  LOOM_TRY_ASSIGN(auto before, manifest(root));
  LOOM_TRY_ASSIGN(auto profile_bytes, fsutil::read_file(root / "profile.json"));
  LOOM_TRY_ASSIGN(auto profile, json::parse(profile_bytes));
  if (json::get_string(profile, "schema") != "loom.structure_readers.profile/1")
    return Error(Errc::Unavailable, "host_reader_profile_invalid");
  for (const auto& value : {profile.at("timeout_ms"), profile.at("poll_ms"), profile.at("max_output_bytes"),
       profile.at("limits").at("max_input_bytes"), profile.at("limits").at("cpu_seconds"), profile.at("limits").at("memory_bytes")}) {
    if (!value.is_number_integer() || value <= 0) return Error(Errc::InvalidArgument, "host_reader_limit_invalid");
  }
  std::error_code ec;
  const auto size = std::filesystem::file_size(original, ec);
  if (ec || size > profile.at("limits").at("max_input_bytes").get<std::uint64_t>())
    return Error(Errc::InvalidArgument, "host_reader_input_limit");
  LOOM_TRY_ASSIGN(auto bytes, fsutil::read_file(original));
  if (Sha256::hex(bytes) != hash) return Error(Errc::Conflict, "host_reader_original_mismatch");
  if (utf8::repair(bytes) != bytes) return Error(Errc::Unsupported, "host_reader_invalid_utf8");
  Json config = profile;
  config["python"] = before.at("python");
  config["script"] = (root / "worker.py").string();
  Json job{{"format", format}, {"source_key", key}, {"name", name}, {"profile", profile}};
  if (!selection.is_null()) job["selection"] = selection;
  LOOM_TRY_ASSIGN(auto output, chat::read_local_json(original, config, job));
  LOOM_TRY_ASSIGN(auto after, manifest(root));
  LOOM_TRY_ASSIGN(auto bytes_after, fsutil::read_file(original));
  if (before != after || bytes != bytes_after) return Error(Errc::Conflict, "host_reader_changed");
  const auto source = "src_text_" + Sha256::hex(json::canonical(Json::array({key, hash})));
  const auto& receipt = output.at("receipt");
  if (output.at("schema") != "loom.host_structure/1" || receipt.at("original_sha256") != hash ||
      receipt.at("source").at("id") != source || receipt.at("source").at("uri") != key ||
      receipt.at("source").at("title") != name || receipt.at("source").at("size") != bytes.size() ||
      !output.at("parts").is_array() || !receipt.at("observations").is_array())
    return Error(Errc::Conflict, "host_reader_result_invalid");
  std::size_t index = 0;
  for (const auto& part : output.at("parts")) {
    LOOM_TRY_ASSIGN(auto unit, model::Unit::from_json(part.at("unit")));
    const auto start = unit.locator.byte_start.value_or(-1), len = unit.locator.byte_len.value_or(-1);
    if (unit.source != source || unit.locator.source != source || start < 0 || len < 0 ||
        static_cast<std::uint64_t>(start) > bytes.size() || static_cast<std::uint64_t>(len) > bytes.size() - start ||
        unit.id != model::Unit::make_id(source, unit.locator) || unit.bytes != len)
      return Error(Errc::Conflict, "host_reader_part_invalid");
    const auto& parent = part.at("parent");
    if (!parent.is_null() && (!parent.is_number_unsigned() && !parent.is_number_integer()))
      return Error(Errc::Conflict, "host_reader_parent_invalid");
    if (!parent.is_null() && (parent < 0 || parent.get<std::size_t>() >= index))
      return Error(Errc::Conflict, "host_reader_parent_invalid");
    ++index;
  }
  // Selection is replay output, not part of the persisted materialization receipt.
  Json materialization = output; materialization.erase("selection");
  Json binding{{"schema", "loom.host_reader_binding/1"}, {"format", format}, {"source_key", key}, {"name", name},
      {"input_sha256", hash}, {"runtime", before}, {"profile_sha256", Sha256::hex(profile_bytes)},
      {"receipt_blob", Sha256::hex(json::canonical(materialization))}};
  return Prepared{std::move(output), std::move(binding)};
} catch (const Json::exception&) {
  return Error(Errc::Conflict, "host_reader_invalid_data");
}
Status verify_node(const Node& node, const std::filesystem::path& original) try {
  const auto& meta = node.metadata;
  const auto& saved = meta.at("method").at("host_reader");
  LOOM_TRY_ASSIGN(auto fresh, prepare(original, saved.at("input_sha256").get<std::string>(),
      saved.at("format").get<std::string>(), saved.at("source_key").get<std::string>(), saved.at("name").get<std::string>()));
  if (json::canonical(fresh.binding) != json::canonical(saved) || Sha256::hex(json::canonical(meta.at("method"))) != meta.at("method_sha256").get<std::string>())
    return Error(Errc::Conflict, "host_reader_binding_mismatch");
  const auto index = meta.at("index").get<std::size_t>();
  const auto& part = fresh.output.at("parts").at(index);
  if (json::canonical(part.at("unit")) != json::canonical(meta.at("unit")) || part.at("observations") != meta.at("observation_refs") ||
      node.id != occurrence_id(part.at("unit"), meta.at("method_sha256").get<std::string>(), index))
    return Error(Errc::Conflict, "host_reader_occurrence_mismatch");
  const auto start=part.at("unit").at("locator").at("byte_start").get<std::size_t>();
  const auto len=part.at("unit").at("locator").at("byte_len").get<std::size_t>();
  if (fresh.output.contains("text") && fresh.output.at("text").get<std::string>().substr(start,len)!=node.content)
    return Error(Errc::Conflict,"host_reader_content_mismatch");
  return {};
} catch (const Json::exception&) { return Error(Errc::Conflict, "host_reader_invalid_record"); }
}
