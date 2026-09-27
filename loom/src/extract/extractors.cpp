// The extractors of the extract area (kb::kExtractors): observations ->
// typed entity mentions and OBSERVED claims with located support. Nothing
// here infers (§2.3): every claim points at the observations it was read
// from. All matching tables are pack data (lexicon.h); the code holds the
// algorithms. Deterministic.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

#include "archive/archive_internal.h"
#include "extract/extract_internal.h"
#include "loom/util/utf8.h"

namespace loom::extract::detail {

using model::Claim;
using model::Entity;
using model::Observation;
using model::ObservationKind;

std::string reliability_key(std::string_view ex) {
  static const std::map<std::string, std::string, std::less<>> kMap = {
      {"extract.alias", "extractor.alias"},         {"extract.gazetteer", "extractor.gazetteer"},
      {"extract.names", "extractor.enumeration"},   {"extract.relation_patterns", "extractor.pattern"},
      {"extract.versions", "extractor.version"},    {"extract.forks", "extractor.fork"},
      {"extract.code_symbols", "extractor.symbol"}, {"extract.citations", "extractor.pattern"},
      {"extract.dates", "extractor.pattern"},       {"extract.speakers", "extractor.section"},
      {"extract.headers", "extractor.section"},     {"extract.areas", "extractor.section"},
      {"extract.generalizations", "extractor.section"}, {"extract.options", "extractor.enumeration"},
      {"extract.items", "extractor.item"},          {"extract.decisions", "extractor.item"},
      {"extract.status_cues", "extractor.item"},    {"extract.normative", "extractor.item"},
      {"resolve.lineage", "extractor.codebase"},    {"resolve.same_as", "extractor.alias"},
  };
  std::string_view base = ex.substr(0, ex.find('@'));
  auto it = kMap.find(base);
  return it == kMap.end() ? "rule.default" : it->second;
}

namespace {

struct ObsInfo {
  const Observation* o = nullptr;
  std::string folded;
  std::vector<Token> toks;   // tokens of the folded text
  std::vector<Token> otoks;  // tokens of the original text (parallel when sizes match)
  bool text = false;         // a leaf text observation (sentence, list item, heading, lone utterance)
};

struct Mention {
  std::size_t obs = 0;
  std::string entity;
  std::string kind;
  std::string cls;
  std::string label;
  std::size_t fstart = 0, fend = 0;  // byte range in the folded text
  std::string source;                // extractor name
};

std::string date10(std::string_view d) { return std::string(d.substr(0, std::min<std::size_t>(10, d.size()))); }

bool has_alpha(std::string_view s) {
  return std::any_of(s.begin(), s.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || (c & 0x80); });
}

const std::vector<std::string> kStatusClasses = {"status.lost",     "status.restored", "status.superseded",
                                                 "status.abandoned", "status.partial",  "status.implemented",
                                                 "status.planned"};

std::optional<model::StatusValue> status_of_class(std::string_view c) {
  if (c == "status.lost") return model::StatusValue::Lost;
  if (c == "status.restored") return model::StatusValue::Restored;
  if (c == "status.superseded") return model::StatusValue::Superseded;
  if (c == "status.abandoned") return model::StatusValue::Abandoned;
  if (c == "status.partial") return model::StatusValue::Partial;
  if (c == "status.implemented") return model::StatusValue::Implemented;
  if (c == "status.planned") return model::StatusValue::Planned;
  return std::nullopt;
}

const std::set<std::string> kConnectives = {"potem", "then", "next", "na", "koncu", "finally", "and", "i", "oraz",
                                            "also", "plus", "a", "lastly", "najpierw", "first"};
const std::set<std::string> kNonFeatureKinds = {"project", "version", "platform", "language", "document", "repo",
                                                "tool", "protocol", "format", "legal_norm", "citation", "party",
                                                "actor", "character", "ui_framework", "sync", "storage", "role"};

class Run {
 public:
  Run(const Lexicons& lex, const kb::Pack& pack, std::string artifact_type, const std::vector<std::string>& ops,
      const UnitContent& c, const std::vector<Observation>& obs)
      : lex_(lex), pack_(pack), type_(std::move(artifact_type)), ops_(ops.begin(), ops.end()), c_(c), obs_(obs) {
    const Json& cal = pack.policy("calibration");
    if (const Json* r = json::find(cal, "reliability"); r && r->is_object()) {
      for (auto it = r->begin(); it != r->end(); ++it) {
        double a = json::get_number(it.value(), "alpha", 1), b = json::get_number(it.value(), "beta", 1);
        rel_[it.key()] = a / std::max(1e-9, a + b);
      }
    }
    if (const Json* cc = json::find(lex.thresholds, "concepts")) mention_cap_ = static_cast<int>(json::get_int(*cc, "mentions_cap_per_entity_unit", 32));
    ub_ = unit_blocks(c);
  }

  Extraction run();

 private:
  const Lexicons& lex_;
  const kb::Pack& pack_;
  std::string type_;
  std::set<std::string> ops_;
  const UnitContent& c_;
  const std::vector<Observation>& obs_;
  UnitBlocks ub_;
  std::map<std::string, double> rel_;
  int mention_cap_ = 32;

  std::vector<ObsInfo> info_;
  std::vector<Mention> mentions_;
  std::map<std::string, Entity> ents_;
  std::map<std::string, std::set<std::string>> ent_obs_;
  std::map<std::string, Claim> claims_;
  std::vector<model::Area> areas_;
  std::map<std::string, model::Principle> principles_;
  std::map<std::string, model::Decision> decisions_;
  std::map<std::string, model::Fork> forks_;
  std::map<std::string, model::StatusRecord> statuses_;
  std::vector<ClassifiedItem> items_;
  std::vector<std::pair<std::size_t, std::string>> versions_;  // (obs index, version)
  std::vector<std::string> item_type_;                         // per obs
  Json names_ = Json::array();
  Json gated_ = Json::array();
  int unanchored_ = 0;
  std::string subject_;
  std::string subject_kind_;
  std::string subject_project_kind_;
  Json stats_ = Json::object();

  bool op(std::string_view o) const { return ops_.count(std::string(o)) > 0; }
  double rel(std::string_view extractor) const {
    auto it = rel_.find(reliability_key(extractor));
    return it == rel_.end() ? 0.7 : it->second;
  }
  std::string ex_name(std::string_view n) const { return std::string(n) + "@" + std::string(kExtractorVersion); }

  // ── entities and claims ───────────────────────────────────────────
  std::string entity(const std::string& kind, std::string canonical, const std::string& label,
                     const std::string& surface, std::size_t obs, const std::string& method, double conf = 0.9) {
    if (canonical.empty()) canonical = lex_.fold(label);
    std::string id = Entity::make_id(kind, canonical);
    auto [it, fresh] = ents_.try_emplace(id);
    Entity& e = it->second;
    const Observation& o = *info_[obs].o;
    if (fresh) {
      e.id = id;
      e.kind = kind;
      e.canonical_key = canonical;
      e.label = label;
      e.evidence = model::EvidenceClass::Observed;
      e.origin = c_.origin;
      e.confidence = conf;
      e.attrs = Json{{"units", Json::array({c_.unit.id})}, {"observations", Json::array()}, {"method", method}};
    }
    e.confidence = std::max(e.confidence, conf);
    std::string akey = lex_.norm.phrase_key(surface.empty() ? label : surface, false);
    if (!akey.empty()) {
      bool found = false;
      for (auto& a : e.aliases) {
        if (a.key == akey) {
          ++a.count;
          found = true;
        }
      }
      if (!found) {
        model::Alias a;
        a.key = akey;
        a.surface = surface.empty() ? label : surface;
        a.lang = o.lang;
        a.method = method == "lexicon" ? "lexicon" : "mined";
        a.count = 1;
        a.confidence = conf;
        e.aliases.push_back(std::move(a));
      }
    }
    std::string d = o.date;
    if (!d.empty()) {
      if (e.first_seen.empty() || d < e.first_seen) e.first_seen = d;
      if (e.last_seen.empty() || d > e.last_seen) e.last_seen = d;
    }
    auto& os = ent_obs_[id];
    if (static_cast<int>(os.size()) < mention_cap_) os.insert(o.id);
    return id;
  }

  model::Support support(std::size_t obs, std::string quote, const std::string& extractor, double q) const {
    model::Support s;
    const Observation& o = *info_[obs].o;
    s.observation = o.id;
    s.locator = o.locator;
    s.quote = quote.empty() ? o.text : std::move(quote);
    s.extractor = extractor;
    s.quality = std::clamp(q, 0.0, 1.0);
    return s;
  }

  std::string claim(const std::string& subject, const std::string& pred, const std::string& object, const Json& value,
                    model::Qualifiers q, model::Support sup) {
    if (subject.empty() || (object.empty() && value.is_null())) return "";
    if (!object.empty() && object == subject) return "";
    std::string id = Claim::make_id(subject, pred, object, value, q);
    auto [it, fresh] = claims_.try_emplace(id);
    Claim& c = it->second;
    if (fresh) {
      c.id = id;
      c.subject = subject;
      c.predicate = pred;
      c.object = object;
      c.value = object.empty() ? value : Json();
      c.qualifiers = std::move(q);
      c.assessment.evidence = model::EvidenceClass::Observed;
      c.assessment.origin = c_.origin;
      c.assessment.status = model::ClaimStatus::Active;
    }
    double r = rel(sup.extractor) * sup.quality;
    bool dup = false;
    for (const auto& s : c.assessment.support) dup = dup || (s.observation == sup.observation && s.extractor == sup.extractor);
    if (!dup) c.assessment.support.push_back(std::move(sup));
    // one unit: noisy-OR collapses to the best support (unit-deduplicated)
    c.assessment.confidence = std::max(c.assessment.confidence, std::clamp(r, 0.0, 1.0));
    return id;
  }

  // ── helpers over observations ─────────────────────────────────────
  std::string surface_of(std::size_t obs, std::size_t tb, std::size_t te) const {
    const ObsInfo& in = info_[obs];
    if (in.otoks.size() == in.toks.size() && te > tb) {
      return in.o->text.substr(in.otoks[tb].start, in.otoks[te - 1].end - in.otoks[tb].start);
    }
    if (te > tb) return in.folded.substr(in.toks[tb].start, in.toks[te - 1].end - in.toks[tb].start);
    return "";
  }
  std::string surface_bytes(std::size_t obs, std::size_t fs, std::size_t fe) const {
    // folded byte range -> original surface via tokens
    const ObsInfo& in = info_[obs];
    std::size_t tb = in.toks.size(), te = 0;
    for (std::size_t i = 0; i < in.toks.size(); ++i) {
      if (in.toks[i].start >= fs && in.toks[i].end <= fe) {
        tb = std::min(tb, i);
        te = i + 1;
      }
    }
    if (tb >= te) return std::string(in.folded.substr(fs, fe - fs));
    return surface_of(obs, tb, te);
  }
  std::size_t tok_at(std::size_t obs, std::size_t fpos) const {
    const auto& t = info_[obs].toks;
    for (std::size_t i = 0; i < t.size(); ++i) {
      if (t[i].end > fpos) return i;
    }
    return t.size();
  }
  std::string block_folded(std::size_t obs) const {
    // context window: the observation and its neighbours of the same unit
    std::string w = lex_.fold(c_.unit.title) + " ";
    std::size_t a = obs >= 3 ? obs - 3 : 0, b = std::min(info_.size(), obs + 4);
    for (std::size_t i = a; i < b; ++i) {
      w += info_[i].folded;
      w += " ";
    }
    return w;
  }
  static bool window_has(std::string_view window, std::string_view term) {
    if (term.empty()) return false;
    Phrase p;
    p.text = std::string(term);
    p.prefix = true;
    return find_phrase(window, p, 0, nullptr) != std::string_view::npos;
  }
  bool is_stop(std::string_view t) const { return lex_.norm.is_stopword(t); }

  // phases
  void prepare();
  void find_lexicon_mentions();
  void find_named_mentions();
  void find_version_mentions();
  void choose_subject();
  void do_entities_lexicon();
  void do_relation_patterns();
  void do_versions();
  void do_items();
  void do_decisions();
  void do_status();
  void do_forks();
  void do_normative();
  void do_areas();
  void do_citations();
  void do_dates();
  void do_speakers();
  void do_headers();
  void do_code_symbols();

  std::string add_mention(std::size_t obs, const std::string& kind, const std::string& cls, std::string canonical,
                          const std::string& label, const std::string& surface, std::size_t fs, std::size_t fe,
                          const std::string& method, const std::string& source, double conf) {
    std::string id = entity(kind, std::move(canonical), label, surface, obs, method, conf);
    mentions_.push_back(Mention{obs, id, kind, cls, label, fs, fe, source});
    return id;
  }
  bool overlaps(std::size_t obs, std::size_t fs, std::size_t fe) const {
    for (const auto& m : mentions_) {
      if (m.obs == obs && fs < m.fend && m.fstart < fe) return true;
    }
    return false;
  }
  std::string nearest_version(std::size_t obs) const {
    std::string before, after;
    std::size_t bdist = SIZE_MAX, adist = SIZE_MAX;
    for (const auto& [i, v] : versions_) {
      if (i == obs) return v;
      if (i < obs && obs - i < bdist) {
        bdist = obs - i;
        before = v;
      }
      if (i > obs && i - obs < adist) {
        adist = i - obs;
        after = v;
      }
    }
    return !before.empty() ? before : after;
  }
  std::string branch_of(std::size_t obs) const {
    for (const auto& m : mentions_) {
      if (m.obs == obs && m.kind == "branch") return m.label;
    }
    return "";
  }
  std::vector<std::string> content_keys(std::string_view text) const {
    std::vector<std::string> out;
    for (const auto& t : lex_.norm.tokens(text)) {
      if (is_stop(t) || utf8::length(t) < 2) continue;
      out.push_back(lex_.norm.match_key(t));
    }
    return out;
  }
  std::vector<std::string> parse_options(std::string text) const;
  std::set<std::size_t> affirmed_;
  // First sentence of a user message that starts with an affirmation, right
  // after a message of the other side that asked something.
  bool affirms_question(std::size_t oi) const {
    const Observation& o = *info_[oi].o;
    if (o.speaker != "user" && o.speaker != "human") return false;
    std::string node = json::get_string(o.attrs, "node");
    if (node.empty()) return false;
    for (std::size_t k = oi; k-- > 0;) {
      if (json::get_string(info_[k].o->attrs, "node") == node && info_[k].o->kind != ObservationKind::Utterance) return false;
      if (info_[k].o->kind == ObservationKind::Utterance && json::get_string(info_[k].o->attrs, "node") != node) {
        const Observation& prev = *info_[k].o;
        if (prev.speaker == o.speaker || prev.text.find('?') == std::string::npos) return false;
        if (json::get_string(o.attrs, "parent") != json::get_string(prev.attrs, "node")) return false;
        for (const auto& h : lex_.match("affirmation", info_[oi].folded)) {
          if (h.pos == 0) return true;
        }
        return false;
      }
    }
    return false;
  }
  // content-token run nearest to a cue (feature label), tokens [tb, te)
  std::optional<std::pair<std::size_t, std::size_t>> topic_run(std::size_t obs, std::size_t from, std::size_t to,
                                                               const std::vector<std::pair<std::size_t, std::size_t>>& cues,
                                                               std::size_t anchor) const;
};

void Run::prepare() {
  bool has_leaf = false;
  for (const auto& o : obs_) {
    has_leaf = has_leaf || o.kind == ObservationKind::Sentence || o.kind == ObservationKind::ListItem;
  }
  info_.reserve(obs_.size());
  for (const auto& o : obs_) {
    ObsInfo in;
    in.o = &o;
    in.folded = lex_.fold(o.text);
    in.toks = tokenize(in.folded);
    in.otoks = tokenize(o.text);
    switch (o.kind) {
      case ObservationKind::Sentence:
      case ObservationKind::ListItem:
      case ObservationKind::Heading:
        in.text = true;
        break;
      case ObservationKind::Utterance:
      case ObservationKind::Field:
        in.text = !has_leaf || json::get_bool(o.attrs, "leaf");
        break;
      default:
        break;
    }
    info_.push_back(std::move(in));
  }
  item_type_.assign(info_.size(), "");
}

void Run::find_lexicon_mentions() {
  for (std::size_t oi = 0; oi < info_.size(); ++oi) {
    const ObsInfo& in = info_[oi];
    if (!in.text && in.o->kind != ObservationKind::CodeBlock) continue;
    const bool parallel = in.otoks.size() == in.toks.size();
    std::size_t i = 0;
    while (i < in.toks.size()) {
      const std::string& ft = in.toks[i].lower;
      bool hit = false;
      if (!is_stop(ft)) {
        std::string k1 = lex_.norm.match_key(ft, kb::Lang::En), k2 = lex_.norm.match_key(ft, kb::Lang::Pl);
        std::string k3 = parallel ? lex_.norm.match_key(in.otoks[i].lower) : k1;
        bool maybe = lex_.first_keys.count(k1) || lex_.first_keys.count(k2) || lex_.first_keys.count(k3);
        std::size_t maxl = std::min(in.toks.size() - i, lex_.max_form_tokens + 2);
        for (std::size_t L = maxl; L >= 1 && maybe && !hit; --L) {
          std::size_t fs = in.toks[i].start, fe = in.toks[i + L - 1].end;
          // a span ends on a content word and does not cross a clause delimiter
          if (L > 1 && is_stop(in.toks[i + L - 1].lower)) continue;
          if (L > 1 && std::string_view(in.folded.data() + fs, fe - fs).find_first_of(",.;:!?()") != std::string_view::npos) {
            continue;
          }
          if (L > 1 && std::string_view(in.folded.data() + fs, fe - fs).find(" - ") != std::string_view::npos) continue;
          std::string span = parallel ? in.o->text.substr(in.otoks[i].start, in.otoks[i + L - 1].end - in.otoks[i].start)
                                      : in.folded.substr(fs, fe - fs);
          std::set<std::string> keys;
          for (kb::Lang l : {kb::Lang::Unknown, kb::Lang::En, kb::Lang::Pl}) keys.insert(lex_.norm.phrase_key(span, false, l));
          for (const auto& k : keys) {
            auto it = lex_.forms.find(k);
            if (it == lex_.forms.end() || k.empty()) continue;
            for (const auto& f : it->second) {
              const LexEntry& e = lex_.entries[f.entry];
              if (!f.ambiguous && f.negative_ctx.empty() && f.requires_ctx.empty()) {
                // ok
              } else {
                std::string w = block_folded(oi);
                std::string neg;
                for (const auto& t : f.negative_ctx) {
                  if (window_has(w, t)) neg = t;
                }
                int n = 0;
                for (const auto& t : f.requires_ctx) n += window_has(w, t) ? 1 : 0;
                bool pass = neg.empty() && (!f.ambiguous || n >= f.ctx_min);
                if (!pass) {
                  gated_.push_back(Json{{"observation", in.o->id},
                                        {"form", f.surface},
                                        {"entry", e.id},
                                        {"reason", !neg.empty() ? "negative_context:" + neg : "missing_context"}});
                  continue;
                }
              }
              std::string method = e.source == "discovered" ? "mined" : "lexicon";
              std::string src = e.source == "gazetteer" ? "extract.gazetteer" : e.source == "discovered" ? "extract.names" : "extract.alias";
              add_mention(oi, e.kind, e.cls, e.canonical, e.label, span, fs, fe, method, ex_name(src),
                          e.source == "discovered" ? 0.7 : 0.9);
              hit = true;
              i += L;
              break;
            }
            if (hit) break;
          }
        }
        if (!hit) {
          // inflected single-token discovered names ("Strozu" for "Stroz")
          for (const auto& [key, f] : lex_.inflecting) {
            std::string fsurf = lex_.fold(f.surface);
            if (ft.size() > fsurf.size() && ft.size() <= fsurf.size() + 3 && ft.rfind(fsurf, 0) == 0) {
              const LexEntry& e = lex_.entries[f.entry];
              std::string surf = parallel ? in.otoks[i].surface : ft;
              add_mention(oi, e.kind, e.cls, e.canonical, e.label, surf, in.toks[i].start, in.toks[i].end, "mined",
                          ex_name("extract.names"), 0.6);
              hit = true;
              i += 1;
              break;
            }
          }
        }
      }
      if (!hit) ++i;
    }
  }
}

void Run::find_named_mentions() {
  const auto& heads = lex_.phrases("project.head");
  const auto& comp_heads = lex_.phrases("component.head");
  auto is_head = [&](std::string_view folded_tok) {
    for (const auto& p : heads) {
      if (folded_tok == p.text) return true;
    }
    return false;
  };
  auto comp_head_of = [&](std::string_view ident) {
    auto parts = archive::split_identifier(ident);
    if (parts.size() < 2) return false;
    std::string last = lex_.fold(parts.back());
    for (const auto& p : comp_heads) {
      if (last == p.text || (p.prefix && last.rfind(p.text, 0) == 0)) return true;
    }
    return false;
  };
  auto note_name = [&](const std::string& kind, const std::string& label, const std::vector<std::string>& aliases) {
    Json a = Json::array();
    for (const auto& x : aliases) a.push_back(x);
    names_.push_back(Json{{"kind", kind}, {"label", label}, {"aliases", a}});
  };
  auto mention_label = [&](std::size_t oi, const std::string& kind, const std::string& label, std::size_t fs, std::size_t fe,
                           const std::string& rule, double conf) {
    if (overlaps(oi, fs, fe)) {
      // an existing lexicon mention of the same span: keep it
      for (const auto& m : mentions_) {
        if (m.obs == oi && fs < m.fend && m.fstart < fe) return m.entity;
      }
    }
    return add_mention(oi, kind, "mined:" + rule, lex_.norm.phrase_key(label), label, label, fs, fe, "mined",
                       ex_name("extract.names"), conf);
  };
  // Claude project units: the project name.
  if (ub_.platform == "claude_projects") {
    std::string name = json::get_string(c_.structured, "name");
    auto toks = lex_.norm.tokens(name);
    static const std::set<std::string> kGeneric = {"dev", "app", "project", "projekt", "work", "main", "prod", "notes"};
    std::string label = name;
    if (toks.size() >= 2 && kGeneric.count(toks.back())) label = std::string(utf8::strip(name.substr(0, name.rfind(' '))));
    for (std::size_t oi = 0; oi < info_.size(); ++oi) {
      if (json::get_string(info_[oi].o->attrs, "field") == "name") {
        std::string id = mention_label(oi, "project", label, 0, info_[oi].folded.size(), "claude_project", 0.9);
        (void)id;
        note_name("project", label, label != name ? std::vector<std::string>{name} : std::vector<std::string>{});
      }
    }
  }
  const double fork_score = [&] {
    double s = 0;
    for (const auto& in : info_) s += in.text ? lex_.score("fork", in.folded) : 0.0;
    return s;
  }();
  for (std::size_t oi = 0; oi < info_.size(); ++oi) {
    const ObsInfo& in = info_[oi];
    if (!in.text) continue;
    const std::string& text = in.o->text;
    const std::string& f = in.folded;
    const bool parallel = in.otoks.size() == in.toks.size();
    // (a) appositive "<head np> (<Name>)" and (b) "projects (A/B, C, D)"
    for (std::size_t p = f.find('('); p != std::string::npos; p = f.find('(', p + 1)) {
      std::size_t q = f.find(')', p);
      if (q == std::string::npos) break;
      std::size_t tp = tok_at(oi, p);
      // head cue within the 4 tokens before '('
      std::size_t hb = tp >= 4 ? tp - 4 : 0;
      std::size_t head = SIZE_MAX;
      for (std::size_t k = hb; k < tp; ++k) {
        if (is_head(in.toks[k].lower)) head = k;
      }
      if (head == SIZE_MAX) continue;
      std::string inner = parallel ? text.substr(in.otoks[tp < in.otoks.size() ? tp : 0].start) : f.substr(p + 1);
      std::string_view body(f.data() + p + 1, q - p - 1);
      std::vector<std::string> items;
      std::size_t s = 0;
      while (s <= body.size()) {
        std::size_t c = body.find(',', s);
        if (c == std::string_view::npos) c = body.size();
        items.emplace_back(utf8::strip(body.substr(s, c - s)));
        s = c + 1;
      }
      std::string headtok = in.toks[head].lower;
      bool plural = headtok == "projektami" || headtok == "projekty" || headtok == "projects" || items.size() >= 2;
      if (items.size() == 1 && !plural) {
        // appositive: the head np is an alias of the name in parentheses
        std::size_t fs = p + 1, fe = q;
        std::string name = surface_bytes(oi, fs, fe);
        if (lex_.norm.tokens(name).size() > 3 || lex_.norm.tokens(name).empty()) continue;
        std::string np = surface_of(oi, head, tp);
        std::string id = mention_label(oi, "project", name, fs, fe, "appositive", 0.8);
        if (!np.empty()) {
          entity(ents_[id].kind, ents_[id].canonical_key, ents_[id].label, np, oi, "mined", 0.7);
          note_name("project", name, {np});
        }
      } else if (plural && items.size() >= 2) {
        std::size_t off = p + 1;
        for (const auto& it : items) {
          std::size_t at = f.find(it, off);
          if (at == std::string::npos || it.empty()) continue;
          off = at + it.size();
          // "A/B": aliases of one project
          std::vector<std::string> parts;
          std::size_t s2 = 0;
          while (s2 <= it.size()) {
            std::size_t sl = it.find('/', s2);
            if (sl == std::string::npos) sl = it.size();
            parts.emplace_back(utf8::strip(it.substr(s2, sl - s2)));
            s2 = sl + 1;
          }
          if (parts.empty() || lex_.norm.tokens(parts[0]).empty() || lex_.norm.tokens(parts[0]).size() > 4) continue;
          std::size_t p0 = f.find(parts[0], at);
          std::string label = surface_bytes(oi, p0, p0 + parts[0].size());
          std::string id = mention_label(oi, "project", label, p0, p0 + parts[0].size(), "enumeration", 0.75);
          std::vector<std::string> al;
          for (std::size_t k = 1; k < parts.size(); ++k) {
            std::size_t pk = f.find(parts[k], at);
            if (pk == std::string::npos || parts[k].empty()) continue;
            std::string sk = surface_bytes(oi, pk, pk + parts[k].size());
            entity("project", ents_[id].canonical_key, ents_[id].label, sk, oi, "mined", 0.7);
            al.push_back(sk);
          }
          note_name("project", label, al);
        }
      }
    }
    // (c) "(nowy) projekt: X" — the first noun phrase after the colon
    for (std::size_t k = 0; k + 1 < in.toks.size(); ++k) {
      if (!is_head(in.toks[k].lower)) continue;
      std::size_t after = in.toks[k].end;
      if (after >= f.size() || f[after] != ':') continue;
      std::size_t e = k + 1;
      std::size_t lim = std::min(in.toks.size(), k + 4);
      while (e < lim) {
        std::string_view gap(f.data() + in.toks[e - 1].end, in.toks[e].start - in.toks[e - 1].end);
        if (e > k + 1 && gap.find_first_of("+,-.;:(/") != std::string_view::npos) break;
        ++e;
      }
      std::size_t stop = e;
      // cut at the first delimiter after the colon
      for (std::size_t t = k + 2; t < e; ++t) {
        std::string_view gap(f.data() + in.toks[t - 1].end, in.toks[t].start - in.toks[t - 1].end);
        if (gap.find_first_of("+,-.;:(/") != std::string_view::npos) {
          stop = t;
          break;
        }
      }
      if (stop <= k + 1 || stop - (k + 1) > 3) continue;
      std::string label = surface_of(oi, k + 1, stop);
      if (lex_.norm.phrase_key(label).empty()) continue;
      mention_label(oi, "project", label, in.toks[k + 1].start, in.toks[stop - 1].end, "head_colon", 0.6);
      note_name("project", label, {});
    }
    // (d) CamelCase identifiers: components by their head, else concepts
    std::size_t from = 0;
    for (const auto& ident : archive::camel_identifiers(text)) {
      std::size_t at = text.find(ident, from);
      if (at == std::string::npos) continue;
      from = at + ident.size();
      std::string fid = lex_.fold(ident);
      std::size_t fs = f.find(fid);
      if (fs == std::string::npos) continue;
      if (overlaps(oi, fs, fs + fid.size())) continue;
      bool upper = std::all_of(ident.begin(), ident.end(), [](char ch) { return !std::islower(static_cast<unsigned char>(ch)); });
      if (upper) continue;
      std::string kind = comp_head_of(ident) ? "component" : "concept";
      // project head right before/after -> project
      std::size_t ti = tok_at(oi, fs);
      if ((ti > 0 && is_head(in.toks[ti - 1].lower)) || (ti + 1 < in.toks.size() && is_head(in.toks[ti + 1].lower))) {
        kind = "project";
      }
      mention_label(oi, kind, ident, fs, fs + fid.size(), "camel", 0.6);
      if (kind != "concept") note_name(kind, ident, {});
    }
    // (e) quoted Title Case names ('Paper Weather')
    for (char qc : {'\'', '"'}) {
      std::size_t a = text.find(qc);
      while (a != std::string::npos) {
        std::size_t b = text.find(qc, a + 1);
        if (b == std::string::npos) break;
        std::string inner = text.substr(a + 1, b - a - 1);
        auto toks = tokenize(inner);
        bool title = !toks.empty() && toks.size() <= 4 &&
                     std::all_of(toks.begin(), toks.end(), [](const Token& t) {
                       return !t.surface.empty() && std::isupper(static_cast<unsigned char>(t.surface[0]));
                     });
        if (title) {
          std::string fi = lex_.fold(inner);
          std::size_t fs = f.find(fi);
          if (fs != std::string::npos && !overlaps(oi, fs, fs + fi.size())) {
            mention_label(oi, "concept", inner, fs, fs + fi.size(), "quoted_title", 0.6);
            note_name("concept", inner, {});
          }
        }
        a = text.find(qc, b + 1);
      }
    }
    // (f) hyphenated lowercase names in a unit that talks about branches
    if (fork_score >= 1.5) {
      for (std::size_t k = 0; k + 1 < in.toks.size(); ++k) {
        if (in.toks[k].end + 1 != in.toks[k + 1].start || f[in.toks[k].end] != '-') continue;
        if (!parallel) continue;
        const std::string& s1 = in.otoks[k].surface;
        const std::string& s2 = in.otoks[k + 1].surface;
        auto lowerish = [](const std::string& s) {
          return std::none_of(s.begin() + 1, s.end(), [](char ch) { return std::isupper(static_cast<unsigned char>(ch)); });
        };
        if (!lowerish(s1) || !lowerish(s2) || !has_alpha(s1) || !has_alpha(s2)) continue;
        std::string label = lex_.fold(s1) + "-" + lex_.fold(s2);
        std::size_t fs = in.toks[k].start, fe = in.toks[k + 1].end;
        bool known_branch = false;
        for (const auto& m : mentions_) known_branch = known_branch || (m.obs == oi && m.kind == "branch" && m.fstart == fs);
        if (known_branch) continue;
        // only when the name is used as a name: seen twice in the unit or next to a fork cue
        int seen = 0;
        for (const auto& other : info_) seen += other.folded.find(label) != std::string::npos ? 1 : 0;
        // "python-quick: ..." — a name that heads its sentence
        std::string_view after(f.data() + fe, f.size() - fe);
        bool leads = k == 0 && !after.empty() && after[0] == ':';
        if (seen < 2 && !leads && lex_.score("fork", in.folded) <= 0) continue;
        // a compound name beats its parts ("python-quick" is not "Python")
        mentions_.erase(std::remove_if(mentions_.begin(), mentions_.end(),
                                       [&](const Mention& m) { return m.obs == oi && fs < m.fend && m.fstart < fe; }),
                        mentions_.end());
        mention_label(oi, "branch", label, fs, fe, "branch_name", 0.7);
        note_name("branch", label, {});
      }
    }
  }
}

void Run::find_version_mentions() {
  if (!lex_.version_re) return;
  std::vector<std::string> status_classes = lex_.classes_with_prefix("status.");
  for (std::size_t oi = 0; oi < info_.size(); ++oi) {
    const ObsInfo& in = info_[oi];
    if (!in.text && in.o->kind != ObservationKind::Field) continue;
    std::u32string u = utf8::decode(in.o->text);
    // declarations: which versions are declared by a pattern
    std::map<std::string, double> declared;
    for (const auto& d : lex_.version_decls) {
      for (const auto& m : d.re.finditer(u)) {
        if (m.group_count() >= 1 && m.matched(1)) {
          std::string v = kb::normalize_version(m.group_utf8(1));
          if (!v.empty()) declared[v] = std::max(declared[v], d.confidence);
        }
      }
    }
    for (const auto& m : lex_.version_re->finditer(u)) {
      std::string raw = m.group_utf8(0);
      std::string v = kb::normalize_version(m.matched(1) ? m.group_utf8(1) : raw);
      if (v.empty()) continue;
      std::size_t bstart = utf8::byte_offset(in.o->text, static_cast<std::size_t>(m.start(0)));
      std::size_t bend = utf8::byte_offset(in.o->text, static_cast<std::size_t>(m.end(0)));
      // "0.6.x", "1.2.3.4", "3.5%": not a version of ours
      if (bend < in.o->text.size() && (in.o->text[bend] == '.' || in.o->text[bend] == '%') && bend + 1 < in.o->text.size() &&
          std::isalnum(static_cast<unsigned char>(in.o->text[bend + 1]))) {
        continue;
      }
      std::string ctx = lex_.fold(in.o->text.substr(bstart >= 12 ? bstart - 12 : 0, (bstart >= 12 ? 12 : bstart) + (bend - bstart) + 4));
      bool excluded = false;
      for (const auto& x : lex_.version_excludes) excluded = excluded || (!x.empty() && ctx.find(x) != std::string::npos);
      if (excluded) continue;
      // anchors
      double q = 0.0;
      std::string how;
      if (auto it = declared.find(v); it != declared.end()) {
        q = it->second;
        how = "declaration";
      }
      if (raw.size() > 0 && (raw[0] == 'v' || raw[0] == 'V') && q < 0.8) {
        q = 0.8;
        how = "v_prefix";
      }
      // map the byte offset to folded token index approximately via token order
      std::size_t ti = 0;
      if (in.otoks.size() == in.toks.size()) {
        while (ti < in.otoks.size() && in.otoks[ti].end <= bstart) ++ti;
      }
      std::size_t lo = ti >= static_cast<std::size_t>(lex_.version_window) ? ti - lex_.version_window : 0;
      std::size_t hi = std::min(in.toks.size(), ti + lex_.version_window);
      if (q < 0.8) {
        for (std::size_t k = lo; k < hi; ++k) {
          for (const auto& a : lex_.version_anchors) {
            if (in.toks[k].lower.rfind(a, 0) == 0) {
              q = 0.8;
              how = "anchor_word";
            }
          }
        }
      }
      std::string project;
      for (const auto& mn : mentions_) {
        if (mn.obs == oi && mn.kind == "project") {
          project = mn.entity;
          if (q < 0.7) {
            q = 0.7;
            how = "project_alias";
          }
        }
      }
      if (q < 0.6) {
        for (const auto& cls : status_classes) {
          if (lex_.score(cls, in.folded) > 0) {
            q = 0.6;
            how = "status_cue";
          }
        }
      }
      if (q <= 0) {
        ++unanchored_;
        continue;
      }
      versions_.emplace_back(oi, v);
      std::string ffs = lex_.fold(raw);
      std::size_t fs = in.folded.find(ffs);
      Mention mn;
      mn.obs = oi;
      mn.kind = "version";
      mn.cls = how;
      mn.label = v;
      mn.fstart = fs == std::string::npos ? 0 : fs;
      mn.fend = fs == std::string::npos ? 0 : fs + ffs.size();
      mn.entity = project;  // the version's project when named nearby
      mn.source = std::to_string(q);
      mentions_.push_back(std::move(mn));
    }
  }
}

void Run::choose_subject() {
  if (c_.subject.is_object() && !json::get_string(c_.subject, "label").empty()) {
    std::string label = json::get_string(c_.subject, "label");
    std::string kind = json::get_string(c_.subject, "kind", "project");
    std::size_t oi = 0;
    subject_ = info_.empty() ? Entity::make_id(kind, lex_.norm.phrase_key(label))
                             : entity(kind, lex_.norm.phrase_key(label), label, label, oi, "mined", 1.0);
    subject_kind_ = kind;
  } else {
    std::map<std::string, std::pair<int, std::size_t>> count;  // entity -> (count, first position)
    for (std::size_t k = 0; k < mentions_.size(); ++k) {
      if (mentions_[k].kind != "project") continue;
      auto& c = count[mentions_[k].entity];
      if (c.first == 0) c.second = k;
      ++c.first;
    }
    std::string best;
    std::pair<int, std::size_t> bs{0, 0};
    for (const auto& [id, c] : count) {
      if (c.first > bs.first || (c.first == bs.first && c.second < bs.second)) {
        best = id;
        bs = c;
      }
    }
    if (!best.empty()) {
      subject_ = best;
      subject_kind_ = "project";
    }
  }
  if (subject_.empty()) {
    // A unit that names no project is about itself (the document).
    std::string label = c_.unit.title.empty() ? c_.unit.id : c_.unit.title;
    std::string canonical = "unit " + c_.unit.id;
    if (!info_.empty()) {
      subject_ = entity("document", canonical, label, "", 0, "mined", 1.0);
      ents_[subject_].aliases.clear();
    } else {
      subject_ = Entity::make_id("document", canonical);
    }
    subject_kind_ = "document";
  }
  // project kind of the subject (profile projects name theirs)
  const Json& prof = pack_.profile("self");
  if (auto it = ents_.find(subject_); it != ents_.end() && subject_kind_ == "project") {
    if (const Json* ps = json::find(prof, "projects"); ps && ps->is_array()) {
      for (const auto& p : *ps) {
        if (lex_.norm.phrase_key(json::get_string(p, "name")) == it->second.canonical_key) {
          subject_project_kind_ = json::get_string(p, "project_kind");
        }
      }
    }
  }
}

void Run::do_entities_lexicon() {
  // Provenance of every typed mention: (entity, mentioned_in, unit).
  for (const auto& m : mentions_) {
    if (m.kind == "version" || m.entity.empty()) continue;
    model::Qualifiers q;
    std::string ex = m.source.empty() ? ex_name("extract.alias") : m.source;
    claim(m.entity, "mentioned_in", "", Json(c_.unit.id), q, support(m.obs, surface_bytes(m.obs, m.fstart, m.fend), ex, 1.0));
  }
}

void Run::do_versions() {
  for (const auto& m : mentions_) {
    if (m.kind != "version") continue;
    model::Qualifiers q;
    q.version = m.label;
    q.branch = branch_of(m.obs);
    std::string subj = m.entity.empty() ? subject_ : m.entity;
    double quality = std::atof(m.source.c_str());
    claim(subj, "has_version", "", Json(m.label), q, support(m.obs, "", ex_name("extract.versions"), quality));
  }
}

void Run::do_relation_patterns() {
  struct PT {
    std::string f;
    std::size_t start, end;
    bool word;
  };
  for (std::size_t oi = 0; oi < info_.size(); ++oi) {
    const ObsInfo& in = info_[oi];
    if (!in.text) continue;
    const std::string& f = in.folded;
    // clause-aware token stream: words + joiners/arrows; hard stops split clauses
    std::vector<std::vector<PT>> clauses(1);
    std::size_t ti = 0;
    std::size_t i = 0;
    while (i < f.size()) {
      if (ti < in.toks.size() && in.toks[ti].start == i) {
        clauses.back().push_back(PT{in.toks[ti].lower, in.toks[ti].start, in.toks[ti].end, true});
        i = in.toks[ti].end;
        ++ti;
        continue;
      }
      std::string_view rest(f.data() + i, f.size() - i);
      if (rest.substr(0, 3) == "\xE2\x86\x92") {  // →
        clauses.back().push_back(PT{"→", i, i + 3, false});
        i += 3;
      } else if (rest.substr(0, 2) == "->" || rest.substr(0, 2) == "=>") {
        clauses.back().push_back(PT{std::string(rest.substr(0, 2)), i, i + 2, false});
        i += 2;
      } else if (rest[0] == ',' || rest[0] == '+' || rest[0] == '/' || rest[0] == '&') {
        clauses.back().push_back(PT{std::string(1, rest[0]), i, i + 1, false});
        ++i;
      } else if (rest[0] == '.' || rest[0] == ';' || rest[0] == ':' || rest[0] == '(' || rest[0] == ')' || rest[0] == '?' ||
                 rest[0] == '!') {
        if (!clauses.back().empty()) clauses.emplace_back();
        ++i;
      } else {
        ++i;
      }
    }
    auto mentions_at = [&](std::size_t fstart) {
      std::vector<const Mention*> out;
      for (const auto& m : mentions_) {
        if (m.obs == oi && m.fstart == fstart && m.fend > m.fstart && m.kind != "version") out.push_back(&m);
        if (m.obs == oi && m.fstart == fstart && m.kind == "version") out.push_back(&m);
      }
      return out;
    };
    auto type_ok = [&](const Mention& m, const std::vector<std::string>& types) {
      for (const auto& t : types) {
        if (t == "any") return true;
        if (m.kind == t || m.cls == t || m.cls.rfind(t + ".", 0) == 0) return true;
      }
      return false;
    };
    auto lit_ok = [](const PT& t, const Phrase& p) {
      if (p.prefix) return t.f.rfind(p.text, 0) == 0;
      return t.f == p.text;
    };
    for (const auto& toks : clauses) {
      for (const auto& pat : lex_.patterns) {
        struct Bind {
          std::string slot;
          std::vector<std::string> entities;  // mention entities (versions: labels prefixed "v:")
          std::string text;
          std::size_t tb = 0, te = 0;
        };
        std::vector<Bind> best;
        std::function<bool(std::size_t, std::size_t, std::vector<Bind>&)> go = [&](std::size_t ei, std::size_t j,
                                                                                  std::vector<Bind>& b) -> bool {
          if (ei == pat.elems.size()) return true;
          const PatElem& el = pat.elems[ei];
          if (el.kind == PatElem::Literal || el.kind == PatElem::Alt) {
            for (const auto& alt : el.alts) {
              std::size_t k = j;
              bool ok = true;
              for (const auto& w : alt) {
                if (k >= toks.size() || !lit_ok(toks[k], w)) {
                  ok = false;
                  break;
                }
                ++k;
              }
              if (ok && go(ei + 1, k, b)) return true;
            }
            return false;
          }
          if (el.kind == PatElem::Wild) {
            for (std::size_t k = j; k <= std::min(toks.size(), j + 3); ++k) {
              if (go(ei + 1, k, b)) return true;
            }
            return false;
          }
          // slot
          if (j >= toks.size()) return false;
          bool is_text = std::find(el.types.begin(), el.types.end(), "text") != el.types.end();
          if (is_text) {
            std::size_t k = toks.size();
            Bind bd{el.slot, {}, f.substr(toks[j].start, toks[k - 1].end - toks[j].start), j, k};
            b.push_back(bd);
            if (go(ei + 1, k, b)) return true;
            b.pop_back();
            return false;
          }
          for (const Mention* m : mentions_at(toks[j].start)) {
            if (!type_ok(*m, el.types)) continue;
            std::size_t k = j;
            while (k < toks.size() && toks[k].start < m->fend) ++k;
            Bind bd{el.slot, {m->kind == "version" ? "v:" + m->label : m->entity}, "", j, k};
            if (el.plus) {
              // more mentions joined by and / i / oraz / , / +
              while (k + 1 < toks.size()) {
                const std::string& jn = toks[k].f;
                if (jn != "," && jn != "+" && jn != "and" && jn != "i" && jn != "oraz" && jn != "&" && jn != "/") break;
                bool more = false;
                for (const Mention* m2 : mentions_at(toks[k + 1].start)) {
                  if (!type_ok(*m2, el.types)) continue;
                  std::size_t k2 = k + 1;
                  while (k2 < toks.size() && toks[k2].start < m2->fend) ++k2;
                  bd.entities.push_back(m2->entity);
                  k = k2;
                  more = true;
                  break;
                }
                if (!more) break;
              }
            }
            bd.te = k;
            b.push_back(bd);
            if (go(ei + 1, k, b)) return true;
            b.pop_back();
          }
          if (std::find(el.types.begin(), el.types.end(), "any") != el.types.end()) {
            for (std::size_t L = 1; L <= 3 && j + L <= toks.size(); ++L) {
              bool okrun = true;
              for (std::size_t k = j; k < j + L; ++k) okrun = okrun && toks[k].word && !is_stop(toks[k].f);
              if (!okrun) break;
              Bind bd{el.slot, {}, f.substr(toks[j].start, toks[j + L - 1].end - toks[j].start), j, j + L};
              b.push_back(bd);
              if (go(ei + 1, j + L, b)) return true;
              b.pop_back();
            }
          }
          return false;
        };
        for (std::size_t j = 0; j < toks.size(); ++j) {
          std::vector<Bind> b;
          if (!go(0, j, b)) continue;
          // materialise
          std::vector<std::string> subs, objs;
          Json value;
          auto ent_of = [&](const Bind& bd, std::vector<std::string>& out) {
            for (const auto& e : bd.entities) {
              if (e.rfind("v:", 0) == 0) {
                // a version slot: the version as an entity of its own
                std::string v = e.substr(2);
                out.push_back(entity("version", v, v, v, oi, "mined", 0.8));
              } else if (!e.empty()) {
                out.push_back(e);
              }
            }
            if (bd.entities.empty() && !bd.text.empty()) {
              std::string surf = surface_bytes(oi, toks[bd.tb].start, toks[bd.te - 1].end);
              out.push_back(entity("option", lex_.norm.phrase_key(surf), surf, surf, oi, "mined", 0.6));
            }
          };
          for (const auto& bd : b) {
            bool is_text_slot = false;
            for (const auto& el : pat.elems) {
              if (el.kind == PatElem::Slot && el.slot == bd.slot) {
                is_text_slot = std::find(el.types.begin(), el.types.end(), "text") != el.types.end();
              }
            }
            if (bd.slot == "subj") ent_of(bd, subs);
            else if (is_text_slot) value = Json(clip(surface_bytes(oi, toks[bd.tb].start, toks[bd.te - 1].end), 200));
            else ent_of(bd, objs);
          }
          if (pat.subject_ref == "$context_project") subs = {subject_};
          std::string quote = surface_bytes(oi, toks[b.front().tb].start, toks[b.back().te - 1].end);
          for (const auto& s : subs) {
            model::Qualifiers q;
            q.lang = in.o->lang;
            q.extra = Json{{"pattern", pat.id}};
            if (!value.is_null()) {
              claim(s, pat.rel, "", value, q, support(oi, quote, ex_name("extract.relation_patterns"), pat.confidence));
            }
            for (const auto& o : objs) {
              claim(s, pat.rel, o, Json(), q, support(oi, quote, ex_name("extract.relation_patterns"), pat.confidence));
            }
          }
          break;  // one match per pattern per clause
        }
      }
    }
  }
}

void Run::do_items() {
  for (std::size_t oi = 0; oi < info_.size(); ++oi) {
    const ObsInfo& in = info_[oi];
    if (!in.text) continue;
    std::map<std::string, double> score;
    for (const auto& [type, phrases] : lex_.item_cues) {
      for (const auto& p : phrases) {
        std::size_t len = 0;
        if (find_phrase(in.folded, p, 0, &len) != std::string_view::npos) score[type] += p.w;
      }
    }
    // "must not" is an invariant, not a requirement
    if (score.count("invariant") && score.count("requirement")) {
      Phrase mn = make_phrase(lex_.norm, "must not", 1);
      if (find_phrase(in.folded, mn, 0, nullptr) != std::string_view::npos) score["requirement"] -= 1.5;
    }
    score["decision"] += lex_.score("decision", in.folded);
    std::string_view t = utf8::rstrip(in.o->text);
    while (!t.empty() && (t.back() == ')' || t.back() == '*' || t.back() == '"')) t.remove_suffix(1);
    if (!t.empty() && t.back() == '?') score["open_question"] += 2.5;
    if (in.folded.rfind("czy ", 0) == 0) score["open_question"] += 1.0;
    std::string heading = lex_.fold(json::get_string(in.o->attrs, "heading"));
    for (const auto& [type, p] : lex_.heading_hints) {
      if (!heading.empty() && find_phrase(heading, p, 0, nullptr) == 0) score[type] += p.w;
    }
    auto rank = [&](const std::string& ty) {
      auto it = std::find(lex_.item_types.begin(), lex_.item_types.end(), ty);
      return static_cast<int>(it - lex_.item_types.begin());
    };
    std::string best;
    double bs = 0, second = 0;
    for (const auto& [ty, v] : score) {
      if (v > bs || (v == bs && v > 0 && rank(ty) < rank(best))) {
        second = std::max(second, bs);
        best = ty;
        bs = v;
      } else {
        second = std::max(second, v);
      }
    }
    if (best.empty() || bs < 1.5) {
      // an owner's reply that opens with an affirmation to a question of the
      // other side accepts what was asked ("tak, ...", "no to SQLite")
      if (affirms_question(oi)) {
        item_type_[oi] = "decision";
        affirmed_.insert(oi);
      }
      continue;
    }
    item_type_[oi] = best;
    if (!op("items")) continue;
    if (op("decisions") && best == "decision") continue;  // the decisions extractor owns them
    static const std::map<std::string, std::string> kPred = {{"requirement", "has_requirement"},
                                                             {"invariant", "has_invariant"},
                                                             {"open_question", "has_question"},
                                                             {"decision", "has_decision"},
                                                             {"rejected_option", "has_decision"}};
    auto pit = kPred.find(best);
    if (pit == kPred.end()) continue;
    double conf = std::clamp(0.35 + 0.15 * bs - (bs - second < 0.5 ? 0.1 : 0.0), 0.3, 0.95);
    model::Qualifiers q;
    q.lang = in.o->lang;
    q.extra = Json{{"item_type", best}};
    claim(subject_, pit->second, "", Json(clip(in.o->text, 300)), q, support(oi, "", ex_name("extract.items"), conf));
  }
}

std::vector<std::string> Run::parse_options(std::string text) const {
  // drop parenthesised asides
  std::string s;
  int depth = 0;
  for (char c : text) {
    if (c == '(') ++depth;
    else if (c == ')') depth = std::max(0, depth - 1);
    else if (depth == 0) s.push_back(c);
  }
  while (!s.empty() && (s.back() == '?' || s.back() == ' ' || s.back() == '.')) s.pop_back();
  if (std::size_t d = s.rfind(" - "); d != std::string::npos) s = s.substr(d + 3);
  std::string low = utf8::to_lower(s);
  static const std::vector<std::string> kSep = {", czy ", ", albo ", ", or ", " czy ", " albo ", " or ", " vs. ",
                                                " vs ", " versus ", " lub ", ", "};
  std::vector<std::string> parts{s};
  std::vector<std::string> lparts{low};
  for (const auto& sep : kSep) {
    std::vector<std::string> np, nl;
    for (std::size_t k = 0; k < parts.size(); ++k) {
      std::size_t a = 0;
      const std::string& l = lparts[k];
      for (std::size_t p = l.find(sep); p != std::string::npos; p = l.find(sep, a)) {
        np.push_back(parts[k].substr(a, p - a));
        nl.push_back(l.substr(a, p - a));
        a = p + sep.size();
      }
      np.push_back(parts[k].substr(a));
      nl.push_back(l.substr(a));
    }
    parts = std::move(np);
    lparts = std::move(nl);
  }
  std::vector<std::string> out;
  for (auto& p : parts) {
    std::string x(utf8::strip(p));
    for (const char* lead : {"czy ", "Czy ", "or ", "either ", "whether ", "po prostu ", "just ", "albo "}) {
      if (x.rfind(lead, 0) == 0) x = x.substr(std::char_traits<char>::length(lead));
    }
    x = std::string(utf8::strip(x));
    auto n = lex_.norm.tokens(x).size();
    if (n == 0 || n > 8) return {};
    out.push_back(x);
  }
  if (out.size() < 2 || out.size() > 6) return {};
  return out;
}

void Run::do_decisions() {
  for (std::size_t oi = 0; oi < info_.size(); ++oi) {
    const ObsInfo& in = info_[oi];
    if (!in.text) continue;
    double reversal = lex_.score("decision.reversal", in.folded);
    if (item_type_[oi] != "decision" && reversal < 2.5) continue;
    // alternatives: the latest option enumeration in the 8 observations before (or this one)
    std::vector<std::string> options;
    std::size_t opt_obs = SIZE_MAX;
    for (std::size_t back = 0; back <= 8 && back <= oi; ++back) {
      std::size_t k = oi - back;
      if (!info_[k].text) continue;
      const std::string& t = info_[k].o->text;
      bool question = utf8::rstrip(t).size() > 0 && utf8::rstrip(t).back() == '?';
      bool enumer = lex_.score("enumeration.options", info_[k].folded) > 0;
      if (!question && !enumer) continue;  // the options come from a question or an enumeration
      auto opts = parse_options(t);
      if (opts.size() >= 2) {
        options = std::move(opts);
        opt_obs = k;
        break;
      }
    }
    auto dkeys = content_keys(in.o->text);
    std::set<std::string> dset(dkeys.begin(), dkeys.end());
    int chosen = -1;
    int best_overlap = 0;
    for (std::size_t k = 0; k < options.size(); ++k) {
      int ov = 0;
      for (const auto& key : content_keys(options[k])) ov += dset.count(key) ? 1 : 0;
      if (ov > best_overlap) {
        best_overlap = ov;
        chosen = static_cast<int>(k);
      }
    }
    std::string chosen_label = chosen >= 0 ? options[static_cast<std::size_t>(chosen)] : clip(in.o->text, 160);
    model::Qualifiers q;
    q.lang = in.o->lang;
    q.valid_from = date10(in.o->date);
    q.branch = branch_of(oi);
    q.extra = Json{{"reversal", reversal >= 2.5}};
    double conf = (affirmed_.count(oi) ? 0.45 : 0.6) + (chosen >= 0 ? 0.2 : 0.0);
    std::string cid = claim(subject_, "decides", "", Json(chosen_label), q, support(oi, "", ex_name("extract.decisions"), conf));
    if (cid.empty()) continue;
    if (opt_obs != SIZE_MAX && opt_obs != oi) {
      claims_[cid].assessment.support.push_back(support(opt_obs, "", ex_name("extract.options"), 0.8));
    }
    model::Decision d;
    d.id = cid;
    d.subject = subject_;
    d.date = in.o->date;
    for (std::size_t k = 0; k < options.size(); ++k) {
      model::DecisionAlternative a;
      a.label = options[k];
      a.value = Json(options[k]);
      a.chosen = static_cast<int>(k) == chosen;
      d.alternatives.push_back(std::move(a));
    }
    if (chosen < 0) {
      model::DecisionAlternative a;
      a.label = chosen_label;
      a.value = Json(chosen_label);
      a.chosen = true;
      d.alternatives.push_back(std::move(a));
    }
    decisions_[cid] = std::move(d);
  }
}

std::optional<std::pair<std::size_t, std::size_t>> Run::topic_run(
    std::size_t oi, std::size_t from, std::size_t to, const std::vector<std::pair<std::size_t, std::size_t>>& cues,
    std::size_t anchor) const {
  const ObsInfo& in = info_[oi];
  auto usable = [&](std::size_t k) {
    const Token& t = in.toks[k];
    if (is_stop(t.lower) || lex_.negators.count(t.lower) || lex_.generic_words.count(t.lower)) return false;
    if (!has_alpha(t.lower) || utf8::length(t.lower) < 3) return false;
    for (const auto& a : lex_.version_anchors) {
      if (t.lower.rfind(a, 0) == 0) return false;  // "wersji", "version": the version, not the feature
    }
    for (auto [a, b] : cues) {
      if (t.start < b && a < t.end) return false;
    }
    for (const auto& cls : kStatusClasses) {
      for (const auto& p : lex_.phrases(cls)) {
        if (t.lower == p.text || (p.prefix && t.lower.rfind(p.text, 0) == 0)) return false;
      }
    }
    return true;
  };
  std::vector<std::pair<std::size_t, std::size_t>> runs;
  std::size_t k = from;
  while (k < to) {
    if (!usable(k)) {
      ++k;
      continue;
    }
    std::size_t e = k;
    while (e < to && e - k < 3 && usable(e)) {
      if (e > k) {
        std::string_view gap(in.folded.data() + in.toks[e - 1].end, in.toks[e].start - in.toks[e - 1].end);
        if (gap.find_first_of(",.;:()") != std::string_view::npos) break;
      }
      ++e;
    }
    runs.emplace_back(k, e);
    k = e;
  }
  if (runs.empty()) return std::nullopt;
  // nearest run to the anchor token
  std::size_t best = 0;
  std::size_t bd = SIZE_MAX;
  for (std::size_t r = 0; r < runs.size(); ++r) {
    std::size_t d = runs[r].second <= anchor ? anchor - runs[r].second : runs[r].first >= anchor ? runs[r].first - anchor : 0;
    if (d < bd) {
      bd = d;
      best = r;
    }
  }
  return runs[best];
}

void Run::do_status() {
  struct Topic {
    std::size_t obs = 0;
    std::string entity;  // known entity, or "" (created on first use from label)
    std::string label;
  };
  std::optional<Topic> last_topic;
  for (std::size_t oi = 0; oi < info_.size(); ++oi) {
    const ObsInfo& in = info_[oi];
    if (!in.text) continue;
    // clauses: split at , ; and " - "
    std::vector<std::pair<std::size_t, std::size_t>> clauses;
    std::size_t a = 0;
    for (std::size_t i = 0; i <= in.folded.size(); ++i) {
      bool cut = i == in.folded.size() || in.folded[i] == ',' || in.folded[i] == ';' ||
                 (in.folded.compare(i, 3, " - ") == 0);
      if (cut) {
        if (i > a) clauses.emplace_back(a, i);
        a = i + 1;
      }
    }
    bool emitted = false;
    for (auto [cs, ce] : clauses) {
      std::string_view clause(in.folded.data() + cs, ce - cs);
      std::string best_cls;
      double bs = 0;
      std::vector<std::pair<std::size_t, std::size_t>> cue_spans;
      std::size_t cue_pos = cs;
      for (const auto& cls : kStatusClasses) {
        double s = 0;
        for (const auto& h : lex_.match(cls, clause)) {
          cue_spans.emplace_back(cs + h.pos, cs + h.pos + h.len);
          if (!h.negated) s += h.w;
        }
        if (s > bs) {
          bs = s;
          best_cls = cls;
          for (const auto& h : lex_.match(cls, clause)) {
            if (!h.negated) {
              cue_pos = cs + h.pos;
              break;
            }
          }
        }
      }
      if (best_cls.empty() || bs < lex_.status_min_weight) continue;
      auto sv = status_of_class(best_cls);
      if (!sv) continue;
      // the subject: a feature-like mention in the clause, else the nearest content run
      std::string ent;
      for (const auto& m : mentions_) {
        if (m.obs == oi && m.fstart >= cs && m.fend <= ce && !m.entity.empty() && !kNonFeatureKinds.count(m.kind)) {
          ent = m.entity;
          break;
        }
      }
      std::size_t tb = tok_at(oi, cs), te = tok_at(oi, ce);
      std::string quote = surface_bytes(oi, cs, ce);
      if (ent.empty()) {
        auto run = topic_run(oi, tb, te, cue_spans, tok_at(oi, cue_pos));
        if (run) {
          std::string label = surface_of(oi, run->first, run->second);
          ent = entity("feature", lex_.norm.phrase_key(label), label, label, oi, "mined", 0.6);
        } else if (last_topic && oi - last_topic->obs <= 2) {
          // anaphora: the topic of the preceding observation
          if (last_topic->entity.empty()) {
            last_topic->entity = entity("feature", lex_.norm.phrase_key(last_topic->label), last_topic->label,
                                        last_topic->label, last_topic->obs, "mined", 0.5);
          }
          ent = last_topic->entity;
        }
      }
      if (ent.empty()) continue;
      model::Qualifiers q;
      q.version = nearest_version(oi);
      q.branch = branch_of(oi);
      q.valid_from = date10(in.o->date);
      q.lang = in.o->lang;
      std::string st(model::to_string(*sv));
      double conf = std::min(0.95, 0.4 + 0.15 * bs);
      std::string cid = claim(ent, "has_status", "", Json(st), q, support(oi, quote, ex_name("extract.status_cues"), conf));
      model::StatusRecord r;
      r.entity = ent;
      r.branch = q.branch;
      r.version = q.version;
      r.status = *sv;
      r.date = in.o->date;
      r.claim = cid;
      r.id = model::StatusRecord::make_id(r.entity, r.branch, r.version, r.status, r.date);
      statuses_[r.id] = r;
      emitted = true;
    }
    // A changelog line "0.3: desktop notification channel." states what the
    // version contains: implemented in that version.
    if (!emitted) {
      std::size_t colon = in.folded.find(':');
      const Mention* vm = nullptr;
      for (const auto& m : mentions_) {
        if (m.obs == oi && m.kind == "version" && m.fstart <= 1 && colon != std::string::npos && m.fend <= colon &&
            utf8::strip(std::string_view(in.folded).substr(m.fend, colon - m.fend)).empty()) {
          vm = &m;
        }
      }
      if (vm) {
        std::size_t end = in.folded.find_first_of(",.;(", colon);
        if (end == std::string::npos) end = in.folded.size();
        std::size_t tb = tok_at(oi, colon), te = tok_at(oi, end);
        auto run = topic_run(oi, tb, te, {}, tb);
        if (run && run->first == tb) {
          std::size_t re = std::min(te, run->first + 3);
          std::string label = surface_of(oi, run->first, re);
          std::string ent = entity("feature", lex_.norm.phrase_key(label), label, label, oi, "mined", 0.6);
          model::Qualifiers q;
          q.version = vm->label;
          q.branch = branch_of(oi);
          q.valid_from = date10(in.o->date);
          q.lang = in.o->lang;
          std::string cid = claim(ent, "has_status", "", Json("implemented"), q,
                                  support(oi, surface_bytes(oi, 0, end), ex_name("extract.status_cues"), 0.6));
          model::StatusRecord r;
          r.entity = ent;
          r.branch = q.branch;
          r.version = q.version;
          r.status = model::StatusValue::Implemented;
          r.date = in.o->date;
          r.claim = cid;
          r.id = model::StatusRecord::make_id(r.entity, r.branch, r.version, r.status, r.date);
          statuses_[r.id] = r;
        }
      }
    }
    // the observation's topic for anaphora in the next ones
    std::string topic;
    for (const auto& m : mentions_) {
      if (m.obs == oi && !m.entity.empty() && !kNonFeatureKinds.count(m.kind)) topic = m.entity;
    }
    if (topic.empty()) {
      auto run = topic_run(oi, 0, in.toks.size(), {}, 0);
      if (run && run->second - run->first >= 1) {
        std::string label = surface_of(oi, run->first, run->second);
        std::string key = lex_.norm.phrase_key(label);
        std::string id = Entity::make_id("feature", key);
        if (ents_.count(id)) topic = id;
        else if (!key.empty()) {
          last_topic = Topic{oi, "", label};
          continue;
        }
      }
    }
    if (!topic.empty()) last_topic = Topic{oi, topic, ""};
  }
}

void Run::do_forks() {
  // conversation forks (edited messages keep both sides)
  std::map<std::string, std::size_t> node_obs;
  for (std::size_t oi = 0; oi < info_.size(); ++oi) {
    if (info_[oi].o->kind == ObservationKind::Utterance) {
      std::string n = json::get_string(info_[oi].o->attrs, "node");
      if (!n.empty() && !node_obs.count(n)) node_obs[n] = oi;
    }
  }
  if (ub_.forks.is_array()) {
    for (const auto& fk : ub_.forks) {
      model::Fork f;
      f.kind = model::ForkKind::Conversation;
      f.subject = subject_;
      std::string base_node = json::get_string(fk, "node");
      if (auto it = node_obs.find(base_node); it != node_obs.end()) f.base = info_[it->second].o->id;
      f.date = json::get_string(fk, "date");
      bool any_current = false;
      if (const Json* alts = json::find(fk, "alternatives"); alts && alts->is_array()) {
        for (const auto& a : *alts) any_current = any_current || json::get_bool(a, "current");
        for (const auto& a : *alts) {
          model::ForkSide s;
          std::string node = json::get_string(a, "first_node");
          auto it = node_obs.find(node);
          if (it == node_obs.end()) continue;
          s.ref = info_[it->second].o->id;
          s.label = clip(info_[it->second].o->text, 120);
          s.chosen = json::get_bool(a, "current");
          s.abandoned = any_current && !s.chosen;
          s.date = info_[it->second].o->date;
          if (f.date.empty() || (!s.date.empty() && s.date < f.date)) f.date = s.date;
          f.sides.push_back(std::move(s));
        }
      }
      if (f.sides.size() < 2) continue;
      f.id = model::Fork::make_id(f.kind, f.subject, f.base, f.sides);
      forks_[f.id] = std::move(f);
    }
  }
  // design forks: a unit that names >= 2 branches next to fork cues
  std::vector<std::string> branches;
  std::map<std::string, std::string> label_of;
  std::size_t first_obs = SIZE_MAX;
  for (const auto& m : mentions_) {
    if (m.kind != "branch") continue;
    if (std::find(branches.begin(), branches.end(), m.entity) == branches.end()) {
      branches.push_back(m.entity);
      label_of[m.entity] = m.label;
    }
    first_obs = std::min(first_obs, m.obs);
  }
  if (branches.size() >= 2) {
    model::Fork f;
    f.kind = model::ForkKind::Design;
    f.subject = subject_;
    // base: the version in force where the fork is first discussed
    std::size_t fork_obs = first_obs;
    for (std::size_t oi = 0; oi < info_.size(); ++oi) {
      if (info_[oi].text && lex_.score("fork", info_[oi].folded) > 0) {
        fork_obs = std::min(fork_obs, oi);
        break;
      }
    }
    f.base = nearest_version(fork_obs);
    f.date = info_[fork_obs].o->date;
    for (const auto& b : branches) {
      model::ForkSide s;
      s.ref = b;
      s.label = label_of[b];
      for (const auto& [id, r] : statuses_) {
        if (r.entity == b && r.status == model::StatusValue::Abandoned) s.abandoned = true;
      }
      for (const auto& m : mentions_) {
        if (m.entity == b && item_type_[m.obs] == "decision" && !s.abandoned) s.chosen = true;
      }
      if (s.chosen && s.abandoned) s.chosen = false;
      s.date = f.date;
      f.sides.push_back(std::move(s));
      if (!f.base.empty()) {
        model::Qualifiers q;
        q.version = f.base;
        std::size_t mo = first_obs;
        for (const auto& m : mentions_) {
          if (m.entity == b) {
            mo = m.obs;
            break;
          }
        }
        claim(b, "forked_from", "", Json(f.base), q, support(mo, "", ex_name("extract.forks"), 0.7));
      }
    }
    f.id = model::Fork::make_id(f.kind, f.subject, f.base, f.sides);
    forks_[f.id] = std::move(f);
  }
}

model::PrincipleForm form_of(const Lexicons& lex, std::string_view folded) {
  for (const char* w : {"never", "always", "nigdy", "zawsze", "must not", "nie wolno", "invariant"}) {
    Phrase p = make_phrase(lex.norm, w, 1);
    if (find_phrase(folded, p, 0, nullptr) != std::string_view::npos) return model::PrincipleForm::Invariant;
  }
  for (const char* w : {"by default", "domyslnie", "default"}) {
    Phrase p = make_phrase(lex.norm, w, 1);
    if (find_phrase(folded, p, 0, nullptr) != std::string_view::npos) return model::PrincipleForm::Default;
  }
  return model::PrincipleForm::Heuristic;
}

void Run::do_normative() {
  for (std::size_t oi = 0; oi < info_.size(); ++oi) {
    const ObsInfo& in = info_[oi];
    if (!in.text) continue;
    double s = 0;
    for (const auto& h : lex_.match("normative", in.folded)) s += h.w;
    if (s < 1.0) continue;
    std::string_view t = utf8::rstrip(in.o->text);
    if (!t.empty() && t.back() == '?') continue;
    model::Principle p;
    p.statement[in.o->lang.empty() || in.o->lang == "mixed" ? "pl" : in.o->lang] = in.o->text;
    p.phrasings = {in.o->text};
    p.level = model::PrincipleLevel::Strategy;
    p.form = form_of(lex_, in.folded);
    p.evidence_for = {in.o->id};
    p.confidence = std::min(0.8, 0.25 + 0.1 * s);
    p.validation = model::ValidationStatus::Candidate;
    p.owner = in.o->speaker == "user" || in.o->speaker == "human" ? "user" : "";
    p.origin = c_.origin;
    model::Reference r;
    r.doc = c_.unit.id;
    r.date = date10(in.o->date);
    r.observation = in.o->id;
    p.sources = {r};
    std::string key = std::string(model::to_string(p.level)) + '\x1f' + std::string(model::to_string(p.form)) + '\x1f' +
                      lex_.norm.phrase_key(in.o->text);
    p.id = kb::stable_id("p_", key);
    model::Qualifiers q;
    q.lang = in.o->lang;
    claim(subject_, "states_principle", "", Json(p.id), q, support(oi, "", ex_name("extract.normative"), p.confidence));
    principles_[p.id] = std::move(p);
  }
}

void Run::do_areas() {
  // candidate project kinds for classifying members
  std::vector<std::string> kind_ids;
  if (!subject_project_kind_.empty()) kind_ids.push_back(subject_project_kind_);
  else kind_ids = pack_.ids("project_kinds");
  std::vector<model::ProjectKind> pks;
  for (const auto& id : kind_ids) {
    if (auto pk = model::project_kind(pack_, id)) pks.push_back(std::move(*pk));
  }
  for (std::size_t oi = 0; oi < info_.size(); ++oi) {
    const ObsInfo& in = info_[oi];
    if (!in.text) continue;
    auto hits = lex_.match("generalization", in.folded);
    if (hits.empty()) continue;
    const CueHit& h = hits.front();
    std::size_t colon = in.folded.find(':', h.pos + h.len);
    if (colon == std::string::npos) continue;
    std::string area_name = std::string(utf8::strip(surface_bytes(oi, h.pos + h.len, colon)));
    if (lex_.norm.tokens(area_name).size() > 5) continue;
    std::size_t list_end = in.folded.find(" - ", colon);
    if (list_end == std::string::npos) list_end = in.folded.size();
    // split the list after the colon
    std::vector<std::pair<std::size_t, std::size_t>> spans;
    std::size_t a = colon + 1;
    for (std::size_t i = colon + 1; i <= list_end; ++i) {
      if (i == list_end || in.folded[i] == ',' || in.folded[i] == ';') {
        if (i > a) spans.emplace_back(a, i);
        a = i + 1;
      }
    }
    std::string statement = std::string(utf8::strip(surface_bytes(oi, h.pos, in.folded.size())));
    if (statement.empty()) statement = in.o->text;
    model::Area area;
    area.subject = subject_;
    area.observation = in.o->id;
    area.statement = statement;
    area.id = model::Area::make_id(area.subject, area.observation, area.statement);
    // the generating statement: a candidate principle scoped to the area
    model::Principle p;
    p.statement[in.o->lang.empty() || in.o->lang == "mixed" ? "pl" : in.o->lang] = statement;
    p.phrasings = {in.o->text};
    p.level = model::PrincipleLevel::Strategy;
    p.form = form_of(lex_, in.folded);
    p.scope.areas = {area.id};
    p.evidence_for = {in.o->id};
    p.confidence = 0.5;
    p.validation = model::ValidationStatus::Candidate;
    p.owner = in.o->speaker == "user" || in.o->speaker == "human" ? "user" : "";
    p.origin = c_.origin;
    model::Reference r;
    r.doc = c_.unit.id;
    r.date = date10(in.o->date);
    r.observation = in.o->id;
    p.sources = {r};
    p.id = kb::stable_id("p_", std::string(model::to_string(p.level)) + '\x1f' + std::string(model::to_string(p.form)) +
                                   '\x1f' + lex_.norm.phrase_key(statement));
    // members (positive items) vs constraints (items that start with a negation)
    std::vector<Observation> member_obs;
    std::vector<std::string> constraints;
    for (auto [s, e] : spans) {
      std::string item = std::string(utf8::strip(surface_bytes(oi, s, e)));
      auto toks = lex_.norm.tokens(item);
      while (!toks.empty() && kConnectives.count(lex_.fold(toks.front()))) {
        // drop leading connectives ("potem", "na koncu", "then")
        std::string first = toks.front();
        std::string li = utf8::to_lower(item);
        std::size_t at = li.find(first);
        if (at == std::string::npos) break;
        item = std::string(utf8::strip(item.substr(at + first.size())));
        toks.erase(toks.begin());
      }
      if (toks.empty()) continue;
      bool negated = false;
      for (const auto& np : lex_.phrases("negated_item")) negated = negated || lex_.fold(toks.front()) == np.text;
      if (negated) {
        constraints.push_back(item);
        continue;
      }
      Observation mo = *in.o;
      mo.text = item;
      mo.kind = ObservationKind::ListItem;
      member_obs.push_back(std::move(mo));
    }
    std::vector<KindRef> kinds;
    for (const auto& pk : pks) {
      for (const auto& k : pk.domain_kinds) kinds.push_back({&k, pk.header.id});
    }
    auto classified = classify(lex_, kinds, member_obs);
    std::set<std::string> kset;
    std::set<model::Role> rset;
    for (auto& ci : classified) {
      ci.area = area.id;
      std::string ekind = "concept";
      for (const auto& kr : kinds) {
        if (kr.kind->id == ci.kind && !kr.kind->entity_kind.empty()) ekind = kr.kind->entity_kind;
      }
      std::string mid = entity(ekind, lex_.norm.phrase_key(ci.text), ci.text, ci.text, oi, "mined", 0.6);
      model::Qualifiers q;
      q.scope = area.id;
      q.lang = in.o->lang;
      if (!ci.kind.empty()) q.extra = Json{{"domain_kind", ci.kind}};
      std::string cid = claim(mid, "member_of", subject_, Json(), q, support(oi, ci.text, ex_name("extract.areas"), 0.8));
      if (!cid.empty()) area.members.push_back(cid);
      if (!ci.kind.empty()) kset.insert(ci.kind);
      if (ci.role) rset.insert(*ci.role);
      items_.push_back(ci);
    }
    for (const auto& ctext : constraints) {
      model::Qualifiers q;
      q.scope = area.id;
      q.lang = in.o->lang;
      claim(subject_, "has_invariant", "", Json(ctext), q, support(oi, ctext, ex_name("extract.areas"), 0.7));
      p.exceptions.push_back(ctext);
    }
    area.kinds.assign(kset.begin(), kset.end());
    area.roles.assign(rset.begin(), rset.end());
    area.gap = area.members.empty();
    if (op("generalizations") || op("areas")) {
      area.principle = p.id;
      principles_[p.id] = std::move(p);
    }
    if (!area_name.empty()) {
      // the area's name as a concept of the subject
      std::string nid = entity("concept", lex_.norm.phrase_key(area_name), area_name, area_name, oi, "mined", 0.6);
      (void)nid;
    }
    std::sort(area.members.begin(), area.members.end());
    area.members.erase(std::unique(area.members.begin(), area.members.end()), area.members.end());
    areas_.push_back(std::move(area));
  }
}

void Run::do_citations() {
  static const auto re = re::Regex::compile(
      "(?i)\\b(?:art\\.?|§)\\s*\\d+[a-z]?(?:\\s*(?:ust\\.?|pkt\\.?|§|par\\.?|para\\.?)\\s*\\d+[a-z]?)*");
  if (!re) return;
  for (std::size_t oi = 0; oi < info_.size(); ++oi) {
    const ObsInfo& in = info_[oi];
    if (!in.text) continue;
    std::u32string u = utf8::decode(in.o->text);
    for (const auto& m : re->finditer(u)) {
      std::string cite = m.group_utf8(0);
      std::size_t bs = utf8::byte_offset(in.o->text, static_cast<std::size_t>(m.start(0)));
      // a code abbreviation right before ("KL art. 12")
      std::string before(utf8::rstrip(in.o->text.substr(0, bs)));
      std::size_t sp = before.find_last_of(' ');
      std::string code = sp == std::string::npos ? before : before.substr(sp + 1);
      bool is_code = !code.empty() && code.size() <= 6 &&
                     std::all_of(code.begin(), code.end(), [](char c) { return std::isupper(static_cast<unsigned char>(c)) || c == '.'; });
      std::string label = is_code ? code + " " + cite : cite;
      std::string id = entity("citation", lex_.norm.phrase_key(label), label, label, oi, "mined", 0.8);
      claim(subject_, "cites", id, Json(), model::Qualifiers{}, support(oi, label, ex_name("extract.citations"), 0.8));
    }
    for (const auto& mn : mentions_) {
      if (mn.obs == oi && mn.kind == "legal_norm") {
        claim(subject_, "cites", mn.entity, Json(), model::Qualifiers{}, support(oi, mn.label, ex_name("extract.citations"), 0.8));
      }
    }
  }
}

void Run::do_dates() {
  static const std::map<std::string, int> kMonths = {
      {"stycznia", 1},  {"lutego", 2},    {"marca", 3},     {"kwietnia", 4}, {"maja", 5},       {"czerwca", 6},
      {"lipca", 7},     {"sierpnia", 8},  {"wrzesnia", 9},  {"pazdziernika", 10}, {"listopada", 11}, {"grudnia", 12},
      {"january", 1},   {"february", 2},  {"march", 3},     {"april", 4},    {"may", 5},        {"june", 6},
      {"july", 7},      {"august", 8},    {"september", 9}, {"october", 10}, {"november", 11},  {"december", 12}};
  static const auto iso = re::Regex::compile("\\b(\\d{4})-(\\d{2})-(\\d{2})\\b");
  for (std::size_t oi = 0; oi < info_.size(); ++oi) {
    const ObsInfo& in = info_[oi];
    if (!in.text) continue;
    std::vector<std::string> dates;
    if (iso) {
      for (const auto& m : iso->finditer(utf8::decode(in.o->text))) dates.push_back(m.group_utf8(0));
    }
    const auto& t = in.toks;
    std::string year = date10(in.o->date).substr(0, 4);
    for (std::size_t k = 0; k + 1 < in.otoks.size() && in.otoks.size() == t.size(); ++k) {
      const std::string& d = in.otoks[k].surface;
      if (d.empty() || d.size() > 2 || !std::all_of(d.begin(), d.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) continue;
      auto it = kMonths.find(t[k + 1].lower);
      if (it == kMonths.end() || year.size() != 4) continue;
      int day = std::atoi(d.c_str());
      if (day < 1 || day > 31) continue;
      std::string y = year;
      if (k + 2 < in.otoks.size() && in.otoks[k + 2].surface.size() == 4 && std::isdigit(static_cast<unsigned char>(in.otoks[k + 2].surface[0]))) {
        y = in.otoks[k + 2].surface;
      }
      char buf[16];
      std::snprintf(buf, sizeof buf, "%s-%02d-%02d", y.c_str(), it->second, day);
      dates.emplace_back(buf);
    }
    for (const auto& d : dates) {
      model::Qualifiers q;
      q.valid_from = d;
      claim(subject_, "has_event", "", Json{{"date", d}, {"text", clip(in.o->text, 160)}}, q,
            support(oi, "", ex_name("extract.dates"), 0.8));
    }
  }
}

void Run::do_speakers() {
  for (std::size_t oi = 0; oi < info_.size(); ++oi) {
    const Observation& o = *info_[oi].o;
    if (o.kind != ObservationKind::Utterance || o.speaker.empty()) continue;
    if (o.speaker == "user" || o.speaker == "assistant" || o.speaker == "human" || o.speaker == "system") continue;
    std::string kind = type_ == "screenplay" ? "character" : "actor";
    std::string id = entity(kind, lex_.norm.phrase_key(o.speaker), o.speaker, o.speaker, oi, "mined", 0.8);
    claim(id, "participates_in", subject_, Json(), model::Qualifiers{}, support(oi, o.speaker, ex_name("extract.speakers"), 0.9));
  }
}

void Run::do_headers() {
  for (std::size_t oi = 0; oi < info_.size(); ++oi) {
    const Observation& o = *info_[oi].o;
    if (o.kind != ObservationKind::Field) continue;
    std::string field = json::get_string(o.attrs, "field");
    std::size_t colon = o.text.find(':');
    if (colon == std::string::npos) continue;
    std::string value(utf8::strip(o.text.substr(colon + 1)));
    if (field == "from" || field == "to" || field == "cc") {
      std::size_t s = 0;
      while (s <= value.size()) {
        std::size_t c = value.find(',', s);
        if (c == std::string::npos) c = value.size();
        std::string who(utf8::strip(value.substr(s, c - s)));
        std::string name = who;
        if (std::size_t lt = who.find('<'); lt != std::string::npos) {
          name = std::string(utf8::strip(who.substr(0, lt)));
          if (name.empty()) name = who.substr(lt + 1, who.find('>') - lt - 1);
        }
        std::string quoted = name;
        if (quoted.size() >= 2 && quoted.front() == '"' && quoted.back() == '"') quoted = quoted.substr(1, quoted.size() - 2);
        if (!quoted.empty()) {
          std::string id = entity("party", lex_.norm.phrase_key(quoted), quoted, quoted, oi, "mined", 0.9);
          model::Qualifiers q;
          q.extra = Json{{"field", field}};
          claim(id, "participates_in", subject_, Json(), q, support(oi, who, ex_name("extract.headers"), 0.95));
        }
        s = c + 1;
      }
    } else if (field == "date") {
      model::Qualifiers q;
      q.valid_from = value;
      claim(subject_, "has_event", "", Json{{"date", value}, {"text", "sent"}}, q, support(oi, "", ex_name("extract.headers"), 0.95));
    }
  }
}

void Run::do_code_symbols() {
  for (std::size_t oi = 0; oi < info_.size(); ++oi) {
    const Observation& o = *info_[oi].o;
    std::vector<std::string> syms;
    if (o.kind == ObservationKind::Field && o.attrs.contains("symbol")) {
      syms.push_back(json::get_string(o.attrs, "symbol"));
    } else if (o.kind == ObservationKind::CodeBlock) {
      for (const auto& s : archive::camel_identifiers(o.text)) syms.push_back(s);
    }
    std::set<std::string> seen;
    for (const auto& s : syms) {
      if (s.empty() || !std::isupper(static_cast<unsigned char>(s[0])) || !seen.insert(s).second) continue;
      std::string words;
      for (const auto& w : archive::split_identifier(s)) words += (words.empty() ? "" : " ") + w;
      std::string id = entity("component", lex_.norm.phrase_key(words), s, s, oi, "mined", 0.9);
      claim(subject_, "has_component", id, Json(), model::Qualifiers{}, support(oi, s, ex_name("extract.code_symbols"), 0.95));
    }
  }
}

std::map<std::string, double>& timing() {
  static std::map<std::string, double> t;
  return t;
}
struct Tm {
  const char* n;
  std::chrono::steady_clock::time_point a = std::chrono::steady_clock::now();
  ~Tm() { timing()[n] += std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count(); }
};
#define TIMED(name, expr) \
  do {                    \
    Tm tm_{name};         \
    expr;                 \
  } while (0)

Extraction Run::run() {
  TIMED("prepare", prepare());
  TIMED("lexicon", find_lexicon_mentions());
  TIMED("named", find_named_mentions());
  TIMED("versions_m", find_version_mentions());
  choose_subject();
  if (op("entities_lexicon")) TIMED("ent", do_entities_lexicon());
  if (op("versions")) do_versions();
  if (op("relation_patterns")) TIMED("rel", do_relation_patterns());
  TIMED("items", do_items());  // classification feeds decisions and forks even without "items"
  if (op("decisions")) TIMED("dec", do_decisions());
  if (op("status_cues")) TIMED("status", do_status());
  if (op("forks")) do_forks();
  if (op("normative")) do_normative();
  if (op("generalizations") || op("areas")) TIMED("areas", do_areas());
  if (op("citations")) TIMED("cit", do_citations());
  if (op("dates")) TIMED("dates", do_dates());
  if (op("speakers")) do_speakers();
  if (op("headers")) do_headers();
  if (op("code_symbols")) do_code_symbols();
  if (std::getenv("LOOM_EXTRACT_TIMING")) {
    for (const auto& [k, v] : timing()) std::fprintf(stderr, "%s=%.3f ", k.c_str(), v);
    std::fprintf(stderr, "\n");
  }

  Extraction ex;
  ex.observations = obs_;
  for (auto& [id, e] : ents_) {
    Json ol = Json::array();
    for (const auto& o : ent_obs_[id]) ol.push_back(o);
    e.attrs["observations"] = ol;
    std::sort(e.aliases.begin(), e.aliases.end(), [](const model::Alias& a, const model::Alias& b) { return a.key < b.key; });
    ex.entities.push_back(e);
  }
  for (auto& [id, c] : claims_) {
    std::sort(c.assessment.support.begin(), c.assessment.support.end(),
              [](const model::Support& a, const model::Support& b) {
                return std::tie(a.observation, a.extractor) < std::tie(b.observation, b.extractor);
              });
    ex.claims.push_back(c);
  }
  std::sort(areas_.begin(), areas_.end(), [](const model::Area& a, const model::Area& b) { return a.id < b.id; });
  ex.areas = areas_;
  for (auto& [id, p] : principles_) ex.principles.push_back(p);
  for (auto& [id, d] : decisions_) ex.decisions.push_back(d);
  for (auto& [id, f] : forks_) ex.forks.push_back(f);
  std::vector<model::StatusRecord> recs;
  for (auto& [id, r] : statuses_) recs.push_back(r);
  ex.statuses = model::order_status_history(std::move(recs));
  ex.items = items_;
  std::map<std::string, int> kinds;
  for (const auto& e : ex.entities) ++kinds[e.kind];
  Json kj = Json::object();
  for (const auto& [k, n] : kinds) kj[k] = n;
  ex.stats = Json{{"unit", c_.unit.id},
                  {"subject", subject_},
                  {"subject_kind", subject_kind_},
                  {"observations", ex.observations.size()},
                  {"entities", ex.entities.size()},
                  {"entity_kinds", kj},
                  {"claims", ex.claims.size()},
                  {"areas", ex.areas.size()},
                  {"principles", ex.principles.size()},
                  {"decisions", ex.decisions.size()},
                  {"forks", ex.forks.size()},
                  {"statuses", ex.statuses.size()},
                  {"versions_unanchored", unanchored_},
                  {"gated", gated_},
                  {"names", names_}};
  return ex;
}

}  // namespace

Extraction run_extractors(const Lexicons& lex, const kb::Pack& pack, const std::string& artifact_type,
                          const std::vector<std::string>& ops, const UnitContent& content,
                          const std::vector<Observation>& observations) {
  Run r(lex, pack, artifact_type, ops, content, observations);
  return r.run();
}

std::vector<ClassifiedItem> classify(const Lexicons& lex, const std::vector<KindRef>& kinds,
                                     const std::vector<Observation>& items) {
  std::vector<ClassifiedItem> out;
  for (const auto& o : items) {
    ClassifiedItem ci;
    ci.observation = o.id;
    ci.text = o.text;
    std::string folded = lex.fold(o.text);
    std::string key = " " + lex.norm.phrase_key(o.text, true) + " ";
    double best = 0;
    for (const auto& kr : kinds) {
      const model::DomainKind& k = *kr.kind;
      double s = 0;
      if (const Json* terms = json::find(k.anchors, "terms"); terms && terms->is_object()) {
        for (auto it = terms->begin(); it != terms->end(); ++it) {
          if (!it.value().is_array()) continue;
          for (const auto& t : it.value()) {
            if (!t.is_string()) continue;
            std::string tk = lex.norm.phrase_key(t.get<std::string>(), true);
            if (!tk.empty() && key.find(" " + tk + " ") != std::string::npos) s += 1.0;
          }
        }
      }
      if (const Json* lx = json::find(k.anchors, "lexicon"); lx && lx->is_array()) {
        for (const auto& cls : *lx) {
          if (!cls.is_string()) continue;
          std::string c = cls.get<std::string>();
          for (const auto& [fk, forms] : lex.forms) {
            if (key.find(" " + fk + " ") == std::string::npos) continue;
            for (const auto& f : forms) {
              const LexEntry& e = lex.entries[f.entry];
              if (e.cls == c || e.cls.rfind(c + ".", 0) == 0) s += 1.5;
            }
          }
        }
      }
      if (const Json* cs = json::find(k.anchors, "cues"); cs && cs->is_array()) {
        for (const auto& cls : *cs) {
          if (cls.is_string() && lex.score(cls.get<std::string>(), folded) > 0) s += 1.0;
        }
      }
      if (s > best) {
        best = s;
        ci.kind = k.id;
        ci.role = k.role;
      }
    }
    ci.score = best > 0 ? best / (best + 1.0) : 0.0;
    out.push_back(std::move(ci));
  }
  return out;
}

}  // namespace loom::extract::detail
