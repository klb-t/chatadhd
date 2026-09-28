// Project attribution by cosine similarity (resolve.h): the vector layer
// (TF-IDF default, pluggable multimodal embedder with a content-hash cache),
// multi-label shared foundations, and the before/after measurement on the
// fictional synthetic_dev corpus: attribution accuracy for units that never
// name their project, alias recall, and the share of claims attached to a
// project.
#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

#include "loom/extract.h"
#include "loom/kb.h"
#include "loom/resolve.h"
#include "loom/selector.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {

std::shared_ptr<const kb::Pack> pack() {
  static auto p = unwrap(kb::Pack::load_builtin());
  return p;
}

bool verbose() {
  const char* v = std::getenv("LOOM_EXTRACT_EVAL_VERBOSE");
  return v && *v && std::string(v) != "0";
}

struct Corpus {
  std::vector<extract::UnitContent> units;
  extract::Extraction ex;
  resolve::ResolveResult res;
  std::vector<model::Claim> claims;
  resolve::AttributionResult attr;
  Json gt;
  std::map<std::string, std::string> unit_conv;     // unit id -> conv ext id
  std::map<std::string, std::string> conv_project;  // GT conv -> GT project
  std::map<std::string, std::string> obs_unit;

  Corpus() {
    fs::path dir = fs::path(LOOM_TEST_FIXTURES) / "eval" / "synthetic_dev";
    for (const char* z : {"chatgpt_export.zip", "claude_export.zip"}) {
      for (auto& u : unwrap(extract::read_units(dir / z))) units.push_back(std::move(u));
    }
    for (const auto& u : units) unit_conv[u.unit.id] = json::get_string(u.unit.attrs, "ext_id");
    ex = extract::extract_units(pack(), units);
    for (const auto& o : ex.observations) obs_unit[o.id] = o.unit;
    resolve::Resolver r(pack());
    res = unwrap(r.resolve(ex.entities, ex.observations));
    claims = r.apply_remap(ex.claims, res);
    auto space = resolve::make_tfidf_space(pack());
    auto cfg = resolve::AttributionConfig::from_policy(*pack());
    // exploration only: LOOM_ATTRIBUTION_CFG='{"min_cosine":0.1,...}'
    if (const char* o = std::getenv("LOOM_ATTRIBUTION_CFG")) {
      Json j = json::parse_or(o, Json::object());
      cfg.min_cosine = json::get_number(j, "min_cosine", cfg.min_cosine);
      cfg.margin = json::get_number(j, "margin", cfg.margin);
      cfg.tau = json::get_number(j, "tau", cfg.tau);
      cfg.tau_confident = json::get_number(j, "tau_confident", cfg.tau_confident);
      cfg.ambiguity = json::get_number(j, "ambiguity", cfg.ambiguity);
      cfg.w_cosine = json::get_number(j, "w_cosine", cfg.w_cosine);
      cfg.w_context = json::get_number(j, "w_context", cfg.w_context);
    }
    attr = unwrap(resolve::attribute_units(*pack(), res.entities, claims, ex.observations, *space, cfg));
    gt = unwrap(json::parse(unwrap(fsutil::read_file(dir / "ground_truth.json"))));
    for (const auto& u : gt["units"]["relevant"]) conv_project[u["conv_id"]] = json::get_string(u, "project");
  }
};

Corpus& corpus() {
  static Corpus c;
  return c;
}

// GT project id -> best matching entity (alias key overlap), and alias P/R.
struct AliasEval {
  std::map<std::string, std::string> gt_entity;
  double precision = 0, recall = 0;
};
AliasEval alias_eval(const std::vector<model::Entity>& ents, const Json& gt) {
  kb::Normalizer norm(*pack());
  AliasEval out;
  int tp = 0, pred = 0, total = 0;
  std::set<std::string> used;
  for (const auto& p : gt["projects"]) {
    std::set<std::string> gk;
    for (const auto& a : p["aliases"]) gk.insert(norm.phrase_key(a.get<std::string>()));
    gk.insert(norm.phrase_key(p["name"].get<std::string>()));
    total += static_cast<int>(gk.size());
    const model::Entity* best = nullptr;
    int bo = 0;
    for (const auto& e : ents) {
      if (e.kind != "project" || e.status != model::ClaimStatus::Active) continue;
      int ov = 0;
      for (const auto& a : e.aliases) ov += gk.count(a.key) ? 1 : 0;
      if (ov > bo) {
        bo = ov;
        best = &e;
      }
    }
    if (!best || used.count(best->id)) continue;
    used.insert(best->id);
    out.gt_entity[p["id"]] = best->id;
    std::set<std::string> pk;
    for (const auto& a : best->aliases) pk.insert(a.key);
    pred += static_cast<int>(pk.size());
    for (const auto& k : pk) tp += gk.count(k) ? 1 : 0;
  }
  out.precision = pred ? static_cast<double>(tp) / pred : 0;
  out.recall = total ? static_cast<double>(tp) / total : 0;
  return out;
}

double f1(double p, double r) { return p + r > 0 ? 2 * p * r / (p + r) : 0.0; }

// A deterministic fake embedder: bag of lowercase letters (26 dims).
class LetterEmbedder : public resolve::MultimodalEmbedder {
 public:
  int calls = 0;
  std::string model_id() const override { return "fake-letters-1"; }
  std::vector<std::string> modalities() const override { return {"text", "image"}; }
  Result<std::vector<std::vector<float>>> embed(const std::vector<resolve::EmbedInput>& in) override {
    ++calls;
    std::vector<std::vector<float>> out;
    for (const auto& x : in) {
      std::vector<float> v(26, 0.0f);
      for (char c : x.text + x.blob) {
        if (c >= 'a' && c <= 'z') v[static_cast<std::size_t>(c - 'a')] += 1.0f;
      }
      out.push_back(v);
    }
    return out;
  }
};

class TextOnly : public EmbeddingProvider {
 public:
  std::string model_id() const override { return "text-only"; }
  Result<std::vector<std::vector<float>>> embed(const std::vector<std::string>& texts) override {
    std::vector<std::vector<float>> out;
    for (const auto& t : texts) out.push_back({static_cast<float>(t.size()), 1.0f});
    return out;
  }
};

}  // namespace

TEST_SUITE("resolve_attribution") {
  TEST_CASE("vector layer: TF-IDF is PL+EN aware; embedder plugs in with modalities and a hash+model cache") {
    auto tf = resolve::make_tfidf_space(pack());
    std::vector<resolve::EmbedInput> corpus{{"a", "text", "graf wiedzy i pamięć rozmów", "", ""},
                                            {"b", "text", "knowledge graph and conversation memory", "", ""},
                                            {"c", "text", "przepis na sernik", "", ""}};
    LOOM_REQUIRE_OK(tf->fit(corpus));
    auto v = unwrap(tf->vectors(corpus));
    CHECK(tf->method() == "tfidf");
    CHECK(resolve::cosine(v[0], v[1]) > resolve::cosine(v[0], v[2]));

    auto emb = std::make_shared<LetterEmbedder>();
    auto cache = std::make_shared<resolve::MemoryEmbeddingCache>();
    auto es = resolve::make_embedding_space(emb, cache);
    CHECK(es->method() == "embedding:fake-letters-1");
    std::vector<resolve::EmbedInput> mm{{"t", "text", "abc", "", ""}, {"i", "image", "", "img://abc", ""},
                                        {"a", "audio", "", "snd://x", ""}};
    auto v1 = unwrap(es->vectors(mm));
    CHECK(!v1[0].empty());
    CHECK(!v1[1].empty());
    CHECK(v1[2].empty());  // modality the embedder lacks: no vector, never a guess
    CHECK(cache->size() == 2);
    auto v2 = unwrap(es->vectors(mm));
    CHECK(emb->calls == 1);  // second time from the cache (content hash + model id)
    CHECK(resolve::cosine(v1[0], v2[0]) == doctest::Approx(1.0));
    auto restored = resolve::MemoryEmbeddingCache::from_json(cache->to_json());
    CHECK(restored.size() == 2);
    // a text-only EmbeddingProvider (selector.h) plugs in through the adapter
    auto ts = resolve::make_embedding_space(resolve::embedder_from_text_provider(std::make_shared<TextOnly>()));
    CHECK(ts->modalities() == std::vector<std::string>{"text"});
  }

  TEST_CASE("synthetic_dev: attribution of units that never name their project, before vs after") {
    auto& C = corpus();
    // before: every unnamed unit keeps its document subject
    std::map<std::string, std::string> doc_unit;  // document entity -> unit
    for (const auto& e : C.res.entities) {
      if (e.kind == "document" && e.canonical_key.rfind("unit ", 0) == 0) doc_unit[e.id] = e.canonical_key.substr(5);
    }
    auto before = alias_eval(C.res.entities, C.gt);
    std::vector<model::Entity> after_ents = C.res.entities;
    for (const auto& u : C.attr.updated_entities) {
      for (auto& e : after_ents) {
        if (e.id == u.id) e = u;
      }
    }
    auto after = alias_eval(after_ents, C.gt);
    // unnamed relevant units and their GT project
    int unnamed = 0, top_ok = 0, label_ok = 0, ambiguous = 0;
    std::map<std::string, const resolve::UnitAttribution*> by_unit;
    for (const auto& ua : C.attr.units) by_unit[ua.unit] = &ua;
    for (const auto& [did, uid] : doc_unit) {
      auto conv = C.unit_conv[uid];
      auto it = C.conv_project.find(conv);
      if (it == C.conv_project.end() || it->second.empty()) continue;
      ++unnamed;
      auto gi = after.gt_entity.find(it->second);
      std::string want = gi == after.gt_entity.end() ? "" : gi->second;
      auto ua = by_unit.find(uid);
      if (verbose() && ua != by_unit.end()) {
        std::string line = conv + " GT=" + it->second + " :";
        for (const auto& c : ua->second->candidates) {
          std::string lab = c.target;
          for (const auto& e : after_ents) {
            if (e.id == c.target) lab = e.label;
          }
          line += " " + lab + "=" + json::format_float_py(std::round(c.score * 100) / 100) + "(" +
                  json::format_float_py(std::round(c.cosine * 100) / 100) + "/" + json::format_float_py(std::round(c.context * 100) / 100) + ")";
        }
        MESSAGE(line);
      }
      if (ua == by_unit.end() || ua->second->chosen.empty()) {
        if (verbose()) MESSAGE("unattributed " << conv);
        continue;
      }
      ambiguous += ua->second->chosen.size() > 1;
      if (ua->second->chosen.front() == want) ++top_ok;
      if (std::find(ua->second->chosen.begin(), ua->second->chosen.end(), want) != ua->second->chosen.end()) ++label_ok;
      else if (verbose()) MESSAGE("wrong " << conv << " -> " << ua->second->to_json().dump());
    }
    // claims of relevant units attached to a project (subject a project, or an inferred re-subjected copy)
    std::set<std::string> project_ids;
    for (const auto& e : after_ents) {
      if (e.kind == "project") project_ids.insert(e.id);
    }
    std::set<std::string> copied;
    for (const auto& c : C.attr.resubjected) copied.insert(c.assessment.premises.claims.front());
    int total = 0, attached_before = 0, attached_after = 0;
    for (const auto& c : C.claims) {
      if (c.predicate == "mentioned_in" || c.assessment.status != model::ClaimStatus::Active) continue;
      bool relevant = false;
      for (const auto& s : c.assessment.support) {
        auto u = C.obs_unit.find(s.observation);
        if (u != C.obs_unit.end() && C.conv_project.count(C.unit_conv[u->second]) &&
            !C.conv_project[C.unit_conv[u->second]].empty()) {
          relevant = true;
        }
      }
      if (!relevant) continue;
      ++total;
      bool b = project_ids.count(c.subject) > 0;
      attached_before += b;
      attached_after += b || copied.count(c.id);
    }
    // noise: unrelated conversations (generic chatter, lexical traps) must stay unattributed
    std::set<std::string> noise;
    for (const char* k : {"noise_generic", "noise_traps"}) {
      for (const auto& u : C.gt["units"][k]) noise.insert(u["conv_id"]);
    }
    int noise_total = 0, noise_attributed = 0;
    for (const auto& ua : C.attr.units) {
      if (!noise.count(C.unit_conv[ua.unit])) continue;
      ++noise_total;
      if (!ua.chosen.empty()) {
        ++noise_attributed;
        if (verbose()) MESSAGE("noise attributed " << C.unit_conv[ua.unit]);
      }
    }
    MESSAGE("noise conversations attributed to a project: " << noise_attributed << "/" << noise_total);
    CHECK(noise_attributed * 3 <= noise_total);
    double acc = unnamed ? static_cast<double>(top_ok) / unnamed : 0;
    double lab = unnamed ? static_cast<double>(label_ok) / unnamed : 0;
    MESSAGE("method " << C.attr.method << ", stats " << C.attr.stats.dump());
    MESSAGE("unnamed relevant units = " << unnamed << ": attribution accuracy before 0 -> after " << acc
                                        << " (top-1), " << lab << " (GT among chosen), ambiguous " << ambiguous);
    MESSAGE("alias recall before " << before.recall << " -> after " << after.recall << " (precision " << before.precision
                                   << " -> " << after.precision << ", F1 " << f1(before.precision, before.recall) << " -> "
                                   << f1(after.precision, after.recall) << ")");
    MESSAGE("claims attached to a project: before " << static_cast<double>(attached_before) / total << " -> after "
                                                     << static_cast<double>(attached_after) / total << "  (" << total
                                                     << " claims of relevant units)");
    CHECK(unnamed > 0);
    CHECK(acc >= 0.5);
    CHECK(after.recall >= before.recall);
    CHECK(attached_after > attached_before);
    // every about claim is inferred, system, with an expected property and alternatives recorded
    for (const auto& c : C.attr.claims) {
      INFO(c.to_json().dump());
      CHECK(c.validate());
      if (c.predicate == "about") {
        CHECK(c.assessment.evidence == model::EvidenceClass::Inferred);
        CHECK(c.assessment.origin == model::Origin::System);
        CHECK(c.assessment.expected);
        CHECK(json::get_string(c.qualifiers.extra, "method") == "tfidf");
      }
    }
    for (const auto& c : C.attr.resubjected) {
      CHECK(c.validate());
      CHECK(!json::get_string(c.qualifiers.extra, "original_subject").empty());
    }
  }

  TEST_CASE("multi-label: a shared foundation attaches to every project that uses it; deterministic") {
    kb::Normalizer norm(*pack());
    auto mk_ent = [&](const std::string& kind, const std::string& label) {
      model::Entity e;
      e.kind = kind;
      e.label = label;
      e.canonical_key = norm.phrase_key(label);
      e.id = model::Entity::make_id(kind, e.canonical_key);
      model::Alias a;
      a.key = e.canonical_key;
      a.surface = label;
      e.aliases.push_back(a);
      return e;
    };
    auto alpha = mk_ent("project", "Alpha"), beta = mk_ent("project", "Beta"), prov = mk_ent("component", "ProviderRegistry");
    std::vector<model::Entity> ents{alpha, beta, prov};
    std::vector<model::Observation> obs;
    std::vector<model::Claim> claims;
    auto unit = [&](const std::string& uid, const std::string& text, const std::vector<std::string>& mentions, bool named) {
      model::Observation o;
      o.unit = uid;
      o.text = text;
      o.date = "2025-01-01";
      o.id = "ob_" + uid;
      obs.push_back(o);
      if (!named) {
        auto d = mk_ent("document", "doc " + uid);
        d.canonical_key = "unit " + uid;
        d.id = model::Entity::make_id("document", d.canonical_key);
        ents.push_back(d);
        model::Claim c;
        c.subject = d.id;
        c.predicate = "has_decision";
        c.value = text;
        model::Support s;
        s.observation = o.id;
        s.extractor = "extract.items@1";
        c.assessment.support.push_back(s);
        c.assessment.confidence = 0.6;
        c.id = model::Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
        claims.push_back(c);
      }
      for (const auto& m : mentions) {
        model::Claim c;
        c.subject = m;
        c.predicate = "mentioned_in";
        c.value = uid;
        model::Support s;
        s.observation = o.id;
        s.extractor = "extract.alias@1";
        c.assessment.support.push_back(s);
        c.id = model::Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
        claims.push_back(c);
      }
    };
    unit("u1", "Alpha app: ProviderRegistry adapters for speech and OCR, capability routing", {alpha.id, prov.id}, true);
    unit("u2", "Beta pipeline: ProviderRegistry adapters for video generation, capability routing", {beta.id, prov.id}, true);
    unit("u3", "Alpha notes screen, offline notes sync and note history", {alpha.id}, true);
    unit("u4", "new capability adapter behind the ProviderRegistry, routing by capability", {prov.id}, false);
    unit("u5", "notes screen history and offline notes sync again", {}, false);
    auto space = resolve::make_tfidf_space(pack());
    resolve::AttributionConfig cfg;
    auto r = unwrap(resolve::attribute_units(*pack(), ents, claims, obs, *space, cfg));
    REQUIRE(r.foundations.size() == 1);
    CHECK(r.foundations[0].label == "ProviderRegistry");
    int uses = 0;
    for (const auto& c : r.claims) uses += c.predicate == "uses_foundation";
    CHECK(uses == 2);  // Alpha and Beta both use it
    std::map<std::string, std::vector<std::string>> chosen;
    for (const auto& ua : r.units) chosen[ua.unit] = ua.chosen;
    // u4 is about the shared foundation (so about both projects), not forced onto one
    REQUIRE(!chosen["u4"].empty());
    CHECK(std::find(chosen["u4"].begin(), chosen["u4"].end(), r.foundations[0].id) != chosen["u4"].end());
    // u5 is about Alpha, and its claim is re-subjected (inferred) keeping the original subject
    REQUIRE(chosen["u5"].size() == 1);
    CHECK(chosen["u5"][0] == alpha.id);
    REQUIRE(r.resubjected.size() == 1);
    CHECK(r.resubjected[0].subject == alpha.id);
    CHECK(r.resubjected[0].assessment.evidence == model::EvidenceClass::Inferred);
    // determinism
    auto space2 = resolve::make_tfidf_space(pack());
    auto r2 = unwrap(resolve::attribute_units(*pack(), ents, claims, obs, *space2, cfg));
    CHECK(json::canonical(r.to_json()) == json::canonical(r2.to_json()));
  }
}
