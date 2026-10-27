// Platform table loaded from the compiled import preset (see loom/platforms.h).
#include "loom/platforms.h"

#include <regex>
#include <stdexcept>

#include "import/import_preset.h"

namespace loom {
namespace {

std::string text_of(const Json& o, const char* key) {
  const Json* v = json::find(o, key);
  return v && v->is_string() ? v->get<std::string>() : std::string();
}

std::vector<PlatformSpec> load() {
  const Json& table = import_preset_platforms();
  std::vector<PlatformSpec> out;
  if (table.is_null()) return out;
  if (!table.is_array()) throw std::runtime_error("import preset platforms must be an array");
  for (const auto& e : table) {
    if (!e.is_object()) throw std::runtime_error("import preset platform must be an object");
    PlatformSpec p;
    p.id = text_of(e, "id");
    if (p.id.empty()) throw std::runtime_error("import preset platform needs an id");
    p.vendor = text_of(e, "vendor");
    p.provider = text_of(e, "provider");
    p.unit_kind = text_of(e, "unit_kind");
    p.walker = text_of(e, "walker");
    p.id_field = text_of(e, "id_field");
    p.id_literal = text_of(e, "id_literal");
    p.shard_prefix = text_of(e, "shard_prefix");
    p.shard_pattern = text_of(e, "shard_pattern");
    if (const Json* m = json::find(e, "match"); m && m->is_array()) {
      for (const auto& group : *m) {
        std::vector<std::string> keys;
        if (group.is_array())
          for (const auto& k : group)
            if (k.is_string()) keys.push_back(k.get<std::string>());
        p.match.push_back(std::move(keys));
      }
    }
    if (const Json* s = json::find(e, "shape"); s && s->is_object())
      for (auto it = s->begin(); it != s->end(); ++it)
        if (it.value().is_string()) p.shape[it.key()] = it.value().get<std::string>();
    out.push_back(std::move(p));
  }
  return out;
}

}  // namespace

bool PlatformSpec::matches(const Json& element) const {
  if (!element.is_object() || match.empty()) return false;
  for (const auto& group : match) {
    bool any = false;
    for (const auto& key : group)
      if (json::find(element, key)) { any = true; break; }
    if (!any) return false;
  }
  return true;
}

bool PlatformSpec::shape_ok(const Json& element) const {
  if (!element.is_object()) return false;
  for (const auto& [key, type] : shape) {
    const Json* v = json::find(element, key);
    if (!v) return false;
    if (type == "object" && !v->is_object()) return false;
    if (type == "array" && !v->is_array()) return false;
  }
  return true;
}

std::string PlatformSpec::ext_id(const Json& element) const {
  if (!id_literal.empty()) return id_literal;
  if (id_field.empty()) return "";
  return json::get_string(element, id_field);
}

const std::vector<PlatformSpec>& import_platforms() {
  static const std::vector<PlatformSpec> table = load();
  return table;
}

const PlatformSpec* find_platform(std::string_view id) {
  for (const auto& p : import_platforms())
    if (p.id == id) return &p;
  return nullptr;
}

const PlatformSpec* sniff_platform(const Json& element) {
  for (const auto& p : import_platforms())
    if (p.matches(element)) return &p;
  return nullptr;
}

const PlatformSpec* platform_for_unit(std::string_view unit_kind, std::string_view vendor) {
  const PlatformSpec* fallback = nullptr;
  for (const auto& p : import_platforms()) {
    if (p.unit_kind != unit_kind) continue;
    if (p.vendor == vendor) return &p;
    fallback = &p;  // unknown vendor: the last platform of that unit kind
  }
  return fallback;
}

const PlatformSpec* platform_for_shard(std::string_view base) {
  for (const auto& p : import_platforms())
    if (!p.shard_prefix.empty() && base.rfind(p.shard_prefix, 0) == 0) {
      if (p.shard_pattern.empty()) return &p;
      try {
        if (std::regex_search(std::string(base), std::regex(p.shard_pattern))) return &p;
      } catch (const std::regex_error&) {
        return &p;  // an invalid pattern in data falls back to the prefix alone
      }
    }
  return nullptr;
}

const PlatformSpec* platform_for_provider(std::string_view provider) {
  for (const auto& p : import_platforms())
    if (p.provider == provider) return &p;
  return nullptr;
}

}  // namespace loom
