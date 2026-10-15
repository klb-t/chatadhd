// P27: chat_context_format and chat_reply_format. See context_format.h.
#include "context_format.h"

#include <algorithm>
#include <deque>

#include "loom/annotation_layer.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/log.h"
#include "loom/runtime_profile.h"
#include "loom/scope_resolver.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace loom::chat::formats {
namespace {

constexpr const char* kLog = "loom.chat_formats";

std::string replace_all(std::string text, const std::string& from, const std::string& to) {
  if (from.empty()) return text;
  for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) text.replace(at, from.size(), to);
  return text;
}

// Does the model support the capability: true / false from the user's model_capabilities, null when nothing says.
Json capability_of(const Config& cfg, const Json& reply, const std::string& model, const std::string& capability) {
  const Json all = cfg.get(json::get_string(reply, "capability_setting", "model_capabilities"));
  if (!all.is_object()) return nullptr;
  const Json per = all.value(model, Json::object());
  if (!per.is_object() || !per.contains(capability) || !per[capability].is_boolean()) return nullptr;
  return per[capability];
}

std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

Reply fail(const std::string& why) {
  Reply r;
  r.error = why;
  return r;
}

}  // namespace

Result<Axes> resolve(const std::filesystem::path& root, const Config& cfg, Database* db, const std::string& conv_id, const std::string& model,
                     const std::optional<std::string>& override_flow, bool reply_allowed) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("guardian", root));
  const Json& values = profile.values();
  if (!values.contains("context_io")) return Error(Errc::NotFound, "profile guardian has no context_io section");
  Axes a;
  a.section = values.at("context_io");
  const Json& section = a.section;
  const Json models = values.value("models", Json::object());
  a.model_class = annotations::model_tier(models, model);
  const Json flows = section.value("flows", Json::object());
  // A flow named by the request or by the scope chain; else the flow of the class of the model (data). The model of the conversation is only
  // ever read through its class, never inherited as a setting.
  std::string name;
  if (override_flow && !override_flow->empty()) {
    name = *override_flow;
    a.reason = "request";
  } else {
    scopes::Binding bind;
    bind.key = json::get_string(section, "setting", "chat_flow");
    bind.installation = json::get_string(section, "active");
    bind.installation_source = "profile:guardian.context_io.active";
    bind.conv_id = conv_id;
    const Json v = scopes::value_or(root, cfg, db, bind, Json(bind.installation));
    if (v.is_string()) name = v.get<std::string>();
    a.reason = "scope chain";
  }
  if (name.empty()) {
    const Json by_class = section.value("class_flows", Json::object());
    name = json::get_string(by_class, a.model_class, json::get_string(by_class, "unknown", json::get_string(section, "unknown", "plain")));
    a.reason = "class " + a.model_class;
  }
  if (!flows.contains(name)) {
    log::write(log::Level::Warning, kLog, "chat_flow '" + name + "' is not a flow in context_io.flows; using " + json::get_string(section, "unknown", "plain"));
    name = json::get_string(section, "unknown", "plain");
    a.reason += " (unknown flow)";
  }
  a.flow = name;
  const Json& flow = flows.at(name);
  a.renderer = json::get_string(flow, "renderer", "chat_turns");
  a.output = json::get_string(flow, "output", "text");
  const Json renderers = section.value("renderers", Json::object());
  if (!renderers.contains(a.renderer)) return Error(Errc::InvalidArgument, "context_io.renderers has no renderer '" + a.renderer + "'");
  a.turns = json::get_bool(renderers.at(a.renderer), "turns", true);
  a.packet = json::get_bool(renderers.at(a.renderer), "packet", false);
  const Json outputs = section.value("outputs", Json::object());
  if (!outputs.contains(a.output)) return Error(Errc::InvalidArgument, "context_io.outputs has no output '" + a.output + "'");
  const Json& out = outputs.at(a.output);
  if (json::get_bool(out, "graph", false)) {
    if (!reply_allowed) {
      a.reason += "; context_execution.graph_reply owns the graph reply of this send";
    } else {
      a.reply_graph = true;
      // Forced: the structured response format. A model known not to support it gets the instruction only, and the reply is validated.
      a.mode = json::get_string(out, "force", "response_format");
      const std::string needs = json::get_string(out, "requires_capability");
      const Json cap = needs.empty() ? Json() : capability_of(cfg, out, model, needs);
      if (cap.is_boolean() && !cap.get<bool>()) {
        a.mode = json::get_string(out, "fallback_mode", "prompt_only");
        a.reason += "; model_capabilities: no " + needs;
      }
      if (!out.value("modes", Json::object()).contains(a.mode)) return Error(Errc::InvalidArgument, "output '" + a.output + "' has no mode '" + a.mode + "'");
    }
  }
  return a;
}

namespace {
// The columns of a message row, as the embedder compares them (everything but the text, which is compared by its hash).
Json raw_of(const Message& m) {
  return Json{{"conv_id", m.conv_id}, {"parent_id", m.parent_id ? Json(*m.parent_id) : Json(nullptr)}, {"role", m.role},
              {"model", m.model ? Json(*m.model) : Json(nullptr)}, {"status", m.status},
              {"version_group_id", m.version_group_id ? Json(*m.version_group_id) : Json(nullptr)}, {"version_num", m.version_num},
              {"weight", m.weight}, {"attachments", m.attachments}, {"metadata", m.metadata}, {"created", m.created},
              {"semantic_status", m.semantic_status}, {"text_sha256", Sha256::hex(m.text)}};
}
}  // namespace

Json packet(const Json& section, const std::string& conv_id, const std::vector<Entry>& entries, const std::vector<Link>& graph_links, const Json& inputs) {
  const Json spec = section.value("packet", Json::object());
  const Json predicates = spec.value("predicates", Json::object());
  const std::string origin = json::get_string(spec, "origin", "conversation");
  Json nodes = Json::array(), links = Json::array();
  std::set<std::string> kept;
  for (const auto& e : entries) kept.insert(e.msg.id);
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const Entry& e = entries[i];
    const Message& m = e.msg;
    // The selection keeps the identity and the place of every element: where it came from (source) is never dropped here, only by a renderer.
    nodes.push_back(Json{{"id", m.id}, {"kind", json::get_string(spec, "node_kind", "message")}, {"role", m.role}, {"text", e.text},
                         {"order", i}, {"weight", m.weight},
                         {"provenance", Json{{"origin", origin}, {"source", "message"}, {"message_id", m.id}, {"conversation_id", conv_id},
                                             {"text_form", e.selected_level ? "selected_level" : "raw"}}},
                         {"source", Json{{"table", "messages"}, {"id", m.id}, {"position", i}, {"raw", raw_of(m)}}}});
    if (i > 0) links.push_back(Json{{"from", m.id}, {"predicate", json::get_string(predicates, "order", "follows")}, {"to", entries[i - 1].msg.id}, {"derived", true}});
    if (m.parent_id && kept.count(*m.parent_id) && (i == 0 || entries[i - 1].msg.id != *m.parent_id))
      links.push_back(Json{{"from", m.id}, {"predicate", json::get_string(predicates, "parent", "reply_to")}, {"to", *m.parent_id}, {"derived", true}});
  }
  for (const auto& l : graph_links)
    links.push_back(Json{{"from", l.src}, {"predicate", l.link_type}, {"to", l.dst}, {"weight", l.weight}, {"metadata", l.metadata},
                         {"source", Json{{"table", "links"}, {"id", l.id}, {"created", l.created}}}});
  Json p{{"schema", json::get_string(spec, "schema", "loom.context_graph/1")}, {"conversation_id", conv_id}, {"selection", inputs}, {"nodes", nodes}, {"links", links}};
  p["packet_id"] = Sha256::hex(json::dump(p));
  return p;
}

Json render_packet(const Json& section, const std::string& renderer, const Json& pkt) {
  const Json drop = section.value("renderers", Json::object()).value(renderer, Json::object()).value("drop", Json::array());
  if (!drop.is_array() || drop.empty()) return pkt;
  Json out = pkt;
  for (auto& n : out["nodes"])
    for (const auto& k : drop) if (k.is_string()) n.erase(k.get<std::string>());
  for (auto& l : out["links"])
    for (const auto& k : drop) if (k.is_string()) l.erase(k.get<std::string>());
  return out;
}

std::string packet_block(const Json& section, const Json& pkt) {
  const Json spec = section.value("packet", Json::object());
  return replace_all(json::get_string(spec, "recipe"), "{{packet_id}}", json::get_string(pkt, "packet_id")) + json::dump(pkt);
}

std::string reply_instruction(const Json& section, const std::string& mode, const std::string& packet_id) {
  const Json reply = section.value("outputs", Json::object()).value("graph_reply", Json::object());
  std::string out = json::get_string(reply, "recipe");
  if (!out.empty() && out.back() != '\n') out += "\n";
  out += replace_all(json::get_string(reply, "base_packet_line"), "{{packet_id}}", packet_id);
  const Json def = reply.value("modes", Json::object()).value(mode, Json::object());
  if (json::get_bool(def, "schema_hint", false))
    out += "Answer with exactly one JSON object that follows this JSON schema:\n" + json::dump(reply.value("response_format", Json::object()).value("schema", Json::object())) + "\n";
  return out;
}

void apply_reply_request(Json& payload, const Json& section, const std::string& mode) {
  const Json reply = section.value("outputs", Json::object()).value("graph_reply", Json::object());
  const Json def = reply.value("modes", Json::object()).value(mode, Json::object());
  if (json::get_bool(def, "response_format", false)) {
    const Json rf = reply.value("response_format", Json::object());
    payload["response_format"] = Json{{"type", "json_schema"},
        {"json_schema", Json{{"name", json::get_string(rf, "name", "loom_graph_reply_v2")}, {"schema", rf.value("schema", Json::object())},
                             {"strict", json::get_bool(rf, "strict", true)}}}};
  }
  const Json provider = def.value("provider", Json());
  if (provider.is_object()) {
    if (!payload.contains("provider") || !payload["provider"].is_object()) payload["provider"] = Json::object();
    for (auto it = provider.begin(); it != provider.end(); ++it) payload["provider"][it.key()] = it.value();
  }
}

Reply parse_reply(const Json& section, const std::string& model_text, const std::string& packet_id, const std::set<std::string>& prior_ids) {
  const Json spec = section.value("outputs", Json::object()).value("graph_reply", Json::object());
  Reply out;
  std::string body = trim(model_text);
  if (json::get_bool(spec, "tolerate_fence", true) && body.rfind("```", 0) == 0) {
    const auto nl = body.find('\n');
    if (nl != std::string::npos) {
      body = trim(body.substr(nl + 1));
      if (body.size() >= 3 && body.compare(body.size() - 3, 3, "```") == 0) body = trim(body.substr(0, body.size() - 3));
      out.fence_stripped = true;
    }
  }
  auto parsed = json::parse(body);
  if (!parsed || !parsed->is_object()) return fail("not_json");
  const Json& g = *parsed;
  static const std::set<std::string> top = {"schema", "base_packet_sha256", "response_id", "nodes", "links"};
  for (auto it = g.begin(); it != g.end(); ++it)
    if (!top.count(it.key())) return fail("unknown_field:" + it.key());
  for (const auto& k : top)
    if (!g.contains(k)) return fail("missing_field:" + k);
  if (!g["schema"].is_string() || g["schema"] != json::get_string(spec, "schema", "loom.graph_reply/2")) return fail("schema_mismatch");
  if (!g["base_packet_sha256"].is_string() || g["base_packet_sha256"] != packet_id) return fail("base_packet_mismatch");
  if (!g["response_id"].is_string() || !g["nodes"].is_array() || g["nodes"].empty() || !g["links"].is_array()) return fail("field_type");

  struct Node { std::string role; Json text; std::vector<std::string> children; };
  std::map<std::string, Node> nodes;
  std::vector<std::string> order;
  for (const auto& n : g["nodes"]) {
    if (!n.is_object()) return fail("node_not_object");
    for (auto it = n.begin(); it != n.end(); ++it)
      if (it.key() != "id" && it.key() != "text" && it.key() != "role" && it.key() != "children") return fail("node_unknown_field:" + it.key());
    if (!n.contains("id") || !n["id"].is_string() || n["id"].get<std::string>().empty()) return fail("node_id");
    if (!n.contains("role") || !n["role"].is_string()) return fail("node_role");
    if (!n.contains("text") || !(n["text"].is_string() || n["text"].is_null())) return fail("node_text");
    if (!n.contains("children") || !n["children"].is_array()) return fail("node_children");
    const std::string id = n["id"].get<std::string>();
    if (nodes.count(id)) return fail("duplicate_id:" + id);
    if (prior_ids.count(id)) return fail("id_collides_with_prior:" + id);
    Node node{n["role"].get<std::string>(), n["text"], {}};
    for (const auto& c : n["children"]) {
      if (!c.is_string()) return fail("child_not_string");
      node.children.push_back(c.get<std::string>());
    }
    nodes[id] = std::move(node);
    order.push_back(id);
  }
  const std::string root = g["response_id"].get<std::string>();
  if (!nodes.count(root)) return fail("response_missing");
  if (nodes[root].role != "response") return fail("response_role");
  // Exactly one rooted tree: breadth-first from the response; a child met twice is shared or a cycle; a node never met is stray.
  std::vector<std::string> bfs;
  std::set<std::string> seen{root};
  std::deque<std::string> queue{root};
  while (!queue.empty()) {
    const std::string id = queue.front();
    queue.pop_front();
    bfs.push_back(id);
    for (const auto& c : nodes[id].children) {
      if (!nodes.count(c)) return fail("unknown_child:" + c);
      if (!seen.insert(c).second) return fail("shared_child_or_cycle:" + c);
      queue.push_back(c);
    }
  }
  if (seen.size() != nodes.size()) return fail("unreachable_node");
  for (auto it = bfs.rbegin(); it != bfs.rend(); ++it) {
    const Node& n = nodes[*it];
    std::string text;
    if (n.children.empty()) {
      if (!n.text.is_string() || n.text.get<std::string>().empty()) return fail("leaf_text:" + *it);
      text = n.text.get<std::string>();
    } else {
      for (const auto& c : n.children) text += out.rendered[c];
      if (n.text.is_string() && n.text.get<std::string>() != text) return fail("composite_text_mismatch:" + *it);
    }
    out.rendered[*it] = text;
  }
  for (const auto& l : g["links"]) {
    if (!l.is_object() || l.size() != 3 || !l.contains("from") || !l.contains("predicate") || !l.contains("to") || !l["from"].is_string() ||
        !l["predicate"].is_string() || !l["to"].is_string() || l["predicate"].get<std::string>().empty())
      return fail("link_shape");
    if (!nodes.count(l["from"].get<std::string>())) return fail("link_from_not_local");
    const std::string to = l["to"].get<std::string>();
    if (!nodes.count(to) && !prior_ids.count(to)) return fail("link_to_unknown:" + to);
  }
  out.text = out.rendered[root];
  out.graph = g;
  out.ok = true;
  return out;
}

std::string invalid_note(const Json& section, const std::string& lang, const std::string& reason) {
  const Json note = section.value("outputs", Json::object()).value("graph_reply", Json::object()).value("notes", Json::object()).value("invalid", Json::object());
  std::string text = json::get_string(note, lang, json::get_string(note, "en", "[graph reply not usable: {{reason}}]"));
  return replace_all(text, "{{reason}}", reason);
}

Json plan_placement(const Json& section, const std::string& message_id, const Reply& reply, const std::set<std::string>& prior_ids) {
  // Placement is a step of its own (an analysis-like node); the embedder only applies its plan. The cascade is data (outputs.graph_reply.placement.cascade);
  // implemented here: explicit_ref (a link names a local node or an existing id) and deterministic_key (a local node gets the id derived from the message and
  // its local id, so placing it twice lands on the same element). Vector similarity, the matching method of the guardian (annotations/matching.cpp) and a model
  // are further steps of the cascade, not run here: what they would place stays unplaced and is listed.
  Json nodes = Json::object(), links = Json::array(), unplaced = Json::array();
  for (const auto& n : reply.graph["nodes"]) {
    const std::string local = n["id"].get<std::string>();
    nodes[local] = "n_" + Sha256::hex(message_id + "/" + local).substr(0, 12);
  }
  for (const auto& l : reply.graph["links"]) {
    const std::string to = l["to"].get<std::string>();
    Json item{{"from", nodes[l["from"].get<std::string>()]}, {"predicate", l["predicate"]}};
    if (nodes.contains(to)) { item["to"] = nodes[to]; item["placed_by"] = "deterministic_key"; }
    else if (prior_ids.count(to)) { item["to"] = to; item["placed_by"] = "explicit_ref"; }
    else { unplaced.push_back(Json{{"link_to", to}, {"why", "no step of the cascade placed it"}}); continue; }
    links.push_back(item);
  }
  return Json{{"nodes", nodes}, {"links", links}, {"unplaced", unplaced}, {"cascade", section.value("outputs", Json::object()).value("graph_reply", Json::object()).value("placement", Json::object()).value("cascade", Json::array())}};
}

Result<Json> embed(Database& db, const Json& section, const std::string& message_id, const std::string& conv_id, const std::string& model,
                   const Reply& reply, const Json& plan) {
  const Json spec = section.value("outputs", Json::object()).value("graph_reply", Json::object());
  const Json cfg = spec.value("embed", Json::object());
  if (!json::get_bool(cfg, "enabled", true)) return Json{{"embedded", false}, {"reason", "disabled by outputs.graph_reply.embed.enabled"}};
  const std::string kind = json::get_string(cfg, "node_kind", "reply_part");
  const std::string part_link = json::get_string(cfg, "part_link", "part_of");
  const std::string message_link = json::get_string(cfg, "message_link", "generated_by");
  const std::string origin = json::get_string(cfg, "link_origin", "model");
  const std::string packet_id = json::get_string(reply.graph, "base_packet_sha256");
  const std::string root = json::get_string(reply.graph, "response_id");
  const Json& global = plan["nodes"];
  std::size_t made_nodes = 0, made_links = 0;
  for (const auto& n : reply.graph["nodes"]) {
    const std::string local = n["id"].get<std::string>();
    const std::string& text = reply.rendered.at(local);
    NodeOptions opts;
    opts.node_id = global[local].get<std::string>();
    opts.content = text;
    Json children = Json::array();
    for (const auto& c : n["children"]) children.push_back(global[c.get<std::string>()]);
    opts.metadata = Json{{"origin", origin}, {"model", model}, {"message_id", message_id}, {"conversation_id", conv_id}, {"local_id", local},
                         {"role", n["role"]}, {"schema", json::get_string(spec, "schema")}, {"base_packet_sha256", packet_id},
                         {"children", children}, {"is_response", local == root}};
    opts.tags = Json::array({"reply_graph"});
    const std::string label = std::string(utf8::prefix(text, 80));
    LOOM_TRY_ASSIGN(auto nid, db.create_node(label.empty() ? local : label, kind, opts));
    (void)nid;
    ++made_nodes;
  }
  const Json structure{{"origin", "reply_structure"}, {"message_id", message_id}};
  for (const auto& n : reply.graph["nodes"]) {
    for (const auto& c : n["children"]) {
      LOOM_TRY_ASSIGN(auto lid, db.create_link(global[c.get<std::string>()].get<std::string>(), global[n["id"].get<std::string>()].get<std::string>(), part_link, 1.0, structure));
      (void)lid;
      ++made_links;
    }
  }
  LOOM_TRY_ASSIGN(auto mlid, db.create_link(global[root].get<std::string>(), message_id, message_link, 1.0, structure));
  (void)mlid;
  ++made_links;
  for (const auto& l : plan["links"]) {
    Json lm{{"origin", origin}, {"message_id", message_id}, {"proposal", true}, {"placed_by", l["placed_by"]}};
    LOOM_TRY_ASSIGN(auto lid, db.create_link(l["from"].get<std::string>(), l["to"].get<std::string>(), l["predicate"].get<std::string>(), 1.0, lm));
    (void)lid;
    ++made_links;
  }
  return Json{{"embedded", true}, {"nodes", made_nodes}, {"links", made_links}, {"root_node", global[root]}, {"unplaced", plan["unplaced"]}};
}

Result<Json> embed_packet(Database& db, const Json& section, const std::string& conv_id, const Json& pkt) {
  const Json spec = section.value("packet", Json::object());
  if (!pkt.is_object() || json::get_string(pkt, "schema") != json::get_string(spec, "schema", "loom.context_graph/1") || !pkt.contains("nodes") ||
      !pkt["nodes"].is_array() || !pkt.contains("links") || !pkt["links"].is_array())
    return Error(Errc::InvalidArgument, "not a " + json::get_string(spec, "schema", "loom.context_graph/1") + " packet");
  const std::string packet_id = json::get_string(pkt, "packet_id");
  Json orphans = Json::array(), conflicts = Json::array();
  std::size_t unchanged = 0, created_nodes = 0, created_links = 0;
  Json created_ids = Json::array();
  std::map<std::string, std::string> target;  // packet node id -> message id in the graph
  LOOM_TRY_ASSIGN(auto existing, db.get_msgs(conv_id));
  std::map<std::string, std::string> earlier;  // packet node id embedded before -> message id
  for (const auto& m : existing) {
    const Json emb = m.metadata.is_object() ? m.metadata.value("embedded", Json::object()) : Json::object();
    if (emb.is_object() && emb.contains("node")) earlier[json::get_string(emb, "node")] = m.id;
  }
  std::map<std::string, std::string> parent_of;  // packet node id -> packet node id it answers (reply_to / follows)
  for (const auto& l : pkt["links"]) {
    if (!json::get_bool(l, "derived", false)) continue;
    const std::string kind = json::get_string(l, "predicate");
    const std::string from = json::get_string(l, "from");
    if (kind == json::get_string(spec.value("predicates", Json::object()), "parent", "reply_to") || !parent_of.count(from)) parent_of[from] = json::get_string(l, "to");
  }
  for (const auto& n : pkt["nodes"]) {
    const std::string pid = json::get_string(n, "id");
    const Json source = n.value("source", Json());
    if (source.is_object()) {
      const std::string id = json::get_string(source, "id");
      LOOM_TRY_ASSIGN(auto row, db.get_msg(id));
      if (!row) { orphans.push_back(Json{{"node", pid}, {"source", id}, {"why", "the source row is gone"}}); continue; }
      target[pid] = id;
      const Json now = raw_of(*row);
      const Json was = source.value("raw", Json::object());
      if (now != was) { conflicts.push_back(Json{{"node", pid}, {"source", id}, {"why", "the row changed since the selection"}}); continue; }
      if (json::get_string(n.value("provenance", Json::object()), "text_form") == "raw" && Sha256::hex(json::get_string(n, "text")) != json::get_string(was, "text_sha256")) {
        conflicts.push_back(Json{{"node", pid}, {"source", id}, {"why", "the raw text differs from the graph; it is not overwritten"}});
        continue;
      }
      ++unchanged;
      continue;
    }
    // No source: new data. A node embedded before is found again; nothing is added twice.
    if (auto it = earlier.find(pid); it != earlier.end()) { target[pid] = it->second; ++unchanged; continue; }
    NewMessage nm;
    nm.conv_id = conv_id;
    nm.role = json::get_string(n, "role", "user");
    nm.text = json::get_string(n, "text");
    nm.weight = json::get_number(n, "weight", 1.0);
    if (auto p = parent_of.find(pid); p != parent_of.end())
      if (auto t = target.find(p->second); t != target.end()) nm.parent_id = t->second;
    nm.metadata = Json{{"embedded", Json{{"packet_id", packet_id}, {"node", pid}, {"origin", json::get_string(n.value("provenance", Json::object()), "origin", "external")}}}};
    LOOM_TRY_ASSIGN(auto mid, db.create_msg(nm));
    target[pid] = mid;
    created_ids.push_back(mid);
    ++created_nodes;
  }
  for (const auto& l : pkt["links"]) {
    if (json::get_bool(l, "derived", false)) continue;  // order and parent are the structure of the nodes, already compared
    const Json source = l.value("source", Json());
    const std::string from = json::get_string(l, "from"), to = json::get_string(l, "to"), type = json::get_string(l, "predicate");
    LOOM_TRY_ASSIGN(auto at_from, db.get_links(from, type));
    const Link* found = nullptr;
    if (source.is_object()) {
      for (const auto& x : at_from) if (x.id == json::get_string(source, "id")) found = &x;
      if (!found) { orphans.push_back(Json{{"link", json::get_string(source, "id")}, {"why", "the source link is gone"}}); continue; }
      if (found->dst != to || found->src != from || found->link_type != type || found->weight != json::get_number(l, "weight", 1.0) || found->metadata != l.value("metadata", Json::object()))
        conflicts.push_back(Json{{"link", found->id}, {"why", "the link changed since the selection"}});
      else ++unchanged;
      continue;
    }
    const auto ends = [&](const std::string& id) { auto t = target.find(id); return t != target.end() ? t->second : id; };
    const std::string src = ends(from), dst = ends(to);
    LOOM_TRY_ASSIGN(auto at_src, db.get_links(src, type));
    bool there = false;
    for (const auto& x : at_src) if (x.src == src && x.dst == dst) there = true;
    if (there) { ++unchanged; continue; }
    LOOM_TRY_ASSIGN(auto lid, db.create_link(src, dst, type, json::get_number(l, "weight", 1.0), l.value("metadata", Json::object())));
    (void)lid;
    ++created_links;
  }
  return Json{{"unchanged", unchanged}, {"created_nodes", created_nodes}, {"created_links", created_links}, {"created_message_ids", created_ids}, {"orphans", orphans}, {"conflicts", conflicts}};
}

}  // namespace loom::chat::formats
