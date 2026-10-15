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

// One axis: the preset name from the scope chain (an override of the request wins), unknown names fall back to the data's `unknown`.
std::string axis_name(const std::filesystem::path& root, const Config& cfg, Database* db, const std::string& conv_id, const Json& part,
                      const std::string& source, const std::optional<std::string>& override_value) {
  const std::string key = json::get_string(part, "setting");
  const std::string installation = json::get_string(part, "active");
  std::string name = installation;
  if (override_value && !override_value->empty()) {
    name = *override_value;
  } else if (!key.empty()) {
    scopes::Binding bind;
    bind.key = key;
    bind.installation = installation;
    bind.installation_source = source;
    bind.conv_id = conv_id;
    const Json v = scopes::value_or(root, cfg, db, bind, Json(installation));
    if (v.is_string() && !v.get<std::string>().empty()) name = v.get<std::string>();
  }
  const Json presets = part.value("presets", Json::object());
  if (!presets.contains(name)) {
    log::write(log::Level::Debug, kLog, "'" + name + "' is not a preset of " + key + "; using " + json::get_string(part, "unknown", installation));
    name = json::get_string(part, "unknown", installation);
  }
  return name;
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
                     const std::optional<std::string>& override_context, const std::optional<std::string>& override_reply, bool reply_allowed) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("guardian", root));
  const Json& values = profile.values();
  if (!values.contains("context_format")) return Error(Errc::NotFound, "profile guardian has no context_format section");
  Axes a;
  a.section = values.at("context_format");
  const Json& section = a.section;
  a.context_format = axis_name(root, cfg, db, conv_id, section, "profile:guardian.context_format.active", override_context);
  const Json& cpreset = section.at("presets").at(a.context_format);
  a.turns = json::get_bool(cpreset, "turns", true);
  a.packet = json::get_bool(cpreset, "packet", false);

  const Json reply = section.value("reply", Json::object());
  a.reply_format = axis_name(root, cfg, db, conv_id, reply, "profile:guardian.context_format.reply.active", override_reply);
  const bool wants_graph = json::get_bool(reply.at("presets").at(a.reply_format), "graph", false);
  if (wants_graph && !reply_allowed) {
    a.flow_reason = "context_execution.graph_reply is active: it owns the graph reply of this send";
  } else if (wants_graph) {
    a.reply_graph = true;
    // The flow follows the class of the model (data), never the model of the conversation as a setting; a known capability refines it.
    const Json models = values.value("models", Json::object());
    a.model_class = annotations::model_tier(models, model);
    const Json flows = reply.value("flows", Json::object());
    a.flow = json::get_string(flows, a.model_class, json::get_string(flows, "unknown", "prompt_only"));
    a.flow_reason = "class " + a.model_class;
    const Json defs = reply.value("flow_defs", Json::object());
    for (auto it = defs.begin(); it != defs.end(); ++it) {
      const std::string needs = json::get_string(it.value(), "requires_capability");
      if (needs.empty()) continue;
      const Json cap = capability_of(cfg, reply, model, needs);
      if (cap.is_boolean() && cap.get<bool>() && a.flow != it.key()) {
        a.flow = it.key();
        a.flow_reason = "model_capabilities: " + needs;
      } else if (cap.is_boolean() && !cap.get<bool>() && a.flow == it.key()) {
        a.flow = json::get_string(it.value(), "fallback_flow", "prompt_only");
        a.flow_reason = "model_capabilities: no " + needs;
      }
    }
    if (!defs.contains(a.flow)) return Error(Errc::InvalidArgument, "context_format.reply.flow_defs has no flow '" + a.flow + "'");
  }
  return a;
}

Json packet(const Json& section, const std::string& conv_id, const std::vector<Entry>& entries) {
  const Json spec = section.value("packet", Json::object());
  const Json predicates = spec.value("predicates", Json::object());
  const std::string origin = json::get_string(spec, "origin", "conversation");
  Json nodes = Json::array(), links = Json::array();
  std::set<std::string> kept;
  for (const auto& e : entries) kept.insert(e.id);
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const Entry& e = entries[i];
    nodes.push_back(Json{{"id", e.id}, {"kind", json::get_string(spec, "node_kind", "message")}, {"role", e.role}, {"text", e.text},
                         {"order", i}, {"weight", e.weight},
                         {"provenance", Json{{"origin", origin}, {"source", "message"}, {"message_id", e.id}, {"conversation_id", conv_id},
                                             {"text_form", e.selected_level ? "selected_level" : "raw"}}}});
    if (i > 0) links.push_back(Json{{"from", e.id}, {"predicate", json::get_string(predicates, "order", "follows")}, {"to", entries[i - 1].id}});
    if (!e.parent_id.empty() && kept.count(e.parent_id) && (i == 0 || entries[i - 1].id != e.parent_id))
      links.push_back(Json{{"from", e.id}, {"predicate", json::get_string(predicates, "parent", "reply_to")}, {"to", e.parent_id}});
  }
  Json p{{"schema", json::get_string(spec, "schema", "loom.context_graph/1")}, {"conversation_id", conv_id}, {"nodes", nodes}, {"links", links}};
  p["packet_id"] = Sha256::hex(json::dump(p));
  return p;
}

std::string packet_block(const Json& section, const Json& pkt) {
  const Json spec = section.value("packet", Json::object());
  return replace_all(json::get_string(spec, "recipe"), "{{packet_id}}", json::get_string(pkt, "packet_id")) + json::dump(pkt);
}

std::string reply_instruction(const Json& section, const std::string& flow, const std::string& packet_id) {
  const Json reply = section.value("reply", Json::object());
  std::string out = json::get_string(reply, "recipe");
  if (!out.empty() && out.back() != '\n') out += "\n";
  out += replace_all(json::get_string(reply, "base_packet_line"), "{{packet_id}}", packet_id);
  const Json def = reply.value("flow_defs", Json::object()).value(flow, Json::object());
  if (json::get_bool(def, "schema_hint", false))
    out += "Answer with exactly one JSON object that follows this JSON schema:\n" + json::dump(reply.value("response_format", Json::object()).value("schema", Json::object())) + "\n";
  return out;
}

void apply_reply_request(Json& payload, const Json& section, const std::string& flow) {
  const Json reply = section.value("reply", Json::object());
  const Json def = reply.value("flow_defs", Json::object()).value(flow, Json::object());
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
  const Json spec = section.value("reply", Json::object());
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
  const Json note = section.value("reply", Json::object()).value("notes", Json::object()).value("invalid", Json::object());
  std::string text = json::get_string(note, lang, json::get_string(note, "en", "[graph reply not usable: {{reason}}]"));
  return replace_all(text, "{{reason}}", reason);
}

Result<Json> embed(Database& db, const Json& section, const std::string& message_id, const std::string& conv_id, const std::string& model,
                   const Reply& reply) {
  const Json spec = section.value("reply", Json::object());
  const Json cfg = spec.value("embed", Json::object());
  if (!json::get_bool(cfg, "enabled", true)) return Json{{"embedded", false}, {"reason", "disabled by context_format.reply.embed.enabled"}};
  const std::string kind = json::get_string(cfg, "node_kind", "reply_part");
  const std::string part_link = json::get_string(cfg, "part_link", "part_of");
  const std::string message_link = json::get_string(cfg, "message_link", "generated_by");
  const std::string origin = json::get_string(cfg, "link_origin", "model");
  const std::string packet_id = json::get_string(reply.graph, "base_packet_sha256");
  const std::string root = json::get_string(reply.graph, "response_id");
  std::map<std::string, std::string> global;  // local id -> node id
  for (const auto& n : reply.graph["nodes"]) {
    const std::string local = n["id"].get<std::string>();
    global[local] = "n_" + Sha256::hex(message_id + "/" + local).substr(0, 12);
  }
  std::size_t made_nodes = 0, made_links = 0;
  for (const auto& n : reply.graph["nodes"]) {
    const std::string local = n["id"].get<std::string>();
    const std::string& text = reply.rendered.at(local);
    NodeOptions opts;
    opts.node_id = global[local];
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
  const Json prov{{"origin", origin}, {"message_id", message_id}};
  for (const auto& n : reply.graph["nodes"]) {
    const std::string local = n["id"].get<std::string>();
    for (const auto& c : n["children"]) {
      LOOM_TRY_ASSIGN(auto lid, db.create_link(global[c.get<std::string>()], global[local], part_link, 1.0, Json{{"origin", "reply_structure"}, {"message_id", message_id}}));
      (void)lid;
      ++made_links;
    }
  }
  LOOM_TRY_ASSIGN(auto mlid, db.create_link(global[root], message_id, message_link, 1.0, Json{{"origin", "reply_structure"}, {"message_id", message_id}}));
  (void)mlid;
  ++made_links;
  for (const auto& l : reply.graph["links"]) {
    const std::string to = l["to"].get<std::string>();
    const auto local_to = global.find(to);
    Json lm = prov;
    lm["proposal"] = true;
    LOOM_TRY_ASSIGN(auto lid, db.create_link(global[l["from"].get<std::string>()], local_to != global.end() ? local_to->second : to,
                                             l["predicate"].get<std::string>(), 1.0, lm));
    (void)lid;
    ++made_links;
  }
  return Json{{"embedded", true}, {"nodes", made_nodes}, {"links", made_links}, {"root_node", global[root]}};
}

}  // namespace loom::chat::formats
