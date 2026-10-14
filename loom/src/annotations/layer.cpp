// The shared annotation layer as a contract. See loom/annotation_layer.h.
#include "loom/annotation_layer.h"

#include <algorithm>
#include <cmath>

#include "loom/runtime_profile.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"

namespace loom::annotations {
namespace {

bool listed(const Json& arr, const std::string& v) {
  if (!arr.is_array()) return false;
  for (const auto& x : arr)
    if (x.is_string() && x.get<std::string>() == v) return true;
  return false;
}

std::string category_of(Database& db, const Policy& pol, const MessageAnnotation& a) {
  std::string c = json::get_string(a.metadata, "category");
  if (!c.empty() || a.node_id.empty()) return c;
  auto n = db.get_node(a.node_id);
  const std::string prefix = pol.label_prefix();
  if (n && *n && (*n)->label.rfind(prefix, 0) == 0) c = (*n)->label.substr(prefix.size());
  return c;
}

bool overlaps(std::int64_t s1, std::int64_t e1, std::int64_t s2, std::int64_t e2) { return s1 < e2 && s2 < e1; }

// Does a stand above b? user > confidence; at a tie the newer one (the caller passes the new claim as `a`) wins.
int rank_origin(const std::string& origin) { return origin == "user" ? 2 : 0; }

Status patch_lifecycle(Database& db, const MessageAnnotation& a, const std::string& lifecycle, const std::string& by, const std::string& state) {
  Json meta = a.metadata.is_object() ? a.metadata : Json::object();
  meta["lifecycle"] = lifecycle;
  if (by.empty()) meta.erase("superseded_by");
  else meta["superseded_by"] = by;
  MessageAnnotationPatch p;
  p.state = state;
  p.metadata = meta;
  auto r = db.revise_message_annotation(a.id, a.revision, p);
  if (!r) return r.error();
  return ok_status();
}

}  // namespace

Result<Policy> Policy::load(const std::filesystem::path& root) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("guardian", root));
  Policy p;
  p.p = profile.values().value("annotations", Json::object());
  return p;
}

std::string Policy::label_prefix() const { return json::get_string(p, "label_prefix", "annotation:"); }
std::string Policy::node_kind() const { return json::get_string(p, "node_kind", "annotation_category"); }

std::string Policy::class_of(const std::string& category) const {
  const Json* classes = json::find(p, "classes");
  if (classes && classes->is_object())
    for (auto it = classes->begin(); it != classes->end(); ++it)
      if (listed(it.value(), category)) return it.key();
  return {};
}

std::string Policy::polarity_of(const std::string& category) const {
  const Json* pol = json::find(p, "polarity");
  if (pol && pol->is_object())
    for (const char* k : {"positive", "negative"})
      if (listed(pol->value(k, Json::array()), category)) return k;
  return "neutral";
}

std::string method_version(const std::string& material) { return Sha256::hex(material).substr(0, 12); }

std::string state_of(const MessageAnnotation& a) {
  if (a.state == "active") return "active";
  return json::get_string(a.metadata, "lifecycle") == "superseded" ? "superseded" : "retracted";
}

Result<Written> write(Database& db, const Policy& pol, const Claim& c) {
  if (c.message_id.empty() || c.category.empty()) return Error(Errc::InvalidArgument, "an annotation needs a message and a category");
  if (c.producer.empty()) return Error(Errc::InvalidArgument, "an annotation needs a producer");
  LOOM_TRY_ASSIGN(auto msg, db.get_msg(c.message_id));
  if (!msg) return Error(Errc::NotFound, "no message '" + c.message_id + "'");
  const auto length = static_cast<std::int64_t>(utf8::length(msg->text));
  const std::int64_t start = std::max<std::int64_t>(0, c.start_char);
  const std::int64_t end = c.end_char < 0 ? length : std::min(c.end_char, length);
  if (end <= start) return Error(Errc::InvalidArgument, "an annotation needs a non-empty range");
  const std::string hash = Sha256::hex(msg->text);

  MessageAnnotationListOptions all;
  all.limit = std::nullopt;
  all.include_retracted = true;
  LOOM_TRY_ASSIGN(auto have, db.list_message_annotations(c.message_id, all));

  Written out;
  std::vector<MessageAnnotation> rivals;  // in force, same category, overlapping
  for (const auto& a : have) {
    if (category_of(db, pol, a) != c.category) continue;
    const bool same_range = a.start_char == start && a.end_char == end;
    const bool same_producer = json::get_string(a.metadata, "producer") == c.producer;
    const std::string st = state_of(a);
    if (same_producer && same_range && json::get_string(a.metadata, "method_version") == c.method_version) {
      if (st == "active") {
        out.id = a.id;
        out.outcome = Outcome::Duplicate;
        return out;
      }
      if (st == "retracted") {  // what the user took back stays back
        out.id = a.id;
        out.outcome = Outcome::Suppressed;
        return out;
      }
    }
    if (st == "active" && overlaps(start, end, a.start_char, a.end_char)) rivals.push_back(a);
  }

  const Json& cost = c.cost;
  Json meta{{"schema", kSchema}, {"category", c.category}, {"producer", c.producer},
            {"method", Json{{"name", c.method}}}, {"method_version", c.method_version},
            {"confidence", std::round(std::clamp(c.confidence, 0.0, 1.0) * 1000) / 1000},
            {"cost", cost.is_object() ? cost : Json::object()}, {"lifecycle", "active"}};
  if (!c.qualifier.empty()) meta["qualifier"] = c.qualifier;
  if (c.extra.is_object() && !c.extra.empty()) {
    Json data = Json::object();
    for (auto it = c.extra.begin(); it != c.extra.end(); ++it) data[it.key()] = it.value();
    meta["data"] = data;
  }

  // Does an in-force rival stand above the new claim?
  const MessageAnnotation* winner = nullptr;
  for (const auto& r : rivals) {
    const int ro = rank_origin(r.origin), no = rank_origin(c.origin);
    const double rc = json::get_number(r.metadata, "confidence", 1.0);
    bool rival_wins = ro > no || (ro == no && rc > c.confidence + 1e-9);
    if (r.origin == "user" && c.origin != "user") rival_wins = true;
    if (rival_wins && (!winner || rc > json::get_number(winner->metadata, "confidence", 1.0))) winner = &r;
  }

  LOOM_TRY_ASSIGN(auto node, db.get_or_create_node(pol.label_prefix() + c.category, pol.node_kind(),
                                                   NodeOptions{{}, Json::array({"annotation_category"}), Json{{"schema", "loom.annotation_category/1"}, {"category", c.category}}, std::nullopt}));
  NewMessageAnnotation n;
  n.message_id = c.message_id;
  n.node_id = node;
  n.start_char = start;
  n.end_char = end;
  n.origin = c.origin;
  n.metadata = meta;
  n.expected_text_hash = hash;
  LOOM_TRY_ASSIGN(auto made, db.create_message_annotation(n));
  out.id = made.id;
  if (winner) {
    out.outcome = Outcome::Lost;
    out.superseded_by = winner->id;
    LOOM_TRY(patch_lifecycle(db, made, "superseded", winner->id, "retracted"));
    return out;
  }
  out.outcome = rivals.empty() ? Outcome::Created : Outcome::Replaced;
  for (const auto& r : rivals) {
    LOOM_TRY(patch_lifecycle(db, r, "superseded", made.id, "retracted"));
    out.superseded.push_back(r.id);
  }
  return out;
}

Result<Json> list(Database& db, const Policy& pol, const std::string& message_id, const ListOptions& opts) {
  MessageAnnotationListOptions all;
  all.limit = std::nullopt;
  all.include_retracted = opts.include_inactive;
  LOOM_TRY_ASSIGN(auto rows, db.list_message_annotations(message_id, all));
  LOOM_TRY_ASSIGN(auto msg, db.get_msg(message_id));
  const auto length = msg ? static_cast<std::int64_t>(utf8::length(msg->text)) : std::int64_t(0);
  const Json shield = pol.p.value("conflicts", Json::object()).value("shield", Json::object());
  const double shield_min = json::get_number(shield, "min_confidence", 0.5);
  const bool whole_shields = json::get_bool(shield, "whole_message_shields", false);
  const std::string over = json::get_string(shield, "signal_over", "droppable");

  struct Row { const MessageAnnotation* a; std::string category, cls; double confidence; };
  std::vector<Row> info;
  for (const auto& a : rows) info.push_back({&a, category_of(db, pol, a), "", json::get_number(a.metadata, "confidence", 1.0)});
  for (auto& r : info) r.cls = pol.class_of(r.category);

  Json out = Json::array();
  for (const auto& r : info) {
    const MessageAnnotation& a = *r.a;
    if (r.category.empty()) continue;
    const std::string st = state_of(a);
    if (st != "active" && !opts.include_inactive) continue;
    if (r.confidence < opts.min_confidence) continue;
    Json row{{"id", a.id}, {"category", r.category}, {"class", r.cls}, {"polarity", pol.polarity_of(r.category)},
             {"producer", json::get_string(a.metadata, "producer", a.origin)}, {"method", a.metadata.value("method", Json::object()).value("name", "")},
             {"method_version", json::get_string(a.metadata, "method_version")}, {"origin", a.origin}, {"confidence", r.confidence},
             {"qualifier", json::get_string(a.metadata, "qualifier")}, {"cost", a.metadata.value("cost", Json::object())},
             {"start_char", a.start_char}, {"end_char", a.end_char}, {"excerpt", a.excerpt}, {"state", st}, {"revision", a.revision},
             {"data", a.metadata.value("data", Json::object())}};
    if (!json::get_string(a.metadata, "superseded_by").empty() && st == "superseded") row["superseded_by"] = a.metadata["superseded_by"];
    if (opts.shield && st == "active" && r.cls == over) {
      for (const auto& s : info) {
        if (s.cls != "signal" || state_of(*s.a) != "active" || s.confidence < shield_min) continue;
        if (!whole_shields && s.a->start_char <= 0 && s.a->end_char >= length) continue;
        if (!overlaps(a.start_char, a.end_char, s.a->start_char, s.a->end_char)) continue;
        row["shielded_by"] = s.a->id;
        break;
      }
      if (row.contains("shielded_by") && !opts.include_inactive) continue;
    }
    out.push_back(std::move(row));
  }
  return out;
}

Status record_run(Database& db, const Policy& pol, const std::string& message_id, const std::string& producer, const std::string& version,
                  const std::vector<std::string>& categories, const Json& cost) {
  (void)pol;
  LOOM_TRY_ASSIGN(auto msg, db.get_msg(message_id));
  if (!msg) return Error(Errc::NotFound, "no message '" + message_id + "'");
  Json cats = Json::array();
  for (const auto& c : categories) cats.push_back(c);
  Json meta{{"schema", "loom.annotation_run/1"}, {"message_id", message_id}, {"producer", producer}, {"method_version", version}, {"categories", cats},
            {"cost", cost.is_object() ? cost : Json::object()}, {"text_sha256", Sha256::hex(msg->text)}, {"at", timeutil::utc_now_iso()}};
  const std::string label = "run:" + message_id + ":" + producer;
  LOOM_TRY_ASSIGN(auto have, db.find_node(label, "annotation_run"));
  if (have) {
    NodePatch p;
    p.metadata = meta;
    return db.update_node(have->id, p);
  }
  NodeOptions o;
  o.metadata = meta;
  o.tags = Json::array({"annotation_run"});
  LOOM_TRY_ASSIGN(auto id, db.create_node(label, "annotation_run", o));
  LOOM_TRY_ASSIGN(auto l, db.create_link(id, message_id, "derived_from", 1.0, Json{{"role", "run_of"}}));
  (void)l;
  return ok_status();
}

Result<Json> runs_of(Database& db, const Policy& pol, const std::string& message_id) {
  LOOM_TRY_ASSIGN(auto msg, db.get_msg(message_id));
  Json out = Json::array();
  if (!msg) return out;
  const std::string hash = Sha256::hex(msg->text);
  const Json* producers = json::find(pol.p, "producers");
  if (!producers || !producers->is_object()) return out;
  for (auto it = producers->begin(); it != producers->end(); ++it) {
    LOOM_TRY_ASSIGN(auto n, db.find_node("run:" + message_id + ":" + it.key(), "annotation_run"));
    if (!n || json::get_string(n->metadata, "text_sha256") != hash) continue;
    out.push_back(Json{{"producer", n->metadata.value("producer", "")}, {"method_version", n->metadata.value("method_version", "")}, {"categories", n->metadata.value("categories", Json::array())},
                       {"cost", n->metadata.value("cost", Json::object())}, {"at", n->metadata.value("at", "")}});
  }
  return out;
}

Result<Json> supersede(Database& db, const std::string& id, const std::string& by) {
  LOOM_TRY_ASSIGN(auto cur, db.get_message_annotation(id));
  if (!cur) return Error(Errc::NotFound, "no annotation '" + id + "'");
  LOOM_TRY(patch_lifecycle(db, *cur, "superseded", by, "retracted"));
  return Json{{"id", id}, {"state", "superseded"}, {"superseded_by", by}};
}

Result<Json> restore(Database& db, const std::string& id) {
  LOOM_TRY_ASSIGN(auto cur, db.get_message_annotation(id));
  if (!cur) return Error(Errc::NotFound, "no annotation '" + id + "'");
  LOOM_TRY(patch_lifecycle(db, *cur, "active", "", "active"));
  return Json{{"id", id}, {"state", "active"}};
}

}  // namespace loom::annotations
