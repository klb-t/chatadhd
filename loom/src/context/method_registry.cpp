#include "method_registry.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

#include "loom/db.h"
#include "loom/graph_packet_store.h"
#include "loom/knowledge_store.h"
#include "loom/model.h"
#include "loom/util/sha256.h"

namespace loom::context {
namespace {

Error invalid(std::string text) { return {Errc::InvalidArgument, "method registry: " + text}; }
Error unavailable(std::string text) { return {Errc::Unavailable, "method registry: " + text}; }
Error conflict(std::string text) { return {Errc::Conflict, "method registry: " + text}; }
std::string hash(const Json& j) { return Sha256::hex(json::canonical(j)); }
bool digest(const Json& j) {
  if (!j.is_string()) return false;
  const auto& s = j.get_ref<const std::string&>();
  return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}
Result<std::string> text(const Json& j, const char* key) {
  if (!j.is_object() || !j.contains(key) || !j[key].is_string() || j[key].get_ref<const std::string&>().empty())
    return invalid(std::string("nonempty string required: ") + key);
  return j[key].get<std::string>();
}
Json merge(Json into, const Json& overlay) {
  if (!into.is_object() || !overlay.is_object()) return overlay;
  for (auto i = overlay.begin(); i != overlay.end(); ++i) {
    if (into.contains(i.key())) into[i.key()] = merge(into[i.key()], i.value());
    else into[i.key()] = i.value();
  }
  return into;
}
Status finite(const Json& j) {
  if (j.is_number_float() && !std::isfinite(j.get<double>())) return invalid("nonfinite JSON number");
  if (j.is_structured()) for (const auto& v : j) LOOM_TRY(finite(v));
  return {};
}
Status measurements(const Json& j) {
  if (j.is_null()) return {};
  if (!j.is_object()) return invalid("measurements must be a map of measured numbers or null");
  for (const auto& v : j) if (!(v.is_number() || v.is_null())) return invalid("measurement must be numeric or explicitly unknown/null");
  return finite(j);
}
Result<std::string> role(const Json& v, const char* group, const char* name) {
  if (!v.is_object() || !v.contains(group)) return unavailable(std::string("vocabulary missing ") + group);
  if (v[group].is_object() && !v[group].contains(name)) return unavailable(std::string("vocabulary missing ") + group + "." + name);
  return text(v[group], name);
}
Status vocabulary(const Json& v) {
  for (const char* name : {"method", "method_version", "run"}) { LOOM_TRY(role(v, "kinds", name)); }
  for (const char* name : {"version_of", "requests_method_version", "produced_in_run", "produced_by_method_version"}) {
    LOOM_TRY(role(v, "predicates", name));
  }
  for (const char* group : {"kinds", "predicates"}) {
    if (!v[group].is_object()) return invalid("vocabulary group must be an object");
    std::set<std::string> seen;
    for (const auto& item : v[group]) {
      if (!item.is_string() || item.get_ref<const std::string&>().empty()) return invalid("vocabulary values must be nonempty strings");
      if (!seen.insert(item.get<std::string>()).second) return invalid("ambiguous vocabulary role values");
    }
  }
  return {};
}
std::string entity_role(const Json& e, const Json& v) {
  for (auto i = v["kinds"].begin(); i != v["kinds"].end(); ++i) if (e["kind"] == i.value()) return i.key();
  return {};
}
Status immutable(const Json& e, const Json& v) {
  const auto r = entity_role(e, v);
  const auto& a = e["attrs"];
  if (r == "prompt_version") {
    if (!a.contains("text") || !a["text"].is_string() || !digest(a.value("text_sha256", Json())) ||
        a["text_sha256"] != Sha256::hex(a["text"].get<std::string>())) return conflict("prompt bytes/hash drift " + e["id"].get<std::string>());
  } else if (r == "method_version" || r == "recipe_version" || r == "preset_version" || r == "combination_version" || r == "parameter_set_version") {
    if (!a.contains("definition") || !a["definition"].is_object() || !digest(a.value("definition_sha256", Json())) ||
        a["definition_sha256"] != hash(a["definition"])) return conflict("immutable definition/hash drift " + e["id"].get<std::string>());
    if (r == "parameter_set_version" && (!a["definition"].contains("effective_parameters") || !a["definition"].contains("user_overrides")))
      return invalid("parameter-set definition must capture effective_parameters and user_overrides");
  }
  return {};
}
Status immutable_attrs(const Json& old, const Json& current) {
  for (const char* key : {"definition", "definition_sha256", "text", "text_sha256"}) {
    const bool protected_field = std::string_view(key).starts_with("definition") ? old.contains("definition_sha256") : old.contains("text_sha256");
    if (protected_field && (!old.contains(key) || !current.contains(key) || old[key] != current[key]))
      return conflict("immutable version definition/prompt bytes changed or stripped");
  }
  return {};
}
Status put(Json& index, const Json& row, bool source, bool overlay, const Json& v) {
  const auto& native = source ? row.at("observation") : row;
  LOOM_TRY_ASSIGN(auto id, text(native, "id"));
  if (index.contains(id)) {
    const auto& old = index[id];
    if (!overlay && old != row) return conflict("duplicate identity " + id);
    if (source && old != row) return conflict("immutable source identity changed " + id);
    if (!source && row.contains("attrs")) {
      const auto r = entity_role(row, v);
      if (r.ends_with("_version")) LOOM_TRY(immutable_attrs(old["attrs"], row["attrs"]));
    }
  }
  index[id] = row;
  return {};
}
Status dto(const Json& row, const char* collection) {
  if (std::string_view(collection) == "entities") {
    LOOM_TRY_ASSIGN(auto v, model::Entity::from_json(row));
    if (v.to_json() != row) return invalid("entity DTO is not lossless");
  } else if (std::string_view(collection) == "claims") {
    LOOM_TRY_ASSIGN(auto v, model::Claim::from_json(row));
    LOOM_TRY(v.validate());
    if (v.to_json() != row) return invalid("claim DTO is not lossless");
  } else {
    if (!row.is_object() || row.size() != 3 || !row.contains("observation") || !row.contains("known_at") || !row.contains("text_sha256"))
      return invalid("source DTO fields");
    LOOM_TRY_ASSIGN(auto v, model::Observation::from_json(row["observation"]));
    if (v.to_json() != row["observation"] || !digest(row["text_sha256"]) || row["text_sha256"] != Sha256::hex(v.text))
      return conflict("source bytes/hash drift");
  }
  return {};
}
bool active(const Json& e) { return e.value("status", "") == "active"; }
bool active_claim(const Json& c) { return c["assessment"].value("status", "") == "active"; }
Result<Json> linked(const Json& s, const std::string& subject, const char* predicate_role, bool required = false) {
  auto p = role(s["vocabulary"], "predicates", predicate_role);
  if (!p) { if (required) return p.error(); return Json(nullptr); }
  std::set<std::string> ids;
  for (const auto& c : s["claims"]) if (c["subject"] == subject && c["predicate"] == *p && active_claim(c)) {
    if (!c["object"].is_string() || c["object"].get_ref<const std::string&>().empty()) return invalid("structural edge needs an entity object");
    ids.insert(c["object"].get<std::string>());
  }
  if (ids.size() > 1) return conflict("ambiguous " + std::string(predicate_role) + " for " + subject);
  if (ids.empty()) { if (required) return unavailable("missing active " + std::string(predicate_role) + " edge for " + subject); return Json(nullptr); }
  const auto& id = *ids.begin();
  if (!s["entities"].contains(id) || !active(s["entities"][id])) return unavailable("referenced entity missing/inactive " + id);
  return s["entities"][id];
}
bool edge(const Json& s, const std::string& subject, const std::string& object, const char* r) {
  auto p = role(s["vocabulary"], "predicates", r);
  if (!p) return false;
  for (const auto& c : s["claims"]) if (c["subject"] == subject && c["object"] == object && c["predicate"] == *p && active_claim(c)) return true;
  return false;
}
Status closure(const Json& s) {
  for (const auto& e : s["entities"]) {
    if (e["parent"] != "" && !s["entities"].contains(e["parent"].get<std::string>())) return invalid("missing native parent");
    LOOM_TRY(immutable(e, s["vocabulary"]));
  }
  for (const auto& c : s["claims"]) {
    if (!s["entities"].contains(c["subject"].get<std::string>()) ||
        (c["object"] != "" && !s["entities"].contains(c["object"].get<std::string>()))) return invalid("dangling native Claim endpoint");
    for (const char* name : {"premises", "counter", "consequences"}) for (const auto& id : c["assessment"][name]["claims"])
      if (!s["claims"].contains(id.get<std::string>())) return invalid("missing native Claim dependency");
    for (const auto& id : c["assessment"]["counter"]["observations"])
      if (!s["sources"].contains(id.get<std::string>())) return invalid("missing native counter Observation");
    for (const auto& support : c["assessment"]["basis"]["support"]) {
      const auto id = support["observation"].get<std::string>();
      if (!s["sources"].contains(id)) return invalid("missing native support source");
      const auto& o = s["sources"][id]["observation"];
      const auto raw = o["text"].get<std::string>();
      const auto quote = support["quote"].get<std::string>();
      if (quote.empty() || raw.find(quote) == std::string::npos) return invalid("support quote does not match native source");
      const auto& loc = support["locator"];
      const auto& original = o["locator"];
      if (loc != original) {
        for (const char* key : {"source", "member", "json_pointer", "time_start", "time_end"})
          if (loc[key] != original[key]) return invalid("support locator does not address native source");
        if (!loc["byte_start"].is_number_integer() || !loc["byte_len"].is_number_integer() || !original["byte_start"].is_number_integer())
          return invalid("native support subspan unverifiable");
        const auto start = loc["byte_start"].get<std::int64_t>(), base = original["byte_start"].get<std::int64_t>(), length = loc["byte_len"].get<std::int64_t>();
        if (start < base || base < 0 || length < 0 || static_cast<std::uint64_t>(start - base) > raw.size() ||
            static_cast<std::uint64_t>(length) > raw.size() - static_cast<std::size_t>(start - base) ||
            raw.substr(static_cast<std::size_t>(start - base), static_cast<std::size_t>(length)) != quote)
          return invalid("native support subspan quote mismatch");
      }
    }
  }
  return {};
}
Json values(const Json& object) {
  Json a = Json::array();
  for (const auto& v : object) a.push_back(v);
  return a;
}
Json definition_source_status(const Json& s) {
  Json out = Json::object();
  for (const auto& e : s["entities"]) {
    const auto r = entity_role(e, s["vocabulary"]);
    if (!r.ends_with("_version")) continue;
    const auto& a = e["attrs"];
    std::string bytes;
    if (r == "prompt_version") bytes = a["text"].get<std::string>();
    else if (a.contains("definition")) bytes = json::canonical(a["definition"]);
    else continue;
    Json ids = Json::array();
    for (const auto& source : s["sources"]) if (source["observation"]["text"] == bytes) ids.push_back(source["observation"]["id"]);
    out[e["id"].get<std::string>()] = Json{{"status", ids.empty() ? "exact_definition_source_unavailable" : "exact_bytes_recorded"}, {"source_ids", ids}};
  }
  return out;
}
Json capability(const Json& c, const char* group, const std::string& id) {
  if (!c.is_object() || !c.contains(group) || !c[group].is_object() || !c[group].contains(id))
    return Json{{"available", false}, {"reason", "execution_capability_not_advertised"}};
  Json out = c[group][id];
  if (!out.is_object() || !out.contains("available") || !out["available"].is_boolean())
    return Json{{"available", false}, {"reason", "invalid_capability_descriptor"}};
  return out;
}
std::string fusion_key(const Json& f) {
  if (f.is_string()) return f.get<std::string>();
  if (f.is_object() && f.contains("operation") && f["operation"].is_string()) return f["operation"].get<std::string>();
  return {};
}
Result<Json> expand(const Json& s, const Json& selection, const Json& c, const Json& member,
                    Json path, Json combination_parameters, Json member_parameters,
                    Json fusions, double weight, std::size_t member_index, std::set<std::string>& ancestors) {
  if (!member.is_object()) return invalid("selection member must be an object");
  LOOM_TRY(finite(member));
  if (member.contains("weight")) {
    if (!member["weight"].is_number()) return invalid("weight must be finite numeric data");
    weight *= member["weight"].get<double>();
    if (!std::isfinite(weight)) return invalid("combination weight representation overflow");
  }
  if (member.contains("parameters")) {
    member_parameters = merge(std::move(member_parameters), member["parameters"]);
  }
  const bool combo = member.contains("combination_version_id");
  if (combo == member.contains("method_version_id")) return invalid("member needs exactly one version role");
  LOOM_TRY_ASSIGN(auto id, text(member, combo ? "combination_version_id" : "method_version_id"));
  if (!s["entities"].contains(id) || !active(s["entities"][id])) return unavailable("selected version missing/inactive " + id);
  const auto& e = s["entities"][id];
  if (entity_role(e, s["vocabulary"]) != (combo ? "combination_version" : "method_version")) return invalid("selected version has wrong native kind");
  const auto& d = e["attrs"]["definition"];
  path.push_back(Json{{"id", id}, {"member", member}, {"member_index", member_index}, {"definition_sha256", e["attrs"]["definition_sha256"]}});
  if (combo) {
    if (!ancestors.insert(id).second) return invalid("cyclic method combination " + id);
    if (!d.contains("members") || !d["members"].is_array()) return unavailable("combination members data missing");
    if (d.contains("parameters")) {
      combination_parameters = merge(std::move(combination_parameters), d["parameters"]);
    }
    Json f = d.value("fusion", d.value("composition", Json(nullptr)));
    fusions.push_back(Json{{"combination_version_id", id}, {"definition_sha256", e["attrs"]["definition_sha256"]},
        {"fusion", f}, {"capability", capability(c, "fusion", fusion_key(f))}});
    Json out = Json::array();
    for (std::size_t child_index = 0; child_index < d["members"].size(); ++child_index) {
      const auto& child = d["members"][child_index];
      if (!child.is_object()) return invalid("combination member DTO");
      const auto key = child.contains("combination_version_id") ? "combination_version_id" : "method_version_id";
      LOOM_TRY_ASSIGN(auto child_id, text(child, key));
      if (!edge(s, id, child_id, "includes_method")) return unavailable("combination member lacks active native includes_method edge");
      LOOM_TRY_ASSIGN(auto rows, expand(s, selection, c, child, path, combination_parameters, member_parameters, fusions, weight, child_index, ancestors));
      for (auto& row : rows) out.push_back(std::move(row));
    }
    ancestors.erase(id);
    return out;
  }
  LOOM_TRY_ASSIGN(auto method, linked(s, id, "version_of", true));
  if (entity_role(method, s["vocabulary"]) != "method") return invalid("version_of object is not method identity");
  LOOM_TRY_ASSIGN(auto recipe, linked(s, id, "uses_recipe"));
  LOOM_TRY_ASSIGN(auto preset, linked(s, id, "uses_preset"));
  LOOM_TRY_ASSIGN(auto parameter_set, linked(s, id, "uses_parameter_set"));
  if (!parameter_set.is_null()) {
    if (entity_role(parameter_set, s["vocabulary"]) != "parameter_set_version") return invalid("uses_parameter_set kind");
    const auto& pd = parameter_set["attrs"]["definition"];
    if ((d.contains("parameters") && d["parameters"] != pd["effective_parameters"]) ||
        (d.contains("user_overrides") && d["user_overrides"] != pd["user_overrides"]))
      return conflict("method definition differs from its exact parameter-set definition");
  }
  Json prompt = nullptr;
  if (!recipe.is_null()) {
    if (entity_role(recipe, s["vocabulary"]) != "recipe_version") return invalid("uses_recipe kind");
    LOOM_TRY_ASSIGN(prompt, linked(s, recipe["id"].get<std::string>(), "uses_prompt"));
  } else { LOOM_TRY_ASSIGN(prompt, linked(s, id, "uses_prompt")); }
  if (!preset.is_null() && entity_role(preset, s["vocabulary"]) != "preset_version") return invalid("uses_preset kind");
  if (!prompt.is_null() && entity_role(prompt, s["vocabulary"]) != "prompt_version") return invalid("uses_prompt kind");
  for (const auto& binding : {std::pair<const char*, Json>{"recipe_sha256", recipe}, {"preset_sha256", preset}, {"parameter_set_sha256", parameter_set}}) {
    if (d.contains(binding.first) && (binding.second.is_null() || d[binding.first] != binding.second["attrs"]["definition_sha256"]))
      return conflict(std::string("definition/edge hash mismatch ") + binding.first);
  }
  if (!recipe.is_null()) {
    const auto& rd = recipe["attrs"]["definition"];
    const auto ph = rd.value("prompt_sha256", Json(nullptr));
    if (!ph.is_null() && (prompt.is_null() || ph != prompt["attrs"]["text_sha256"])) return conflict("recipe/prompt byte hash mismatch");
  }
  Json selected_preset_id = selection.value("preset_version_id", Json(nullptr));
  for (const auto& occurrence : path) if (occurrence["member"].contains("preset_version_id")) selected_preset_id = occurrence["member"]["preset_version_id"];
  if (!selected_preset_id.is_null()) {
    if (!selected_preset_id.is_string()) return invalid("selected preset identity must be a string");
    const auto pid = selected_preset_id.get<std::string>();
    if (!s["entities"].contains(pid) || !active(s["entities"][pid]) || entity_role(s["entities"][pid], s["vocabulary"]) != "preset_version")
      return unavailable("selected preset missing or inactive " + pid);
    preset = s["entities"][pid];
  }
  Json sources = selection.value("parameter_sources", Json::object());
  if (!sources.is_object()) return invalid("parameter_sources must be an object");
  sources["preset"] = preset.is_null() ? Json::object() : preset["attrs"]["definition"].value("parameters", Json::object());
  sources["recipe"] = recipe.is_null() ? Json::object() : recipe["attrs"]["definition"].value("parameters", Json::object());
  sources["method"] = d.value("parameters", Json::object());
  sources["parameter_set"] = parameter_set.is_null() ? Json::object() : parameter_set["attrs"]["definition"]["effective_parameters"];
  sources["combination"] = combination_parameters;
  sources["member"] = member_parameters;
  sources["selection"] = selection.value("parameters", Json::object());
  sources["user"] = selection.value("user_overrides", Json::object());
  if (!selection.contains("parameter_layers") || !selection["parameter_layers"].is_array()) return unavailable("parameter_layers precedence data missing");
  Json params = Json::object();
  for (const auto& layer : selection["parameter_layers"]) {
    if (!layer.is_string() || !sources.contains(layer.get<std::string>())) return unavailable("parameter layer data missing");
    const auto& part = sources[layer.get<std::string>()];
    params = merge(std::move(params), part);
  }
  const auto mechanism = d.value("execution_capability", std::string());
  Json cap = capability(c, "execution", mechanism);
  Json reasons = Json::array();
  if (!cap["available"].get<bool>()) reasons.push_back(Json{{"kind", "execution"}, {"capability", mechanism}, {"detail", cap}});
  for (const auto& f : fusions) if (!f["capability"]["available"].get<bool>()) reasons.push_back(Json{{"kind", "fusion"}, {"detail", f}});
  Json result{{"method_identity_id", method["id"]}, {"method_version_id", id},
      {"method_version_sha256", e["attrs"]["definition_sha256"]}, {"execution_capability", mechanism},
      {"capability", cap}, {"available", reasons.empty()}, {"unavailable_reasons", reasons},
      {"weight", weight}, {"path", path}, {"fusions", fusions}, {"effective_parameters", params},
      {"parameter_sources", sources}, {"parameter_layers", selection["parameter_layers"]},
      {"user_overrides", selection.value("user_overrides", Json::object())},
      {"recipe", recipe}, {"prompt", prompt}, {"preset", preset}, {"parameter_set", parameter_set},
      {"definition_source_status", s["definition_source_status"]}};
  return Json::array({std::move(result)});
}

Result<Json> source(std::string bytes, const Json& known, const char* role_name) {
  model::Observation o;
  const auto sha = Sha256::hex(bytes);
  o.unit = "un_" + sha;
  o.kind = model::ObservationKind::Field;
  o.text = std::move(bytes);
  o.locator.source = "sha256:" + sha;
  o.locator.member = role_name;
  o.locator.byte_start = 0;
  if (o.text.size() > static_cast<std::size_t>(INT64_MAX)) return invalid("source byte length representation overflow");
  o.locator.byte_len = static_cast<std::int64_t>(o.text.size());
  o.id = model::Observation::make_id(o.unit, o.locator, o.text);
  o.artifact_type = "loom.method_graph/1";
  o.attrs = Json{{"capture_role", role_name}, {"content_truth", "not_established"}};
  return Json{{"observation", o.to_json()}, {"known_at", known}, {"text_sha256", sha}};
}
Result<Json> entity(const Json& v, const char* r, const Json& attrs, const Json& known, std::string id = {}) {
  LOOM_TRY_ASSIGN(auto kind, role(v, "kinds", r));
  model::Entity e;
  e.kind = kind;
  e.canonical_key = id.empty() ? hash(attrs) : id;
  e.id = id.empty() ? model::Entity::make_id(kind, e.canonical_key) : std::move(id);
  e.label = e.id;
  e.first_seen = known.is_string() ? known.get<std::string>() : "";
  e.last_seen = e.first_seen;
  e.evidence = model::EvidenceClass::Derived;
  e.origin = model::Origin::System;
  e.confidence = 1.0;
  e.attrs = attrs;
  return e.to_json();
}
Result<Json> structural(const Json& v, const std::string& a, const char* r, const std::string& b, const Json& support) {
  LOOM_TRY_ASSIGN(auto predicate, role(v, "predicates", r));
  LOOM_TRY_ASSIGN(auto observation, model::Observation::from_json(support["observation"]));
  model::Claim c;
  c.subject = a;
  c.predicate = predicate;
  c.object = b;
  c.qualifiers.scope = "loom.method_graph/1";
  c.qualifiers.extra = Json{{"confidence_scope", "structure_only"}, {"content_truth", "not_established"},
      {"acceptance_establishes_content_truth", false}};
  c.assessment.evidence = model::EvidenceClass::Derived;
  c.assessment.origin = model::Origin::System;
  c.assessment.confidence = 1.0;
  c.assessment.derivation = model::Derivation{"loom.method_registry/1", 1, "", 0};
  c.assessment.support.push_back(model::Support{observation.id, observation.locator, observation.text, "loom.method_registry/1", 1.0});
  c.id = model::Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
  LOOM_TRY(c.validate());
  return c.to_json();
}
Status add_row(Json& rows, const char* collection, const Json& row) {
  const auto id = std::string_view(collection) == "sources" ? row["observation"]["id"] : row["id"];
  for (const auto& old : rows[collection]) {
    const auto old_id = std::string_view(collection) == "sources" ? old["observation"]["id"] : old["id"];
    if (old_id == id) {
      if (std::string_view(collection) == "sources" && old["observation"] == row["observation"] && old["text_sha256"] == row["text_sha256"]) return {};
      if (old != row) return conflict("record identity collision while preparing graph");
      return {};
    }
  }
  rows[collection].push_back(row);
  return {};
}
Result<Json> project(const Json& base, const Json& rows, const Json& origin, const Json& known,
                     const std::string& proposal, const MethodPacketOperation& operation) {
  if (!operation) return unavailable("native packet operations missing");
  if (base.is_null()) {
    Json command = rows;
    command["operation"] = "make";
    command["origin"] = origin;
    command["known_at"] = known;
    return operation(command);
  }
  LOOM_TRY_ASSIGN(auto d, operation(Json{{"operation", "empty_diff"}, {"packet", base}, {"proposal_id", proposal},
      {"origin", origin}, {"known_at", known}}));
  for (const char* collection : {"entities", "claims", "sources"}) for (const auto& row : rows[collection]) {
    const auto id = std::string_view(collection) == "sources" ? row["observation"]["id"] : row["id"];
    const Json* previous = nullptr;
    for (const auto& old : base[collection]) {
      const auto old_id = std::string_view(collection) == "sources" ? old["observation"]["id"] : old["id"];
      if (old_id == id) { previous = &old; break; }
    }
    if (!previous) d[collection]["add"].push_back(row);
    else if (*previous != row) {
      if (std::string_view(collection) == "sources") {
        if ((*previous)["observation"] == row["observation"] && (*previous)["text_sha256"] == row["text_sha256"]) continue;
        return conflict("immutable source would be rewritten");
      }
      d[collection]["update"].push_back(Json{{"id", id}, {"before_sha256", hash(*previous)}, {"after", row}});
    }
  }
  LOOM_TRY_ASSIGN(auto p, operation(Json{{"operation", "preview"}, {"packet", base}, {"diff", d}}));
  if (!p.contains("candidate_packet")) return invalid("native preview omitted candidate packet");
  return p["candidate_packet"];
}
Result<Json> at_path(const Json& value, const Json& path) {
  if (!path.is_array()) return invalid("request binding path must be an array");
  const Json* p = &value;
  for (const auto& key : path) {
    if (key.is_string() && p->is_object() && p->contains(key.get<std::string>())) p = &(*p)[key.get<std::string>()];
    else if (key.is_number_unsigned() || (key.is_number_integer() && key.get<std::int64_t>() >= 0)) {
      const auto n = key.get<std::size_t>();
      if (!p->is_array() || n >= p->size()) return unavailable("request binding index missing");
      p = &(*p)[n];
    } else return unavailable("request binding source path missing");
  }
  return *p;
}
Status set_path(Json& value, const Json& path, const Json& replacement) {
  if (!path.is_array()) return invalid("request binding target must be an array");
  if (path.empty()) { value = replacement; return {}; }
  Json* p = &value;
  for (std::size_t n = 0; n < path.size(); ++n) {
    const auto& key = path[n];
    const bool last = n + 1 == path.size();
    if (key.is_string() && p->is_object()) {
      const auto name = key.get<std::string>();
      if (last) { (*p)[name] = replacement; return {}; }
      if (!p->contains(name)) return unavailable("request binding target parent missing");
      p = &(*p)[name];
    } else if ((key.is_number_unsigned() || (key.is_number_integer() && key.get<std::int64_t>() >= 0)) && p->is_array()) {
      const auto ix = key.get<std::size_t>();
      if (ix >= p->size()) return unavailable("request binding target index missing");
      if (last) { (*p)[ix] = replacement; return {}; }
      p = &(*p)[ix];
    } else return invalid("request binding target shape");
  }
  return {};
}
Status snapshot_identity(const Json& s) {
  if (!s.is_object() || s.value("schema", "") != "loom.method_registry_snapshot/1" || !digest(s.value("snapshot_sha256", Json())))
    return invalid("snapshot shape");
  Json payload = s;
  payload.erase("snapshot_sha256");
  if (s["snapshot_sha256"] != hash(payload)) return conflict("snapshot hash drift");
  return {};
}
Result<Json> bound_definition_records(const Json& bindings, const Json& entities) {
  Json records = Json::object();
  for (auto binding = bindings.begin(); binding != bindings.end(); ++binding) {
    if (binding.key() == "run_id" || binding.value().is_null()) continue;
    if (!binding.value().is_string()) return invalid("definition binding identity must be a string");
    const auto id = binding.value().get<std::string>();
    if (!entities.contains(id)) return unavailable("bound definition native entity missing " + id);
    records[binding.key()] = entities[id]["attrs"];
  }
  return records;
}
Status manifest_identity(const Json& manifest) {
  if (!manifest.is_object() || manifest.value("schema", "") != "loom.method_graph/1" ||
      !manifest.contains("bindings") || !manifest.contains("definition_hashes") || !manifest.contains("trace")) return invalid("method manifest schema/fields");
  LOOM_TRY(vocabulary(manifest.at("vocabulary")));
  const auto& bindings = manifest["bindings"];
  const auto& hashes = manifest["definition_hashes"];
  const auto& trace = manifest["trace"];
  if (!bindings.is_object() || !hashes.is_object() || !trace.is_object() || trace.value("schema", "") != "loom.method_run_trace/1") return invalid("method bindings/trace fields");
  for (const char* key : {"method_identity_id", "method_version_id", "run_id"}) {
    LOOM_TRY(text(bindings, key));
    if (!trace.contains(key) || trace[key] != bindings[key]) return conflict("manifest/trace identity mismatch");
  }
  for (const auto& id : bindings) if (!(id.is_null() || (id.is_string() && !id.get_ref<const std::string&>().empty()))) return invalid("binding identity must be a string or null");
  if (!hashes.contains("method_version") || !digest(hashes["method_version"])) return invalid("method version hash required");
  for (const auto& sha : hashes) if (!sha.is_null() && !digest(sha)) return invalid("definition hash must be SHA256 or null");
  if (!trace.contains("effective_parameters") || !trace.contains("input_sha256") || (!trace["input_sha256"].is_null() && !digest(trace["input_sha256"]))) return invalid("trace parameters/input identity required");
  LOOM_TRY(finite(trace));
  if (trace.contains("measurements")) LOOM_TRY(measurements(trace["measurements"]));
  for (const char* key : {"model", "execution_kind", "measurement_scope", "prepared_at", "projected_at"})
    if (trace.contains(key) && !trace[key].is_null() && !trace[key].is_string()) return invalid("trace typed field must be string or null");
  for (const auto& pair : {std::pair<const char*, const char*>{"recipe_sha256", "recipe"}, {"prompt_sha256", "prompt_bytes"}, {"preset_sha256", "preset"}, {"combination_sha256", "combination"}, {"parameter_set_sha256", "parameter_set"}})
    if (trace.contains(pair.first) && (!hashes.contains(pair.second) || hashes[pair.second] != trace[pair.first])) return conflict("manifest/trace definition hash mismatch");
  if (bindings.contains("parameter_set_version_id") && !bindings["parameter_set_version_id"].is_null()) {
    if (!trace.contains("parameter_set_version_id") || trace["parameter_set_version_id"] != bindings["parameter_set_version_id"] ||
        !hashes.contains("parameter_set") || !digest(hashes["parameter_set"]) || !trace.contains("parameter_set_sha256"))
      return conflict("manifest/trace parameter-set identity mismatch");
  }
  if (manifest.contains("definition_records") && !manifest["definition_records"].is_object()) return invalid("definition_records must be an object");
  for (const char* key : {"raw_response_sha256", "response_text_sha256", "compilation_sha256", "request_sha256", "recipe_request_sha256"})
    if (trace.contains(key) && !trace[key].is_null() && !digest(trace[key])) return invalid("trace digest field invalid");
  return {};
}

}  // namespace

Result<Json> MethodRegistry::load(const Json& profile, const std::vector<std::string>& receipt_ids) {
  try {
    auto lock = db_.lock();
    if (!profile.is_object() || !profile.contains("vocabulary")) return unavailable("method profile data missing");
    LOOM_TRY(finite(profile));
    LOOM_TRY(vocabulary(profile["vocabulary"]));
    Json s{{"schema", "loom.method_registry_snapshot/1"}, {"vocabulary", profile["vocabulary"]},
        {"profile", profile}, {"receipts", Json::array()}, {"entities", Json::object()}, {"claims", Json::object()}, {"sources", Json::object()}};
    for (const char* collection : {"entities", "claims", "sources"}) {
      const auto rows = profile.value(collection, Json::array());
      if (!rows.is_array()) return invalid("profile native collections must be arrays");
      for (const auto& row : rows) {
        LOOM_TRY(dto(row, collection));
        LOOM_TRY(put(s[collection], row, std::string_view(collection) == "sources", false, s["vocabulary"]));
      }
    }
    kb::GraphPacketStore packets(db_);
    kb::KnowledgeStore store(db_);
    std::set<std::string> seen_receipts;
    for (const auto& id : receipt_ids) {
      if (!seen_receipts.insert(id).second) return invalid("duplicate graph receipt");
      LOOM_TRY_ASSIGN(auto read, packets.execute(Json{{"operation", "read"}, {"receipt_id", id}}));
      if (!read["row_drift"].value("matches", false)) return conflict("accepted graph row drift " + id);
      const auto& receipt = read["receipt"];
      const auto run = receipt["run_id"].get<std::string>();
      for (const auto& eid : receipt["selection"]["entities"]) {
        LOOM_TRY_ASSIGN(auto e, store.get_entity(run, eid.get<std::string>()));
        if (!e) return conflict("accepted entity missing");
        LOOM_TRY(put(s["entities"], e->to_json(), false, true, s["vocabulary"]));
      }
      for (const auto& cid : receipt["selection"]["claims"]) {
        LOOM_TRY_ASSIGN(auto c, store.get_claim(run, cid.get<std::string>()));
        if (!c) return conflict("accepted Claim missing");
        LOOM_TRY(put(s["claims"], c->to_json(), false, true, s["vocabulary"]));
      }
      for (const auto& oid : receipt["selection"]["sources"]) {
        LOOM_TRY_ASSIGN(auto o, store.get_observation(run, oid.get<std::string>()));
        if (!o) return conflict("accepted Observation missing");
        Json original = nullptr;
        for (const auto& row : receipt["packet"]["sources"]) if (row["observation"]["id"] == oid) { original = row; break; }
        if (original.is_null()) return conflict("receipt source metadata missing");
        original["observation"] = o->to_json();
        LOOM_TRY(dto(original, "sources"));
        LOOM_TRY(put(s["sources"], original, true, true, s["vocabulary"]));
      }
      s["receipts"].push_back(Json{{"receipt_id", id}, {"run_id", run}, {"stored_row_sha256", receipt["stored_row_sha256"]}});
    }
    LOOM_TRY(closure(s));
    s["definition_source_status"] = definition_source_status(s);
    s["snapshot_sha256"] = hash(s);
    return s;
  } catch (const std::exception& e) { return invalid(e.what()); }
}

Result<Json> MethodRegistry::resolve(const Json& s, const Json& selection_overlay, const Json& native_capabilities) {
  try {
    LOOM_TRY(snapshot_identity(s));
    LOOM_TRY(closure(s));
    if (!selection_overlay.is_object()) return invalid("selection overlay must be an object");
    Json selection = merge(s["profile"].value("selection", Json::object()), selection_overlay);
    LOOM_TRY(finite(selection));
    if (!selection.contains("members") || !selection["members"].is_array()) return unavailable("method selection data missing");
    Json fusions = Json::array();
    if (selection.contains("fusion") && !selection["fusion"].is_null()) {
      const auto& f = selection["fusion"];
      fusions.push_back(Json{{"fusion", f}, {"capability", capability(native_capabilities, "fusion", fusion_key(f))}});
    }
    Json leaves = Json::array();
    std::set<std::string> ancestors;
    for (std::size_t member_index = 0; member_index < selection["members"].size(); ++member_index) {
      const auto& member = selection["members"][member_index];
      LOOM_TRY_ASSIGN(auto rows, expand(s, selection, native_capabilities, member, Json::array(), Json::object(), Json::object(), fusions, 1.0, member_index, ancestors));
      for (auto& row : rows) {
        row["snapshot_sha256"] = s["snapshot_sha256"];
        row["resolution_sha256"] = hash(row);
        leaves.push_back(std::move(row));
      }
    }
    Json result{{"schema", "loom.method_resolution/1"}, {"snapshot_sha256", s["snapshot_sha256"]},
        {"selection", selection}, {"leaves", leaves}, {"capabilities", native_capabilities}};
    result["resolution_sha256"] = hash(result);
    return result;
  } catch (const std::exception& e) { return invalid(e.what()); }
}

Result<Json> MethodRegistry::prepare(const Json& s, const Json& leaf, const Json& rc, const MethodPacketOperation& operation) {
  try {
    auto lock = db_.lock();
    LOOM_TRY(snapshot_identity(s));
    std::vector<std::string> receipts;
    for (const auto& r : s["receipts"]) receipts.push_back(r["receipt_id"].get<std::string>());
    LOOM_TRY_ASSIGN(auto fresh, load(s["profile"], receipts));
    if (fresh["snapshot_sha256"] != s["snapshot_sha256"]) return conflict("graph snapshot changed before dispatch");
    Json checked = leaf;
    checked.erase("resolution_sha256");
    if (leaf.value("resolution_sha256", Json()) != hash(checked) || leaf.value("snapshot_sha256", Json()) != s["snapshot_sha256"])
      return conflict("resolved leaf identity drift");
    if (!leaf.value("available", false)) return unavailable("selected method/fusion execution unavailable");
    LOOM_TRY(finite(rc));
    LOOM_TRY_ASSIGN(auto run_id, text(rc, "run_id"));
    if (!rc.contains("origin") || !rc.contains("known_at")) return invalid("run instrument origin/known_at required");
    const auto input = rc.value("input_sha256", Json(nullptr));
    if (!input.is_null() && !digest(input)) return invalid("input identity must be SHA256 or null");
    Json params = rc.value("effective_parameters", leaf["effective_parameters"]);
    const auto& v = s["vocabulary"];
    const auto& known = rc["known_at"];
    Json rows{{"entities", values(s["entities"])}, {"claims", values(s["claims"])}, {"sources", values(s["sources"])}};
    const auto& original_version = s["entities"][leaf["method_version_id"].get<std::string>()];
    const auto user_overrides = rc.value("user_overrides", leaf["user_overrides"]);
    Json recipe_definition = leaf["recipe"].is_null() ? Json(nullptr) : leaf["recipe"]["attrs"]["definition"];
    if (rc.contains("recipe_overrides")) {
      if (recipe_definition.is_null()) return unavailable("recipe override supplied to a method without recipe");
      if (!rc["recipe_overrides"].is_object()) return invalid("recipe overrides must be an object");
      recipe_definition = merge(std::move(recipe_definition), rc["recipe_overrides"]);
    }
    if (!recipe_definition.is_null() && recipe_definition.contains("parameter_bindings")) {
      const auto& bindings = recipe_definition["parameter_bindings"];
      if (!bindings.is_array()) return invalid("parameter_bindings must be an array");
      for (const auto& binding : bindings) {
        if (!binding.is_object() || !binding.contains("source") || !binding.contains("target")) return invalid("parameter binding source/target required");
        LOOM_TRY_ASSIGN(auto value, at_path(rc, binding["source"]));
        LOOM_TRY(set_path(params, binding["target"], value));
      }
    }
    if (!recipe_definition.is_null() && recipe_definition.contains("prompt_sha256") && !recipe_definition["prompt_sha256"].is_null() &&
        (leaf["prompt"].is_null() || recipe_definition["prompt_sha256"] != leaf["prompt"]["attrs"]["text_sha256"]))
      return conflict("effective recipe prompt hash does not match bound exact prompt bytes");
    Json parameter_definition{{"effective_parameters", params}, {"user_overrides", user_overrides}};
    LOOM_TRY_ASSIGN(auto parameter_set, entity(v, "parameter_set_version",
        Json{{"definition", parameter_definition}, {"definition_sha256", hash(parameter_definition)}}, known));
    LOOM_TRY(add_row(rows, "entities", parameter_set));
    // These roles are caller data; current producer adoption requires real
    // parameter edges even when the consumed set is empty or explicitly null.
    LOOM_TRY(role(v, "predicates", "uses_parameter_set"));
    Json recipe_entity = nullptr;
    if (!recipe_definition.is_null()) {
      recipe_definition["parameters"] = params;
      recipe_definition["user_overrides"] = user_overrides;
      LOOM_TRY_ASSIGN(recipe_entity, entity(v, "recipe_version", Json{{"definition", recipe_definition}, {"definition_sha256", hash(recipe_definition)}}, known));
      LOOM_TRY(add_row(rows, "entities", recipe_entity));
    }
    Json definition = original_version["attrs"]["definition"];
    definition["parameters"] = params;
    definition["user_overrides"] = user_overrides;
    definition["parameter_set_sha256"] = parameter_set["attrs"]["definition_sha256"];
    definition["selection"] = Json{{"weight", leaf["weight"]}, {"path", leaf["path"]}, {"fusions", leaf["fusions"]},
        {"parameter_layers", leaf["parameter_layers"]}};
    if (!recipe_entity.is_null()) definition["recipe_sha256"] = recipe_entity["attrs"]["definition_sha256"];
    if (!leaf["preset"].is_null()) definition["preset_sha256"] = leaf["preset"]["attrs"]["definition_sha256"];
    LOOM_TRY_ASSIGN(auto version, entity(v, "method_version", Json{{"definition", definition}, {"definition_sha256", hash(definition)}}, known));
    LOOM_TRY(add_row(rows, "entities", version));
    Json bindings{{"method_identity_id", leaf["method_identity_id"]}, {"method_version_id", version["id"]}, {"run_id", run_id},
        {"parameter_set_version_id", parameter_set["id"]}};
    Json hashes{{"method_version", version["attrs"]["definition_sha256"]}, {"parameter_set", parameter_set["attrs"]["definition_sha256"]}};
    Json trace{{"schema", "loom.method_run_trace/1"}, {"run_id", run_id}, {"method_identity_id", leaf["method_identity_id"]},
        {"method_version_id", version["id"]}, {"effective_parameters", params}, {"input_sha256", input},
        {"user_overrides", user_overrides}, {"parameter_set_version_id", parameter_set["id"]}, {"parameter_set_sha256", hashes["parameter_set"]},
        {"execution_kind", leaf["execution_capability"]},
        {"prepared_at", known}, {"measurements", rc.value("measurements", Json::object())}, {"selection_path", leaf["path"]},
        {"weight", leaf["weight"]}, {"fusions", leaf["fusions"]}};
    LOOM_TRY(measurements(trace["measurements"]));
    if (rc.contains("measurement_scope")) trace["measurement_scope"] = rc["measurement_scope"];
    if (!recipe_entity.is_null()) {
      bindings["recipe_version_id"] = recipe_entity["id"];
      hashes["recipe"] = recipe_entity["attrs"]["definition_sha256"];
      trace["recipe_sha256"] = hashes["recipe"];
      if (recipe_definition.contains("model")) {
        trace["model"] = recipe_definition["model"];
        trace["requested_model"] = recipe_definition["model"];
      }
    }
    for (const auto& binding : {std::pair<const char*, Json>{"prompt_version_id", leaf["prompt"]}, {"preset_version_id", leaf["preset"]}}) {
      if (binding.second.is_null()) continue;
      bindings[binding.first] = binding.second["id"];
      const bool prompt = std::string_view(binding.first) == "prompt_version_id";
      const auto h = binding.second["attrs"][prompt ? "text_sha256" : "definition_sha256"];
      hashes[prompt ? "prompt_bytes" : "preset"] = h;
      trace[prompt ? "prompt_sha256" : "preset_sha256"] = h;
      if (!prompt) trace["preset_version_id"] = binding.second["id"];
    }
    // Create immutable effective combination versions bottom-up. Only the
    // consumed occurrence is replaced; repeated sibling occurrences remain
    // distinct and continue referencing their own original versions.
    Json effective_combinations = Json::array();
    Json child_id = version["id"];
    for (std::size_t n = leaf["path"].size(); n > 1; --n) {
      const auto& parent = leaf["path"][n - 2];
      const auto id = parent["id"].get<std::string>();
      if (entity_role(s["entities"][id], v) != "combination_version") return invalid("non-combination parent in resolved path");
      Json d = s["entities"][id]["attrs"]["definition"];
      const auto index = leaf["path"][n - 1]["member_index"].get<std::size_t>();
      if (index >= d["members"].size()) return conflict("resolved combination occurrence index drift");
      auto& member = d["members"][index];
      const char* key = member.contains("combination_version_id") ? "combination_version_id" : "method_version_id";
      member[key] = child_id;
      LOOM_TRY_ASSIGN(auto combo, entity(v, "combination_version", Json{{"definition", d}, {"definition_sha256", hash(d)}}, known));
      LOOM_TRY(add_row(rows, "entities", combo));
      effective_combinations.push_back(combo);
      child_id = combo["id"];
      bindings["combination_version_id"] = combo["id"];
      hashes["combination"] = combo["attrs"]["definition_sha256"];
      trace["combination_version_id"] = combo["id"];
      trace["combination_sha256"] = combo["attrs"]["definition_sha256"];
    }
    if (!effective_combinations.empty()) trace["effective_combination_versions"] = effective_combinations;
    for (const char* key : {"model_identity_id", "compiler_transform_id"}) {
      if (!rc.contains(key) || rc[key].is_null()) continue;
      LOOM_TRY_ASSIGN(auto id, text(rc, key));
      if (!s["entities"].contains(id) || !active(s["entities"][id])) return unavailable(std::string("run binding missing native record ") + key);
      const auto expected = std::string_view(key) == "model_identity_id" ? "model_identity" : "compiler_transform";
      if (entity_role(s["entities"][id], v) != expected) return invalid("run optional binding kind mismatch");
      bindings[key] = id;
      trace[key] = id;
    }
    Json definition_entities = Json::object();
    for (const auto& e : rows["entities"]) definition_entities[e["id"].get<std::string>()] = e;
    LOOM_TRY_ASSIGN(auto definition_records, bound_definition_records(bindings, definition_entities));
    Json manifest{{"schema", "loom.method_graph/1"}, {"vocabulary", v}, {"bindings", bindings}, {"definition_hashes", hashes},
        {"definition_records", definition_records}, {"trace", trace}};
    LOOM_TRY_ASSIGN(auto captured, source(json::canonical(manifest), known, "method-manifest.json"));
    LOOM_TRY(add_row(rows, "sources", captured));
    LOOM_TRY_ASSIGN(auto captured_records, source(json::canonical(definition_records), known, "method-definition-records.json"));
    LOOM_TRY(add_row(rows, "sources", captured_records));
    // Capture each definition separately, including exact prompt bytes. Existing
    // fixture metadata without a definition Observation stays explicitly visible.
    for (const auto& e : rows["entities"]) {
      const auto r = entity_role(e, v);
      if (!r.ends_with("_version")) continue;
      const auto& a = e["attrs"];
      if (r != "prompt_version" && !a.contains("definition")) continue;
      LOOM_TRY_ASSIGN(auto captured_definition, source(r == "prompt_version" ? a["text"].get<std::string>() : json::canonical(a["definition"]), known, r.c_str()));
      LOOM_TRY(add_row(rows, "sources", captured_definition));
    }
    LOOM_TRY_ASSIGN(auto run, entity(v, "run", merge(trace, Json{{"projection_status", "prepared"}}), known, run_id));
    LOOM_TRY(add_row(rows, "entities", run));
    auto relation = [&](const Json& a, const char* r, const Json& b) -> Status {
      LOOM_TRY_ASSIGN(auto claim, structural(v, a.get<std::string>(), r, b.get<std::string>(), captured));
      return add_row(rows, "claims", claim);
    };
    LOOM_TRY(relation(version["id"], "version_of", bindings["method_identity_id"]));
    LOOM_TRY(relation(run_id, "requests_method_version", version["id"]));
    LOOM_TRY(relation(version["id"], "uses_parameter_set", parameter_set["id"]));
    LOOM_TRY(relation(run_id, "uses_parameter_set", parameter_set["id"]));
    if (bindings.contains("combination_version_id")) LOOM_TRY(relation(run_id, "uses_combination", bindings["combination_version_id"]));
    if (!recipe_entity.is_null()) {
      LOOM_TRY(relation(version["id"], "uses_recipe", recipe_entity["id"]));
      if (bindings.contains("prompt_version_id")) LOOM_TRY(relation(recipe_entity["id"], "uses_prompt", bindings["prompt_version_id"]));
    } else if (bindings.contains("prompt_version_id")) LOOM_TRY(relation(version["id"], "uses_prompt", bindings["prompt_version_id"]));
    if (bindings.contains("preset_version_id")) LOOM_TRY(relation(version["id"], "uses_preset", bindings["preset_version_id"]));
    for (const auto& combo : effective_combinations) for (const auto& member : combo["attrs"]["definition"]["members"]) {
      const char* key = member.contains("combination_version_id") ? "combination_version_id" : "method_version_id";
      LOOM_TRY(relation(combo["id"], "includes_method", member[key]));
    }
    LOOM_TRY_ASSIGN(auto packet, project(rc.value("base_packet", Json(nullptr)), rows, rc["origin"], known, "method-prepare:" + run_id, operation));
    Json effective_recipe = recipe_definition.is_null() ? Json(nullptr) : recipe_definition;
    if (!effective_recipe.is_null()) {
      effective_recipe["definition"] = recipe_definition;
      effective_recipe["definition_sha256"] = hashes["recipe"];
      const auto request_bindings = recipe_definition.value("request_bindings", Json::array());
      if (!request_bindings.is_array()) return invalid("request_bindings must be an array");
      const auto bind_request = [&](const Json& current_packet) -> Result<Json> {
        Json runtime_context = rc;
        runtime_context["packet"] = current_packet;
        runtime_context["base_packet_sha256"] = current_packet["packet_id"];
        runtime_context["effective_parameters"] = params;
        runtime_context["user_overrides"] = user_overrides;
        Json request = recipe_definition.value("request", Json::object());
        for (const auto& binding : request_bindings) {
          if (!binding.is_object() || !binding.contains("source") || !binding.contains("target")) return invalid("request binding source/target required");
          LOOM_TRY_ASSIGN(auto value, at_path(runtime_context, binding["source"]));
          LOOM_TRY(set_path(request, binding["target"], value));
        }
        return request;
      };
      LOOM_TRY_ASSIGN(auto request, bind_request(packet));
      if (request.is_object() && request.contains("model") && request["model"].is_string()) {
        const auto requested_model = request["model"];
        if ((recipe_definition.contains("model") && recipe_definition["model"].is_string() && recipe_definition["model"] != requested_model) ||
            (params.is_object() && params.contains("model") && params["model"].is_string() && params["model"] != requested_model))
          return conflict("declared model control differs from fully instantiated request model");
        auto& actual_trace = manifest["trace"];
        if (actual_trace.value("requested_model", Json(nullptr)) != requested_model || actual_trace.value("model", Json(nullptr)) != requested_model) {
          actual_trace["requested_model"] = requested_model;
          actual_trace["model"] = requested_model;
          actual_trace["model_identity_basis"] = "fully_instantiated_request";
          run["attrs"] = merge(actual_trace, Json{{"projection_status", "prepared"}});
          LOOM_TRY_ASSIGN(auto final_trace_source, source(json::canonical(actual_trace), known, "prepared-method-run-trace.json"));
          Json trace_rows{{"entities", Json::array({run})}, {"claims", Json::array()}, {"sources", Json::array({final_trace_source})}};
          LOOM_TRY_ASSIGN(packet, project(packet, trace_rows, rc["origin"], known, "method-bound-controls:" + run_id, operation));
          LOOM_TRY_ASSIGN(request, bind_request(packet));
          if (!request.is_object() || request.value("model", Json(nullptr)) != requested_model)
            return conflict("model control depends recursively on its registered packet identity");
        }
      }
      effective_recipe["request"] = request;
      manifest["trace"]["recipe_request_sha256"] = hash(request);
      manifest["trace"]["recipe_request_hash_scope"] = "instantiated_recipe_request_after_graph_registration";
    }
    return Json{{"packet", packet}, {"manifest", manifest}, {"effective_recipe", effective_recipe},
        {"definition_source_status", s["definition_source_status"]}, {"canonical_store_written", false}};
  } catch (const std::exception& e) { return invalid(e.what()); }
}

Result<Json> MethodRegistry::bind_results(const Json& candidate, const Json& original, const Json& rb,
                                         const MethodPacketOperation& operation) {
  try {
    LOOM_TRY(manifest_identity(original));
    LOOM_TRY(finite(rb));
    Json manifest = original;
    const bool had_definition_records = manifest.contains("definition_records");
    // Keep referenced top-level values stable even for older manifests that
    // did not yet contain definition_records (ordered_json object storage).
    if (!had_definition_records) manifest["definition_records"] = Json::object();
    auto& trace = manifest["trace"];
    const auto& v = manifest["vocabulary"];
    const auto& b = manifest["bindings"];
    LOOM_TRY_ASSIGN(auto run_id, text(b, "run_id"));
    LOOM_TRY_ASSIGN(auto version_id, text(b, "method_version_id"));
    const auto known = rb.value("known_at", trace.value("prepared_at", Json(nullptr)));
    if (!rb.contains("origin")) return invalid("result binding instrument origin required");
    std::set<std::string> result_ids;
    if (rb.contains("node_ids")) {
      if (!rb["node_ids"].is_object()) return invalid("node_ids must be actual compiler map");
      for (const auto& id : rb["node_ids"]) {
        if (!id.is_string()) return invalid("compiler node ID must be a string");
        result_ids.insert(id.get<std::string>());
      }
    }
    if (rb.contains("result_entity_ids")) {
      if (!rb["result_entity_ids"].is_array()) return invalid("result_entity_ids must be an array");
      for (const auto& id : rb["result_entity_ids"]) {
        if (!id.is_string()) return invalid("result entity ID must be a string");
        result_ids.insert(id.get<std::string>());
      }
    }
    if (result_ids.empty()) return unavailable("actual result IDs missing");
    Json entities = Json::object();
    for (const auto& e : candidate["entities"]) entities[e["id"].get<std::string>()] = e;
    if (!entities.contains(run_id) || !entities.contains(version_id) || !active(entities[run_id]) || !active(entities[version_id]))
      return unavailable("result binding method/run missing or inactive");
    LOOM_TRY(immutable(entities[version_id], v));
    if (entities[version_id]["attrs"]["definition_sha256"] != manifest["definition_hashes"]["method_version"])
      return conflict("bound method-version identity/hash mismatch");
    const auto& run_attrs = entities[run_id]["attrs"];
    for (const char* key : {"run_id", "method_identity_id", "method_version_id", "effective_parameters", "input_sha256"})
      if (!run_attrs.contains(key) || run_attrs[key] != trace[key]) return conflict("manifest does not identify actual prepared run");
    Json graph_snapshot{{"vocabulary", v}, {"entities", entities}, {"claims", Json::object()}};
    for (const auto& c : candidate["claims"]) graph_snapshot["claims"][c["id"].get<std::string>()] = c;
    if (!edge(graph_snapshot, run_id, version_id, "requests_method_version") ||
        !edge(graph_snapshot, version_id, b["method_identity_id"].get<std::string>(), "version_of"))
      return conflict("prepared run/version lacks actual native method relation");
    if (b.contains("parameter_set_version_id") && !b["parameter_set_version_id"].is_null()) {
      const auto pid = b["parameter_set_version_id"].get<std::string>();
      if (!entities.contains(pid) || !active(entities[pid]) || entity_role(entities[pid], v) != "parameter_set_version")
        return unavailable("bound parameter-set native entity missing or inactive");
      LOOM_TRY(immutable(entities[pid], v));
      const auto& attrs = entities[pid]["attrs"];
      if (attrs["definition_sha256"] != manifest["definition_hashes"]["parameter_set"] ||
          attrs["definition"]["effective_parameters"] != trace["effective_parameters"] ||
          attrs["definition"]["user_overrides"] != trace.value("user_overrides", Json::object()) ||
          entities[version_id]["attrs"]["definition"].value("parameter_set_sha256", Json(nullptr)) != attrs["definition_sha256"])
        return conflict("bound parameter-set definition differs from method/run consumed parameters");
      if (!edge(graph_snapshot, run_id, pid, "uses_parameter_set") || !edge(graph_snapshot, version_id, pid, "uses_parameter_set"))
        return conflict("method/run lacks actual native parameter-set relations");
      if (run_attrs.value("parameter_set_version_id", Json(nullptr)) != b["parameter_set_version_id"] ||
          run_attrs.value("parameter_set_sha256", Json(nullptr)) != attrs["definition_sha256"])
        return conflict("manifest parameter set does not identify actual prepared run");
    }
    if (b.contains("combination_version_id") && !b["combination_version_id"].is_null()) {
      const auto cid = b["combination_version_id"].get<std::string>();
      if (!entities.contains(cid) || !active(entities[cid]) || entity_role(entities[cid], v) != "combination_version")
        return unavailable("bound combination native entity missing or inactive");
      LOOM_TRY(immutable(entities[cid], v));
      if (entities[cid]["attrs"]["definition_sha256"] != manifest["definition_hashes"].value("combination", Json(nullptr)) ||
          run_attrs.value("combination_version_id", Json(nullptr)) != b["combination_version_id"] ||
          trace.value("combination_version_id", Json(nullptr)) != b["combination_version_id"] || !edge(graph_snapshot, run_id, cid, "uses_combination"))
        return conflict("prepared run lacks its actual effective combination relation");
    }
    if (had_definition_records) {
      LOOM_TRY_ASSIGN(auto actual_records, bound_definition_records(b, entities));
      if (manifest["definition_records"] != actual_records) return conflict("captured definition_records differ from actual bound native attrs");
    }
    for (const char* key : {"model_origin", "raw_response_source_ref", "raw_response_sha256", "response_text_sha256",
         "compilation_sha256", "response_provenance", "transform", "measurement_scope"}) if (rb.contains(key)) trace[key] = rb[key];
    if (rb.contains("instrumentation")) trace["instrumentation"] = rb["instrumentation"];
    if (rb.contains("measurements")) trace["measurements"] = rb["measurements"];
    LOOM_TRY(measurements(trace.value("measurements", Json::object())));
    LOOM_TRY(manifest_identity(manifest));
    trace["projected_at"] = known;
    trace["result_bindings"] = Json::array();
    Json actual_models = Json::array();
    Json actual_origins = Json::array();
    for (const auto& id : result_ids) {
      if (!entities.contains(id)) return unavailable("actual result entity missing " + id);
      const auto& actual_attrs = entities[id]["attrs"];
      if (rb.contains("model_origin")) {
        if (!actual_attrs.contains("model_origin") || actual_attrs["model_origin"] != rb["model_origin"])
          return conflict("result model origin differs from actual native result record");
      }
      if (actual_attrs.contains("model_origin") && actual_attrs["model_origin"].is_object()) {
        const auto& actual_origin = actual_attrs["model_origin"];
        const auto actual_model = actual_origin.value("model", Json(nullptr));
        if (rb.contains("expected_model") && rb["expected_model"] != actual_model)
          return conflict("result model identity differs from explicit expected model");
        if (std::find(actual_models.begin(), actual_models.end(), actual_model) == actual_models.end()) actual_models.push_back(actual_model);
        if (std::find(actual_origins.begin(), actual_origins.end(), actual_origin) == actual_origins.end()) actual_origins.push_back(actual_origin);
        if (trace.contains("recipe_sha256") && actual_origin.value("recipe_sha256", Json(nullptr)) != trace["recipe_sha256"])
          return conflict("result recipe identity differs from effective recipe");
        if (rb.contains("compiler_input_sha256") && actual_origin.value("response_sha256", Json(nullptr)) != rb["compiler_input_sha256"])
          return conflict("result response identity differs from captured response");
      }
      Json binding{{"result_entity_id", id}, {"run_id", run_id}, {"method_version_id", version_id}};
      if (b.contains("compiler_transform_id") && !b["compiler_transform_id"].is_null()) binding["compiler_transform_id"] = b["compiler_transform_id"];
      if (actual_attrs.contains("model_origin")) binding["model_origin"] = actual_attrs["model_origin"];
      trace["result_bindings"].push_back(binding);
    }
    if (!actual_models.empty()) {
      // A provider may report a resolved alias/version after preparation. Keep
      // the requested recipe identity and observed model identity separately.
      trace["actual_models"] = actual_models;
      trace["model"] = actual_models.size() == 1 ? actual_models[0] : Json(nullptr);
      trace["actual_model"] = trace["model"];
      trace["model_origin"] = actual_origins.size() == 1 ? actual_origins[0] : Json(nullptr);
      trace["actual_model_origins"] = actual_origins;
      // The response identity in the method trace is the exact compiler input,
      // as witnessed by validated native result origins. Wire and rendered
      // response hashes remain separately captured observations.
      Json response_hashes = Json::array();
      bool all_response_hashes_known = true;
      for (const auto& actual_origin : actual_origins) {
        const auto response_hash = actual_origin.value("response_sha256", Json(nullptr));
        if (!digest(response_hash)) all_response_hashes_known = false;
        else if (std::find(response_hashes.begin(), response_hashes.end(), response_hash) == response_hashes.end()) response_hashes.push_back(response_hash);
      }
      trace["response_sha256"] = all_response_hashes_known && response_hashes.size() == 1 ? response_hashes[0] : Json(nullptr);
      trace["response_hash_scope"] = "compiler_input";
      if (rb.contains("response_sha256") && rb["response_sha256"] != trace["response_sha256"])
        return conflict("caller response identity differs from actual native compiler inputs");
      if (trace.contains("requested_model") && trace["requested_model"] != trace["model"] && b.contains("model_identity_id")) {
        trace["requested_model_identity_id"] = b["model_identity_id"];
        manifest["bindings"]["model_identity_id"] = nullptr;
        trace["model_identity_id"] = nullptr;
        trace["actual_model_identity_id"] = nullptr;
      }
      if (rb.contains("actual_model_identity_id") && !rb["actual_model_identity_id"].is_null()) {
        LOOM_TRY_ASSIGN(auto id, text(rb, "actual_model_identity_id"));
        if (!entities.contains(id) || entity_role(entities[id], v) != "model_identity") return unavailable("actual model descriptor native record missing");
        if (entities[id]["attrs"].contains("model") && entities[id]["attrs"]["model"] != trace["model"]) return conflict("actual model descriptor differs from result origin");
        manifest["bindings"]["model_identity_id"] = id;
        trace["model_identity_id"] = id;
        trace["actual_model_identity_id"] = id;
      }
      if (rb.contains("model_identity_basis")) trace["model_identity_basis"] = rb["model_identity_basis"];
    }
    LOOM_TRY_ASSIGN(auto final_definition_records, bound_definition_records(manifest["bindings"], entities));
    manifest["definition_records"] = final_definition_records;
    LOOM_TRY_ASSIGN(auto captured, source(json::canonical(trace), known, "method-run-trace.json"));
    Json rows{{"entities", Json::array()}, {"claims", Json::array()}, {"sources", Json::array({captured})}};
    LOOM_TRY_ASSIGN(auto captured_definitions, source(json::canonical(final_definition_records), known, "method-definition-records.json"));
    LOOM_TRY(add_row(rows, "sources", captured_definitions));
    if (rb.contains("request_bytes") || rb.contains("request")) {
      if (rb.contains("request_bytes") && !rb["request_bytes"].is_string()) return invalid("request_bytes must be exact text bytes");
      const auto bytes = rb.contains("request_bytes") ? rb["request_bytes"].get<std::string>() : json::canonical(rb["request"]);
      LOOM_TRY_ASSIGN(auto parsed, json::parse(bytes));
      const auto sent_hash = hash(parsed);
      if (rb.contains("expected_request_sha256") && (!digest(rb["expected_request_sha256"]) || rb["expected_request_sha256"] != sent_hash))
        return conflict("actual sent request differs from explicit expected request");
      LOOM_TRY_ASSIGN(auto captured_request, source(bytes, known, "method-request.json"));
      LOOM_TRY(add_row(rows, "sources", captured_request));
      trace["request_source_ref"] = captured_request["observation"]["id"];
      trace["request_bytes_sha256"] = captured_request["text_sha256"];
      trace["request_sha256"] = sent_hash;
      trace["request_hash_scope"] = "actual_sent_payload";
      // The completed trace captures those references as well.
      LOOM_TRY_ASSIGN(captured, source(json::canonical(trace), known, "method-run-trace.json"));
      rows["sources"][0] = captured;
    } else if (trace.contains("recipe_request_sha256")) {
      trace["request_source_status"] = "exact_sent_request_unavailable";
      LOOM_TRY_ASSIGN(captured, source(json::canonical(trace), known, "method-run-trace.json"));
      rows["sources"][0] = captured;
    }
    Json run = entities[run_id];
    // Prepared run snapshot remains in reversible packet history. Final trace
    // is a new immutable Observation, never a rewrite of the earlier capture.
    run["attrs"] = merge(run["attrs"], merge(trace, Json{{"projection_status", "response_projected"}}));
    LOOM_TRY(add_row(rows, "entities", run));
    for (const auto& id : result_ids) {
      for (const auto& pair : {std::pair<const char*, std::string>{"produced_in_run", run_id}, {"produced_by_method_version", version_id}}) {
        LOOM_TRY_ASSIGN(auto claim, structural(v, id, pair.first, pair.second, captured));
        LOOM_TRY(add_row(rows, "claims", claim));
      }
      if (b.contains("compiler_transform_id") && !b["compiler_transform_id"].is_null()) {
        const auto compiler = b["compiler_transform_id"].get<std::string>();
        if (!entities.contains(compiler) || entity_role(entities[compiler], v) != "compiler_transform") return unavailable("compiler binding native record missing");
        LOOM_TRY_ASSIGN(auto claim, structural(v, id, "projected_by_compiler", compiler, captured));
        LOOM_TRY(add_row(rows, "claims", claim));
      }
    }
    LOOM_TRY_ASSIGN(auto packet, project(candidate, rows, rb["origin"], known, "method-results:" + run_id + ":" + hash(trace), operation));
    return Json{{"packet", packet}, {"manifest", manifest}, {"canonical_store_written", false}};
  } catch (const std::exception& e) { return invalid(e.what()); }
}

Result<Json> MethodRegistry::accept(const Json& request) {
  try {
    auto lock = db_.lock();
    if (request.value("operation", "") != "accept") return invalid("accept requires the existing graph store acceptance command");
    // Native store CAS also protects mutable rows. Content-addressed definitions
    // and exact prompt bytes additionally cannot be replaced under one version ID.
    LOOM_TRY_ASSIGN(auto target, text(request, "target"));
    kb::KnowledgeStore store(db_);
    const auto run_id = kb::KnowledgeRun::make_id("loom.graph_packet_store/1", Json{{"graph_packet_target", target}});
    for (const auto& e : request.at("packet").at("entities")) {
      LOOM_TRY_ASSIGN(auto old, store.get_entity(run_id, e["id"].get<std::string>()));
      if (old) LOOM_TRY(immutable_attrs(old->attrs, e["attrs"]));
      if (!e["attrs"].contains("definition_sha256") && !e["attrs"].contains("text_sha256")) continue;
      const auto& attrs = e["attrs"];
      if (attrs.contains("definition_sha256") && (!attrs.contains("definition") || !digest(attrs["definition_sha256"]) || attrs["definition_sha256"] != hash(attrs["definition"])))
        return conflict("accepted definition/hash mismatch");
      if (attrs.contains("text_sha256") && (!attrs.contains("text") || !attrs["text"].is_string() || attrs["text_sha256"] != Sha256::hex(attrs["text"].get<std::string>())))
        return conflict("accepted prompt/hash mismatch");
    }
    return kb::GraphPacketStore(db_).execute(request);
  } catch (const std::exception& e) { return invalid(e.what()); }
}

}  // namespace loom::context
