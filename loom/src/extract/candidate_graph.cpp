#include "loom/knowledge_candidate_graph.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace loom::extract {
namespace {

using Records = std::map<std::string, const Json*>;
using Links = std::map<std::string, std::string>;
using Intervals = std::map<std::string, std::vector<std::pair<std::size_t, std::size_t>>>;

struct Rejected {
  std::string code, path, message;
};

void need(bool ok, std::string code, std::string path, std::string message) {
  if (!ok) throw Rejected{std::move(code), std::move(path), std::move(message)};
}

void exact(const Json& value, std::initializer_list<std::string_view> fields, const std::string& path) {
  need(value.is_object() && value.size() == fields.size(), "shape", path, "unexpected or missing fields");
  for (auto key : fields) need(value.contains(std::string(key)), "shape", path, "missing " + std::string(key));
}

std::string str(const Json& value) { return value.get<std::string>(); }

bool contains(const Json& array, const Json& value) {
  return array.is_array() && std::find(array.begin(), array.end(), value) != array.end();
}

// JSON has already been parsed by the caller. Bound its shape before recursive
// canonical serialization, copying, or graph traversal. This is a resource
// guard, not a replacement for exact schema validation below.
void preflight(const Json& root, std::size_t cap, const std::string& path) {
  std::vector<std::pair<const Json*, std::size_t>> todo{{&root, 0}};
  std::size_t nodes = 0, bytes = 0;
  while (!todo.empty()) {
    auto [j, depth] = todo.back();
    todo.pop_back();
    need(depth <= 128 && ++nodes <= cap, "budget", path, "JSON nesting or node budget exceeded");
    if (j->is_string()) {
      const auto& s = j->get_ref<const std::string&>();
      need(utf8::is_valid(s), "invalid_json_shape", path, "invalid UTF-8 string");
      bytes += s.size();
    }
    if (j->is_number_float()) need(std::isfinite(j->get<double>()), "invalid_json_shape", path, "non-finite JSON number");
    need(bytes <= cap, "budget", path, "JSON string byte budget exceeded");
    if (j->is_object() || j->is_array()) {
      need(j->size() <= cap - std::min(nodes, cap), "budget", path, "JSON member budget exceeded");
      for (auto it = j->begin(); it != j->end(); ++it) {
        if (j->is_object()) {
          need(utf8::is_valid(it.key()), "invalid_json_shape", path, "invalid UTF-8 object key");
          bytes += it.key().size();
        }
        todo.emplace_back(&it.value(), depth + 1);
      }
    }
  }
}

bool utf8_boundary(const std::string& s, std::size_t n) {
  return n == s.size() || (static_cast<unsigned char>(s[n]) & 0xc0) != 0x80;
}

void text(const Json& value, const std::string& path, bool empty = false) {
  need(value.is_string(), "text", path, "expected bounded string");
  const auto& s = value.get_ref<const std::string&>();
  need((empty || !utf8::is_blank(s)) && utf8::length(s) <= 4096, "text", path, "expected bounded nonempty string");
}

bool handle(const Json& value) {
  if (!value.is_string()) return false;
  const auto& s = value.get_ref<const std::string&>();
  if (s.size() < 2 || s.size() > 128 || s[0] != '@') return false;
  return std::all_of(s.begin() + 1, s.end(), [](unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '.' || c == ':' || c == '-';
  });
}

std::size_t union_size(std::vector<std::pair<std::size_t, std::size_t>> spans) {
  std::sort(spans.begin(), spans.end());
  std::size_t end = 0, total = 0;
  for (auto [start, stop] : spans) {
    if (stop > end) { total += stop - std::max(start, end); end = stop; }
  }
  return total;
}

std::set<std::string> keyset(const Links& records) {
  std::set<std::string> out;
  for (const auto& [key, unused] : records) { (void)unused; out.insert(key); }
  return out;
}

std::map<std::string, std::size_t> vocabulary_check(const Json& v) {
  preflight(v, 65536, "vocabulary");
  exact(v, {"schema", "status", "entity_kinds", "term_types", "scope_types", "assertion_contexts", "polarities", "predicates", "operations", "quantifier_kinds", "coverage_statuses", "limits", "policy"}, "vocabulary");
  need(v["schema"] == "loom.candidate_graph_vocabulary/1", "vocabulary", "schema", "wrong vocabulary schema");
  need(v["status"] == "research_candidate_contract_not_core_pack" || v["status"] == "experimental_unpromoted", "vocabulary", "status", "policy must remain experimental and unpromoted");
  auto names = [&](const Json& value, std::initializer_list<std::string_view> expected, const std::string& path) {
    need(value.is_array() && value.size() == expected.size(), "vocabulary", path, "unsupported vocabulary members");
    std::set<std::string> unique;
    for (const auto& name : value) {
      need(name.is_string() && unique.insert(str(name)).second && std::find(expected.begin(), expected.end(), str(name)) != expected.end(), "vocabulary", path, "unknown or duplicate vocabulary member");
    }
  };
  names(v["entity_kinds"], {"expression_occurrence", "term_occurrence", "binder", "scope"}, "entity_kinds");
  names(v["term_types"], {"constant", "variable", "predicate"}, "term_types");
  names(v["scope_types"], {"assertion", "quantifier", "quotation", "hypothesis"}, "scope_types");
  names(v["assertion_contexts"], {"asserted", "hypothetical", "quoted", "unknown"}, "assertion_contexts");
  names(v["polarities"], {"positive", "negative", "unknown"}, "polarities");
  names(v["predicates"], {"operation_type", "quantifier_kind", "operand", "in_scope", "scope_parent", "introduces_scope", "bound_to", "denotes"}, "predicates");
  names(v["quantifier_kinds"], {"forall", "exists"}, "quantifier_kinds");
  names(v["coverage_statuses"], {"represented", "partial", "unsupported", "ambiguous", "omitted"}, "coverage_statuses");
  // This checks that policy matches the supported interchange contract. Port
  // dispatch/cardinality below reads the supplied data, not a separate ontology.
  exact(v["operations"], {"predicate_application", "conditional", "negation", "conjunction", "quantifier"}, "operations");
  for (auto it = v["operations"].begin(); it != v["operations"].end(); ++it) exact(it.value(), {"ports"}, "operations." + it.key());
  const auto& ops = v["operations"];
  exact(ops["predicate_application"]["ports"], {"predicate", "argument"}, "predicate_application.ports");
  exact(ops["conditional"]["ports"], {"antecedent", "consequent"}, "conditional.ports");
  exact(ops["negation"]["ports"], {"body"}, "negation.ports");
  exact(ops["conjunction"]["ports"], {"member"}, "conjunction.ports");
  exact(ops["quantifier"]["ports"], {"binder", "body", "restriction"}, "quantifier.ports");
  auto port = [&](const std::string& op, const std::string& p, int lo, int hi, const std::string& target) {
    const auto& rule = ops[op]["ports"][p];
    exact(rule, {"min", "max", "target"}, op + "." + p);
    need(rule["min"].is_number_integer() && rule["max"].is_number_integer() && rule["min"] == lo && rule["max"] == hi && rule["target"] == target, "vocabulary", op + "." + p, "unsupported port contract");
  };
  port("predicate_application", "predicate", 1, 1, "predicate");
  port("predicate_application", "argument", 0, 64, "term");
  port("conditional", "antecedent", 1, 1, "expression_occurrence");
  port("conditional", "consequent", 1, 1, "expression_occurrence");
  port("negation", "body", 1, 1, "expression_occurrence");
  port("conjunction", "member", 2, 64, "expression_occurrence");
  port("quantifier", "binder", 1, 1, "binder");
  port("quantifier", "body", 1, 1, "expression_occurrence");
  port("quantifier", "restriction", 0, 1, "expression_occurrence");
  exact(v["policy"], {"source_offsets", "ordinal", "unknown", "conjunction_order", "promotion", "inference"}, "policy");
  for (auto it = v["policy"].begin(); it != v["policy"].end(); ++it) text(it.value(), "policy." + it.key());
  const std::map<std::string, std::size_t> ceilings{{"max_packet_bytes", 1048576}, {"max_bundle_bytes", 262144},
    {"max_observations", 128}, {"max_source_bytes", 262144}, {"max_entities", 256},
    {"max_claims", 1024}, {"max_support", 16}, {"max_depth", 32}};
  need(v["limits"].is_object() && v["limits"].size() == ceilings.size(), "vocabulary", "limits", "invalid policy limits");
  std::map<std::string, std::size_t> limits;
  for (const auto& [key, ceiling] : ceilings) {
    need(v["limits"].contains(key) && v["limits"][key].is_number_integer(), "vocabulary", key, "missing integer limit");
    const auto n = v["limits"][key].get<std::int64_t>();
    need(n > 0 && static_cast<std::uint64_t>(n) <= ceiling, "vocabulary", key, "policy limit exceeds native resource ceiling");
    limits[key] = static_cast<std::size_t>(n);
  }
  return limits;
}

class Validator {
 public:
  Validator(const Json& b, const Json& p, const Json& v, const Json& supplied)
      : bundle(b), packet(p), vocabulary(v) {
    limits = vocabulary_check(v);
    need(supplied.is_object(), "limits", "limits", "limits must be an object");
    for (auto it = supplied.begin(); it != supplied.end(); ++it) {
      need(limits.count(it.key()) && it.value().is_number_integer(), "limits", it.key(), "unknown or noninteger limit");
      auto n = it.value().get<std::int64_t>();
      need(n > 0 && static_cast<std::uint64_t>(n) <= limits.at(it.key()), "limits", it.key(), "limit must lower a named positive maximum");
      limits[it.key()] = static_cast<std::size_t>(n);
    }
  }

  void run() {
    for (auto item : {std::make_tuple(&packet, "source_packet", "max_packet_bytes"), std::make_tuple(&bundle, "bundle", "max_bundle_bytes")}) {
      const auto [value, name, cap] = item;
      preflight(*value, limits.at(cap), name);
      need(json::canonical(*value).size() <= limits.at(cap), "budget", name, std::string(cap) + " exceeded");
    }
    packet_check(); draft_check(); structure_check(); coverage_check();
  }

  Json coverage = Json{{"representation_status", "unknown"}, {"semantic_accuracy", nullptr}};

 private:
  const Json& bundle;
  const Json& packet;
  const Json& vocabulary;
  std::map<std::string, std::size_t> limits;
  Records observations, existing_entities, existing_claims, entities, claims;
  std::map<std::string, std::vector<const Json*>> edges;
  Links membership, parents, operations, binder_owner;

  void span(const Json& s, const std::string& path) {
    exact(s, {"observation", "byte_start", "byte_len", "quote"}, path);
    need(s["observation"].is_string() && observations.count(str(s["observation"])), "unknown_observation", path, "support outside source packet");
    need(s["byte_start"].is_number_integer() && s["byte_len"].is_number_integer() && s["quote"].is_string(), "span_bounds", path, "support needs exact UTF-8 byte span");
    const auto start = s["byte_start"].get<std::int64_t>(), length = s["byte_len"].get<std::int64_t>();
    const auto& source = (*observations.at(str(s["observation"]))) ["text"].get_ref<const std::string&>();
    need(start >= 0 && length > 0 && static_cast<std::uint64_t>(start) <= source.size() && static_cast<std::uint64_t>(length) <= source.size() - static_cast<std::size_t>(start), "span_bounds", path, "span exceeds observation");
    const auto begin = static_cast<std::size_t>(start), count = static_cast<std::size_t>(length);
    need(utf8_boundary(source, begin) && utf8_boundary(source, begin + count), "utf8_boundary", path, "span splits a UTF-8 code point");
    need(source.compare(begin, count, s["quote"].get_ref<const std::string&>()) == 0, "quote_mismatch", path, "quote differs from exact observation bytes");
  }
  void support(const Json& s, const std::string& path) {
    need(s.is_array() && !s.empty() && s.size() <= limits.at("max_support"), "support", path, "support must be a nonempty bounded list");
    for (std::size_t i = 0; i < s.size(); ++i) span(s[i], path + "[" + std::to_string(i) + "]");
  }
  void packet_check() {
    need(packet.is_object() && packet.value("schema", Json()) == "loom.source_packet/1", "packet_schema", "source_packet", "wrong packet schema");
    text(packet.at("snapshot_id"), "source_packet.snapshot_id");
    for (auto key : {"observations", "entities", "claims"}) need(packet.contains(key) && packet[key].is_array(), "packet_shape", key, "expected list");
    need(packet["observations"].size() <= limits.at("max_observations"), "budget", "observations", "too many observations");
    std::size_t source_bytes = 0;
    for (const auto& o : packet["observations"]) {
      need(o.is_object(), "packet_shape", "observation", "expected native observation");
      for (auto key : {"id", "unit", "text"}) need(o.contains(key) && o[key].is_string() && (std::string_view(key) == "text" || !str(o[key]).empty()), "packet_shape", key, "missing observation field");
      need(o.contains("locator") && o["locator"].is_object() && o["locator"].contains("source") && o["locator"]["source"].is_string() && !str(o["locator"]["source"]).empty(), "packet_locator", "observation.locator", "located source required");
      need(observations.emplace(str(o["id"]), &o).second, "duplicate_id", "observation.id", "duplicate observation ID");
      source_bytes += o["text"].get_ref<const std::string&>().size();
    }
    need(source_bytes <= limits.at("max_source_bytes"), "budget", "observations", "source byte budget exceeded");
    for (auto pair : {std::make_pair("entities", &existing_entities), std::make_pair("claims", &existing_claims)}) {
      for (const auto& record : packet[pair.first]) {
        need(record.is_object() && record.contains("id") && record["id"].is_string() && !str(record["id"]).empty() && str(record["id"])[0] != '@', "packet_id", pair.first, "existing record needs nonlocal ID");
        need(pair.second->emplace(str(record["id"]), &record).second, "duplicate_id", pair.first, "duplicate existing ID");
      }
    }
    for (const auto& [id, e] : existing_entities) {
      need(!existing_claims.count(id), "typed_namespace", id, "Entity and Claim IDs must not collide");
      text(e->at("kind"), "entities." + id + ".kind");
    }
    for (const auto& [id, c] : existing_claims) {
      need(c->contains("assessment") && (*c)["assessment"].is_object(), "missing_assessment", id, "existing Assessment must be retained");
      const auto& a = (*c)["assessment"];
      for (auto key : {"basis", "premises", "evidence_class", "origin", "confidence", "status"}) need(a.contains(key), "missing_assessment", id, "missing Assessment field");
      need(a["basis"].is_object() && a["premises"].is_object(), "assessment_shape", id, "basis/premises must be objects");
      need(contains(Json::array({"observed", "derived", "inferred", "extrapolated", "absent", "user"}), a["evidence_class"]) && contains(Json::array({"active", "contested", "superseded", "rejected"}), a["status"]), "assessment_shape", id, "invalid native evidence/status");
      need(a["confidence"].is_number() && a["confidence"].get<double>() >= 0 && a["confidence"].get<double>() <= 1, "assessment_shape", id, "native confidence required");
    }
  }

  void draft_check() {
    exact(bundle, {"schema", "packet_id", "entity_drafts", "claim_drafts", "roots", "coverage", "unknowns"}, "bundle");
    need(bundle["schema"] == "loom.candidate_graph/1" && bundle["packet_id"] == packet["snapshot_id"], "bundle_schema", "bundle", "schema or packet identity mismatch");
    for (auto key : {"entity_drafts", "claim_drafts", "roots", "coverage", "unknowns"}) need(bundle[key].is_array(), "shape", key, "expected list");
    need(bundle["entity_drafts"].size() <= limits.at("max_entities") && bundle["claim_drafts"].size() <= limits.at("max_claims"), "budget", "bundle", "draft count limit exceeded");
    std::set<std::string> handles;
    for (auto pair : {std::make_pair("entity_drafts", &entities), std::make_pair("claim_drafts", &claims)}) {
      for (const auto& d : bundle[pair.first]) {
        need(d.is_object() && d.contains("handle") && handle(d["handle"]), "handle", pair.first, "invalid local handle");
        auto h = str(d["handle"]);
        need(handles.insert(h).second, "duplicate_handle", h, "handles must be unique across namespaces");
        pair.second->emplace(h, &d);
      }
    }
    for (const auto& [h, ep] : entities) {
      const auto& e = *ep;
      exact(e, {"handle", "kind", "label", "attrs", "support"}, h);
      need(contains(vocabulary["entity_kinds"], e["kind"]), "entity_kind", h, "unsupported occurrence kind");
      text(e["label"], h + ".label", true); support(e["support"], h + ".support");
      const auto& a = e["attrs"];
      if (e["kind"] == "scope") { exact(a, {"scope_type", "assertion_context"}, h + ".attrs"); need(contains(vocabulary["scope_types"], a["scope_type"]) && contains(vocabulary["assertion_contexts"], a["assertion_context"]), "scope_type", h, "unsupported scope/context"); }
      else if (e["kind"] == "term_occurrence") { exact(a, {"term_type", "symbol"}, h + ".attrs"); need(contains(vocabulary["term_types"], a["term_type"]), "term_type", h, "unsupported term type"); text(a["symbol"], h + ".symbol"); }
      else if (e["kind"] == "binder") { exact(a, {"symbol"}, h + ".attrs"); text(a["symbol"], h + ".symbol"); }
      else { need(e["kind"] == "expression_occurrence", "entity_kind", h, "unsupported native occurrence handler"); exact(a, {}, h + ".attrs"); }
    }
    for (const auto& [h, cp] : claims) {
      const auto& c = *cp;
      exact(c, {"handle", "subject", "predicate", "object", "value", "qualifiers", "assessment"}, h);
      need(c["subject"].is_string() && c["object"].is_string(), "endpoint", h, "endpoints must be Entity references");
      need(entities.count(str(c["subject"])), "subject", h, "subject must be a local occurrence");
      need(contains(vocabulary["predicates"], c["predicate"]), "predicate", h, "unsupported structural predicate");
      const auto predicate = str(c["predicate"]), object = str(c["object"]);
      if (predicate == "operation_type" || predicate == "quantifier_kind") need(object.empty() && c["value"].is_string(), "literal", h, "type claims need string literals");
      else { need(entities.count(object) || existing_entities.count(object), "endpoint", h, "object is not an allowlisted Entity"); need(c["value"].is_null(), "object_value_xor", h, "entity edge must not have a literal"); }
      exact(c["qualifiers"], {"scope", "extra"}, h + ".qualifiers");
      const auto& q = c["qualifiers"];
      need(q["scope"].is_string() && entities.count(str(q["scope"])) && (*entities.at(str(q["scope"]))) ["kind"] == "scope", "scope", h, "qualifier needs local scope");
      const auto& x = q["extra"];
      if (predicate == "operand") exact(x, {"polarity", "assertion_context", "port", "ordinal"}, h + ".extra");
      else exact(x, {"polarity", "assertion_context"}, h + ".extra");
      need(contains(vocabulary["polarities"], x["polarity"]) && contains(vocabulary["assertion_contexts"], x["assertion_context"]), "polarity_context", h, "explicit polarity/context required");
      need(x["assertion_context"] == (*entities.at(str(q["scope"]))) ["attrs"]["assertion_context"], "context_mismatch", h, "claim context differs from scope");
      if (predicate == "operand") { text(x["port"], h + ".port"); need(x["ordinal"].is_number_integer() && x["ordinal"].get<std::int64_t>() >= 0, "ordinal", h, "ordinal must be nonnegative integer"); }
      exact(c["assessment"], {"basis", "premises"}, h + ".assessment");
      const auto& a = c["assessment"];
      exact(a["basis"], {"support"}, h + ".basis"); support(a["basis"]["support"], h + ".support");
      exact(a["premises"], {"claims"}, h + ".premises");
      need(a["premises"]["claims"].is_array(), "premises", h, "expected premise list");
      std::set<std::string> refs;
      for (const auto& ref : a["premises"]["claims"]) {
        need(ref.is_string() && refs.insert(str(ref)).second, "premises", h, "premise IDs must be distinct");
        need(existing_claims.count(str(ref)), "premise_reference", h, "premise outside allowlist");
        const auto& prior = (*existing_claims.at(str(ref))) ["assessment"];
        need(prior["evidence_class"] != "absent" && prior["evidence_class"] != "extrapolated" && prior["status"] != "rejected" && prior["status"] != "superseded", "premise_ineligible", h, "ineligible native premise");
      }
      edges[predicate].push_back(cp);
    }
  }

  std::string kind(const std::string& ref) const { return entities.count(ref) ? str((*entities.at(ref))["kind"]) : "existing"; }
  Links single_map(const std::string& predicate) {
    Links out;
    for (const auto* c : edges[predicate]) need(out.emplace(str((*c)["subject"]), str((*c)["object"])).second, "cardinality", str((*c)["handle"]), "duplicate " + predicate);
    return out;
  }
  std::vector<std::string> ancestors(std::string scope) const {
    std::vector<std::string> chain;
    std::set<std::string> seen;
    while (!scope.empty()) {
      need(seen.insert(scope).second, "scope_cycle", scope, "scope parent cycle");
      chain.push_back(scope);
      need(chain.size() <= limits.at("max_depth"), "depth", scope, "scope depth limit");
      auto it = parents.find(scope); scope = it == parents.end() ? "" : it->second;
    }
    return chain;
  }
  void structure_check() {
    std::set<std::string> scopes, expressions, nonscopes;
    for (const auto& [h, e] : entities) {
      if ((*e)["kind"] == "scope") scopes.insert(h); else nonscopes.insert(h);
      if ((*e)["kind"] == "expression_occurrence") expressions.insert(h);
    }
    membership = single_map("in_scope"); parents = single_map("scope_parent");
    need(keyset(membership) == nonscopes, "scope_membership", "in_scope", "every non-scope occurrence needs one scope");
    for (const auto& [h, s] : membership) need(scopes.count(s), "scope_membership", h, "membership must target scope");
    for (const auto& [child, parent] : parents) need(scopes.count(child) && scopes.count(parent), "scope_parent", child, "parents must be scopes");
    for (const auto& s : scopes) ancestors(s);
    for (const auto& [h, cp] : claims) {
      const auto& c = *cp;
      const auto s = str(c["subject"]), p = str(c["predicate"]), o = str(c["object"]);
      const auto expected = p == "in_scope" ? o : p == "scope_parent" ? s : membership.count(s) ? membership.at(s) : "";
      need(c["qualifiers"]["scope"] == expected, "qualifier_scope", h, "wrong occurrence scope");
      if (p == "operation_type") need(expressions.count(s) && vocabulary["operations"].contains(str(c["value"])) && operations.emplace(s, str(c["value"])).second, "operation_type", h, "expression needs one supported operation type");
    }
    need(keyset(operations) == expressions, "operation_type", "expressions", "every expression needs operation_type");
    std::set<std::string> quantifiers, qkinds;
    for (const auto& [h, op] : operations) if (op == "quantifier") quantifiers.insert(h);
    for (const auto* c : edges["quantifier_kind"]) {
      auto h = str((*c)["subject"]);
      need(quantifiers.count(h) && qkinds.insert(h).second && contains(vocabulary["quantifier_kinds"], (*c)["value"]), "quantifier_kind", str((*c)["handle"]), "quantifier needs one supported kind");
    }
    need(qkinds == quantifiers, "quantifier_kind", "quantifiers", "quantifier kind missing");
    const auto introduced = single_map("introduces_scope");
    need(keyset(introduced) == quantifiers, "quantifier_scope", "introduces_scope", "each quantifier needs one scope");
    std::set<std::string> owned_scopes;
    for (const auto& [q, s] : introduced) need(scopes.count(s) && owned_scopes.insert(s).second && (*entities.at(s))["attrs"]["scope_type"] == "quantifier" && parents.count(s) && parents.at(s) == membership.at(q), "quantifier_scope", q, "quantifier must own distinct child scope");
    for (const auto& s : scopes) need(((*entities.at(s))["attrs"]["scope_type"] == "quantifier") == static_cast<bool>(owned_scopes.count(s)), "quantifier_scope", s, "unowned quantifier scope");
    std::map<std::string, std::map<std::string, std::vector<std::pair<std::int64_t, std::string>>>> operands;
    std::map<std::string, std::vector<std::string>> syntax;
    std::set<std::string> used;
    for (const auto* cp : edges["operand"]) {
      const auto& c = *cp;
      const auto s = str(c["subject"]), o = str(c["object"]), h = str(c["handle"]);
      const auto& x = c["qualifiers"]["extra"];
      need(expressions.count(s), "operand_subject", h, "operand subject must be expression");
      const auto& ports = vocabulary["operations"][operations.at(s)]["ports"];
      const auto port = str(x["port"]);
      need(ports.contains(port), "operand_port", h, "unsupported operand port");
      const auto target = str(ports[port]["target"]), actual = kind(o);
      bool ok = actual == target;
      if (target == "predicate") ok = actual == "existing" || (actual == "term_occurrence" && (*entities.at(o))["attrs"]["term_type"] == "predicate");
      if (target == "term") ok = actual == "existing" || (actual == "term_occurrence" && contains(Json::array({"constant", "variable"}), (*entities.at(o))["attrs"]["term_type"]));
      need(ok, "operand_type", h, "operand target has wrong kind");
      operands[s][port].emplace_back(x["ordinal"].get<std::int64_t>(), o); used.insert(o);
      if (expressions.count(o)) syntax[s].push_back(o);
      if (entities.count(o)) {
        const auto source_scope = membership.at(s), target_scope = membership.at(o);
        if (operations.at(s) == "quantifier") need(target_scope == introduced.at(s), "operand_scope", h, "quantifier operand outside child scope");
        else {
          const bool allowed_child = parents.count(target_scope) && parents.at(target_scope) == source_scope && contains(Json::array({"quotation", "hypothesis"}), (*entities.at(target_scope))["attrs"]["scope_type"]);
          need(target_scope == source_scope || allowed_child, "operand_scope", h, "operand escapes or crosses scope");
        }
      }
    }
    for (const auto& [h, op] : operations) {
      const auto& ports = vocabulary["operations"][op]["ports"];
      for (auto it = ports.begin(); it != ports.end(); ++it) {
        auto& entries = operands[h][it.key()]; std::sort(entries.begin(), entries.end());
        need(entries.size() >= it.value()["min"].get<std::size_t>() && entries.size() <= it.value()["max"].get<std::size_t>(), "operand_cardinality", h + "." + it.key(), "missing or excessive operand");
        for (std::size_t i = 0; i < entries.size(); ++i) need(entries[i].first == static_cast<std::int64_t>(i), "operand_cardinality", h + "." + it.key(), "noncontiguous or duplicate ordinal");
      }
      if (op == "quantifier") {
        need(operands[h].count("binder") && operands[h]["binder"].size() == 1, "vocabulary", "quantifier", "quantifier policy needs one binder");
        auto binder = operands[h]["binder"][0].second;
        need(binder_owner.emplace(binder, h).second, "binder_owner", binder, "binder belongs to two quantifiers");
      }
    }
    std::set<std::string> binders, variables;
    for (const auto& [h, e] : entities) {
      if ((*e)["kind"] == "binder") binders.insert(h);
      if ((*e)["kind"] == "term_occurrence" && (*e)["attrs"]["term_type"] == "variable") variables.insert(h);
    }
    need(binders == keyset(binder_owner), "binder_owner", "binders", "every binder must be owned");
    const auto bound = single_map("bound_to");
    need(keyset(bound) == variables, "binding", "bound_to", "every variable needs one binder");
    for (const auto& [v, b] : bound) need(binders.count(b), "binding", v, "bound reference must target binder");
    std::map<std::pair<std::string, std::string>, std::string> symbols;
    for (const auto& b : binders) need(symbols.emplace(std::make_pair(membership.at(b), str((*entities.at(b))["attrs"]["symbol"])), b).second, "binding_ambiguity", b, "same-name binders in one scope");
    for (const auto& [v, b] : bound) {
      std::string nearest;
      const auto symbol = str((*entities.at(v))["attrs"]["symbol"]);
      for (const auto& s : ancestors(membership.at(v))) { auto it = symbols.find({s, symbol}); if (it != symbols.end()) { nearest = it->second; break; } }
      need(nearest == b, "binding_capture", v, "reference escapes, captures or skips nearest binder");
    }
    for (const auto& [term, entity] : single_map("denotes")) need(kind(term) == "term_occurrence" && !variables.count(term) && existing_entities.count(entity), "denotes", term, "denotes needs nonvariable term and native Entity");
    std::set<std::string> roots;
    for (const auto& r : bundle["roots"]) need(r.is_string() && expressions.count(str(r)) && roots.insert(str(r)).second, "roots", "roots", "roots must be distinct expressions");
    std::set<std::string> visited, active;
    std::map<std::string, std::size_t> heights;
    std::function<std::size_t(const std::string&)> walk = [&](const std::string& node) -> std::size_t {
      need(!active.count(node), "syntax_cycle", node, "expression operand cycle");
      need(active.size() < limits.at("max_depth"), "depth", node, "expression depth limit");
      if (heights.count(node)) { need(active.size() + heights.at(node) <= limits.at("max_depth"), "depth", node, "expression depth limit"); return heights.at(node); }
      visited.insert(node); active.insert(node);
      std::size_t height = 1;
      for (const auto& child : syntax[node]) height = std::max(height, 1 + walk(child));
      active.erase(node); heights[node] = height;
      need(height <= limits.at("max_depth"), "depth", node, "expression depth limit");
      return height;
    };
    for (const auto& root : roots) walk(root);
    need(visited == expressions, "unreachable_expression", "roots", "every expression must be reachable");
    for (const auto& h : nonscopes) need(expressions.count(h) || used.count(h), "unused_occurrence", h, "orphan term/binder");
    std::set<std::string> used_scopes;
    for (const auto& [h, s] : membership) { (void)h; for (const auto& a : ancestors(s)) used_scopes.insert(a); }
    need(used_scopes == scopes, "unused_scope", "scopes", "orphan scope");
  }

  void coverage_check() {
    std::map<std::string, Intervals> by_status;
    for (const auto& s : vocabulary["coverage_statuses"]) by_status[str(s)] = {};
    Intervals covered, unknown;
    auto add = [](Intervals& target, const Json& s) {
      auto start = s["byte_start"].get<std::size_t>();
      target[str(s["observation"])].emplace_back(start, start + s["byte_len"].get<std::size_t>());
    };
    for (std::size_t i = 0; i < bundle["coverage"].size(); ++i) {
      const auto& row = bundle["coverage"][i]; const auto path = "coverage[" + std::to_string(i) + "]";
      exact(row, {"support", "status", "reason", "drafts"}, path);
      support(row["support"], path + ".support"); text(row["reason"], path + ".reason");
      need(row["status"].is_string() && by_status.count(str(row["status"])), "coverage_status", path, "unsupported coverage status");
      need(row["drafts"].is_array(), "coverage_reference", path, "drafts must be list");
      for (const auto& h : row["drafts"]) need(h.is_string() && (entities.count(str(h)) || claims.count(str(h))), "coverage_reference", path, "coverage points outside drafts");
      need(row["status"] != "represented" || !row["drafts"].empty(), "coverage_reference", path, "represented coverage needs draft references");
      for (const auto& s : row["support"]) { add(by_status.at(str(row["status"])), s); add(covered, s); }
    }
    for (std::size_t i = 0; i < bundle["unknowns"].size(); ++i) {
      const auto& row = bundle["unknowns"][i]; const auto path = "unknowns[" + std::to_string(i) + "]";
      exact(row, {"support", "reason"}, path); support(row["support"], path + ".support"); text(row["reason"], path + ".reason");
      for (const auto& s : row["support"]) add(unknown, s);
    }
    std::size_t total = 0, covered_bytes = 0, unknown_bytes = 0;
    Json counts = Json::object(), gaps = Json::array();
    bool other = false;
    for (const auto& [status, refs] : by_status) {
      std::size_t count = 0; for (const auto& [id, spans] : refs) { (void)id; count += union_size(spans); }
      counts[status] = count; if (status != "represented" && count) other = true;
    }
    for (const auto& [id, spans] : covered) { (void)id; covered_bytes += union_size(spans); }
    for (const auto& [id, spans] : unknown) { (void)id; unknown_bytes += union_size(spans); }
    for (const auto& [id, o] : observations) {
      const auto& raw = (*o)["text"].get_ref<const std::string&>(); total += raw.size();
      std::size_t end = 0; auto spans = covered[id]; std::sort(spans.begin(), spans.end());
      auto gap = [&](std::size_t start, std::size_t stop) { gaps.push_back(Json{{"observation", id}, {"byte_start", start}, {"byte_len", stop - start}, {"quote", raw.substr(start, stop - start)}}); };
      for (const auto& [start, stop] : spans) { if (start > end) gap(end, start); end = std::max(end, stop); }
      if (end < raw.size()) gap(end, raw.size());
    }
    const auto represented = counts.value("represented", std::size_t(0));
    const bool complete = covered_bytes == total && represented == total && unknown_bytes == 0 && !other;
    coverage = Json{{"source_observations", observations.size()}, {"source_bytes", total}, {"by_status_bytes", counts},
      {"represented_bytes", represented}, {"uncovered_bytes", total - covered_bytes}, {"uncovered_spans", gaps},
      {"unknown_bytes", unknown_bytes}, {"representation_status", entities.empty() ? "unrepresented" : complete ? "complete_declared" : "partial"},
      {"semantic_accuracy", nullptr}, {"source_status_rows", bundle["coverage"]}, {"located_unknowns", bundle["unknowns"]}};
  }
};

}  // namespace

Json validate_candidate_graph_vocabulary(const Json& vocabulary) {
  Json report{{"version", std::string(kCandidateGraphValidatorVersion)}, {"valid", false},
              {"status", "rejected"}, {"errors", Json::array()}};
  try {
    vocabulary_check(vocabulary);
    report["valid"] = true; report["status"] = "valid";
  } catch (const Rejected& e) {
    report["errors"].push_back(Json{{"code", e.code}, {"path", e.path}, {"message", e.message}});
  } catch (const Json::exception& e) {
    report["errors"].push_back(Json{{"code", "invalid_json_shape"}, {"path", "vocabulary"}, {"message", e.what()}});
  }
  return report;
}

Json validate_candidate_graph_bundle(const Json& bundle, const Json& source_packet,
                                     const Json& vocabulary, const Json& limits) {
  Json report{{"version", std::string(kCandidateGraphValidatorVersion)}, {"valid", false}, {"status", "rejected"},
    {"errors", Json::array()}, {"coverage", Json{{"representation_status", "unknown"}, {"semantic_accuracy", nullptr}}},
    {"packet_hash", nullptr}, {"hash_algorithm", "loom-json-canonical-sha256"},
    {"drafts", Json{{"entities", Json::array()}, {"claims", Json::array()}}}, {"no_inference", true}, {"no_persistence", true}};
  bool safe_to_copy = false;
  try {
    // This guard must precede vocabulary/limit validation: even an unrelated
    // policy rejection must not cause an unchecked deep input copy below.
    preflight(bundle, 262144, "bundle");
    preflight(source_packet, 1048576, "source_packet");
    safe_to_copy = true;
    Validator validator(bundle, source_packet, vocabulary, limits);
    validator.run();
    report["packet_hash"] = Sha256::hex(json::canonical(source_packet));
    report["vocabulary_hash"] = Sha256::hex(json::canonical(vocabulary));
    report["coverage"] = validator.coverage;
    report["valid"] = true; report["status"] = "valid";
    report["drafts"] = Json{{"entities", bundle["entity_drafts"]}, {"claims", bundle["claim_drafts"]}};
  } catch (const Rejected& e) {
    report["errors"].push_back(Json{{"code", e.code}, {"path", e.path}, {"message", e.message}});
  } catch (const Json::exception& e) {
    report["errors"].push_back(Json{{"code", "invalid_json_shape"}, {"path", "input"}, {"message", e.what()}});
  } catch (const std::out_of_range& e) {
    report["errors"].push_back(Json{{"code", "invalid_json_shape"}, {"path", "input"}, {"message", e.what()}});
  }
  if (safe_to_copy) {
    report["retained_input"] = Json{{"bundle", bundle}, {"source_packet", source_packet}};
  } else {
    report["retained_input"] = nullptr;
    report["retention"] = Json{{"status", "not_copied"}, {"reason", "input_preflight_rejection"},
                               {"input_ownership", "caller"}};
  }
  return report;
}

}  // namespace loom::extract
