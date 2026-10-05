#include "loom/runtime_profile.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <set>

#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace loom {
namespace {

#include "runtime_profiles_embedded.inc"

Error invalid(std::string_view pointer, std::string_view reason) {
  return Error(Errc::InvalidArgument, "runtime profile " + std::string(pointer) + ": " + std::string(reason));
}

bool safe_domain(std::string_view domain) {
  return !domain.empty() && std::all_of(domain.begin(), domain.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
  });
}

std::string escape_pointer(std::string_view s) {
  std::string out;
  for (char c : s) out += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
  return out;
}

bool is_type(const Json& value, std::string_view type) {
  if (type == "object") return value.is_object();
  if (type == "array") return value.is_array();
  if (type == "string") return value.is_string();
  if (type == "integer") return value.is_number_integer();
  if (type == "number") return value.is_number() && std::isfinite(value.get<double>());
  if (type == "boolean") return value.is_boolean();
  if (type == "null") return value.is_null();
  return false;
}

struct BinaryNumber {
  bool negative = false;
  std::uint64_t magnitude = 0;
  int exponent = 0;
};

BinaryNumber binary_number(const Json& value) {
  BinaryNumber out;
  if (value.is_number_unsigned()) out.magnitude = value.get<std::uint64_t>();
  else if (value.is_number_integer()) {
    auto n = value.get<std::int64_t>();
    out.negative = n < 0;
    out.magnitude = n < 0 ? static_cast<std::uint64_t>(-(n + 1)) + 1 : static_cast<std::uint64_t>(n);
  } else {
    auto bits = std::bit_cast<std::uint64_t>(value.get<double>());
    out.negative = (bits >> 63) != 0;
    auto e = static_cast<int>((bits >> 52) & 0x7ff);
    out.magnitude = bits & ((std::uint64_t{1} << 52) - 1);
    if (e) out.magnitude |= std::uint64_t{1} << 52;
    out.exponent = e ? e - 1023 - 52 : -1074;
  }
  if (!out.magnitude) { out.negative = false; out.exponent = 0; }
  else {
    auto trailing = std::countr_zero(out.magnitude);
    out.magnitude >>= trailing;
    out.exponent += trailing;
  }
  return out;
}

// Integers and finite IEEE doubles are exact binary rationals. Align only
// when their leading-bit positions match: the shift then always fits 64 bits.
// This avoids uint64->int64 wrapping and integer->double rounding in Json's
// mixed-number comparisons, including on platforms with 64-bit long double.
int compare_numbers(const Json& left, const Json& right) {
  auto a = binary_number(left), b = binary_number(right);
  if (a.negative != b.negative) return a.negative ? -1 : 1;
  int order = 0;
  if (!a.magnitude || !b.magnitude) order = a.magnitude == b.magnitude ? 0 : a.magnitude ? 1 : -1;
  else {
    int leading_a = static_cast<int>(std::bit_width(a.magnitude)) + a.exponent;
    int leading_b = static_cast<int>(std::bit_width(b.magnitude)) + b.exponent;
    if (leading_a != leading_b) order = leading_a < leading_b ? -1 : 1;
    else {
      int exponent = std::min(a.exponent, b.exponent);
      auto x = a.magnitude << (a.exponent - exponent);
      auto y = b.magnitude << (b.exponent - exponent);
      order = x == y ? 0 : x < y ? -1 : 1;
    }
  }
  return a.negative ? -order : order;
}

bool exact_equal(const Json& a, const Json& b) {
  if (a.is_number() && b.is_number()) {
    if (!std::isfinite(a.get<double>()) || !std::isfinite(b.get<double>())) return false;
    return compare_numbers(a, b) == 0;
  }
  if (a.type() != b.type()) return false;
  if (a.is_array()) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (!exact_equal(a[i], b[i])) return false;
    return true;
  }
  if (a.is_object()) {
    if (a.size() != b.size()) return false;
    for (auto it = a.begin(); it != a.end(); ++it) {
      const Json* other = json::find(b, it.key());
      if (!other || !exact_equal(it.value(), *other)) return false;
    }
    return true;
  }
  return a == b;
}

// Deliberately small advertised schema vocabulary. A schema using an
// unsupported assertion is rejected, rather than silently ignored.
Status validate_schema(const Json& schema, const std::string& p) {
  if (!schema.is_object()) return invalid(p, "value schema must be an object");
  static const std::set<std::string> implemented{
      "type", "required", "properties", "items", "additionalProperties", "minimum", "maximum",
      "minLength", "minItems", "enum", "description", "title", "x-setting", "x-unit", "x-consumer"};
  for (auto it = schema.begin(); it != schema.end(); ++it) {
    if (!implemented.count(it.key())) return invalid(p + "/" + escape_pointer(it.key()), "unsupported schema keyword");
  }
  for (const char* key : {"description", "title", "x-setting", "x-unit", "x-consumer"}) {
    if (const Json* n = json::find(schema, key); n && !n->is_string())
      return invalid(p + "/" + key, "annotation must be a string");
  }
  const Json* t = json::find(schema, "type");
  static const std::set<std::string> types{"object", "array", "string", "integer", "number", "boolean", "null"};
  auto supported = [&](const Json& name) { return name.is_string() && types.count(name.get<std::string>()); };
  if (!t || (t->is_array() ? t->empty() || !std::all_of(t->begin(), t->end(), supported) : !supported(*t)))
    return invalid(p, "schema needs a supported type or nonempty type union");
  if (const Json* r = json::find(schema, "required")) {
    if (!r->is_array()) return invalid(p + "/required", "must be an array of names");
    for (const auto& k : *r) if (!k.is_string()) return invalid(p + "/required", "must contain names");
  }
  if (const Json* properties = json::find(schema, "properties")) {
    if (!properties->is_object()) return invalid(p + "/properties", "must be an object");
    for (auto it = properties->begin(); it != properties->end(); ++it)
      LOOM_TRY(validate_schema(it.value(), p + "/properties/" + escape_pointer(it.key())));
  }
  if (const Json* items = json::find(schema, "items")) LOOM_TRY(validate_schema(*items, p + "/items"));
  if (const Json* additional = json::find(schema, "additionalProperties")) {
    if (additional->is_object()) LOOM_TRY(validate_schema(*additional, p + "/additionalProperties"));
    else if (!additional->is_boolean()) return invalid(p + "/additionalProperties", "must be boolean or schema");
  }
  for (const char* key : {"minimum", "maximum"}) {
    if (const Json* n = json::find(schema, key); n && (!n->is_number() || !std::isfinite(n->get<double>())))
      return invalid(p + "/" + key, "must be a finite number");
  }
  for (const char* key : {"minLength", "minItems"}) {
    if (const Json* n = json::find(schema, key); n && (!n->is_number_integer() || n->get<double>() < 0))
      return invalid(p + "/" + key, "must be a nonnegative integer");
  }
  if (const Json* e = json::find(schema, "enum"); e && (!e->is_array() || e->empty()))
    return invalid(p + "/enum", "must be a nonempty array");
  return {};
}

Status validate_value(const Json& value, const Json& schema, const std::string& p) {
  const auto& type = schema.at("type");
  const bool matches = type.is_array()
      ? std::any_of(type.begin(), type.end(), [&](const Json& name) { return is_type(value, name.get<std::string>()); })
      : is_type(value, type.get<std::string>());
  if (!matches) return invalid(p, "expected " + json::dump(type));
  if (const Json* e = json::find(schema, "enum")) {
    if (!std::any_of(e->begin(), e->end(), [&](const Json& candidate) { return exact_equal(candidate, value); }))
      return invalid(p, "value is outside declared enum");
  }
  if (value.is_number()) {
    double number = value.get<double>();
    if (!std::isfinite(number)) return invalid(p, "number must be finite");
    if (const Json* min = json::find(schema, "minimum"); min && compare_numbers(value, *min) < 0) return invalid(p, "below declared minimum");
    if (const Json* max = json::find(schema, "maximum"); max && compare_numbers(value, *max) > 0) return invalid(p, "above declared maximum");
  }
  if (value.is_string()) {
    if (const Json* min = json::find(schema, "minLength"); min && utf8::length(value.get_ref<const std::string&>()) < min->get<std::size_t>())
      return invalid(p, "string shorter than declared minimum");
  }
  if (value.is_array()) {
    if (const Json* min = json::find(schema, "minItems"); min && value.size() < min->get<std::size_t>())
      return invalid(p, "array shorter than declared minimum");
    if (const Json* items = json::find(schema, "items")) {
      for (std::size_t i = 0; i < value.size(); ++i) LOOM_TRY(validate_value(value[i], *items, p + "/" + std::to_string(i)));
    }
  }
  if (value.is_object()) {
    if (const Json* required = json::find(schema, "required")) {
      for (const auto& k : *required) if (!value.contains(k.get<std::string>())) return invalid(p + "/" + k.get<std::string>(), "required field is absent");
    }
    const Json* properties = json::find(schema, "properties");
    const Json* additional = json::find(schema, "additionalProperties");
    for (auto it = value.begin(); it != value.end(); ++it) {
      const Json* child = properties ? json::find(*properties, it.key()) : nullptr;
      if (child) LOOM_TRY(validate_value(it.value(), *child, p + "/" + escape_pointer(it.key())));
      else if (additional && additional->is_object()) LOOM_TRY(validate_value(it.value(), *additional, p + "/" + escape_pointer(it.key())));
      else if (additional && !additional->get<bool>()) return invalid(p + "/" + escape_pointer(it.key()), "unknown executable setting");
    }
  }
  return {};
}

void overlay(Json& base, const Json& changes) {
  if (!base.is_object() || !changes.is_object()) { base = changes; return; }
  for (auto it = changes.begin(); it != changes.end(); ++it) {
    if (base.contains(it.key())) overlay(base[it.key()], it.value());
    else base[it.key()] = it.value();
  }
}
}  // namespace

Result<RuntimeProfile> RuntimeProfile::from_definition(const Json& definition, const Json& overrides) {
  if (!definition.is_object() || json::get_string(definition, "schema") != "loom.runtime_profile/1")
    return invalid("/schema", "expected loom.runtime_profile/1");
  for (auto it = definition.begin(); it != definition.end(); ++it) {
    if (it.key() != "schema" && it.key() != "domain" && it.key() != "revision" &&
        it.key() != "defaults" && it.key() != "value_schema" && it.key() != "description" && it.key() != "title")
      return invalid("/" + escape_pointer(it.key()), "unknown profile descriptor field");
  }
  for (const char* key : {"description", "title"})
    if (const Json* n = json::find(definition, key); n && !n->is_string())
      return invalid(std::string("/") + key, "annotation must be a string");
  std::string domain = json::get_string(definition, "domain");
  if (!safe_domain(domain)) return invalid("/domain", "use lowercase identifier characters");
  const Json* revision = json::find(definition, "revision");
  if (!revision || !revision->is_number_integer() || revision->get<double>() < 1) return invalid("/revision", "expected a positive integer");
  const Json* defaults = json::find(definition, "defaults");
  const Json* schema = json::find(definition, "value_schema");
  if (!defaults || !defaults->is_object() || !schema) return invalid("", "defaults and value_schema are required");
  if (!overrides.is_object()) return invalid("/overrides", "expected a partial object");
  LOOM_TRY(validate_schema(*schema, "/value_schema"));
  LOOM_TRY(validate_value(*defaults, *schema, "/defaults"));
  RuntimeProfile out;
  out.definition_ = definition;
  out.values_ = *defaults;
  out.domain_ = std::move(domain);
  overlay(out.values_, overrides);
  LOOM_TRY(validate_value(out.values_, *schema, "/values"));
  out.hash_ = Sha256::hex(json::canonical(Json{{"definition", definition}, {"values", out.values_}}));
  return out;
}

Result<RuntimeProfile> RuntimeProfile::builtin(std::string_view domain) {
  if (!safe_domain(domain)) return invalid("/domain", "invalid domain");
  // Embedded definitions are immutable for this binary. Parse, validate and
  // hash once; user overlays are still read afresh by load(). Function-local
  // initialization also works when a consumer requests a preset at startup.
  static const auto profiles = [] {
    std::vector<std::pair<std::string_view, Result<RuntimeProfile>>> out;
    for (const auto& entry : kRuntimeProfiles) {
      auto definition = json::parse(entry.second);
      auto profile = definition ? from_definition(*definition)
                                : Result<RuntimeProfile>(definition.error());
      if (profile) {
        for (const auto& source : kRuntimeProfileSources) {
          if (source.first != entry.first) continue;
          auto metadata = json::parse(source.second);
          if (!metadata) profile = metadata.error();
          else profile->source_provenance_ = std::move(*metadata);
          break;
        }
      }
      out.emplace_back(entry.first, std::move(profile));
    }
    return out;
  }();
  for (const auto& entry : profiles) {
    if (entry.first == domain) return entry.second;
  }
  return Error(Errc::NotFound, "runtime profile domain is unavailable: " + std::string(domain));
}

Result<RuntimeProfile> RuntimeProfile::load(std::string_view domain, const std::filesystem::path& data_dir, const Json& overrides) {
  LOOM_TRY_ASSIGN(auto profile, builtin(domain));
  if (!data_dir.empty()) {
    const auto path = data_dir / "profiles" / (std::string(domain) + ".pack");
    std::error_code ec;
    auto status = std::filesystem::symlink_status(path, ec);
    if (ec && ec != std::errc::no_such_file_or_directory)
      return Error(Errc::Io, "cannot inspect runtime profile overlay: " + ec.message());
    if (status.type() != std::filesystem::file_type::not_found) {
      LOOM_TRY_ASSIGN(auto bytes, fsutil::read_file(path));
      if (!utf8::is_valid(bytes)) return invalid("/overlay", "invalid UTF-8");
      LOOM_TRY_ASSIGN(auto doc, json::parse(bytes));
      LOOM_TRY_ASSIGN(profile, profile.with_overlay(doc));
    }
  }
  return profile.with_overrides(overrides);
}

Result<RuntimeProfile> RuntimeProfile::with_overrides(const Json& overrides) const {
  if (!overrides.is_object()) return invalid("/overrides", "expected a partial object");
  if (overrides.empty()) return *this;
  Json changes = values_;
  overlay(changes, overrides);
  return with_values(changes);
}

Result<RuntimeProfile> RuntimeProfile::with_values(const Json& values) const {
  LOOM_TRY(validate_value(values, value_schema(), "/values"));
  RuntimeProfile out = *this;
  out.values_ = values;
  out.hash_ = Sha256::hex(json::canonical(Json{{"definition", definition_}, {"values", values}}));
  return out;
}

Result<RuntimeProfile> RuntimeProfile::with_patch(const Json& patch) const {
  if (!patch.is_array()) return invalid("/patch", "expected RFC 6902 operation array");
  try { return with_values(values_.patch(patch)); }
  catch (const Json::exception& e) { return invalid("/patch", e.what()); }
}

Result<RuntimeProfile> RuntimeProfile::with_overlay(const Json& doc) const {
  if (!doc.is_object() || json::get_string(doc, "schema") != "loom.runtime_profile_overlay/1" ||
      json::get_string(doc, "domain") != domain_)
    return invalid("/overlay", "schema/domain mismatch");
  for (auto it = doc.begin(); it != doc.end(); ++it) {
    if (it.key() != "schema" && it.key() != "domain" && it.key() != "overrides" && it.key() != "patch")
      return invalid("/overlay/" + escape_pointer(it.key()), "unknown overlay field");
  }
  const Json* changes = json::find(doc, "overrides");
  if (!changes) return invalid("/overlay/overrides", "required");
  LOOM_TRY_ASSIGN(auto out, with_overrides(*changes));
  if (const Json* patch = json::find(doc, "patch")) return out.with_patch(*patch);
  return out;
}

Json RuntimeProfile::inspection() const {
  return Json{{"schema", "loom.runtime_profile_effective/1"}, {"domain", domain_},
              {"revision", definition_["revision"]}, {"hash", hash_}, {"is_builtin", is_builtin()},
              {"values", values_}, {"value_schema", value_schema()}, {"definition", definition_},
              {"defaults", defaults()}, {"source_provenance", source_provenance_}};
}

bool RuntimeProfile::is_builtin() const noexcept { return exact_equal(values_, definition_.at("defaults")); }

std::vector<std::string> RuntimeProfile::domains() {
  std::vector<std::string> out;
  for (const auto& entry : kRuntimeProfiles) out.emplace_back(entry.first);
  return out;
}

Result<std::string> render_profile_template(std::string_view text, const Json& variables) {
  if (!variables.is_object()) return invalid("/template", "variables must be an object");
  std::string out;
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    auto start = text.find("{{", cursor);
    if (start == std::string_view::npos) { out += text.substr(cursor); break; }
    out += text.substr(cursor, start - cursor);
    auto end = text.find("}}", start + 2);
    if (end == std::string_view::npos) return invalid("/template", "unclosed substitution");
    std::string name(text.substr(start + 2, end - start - 2));
    const Json* value = nullptr;
    if (!name.empty() && name[0] == '/') {
      try { value = &variables.at(Json::json_pointer(name)); }
      catch (const Json::exception&) { return invalid("/template/" + name, "invalid or absent pointer"); }
    } else value = json::find(variables, name);
    if (!value) return invalid("/template/" + name, "variable is absent");
    out += value->is_string() ? value->get<std::string>() : json::dump(*value);
    cursor = end + 2;
  }
  return out;
}

}  // namespace loom
