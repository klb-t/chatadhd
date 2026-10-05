#include "import_audit.h"

#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

#include "import_preset.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace loom {

Result<ImportAuditPresetValues> import_audit_preset_from_values(const Json& values) {
  if (!values.is_object()) return Error(Errc::InvalidArgument, "audit effective values must be an object");
  const auto* active = json::find(values, "native_active_only");
  if (!active || !active->is_boolean())
    return Error(Errc::InvalidArgument, "audit native_active_only must be present and boolean");
  auto number = [&](std::string_view key) -> Result<double> {
    const auto* value = json::find(values, key);
    if (!value || !value->is_number())
      return Error(Errc::InvalidArgument, "audit numeric effective value missing or invalid: " + std::string(key));
    const double result = value->get<double>();
    if (!std::isfinite(result)) return Error(Errc::InvalidArgument, "audit effective value must be finite");
    return result;
  };
  LOOM_TRY_ASSIGN(auto low, number("chars_per_token_low"));
  LOOM_TRY_ASSIGN(auto high, number("chars_per_token_high"));
  LOOM_TRY_ASSIGN(auto ratio, number("output_ratio"));
  if (high <= 0 || low < high || ratio < 0)
    return Error(Errc::InvalidArgument, "audit bounds require low >= high > 0 and nonnegative ratio");
  return ImportAuditPresetValues{active->get<bool>(), low, high, ratio};
}

const ImportAuditPresetValues& default_import_audit_preset() {
  static const ImportAuditPresetValues preset = [] {
    const auto source = compiled_import_preset_source("import_audit");
    if (!source) throw std::logic_error(source.error().to_string());
    const auto document = json::parse(source.value());
    if (!document || !document->is_object() || document->value("schema", "") != "loom.import_audit_preset/1")
      throw std::logic_error("invalid embedded import audit descriptor");
    const auto* values = json::find(*document, "values");
    if (!values) throw std::logic_error("embedded audit descriptor lacks values");
    const auto decoded = import_audit_preset_from_values(*values);
    if (!decoded) throw std::logic_error(decoded.error().to_string());
    return decoded.value();
  }();
  return preset;
}

Status apply_import_audit_preset_values(ImportAuditOptions& options, const Json& values) {
  LOOM_TRY_ASSIGN(auto decoded, import_audit_preset_from_values(values));
  auto replacement = options;
  replacement.active_only = decoded.active_only;
  replacement.chars_per_token_low = decoded.chars_per_token_low;
  replacement.chars_per_token_high = decoded.chars_per_token_high;
  replacement.output_ratio = decoded.output_ratio;
  LOOM_TRY(validate_import_audit_options(replacement));
  options = replacement;
  return {};
}

Result<Json> inspect_import_audit_preset() {
  LOOM_TRY_ASSIGN(auto raw, compiled_import_preset_source("import_audit"));
  LOOM_TRY_ASSIGN(auto document, json::parse(raw));
  if (!document.is_object() || document.value("schema", "") != "loom.import_audit_preset/1")
    return Error(Errc::InvalidArgument, "invalid audit descriptor schema");
  const auto* values = json::find(document, "values");
  if (!values) return Error(Errc::InvalidArgument, "audit descriptor lacks effective values");
  LOOM_TRY(import_audit_preset_from_values(*values));
  document["source_path"] = "loom/data/presets/import_audit.pack";
  document["source_sha256"] = Sha256::hex(raw);
  document["raw_source"] = std::string(raw);
  return document;
}

Status validate_import_audit_options(const ImportAuditOptions& o) {
  if (!std::isfinite(o.chars_per_token_low) || !std::isfinite(o.chars_per_token_high) ||
      o.chars_per_token_low <= 0 || o.chars_per_token_high <= 0 ||
      o.chars_per_token_low < o.chars_per_token_high)
    return Error(Errc::InvalidArgument, "audit character/token bounds must be finite, positive and low >= high");
  if (!std::isfinite(o.output_ratio) || o.output_ratio < 0)
    return Error(Errc::InvalidArgument, "audit output ratio must be finite and nonnegative");
  for (const auto& price : {o.input_price, o.output_price})
    if (price && (!std::isfinite(*price) || *price < 0))
      return Error(Errc::InvalidArgument, "audit prices must be finite nonnegative USD per million tokens");
  if (o.input_price.has_value() != o.output_price.has_value())
    return Error(Errc::InvalidArgument, "supply both --audit-input-price and --audit-output-price");
  return {};
}

Result<Json> audit_import(Database& db, const std::vector<Conversation>& conversations,
                          const ImportAuditOptions& o) {
  LOOM_TRY(validate_import_audit_options(o));
  auto lock = db.lock();
  // One row at a time: metadata and the entire archive are never materialized.
  LOOM_TRY_ASSIGN(auto row, db.conn().prepare("SELECT role, status, text FROM messages WHERE conv_id=?"));
  Json by_status = Json::object(), by_role = Json::object();
  std::set<std::string> seen;
  std::int64_t messages = 0, chars = 0, projected_messages = 0, projected_chars = 0, projected_conversations = 0;
  auto add = [](std::int64_t& target, std::int64_t value) -> Status {
    if (value > std::numeric_limits<std::int64_t>::max() - target)
      return Error(Errc::InvalidArgument, "audit count exceeds int64 representation");
    target += value;
    return {};
  };
  for (const auto& c : conversations) {
    if (!seen.insert(c.id).second) continue;
    row.reset();
    row.bind(1, c.id);
    bool projected = false;
    while (true) {
      LOOM_TRY_ASSIGN(bool has_row, row.step());
      if (!has_row) break;
      const std::string role = row.is_null(0) ? "<null>" : row.get_text(0);
      const std::string status = row.is_null(1) ? "<null>" : row.get_text(1);
      const auto length = utf8::length(row.get_text(2));  // embedded NUL is a character too
      if (length > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
        return Error(Errc::InvalidArgument, "message length exceeds int64 representation");
      const auto n = static_cast<std::int64_t>(length);
      LOOM_TRY(add(messages, 1));
      LOOM_TRY(add(chars, n));
      for (auto* group : {&by_status[status], &by_role[role]}) {
        if (group->is_null()) *group = Json{{"messages", 0}, {"characters", 0}};
        auto count = (*group)["messages"].get<std::int64_t>();
        auto characters = (*group)["characters"].get<std::int64_t>();
        LOOM_TRY(add(count, 1));
        LOOM_TRY(add(characters, n));
        (*group)["messages"] = count;
        (*group)["characters"] = characters;
      }
      if (!o.active_only || status == "active") {
        projected = true;
        LOOM_TRY(add(projected_messages, 1));
        LOOM_TRY(add(projected_chars, n));
      }
    }
    if (projected) ++projected_conversations;
  }
  Json bands = Json::object();
  for (const auto& [name, cpt] : std::vector<std::pair<std::string, double>>{
           {"low", o.chars_per_token_low}, {"high", o.chars_per_token_high}}) {
    const double input = static_cast<double>(projected_chars) / cpt;
    const double output = input * o.output_ratio;
    if (!std::isfinite(input) || !std::isfinite(output))
      return Error(Errc::InvalidArgument, "audit token estimate exceeds numeric representation");
    Json usd = nullptr;
    if (o.input_price) {
      const double cost = input / 1e6 * *o.input_price + output / 1e6 * *o.output_price;
      if (!std::isfinite(cost)) return Error(Errc::InvalidArgument, "audit cost exceeds numeric representation");
      usd = cost;
    }
    bands[name] = Json{{"input_tokens_estimate", input}, {"output_tokens_estimate", output}, {"model_cost_usd_estimate", usd}};
  }
  return Json{{"schema", "loom.import_audit/1"}, {"model_calls", 0},
              {"local_import_model_cost_usd", 0}, {"local_compute_cost_usd", nullptr},
              {"raw", {{"conversations", seen.size()}, {"messages", messages}, {"characters", chars}}},
              {"projected", {{"scope", o.active_only ? "active" : "all"}, {"conversations", projected_conversations},
                              {"messages", projected_messages}, {"characters", projected_chars}}},
              {"by_status", by_status}, {"by_role", by_role}, {"estimates", bands},
              {"assumptions", {{"character_unit", "Unicode codepoints"}, {"chars_per_token_low", o.chars_per_token_low},
                               {"chars_per_token_high", o.chars_per_token_high}, {"output_ratio", o.output_ratio},
                               {"input_price_usd_per_million", o.input_price ? Json(*o.input_price) : Json(nullptr)},
                               {"output_price_usd_per_million", o.output_price ? Json(*o.output_price) : Json(nullptr)},
                               {"reading_passes", 1}, {"prefix_tokens", 0}, {"prices_verified_current", false},
                               {"content", "normalized message text only; excludes source JSON, attachments and OCR"},
                               {"token_count", "estimate; tokenizer/provider not called"}}}};
}

}  // namespace loom
