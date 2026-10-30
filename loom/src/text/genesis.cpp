// R102 G2: text genesis in the graph — chains of versions, event records, lazy idempotent materialisation, read-only
// queries. See include/loom/text_genesis.h for what the code does today.
#include "loom/text_genesis.h"

#include <algorithm>
#include <map>
#include <set>

namespace loom::text::genesis {
namespace {

std::string idx(std::size_t i) { return i == kNone ? std::string("-") : std::to_string(i); }
Json opt_idx(std::size_t i) { return i == kNone ? Json(nullptr) : Json(i); }

Json advisory(const char* code, std::string pl, std::string en) {
  return Json{{"code", code}, {"pl", std::move(pl)}, {"en", std::move(en)}};
}

std::string event_key(std::string_view kind, std::string_view from, std::string_view to, std::size_t a, std::size_t b) {
  std::string k(kind);
  k += '|';
  k += from;
  k += '|';
  k += to;
  k += '|';
  k += idx(a);
  k += '|';
  k += idx(b);
  return k;
}

// What defines the identity of a node besides its own content: the level and the language of the cut it was made at. A graph
// holding two cuts of one chain (sentence and paragraph, pl and en) keeps their events and versions apart.
std::string cut_tag(const std::string& level, const std::string& language) { return "@" + level + "/" + language; }

Json strings_json(const std::vector<std::string>& v) {
  Json a = Json::array();
  for (const auto& s : v) a.push_back(s);
  return a;
}

}  // namespace

// ── JSON shapes ──────────────────────────────────────────────────────────────────────────────────────────────────────

Json SourceRef::to_json() const {
  return Json{{"id", id},         {"kind", kind}, {"conv_id", conv_id}, {"group", group}, {"ordinal", ordinal},
              {"when", when},     {"role", role}, {"actor", actor}};
}

SourceRef SourceRef::from_message(const Message& m) {
  SourceRef s;
  s.id = m.id;
  s.kind = "message";
  s.conv_id = m.conv_id;
  s.group = m.version_group_id ? *m.version_group_id : m.id;
  s.ordinal = m.version_num;
  s.when = m.created;
  s.role = m.role;
  // Preset: the actor of a message is its model when it names one, else its role ("user"). A track pseudonym (W1)
  // will be another value of the same field.
  s.actor = (m.model && !m.model->empty()) ? *m.model : m.role;
  return s;
}

Json Occurrence::to_json() const {
  return Json{{"fingerprint", fingerprint}, {"index", index}, {"begin", begin}, {"end", end}, {"text", text}};
}

Json VersionFragments::to_json() const {
  Json f = Json::array();
  for (const auto& o : fragments) f.push_back(o.to_json());
  return Json{{"source", source.to_json()}, {"fragments", f}};
}

Json Event::to_json() const {
  Json j{{"kind", kind},
         {"op_kind", op_kind},
         {"key", key},
         {"fragment", fragment},
         {"successor", successor},
         {"text_before", text_before},
         {"text_after", text_after},
         {"from", from.to_json()},
         {"to", to.to_json()},
         {"a_index", opt_idx(a_index)},
         {"b_index", opt_idx(b_index)},
         {"similarity", similarity},
         {"where", Json{{"conv_id", to.conv_id}, {"id", to.id}, {"kind", to.kind}}},
         {"when", to.when},
         {"actor", to.actor}};
  if (!inner.empty()) j["inner"] = inner;
  return j;
}

Json Chain::to_json() const {
  Json v = Json::array(), e = Json::array(), s = Json::array();
  for (const auto& x : versions) v.push_back(x.to_json());
  for (const auto& x : events) e.push_back(x.to_json());
  for (const auto& x : steps) s.push_back(x.to_json());
  return Json{{"level", level}, {"language", language}, {"versions", v}, {"steps", s}, {"events", e}, {"advisories", strings_json(advisories)}};
}

Json LineageStep::to_json() const {
  return Json{{"version", version}, {"index", index}, {"op_kind", op_kind}, {"event", event_key}};
}

Json Lineage::to_json() const {
  Json s = Json::array();
  for (const auto& x : steps) s.push_back(x.to_json());
  return Json{{"steps", s}, {"origin", origin}, {"origin_version", origin_version}};
}

Json MaterializeSummary::to_json() const {
  return Json{{"versions", versions},         {"fragments", fragments},         {"events", events},
              {"nodes_created", nodes_created}, {"nodes_reused", nodes_reused}, {"links_written", links_written},
              {"advisories", strings_json(advisories)}};
}

// ── Options ──────────────────────────────────────────────────────────────────────────────────────────────────────────

Result<Options> Options::from_pack(const Json& pack) {
  Options o;
  LOOM_TRY_ASSIGN(o.align, AlignOptions::from_pack(pack));
  const Json g = pack.is_object() ? pack.value("genesis", Json::object()) : Json::object();
  if (!g.is_object()) return Error(Errc::InvalidArgument, "text pack: genesis must be an object");
  try {
    o.level = g.value("level", o.level);
    o.language = g.value("language", std::string());
    o.record_initial_additions = g.value("record_initial_additions", o.record_initial_additions);
  } catch (const std::exception& e) {
    return Error(Errc::InvalidArgument, std::string("text pack: genesis: ") + e.what());
  }
  o.names = g;
  return o;
}

std::string Options::node_kind(std::string_view which) const {
  if (const Json* n = json::find(names, "nodes"); n && n->is_object())
    if (const Json* v = json::find(*n, which); v && v->is_string()) return v->get<std::string>();
  return "text_" + std::string(which);
}

std::string Options::edge_kind(std::string_view which) const {
  if (const Json* n = json::find(names, "edges"); n && n->is_object())
    if (const Json* v = json::find(*n, which); v && v->is_string()) return v->get<std::string>();
  return std::string(which);  // an edge kind nobody mapped is used under its own name
}

std::string Options::event_kind(std::string_view op_kind) const {
  if (const Json* s = json::find(names, "silent_ops"); s && s->is_array())
    for (const auto& x : *s)
      if (x.is_string() && x.get<std::string>() == op_kind) return std::string();
  if (op_kind == "equal" && !json::find(names, "silent_ops")) return std::string();  // the preset when the pack says nothing
  if (const Json* n = json::find(names, "events"); n && n->is_object())
    if (const Json* v = json::find(*n, op_kind); v && v->is_string()) return v->get<std::string>();
  return std::string(op_kind);  // an op kind nobody mapped is an event of its own name: "something changed"
}

Json known_questions(const Options& opt) {
  if (const Json* q = json::find(opt.names, "questions"); q && q->is_object()) return *q;
  return Json::object();
}

// ── The chain ────────────────────────────────────────────────────────────────────────────────────────────────────────

Result<Chain> compute_chain(const Segmenter& seg, const std::vector<VersionText>& versions, const Options& opt) {
  Chain c;
  c.level = opt.level;
  c.language = seg.language();
  c.advisories = seg.advisories();
  std::vector<std::vector<std::string>> units;  // full fragment texts per version (keys = fingerprints of the cores)
  for (const auto& v : versions) {
    LOOM_TRY_ASSIGN(const Segmentation s, seg.segment(v.text, opt.level));
    VersionFragments vf;
    vf.source = v.source;
    for (std::size_t i = 0; i < s.fragments.size(); ++i) {
      const Fragment& f = s.fragments[i];
      Occurrence o;
      o.fingerprint = f.fingerprint;
      o.index = i;
      o.begin = f.begin;
      o.end = f.end;
      o.text = v.text.substr(f.core_begin, f.core_end - f.core_begin);
      vf.fragments.push_back(std::move(o));
    }
    units.push_back(fragment_texts(v.text, s));
    c.versions.push_back(std::move(vf));
  }
  if (opt.record_initial_additions && !c.versions.empty()) {
    const std::string kind = opt.event_kind("insert");
    if (!kind.empty())
      for (const auto& o : c.versions[0].fragments) {
        Event e;
        e.kind = kind;
        e.op_kind = "insert";
        e.to = c.versions[0].source;
        e.b_index = o.index;
        e.fragment = o.fingerprint;
        e.text_after = o.text;
        e.key = event_key(kind, "", e.to.id, kNone, o.index) + cut_tag(c.level, c.language);
        c.events.push_back(std::move(e));
      }
  }
  for (std::size_t i = 1; i < c.versions.size(); ++i) {
    LOOM_TRY_ASSIGN(Alignment al, align_units(seg, units[i - 1], units[i], opt.align));
    const VersionFragments& A = c.versions[i - 1];
    const VersionFragments& B = c.versions[i];
    for (const EditOp& op : al.ops) {
      const std::string kind = opt.event_kind(op.kind);
      if (kind.empty()) continue;
      Event e;
      e.kind = kind;
      e.op_kind = op.kind;
      e.from = A.source;
      e.to = B.source;
      e.a_index = op.a;
      e.b_index = op.b;
      e.similarity = op.similarity;
      if (op.a != kNone && op.a < A.fragments.size()) {
        e.fragment = A.fragments[op.a].fingerprint;
        e.text_before = A.fragments[op.a].text;
      }
      if (op.b != kNone && op.b < B.fragments.size()) {
        if (e.fragment.empty()) e.fragment = B.fragments[op.b].fingerprint;
        else e.successor = B.fragments[op.b].fingerprint;
        e.text_after = B.fragments[op.b].text;
      }
      if (!op.inner.empty()) {
        Json in = Json::array();
        for (const auto& x : op.inner) in.push_back(x.to_json());
        e.inner = in;
      }
      e.key = event_key(kind, e.from.id, e.to.id, op.a, op.b) + cut_tag(c.level, c.language);
      c.events.push_back(std::move(e));
    }
    c.steps.push_back(std::move(al));
  }
  return c;
}

Result<Lineage> lineage_of(const Chain& chain, std::size_t version, std::size_t index) {
  if (version >= chain.versions.size()) return Error(Errc::InvalidArgument, "lineage: no such version in the chain");
  if (index >= chain.versions[version].fragments.size()) return Error(Errc::InvalidArgument, "lineage: no such fragment in the version");
  std::map<std::string, std::string> keys;  // from|to|a|b -> event key
  for (const auto& e : chain.events) keys[event_key("", e.from.id, e.to.id, e.a_index, e.b_index)] = e.key;
  Lineage L;
  std::size_t v = version, j = index;
  for (;;) {
    LineageStep step;
    step.version = v;
    step.index = j;
    if (v == 0) {
      step.op_kind = "origin";
      L.steps.push_back(step);
      L.origin = "first_version";
      L.origin_version = 0;
      return L;
    }
    const Alignment& al = chain.steps[v - 1];
    const EditOp* hit = nullptr;
    for (const auto& op : al.ops)
      if (op.b == j) {
        if (hit) return Error(Errc::Internal, "lineage: a b unit is covered twice by the alignment");
        hit = &op;
      }
    if (!hit) return Error(Errc::Internal, "lineage: a b unit is not covered by the alignment");
    step.op_kind = hit->kind;
    const std::string from = chain.versions[v - 1].source.id, to = chain.versions[v].source.id;
    if (auto k = keys.find(event_key("", from, to, hit->a, hit->b)); k != keys.end()) step.event_key = k->second;
    L.steps.push_back(step);
    if (hit->a == kNone) {  // appeared here
      L.origin = "new_in_version";
      L.origin_version = v;
      return L;
    }
    j = hit->a;
    --v;
  }
}

// ── Sources from the database ────────────────────────────────────────────────────────────────────────────────────────

namespace {
std::vector<VersionText> to_versions(const std::vector<Message>& ms) {
  std::vector<VersionText> out;
  for (const auto& m : ms) out.push_back(VersionText{SourceRef::from_message(m), m.text});
  std::stable_sort(out.begin(), out.end(), [](const VersionText& a, const VersionText& b) { return a.source.ordinal < b.source.ordinal; });
  return out;
}
}  // namespace

Result<std::vector<VersionText>> versions_of_message(Database& db, std::string_view message_id) {
  LOOM_TRY_ASSIGN(auto m, db.get_msg(message_id));
  if (!m) return Error(Errc::NotFound, "message not found: " + std::string(message_id));
  if (m->version_group_id && !m->version_group_id->empty()) return versions_of_group(db, *m->version_group_id);
  return to_versions({*m});
}

Result<std::vector<VersionText>> versions_of_group(Database& db, std::string_view version_group_id) {
  LOOM_TRY_ASSIGN(auto vs, db.get_versions(version_group_id));
  if (vs.empty()) return Error(Errc::NotFound, "version group not found: " + std::string(version_group_id));
  return to_versions(vs);
}

Result<std::vector<std::vector<VersionText>>> chains_of_conversation(Database& db, std::string_view conv_id) {
  LOOM_TRY_ASSIGN(auto ms, db.get_msgs(conv_id, true));
  std::vector<std::string> order;
  std::map<std::string, std::vector<Message>> groups;
  for (const auto& m : ms) {
    const std::string g = (m.version_group_id && !m.version_group_id->empty()) ? *m.version_group_id : m.id;
    if (!groups.count(g)) order.push_back(g);
    groups[g].push_back(m);
  }
  std::vector<std::vector<VersionText>> out;
  for (const auto& g : order) out.push_back(to_versions(groups[g]));
  return out;
}

// ── Materialisation ──────────────────────────────────────────────────────────────────────────────────────────────────

Result<MaterializeSummary> materialize(Database& db, const Chain& chain, const Options& opt) {
  MaterializeSummary s;
  s.versions = chain.versions.size();
  s.advisories = chain.advisories;
  auto node = [&](const std::string& label, const std::string& kind, const std::string& content, Json metadata) -> Result<std::string> {
    LOOM_TRY_ASSIGN(auto existing, db.find_node(label, kind));
    if (existing) {
      ++s.nodes_reused;
      return existing->id;
    }
    NodeOptions o;
    o.content = content;
    o.metadata = std::move(metadata);
    LOOM_TRY_ASSIGN(auto id, db.create_node(label, kind, o));
    ++s.nodes_created;
    return id;
  };
  auto link = [&](const std::string& src, const std::string& dst, const std::string& type, Json metadata) -> Status {
    LOOM_TRY(db.create_link(src, dst, type, 1.0, std::move(metadata)));
    ++s.links_written;
    return ok_status();
  };
  const std::string frag_kind = opt.node_kind("fragment"), ver_kind = opt.node_kind("version"), ev_kind = opt.node_kind("event");
  std::map<std::string, std::string> frag_ids, ver_ids;  // fingerprint / source id -> node id
  for (std::size_t i = 0; i < chain.versions.size(); ++i) {
    const VersionFragments& v = chain.versions[i];
    Json meta = v.source.to_json();
    meta["level"] = chain.level;
    meta["language"] = chain.language;
    meta["fragment_count"] = v.fragments.size();
    LOOM_TRY_ASSIGN(const std::string vid, node(v.source.id + cut_tag(chain.level, chain.language), ver_kind, "", meta));
    ver_ids[v.source.id] = vid;
    if (i > 0) {
      const SourceRef& prev = chain.versions[i - 1].source;
      LOOM_TRY(link(vid, ver_ids[prev.id], opt.edge_kind("version_chain"),
                    Json{{"ordinal_from", prev.ordinal}, {"ordinal_to", v.source.ordinal}, {"level", chain.level}}));
    }
    std::map<std::string, Json> per_fp;  // fingerprint -> occurrence metadata (a fragment may repeat in one version)
    for (const auto& o : v.fragments) {
      ++s.fragments;
      if (!frag_ids.count(o.fingerprint)) {
        Json fm{{"fingerprint", o.fingerprint}, {"level", chain.level}, {"language", chain.language},
                {"first_seen", Json{{"id", v.source.id}, {"conv_id", v.source.conv_id}, {"when", v.source.when}, {"index", o.index}}}};
        LOOM_TRY_ASSIGN(const std::string fid, node(o.fingerprint, frag_kind, o.text, fm));
        frag_ids[o.fingerprint] = fid;
      }
      Json& m = per_fp[o.fingerprint];
      if (!m.is_object()) m = Json{{"indexes", Json::array()}, {"ranges", Json::array()}, {"level", chain.level}};
      m["indexes"].push_back(o.index);
      m["ranges"].push_back(Json::array({o.begin, o.end}));
    }
    for (const auto& [fp, m] : per_fp) LOOM_TRY(link(frag_ids[fp], vid, opt.edge_kind("occurrence"), m));
  }
  for (const auto& e : chain.events) {
    ++s.events;
    const std::string content = e.text_before.empty() ? e.text_after : e.text_before;
    LOOM_TRY_ASSIGN(const std::string eid, node(e.key, ev_kind, content, e.to_json()));
    auto frag = frag_ids.find(e.fragment);
    if (frag == frag_ids.end()) return Error(Errc::Internal, "materialize: an event names a fragment the chain does not hold");
    LOOM_TRY(link(eid, frag->second, opt.edge_kind("about"), Json{{"event", e.kind}}));
    auto to = ver_ids.find(e.to.id);
    if (to == ver_ids.end()) return Error(Errc::Internal, "materialize: an event names a version the chain does not hold");
    LOOM_TRY(link(eid, to->second, opt.edge_kind("in"), Json{{"when", e.to.when}, {"actor", e.to.actor}, {"conv_id", e.to.conv_id}}));
    if (auto from = ver_ids.find(e.from.id); from != ver_ids.end())
      LOOM_TRY(link(eid, from->second, opt.edge_kind("from"), Json{{"ordinal", e.from.ordinal}}));
    const bool has_successor = !e.successor.empty() && e.successor != e.fragment;
    if (has_successor) {
      auto succ = frag_ids.find(e.successor);
      if (succ == frag_ids.end()) return Error(Errc::Internal, "materialize: an event names a successor the chain does not hold");
      LOOM_TRY(link(eid, succ->second, opt.edge_kind("successor"), Json{{"similarity", e.similarity}}));
      // Preset: the edge named after the event kind goes to the successor fragment when there is one (replaced_by),
      // and back (derived_from); otherwise it goes to the version the event happened in (deleted_in, added_in, moved_to).
      LOOM_TRY(link(frag->second, succ->second, opt.edge_kind(e.kind), Json{{"event", e.key}, {"similarity", e.similarity}, {"in", e.to.id}}));
      LOOM_TRY(link(succ->second, frag->second, opt.edge_kind("replacement_back"), Json{{"event", e.key}, {"in", e.to.id}}));
    } else {
      LOOM_TRY(link(frag->second, to->second, opt.edge_kind(e.kind),
                    Json{{"event", e.key}, {"a_index", opt_idx(e.a_index)}, {"b_index", opt_idx(e.b_index)}}));
    }
  }
  return s;
}

// ── Queries ──────────────────────────────────────────────────────────────────────────────────────────────────────────

namespace {

struct Match {
  std::string fingerprint, text;
  std::string how;  // "fingerprint" | "contains"
};

// The fragments of a chain the question is about: by fingerprint, else (preset match.contains) every distinct fragment
// whose normalized text contains the normalized asked text, up to match.max_candidates.
std::vector<Match> matches_in(const Segmenter& seg, const Chain& chain, const std::string& fp, const std::string& text, const Options& opt) {
  std::vector<Match> out;
  std::set<std::string> seen;
  for (const auto& v : chain.versions)
    for (const auto& o : v.fragments)
      if (!fp.empty() && o.fingerprint == fp && seen.insert(o.fingerprint).second) out.push_back(Match{o.fingerprint, o.text, "fingerprint"});
  const Json m = opt.names.value("match", Json::object());
  const bool contains = m.is_object() ? m.value("contains", true) : true;
  const std::size_t max_c = static_cast<std::size_t>(m.is_object() ? m.value("max_candidates", 5) : 5);
  if (out.empty() && contains && !text.empty()) {
    const std::string needle = seg.normalize(text);
    if (!needle.empty())
      for (const auto& v : chain.versions)
        for (const auto& o : v.fragments) {
          if (out.size() >= max_c) break;
          if (seen.count(o.fingerprint)) continue;
          if (seg.normalize(o.text).find(needle) != std::string::npos) {
            seen.insert(o.fingerprint);
            out.push_back(Match{o.fingerprint, o.text, "contains"});
          }
        }
  }
  return out;
}

Json chain_ref(const Chain& chain) {
  if (chain.versions.empty()) return Json{{"group", ""}, {"conv_id", ""}, {"versions", 0}};
  const SourceRef& s = chain.versions.front().source;
  return Json{{"group", s.group}, {"conv_id", s.conv_id}, {"versions", chain.versions.size()}};
}

Json version_brief(const SourceRef& s) {
  return Json{{"id", s.id}, {"ordinal", s.ordinal}, {"when", s.when}, {"actor", s.actor}, {"conv_id", s.conv_id}};
}

// Occurrences already in the graph (other chains, other conversations), newest last.
Result<Json> graph_occurrences(Database& db, const std::string& fp, const Options& opt) {
  Json out = Json::array();
  LOOM_TRY_ASSIGN(auto node, db.find_node(fp, opt.node_kind("fragment")));
  if (!node) return out;
  LOOM_TRY_ASSIGN(auto links, db.get_links(node->id, opt.edge_kind("occurrence")));
  std::vector<Json> rows;
  for (const auto& l : links) {
    if (l.src != node->id) continue;
    LOOM_TRY_ASSIGN(auto v, db.get_node(l.dst));
    if (!v) continue;
    Json row = v->metadata;
    row["indexes"] = l.metadata.value("indexes", Json::array());
    rows.push_back(row);
  }
  std::sort(rows.begin(), rows.end(), [](const Json& a, const Json& b) { return a.value("when", "") < b.value("when", ""); });
  for (auto& r : rows) out.push_back(std::move(r));
  return out;
}

Result<std::vector<Chain>> chains_for(Database& db, const Segmenter& seg, const Query& q, const Options& opt) {
  std::vector<std::vector<VersionText>> sources;
  if (!q.message_id.empty()) {
    LOOM_TRY_ASSIGN(auto v, versions_of_message(db, q.message_id));
    sources.push_back(std::move(v));
  } else if (!q.version_group_id.empty()) {
    LOOM_TRY_ASSIGN(auto v, versions_of_group(db, q.version_group_id));
    sources.push_back(std::move(v));
  } else if (!q.conv_id.empty()) {
    LOOM_TRY_ASSIGN(sources, chains_of_conversation(db, q.conv_id));
  }
  std::vector<Chain> out;
  for (const auto& s : sources) {
    LOOM_TRY_ASSIGN(Chain c, compute_chain(seg, s, opt));
    out.push_back(std::move(c));
  }
  return out;
}

Json events_about(const Chain& chain, const std::string& fp) {
  Json out = Json::array();
  for (const auto& e : chain.events)
    if (e.fragment == fp || e.successor == fp) out.push_back(e.to_json());
  return out;
}

// Where the fragment stands in the last version of the chain: present, or gone since version k (and how).
Json final_state(const Chain& chain, const std::string& fp) {
  if (chain.versions.empty()) return Json{{"state", "unknown"}};
  const VersionFragments& last = chain.versions.back();
  for (const auto& o : last.fragments)
    if (o.fingerprint == fp) return Json{{"state", "present"}, {"version", version_brief(last.source)}, {"index", o.index}};
  // the last event that took it away (newest version wins)
  const Event* gone = nullptr;
  for (const auto& e : chain.events)
    if (e.fragment == fp && e.successor != fp && (!gone || e.to.ordinal > gone->to.ordinal)) gone = &e;
  if (gone) return Json{{"state", "gone"}, {"event", gone->kind}, {"version", version_brief(gone->to)}, {"successor", gone->successor}, {"text_after", gone->text_after}};
  return Json{{"state", "absent"}};
}

}  // namespace

Result<Json> query(Database& db, const Segmenter& seg, const Query& q, const Options& opt) {
  const Json questions = known_questions(opt);
  if (!questions.contains(q.question)) {
    std::string names;
    for (auto it = questions.begin(); it != questions.end(); ++it) names += (names.empty() ? "" : ", ") + it.key();
    return Error(Errc::InvalidArgument, "unknown question \"" + q.question + "\"; this build answers: " + names);
  }
  Json out{{"question", q.question}, {"label", questions.at(q.question)}, {"computed", Json::array()}, {"advisories", Json::array()}, {"matches", Json::array()}};
  const std::string fp = !q.fingerprint.empty() ? q.fingerprint : (q.text.empty() ? std::string() : seg.fingerprint(q.text));
  out["fingerprint"] = fp;

  if (q.question == "missing") {
    std::vector<VersionText> ab;
    if (!q.from_id.empty() || !q.to_id.empty()) {
      if (q.from_id.empty() || q.to_id.empty()) return Error(Errc::InvalidArgument, "missing: name both versions (from_id and to_id) or neither");
      for (const std::string* id : {&q.from_id, &q.to_id}) {
        LOOM_TRY_ASSIGN(auto m, db.get_msg(*id));
        if (!m) return Error(Errc::NotFound, "message not found: " + *id);
        ab.push_back(VersionText{SourceRef::from_message(*m), m->text});
      }
    } else {
      LOOM_TRY_ASSIGN(auto chains, chains_for(db, seg, q, opt));
      if (chains.empty()) return Error(Errc::InvalidArgument, "missing: name a message (message_id), a version group or two versions");
      // the version asked about (the named message, else the last) and the one before it
      const std::vector<VersionText>* src = nullptr;
      LOOM_TRY_ASSIGN(auto versions, !q.message_id.empty() ? versions_of_message(db, q.message_id) : versions_of_group(db, chains[0].versions.front().source.group));
      src = &versions;
      std::size_t to = src->size() - 1;
      if (!q.message_id.empty())
        for (std::size_t i = 0; i < src->size(); ++i)
          if ((*src)[i].source.id == q.message_id) to = i;
      if (to == 0) {
        out["advisories"].push_back(advisory("first_version", "To jest pierwsza wersja — nie ma poprzedniej do porównania.", "This is the first version — there is no previous one to compare with."));
        out["from"] = nullptr;
        out["to"] = version_brief((*src)[to].source);
        out["removed"] = Json::array();
        out["replaced"] = Json::array();
        out["moved"] = Json::array();
        out["added"] = Json::array();
        out["computed"].push_back("live");
        return out;
      }
      ab = {(*src)[to - 1], (*src)[to]};
    }
    LOOM_TRY_ASSIGN(Chain c, compute_chain(seg, ab, opt));
    out["from"] = version_brief(ab[0].source);
    out["to"] = version_brief(ab[1].source);
    Json removed = Json::array(), replaced = Json::array(), moved = Json::array(), added = Json::array(), other = Json::array();
    for (const auto& e : c.events) {
      Json ej = e.to_json();
      const std::string del = opt.event_kind("delete"), ins = opt.event_kind("insert"), rep = opt.event_kind("replace"), mov = opt.event_kind("move");
      if (e.kind == del) removed.push_back(ej);
      else if (e.kind == ins) added.push_back(ej);
      else if (e.kind == rep) replaced.push_back(ej);
      else if (e.kind == mov) moved.push_back(ej);
      else other.push_back(ej);
    }
    out["removed"] = removed;
    out["replaced"] = replaced;
    out["moved"] = moved;
    out["added"] = added;
    if (!other.empty()) out["other"] = other;
    out["kept"] = c.steps.empty() ? 0 : c.steps[0].count("equal");
    out["computed"].push_back("live");
    for (const auto& a : c.advisories) out["advisories"].push_back(advisory("segmentation", a, a));
    return out;
  }

  // origin / replaced / events: over the chains named (live) plus what the graph holds for the fingerprint
  LOOM_TRY_ASSIGN(auto chains, chains_for(db, seg, q, opt));
  if (fp.empty() && q.text.empty()) return Error(Errc::InvalidArgument, q.question + ": name the sentence (text) or its fingerprint");
  std::set<std::string> fps_seen;
  for (const Chain& c : chains) {
    for (const Match& m : matches_in(seg, c, fp, q.text, opt)) {
      fps_seen.insert(m.fingerprint);
      Json row{{"fingerprint", m.fingerprint}, {"text", m.text}, {"matched_by", m.how}, {"chain", chain_ref(c)}};
      if (q.question == "origin") {
        // the latest version holding it; its lineage leads to the origin
        for (std::size_t v = c.versions.size(); v-- > 0;) {
          const auto& frs = c.versions[v].fragments;
          auto it = std::find_if(frs.begin(), frs.end(), [&](const Occurrence& o) { return o.fingerprint == m.fingerprint; });
          if (it == frs.end()) continue;
          LOOM_TRY_ASSIGN(Lineage L, lineage_of(c, v, it->index));
          Json lj = L.to_json();
          Json steps = Json::array();
          for (const auto& st : L.steps) {
            Json sj = st.to_json();
            sj["version_ref"] = version_brief(c.versions[st.version].source);
            sj["text"] = c.versions[st.version].fragments[st.index].text;
            steps.push_back(sj);
          }
          lj["steps"] = steps;
          row["lineage"] = lj;
          row["origin"] = Json{{"how", L.origin}, {"version", version_brief(c.versions[L.origin_version].source)},
                               {"text", c.versions[L.origin_version].fragments[L.steps.back().index].text}};
          break;
        }
        // first occurrence in the chain by time
        for (const auto& v : c.versions) {
          bool has = false;
          for (const auto& o : v.fragments) has = has || o.fingerprint == m.fingerprint;
          if (has) {
            row["first_seen"] = version_brief(v.source);
            break;
          }
        }
      }
      row["events"] = events_about(c, m.fingerprint);
      row["final_state"] = final_state(c, m.fingerprint);
      if (q.question == "replaced") {
        // the forward chain of successors
        Json succ = Json::array();
        std::string cur = m.fingerprint;
        std::set<std::string> guard;
        while (guard.insert(cur).second) {
          const Event* next = nullptr;
          for (const auto& e : c.events)
            if (e.fragment == cur && !e.successor.empty() && e.successor != cur && (!next || e.to.ordinal < next->to.ordinal)) next = &e;
          if (!next) break;
          succ.push_back(Json{{"fingerprint", next->successor}, {"text", next->text_after}, {"event", next->kind}, {"similarity", next->similarity},
                              {"version", version_brief(next->to)}, {"inner", next->inner}});
          cur = next->successor;
        }
        row["successors"] = succ;
      }
      out["matches"].push_back(row);
    }
  }
  if (!chains.empty()) out["computed"].push_back("live");
  // the graph: every recorded occurrence of the fingerprint(s), also in other conversations
  std::set<std::string> ask = fps_seen;
  if (!fp.empty()) ask.insert(fp);
  Json graph = Json::array();
  bool any_graph = false;
  for (const auto& f : ask) {
    LOOM_TRY_ASSIGN(Json occ, graph_occurrences(db, f, opt));
    if (!occ.empty()) {
      any_graph = true;
      graph.push_back(Json{{"fingerprint", f}, {"occurrences", occ}});
    }
  }
  out["graph"] = graph;
  if (any_graph) out["computed"].push_back("graph");
  else out["advisories"].push_back(advisory("not_materialized", "Graf nie ma jeszcze węzła tego fragmentu — inne rozmowy nie są przeszukane; zapis genezy (materializacja) to doda.",
                                            "The graph holds no node of this fragment yet — other conversations are not searched; recording the genesis (materialize) adds it."));
  if (out["matches"].empty() && !any_graph)
    out["advisories"].push_back(advisory("no_match", "Nie znalazłem takiego zdania w nazwanych wersjach.", "No such sentence in the named versions."));
  return out;
}

}  // namespace loom::text::genesis
