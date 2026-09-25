// OWNER: wave 2 semantic. Exact port of core/semantic.py.
#include "loom/semantic_analyzer.h"

#include "loom/log.h"
#include "loom/re/regex.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"

namespace loom {
namespace {
#include "builtin_rules.json.inc"

unsigned flags_from_json(const Json& arr) {
  unsigned f = 0;
  if (!arr.is_array()) return f;
  for (const auto& v : arr) {
    if (!v.is_string()) continue;
    const auto& s = v.get_ref<const std::string&>();
    if (s == "IGNORECASE") f |= re::kIgnoreCase;
    if (s == "MULTILINE") f |= re::kMultiline;
    if (s == "DOTALL") f |= re::kDotAll;
  }
  return f;
}

Json flags_to_json(unsigned f) {
  Json a = Json::array();
  if (f & re::kIgnoreCase) a.push_back("IGNORECASE");
  if (f & re::kMultiline) a.push_back("MULTILINE");
  if (f & re::kDotAll) a.push_back("DOTALL");
  return a;
}
}  // namespace

Json ExtractedEntity::to_json() const {
  return Json{{"text", text},   {"entity_type", entity_type}, {"confidence", confidence},
              {"start", start}, {"end", end},                 {"metadata", metadata}};
}

Json ExtractedRelation::to_json() const {
  return Json{{"subject", subject},       {"predicate", predicate},     {"obj", obj},
              {"confidence", confidence}, {"source_text", source_text}};
}

Json Analysis::to_json() const {
  Json e = Json::array();
  for (const auto& x : entities) e.push_back(x.to_json());
  Json r = Json::array();
  for (const auto& x : relations) r.push_back(x.to_json());
  return Json{{"entities", e}, {"topics", topics}, {"relations", r}, {"timestamp", timestamp}};
}

const AnalyzerRules& AnalyzerRules::builtin() {
  static const AnalyzerRules kRules = [] {
    auto parsed = json::parse(kBuiltinRulesJson);
    if (!parsed) {
      log::error("loom.semantic", "embedded analyzer rules are invalid JSON");
      return AnalyzerRules{};
    }
    auto r = AnalyzerRules::from_json(*parsed);
    if (!r) {
      log::error("loom.semantic", "embedded analyzer rules rejected: {}", r.error().message);
      return AnalyzerRules{};
    }
    return std::move(r).value();
  }();
  return kRules;
}

Result<AnalyzerRules> AnalyzerRules::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "analyzer rules must be an object");
  AnalyzerRules r;
  if (const Json* ep = json::find(j, "entity_patterns"); ep && ep->is_array()) {
    for (const auto& p : *ep) {
      EntityPattern e;
      e.entity_type = json::get_string(p, "entity_type");
      e.pattern = json::get_string(p, "pattern");
      if (e.entity_type.empty() || e.pattern.empty()) return Error(Errc::InvalidArgument, "entity pattern incomplete");
      if (const Json* f = json::find(p, "flags")) e.flags = flags_from_json(*f);
      r.entity_patterns.push_back(std::move(e));
    }
  }
  if (const Json* tp = json::find(j, "topics"); tp && tp->is_array()) {
    for (const auto& t : *tp) {
      std::string name = json::get_string(t, "topic");
      if (name.empty()) return Error(Errc::InvalidArgument, "topic without a name");
      std::vector<std::string> kws;
      if (const Json* k = json::find(t, "keywords"); k && k->is_array()) {
        for (const auto& kw : *k) {
          if (kw.is_string()) kws.push_back(kw.get<std::string>());
        }
      }
      r.topics.emplace_back(std::move(name), std::move(kws));
    }
  }
  if (const Json* rp = json::find(j, "relation_patterns"); rp && rp->is_array()) {
    for (const auto& p : *rp) {
      RelationPattern x;
      x.predicate = json::get_string(p, "predicate");
      x.pattern = json::get_string(p, "pattern");
      if (x.predicate.empty() || x.pattern.empty()) return Error(Errc::InvalidArgument, "relation pattern incomplete");
      if (const Json* f = json::find(p, "flags")) x.flags = flags_from_json(*f);
      x.confidence = json::get_number(p, "confidence", 0.5);
      r.relation_patterns.push_back(std::move(x));
    }
  }
  return r;
}

Json AnalyzerRules::to_json() const {
  Json ep = Json::array();
  for (const auto& e : entity_patterns) {
    ep.push_back(Json{{"entity_type", e.entity_type}, {"pattern", e.pattern}, {"flags", flags_to_json(e.flags)}});
  }
  Json tp = Json::array();
  for (const auto& [t, kws] : topics) tp.push_back(Json{{"topic", t}, {"keywords", kws}});
  Json rp = Json::array();
  for (const auto& x : relation_patterns) {
    rp.push_back(Json{{"predicate", x.predicate},
                      {"pattern", x.pattern},
                      {"flags", flags_to_json(x.flags)},
                      {"confidence", x.confidence}});
  }
  return Json{{"entity_patterns", ep}, {"topics", tp}, {"relation_patterns", rp}};
}

struct CompiledEntityPattern {
  std::string entity_type;
  re::Regex regex;
};
struct CompiledRelationPattern {
  std::string predicate;
  re::Regex regex;
  double confidence;
};

struct SemanticAnalyzer::Impl {
  AnalyzerRules rules;
  std::vector<CompiledEntityPattern> entity_patterns;
  std::vector<CompiledRelationPattern> relation_patterns;
};

SemanticAnalyzer::SemanticAnalyzer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SemanticAnalyzer::~SemanticAnalyzer() = default;

Result<std::unique_ptr<SemanticAnalyzer>> SemanticAnalyzer::create(const AnalyzerRules& rules) {
  auto impl = std::make_unique<Impl>();
  impl->rules = rules;
  for (const auto& p : rules.entity_patterns) {
    auto rx = re::Regex::compile(p.pattern, p.flags);
    if (!rx) return Error(Errc::Parse, "entity pattern '" + p.entity_type + "': " + rx.error().message);
    impl->entity_patterns.push_back(CompiledEntityPattern{p.entity_type, std::move(rx).value()});
  }
  for (const auto& p : rules.relation_patterns) {
    auto rx = re::Regex::compile(p.pattern, p.flags);
    if (!rx) return Error(Errc::Parse, "relation pattern '" + p.predicate + "': " + rx.error().message);
    impl->relation_patterns.push_back(CompiledRelationPattern{p.predicate, std::move(rx).value(), p.confidence});
  }
  return std::make_unique<SemanticAnalyzer>(std::move(impl));
}

const AnalyzerRules& SemanticAnalyzer::rules() const noexcept { return impl_->rules; }

std::vector<ExtractedEntity> SemanticAnalyzer::extract_entities(std::string_view text) const {
  std::vector<ExtractedEntity> out;
  std::vector<std::pair<std::string, std::string>> seen;  // (type, value.lower())
  std::u32string text32 = utf8::decode(text);

  auto contains_seen = [&](const std::string& type, const std::string& value_lower) {
    for (const auto& s : seen) {
      if (s.first == type && s.second == value_lower) return true;
    }
    return false;
  };

  for (const auto& ep : impl_->entity_patterns) {
    for (const auto& m : ep.regex.finditer(text32)) {
      std::u32string_view raw = m.lastindex() ? m.group(1) : m.group(0);
      std::string value = utf8::encode(raw);
      value = std::string(utf8::strip(value));
      std::string value_lower = utf8::to_lower(value);
      if (contains_seen(ep.entity_type, value_lower)) continue;
      seen.emplace_back(ep.entity_type, value_lower);
      ExtractedEntity e;
      e.text = value;
      e.entity_type = ep.entity_type;
      e.confidence = 1.0;
      e.start = static_cast<std::size_t>(m.start(0));
      e.end = static_cast<std::size_t>(m.end(0));
      out.push_back(std::move(e));
    }
  }
  return out;
}

std::vector<std::string> SemanticAnalyzer::extract_topics(std::string_view text, int threshold) const {
  std::vector<std::string> out;
  std::string text_lower = utf8::to_lower(text);
  for (const auto& [topic, keywords] : impl_->rules.topics) {
    int hits = 0;
    for (const auto& kw : keywords) {
      std::string kw_lower = utf8::to_lower(kw);
      if (!kw_lower.empty() && text_lower.find(kw_lower) != std::string::npos) hits++;
    }
    if (hits >= threshold) out.push_back(topic);
  }
  return out;
}

std::vector<ExtractedRelation> SemanticAnalyzer::extract_relations(std::string_view text) const {
  std::vector<ExtractedRelation> out;
  std::u32string text32 = utf8::decode(text);
  for (const auto& rp : impl_->relation_patterns) {
    for (const auto& m : rp.regex.finditer(text32)) {
      ExtractedRelation r;
      r.subject = std::string(utf8::strip(m.group_utf8(1)));
      r.predicate = rp.predicate;
      r.obj = std::string(utf8::strip(m.group_utf8(2)));
      r.confidence = rp.confidence;
      r.source_text = m.group_utf8(0);
      out.push_back(std::move(r));
    }
  }
  return out;
}

Analysis SemanticAnalyzer::analyse(std::string_view text) const {
  Analysis a;
  a.entities = extract_entities(text);
  a.topics = extract_topics(text);
  a.relations = extract_relations(text);
  a.timestamp = timeutil::utc_now_iso();
  return a;
}

Json SemanticAnalyzer::to_unified(const Analysis& a) {
  Json ents = Json::array();
  for (const auto& e : a.entities) {
    ents.push_back(Json{{"name", e.text}, {"kind", e.entity_type}, {"relevance", e.confidence}});
  }
  Json tops = Json::array();
  for (const auto& t : a.topics) tops.push_back(Json{{"label", t}, {"confidence", 0.5}});
  Json rels = Json::array();
  for (const auto& r : a.relations) {
    rels.push_back(Json{{"subject", r.subject}, {"predicate", r.predicate}, {"object", r.obj}});
  }
  return Json{{"entities", ents}, {"topics", tops},        {"relations", rels},
              {"summary", ""},    {"sentiment", "neutral"}, {"source", "regex"}};
}

}  // namespace loom
