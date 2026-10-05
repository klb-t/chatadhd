#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "loom/util/base64.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"
#include "packet.h"
namespace loom::packet {
namespace {
constexpr const char* compiler = "loom.graph_reply_compiler/1";
[[noreturn]] void fail(const char* code) { throw std::invalid_argument(code); }
std::string str(const Json& j) {
  text(j);
  return j.get<std::string>();
}
Json entity(const std::string& id, const char* kind, const Json& label, const Json& attrs,
            const Json& parent = "", const char* o = "system") {
  return Json{{"id", id},
              {"kind", kind},
              {"canonical_key", id},
              {"label", label},
              {"labels", Json::object()},
              {"aliases", Json::array()},
              {"parent", parent},
              {"first_seen", ""},
              {"last_seen", ""},
              {"evidence_class", "derived"},
              {"origin", o},
              {"confidence", 1.0},
              {"status", "active"},
              {"attrs", attrs}};
}
Json source(const std::string& id, const std::string& unit, const std::string& t, const char* member,
            const char* artifact, const Json& known, const Json& attrs) {
  auto hash = Sha256::hex(t);
  Json loc = {
      {"source", "sha256:" + hash}, {"member", member},      {"json_pointer", ""},  {"byte_start", 0},
      {"byte_len", t.size()},       {"time_start", nullptr}, {"time_end", nullptr}, {"line", nullptr}};
  Json o = {{"id", id},
            {"unit", unit},
            {"kind", "utterance"},
            {"text", t},
            {"locator", loc},
            {"lang", ""},
            {"date", known.is_null() ? Json("") : known},
            {"ordinal", 0},
            {"artifact_type", artifact},
            {"speaker", "assistant"},
            {"attrs", attrs}};
  return Json{{"observation", o}, {"known_at", known}, {"text_sha256", hash}};
}
Json claim(const std::string& id, const std::string& subject, const std::string& predicate,
           const std::string& obj, const Json& request, const Json& s, const std::string& quote) {
  Json q = {{"valid_from", ""},
            {"valid_to", ""},
            {"version", ""},
            {"branch", ""},
            {"scope", "graph_reply_structure"},
            {"lang", ""},
            {"extra", Json{{"request_id", request},
                           {"confidence_scope", "structure_only"},
                           {"content_verification", "unverified"}}}};
  Json support = {{"observation", s["observation"]["id"]},
                  {"locator", s["observation"]["locator"]},
                  {"quote", quote},
                  {"extractor", compiler},
                  {"quality", 1.0}};
  Json a = {{"basis",
             Json{{"support", Json::array({support})},
                  {"derivation",
                   Json{{"operator", compiler}, {"operator_version", 1}, {"morphism", ""}, {"depth", 0}}}}},
            {"evidence_class", "derived"},
            {"origin", "system"},
            {"confidence", 1.0},
            {"premises",
             Json{{"claims", Json::array()}, {"principles", Json::array()}, {"assumptions", Json::array()}}},
            {"counter", Json{{"observations", Json::array()}, {"claims", Json::array()}}},
            {"status", "active"},
            {"consequences",
             Json{{"claims", Json::array()}, {"predictions", Json::array()}, {"checks", Json::array()}}},
            {"open", Json{{"slots", Json::array()}, {"questions", Json::array()}, {"fill_query", nullptr}}},
            {"expected_property", nullptr},
            {"check_state", "n/a"},
            {"alternatives", Json::array()}};
  return Json{{"id", id},         {"subject", subject}, {"predicate", predicate}, {"object", obj},
              {"value", nullptr}, {"qualifiers", q},    {"assessment", a}};
}
}  // namespace
Json capture(std::string_view raw) {
  return Json{{"schema", "loom.graph_reply_capture/1"},
              {"sha256", Sha256::hex(raw)},
              {"byte_len", raw.size()},
              {"raw_base64", base64::encode(raw)}};
}
std::string capture_bytes(const Json& c) {
  shape(c, {"schema", "sha256", "byte_len", "raw_base64"});
  if (c["schema"] != "loom.graph_reply_capture/1" || !c["raw_base64"].is_string() ||
      !c["byte_len"].is_number_integer())
    fail("graph_reply_capture_invalid");
  auto raw = base64::decode(c["raw_base64"].get<std::string>(), true);
  if (!raw) fail("graph_reply_capture_invalid");
  if (c["byte_len"] != raw->size() || c["sha256"] != Sha256::hex(*raw))
    fail("graph_reply_capture_hash_drift");
  return *raw;
}
Json compile_reply(const Json& p, std::string_view raw, const Json& input_host, const Json& limits) {
  validate(p, limits);
  Json h = input_host;
  for (auto k : {"actor", "recipe_sha256", "parent_turn_id", "known_at"})
    if (!h.contains(k)) h[k] = std::string_view(k) == "actor" ? Json("graph_reply_model") : Json(nullptr);
  shape(h, {"request_id", "turn_id", "model", "actor", "recipe_sha256", "parent_turn_id", "known_at"});
  for (auto k : {"request_id", "turn_id", "model", "actor"}) text(h[k]);
  timestamp(h["known_at"]);
  Json cap = capture(raw);
  Json model_origin = {{"kind", "model"},
                       {"actor", h["actor"]},
                       {"model", h["model"]},
                       {"recipe_sha256", h["recipe_sha256"]},
                       {"response_sha256", cap["sha256"]}};
  origin(model_origin);
  if (!h["parent_turn_id"].is_null()) {
    text(h["parent_turn_id"]);
    bool found = false;
    for (const auto& e : p["entities"])
      if (e["id"] == h["parent_turn_id"] && e["kind"] == "conversation_turn") found = true;
    if (!found) fail("graph_reply_unknown_parent_turn");
  }
  Json r = parse_strict(raw);
  resources(r, limits);
  shape(r, {"schema", "base_packet_sha256", "response_id", "nodes", "links"});
  bool v2 = r["schema"] == "loom.graph_reply/2";
  if (!v2 && r["schema"] != "loom.graph_reply/1") fail("graph_reply_schema_invalid");
  if (r["base_packet_sha256"] != p["packet_id"]) fail("graph_reply_stale_base");
  auto root = str(r["response_id"]);
  if (!r["nodes"].is_array() || r["nodes"].empty()) fail("graph_reply_nodes_required");
  std::map<std::string, Json> nodes;
  std::map<std::string, std::string> parents;
  for (const auto& n : r["nodes"]) {
    shape(n, {"id", "text", "role", "children"});
    auto id = str(n["id"]);
    text(n["role"]);
    if (!v2 || !n["text"].is_null()) text(n["text"]);
    if (!nodes.emplace(id, n).second) fail("graph_reply_duplicate_node_id");
    if (!n["children"].is_array()) fail("graph_reply_children_array_required");
    std::set<std::string> children;
    for (const auto& c : n["children"])
      if (!children.insert(str(c)).second) fail("graph_reply_duplicate_child");
    if (n["text"].is_null() && n["children"].empty()) fail("graph_reply_null_leaf_text_invalid");
  }
  if (!nodes.count(root) || nodes.at(root)["role"] != "response") fail("graph_reply_response_root_invalid");
  for (const auto& [id, n] : nodes)
    for (const auto& c : n["children"]) {
      auto child = str(c);
      if (!nodes.count(child)) fail("graph_reply_unknown_child");
      if (!parents.emplace(child, id).second) fail("graph_reply_multiple_parents");
    }
  if (parents.count(root)) fail("graph_reply_root_has_parent");
  std::vector<std::pair<std::string, bool>> work = {{root, false}};
  std::set<std::string> seen;
  std::map<std::string, std::string> rendered;
  while (!work.empty()) {
    auto [id, ready] = work.back();
    work.pop_back();
    const auto& n = nodes.at(id);
    if (!ready) {
      if (!seen.insert(id).second) fail("graph_reply_cycle");
      work.emplace_back(id, true);
      for (auto i = n["children"].rbegin(); i != n["children"].rend(); ++i) work.emplace_back(str(*i), false);
      continue;
    }
    std::string t;
    if (!n["children"].empty()) {
      for (const auto& c : n["children"]) t += rendered.at(str(c));
      if (!n["text"].is_null() && n["text"] != t) fail("graph_reply_partition_mismatch");
    } else
      t = n["text"].get<std::string>();
    rendered[id] = std::move(t);
  }
  if (seen.size() != nodes.size()) fail("graph_reply_disconnected_or_cyclic_nodes");
  std::set<std::string> existing, all_existing;
  for (auto name : {"definitions", "entities", "claims", "sources"})
    for (const auto& item : p[name]) {
      auto id = str(std::string_view(name) == "sources" ? item["observation"]["id"] : item["id"]);
      all_existing.insert(id);
      if (std::string_view(name) == "entities") existing.insert(id);
    }
  for (const auto& [id, n] : nodes)
    if (all_existing.count(id)) fail("graph_reply_local_context_identity_collision");
  if (!r["links"].is_array()) fail("graph_reply_links_array_required");
  std::set<std::tuple<std::string, std::string, std::string>> links;
  for (const auto& link : r["links"]) {
    shape(link, {"from", "predicate", "to"});
    auto from = str(link["from"]), pred = str(link["predicate"]), to = str(link["to"]);
    if (!nodes.count(from)) fail("graph_reply_unknown_link_source");
    if (!nodes.count(to) && !existing.count(to)) fail("graph_reply_unknown_context_entity");
    if (!links.emplace(from, pred, to).second) fail("graph_reply_duplicate_link");
  }
  Json spans = Json::object();
  using Span = std::tuple<std::string, Json, std::size_t, std::size_t, std::size_t>;
  std::vector<Span> pending = {{root, nullptr, 0, 0, 0}};
  while (!pending.empty()) {
    auto [id, parent, ordinal, byte, charpos] = pending.back();
    pending.pop_back();
    const auto& t = rendered.at(id);
    spans[id] = Json{{"byte_start", byte},          {"byte_len", t.size()}, {"char_start", charpos},
                     {"char_len", utf8::length(t)}, {"parent_id", parent},  {"ordinal", ordinal}};
    std::vector<Span> children;
    std::size_t i = 0;
    for (const auto& child : nodes.at(id)["children"]) {
      auto c = str(child);
      children.emplace_back(c, id, i++, byte, charpos);
      byte += rendered.at(c).size();
      charpos += utf8::length(rendered.at(c));
    }
    pending.insert(pending.end(), children.rbegin(), children.rend());
  }
  auto seed = digest(Json{{"request_id", h["request_id"]}, {"turn_id", h["turn_id"]}}),
       turn = "gr_turn_" + seed;
  auto ns = digest(Json{{"base_packet_sha256", p["packet_id"]},
                        {"request_id", h["request_id"]},
                        {"turn_id", h["turn_id"]},
                        {"response_sha256", cap["sha256"]}});
  Json ids = Json::object();
  for (const auto& n : r["nodes"])
    ids[str(n["id"])] = "gr_node_" + digest(Json{{"namespace", ns}, {"local_id", n["id"]}});
  auto rawid = "gr_raw_" + ns, displayid = "gr_display_" + ns;
  auto response = rendered.at(root);
  auto raws = source(rawid, turn, std::string(raw), "response.json", "graph_reply_wire", h["known_at"],
                     Json{{"request_id", h["request_id"]},
                          {"model_origin", model_origin},
                          {"observation_scope", "exact_model_output_bytes_not_world_truth"}});
  auto display = source(displayid, turn, response, "response.txt", "graph_reply_display", h["known_at"],
                        Json{{"request_id", h["request_id"]},
                             {"derived_from", rawid},
                             {"transform", v2 ? "compose_ordered_leaf_text" : "json_decode_root_text"},
                             {"model_origin", model_origin},
                             {"observation_scope", "decoded_model_output_not_world_truth"}});
  auto co = model_origin;
  co["kind"] = "system";
  co["actor"] = compiler;
  auto d = empty_diff(p, "gr_proposal_" + ns, co, h["known_at"]);
  d["sources"]["add"] = Json::array({raws, display});
  d["entities"]["add"].push_back(entity(turn, "conversation_turn", h["turn_id"],
                                        Json{{"turn_id", h["turn_id"]},
                                             {"request_id", h["request_id"]},
                                             {"parent_turn_id", h["parent_turn_id"]},
                                             {"response_entity_id", ids[root]},
                                             {"raw_source_id", rawid},
                                             {"display_source_id", displayid},
                                             {"model_origin", model_origin},
                                             {"confidence_scope", "identity/structure_only"},
                                             {"content_verification", "unverified"}}));
  for (const auto& n : r["nodes"]) {
    auto id = str(n["id"]);
    const auto& span = spans[id];
    Json resolved = Json::array(), children = Json::array();
    for (const auto& link : r["links"])
      if (link["from"] == id) {
        auto l = link;
        l["resolved_from"] = ids[str(l["from"])];
        auto to = str(l["to"]);
        l["resolved_to"] = ids.contains(to) ? ids[to] : l["to"];
        l["target_scope"] = ids.contains(to) ? "local" : "context";
        l["status"] = "draft";
        l["content_verification"] = "unverified";
        l["origin"] = model_origin;
        l["context_packet_sha256"] = p["packet_id"];
        resolved.push_back(l);
      }
    for (const auto& c : n["children"]) children.push_back(ids[str(c)]);
    Json attrs = {{"local_id", id},
                  {"text", rendered.at(id)},
                  {"role", n["role"]},
                  {"children", children},
                  {"model_links", resolved},
                  {"source_observation_id", displayid},
                  {"source_span", span},
                  {"turn_entity_id", turn},
                  {"model_origin", model_origin},
                  {"content_verification", "unverified"},
                  {"confidence_scope", "identity/structure_only"}};
    if (v2) {
      attrs["wire_text"] = n["text"];
      attrs["response_text"] = rendered.at(id);
    }
    d["entities"]["add"].push_back(
        entity(str(ids[id]), "graph_reply_node", rendered.at(id), attrs,
               span["parent_id"].is_null() ? Json(turn) : ids[str(span["parent_id"])], "model_knowledge"));
  }
  std::vector<std::tuple<std::string, std::string, std::string>> edges = {
      {turn, "has_response", str(ids[root])}};
  for (const auto& n : r["nodes"])
    for (const auto& child : n["children"])
      edges.emplace_back(str(ids[str(n["id"])]), "contains_part", str(ids[str(child)]));
  if (!h["parent_turn_id"].is_null()) edges.emplace_back(turn, "reply_to_turn", str(h["parent_turn_id"]));
  for (const auto& [subject, pred, obj] : edges) {
    auto id = "gr_structure_" +
              digest(Json{{"namespace", ns}, {"subject", subject}, {"predicate", pred}, {"object", obj}});
    d["claims"]["add"].push_back(claim(id, subject, pred, obj, h["request_id"], display, response));
  }
  preview(p, d, limits);
  Json result = {{"schema", "loom.graph_reply_compilation/1"},
                 {"base_packet_sha256", p["packet_id"]},
                 {"raw_capture", cap},
                 {"host", h},
                 {"response_text", response},
                 {"spans", spans},
                 {"node_ids", ids},
                 {"turn_entity_id", turn},
                 {"diff", d},
                 {"canonical_store_written", false},
                 {"acceptance_establishes_content_truth", false}};
  result["compilation_sha256"] = digest(result);
  resources(result, limits);
  return result;
}

Json reply_fragment(const Json& c, const Json& address) {
  // The command adapter recompiles captured bytes before calling this lookup.
  // Model supplied ranges or edited entity attributes never become addresses.
  if (!address.is_object() || address.size() != 1 ||
      (!address.contains("local_id") && !address.contains("node_id")))
    fail("graph_reply_fragment_address_invalid");
  const auto key = str(address.begin().value());
  std::string local;
  if (address.contains("local_id")) {
    if (c["node_ids"].contains(key)) local = key;
  } else {
    for (auto it = c["node_ids"].begin(); it != c["node_ids"].end(); ++it)
      if (it.value() == key) local = it.key();
  }
  if (local.empty()) fail("graph_reply_fragment_unknown_address");
  const auto& node_id = c["node_ids"][local];
  for (const auto& e : c["diff"]["entities"]["add"]) {
    if (e["id"] != node_id) continue;
    const auto& a = e["attrs"];
    const auto& span = c["spans"][local];
    for (const auto& s : c["diff"]["sources"]["add"]) {
      if (s["observation"]["id"] != a["source_observation_id"]) continue;
      Json locator = s["observation"]["locator"];
      locator["byte_start"] =
          locator["byte_start"].get<std::uint64_t>() + span["byte_start"].get<std::uint64_t>();
      locator["byte_len"] = span["byte_len"];
      return Json{{"schema", "loom.graph_reply_fragment/1"},
                  {"local_id", local},
                  {"node_id", node_id},
                  {"turn_entity_id", c["turn_entity_id"]},
                  {"text", a["text"]},
                  {"span", span},
                  {"source_observation_id", a["source_observation_id"]},
                  {"source_locator", locator},
                  {"model_origin", a["model_origin"]},
                  {"compilation_sha256", c["compilation_sha256"]}};
    }
  }
  fail("graph_reply_fragment_missing_source");
}
}  // namespace loom::packet
