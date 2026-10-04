#include "packet.h"

#include <charconv>
#include <cmath>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
#include <vector>

#include "loom/model.h"
#include "loom/util/base64.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"
namespace loom::packet {
namespace {
using Index = std::map<std::string, Json>;
using Indices = std::map<std::string, Index>;
const char* const collections[] = {"definitions", "entities", "claims", "sources"};
[[noreturn]] void fail(const char* code) { throw std::invalid_argument(code); }
std::string str(const Json& j) {
  text(j);
  return j.get<std::string>();
}
void array(const Json& j) {
  if (!j.is_array()) fail("graph_packet_array_required");
}
void object(const Json& j) {
  if (!j.is_object()) fail("graph_packet_object_required");
}
void texts(const Json& j) {
  array(j);
  for (const auto& v : j) text(v, true);
}
void integer(const Json& j, bool nonnegative = false) {
  if (!j.is_number_integer() || (nonnegative && !j.is_number_unsigned() && j.get<std::int64_t>() < 0))
    fail("graph_packet_integer_invalid");
}
void number(const Json& j, bool unit = false) {
  if (!j.is_number() || !std::isfinite(j.get<double>()) ||
      (unit && (j.get<double>() < 0 || j.get<double>() > 1)))
    fail("graph_packet_number_invalid");
}
bool same(const Json& a, const Json& b) {
  // sorted canonical bytes bind numeric representation as well as value;
  // native shape checks allow int/float equivalence only when exactly representable.
  if (a.is_object() && b.is_object()) {
    if (a.size() != b.size()) return false;
    for (auto i = a.begin(); i != a.end(); ++i)
      if (!b.contains(i.key()) || !same(i.value(), b[i.key()])) return false;
    return true;
  }
  if (a.is_array() && b.is_array()) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
      if (!same(a[i], b[i])) return false;
    return true;
  }
  if (a.is_number() && b.is_number()) {
    if (a.is_number_float() != b.is_number_float()) {
      const auto& n = a.is_number_float() ? b : a;
      double f = (a.is_number_float() ? a : b).get<double>();
      if (!std::isfinite(f) || std::trunc(f) != f) return false;
      if (n.is_number_unsigned())
        return f >= 0 && f < std::ldexp(1.0, 64) && static_cast<std::uint64_t>(f) == n.get<std::uint64_t>();
      return f >= -std::ldexp(1.0, 63) && f < std::ldexp(1.0, 63) &&
             static_cast<std::int64_t>(f) == n.get<std::int64_t>();
    }
    if (a.is_number_integer() && b.is_number_integer() && a.is_number_unsigned() != b.is_number_unsigned()) {
      const auto& s = a.is_number_unsigned() ? b : a;
      const auto& u = a.is_number_unsigned() ? a : b;
      return s.get<std::int64_t>() >= 0 &&
             static_cast<std::uint64_t>(s.get<std::int64_t>()) == u.get<std::uint64_t>();
    }
  }
  return a == b;
}
std::string id(const char* name, const Json& record) {
  return str(std::string_view(name) == "sources" ? record.at("observation").at("id") : record.at("id"));
}
Indices indexes(const Json& p) {
  Indices out;
  for (auto name : collections) {
    out[name];
    for (const auto& r : p.at(name)) out[name].emplace(id(name, r), r);
  }
  return out;
}
Json payload(Json j, const char* key) {
  j.erase(key);
  return j;
}
void locator(const Json& j) {
  shape(j, {"source", "member", "json_pointer", "byte_start", "byte_len", "time_start", "time_end", "line"});
  for (auto k : {"source", "member", "json_pointer"}) text(j[k], true);
  for (auto k : {"byte_start", "byte_len", "line"})
    if (!j[k].is_null()) integer(j[k], true);
  for (auto k : {"time_start", "time_end"})
    if (!j[k].is_null()) number(j[k]);
  if (!j["time_start"].is_null() && !j["time_end"].is_null() &&
      j["time_end"].get<double>() < j["time_start"].get<double>())
    fail("native_locator_time_order_invalid");
}
template <class T>
void native(const Json& j) {
  auto result = T::from_json(j);
  if (!result) fail("graph_packet_native_record_invalid");
  if (!same(j, result->to_json())) fail("graph_packet_native_record_shape_invalid");
}
void record(const char* name, const Json& r) {
  if (std::string_view(name) == "definitions") {
    shape(r, {"id", "kind", "description", "examples", "origin", "attrs"});
    text(r["id"]);
    text(r["kind"]);
    text(r["description"], true);
    array(r["examples"]);
    object(r["attrs"]);
    origin(r["origin"]);
  } else if (std::string_view(name) == "entities") {
    native<model::Entity>(r);
    text(r["id"]);
    text(r["kind"]);
    text(r["canonical_key"]);
    number(r["confidence"], true);
    for (const auto& a : r["aliases"]) {
      integer(a["count"]);
      number(a["confidence"], true);
      text(a["key"]);
    }
  } else if (std::string_view(name) == "sources") {
    shape(r, {"observation", "known_at", "text_sha256"});
    native<model::Observation>(r["observation"]);
    const auto& o = r["observation"];
    text(o["id"]);
    text(o["unit"]);
    text(o["locator"]["source"]);
    locator(o["locator"]);
    integer(o["ordinal"]);
    timestamp(r["known_at"]);
    if (r["text_sha256"] != Sha256::hex(o["text"].get<std::string>())) fail("source_text_hash_drift");
  } else {
    native<model::Claim>(r);
    text(r["id"]);
    text(r["subject"]);
    text(r["predicate"]);
    auto c = model::Claim::from_json(r);
    if (!c->validate()) fail("graph_packet_native_claim_invalid");
    const auto& a = r["assessment"];
    number(a["confidence"], true);
    for (const auto& s : a["basis"]["support"]) {
      text(s["observation"]);
      locator(s["locator"]);
      number(s["quality"], true);
    }
    if (!a["basis"]["derivation"].is_null()) {
      const auto& d = a["basis"]["derivation"];
      text(d["operator"]);
      integer(d["depth"], true);
      integer(d["operator_version"], true);
      if (d["operator_version"] == 0) fail("native_derivation_numbers_invalid");
    }
    for (auto k : {"premises", "counter", "consequences"})
      for (const auto& v : a[k]) texts(v);
    texts(a["open"]["slots"]);
    texts(a["open"]["questions"]);
    for (const auto& alt : a["alternatives"]) {
      number(alt["score"]);
      if (alt["object"] == "" && alt["value"].is_null()) fail("native_alternative_object_or_value_required");
    }
  }
}
void metadata(const Json& m, const Json& r) {
  shape(m, {"known_at", "origin", "record_sha256"});
  timestamp(m["known_at"]);
  origin(m["origin"]);
  if (m["record_sha256"] != digest(r)) fail("graph_packet_record_provenance_hash_drift");
}
void content(const Json& p) {
  shape(p, {"schema", "packet_id", "definitions", "entities", "claims", "sources", "task", "provenance",
            "history"});
  if (p["schema"] != "loom.graph_packet/1" || p["packet_id"] != digest(payload(p, "packet_id")))
    fail("graph_packet_content_identity_invalid");
  object(p["task"]);
  array(p["history"]);
  shape(p["provenance"], {"definitions", "entities", "claims", "sources"});
  std::set<std::string> ids;
  for (auto name : collections) {
    array(p[name]);
    object(p["provenance"][name]);
    for (const auto& r : p[name]) {
      record(name, r);
      auto key = id(name, r);
      if (!ids.insert(key).second) fail("graph_packet_duplicate_or_colliding_record_id");
      if (!p["provenance"][name].contains(key)) fail("graph_packet_record_provenance_missing");
      metadata(p["provenance"][name][key], r);
    }
    if (p["provenance"][name].size() != p[name].size()) fail("graph_packet_extra_provenance_id");
  }
  const auto ix = indexes(p);
  const auto& entities = ix.at("entities");
  const auto& claims = ix.at("claims");
  const auto& sources = ix.at("sources");
  for (const auto& [key, e] : entities)
    if (e["parent"] != "" && !entities.count(e["parent"].get<std::string>()))
      fail("graph_packet_unknown_entity_parent");
  for (const auto& [key, c] : claims) {
    if (!entities.count(c["subject"].get<std::string>()) ||
        (c["object"] != "" && !entities.count(c["object"].get<std::string>())))
      fail("graph_packet_unknown_claim_entity");
    const auto& a = c["assessment"];
    for (auto k : {"premises", "counter", "consequences"})
      for (const auto& ref : a[k]["claims"])
        if (!claims.count(ref.get<std::string>())) fail("graph_packet_unknown_claim_reference");
    for (const auto& s : a["basis"]["support"]) {
      auto it = sources.find(s["observation"].get<std::string>());
      if (it == sources.end()) fail("graph_packet_missing_support_observation");
      const auto& o = it->second["observation"];
      auto quote = s["quote"].get<std::string>();
      auto raw = o["text"].get<std::string>();
      if (quote.empty() || raw.find(quote) == std::string::npos) fail("graph_packet_support_quote_mismatch");
      const auto& l = s["locator"];
      const auto& orig = o["locator"];
      if (!same(l, orig)) {
        for (auto k : {"source", "member", "json_pointer", "time_start", "time_end"})
          if (!same(l[k], orig[k])) fail("graph_packet_support_locator_mismatch");
        if (orig["byte_start"].is_null() || l["byte_start"].is_null() || l["byte_len"].is_null())
          fail("graph_packet_support_subspan_unverifiable");
        auto start = l["byte_start"].get<std::uint64_t>(), base = orig["byte_start"].get<std::uint64_t>(),
             len = l["byte_len"].get<std::uint64_t>();
        if (start < base || start - base > raw.size() || len > raw.size() - (start - base) ||
            raw.substr(start - base, len) != quote)
          fail("graph_packet_support_subspan_quote_mismatch");
      }
    }
    for (const auto& ref : a["premises"]["claims"]) {
      const auto& premise = claims.at(ref.get<std::string>())["assessment"];
      if (premise["evidence_class"] == "absent" || premise["evidence_class"] == "extrapolated")
        fail("native_absent_or_extrapolated_premise_forbidden");
      const auto& d = a["basis"]["derivation"];
      const auto& pd = premise["basis"]["derivation"];
      if (!d.is_null() && !pd.is_null() && d["morphism"] != "" && pd["morphism"] != "")
        fail("native_transfer_chain_forbidden");
    }
  }
}
void diff_structure(const Json& p, const Json& d) {
  shape(d, {"schema", "base_packet_sha256", "proposal_id", "origin", "known_at", "definitions", "entities",
            "claims", "sources", "task", "annotations"});
  if (d["schema"] != "loom.graph_packet_diff/1" || d["base_packet_sha256"] != p["packet_id"])
    fail("graph_diff_stale_or_incompatible_base");
  text(d["proposal_id"]);
  origin(d["origin"]);
  timestamp(d["known_at"]);
  auto ix = indexes(p);
  for (auto name : collections) {
    shape(d[name], {"add", "update", "remove"});
    std::set<std::string> touched;
    for (auto action : {"add", "update", "remove"}) {
      array(d[name][action]);
      for (const auto& e : d[name][action]) {
        std::string ident;
        if (std::string_view(action) == "add") {
          record(name, e);
          ident = id(name, e);
          if (ix[name].count(ident)) fail("graph_diff_add_existing_record");
        } else {
          if (std::string_view(action) == "update")
            shape(e, {"id", "before_sha256", "after"});
          else
            shape(e, {"id", "before_sha256", "reason"});
          ident = str(e["id"]);
          if (!ix[name].count(ident) || e["before_sha256"] != digest(ix[name].at(ident)))
            fail("graph_diff_record_compare_and_swap_failed");
          if (std::string_view(action) == "remove")
            text(e["reason"]);
          else {
            const auto& after = e["after"];
            const auto& before = ix[name].at(ident);
            record(name, after);
            if (id(name, after) != ident) fail("graph_diff_update_cannot_change_record_id");
            if (std::string_view(name) == "sources" && !same(after, before))
              fail("graph_diff_raw_source_update_requires_new_record");
            if (std::string_view(name) == "entities")
              for (auto k : {"kind", "canonical_key"})
                if (!same(after[k], before[k]))
                  fail("graph_diff_entity_identity_change_requires_new_record_id");
            if (std::string_view(name) == "claims")
              for (auto k : {"subject", "predicate", "object", "value", "qualifiers"})
                if (!same(after[k], before[k]))
                  fail("graph_diff_claim_content_change_requires_new_record_id");
          }
        }
        if (!touched.insert(ident).second) fail("graph_diff_duplicate_record_edit");
      }
    }
  }
  if (!d["task"].is_null()) {
    shape(d["task"], {"before_sha256", "after"});
    object(d["task"]["after"]);
    if (d["task"]["before_sha256"] != digest(p["task"])) fail("graph_diff_task_compare_and_swap_failed");
  }
  shape(d["annotations"], {"critique", "questions", "operations"});
  for (const auto& v : d["annotations"]) array(v);
  for (const auto& op : d["annotations"]["operations"]) {
    shape(op, {"kind", "inputs", "outputs", "reason"});
    text(op["kind"]);
    texts(op["inputs"]);
    texts(op["outputs"]);
    text(op["reason"], true);
  }
}
std::pair<Json, Json> candidate(const Json& p, const Json& d) {
  Json result = p, prov = p["provenance"], changes = Json::array();
  for (auto name : collections) {
    auto ix = indexes(p).at(name);
    std::vector<std::string> order;
    for (const auto& r : p[name]) order.push_back(id(name, r));
    for (auto action : {"add", "update", "remove"})
      for (const auto& e : d[name][action]) {
        auto ident = std::string_view(action) == "add" ? id(name, e) : str(e["id"]);
        Json before = ix.count(ident) ? ix.at(ident) : Json(nullptr),
             bp = prov[name].contains(ident) ? prov[name][ident] : Json(nullptr);
        Json after = std::string_view(action) == "add"      ? e
                     : std::string_view(action) == "update" ? e["after"]
                                                            : Json(nullptr),
             ap = nullptr;
        if (after.is_null()) {
          ix.erase(ident);
          prov[name].erase(ident);
        } else {
          if (!ix.count(ident)) order.push_back(ident);
          ix[ident] = after;
          ap = Json{{"known_at", std::string_view(name) == "sources" ? after["known_at"] : d["known_at"]},
                    {"origin", d["origin"]},
                    {"record_sha256", digest(after)}};
          prov[name][ident] = ap;
        }
        changes.push_back(Json{{"collection", name},
                               {"action", action},
                               {"record_id", ident},
                               {"before", before},
                               {"after", after},
                               {"before_provenance", bp},
                               {"after_provenance", ap}});
      }
    result[name] = Json::array();
    for (const auto& ident : order)
      if (ix.count(ident)) result[name].push_back(ix.at(ident));
  }
  result["provenance"] = prov;
  if (!d["task"].is_null()) result["task"] = d["task"]["after"];
  Json order = Json::object();
  for (auto name : collections) {
    order[name] = Json::array();
    for (const auto& r : p[name]) order[name].push_back(id(name, r));
  }
  Json event = {{"base_packet_id", p["packet_id"]},
                {"diff", d},
                {"changes", changes},
                {"previous_task", p["task"]},
                {"previous_order", order},
                {"result_origin", d["origin"]}};
  event["application_id"] = digest(event);
  result["history"].push_back(event);
  result["packet_id"] = digest(payload(result, "packet_id"));
  return {result, event};
}
void history(const Json& p) {
  Json current = p;
  while (!current["history"].empty()) {
    const Json event = current["history"].back();
    shape(event, {"application_id", "base_packet_id", "diff", "changes", "previous_task", "previous_order",
                  "result_origin"});
    if (event["application_id"] != digest(payload(event, "application_id")))
      fail("graph_packet_history_hash_drift");
    origin(event["result_origin"]);
    shape(event["previous_order"], {"definitions", "entities", "claims", "sources"});
    array(event["changes"]);
    object(event["previous_task"]);
    auto ix = indexes(current);
    auto prov = current["provenance"];
    std::set<std::pair<std::string, std::string>> touched;
    for (auto it = event["changes"].rbegin(); it != event["changes"].rend(); ++it) {
      const auto& c = *it;
      shape(c, {"collection", "action", "record_id", "before", "after", "before_provenance",
                "after_provenance"});
      auto name = str(c["collection"]), ident = str(c["record_id"]), action = str(c["action"]);
      if (!ix.count(name) || (action != "add" && action != "update" && action != "remove") ||
          !touched.emplace(name, ident).second)
        fail("graph_packet_history_change_identity_invalid");
      if (!same(ix[name].count(ident) ? ix[name].at(ident) : Json(nullptr), c["after"]) ||
          !same(prov[name].contains(ident) ? prov[name][ident] : Json(nullptr), c["after_provenance"]))
        fail("graph_packet_history_after_state_mismatch");
      if (c["before"].is_null()) {
        if (action != "add" || !c["before_provenance"].is_null())
          fail("graph_packet_history_missing_before_record");
        ix[name].erase(ident);
        prov[name].erase(ident);
      } else {
        record(name.c_str(), c["before"]);
        if (id(name.c_str(), c["before"]) != ident || action == "add")
          fail("graph_packet_history_before_identity_invalid");
        metadata(c["before_provenance"], c["before"]);
        ix[name][ident] = c["before"];
        prov[name][ident] = c["before_provenance"];
      }
    }
    Json parent = current;
    parent["history"].erase(parent["history"].end() - 1);
    parent["task"] = event["previous_task"];
    parent["provenance"] = prov;
    for (auto name : collections) {
      array(event["previous_order"][name]);
      std::set<std::string> seen;
      parent[name] = Json::array();
      for (const auto& entry : event["previous_order"][name]) {
        auto ident = str(entry);
        if (!seen.insert(ident).second || !ix[name].count(ident))
          fail("graph_packet_history_previous_order_invalid");
        parent[name].push_back(ix[name].at(ident));
      }
      if (seen.size() != ix[name].size()) fail("graph_packet_history_previous_order_invalid");
    }
    parent["packet_id"] = digest(payload(parent, "packet_id"));
    if (parent["packet_id"] != event["base_packet_id"]) fail("graph_packet_history_parent_hash_mismatch");
    content(parent);
    diff_structure(parent, event["diff"]);
    auto replay = candidate(parent, event["diff"]);
    if (!same(replay.first, current) || !same(replay.second, event))
      fail("graph_packet_history_forward_replay_mismatch");
    current = std::move(parent);
  }
}
Json apply(const Json& p, const Json& d, const Json& policy, bool accepted) {
  shape(policy, {"schema", "acceptance", "allow_source_tombstones"});
  if (policy["schema"] != "loom.graph_packet_apply_policy/1" ||
      (policy["acceptance"] != "preview" && policy["acceptance"] != "auto") ||
      !policy["allow_source_tombstones"].is_boolean())
    fail("graph_packet_apply_policy_invalid");
  auto v = preview(p, d);
  if (!d["sources"]["remove"].empty() && !policy["allow_source_tombstones"].get<bool>())
    fail("source_projection_tombstones_disabled_by_policy");
  bool take = policy["acceptance"] == "auto" || accepted;
  const auto& c = v["candidate_packet"];
  Json receipt = {{"schema", "loom.graph_packet_application/1"},
                  {"accepted", take},
                  {"policy", policy},
                  {"explicitly_accepted", accepted},
                  {"before_packet", p},
                  {"diff", d},
                  {"candidate_packet_sha256", c["packet_id"]},
                  {"after_packet_sha256", take ? c["packet_id"] : p["packet_id"]},
                  {"canonical_store_written", false},
                  {"acceptance_establishes_content_truth", false}};
  receipt["receipt_sha256"] = digest(receipt);
  return Json{{"packet", take ? c : p}, {"receipt", receipt}};
}
}  // namespace
void shape(const Json& v, std::initializer_list<const char*> fields) {
  if (!v.is_object() || v.size() != fields.size()) fail("graph_packet_object_shape_invalid");
  for (auto k : fields)
    if (!v.contains(k)) fail("graph_packet_object_shape_invalid");
}
void text(const Json& v, bool empty) {
  if (!v.is_string() || (!empty && v.get_ref<const std::string&>().empty()) ||
      !utf8::is_valid(v.get_ref<const std::string&>()))
    fail("graph_packet_string_invalid");
}
void timestamp(const Json& v) {
  if (v.is_null()) return;
  text(v);
  static const std::regex iso(
      R"(^([0-9]{4})-([0-9]{2})-([0-9]{2})[T ]([0-9]{2}):([0-9]{2})(?::([0-9]{2})(?:\.[0-9]+)?)?(Z|[+-][0-9]{2}:[0-9]{2})$)");
  std::smatch m;
  const auto value = v.get<std::string>();
  if (!std::regex_match(value, m, iso)) fail("graph_packet_timestamp_invalid");
  int year = std::stoi(m[1]), month = std::stoi(m[2]), day = std::stoi(m[3]);
  const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  if (year < 1 || month < 1 || month > 12 || day < 1 || day > days[month - 1] + (month == 2 && leap) ||
      std::stoi(m[4]) > 23 || std::stoi(m[5]) > 59 || (m[6].matched && std::stoi(m[6]) > 59))
    fail("graph_packet_timestamp_invalid");
  auto zone = m[7].str();
  if (zone != "Z" && (std::stoi(zone.substr(1, 2)) > 23 || std::stoi(zone.substr(4, 2)) > 59))
    fail("graph_packet_timestamp_invalid");
}
void origin(const Json& v) {
  shape(v, {"kind", "actor", "model", "recipe_sha256", "response_sha256"});
  if (v["kind"] != "recorded" && v["kind"] != "model" && v["kind"] != "user" && v["kind"] != "system")
    fail("invalid_instrument_origin_kind");
  text(v["actor"]);
  for (auto k : {"model", "recipe_sha256", "response_sha256"})
    if (!v[k].is_null()) text(v[k]);
  if (v["kind"] == "model" && v["model"].is_null()) fail("model_instrument_identity_required");
  for (auto k : {"recipe_sha256", "response_sha256"})
    if (!v[k].is_null()) {
      auto s = v[k].get<std::string>();
      if (s.size() != 64 || s.find_first_not_of("0123456789abcdef") != std::string::npos)
        fail("instrument_hash_invalid");
    }
}
std::string digest(const Json& v) { return Sha256::hex(json::canonical(v)); }
void resources(const Json& v, const Json& limits) {
  object(limits);
  for (auto it = limits.begin(); it != limits.end(); ++it) {
    if (it.key() != "max_nodes" && it.key() != "max_depth" && it.key() != "max_string_bytes" &&
        it.key() != "max_integer_bits")
      fail("graph_packet_resource_policy_invalid");
    if (!it.value().is_null()) {
      integer(it.value(), true);
      if (it.value() == 0) fail("graph_packet_resource_policy_invalid");
    }
  }
  auto cap = [&](const char* k, std::size_t n) {
    if (limits.contains(k) && !limits[k].is_null() && n > limits[k].get<std::uint64_t>())
      fail("graph_packet_configured_resource_limit");
  };
  std::vector<std::pair<const Json*, std::size_t>> work = {{&v, 0}};
  std::size_t nodes = 0, bytes = 0;
  while (!work.empty()) {
    auto [j, depth] = work.back();
    work.pop_back();
    cap("max_nodes", ++nodes);
    cap("max_depth", depth);
    if (j->is_object())
      for (auto it = j->begin(); it != j->end(); ++it) {
        cap("max_nodes", ++nodes);
        cap("max_depth", depth + 1);
        if (!utf8::is_valid(it.key())) fail("graph_packet_invalid_utf8");
        bytes += it.key().size();
        cap("max_string_bytes", bytes);
        work.emplace_back(&it.value(), depth + 1);
      }
    else if (j->is_array())
      for (const auto& child : *j) work.emplace_back(&child, depth + 1);
    else if (j->is_string()) {
      text(*j, true);
      bytes += j->get_ref<const std::string&>().size();
      cap("max_string_bytes", bytes);
    } else if (j->is_number_integer()) {
      std::uint64_t n;
      if (j->is_number_unsigned())
        n = j->get<std::uint64_t>();
      else {
        auto s = j->get<std::int64_t>();
        n = s < 0 ? static_cast<std::uint64_t>(-(s + 1)) + 1 : static_cast<std::uint64_t>(s);
      }
      std::size_t bits = 0;
      while (n) {
        ++bits;
        n >>= 1;
      }
      cap("max_integer_bits", bits);
    } else if (j->is_number_float())
      number(*j);
  }
}
Json parse_strict(std::string_view raw) {
  if (!utf8::is_valid(raw)) fail("graph_packet_invalid_utf8");
  if (raw.starts_with("\xef\xbb\xbf")) fail("invalid_json");
  // nlohmann otherwise converts overflowing integer literals to double.
  // Reject unrepresentable integers instead of silently changing source data.
  bool quoted = false, escaped = false;
  for (std::size_t i = 0; i < raw.size(); ++i) {
    char c = raw[i];
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
      continue;
    }
    if (c == '"') {
      quoted = true;
      continue;
    }
    if (c != '-' && (c < '0' || c > '9')) continue;
    auto start = i;
    while (i + 1 < raw.size() &&
           (std::string_view("0123456789.eE+-").find(raw[i + 1]) != std::string_view::npos))
      ++i;
    auto token = raw.substr(start, i - start + 1);
    if (token.find_first_of(".eE") == std::string_view::npos) {
      std::errc error;
      if (token.front() == '-') {
        std::int64_t n;
        error = std::from_chars(token.data(), token.data() + token.size(), n).ec;
      } else {
        std::uint64_t n;
        error = std::from_chars(token.data(), token.data() + token.size(), n).ec;
      }
      if (error == std::errc::result_out_of_range)
        fail("graph_packet_native_integer_representation_unavailable");
    }
  }
  std::vector<std::set<std::string>> keys;
  auto callback = [&](int, Json::parse_event_t event, Json& value) {
    if (event == Json::parse_event_t::object_start)
      keys.emplace_back();
    else if (event == Json::parse_event_t::key) {
      if (!keys.back().insert(value.get<std::string>()).second) fail("duplicate_json_key");
    } else if (event == Json::parse_event_t::object_end)
      keys.pop_back();
    return true;
  };
  return Json::parse(raw.begin(), raw.end(), callback);
}
void validate(const Json& p, const Json& limits) {
  resources(p, limits);
  content(p);
  history(p);
}
Json empty_diff(const Json& p, const Json& proposal, const Json& o, const Json& known) {
  validate(p);
  text(proposal);
  origin(o);
  timestamp(known);
  Json d = {{"schema", "loom.graph_packet_diff/1"},
            {"base_packet_sha256", p["packet_id"]},
            {"proposal_id", proposal},
            {"origin", o},
            {"known_at", known}};
  for (auto name : collections)
    d[name] = Json{{"add", Json::array()}, {"update", Json::array()}, {"remove", Json::array()}};
  d["task"] = nullptr;
  d["annotations"] =
      Json{{"critique", Json::array()}, {"questions", Json::array()}, {"operations", Json::array()}};
  return d;
}
Json preview(const Json& p, const Json& d) {
  validate(p);
  resources(d, Json::object());
  diff_structure(p, d);
  auto c = candidate(p, d);
  validate(c.first);
  return Json{{"schema", "loom.graph_packet_preview/1"},
              {"base_packet_id", p["packet_id"]},
              {"diff_sha256", digest(d)},
              {"candidate_packet", c.first},
              {"changes", c.second["changes"]},
              {"annotations", d["annotations"]},
              {"canonical_store_written", false},
              {"acceptance_establishes_content_truth", false}};
}
Result<Json> execute(const Json& r) {
  Json saved = nullptr;
  try {
    object(r);
    auto op = str(r.at("operation"));
    std::string raw;
    if (op == "compile_reply" || op == "capture" || op == "decode") {
      if (r.contains("raw") == r.contains("raw_base64"))
        fail("graph_reply_exactly_one_raw_encoding_required");
      if (r.contains("raw"))
        raw = r["raw"].get<std::string>();
      else {
        auto decoded = base64::decode(r["raw_base64"].get<std::string>(), true);
        if (!decoded) fail("graph_reply_raw_base64_invalid");
        raw = *decoded;
      }
      saved = capture(raw);
    }
    const auto limits = r.value("resource_limits", Json::object());
    if (op == "capture") return saved;
    if (op == "capabilities")
      return Json{{"schema", "loom.packet_capabilities/1"},
                  {"operations", Json::array({"make", "validate", "encode", "decode", "empty_diff", "preview",
                                              "apply", "invert", "capture", "compile_reply",
                                              "validate_compilation", "apply_compiled_reply"})},
                  {"packet_schema", "loom.graph_packet/1"},
                  {"reply_schemas", Json::array({"loom.graph_reply/1", "loom.graph_reply/2"})},
                  {"provider_calls", false},
                  {"canonical_store_written", false},
                  {"resource_limits",
                   Json::array({"max_nodes", "max_depth", "max_string_bytes", "max_integer_bits"})},
                  {"numeric_representation", "signed/unsigned 64-bit integers and finite binary64"}};
    if (op == "decode") {
      auto decoded = parse_strict(raw);
      validate(decoded, limits);
      return decoded;
    }
    if (op == "make") {
      const auto& o = r.at("origin");
      origin(o);
      const auto known = r.value("known_at", Json(nullptr));
      timestamp(known);
      Json made = {{"schema", "loom.graph_packet/1"},
                   {"task", r.value("task", Json::object())},
                   {"provenance", Json::object()},
                   {"history", Json::array()}};
      for (auto name : collections) {
        made[name] = r.value(name, Json::array());
        array(made[name]);
        made["provenance"][name] = Json::object();
        for (const auto& entry : made[name]) {
          auto key = id(name, entry);
          made["provenance"][name][key] =
              Json{{"known_at", std::string_view(name) == "sources" ? entry.at("known_at") : known},
                   {"origin", o},
                   {"record_sha256", digest(entry)}};
        }
      }
      made["packet_id"] = digest(made);
      validate(made, limits);
      return made;
    }
    if (r.contains("diff")) resources(r["diff"], limits);
    if (r.contains("compilation")) resources(r["compilation"], limits);
    if (r.contains("receipt")) resources(r["receipt"], limits);
    const auto& p = r.at("packet");
    validate(p, limits);
    if (op == "validate") return p;
    if (op == "encode") return Json(json::canonical(p));
    if (op == "empty_diff")
      return empty_diff(p, r.at("proposal_id"), r.at("origin"), r.value("known_at", Json(nullptr)));
    if (op == "preview") return preview(p, r.at("diff"));
    if (op == "apply") return apply(p, r.at("diff"), r.at("policy"), r.value("explicitly_accepted", false));
    if (op == "invert") {
      const auto& receipt = r.at("receipt");
      shape(receipt, {"schema", "accepted", "policy", "explicitly_accepted", "before_packet", "diff",
                      "candidate_packet_sha256", "after_packet_sha256", "canonical_store_written",
                      "acceptance_establishes_content_truth", "receipt_sha256"});
      if (receipt["schema"] != "loom.graph_packet_application/1" ||
          receipt["receipt_sha256"] != digest(payload(receipt, "receipt_sha256")))
        fail("graph_packet_application_receipt_drift");
      if (p["packet_id"] != receipt["after_packet_sha256"])
        fail("graph_packet_inverse_requires_current_application_head");
      validate(receipt["before_packet"]);
      auto replay = apply(receipt["before_packet"], receipt["diff"], receipt["policy"],
                          receipt["explicitly_accepted"].get<bool>());
      if (!same(replay["packet"], p)) fail("graph_packet_application_replay_mismatch");
      return receipt["before_packet"];
    }
    if (op == "compile_reply") {
      saved = capture(raw);
      return compile_reply(p, raw, r.at("host"), limits);
    }
    if (op == "validate_compilation" || op == "apply_compiled_reply") {
      const auto& c = r.at("compilation");
      shape(c, {"schema", "base_packet_sha256", "raw_capture", "host", "response_text", "spans", "node_ids",
                "turn_entity_id", "diff", "canonical_store_written", "acceptance_establishes_content_truth",
                "compilation_sha256"});
      saved = c["raw_capture"];
      if (c["schema"] != "loom.graph_reply_compilation/1" ||
          c["compilation_sha256"] != digest(payload(c, "compilation_sha256")))
        fail("graph_reply_compilation_hash_drift");
      // Captured bytes are checked by the compiler before deterministic replay.
      extern std::string capture_bytes(const Json&);
      auto expected = compile_reply(p, capture_bytes(c["raw_capture"]), c["host"], limits);
      if (!same(expected, c)) fail("graph_reply_compilation_replay_mismatch");
      if (op == "validate_compilation") return c;
      return apply(p, c["diff"], r.at("policy"), r.value("explicitly_accepted", false));
    }
    fail("graph_packet_unknown_operation");
  } catch (const std::exception& e) {
    const std::string message = e.what();
    Json error = {
        {"code", message == "graph_packet_native_integer_representation_unavailable" ? "unavailable"
                                                                                     : "invalid_argument"},
        {"message", message}};
    if (!saved.is_null()) error["raw_capture"] = saved;
    return Json{{"error", error}};
  }
}
}  // namespace loom::packet
