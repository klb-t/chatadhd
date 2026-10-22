// The perspective sent_to_model over the projection (source "receipt", M1-K1c2): the record of one request - what was put into it and what
// was left out - drawn as nodes and links. See graph/projection.h render_receipt for the contract.
//
// A projection: computed on demand from the record stored with the message the person sent (metadata.context_trace.receipt); nothing is
// written to the nodes or links tables, so deleting this file (and the source "receipt" of the perspective) removes the whole feature.
// Every word the owner reads - the labels of derived nodes, the names of sections and reasons, the empty-graph sentence - is data
// (runtime/chat_ops.pack texts sent_*, pl + en); this file owns only ids and the shape.
#include <algorithm>
#include <map>
#include <set>

#include "chat/context_format.h"
#include "graph/projection.h"
#include "loom/db.h"
#include "loom/log.h"
#include "loom/memory_engine.h"
#include "loom/runtime_profile.h"
#include "loom/util/utf8.h"

namespace loom::projection {

namespace {

constexpr const char* kLog = "loom.projection.receipt";

bool has_prefix(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(0, p.size()) == p; }

// The texts of chat_ops.pack (the words of the action context.receipt and of this projection), in the language of the render.
struct Words {
  Json texts = Json::object();
  std::string lang = "en";
  Json settings = Json::object();  // chat_ops.pack receipt_graph

  static Words load(const std::filesystem::path& root, const std::string& requested) {
    Words w;
    w.lang = requested.empty() ? std::string("en") : requested.substr(0, requested.find_first_of("-_"));
    if (auto p = RuntimeProfile::load("chat_ops", root)) {
      w.texts = p->values().value("texts", Json::object());
      w.settings = p->values().value("receipt_graph", Json::object());
    } else {
      log::write(log::Level::Debug, kLog, "chat_ops not readable: " + p.error().message);
    }
    return w;
  }
  std::optional<std::string> find(const std::string& id) const {
    for (const std::string& l : {lang, std::string("en")}) {
      const Json* table = json::find(texts, l);
      const Json* t = table ? json::find(*table, id) : nullptr;
      if (t && t->is_string()) return t->get<std::string>();
    }
    return std::nullopt;
  }
  std::string say(const std::string& id, const Json& vars = Json::object()) const {
    const auto t = find(id);
    if (!t) return id;
    auto rendered = render_profile_template(*t, vars);
    return rendered ? *rendered : *t;
  }
  // The word for `id`, else `fallback` (the receipt's own label, or the raw code).
  std::string word_or(const std::string& id, const std::string& fallback) const {
    const auto t = find(id);
    return t ? *t : fallback;
  }
};

std::string kind_of_source(const std::string& source_kind) {
  const auto at = source_kind.find(':');
  const std::string k = source_kind.substr(0, at);
  return k.empty() ? std::string("item") : k;
}

}  // namespace

Result<Json> render_receipt(const Env& env, const std::set<std::string>& kinds, const Json& request) {
  if (!env.db) return Error(Errc::InvalidArgument, "perspective sent_to_model needs a graph (a database)");
  LOOM_TRY_ASSIGN(auto cat, Catalogue::open(env));
  return render_receipt(cat, kinds, request);
}

Result<Json> render_receipt(Catalogue& cat, const std::set<std::string>& kinds, const Json& request) {
  const Env& env = cat.env();
  if (!env.db) return Error(Errc::InvalidArgument, "perspective sent_to_model needs a graph (a database)");
  const Words words = Words::load(env.root, json::get_string(request, "lang", env.lang));

  std::string named = json::get_string(request, "msg_id");
  if (named.empty()) {
    const Json seeds = request.value("seeds", Json::array());
    if (seeds.is_array() && !seeds.empty() && seeds[0].is_string()) named = seeds[0].get<std::string>();
  }
  if (named.empty()) named = json::get_string(request, "focus");
  if (named.empty()) return Error(Errc::InvalidArgument, "perspective sent_to_model needs request.msg_id (the message whose record to show)");
  LOOM_TRY_ASSIGN(auto stored, chat::formats::stored_receipt(*env.db, cat.db_id(named)));

  const std::string req = stored.request_msg_id;
  const std::string req_address = cat.canonical(req);
  std::int64_t label_chars = request.contains("label_chars") && request["label_chars"].is_number_integer()
                                 ? request["label_chars"].get<std::int64_t>()
                                 : json::get_int(words.settings, "label_chars", 80);
  const auto preview = [&](const std::string& text) {
    std::string flat;
    for (const char c : text) flat += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    if (label_chars <= 0 || utf8::length(flat) <= static_cast<std::size_t>(label_chars)) return flat;
    return std::string(utf8::prefix(flat, static_cast<std::size_t>(label_chars))) + "…";
  };

  Json ks = Json::array();
  for (const auto& k : kinds) ks.push_back(k);
  Json out{{"schema", "loom.perspective_render/1"}, {"source", "receipt"}, {"edge_kinds", ks}, {"direction", "out"}, {"hops", Json(nullptr)},
           {"msg_id", stored.msg_id}, {"request_msg_id", req},
           {"focus", Json{{"id", req}, {"address", req_address}}}};

  // No record: before M1, or context_receipt_store=never / trace_context=false / the request opted out. The same words as the action
  // context.receipt, an empty graph, no error.
  if (!stored.receipt.is_object()) {
    out["nodes"] = Json::array();
    out["edges"] = Json::array();
    out["dropped"] = Json::array();
    out["sections"] = Json::array();
    out["empty"] = true;
    out["receipt"] = Json{{"present", false}};
    out["missing_reason"] = "before_m1";
    out["also_when"] = Json::array({"context_receipt_store=never", "trace_context=false"});
    out["text"] = words.say("context_receipt_missing");
    out["counts"] = Json{{"nodes", 0}, {"edges", 0}, {"derived_nodes", 0}, {"resolved_nodes", 0}, {"sent", 0}, {"left_out", 0}};
    return out;
  }
  const Json& receipt = stored.receipt;

  Json nodes = Json::array(), edges = Json::array();
  std::set<std::string> have;
  std::int64_t derived_nodes = 0, resolved_nodes = 0, sent = 0, left_out = 0, link_no = 0;
  const auto add_node = [&](Json n) {
    if (have.insert(n.at("id").get<std::string>()).second) {
      if (n.value("derived", false)) ++derived_nodes;
      else ++resolved_nodes;
      nodes.push_back(std::move(n));
    }
  };
  const auto add_link = [&](const std::string& type, const std::string& dst, double weight, Json metadata) {
    if (!kinds.count(type)) return;
    Json e{{"id", "sent:" + req + ":link:" + std::to_string(++link_no)}, {"src", req}, {"dst", dst}, {"type", type}, {"kind", type}, {"weight", weight},
           {"derived", true}, {"label", cat.edge_label(type)}, {"meaning", cat.edge_meaning(type)}, {"metadata", std::move(metadata)}};
    if (const std::string inv = cat.edge_inverse(type); !inv.empty()) e["inverse"] = inv;
    edges.push_back(std::move(e));
  };

  // The request itself: the node every link starts from.
  {
    std::string label = req;
    std::string role = "user";
    std::string conv;
    if (stored.request) { label = preview(stored.request->text); role = stored.request->role; conv = stored.request->conv_id; }
    add_node(Json{{"id", req}, {"address", req_address}, {"kind", "message"}, {"label", label}, {"derived", false},
                  {"metadata", Json{{"role", role}, {"conv_id", conv}, {"focus", true}}}});
  }

  // A ref that is a message or a node of the database is THAT node (the person can go on from it); anything else has no row.
  const auto resolve = [&](const std::string& ref) -> Result<std::optional<Json>> {
    if (has_prefix(ref, "m_")) {
      LOOM_TRY_ASSIGN(auto m, env.db->get_msg(ref));
      if (m) return std::optional<Json>(Json{{"id", ref}, {"address", cat.canonical(ref)}, {"kind", "message"}, {"label", preview(m->text)}, {"derived", false},
                                             {"metadata", Json{{"role", m->role}, {"conv_id", m->conv_id}}}});
    } else if (has_prefix(ref, "n_")) {
      LOOM_TRY_ASSIGN(auto n, env.db->get_node(ref));
      if (n) return std::optional<Json>(Json{{"id", ref}, {"address", cat.canonical(ref)}, {"kind", n->kind}, {"label", n->label}, {"derived", false},
                                             {"metadata", Json::object()}});
    }
    // A node of the memory tree (memory.json), listed by the receipt per element: the node of the source "memory.notes" (address memory:<id>).
    if (env.memory && !ref.empty() && ref.find(':') == std::string::npos)
      if (const auto mn = env.memory->get_node(ref))
        return std::optional<Json>(Json{{"id", "memory:" + ref}, {"address", "memory:" + ref}, {"kind", "memory_note"}, {"label", preview(mn->content)}, {"derived", false},
                                        {"metadata", Json{{"node_type", mn->node_type}, {"tags", mn->tags}}}});
    return std::optional<Json>();
  };
  // The node for a ref: the real one, else a derived one (nothing is written; the id is stable for this request and ref).
  const auto node_for = [&](const std::string& ref, const std::string& label, const std::string& source_kind, const std::string& cls) -> Result<std::string> {
    LOOM_TRY_ASSIGN(auto real, resolve(ref));
    if (real) {
      const std::string id = real->at("id").get<std::string>();
      add_node(std::move(*real));
      return id;
    }
    const std::string id = "sent:" + req + ":" + ref;
    add_node(Json{{"id", id}, {"address", id}, {"kind", kind_of_source(source_kind)}, {"label", words.word_or("sent_item_" + ref, label.empty() ? ref : label)},
                  {"derived", true},
                  {"metadata", Json{{"derived", true}, {"ref", ref}, {"source_kind", source_kind}, {"class", cls}, {"request_msg_id", req}}}});
    return id;
  };

  const bool truncated = json::get_bool(receipt, "truncated", false);
  Json section_rows = Json::array();
  for (const auto& s : receipt.value("sections", Json::array())) {
    const std::string cls = json::get_string(s, "class");
    const Json items = s.value("items", Json::array());
    const std::int64_t omitted = json::get_int(s, "items_omitted", 0);
    section_rows.push_back(Json{{"class", cls}, {"label", words.word_or("sent_section_" + cls, cls)}, {"tokens_est", s.value("tokens_est", 0)},
                                {"items", items.size()}, {"items_omitted", omitted}});
    if (items.is_array() && !items.empty()) {
      for (const auto& it : items) {
        const std::string ref = json::get_string(it, "ref");
        const std::string label = json::get_string(it, "label");
        const std::string source_kind = json::get_string(it, "source_kind");
        LOOM_TRY_ASSIGN(auto dst, node_for(ref, label, source_kind, cls));
        add_link("sent_to_model", dst, json::get_number(it, "tokens_est", 0.0),
                 Json{{"class", cls}, {"section", words.word_or("sent_section_" + cls, cls)}, {"why", json::get_string(it, "why")}, {"source_kind", source_kind},
                      {"ref", ref}, {"label", label}, {"tokens_est", it.value("tokens_est", 0)}});
        ++sent;
      }
    } else if (omitted > 0) {
      // A shortened record (context_receipt_max_bytes) keeps the section and its size but not the list: the section stands for its items.
      const std::string id = "sent:" + req + ":section:" + cls;
      add_node(Json{{"id", id}, {"address", id}, {"kind", "section"},
                    {"label", words.say("sent_section_omitted_label", Json{{"what", words.word_or("sent_section_" + cls, cls)}, {"items", omitted}})},
                    {"derived", true}, {"metadata", Json{{"derived", true}, {"class", cls}, {"items_omitted", omitted}, {"request_msg_id", req}}}});
      add_link("sent_to_model", id, json::get_number(s, "tokens_est", 0.0),
               Json{{"class", cls}, {"section", words.word_or("sent_section_" + cls, cls)}, {"items_omitted", omitted}, {"truncated", true}, {"tokens_est", s.value("tokens_est", 0)}});
      ++sent;
    }
  }

  // What was NOT sent, and why: visible as links of the second relation, never silently omitted.
  Json dropped_rows = Json::array();
  for (const auto& d : receipt.value("dropped", Json::array())) {
    const std::string group = json::get_string(d, "ref");
    const std::string reason = json::get_string(d, "reason");
    const std::int64_t count = json::get_int(d, "count", 0);
    const std::string reason_human = words.word_or("sent_reason_" + reason, reason);
    const std::string group_human = words.word_or("sent_section_" + group, group);
    std::vector<std::string> refs;
    for (const auto& r : d.value("refs", Json::array()))
      if (r.is_string()) refs.push_back(r.get<std::string>());
    dropped_rows.push_back(Json{{"ref", group}, {"label", group_human}, {"reason", reason}, {"reason_human", reason_human}, {"count", count}, {"refs", d.value("refs", Json::array())}});
    const Json base{{"group", group}, {"section", group_human}, {"reason", reason}, {"reason_human", reason_human}, {"group_count", count}};
    if (refs.empty()) {
      // Only the group is known (a source switched off, a shortened record): one node for the group and the reason.
      const std::string id = "sent:" + req + ":dropped:" + group + ":" + reason;
      add_node(Json{{"id", id}, {"address", id}, {"kind", "left_out"},
                    {"label", words.say("sent_dropped_label", Json{{"what", group_human}, {"reason", reason_human}, {"count", count}})},
                    {"derived", true}, {"metadata", Json{{"derived", true}, {"group", group}, {"reason", reason}, {"count", count}, {"request_msg_id", req}}}});
      add_link("left_out_of_model", id, static_cast<double>(count), base);
      ++left_out;
    } else {
      for (const auto& ref : refs) {
        LOOM_TRY_ASSIGN(auto dst, node_for(ref, ref, "left_out", group));
        Json meta = base;
        meta["ref"] = ref;
        add_link("left_out_of_model", dst, 1.0, std::move(meta));
        ++left_out;
      }
    }
  }

  // A cap asked for by the request (none by default): the request message first, the links to what is shown only.
  const std::int64_t limit = request.contains("limit") && request["limit"].is_number_integer() ? request["limit"].get<std::int64_t>() : -1;
  bool cut = false;
  if (limit >= 0 && static_cast<std::int64_t>(nodes.size()) > limit) {
    Json kept = Json::array();
    std::set<std::string> shown;
    for (auto& n : nodes) {
      if (static_cast<std::int64_t>(kept.size()) >= limit) break;
      shown.insert(n.at("id").get<std::string>());
      kept.push_back(std::move(n));
    }
    Json kept_edges = Json::array();
    for (auto& e : edges)
      if (shown.count(e.at("src").get<std::string>()) && shown.count(e.at("dst").get<std::string>())) kept_edges.push_back(std::move(e));
    nodes = std::move(kept);
    edges = std::move(kept_edges);
    cut = true;
  }

  out["nodes"] = nodes;
  out["edges"] = edges;
  out["sections"] = section_rows;
  out["dropped"] = dropped_rows;
  out["receipt"] = Json{{"present", true}, {"schema", json::get_string(receipt, "schema")}, {"tokens_est", receipt.value("tokens_est", 0)},
                        {"truncated", truncated}, {"sections", receipt.value("sections", Json::array()).size()}, {"sha256", json::get_string(receipt, "sha256")}};
  if (truncated) out["receipt"]["truncated_reason"] = json::get_string(receipt, "truncated_reason");
  out["counts"] = Json{{"nodes", nodes.size()}, {"edges", edges.size()}, {"derived_nodes", derived_nodes}, {"resolved_nodes", resolved_nodes}, {"sent", sent}, {"left_out", left_out}};
  out["note"] = words.say("context_receipt_note");
  if (cut) out["truncated"] = true;
  return out;
}

std::vector<Json> receipt_edges(const Json& render, const std::set<std::string>& kinds) {
  std::map<std::string, std::string> address_of;
  const Json nodes = render.value("nodes", Json::array());
  for (const auto& n : nodes) address_of[json::get_string(n, "id")] = json::get_string(n, "address", json::get_string(n, "id"));
  const auto at = [&](const std::string& id) {
    const auto it = address_of.find(id);
    return it == address_of.end() ? id : it->second;
  };
  std::vector<Json> out;
  const Json edges = render.value("edges", Json::array());
  for (const auto& e : edges) {
    const std::string kind = json::get_string(e, "type", json::get_string(e, "kind"));
    if (!kinds.empty() && !kinds.count(kind)) continue;
    out.push_back(Json{{"src", at(json::get_string(e, "src"))}, {"dst", at(json::get_string(e, "dst"))}, {"kind", kind}, {"id", json::get_string(e, "id")},
                       {"weight", e.value("weight", Json(0))}, {"metadata", e.value("metadata", Json::object())}, {"derived", true}});
  }
  return out;
}

Result<std::optional<Json>> receipt_node(Catalogue& cat, const std::string& address) {
  if (!has_prefix(address, "sent:")) return std::optional<Json>();
  const std::string rest = address.substr(5);
  const auto colon = rest.find(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= rest.size()) return std::optional<Json>();
  const std::string req = rest.substr(0, colon);
  auto got = cat.receipt_of(req);
  if (!got) {
    // not an error of the person's: an address of a request that is gone (or never was) is simply not a node
    log::write(log::Level::Debug, kLog, "receipt_node " + address + ": " + got.error().message);
    return std::optional<Json>();
  }
  const Json& render = **got;
  if (!json::get_bool(render.value("receipt", Json::object()), "present", false)) return std::optional<Json>();
  const Json nodes = render.value("nodes", Json::array());
  const Json* found = nullptr;
  for (const auto& n : nodes)
    if (json::get_string(n, "id") == address) found = &n;
  if (!found) return std::optional<Json>();
  const Words words = Words::load(cat.env().root, cat.env().lang);
  const std::string req_address = json::get_string(render.value("focus", Json::object()), "address");

  // What the person sees of it: the links that reach it (their weight is the tokens) and the facts the record gives.
  const Json all_edges = render.value("edges", Json::array());
  Json links = Json::array();
  Json facts = Json::object();
  const Json meta = found->value("metadata", Json::object());
  for (const auto& k : {"ref", "source_kind", "class", "items_omitted", "group", "reason", "count"})
    if (meta.contains(k)) facts[k] = meta[k];
  double tokens = -1;
  bool sent_link = false;
  for (const auto& e : all_edges) {
    if (json::get_string(e, "dst") != address) continue;
    const std::string kind = json::get_string(e, "type", json::get_string(e, "kind"));
    const Json m = e.value("metadata", Json::object());
    links.push_back(Json{{"kind", kind}, {"label", e.value("label", Json(kind))}, {"weight", e.value("weight", Json(0))}});
    for (const auto& k : {"class", "section", "why", "source_kind", "ref", "tokens_est", "reason", "reason_human", "group", "group_count", "truncated", "items_omitted"})
      if (m.contains(k) && !facts.contains(k)) facts[k] = m[k];
    if (kind == "sent_to_model") {
      sent_link = true;
      tokens = json::get_number(e, "weight", 0.0);
    }
  }
  facts["request"] = req_address;
  facts["request_msg_id"] = req;
  facts["links"] = links;
  std::string why = json::get_string(facts, "why");
  if (why.empty()) why = json::get_string(facts, "section");
  const Json record_evidence{{"class", "observed"}, {"confidence", 1.0},
                             {"provenance", Json{{"layer", "record"}, {"source_ref", cat.canonical(req) + " metadata.context_trace.receipt"},
                                                 {"method", "context_receipt"}, {"time", nullptr}}}};
  Json row{{"address", address},
           {"label", found->value("label", Json(address))},
           {"kind", found->value("kind", Json("item"))},
           {"source", "receipt"},
           {"parent", req_address},
           {"evidence", record_evidence},
           {"provenance", Json{{"layer", "record"}, {"layers", Json::array({"record"})}}},
           {"has_children", false},
           {"derived", true},
           {"derived_note", words.say("sent_derived_note")},
           {"receipt", facts},
           {"why", why},
           {"attrs", Json::object()}};
  if (sent_link) {
    row["value"] = tokens;
    row["value_human"] = words.say("sent_tokens_value", Json{{"tokens", static_cast<std::int64_t>(tokens)}});
    row["evidence_overrides"] = Json{{"value", Json{{"class", "inferred"}, {"confidence", nullptr},
                                                    {"provenance", Json{{"layer", "record"}, {"source_ref", "chars / codepoints_per_token"}, {"method", "token_estimate"}, {"time", nullptr}}}}}};
  } else if (const std::string rh = json::get_string(facts, "reason_human"); !rh.empty()) {
    row["value_human"] = rh;
  }
  return std::optional<Json>(std::move(row));
}

std::string receipt_words(const Env& env, const std::string& id) { return Words::load(env.root, env.lang).say(id); }

Json receipt_summary(const Json& render) {
  Json out = Json::object();
  for (const char* k : {"msg_id", "request_msg_id", "receipt", "counts", "sections", "dropped", "note", "empty", "missing_reason", "also_when", "text", "truncated"})
    if (render.contains(k)) out[k] = render[k];
  out["present"] = json::get_bool(render.value("receipt", Json::object()), "present", false);
  out["request_address"] = json::get_string(render.value("focus", Json::object()), "address");
  return out;
}

}  // namespace loom::projection
