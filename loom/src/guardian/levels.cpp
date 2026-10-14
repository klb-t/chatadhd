// The shared annotation layer (span -> category) and the lossy levels derived from it.
#include <algorithm>
#include <map>
#include <set>

#include "internal.h"
#include "loom/log.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"
#include "text.h"

namespace loom::guardian::detail {
namespace {

constexpr const char* kProducer = "guardian_heuristic";

const Json& annotations_cfg(const Data& d) { return d.p.at("annotations"); }

std::string category_node(Database& db, const Data& d, const std::string& category, std::map<std::string, std::string>& cache) {
  if (auto it = cache.find(category); it != cache.end()) return it->second;
  const Json& a = annotations_cfg(d);
  NodeOptions o;
  o.metadata = Json{{"schema", "loom.annotation_category/1"}, {"category", category}};
  o.tags = Json::array({"annotation_category"});
  auto id = db.get_or_create_node(json::get_string(a, "label_prefix", "annotation:") + category, json::get_string(a, "node_kind", "annotation_category"), o);
  std::string out = id ? *id : std::string();
  cache[category] = out;
  return out;
}

// The categories a level drops, following `inherits`.
std::set<std::string> level_categories(const Data& d, const std::string& level) {
  std::set<std::string> out;
  const Json& defs = d.p.at("levels").at("defs");
  std::string cur = level;
  for (int guard = 0; guard < 8 && !cur.empty(); ++guard) {
    const Json* def = json::find(defs, cur);
    if (!def) break;
    for (const auto& c : text::strings_of(json::find(*def, "drop_categories"))) out.insert(c);
    cur = json::get_string(*def, "inherits");
  }
  return out;
}

}  // namespace

std::string level_label(const std::string& message_id, const std::string& level) { return "lvl:" + message_id + ":" + level; }

Result<Json> annotations_of(Database& db, const Data& d, const std::string& message_id) {
  MessageAnnotationListOptions o;
  o.limit = std::nullopt;
  LOOM_TRY_ASSIGN(auto list, db.list_message_annotations(message_id, o));
  const std::string prefix = json::get_string(annotations_cfg(d), "label_prefix", "annotation:");
  std::map<std::string, std::string> categories;
  Json out = Json::array();
  for (const auto& a : list) {
    if (a.state != "active") continue;
    std::string category = json::get_string(a.metadata, "category");
    if (category.empty() && !a.node_id.empty()) {
      auto n = db.get_node(a.node_id);
      if (n && *n && (*n)->label.rfind(prefix, 0) == 0) category = (*n)->label.substr(prefix.size());
    }
    if (category.empty()) continue;
    out.push_back(Json{{"id", a.id}, {"category", category}, {"producer", json::get_string(a.metadata, "producer", a.origin)}, {"origin", a.origin},
                       {"confidence", json::get_number(a.metadata, "confidence", 1.0)}, {"start_char", a.start_char}, {"end_char", a.end_char},
                       {"revision", a.revision}, {"excerpt", a.excerpt}});
  }
  return out;
}

Result<int> annotate_message(Database& db, const Data& d, const Message& m) {
  const auto roles = text::strings_of(json::find(d.p.at("levels"), "roles"));
  if (std::find(roles.begin(), roles.end(), m.role) == roles.end() || is_traffic(d, m) || m.text.empty()) return 0;
  {
    MessageAnnotationListOptions all;
    all.limit = std::nullopt;
    all.include_retracted = true;  // a noise annotation the user took back is not written again
    LOOM_TRY_ASSIGN(auto have, db.list_message_annotations(m.id, all));
    for (const auto& a : have) {
      const std::string c = json::get_string(a.metadata, "category");
      if (json::get_string(a.metadata, "producer") == kProducer && (c == "filler" || c == "repetition" || c == "restart")) return 0;
    }
  }
  const auto cps = utf8::decode(m.text);
  const auto spans = text::noise_spans(cps, d.p.at("levels").at("noise"), d.fold);
  std::map<std::string, std::string> cache;
  int written = 0;
  const std::string hash = sha256_hex(m.text);
  for (const auto& s : spans) {
    NewMessageAnnotation n;
    n.message_id = m.id;
    n.node_id = category_node(db, d, s.category, cache);
    if (n.node_id.empty()) continue;
    n.start_char = static_cast<std::int64_t>(s.start);
    n.end_char = static_cast<std::int64_t>(s.end);
    n.origin = json::get_string(annotations_cfg(d).at("producers").at(kProducer), "origin", "recorded");
    n.metadata = Json{{"schema", json::get_string(annotations_cfg(d), "schema")}, {"category", s.category}, {"producer", kProducer}, {"confidence", 1.0}, {"matched", s.matched}};
    n.expected_text_hash = hash;
    LOOM_TRY_ASSIGN(auto made, db.create_message_annotation(n));
    (void)made;
    ++written;
  }
  return written;
}

Result<Json> build_levels(Database& db, const Data& d, const Message& m) {
  Json out{{"message_id", m.id}, {"levels", Json::object()}};
  LOOM_TRY_ASSIGN(Json anns, annotations_of(db, d, m.id));
  const auto cps = utf8::decode(m.text);
  const double min_conf = json::get_number(d.p.at("levels"), "min_confidence", 0.5);
  const Json& defs = d.p.at("levels").at("defs");
  const std::string hash = sha256_hex(m.text);
  for (const auto& name : text::strings_of(json::find(d.p.at("levels"), "order"))) {
    const Json* def = json::find(defs, name);
    if (!def || json::get_string(*def, "kind") == "compiled") continue;
    const auto cats = level_categories(d, name);
    if (cats.empty()) continue;
    std::vector<std::pair<std::size_t, std::size_t>> spans;
    Json dropped = Json::array();
    for (const auto& a : anns) {
      if (!cats.count(a.value("category", "")) || a.value("confidence", 1.0) < min_conf) continue;
      const auto from = static_cast<std::size_t>(a.value("start_char", 0)), to = std::min(static_cast<std::size_t>(a.value("end_char", 0)), cps.size());
      if (to <= from) continue;
      spans.emplace_back(from, to);
      dropped.push_back(Json{{"category", a["category"]}, {"annotation_id", a["id"]}, {"producer", a["producer"]}, {"start_char", from}, {"end_char", to},
                             {"text", utf8::encode(std::u32string_view(cps).substr(from, to - from))}});
    }
    std::string derived = dropped.empty() ? std::string() : text::without(cps, spans);
    const bool usable = !dropped.empty() && !derived.empty() && derived != m.text;
    const std::string label = level_label(m.id, name);
    LOOM_TRY_ASSIGN(auto existing, db.find_node(label, json::get_string(d.p.at("nodes"), "level", "guardian_level")));
    if (!usable && !existing) continue;  // nothing to say, nothing recorded
    Json meta{{"schema", "loom.guardian_level/1"}, {"level", name}, {"message_id", m.id}, {"conv_id", m.conv_id}, {"source_text_sha256", hash},
              {"dropped", dropped}, {"usable", usable}, {"updated", timeutil::utc_now_iso()}};
    if (!usable && !dropped.empty()) meta["note"] = "everything would be dropped; the raw text stays";
    std::string node_id;
    if (existing) {
      node_id = existing->id;
      NodePatch p;
      p.metadata = meta;
      p.content = usable ? derived : std::string();
      LOOM_TRY(db.update_node(node_id, p));
    } else {
      NodeOptions o;
      o.content = derived;
      o.metadata = meta;
      o.tags = Json::array({"guardian_level", name});
      LOOM_TRY_ASSIGN(node_id, db.create_node(label, json::get_string(d.p.at("nodes"), "level", "guardian_level"), o));
      LOOM_TRY_ASSIGN(auto linked, db.create_link(node_id, m.id, json::get_string(d.p.at("nodes").at("links"), "evidence", "derived_from"), 1.0, Json{{"role", "level_of"}, {"level", name}}));
      (void)linked;
    }
    out["levels"][name] = Json{{"node", node_id}, {"text", usable ? derived : m.text}, {"usable", usable}, {"dropped", dropped}};
  }
  return out;
}

std::optional<std::string> level_text(Database& db, const Data& d, const Message& m, const std::string& level) {
  auto node = db.find_node(level_label(m.id, level), json::get_string(d.p.at("nodes"), "level", "guardian_level"));
  if (!node || !*node) return std::nullopt;
  const Node& n = **node;
  if (!json::get_bool(n.metadata, "usable", false) || n.content.empty()) return std::nullopt;
  if (json::get_string(n.metadata, "source_text_sha256") != sha256_hex(m.text)) return std::nullopt;  // the message changed since
  return n.content;
}

}  // namespace loom::guardian::detail
