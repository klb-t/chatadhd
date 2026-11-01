#include "docx_projection.h"

#include <algorithm>
#include <limits>

#include "chat/docx_basic/adapter.h"
#include "chat/docx_basic/package.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace loom::text::sources::docx {
namespace {
Result<std::size_t> index(const Json& value) {
  if (!value.is_number_integer() || value < 0 ||
      value > (std::numeric_limits<std::int64_t>::max)())
    return Error(Errc::Conflict, "docx_projection_range_invalid");
  return value.get<std::size_t>();
}
}

Status validate(const Json& e, const std::string& hash, std::int64_t size) try {
  if (json::get_string(e, "schema") != "loom.docx_basic.result/1" ||
      json::get_string(e, "extractor") != "loom.docx_basic/1" ||
      !e.at("partial").is_boolean() || !e.at("partial").get<bool>() ||
      !e.at("warnings").is_array() || !e.at("blocks").is_array() || e.at("blocks").empty())
    return Error(Errc::Conflict, "docx_projection_invalid");
  for (const auto& warning : e.at("warnings"))
    if (!warning.is_string()) return Error(Errc::Conflict, "docx_projection_invalid");
  const auto& source = e.at("source");
  LOOM_TRY_ASSIGN(auto source_size, index(source.at("size")));
  if (json::get_string(source, "sha256") != hash || size < 0 || source_size != static_cast<std::uint64_t>(size))
    return Error(Errc::Conflict, "docx_source_mismatch");
  const auto member = json::get_string(source, "member");
  if (member.empty()) return Error(Errc::Conflict, "docx_projection_invalid");
  const auto text = e.at("text").get<std::string>();
  const auto& projection = e.at("projection");
  LOOM_TRY_ASSIGN(auto text_size, index(projection.at("size")));
  if (json::get_string(projection, "schema") != "loom.docx_basic.projection/1" ||
      json::get_string(projection, "coordinate") != "derived_utf8_bytes" ||
      json::get_string(projection, "sha256") != Sha256::hex(text) || text_size != text.size() ||
      utf8::repair(text) != text || !projection.at("ranges").is_array() ||
      projection.at("ranges").size() != e.at("blocks").size())
    return Error(Errc::Conflict, "docx_projection_invalid");
  std::size_t cursor = 0, previous_body = 0;
  for (std::size_t i = 0; i < e.at("blocks").size(); ++i) {
    const auto& block = e.at("blocks")[i];
    const auto& range = projection.at("ranges")[i];
    const auto locator = block.at("locator").get<std::string>();
    const auto prefix = member + "#body/";
    if (locator.rfind(prefix, 0) != 0) return Error(Errc::Conflict, "docx_projection_locator_invalid");
    const auto ordinal = locator.substr(prefix.size());
    if (ordinal.empty() || (ordinal.size() > 1 && ordinal.front() == '0') ||
        !std::all_of(ordinal.begin(), ordinal.end(), [](char c) { return c >= '0' && c <= '9'; }))
      return Error(Errc::Conflict, "docx_projection_locator_invalid");
    std::size_t body = 0;
    for (const char c : ordinal) {
      if (body > ((std::numeric_limits<std::size_t>::max)() - (c - '0')) / 10)
        return Error(Errc::Conflict, "docx_projection_locator_invalid");
      body = body * 10 + (c - '0');
    }
    if (i && body <= previous_body) return Error(Errc::Conflict, "docx_projection_locator_invalid");
    previous_body = body;
    if (!range.at("byte_range").is_array() || range.at("byte_range").size() != 2 ||
        !range.at("text_range").is_array() || range.at("text_range").size() != 2)
      return Error(Errc::Conflict, "docx_projection_range_invalid");
    LOOM_TRY_ASSIGN(auto begin, index(range.at("byte_range")[0]));
    LOOM_TRY_ASSIGN(auto end, index(range.at("byte_range")[1]));
    LOOM_TRY_ASSIGN(auto start, index(range.at("text_range")[0]));
    LOOM_TRY_ASSIGN(auto stop, index(range.at("text_range")[1]));
    if (begin != cursor || begin > start || start > stop || stop > end || end > text.size())
      return Error(Errc::Conflict, "docx_projection_range_invalid");
    const auto kind = json::get_string(block, "kind");
    std::string marker;
    if (kind == "heading") {
      LOOM_TRY_ASSIGN(auto level, index(block.at("level")));
      // OOXML outlineLvl 0..8 is rendered as heading depth 1..9 by this reader.
      if (level < 1 || level > 9) return Error(Errc::Conflict, "docx_projection_invalid");
      marker = std::string(level, '#') + ' ';
    } else if (kind == "list_item") marker = "- ";
    else if (kind != "paragraph" && kind != "table")
      return Error(Errc::Conflict, "docx_projection_invalid");
    const auto value = block.at("text").get<std::string>();
    const std::string separator = i + 1 < e.at("blocks").size() ? "\n" : "";
    if (text.substr(begin, end - begin) != marker + value + separator ||
        start - begin != marker.size() || stop - start != value.size() ||
        text.substr(start, stop - start) != value)
      return Error(Errc::Conflict, "docx_projection_text_mismatch");
    cursor = end;
  }
  if (cursor != text.size()) return Error(Errc::Conflict, "docx_projection_range_invalid");
  return {};
} catch (const Json::exception&) {
  return Error(Errc::Conflict, "docx_projection_invalid");
}

Result<Prepared> prepare(Runtime& rt, const std::string& hash) {
  LOOM_TRY(rt.blobs().verify(hash));
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("chat_context", rt.paths().root));
  Json reader;
  for (const auto& item : profile.values().at("attachments").at("document_readers")) {
    const auto extensions = item.value("extensions", Json::array());
    if (std::find(extensions.begin(), extensions.end(), Json(".docx")) != extensions.end()) {
      reader = item;
      break;
    }
  }
  if (json::get_string(reader, "adapter") != "docx_basic")
    return Error(Errc::Unavailable, "docx_reader_unavailable");
  LOOM_TRY_ASSIGN(auto command, chat::docx_command(reader));
  if (!std::filesystem::path(command.python).is_absolute() ||
      !std::filesystem::path(command.script).is_absolute() ||
      command.python.find('\0') != std::string::npos || command.script.find('\0') != std::string::npos)
    return Error(Errc::Unavailable, "docx_reader_unavailable");
  LOOM_TRY_ASSIGN(auto script, fsutil::read_file(command.script));
  reader["python"] = command.python;
  reader["script"] = command.script;
  LOOM_TRY_ASSIGN(auto extraction, chat::read_docx_basic(rt.blobs().local_path(hash), reader));
  // Refuse source/helper replacement during extraction. The reader's source
  // hash also binds its exact input; the saved output is the replay evidence.
  LOOM_TRY(rt.blobs().verify(hash));
  LOOM_TRY_ASSIGN(auto after, fsutil::read_file(command.script));
  if (script != after) return Error(Errc::Conflict, "docx_reader_changed");
  std::error_code ec;
  const auto size = std::filesystem::file_size(rt.blobs().local_path(hash), ec);
  if (ec || size > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
    return Error(Errc::Io, "docx_input_unavailable");
  LOOM_TRY(validate(extraction, hash, static_cast<std::int64_t>(size)));
  const Json transform{{"schema", "loom.docx_source_transform/1"},
                       {"extractor", extraction.at("extractor")},
                       {"projection", extraction.at("projection").at("schema")},
                       {"reader_script_blob", Sha256::hex(script)}, {"limits", reader.at("limits")}};
  Json derivation{{"schema", "loom.docx_source_derivation/1"},
                  {"original_source_id", "src_docx_" + hash}, {"original_sha256", hash},
                  {"original_size", size}, {"member", extraction.at("source").at("member")},
                  {"transform", transform}, {"transform_sha256", Sha256::hex(json::canonical(transform))},
                  {"extraction_blob", Sha256::hex(json::canonical(extraction))},
                  {"text_sha256", extraction.at("projection").at("sha256")}};
  return Prepared{std::move(extraction), std::move(reader), std::move(derivation), std::move(script)};
}
}  // namespace loom::text::sources::docx
