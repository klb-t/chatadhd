#pragma once
// Integrator-only receipt boundary for existing source_parts. No interpreter,
// storage engine or new public ABI. The host runs reader.py + validate first;
// this checks native contracts and byte integrity, not Python syntax authenticity.
#include <limits>
#include <set>
#include "loom/model.h"
#include "loom/util/sha256.h"

namespace loom::text::sources::python_ast {
inline Result<std::int64_t> integer(const Json& value) {
  if (!value.is_number_integer() || value < 0 ||
      value > (std::numeric_limits<std::int64_t>::max)())
    return Error(Errc::Conflict, "python_ast_integer_invalid");
  return value.get<std::int64_t>();
}

inline Status validate(const Json& receipt, const std::string& bytes,
                       const std::string& key, const std::string& name) try {
  const auto hash = Sha256::hex(bytes);
  const auto source = "src_text_" + Sha256::hex(json::canonical(Json::array({key, hash})));
  const auto& src = receipt.at("source");
  const auto& method = receipt.at("method");
  const auto& ds = receipt.at("declarations");
  const auto& us = receipt.at("units");
  const auto& os = receipt.at("observations");
  if (receipt.at("schema") != "loom.python_ast_basic.result/1" ||
      receipt.at("parser") != "loom.python_ast_basic/1" ||
      receipt.at("coordinate") != "source_utf8_bytes" ||
      receipt.at("original_sha256") != hash || src.at("blob_hash") != hash ||
      src.at("id") != source || src.at("uri") != key || src.at("title") != name ||
      src.at("size") != bytes.size() || method.at("id") != receipt.at("parser") ||
      method.at("coordinate") != "source_utf8_bytes" ||
      method.at("structural_evidence_class") != "derived" ||
      receipt.at("method_sha256") != Sha256::hex(json::canonical(method)) ||
      !ds.is_array() || ds.empty() || !us.is_array() || !os.is_array() ||
      ds.size() != us.size() || ds.size() != os.size() ||
      ds.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    return Error(Errc::Conflict, "python_ast_receipt_invalid");
  std::set<std::string> ids;
  std::int64_t previous = 0;
  for (std::size_t i = 0; i < ds.size(); ++i) {
    const auto& d = ds[i];
    const auto& range = d.at("byte_range");
    const auto& lines = d.at("line_range");
    const auto& ast = d.at("ast_range");
    if (!range.is_array() || range.size() != 2 ||
        !lines.is_array() || lines.size() != 2 || !ast.is_array() || ast.size() != 2)
      return Error(Errc::Conflict, "python_ast_range_invalid");
    LOOM_TRY_ASSIGN(auto begin, integer(range[0]));
    LOOM_TRY_ASSIGN(auto end, integer(range[1]));
    LOOM_TRY_ASSIGN(auto first, integer(lines[0]));
    LOOM_TRY_ASSIGN(auto last, integer(lines[1]));
    LOOM_TRY_ASSIGN(auto ast_begin, integer(ast[0]));
    LOOM_TRY_ASSIGN(auto ast_end, integer(ast[1]));
    if (begin < previous || begin > end || static_cast<std::uint64_t>(end) > bytes.size() ||
        first < 1 || first > last || ast_begin < begin || ast_begin > ast_end || ast_end != end ||
        first > (std::numeric_limits<int>::max)() || last > (std::numeric_limits<int>::max)())
      return Error(Errc::Conflict, "python_ast_range_invalid");
    previous = begin;
    const auto kind = d.at("kind").get<std::string>();
    if ((i == 0 && (kind != "module" || !d.at("parent").is_null() ||
                    begin != 0 || static_cast<std::uint64_t>(end) != bytes.size())) ||
        (i != 0 && kind != "class" && kind != "function" && kind != "async_function"))
      return Error(Errc::Conflict, "python_ast_scope_invalid");
    if (i) {
      LOOM_TRY_ASSIGN(auto parent, integer(d.at("parent")));
      if (static_cast<std::uint64_t>(parent) >= i ||
          ds[parent].at("byte_range")[0] > begin || ds[parent].at("byte_range")[1] < end)
        return Error(Errc::Conflict, "python_ast_scope_invalid");
    }
    LOOM_TRY_ASSIGN(auto unit, model::Unit::from_json(us[i]));
    LOOM_TRY_ASSIGN(auto observation, model::Observation::from_json(os[i]));
    const auto quote = bytes.substr(static_cast<std::size_t>(begin), static_cast<std::size_t>(end - begin));
    const auto& attrs = unit.attrs;
    if (unit.source != source || unit.locator.source != source ||
        !unit.locator.member.empty() || !unit.locator.json_pointer.empty() ||
        unit.locator.time_start || unit.locator.time_end ||
        unit.locator.byte_start != begin || unit.locator.byte_len != end - begin ||
        (i == 0 ? unit.locator.line.has_value() : unit.locator.line != first) ||
        unit.bytes != end - begin || unit.title != d.at("name").get<std::string>() ||
        unit.kind != method.at("profile").at("kinds").at(kind).get<std::string>() ||
        unit.id != model::Unit::make_id(source, unit.locator) || !ids.insert(unit.id).second ||
        d.at("unit_id") != unit.id || observation.unit != unit.id ||
        observation.id != model::Observation::make_id(unit.id, unit.locator, quote) ||
        d.at("observation_id") != observation.id || observation.text != quote ||
        observation.locator.to_json() != unit.locator.to_json() ||
        observation.kind != model::ObservationKind::CodeBlock || observation.ordinal != static_cast<int>(i) ||
        d.at("text_sha256") != Sha256::hex(quote) || attrs.at("recognized_kind") != kind ||
        attrs.at("name") != d.at("name") || attrs.at("parent") != d.at("parent") ||
        attrs.at("line_range") != lines || attrs.at("ast_range") != ast ||
        attrs.at("original_sha256") != hash || attrs.at("coordinate") != "source_utf8_bytes" ||
        attrs.at("method_sha256") != receipt.at("method_sha256") ||
        attrs.at("structural_evidence_class") != "derived" ||
        observation.attrs.at("evidence_class") != "observed" ||
        observation.attrs.at("structural_evidence_class") != "derived")
      return Error(Errc::Conflict, "python_ast_record_invalid");
  }
  return {};
} catch (const Json::exception&) {
  return Error(Errc::Conflict, "python_ast_receipt_invalid");
}
}  // namespace loom::text::sources::python_ast
