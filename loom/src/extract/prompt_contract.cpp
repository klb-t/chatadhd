#include "extract/prompt_contract.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

#include "loom/re/regex.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace loom::extract::prompts {
namespace {
#include "extract/prompt_contract_data.inc"

const Json& builtins() {
  static const Json data = [] {
    auto parsed = json::parse(kBuiltinPromptData);
    return parsed ? parsed.value() : Json::object();
  }();
  return data;
}

Error invalid(std::string text) { return Error(Errc::InvalidArgument, std::move(text)); }

Status exact_keys(const Json& object, const std::set<std::string, std::less<>>& allowed,
                  std::string_view location) {
  if (!object.is_object()) return invalid(std::string(location) + ": expected an object");
  for (auto it = object.begin(); it != object.end(); ++it)
    if (!allowed.count(it.key())) return invalid(std::string(location) + ": unsupported field " + it.key());
  return {};
}

std::string pointer_part(std::string_view key) {
  std::string out;
  for (char c : key) {
    if (c == '~') out += "~0";
    else if (c == '/') out += "~1";
    else out += c;
  }
  return out;
}

void issue(Json& issues, std::string_view path, std::string_view schema_path,
           std::string_view code, std::string_view message) {
  issues.push_back(Json{{"path", path}, {"schema_path", schema_path}, {"code", code}, {"message", message}});
}

bool string_array(const Json& value) {
  if (!value.is_array()) return false;
  return std::all_of(value.begin(), value.end(), [](const Json& x) { return x.is_string(); });
}

int compare_number(const Json& a, const Json& b) {
  if (a.is_number_integer() && b.is_number_integer()) {
    const bool a_negative = !a.is_number_unsigned() && a.get<std::int64_t>() < 0;
    const bool b_negative = !b.is_number_unsigned() && b.get<std::int64_t>() < 0;
    if (a_negative != b_negative) return a_negative ? -1 : 1;
    if (a_negative) {
      const auto av = a.get<std::int64_t>(), bv = b.get<std::int64_t>();
      return (av > bv) - (av < bv);
    }
    const auto av = a.get<std::uint64_t>(), bv = b.get<std::uint64_t>();
    return (av > bv) - (av < bv);
  }
  const auto av = a.get<long double>(), bv = b.get<long double>();
  return (av > bv) - (av < bv);
}

std::uint64_t integer_magnitude(const Json& value) {
  if (value.is_number_unsigned()) return value.get<std::uint64_t>();
  const auto n = value.get<std::int64_t>();
  return n < 0 ? static_cast<std::uint64_t>(-(n + 1)) + 1 : static_cast<std::uint64_t>(n);
}

// JSON Schema equality ignores object insertion order and numeric storage
// type. This is distinct from request serialization, which preserves order.
bool equivalent(const Json& a, const Json& b) {
  if (a.is_number() && b.is_number()) return std::isfinite(a.get<long double>()) && std::isfinite(b.get<long double>()) && compare_number(a, b) == 0;
  if (a.type() != b.type()) return false;
  if (a.is_object()) {
    if (a.size() != b.size()) return false;
    for (auto it = a.begin(); it != a.end(); ++it) {
      const Json* other = json::find(b, it.key());
      if (!other || !equivalent(it.value(), *other)) return false;
    }
    return true;
  }
  if (a.is_array()) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (!equivalent(a[i], b[i])) return false;
    return true;
  }
  return a == b;
}

void schema_syntax(const Json& schema, const std::string& path, Json& issues) {
  if (schema.is_boolean()) return;
  if (!schema.is_object()) { issue(issues, "", path, "schema_shape", "schema must be an object or boolean"); return; }
  static const std::set<std::string, std::less<>> supported{
    "$schema", "$id", "$comment", "title", "description", "default", "examples", "deprecated", "readOnly", "writeOnly",
    "type", "required", "properties", "additionalProperties", "items", "enum", "const", "oneOf", "anyOf", "allOf", "not",
    "minLength", "maxLength", "pattern", "minimum", "maximum", "exclusiveMinimum", "exclusiveMaximum", "multipleOf",
    "minItems", "maxItems", "uniqueItems", "minProperties", "maxProperties"};
  static const std::set<std::string, std::less<>> types{"null", "boolean", "object", "array", "number", "integer", "string"};
  for (auto it = schema.begin(); it != schema.end(); ++it) {
    const std::string at = path + "/" + pointer_part(it.key());
    if (!supported.count(it.key())) {
      issue(issues, "", at, "unsupported_schema_keyword", "constraint is not supported by this validator"); continue;
    }
    if (it.key() == "type") {
      const Json names = it.value().is_array() ? it.value() : Json::array({it.value()});
      if (names.empty()) issue(issues, "", at, "schema_type", "type list must not be empty");
      for (const auto& name : names)
        if (!name.is_string() || !types.count(name.get<std::string>())) issue(issues, "", at, "schema_type", "unsupported type");
    } else if (it.key() == "required") {
      if (!string_array(it.value())) issue(issues, "", at, "schema_required", "required must be an array of strings");
    } else if (it.key() == "properties") {
      if (!it.value().is_object()) issue(issues, "", at, "schema_properties", "properties must be an object");
      else for (auto child = it.value().begin(); child != it.value().end(); ++child)
        schema_syntax(child.value(), at + "/" + pointer_part(child.key()), issues);
    } else if (it.key() == "items" || it.key() == "additionalProperties" || it.key() == "not") {
      schema_syntax(it.value(), at, issues);
    } else if (it.key() == "oneOf" || it.key() == "anyOf" || it.key() == "allOf") {
      if (!it.value().is_array() || it.value().empty()) issue(issues, "", at, "schema_combinator", "expected a nonempty schema array");
      else for (std::size_t i = 0; i < it.value().size(); ++i) schema_syntax(it.value()[i], at + "/" + std::to_string(i), issues);
    } else if (it.key() == "enum") {
      if (!it.value().is_array() || it.value().empty()) issue(issues, "", at, "schema_enum", "expected a nonempty enum array");
    } else if (it.key() == "pattern") {
      if (!it.value().is_string() || !re::Regex::compile(it.value().is_string() ? it.value().get<std::string>() : ""))
        issue(issues, "", at, "schema_pattern", "invalid or unsupported regular expression");
    } else if (it.key() == "uniqueItems") {
      if (!it.value().is_boolean()) issue(issues, "", at, "schema_boolean", "uniqueItems must be boolean");
    } else if (it.key() == "minimum" || it.key() == "maximum" || it.key() == "exclusiveMinimum" ||
               it.key() == "exclusiveMaximum" || it.key() == "multipleOf") {
      if (!it.value().is_number() || !std::isfinite(it.value().get<long double>()) ||
          (it.key() == "multipleOf" && it.value().get<long double>() <= 0))
        issue(issues, "", at, "schema_number", "expected a numeric bound, or positive multipleOf");
    } else if (it.key() == "minLength" || it.key() == "maxLength" || it.key() == "minItems" ||
               it.key() == "maxItems" || it.key() == "minProperties" || it.key() == "maxProperties") {
      if (!it.value().is_number_integer() || it.value().get<double>() < 0)
        issue(issues, "", at, "schema_nonnegative_integer", "expected a nonnegative integer");
    }
  }
}

bool has_type(const Json& value, const std::string& type) {
  if (type == "null") return value.is_null();
  if (type == "boolean") return value.is_boolean();
  if (type == "object") return value.is_object();
  if (type == "array") return value.is_array();
  if (type == "number") return value.is_number();
  if (type == "integer") return value.is_number_integer() || (value.is_number_float() && std::floor(value.get<double>()) == value.get<double>());
  if (type == "string") return value.is_string();
  return false;
}

void check_value(const Json& schema, const Json& value, const std::string& path,
                 const std::string& schema_path, Json& issues) {
  if (schema.is_boolean()) {
    if (!schema.get<bool>()) issue(issues, path, schema_path, "false_schema", "value is not admitted by this schema");
    return;
  }
  if (!schema.is_object()) return;  // syntax report covers this
  if (const Json* types = json::find(schema, "type")) {
    const Json names = types->is_array() ? *types : Json::array({*types});
    bool matched = false;
    for (const auto& type : names) if (type.is_string()) matched = matched || has_type(value, type.get<std::string>());
    if (!matched) issue(issues, path, schema_path + "/type", "type", "value has a different JSON type");
  }
  if (const Json* constant = json::find(schema, "const"); constant && !equivalent(value, *constant))
    issue(issues, path, schema_path + "/const", "const", "value differs from the required constant");
  if (const Json* values = json::find(schema, "enum"); values && values->is_array() &&
      !std::any_of(values->begin(), values->end(), [&](const Json& item) { return equivalent(value, item); }))
    issue(issues, path, schema_path + "/enum", "enum", "value is not in the allowed enumeration");
  for (const char* key : {"allOf", "anyOf", "oneOf"}) {
    const Json* branches = json::find(schema, key);
    if (!branches || !branches->is_array()) continue;
    std::size_t matches = 0;
    for (std::size_t i = 0; i < branches->size(); ++i) {
      Json branch_issues = Json::array();
      check_value((*branches)[i], value, path, schema_path + "/" + key + "/" + std::to_string(i), branch_issues);
      matches += branch_issues.empty();
      if (std::string_view(key) == "allOf") for (auto& x : branch_issues) issues.push_back(std::move(x));
    }
    if ((std::string_view(key) == "anyOf" && matches == 0) || (std::string_view(key) == "oneOf" && matches != 1))
      issue(issues, path, schema_path + "/" + key, key, "no admissible schema branch or ambiguous oneOf");
  }
  if (const Json* inverse = json::find(schema, "not")) {
    Json inverse_issues = Json::array();
    check_value(*inverse, value, path, schema_path + "/not", inverse_issues);
    if (inverse_issues.empty()) issue(issues, path, schema_path + "/not", "not", "value matches a forbidden schema");
  }
  if (value.is_object()) {
    if (const Json* required = json::find(schema, "required"); required && required->is_array())
      for (const auto& name : *required) if (name.is_string() && !value.contains(name.get<std::string>()))
        issue(issues, path, schema_path + "/required", "required", "missing property " + name.get<std::string>());
    const Json* properties = json::find(schema, "properties");
    const Json* additional = json::find(schema, "additionalProperties");
    for (auto it = value.begin(); it != value.end(); ++it) {
      const Json* child = properties && properties->is_object() ? json::find(*properties, it.key()) : nullptr;
      if (child) check_value(*child, it.value(), path + "/" + pointer_part(it.key()), schema_path + "/properties/" + pointer_part(it.key()), issues);
      else if (additional) check_value(*additional, it.value(), path + "/" + pointer_part(it.key()), schema_path + "/additionalProperties", issues);
    }
  }
  if (value.is_array()) {
    if (const Json* items = json::find(schema, "items"))
      for (std::size_t i = 0; i < value.size(); ++i) check_value(*items, value[i], path + "/" + std::to_string(i), schema_path + "/items", issues);
    if (json::get_bool(schema, "uniqueItems")) {
      bool duplicate = false;
      for (std::size_t i = 0; i < value.size() && !duplicate; ++i)
        for (std::size_t j = 0; j < i; ++j) if (equivalent(value[i], value[j])) { duplicate = true; break; }
      if (duplicate) issue(issues, path, schema_path + "/uniqueItems", "uniqueItems", "array contains duplicate values");
    }
  }
  if (value.is_string()) {
    if (const Json* pattern = json::find(schema, "pattern"); pattern && pattern->is_string()) {
      auto compiled = re::Regex::compile(pattern->get<std::string>());
      if (compiled && !compiled->search_utf8(value.get<std::string>())) issue(issues, path, schema_path + "/pattern", "pattern", "string does not match pattern");
    }
  }
  for (const char* key : {"minLength", "maxLength", "minItems", "maxItems", "minProperties", "maxProperties"}) {
    const Json* bound = json::find(schema, key);
    if (!bound || !bound->is_number_integer()) continue;
    std::optional<std::size_t> size;
    if ((std::string_view(key) == "minLength" || std::string_view(key) == "maxLength") && value.is_string()) size = utf8::length(value.get<std::string>());
    if ((std::string_view(key) == "minItems" || std::string_view(key) == "maxItems") && value.is_array()) size = value.size();
    if ((std::string_view(key) == "minProperties" || std::string_view(key) == "maxProperties") && value.is_object()) size = value.size();
    if (size && ((std::string_view(key).substr(0, 3) == "min" && static_cast<double>(*size) < bound->get<double>()) ||
                 (std::string_view(key).substr(0, 3) == "max" && static_cast<double>(*size) > bound->get<double>())))
      issue(issues, path, schema_path + "/" + key, key, "value size is outside the configured bound");
  }
  if (value.is_number()) {
    const long double number = value.get<long double>();
    for (const char* key : {"minimum", "maximum", "exclusiveMinimum", "exclusiveMaximum", "multipleOf"}) {
      const Json* bound = json::find(schema, key);
      if (!bound || !bound->is_number()) continue;
      const long double n = bound->get<long double>();
      const std::string_view k(key);
      const auto comparison = compare_number(value, *bound);
      bool failed = (k == "minimum" && comparison < 0) || (k == "maximum" && comparison > 0) ||
                    (k == "exclusiveMinimum" && comparison <= 0) || (k == "exclusiveMaximum" && comparison >= 0);
      if (k == "multipleOf" && n > 0) {
        if (value.is_number_integer() && bound->is_number_integer()) failed = integer_magnitude(value) % bound->get<std::uint64_t>() != 0;
        else {
          const auto remainder = std::abs(std::fmod(number, n));
          // Decimal literals enter Json as binary64; tolerate their last-bit
          // conversion error without scaling by the size of the quotient.
          failed = std::min(remainder, n - remainder) > std::numeric_limits<double>::epsilon() * n * 8;
        }
      }
      if (failed) issue(issues, path, schema_path + "/" + key, key, "number is outside the configured constraint");
    }
  }
}

Status contract_syntax(const Json& definition) {
  LOOM_TRY(exact_keys(definition, {"schema", "id", "version", "messages", "request_parameters", "transport", "analysis_parameters", "output_schema", "validation_mode", "description", "title", "extensions"}, "prompt contract"));
  if (json::get_string(definition, "schema") != "loom.analysis_prompt/1" || json::get_string(definition, "id").empty() ||
      !definition.contains("version") || !definition["version"].is_number_integer() || definition["version"].get<double>() < 0)
    return invalid("prompt contract: schema/id/version is missing or invalid");
  const Json* messages = json::find(definition, "messages");
  if (!messages || !messages->is_array()) return invalid("prompt contract.messages: expected an array");
  for (const auto& message : *messages) {
    LOOM_TRY(exact_keys(message, {"role", "content"}, "prompt contract.messages"));
    if (json::get_string(message, "role").empty() || !message.contains("content") || !message["content"].is_array())
      return invalid("prompt contract.messages: expected a role and content parts");
    for (const auto& part : message["content"]) {
      if (part.is_string()) continue;
      LOOM_TRY(exact_keys(part, {"binding", "max_codepoints"}, "prompt content binding"));
      if (json::get_string(part, "binding").empty()) return invalid("prompt content binding: missing binding name");
      if (const Json* maximum = json::find(part, "max_codepoints"); maximum && !maximum->is_null() &&
          (!maximum->is_number_integer() || maximum->get<double>() < 0)) return invalid("max_codepoints must be nonnegative or null");
    }
  }
  for (const char* field : {"request_parameters", "transport", "analysis_parameters"})
    if (!definition.contains(field) || !definition[field].is_object()) return invalid(std::string("prompt contract.") + field + ": expected an object");
  const Json& transport = definition["transport"];
  LOOM_TRY(exact_keys(transport, {"method", "path", "url", "headers", "timeout_ms", "stream", "body_field_order"}, "prompt transport"));
  for (const char* key : {"method", "path", "url"})
    if (transport.contains(key) && !transport[key].is_string()) return invalid(std::string("prompt transport.") + key + ": expected a string");
  if (transport.contains("headers")) {
    if (!transport["headers"].is_object()) return invalid("prompt transport.headers: expected an object");
    for (const auto& value : transport["headers"]) if (!value.is_string()) return invalid("prompt transport.headers: values must be strings");
  }
  if (const Json* timeout = json::find(transport, "timeout_ms"); timeout &&
      (!timeout->is_number_integer() || timeout->get<double>() < 0 || timeout->get<double>() > std::numeric_limits<int>::max()))
    return invalid("prompt transport.timeout_ms: outside the native int representation");
  if (transport.contains("stream") && !transport["stream"].is_boolean()) return invalid("prompt transport.stream: expected boolean");
  if (const Json* order = json::find(transport, "body_field_order")) {
    if (!string_array(*order)) return invalid("prompt transport.body_field_order: expected an array of strings");
    std::set<std::string> names;
    for (const auto& key : *order)
      if (!names.insert(key.get<std::string>()).second) return invalid("prompt transport.body_field_order: duplicate field");
  }
  const std::string mode = json::get_string(definition, "validation_mode");
  if (mode != "strict" && mode != "lenient" && mode != "off") return invalid("prompt validation_mode must be strict, lenient or off");
  if (!definition.contains("output_schema") || (!definition["output_schema"].is_object() && !definition["output_schema"].is_boolean()))
    return invalid("prompt output_schema must be an object or boolean");
  if (definition.contains("preserve_unknown_fields") && !definition["preserve_unknown_fields"].is_boolean())
    return invalid("preserve_unknown_fields must be boolean");
  if (mode == "strict") {
    Json problems = Json::array();
    schema_syntax(definition["output_schema"], "/output_schema", problems);
    if (!problems.empty()) return invalid("unsupported or malformed output schema: " + json::canonical(problems));
  }
  return {};
}

}  // namespace

Json overlay(const Json& base, const Json& patch) {
  if (!base.is_object() || !patch.is_object()) return patch;
  Json result = base;
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    if (result.contains(it.key()) && result[it.key()].is_object() && it.value().is_object()) result[it.key()] = overlay(result[it.key()], it.value());
    else result[it.key()] = it.value();
  }
  return result;
}

Result<Contract> from_snapshot(const Json& definition) {
  LOOM_TRY(contract_syntax(definition));
  return Contract{definition, Sha256::hex(json::canonical(definition))};
}

std::vector<std::string> builtin_ids() {
  std::vector<std::string> ids;
  for (auto it = builtins().begin(); it != builtins().end(); ++it) ids.push_back(it.key());
  return ids;
}

Json builtin_catalog() {
  Json result = Json::array();
  for (auto it = builtins().begin(); it != builtins().end(); ++it)
    result.push_back(Json{{"id", it.key()}, {"file", it.value()["file"]}, {"version", it.value()["definition"]["version"]},
                          {"contract_hash", Sha256::hex(json::canonical(it.value()["definition"]))}, {"source_hash", it.value()["source_hash"]}});
  return result;
}

Result<Contract> resolve(std::string_view id, const std::filesystem::path& overlay_dir, const Json& patch) {
  if (!patch.is_object()) return invalid("prompt override must be an object");
  Json definition;
  if (builtins().contains(std::string(id))) definition = builtins()[std::string(id)]["definition"];
  std::error_code ec;
  const bool exists = !overlay_dir.empty() && std::filesystem::exists(overlay_dir, ec);
  if (ec) return Error(Errc::Io, "cannot inspect prompt overlay: " + ec.message());
  if (exists) {
    if (!std::filesystem::is_directory(overlay_dir, ec) || ec) return invalid("prompt overlay path must be a directory");
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(overlay_dir, ec), end; !ec && it != end; it.increment(ec))
      if (it->is_regular_file() && it->path().extension() == ".prompt") files.push_back(it->path());
    if (ec) return Error(Errc::Io, "cannot enumerate prompt overlay: " + ec.message());
    std::sort(files.begin(), files.end());
    std::set<std::string> found;
    for (const auto& file : files) {
      std::ifstream input(file, std::ios::binary);
      if (!input) return Error(Errc::Io, "cannot read prompt overlay: " + file.string());
      std::ostringstream raw; raw << input.rdbuf();
      auto parsed = json::parse(raw.str());
      if (!parsed || !parsed->is_object() || json::get_string(*parsed, "id").empty())
        return invalid("invalid prompt overlay " + file.string() + (parsed ? ": missing object/id" : ": " + parsed.error().message));
      const std::string overlay_id = json::get_string(*parsed, "id");
      if (!found.insert(overlay_id).second) return invalid("duplicate prompt overlay id: " + overlay_id);
      if (overlay_id == id) definition = definition.is_object() ? overlay(definition, *parsed) : *parsed;
    }
  }
  if (!definition.is_object()) return Error(Errc::NotFound, "unknown prompt contract: " + std::string(id));
  definition = overlay(definition, patch);
  if (json::get_string(definition, "id") != id) return invalid("prompt override changed the selected contract id");
  return from_snapshot(definition);
}

Result<Json> prepare_request(const Contract& contract, std::string_view model, std::string_view provider,
                             const Json& bindings, const Json& request_patch) {
  LOOM_TRY(contract_syntax(contract.definition));
  if (contract.hash != Sha256::hex(json::canonical(contract.definition)))
    return Error(Errc::Conflict, "prompt contract snapshot and hash disagree");
  if (!bindings.is_object() || !request_patch.is_object()) return invalid("bindings and request override must be objects");
  Json body = overlay(Json{{"model", std::string(model)}}, contract.definition["request_parameters"]);
  Json messages = Json::array();
  // Fully supplied message arrays do not require any unused template binding.
  if (!body.contains("messages") && !request_patch.contains("messages")) for (const auto& message : contract.definition["messages"]) {
    std::string text;
    for (const auto& part : message["content"]) {
      if (part.is_string()) { text += part.get<std::string>(); continue; }
      const std::string binding = json::get_string(part, "binding");
      const Json* value = json::find(bindings, binding);
      if (!value) return invalid("missing prompt binding: " + binding);
      std::string rendered = binding == "input_json" || !value->is_string() ? json::canonical(*value) : value->get<std::string>();
      if (const Json* maximum = json::find(part, "max_codepoints"); maximum && !maximum->is_null()) {
        const auto cap = maximum->get<std::uint64_t>();
        if (cap < utf8::length(rendered)) rendered.resize(utf8::byte_offset(rendered, static_cast<std::size_t>(cap)));
      }
      text += rendered;
    }
    messages.push_back(Json{{"role", message["role"]}, {"content", std::move(text)}});
  }
  // ordered_json preserves insertion order. Keep the historical wire order:
  // model, decoding parameters, messages. Explicit messages in request data
  // remain a caller override, rather than being replaced by the renderer.
  if (!body.contains("messages") && !request_patch.contains("messages")) body["messages"] = std::move(messages);
  body = overlay(body, request_patch);
  const Json& transport = contract.definition["transport"];
  if (const Json* order = json::find(transport, "body_field_order")) {
    Json ordered = Json::object();
    for (const auto& key : *order) {
      const auto name = key.get<std::string>();
      if (body.contains(name)) ordered[name] = body[name];
    }
    for (auto it = body.begin(); it != body.end(); ++it)
      if (!ordered.contains(it.key())) ordered[it.key()] = it.value();
    body = std::move(ordered);
  }
  std::string base(provider);
  while (!base.empty() && base.back() == '/') base.pop_back();
  const std::string url = json::get_string(transport, "url", base + json::get_string(transport, "path", "/chat/completions"));
  Json headers = transport.value("headers", Json::object());
  const std::string bytes = json::dump(body);
  Json request{{"method", json::get_string(transport, "method", "POST")}, {"url", url}, {"headers", headers},
               {"body_json", body}, {"body_bytes", bytes}, {"transport", transport},
               {"contract_hash", contract.hash}, {"output_schema_hash", Sha256::hex(json::canonical(contract.definition["output_schema"]))}};
  request["request_hash"] = Sha256::hex(json::canonical(Json{{"method", request["method"]}, {"url", url}, {"headers", headers},
      {"body_bytes", bytes}, {"transport", transport}, {"validation_mode", contract.definition["validation_mode"]},
      {"output_schema_hash", request["output_schema_hash"]}}));
  return request;
}

Json validate_output(const Contract& contract, const Json& output) {
  const std::string mode = json::get_string(contract.definition, "validation_mode");
  if (mode == "off") return Json{{"mode", mode}, {"checked", false}, {"valid", true}, {"schema_valid", nullptr}, {"issues", Json::array()}};
  Json issues = Json::array();
  const Json* schema = json::find(contract.definition, "output_schema");
  if (schema) {
    schema_syntax(*schema, "/output_schema", issues);
    if (issues.empty()) check_value(*schema, output, "", "/output_schema", issues);
  } else issue(issues, "", "/output_schema", "schema_missing", "output schema is absent");
  for (auto& row : issues) row["severity"] = mode == "lenient" ? "warning" : "error";
  const bool schema_valid = issues.empty();
  return Json{{"mode", mode}, {"checked", true}, {"valid", mode == "lenient" || schema_valid},
              {"schema_valid", schema_valid}, {"issues", issues}};
}

}  // namespace loom::extract::prompts
