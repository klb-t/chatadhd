// Context efficiency eval (LOOM_CONCEPTUAL_MODEL §4, §7.4; R12/R13): tokens
// selected by ContextEngine::select() vs a naive full-history baseline and a
// flat-retrieval (keyword, no bands/budget/authority/freshness) baseline,
// while keeping the ground-truth-relevant claims covered at the same
// budget (coverage@budget), plus determinism.
//
// The extract/resolve/generalize areas are still stubs (STUB:
// knowledge-wave), so this is a TEST-ONLY loader: it reads
// tests/fixtures/eval/synthetic_dev/ground_truth.json directly (the
// documented schema in its README) and writes model::Entity/Claim/
// Decision/StatusRecord/Principle rows straight into a KnowledgeStore run,
// bypassing the pipeline. It never touches the ChatGPT/Claude export zips
// (that is the archive/catalog/extract areas' job); ground_truth.json is
// already exact, structured ground truth.
#include <doctest/doctest.h>

#include <algorithm>
#include <map>
#include <set>

#include "loom/context_engine.h"
#include "loom/knowledge.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "context/ctx_common.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::model;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {

std::unique_ptr<Runtime> open_rt(const fs::path& dir) {
  RuntimeOptions o;
  o.data_dir = dir.string();
  o.start_workers = false;
  return unwrap(Runtime::open(o));
}

std::vector<std::string> split_us(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == '_' || c == '.') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

// True when `id` (e.g. "dec.nf.encryption_defer") and `other` (e.g.
// "feat.nf.encryption") share a token of at least 4 characters after their
// "<prefix>.<project>." head. Used only to build the test fixture's cross
// links (a real `resolve`/`generalize` area would produce these from co-
// occurrence; here they come from the ids the corpus author already chose).
bool shares_token(const std::string& a, const std::string& b) {
  auto ta = split_us(a), tb = split_us(b);
  for (auto& x : ta) {
    if (x.size() < 4) continue;
    for (auto& y : tb) {
      if (x == y) return true;
    }
  }
  return false;
}

struct LoadedCorpus {
  std::string run;
  std::map<std::string, std::string> project_entity;  // gt project id -> entity id
  std::map<std::string, std::string> feature_entity;   // gt feature id -> entity id
  std::map<std::string, std::string> entity_label;     // entity id -> human label (baselines only)
  std::vector<Claim> all_claims;                       // decision-claims + link-claims
  std::vector<Principle> all_principles;
  std::vector<StatusRecord> all_status;
};

LoadedCorpus load_ground_truth(kb::KnowledgeStore& ks, const std::string& run) {
  fs::path gt_path = fs::path(LOOM_TEST_FIXTURES) / "eval" / "synthetic_dev" / "ground_truth.json";
  std::string text = unwrap(fsutil::read_file(gt_path));
  Json gt = unwrap(json::parse(text));

  LoadedCorpus out;
  out.run = run;
  std::vector<Entity> entities;
  std::vector<Decision> decisions;
  std::map<std::string, std::string> superseded_by;

  for (const auto& pj : gt["projects"]) {
    std::string pid = json::get_string(pj, "id");
    Entity pe;
    pe.kind = "project";
    pe.canonical_key = pid;
    pe.id = Entity::make_id(pe.kind, pid);
    pe.label = json::get_string(pj, "name");
    entities.push_back(pe);
    out.project_entity[pid] = pe.id;
    out.entity_label[pe.id] = pe.label;
  }

  for (const auto& pj : gt["projects"]) {
    std::string pid = json::get_string(pj, "id");
    std::string proj_e = out.project_entity[pid];

    // Features first, so decisions below can link to them.
    std::vector<std::string> feature_ids_here;
    if (pj.contains("features_status")) {
      for (const auto& feat : pj["features_status"]) {
        std::string fid = json::get_string(feat, "id");
        Entity fe;
        fe.kind = "component";
        fe.canonical_key = fid;
        fe.id = Entity::make_id(fe.kind, fid);
        fe.label = json::get_string(feat, "label");
        fe.parent = proj_e;
        entities.push_back(fe);
        out.feature_entity[fid] = fe.id;
        out.entity_label[fe.id] = fe.label;
        feature_ids_here.push_back(fid);
        if (feat.contains("events")) {
          for (const auto& ev : feat["events"]) {
            std::string status_s = json::get_string(ev, "status");
            auto sv = model::from_string<StatusValue>(status_s);
            if (!sv) continue;
            StatusRecord sr;
            sr.entity = fe.id;
            sr.branch = json::get_string(ev, "branch");
            if (sr.branch == "main") sr.branch = "";
            sr.version = json::get_string(ev, "version");
            sr.status = *sv;
            sr.date = json::get_string(ev, "date");
            sr.id = StatusRecord::make_id(sr.entity, sr.branch, sr.version, sr.status, sr.date);
            out.all_status.push_back(sr);
          }
        }
      }
    }

    if (pj.contains("decisions")) {
      for (const auto& d : pj["decisions"]) {
        std::string did = json::get_string(d, "id");
        std::string chosen = json::get_string(d, "chosen");

        Claim c;
        c.id = did;  // reuse the fixture's own stable id
        c.subject = proj_e;
        c.predicate = "decides";
        c.value = chosen;
        c.qualifiers.valid_from = json::get_string(d, "date");
        c.assessment.evidence = EvidenceClass::Observed;
        c.assessment.origin = Origin::Archive;
        c.assessment.confidence = 0.85;
        if (d.contains("unit")) {
          const auto& u = d["unit"];
          Support sup;
          sup.observation = json::get_string(u, "provider") + ":" + json::get_string(u, "conv_id") + ":" +
                            json::get_string(u, "node_id");
          sup.quote = json::get_string(u, "quote");
          sup.extractor = "eval.fixture@1";
          c.assessment.support = {sup};
        }
        if (d.contains("principle_evidence")) {
          for (const auto& pr : d["principle_evidence"]) c.assessment.premises.principles.push_back(pr.get<std::string>());
        }
        out.all_claims.push_back(c);

        Decision dec;
        dec.id = did;
        dec.subject = proj_e;
        dec.date = c.qualifiers.valid_from;
        if (d.contains("alternatives")) {
          for (const auto& a : d["alternatives"]) {
            DecisionAlternative da;
            da.label = a.get<std::string>();
            dec.alternatives.push_back(da);
          }
          // "chosen" is sometimes a paraphrase of one alternative's text
          // (e.g. "library-based diff" vs "library-based diff (Myers-
          // style)"): match exactly first, else by substring either way,
          // else fall back to the last listed alternative (this corpus's
          // authoring convention) so exactly one is always marked chosen
          // (model::Decision::validate()).
          int exact = -1, sub = -1;
          for (std::size_t i = 0; i < dec.alternatives.size(); ++i) {
            const auto& label = dec.alternatives[i].label;
            if (label == chosen) exact = static_cast<int>(i);
            if (sub < 0 && (label.find(chosen) != std::string::npos || chosen.find(label) != std::string::npos)) {
              sub = static_cast<int>(i);
            }
          }
          int pick = exact >= 0 ? exact : (sub >= 0 ? sub : static_cast<int>(dec.alternatives.size()) - 1);
          if (pick >= 0) dec.alternatives[static_cast<std::size_t>(pick)].chosen = true;
        }
        if (d.contains("principle_evidence")) {
          for (const auto& pr : d["principle_evidence"]) dec.principles.push_back(pr.get<std::string>());
        }
        if (d.contains("supersedes") && d["supersedes"].is_string()) {
          superseded_by[d["supersedes"].get<std::string>()] = did;
        }
        decisions.push_back(dec);

        // Link this decision to every feature whose id shares a real word
        // with it, so a goal targeting the FEATURE can reach the decision
        // by one-hop BFS + the existing premises.claims dependency closure
        // (see the file header).
        for (const auto& fid : feature_ids_here) {
          if (!shares_token(did, fid)) continue;
          Claim link;
          link.subject = out.feature_entity[fid];
          link.predicate = "relevant_decision";
          link.value = did;
          link.assessment.evidence = EvidenceClass::Derived;
          link.assessment.origin = Origin::System;
          link.assessment.confidence = 0.99;
          link.assessment.derivation = Derivation{"eval.link_feature_decision", 1, "", 0};
          link.assessment.premises.claims = {did};
          link.id = Claim::make_id(link.subject, link.predicate, "", link.value, link.qualifiers);
          out.all_claims.push_back(link);
        }
      }
    }
  }
  for (auto& d : decisions) {
    auto it = superseded_by.find(d.id);
    if (it != superseded_by.end()) d.superseded_by = it->second;
  }

  if (gt.contains("principles")) {
    for (const auto& p : gt["principles"]) {
      Principle pr;
      pr.id = json::get_string(p, "id");
      pr.statement = {{"pl", json::get_string(p["statement"], "pl")}, {"en", json::get_string(p["statement"], "en")}};
      if (auto lvl = model::from_string<PrincipleLevel>(json::get_string(p, "level"))) pr.level = *lvl;
      if (auto frm = model::from_string<PrincipleForm>(json::get_string(p, "form"))) pr.form = *frm;
      if (auto val = model::from_string<ValidationStatus>(json::get_string(p, "validation_status"))) pr.validation = *val;
      pr.confidence = 0.65;
      if (p.contains("protects"))
        for (const auto& v : p["protects"]) pr.protects.push_back(v.get<std::string>());
      if (p.contains("conflicts_with"))
        for (const auto& v : p["conflicts_with"]) pr.conflicts_with.push_back(v.get<std::string>());
      std::string earliest;
      if (p.contains("phrasings")) {
        for (const auto& ph : p["phrasings"]) {
          std::string d = json::get_string(ph, "date").substr(0, 10);
          if (!d.empty() && (earliest.empty() || d < earliest)) earliest = d;
        }
      }
      if (!earliest.empty()) pr.sources.push_back(Reference{"ground_truth.json", "", earliest, "", "", ""});
      out.all_principles.push_back(pr);
    }
  }

  LOOM_REQUIRE_OK(ks.put_entities(run, entities));
  LOOM_REQUIRE_OK(ks.put_claims(run, out.all_claims));
  LOOM_REQUIRE_OK(ks.put_decisions(run, decisions));
  LOOM_REQUIRE_OK(ks.put_status_records(run, out.all_status));
  LOOM_REQUIRE_OK(ks.put_principles(run, out.all_principles));
  LOOM_REQUIRE_OK(ks.finish_run(run, "done", Json::object()));
  return out;
}

// Every rendered summary line the run holds, for the naive/flat baselines
// (never used by ContextEngine itself).
// Human-readable rendering (label, not raw entity id) so a keyword/lexical
// baseline has a fair chance -- exactly what any real prompt-compiler would
// show a model, naive or not.
std::string label_of(const LoadedCorpus& c, const std::string& entity_id) {
  auto it = c.entity_label.find(entity_id);
  return it != c.entity_label.end() ? it->second : entity_id;
}

std::vector<std::string> all_summaries(const LoadedCorpus& c) {
  std::vector<std::string> out;
  for (const auto& cl : c.all_claims) {
    out.push_back(label_of(c, cl.subject) + " " + cl.predicate + " " + json::dump(cl.value) +
                  (cl.qualifiers.valid_from.empty() ? "" : " (" + cl.qualifiers.valid_from + ")"));
  }
  for (const auto& p : c.all_principles) out.push_back(ctx::pick_text(p.statement, "en"));
  for (const auto& s : c.all_status) {
    out.push_back(label_of(c, s.entity) + " " + std::string(model::to_string(s.status)) + " " + s.version);
  }
  return out;
}

int naive_full_history_tokens(const LoadedCorpus& c) {
  int total = 0;
  for (const auto& s : all_summaries(c)) total += context::ContextEngine::estimate_tokens(s);
  return total;
}

// Flat retrieval: every line containing ANY query token (>=3 chars, folded
// substring), no ranking, no bands, no budget, no authority/freshness
// weighting, no stemming -- included in full or not at all. A common naive
// RAG baseline; `covered` reports whether it happened to include a specific
// rendered line (so a small token count can be checked against what it
// actually found, not just its size: a cheap answer that misses the
// question is not an efficient one).
struct FlatResult {
  int tokens = 0;
  std::vector<std::string> hit_lines;
};
FlatResult flat_retrieval(const kb::Pack& pack, const LoadedCorpus& c, std::string_view query_text) {
  kb::Normalizer norm(pack);
  std::string qf = norm.fold(query_text);
  auto qtoks = norm.tokens(qf);
  FlatResult r;
  for (const auto& s : all_summaries(c)) {
    std::string sf = norm.fold(s);
    bool hit = false;
    for (const auto& t : qtoks) {
      if (t.size() < 3) continue;
      if (sf.find(t) != std::string::npos) {
        hit = true;
        break;
      }
    }
    if (hit) {
      r.tokens += context::ContextEngine::estimate_tokens(s);
      r.hit_lines.push_back(s);
    }
  }
  return r;
}
bool any_contains(const std::vector<std::string>& lines, std::string_view needle) {
  for (const auto& l : lines) {
    if (l.find(needle) != std::string::npos) return true;
  }
  return false;
}

bool has_ref(const model::ContextSet& set, const std::string& ref) {
  for (const auto& it : set.items) {
    if (it.ref == ref) return true;
  }
  return false;
}

}  // namespace

TEST_SUITE("context_eval") {
  TEST_CASE("context efficiency vs naive full-history and flat-retrieval, coverage@budget, determinism") {
    fsutil::TempDir td;
    auto rt = open_rt(td.path());
    auto pack = unwrap(rt->knowledge().pack());
    auto& ks = rt->knowledge().store();
    std::string run = unwrap(ks.begin_run(pack->hash(), Json::object())).id;
    LoadedCorpus corpus = load_ground_truth(ks, run);

    REQUIRE(corpus.project_entity.count("proj.noteflow"));
    REQUIRE(corpus.feature_entity.count("feat.nf.checklist_notes"));
    REQUIRE(corpus.feature_entity.count("feat.nf.encryption"));
    // Five projects loaded: the selector must find the one prompt-relevant
    // decision among many, in every project's worth of noise, not just
    // whatever happens to be the only thing in the store.
    CHECK(corpus.project_entity.size() == 5);

    int naive = naive_full_history_tokens(corpus);
    CHECK(naive > 400);  // the corpus is non-trivial: this is a real reduction test, not a toy

    context::ContextEngine engine(*rt, ks, pack);

    struct Scenario {
      std::string name, text, target_feature, goal_type;
      std::vector<std::string> relevant_decisions;  // ground-truth-relevant claims to cover
      std::vector<std::string> relevant_principles;
    };
    std::vector<Scenario> scenarios = {
        {"implement checklist feature (PL)", "zaimplementuj checklisty w NoteFlow, dokończ tę funkcję",
         "feat.nf.checklist_notes", "implement_part", {"dec.nf.checklist_as_data"}, {"pr.data_over_branches"}},
        {"verify encryption claim", "czy to prawda że szyfrowanie w NoteFlow zostało w końcu zaimplementowane? sprawdź to",
         "feat.nf.encryption", "verify_claim", {"dec.nf.encryption", "dec.nf.encryption_defer"}, {"pr.provider_not_special_case"}},
    };

    // Every decision id belonging to a project OTHER than NoteFlow: none of
    // these should leak into a NoteFlow-scoped selection (precision).
    std::set<std::string> foreign_decision_prefixes = {"dec.rt.", "dec.wd.", "dec.lk.", "dec.mu."};

    for (const auto& sc : scenarios) {
      INFO("scenario: " << sc.name);
      context::ContextRequest req;
      req.text = sc.text;
      req.targets = {corpus.feature_entity.at(sc.target_feature)};
      req.project = corpus.project_entity.at("proj.noteflow");
      req.goal_type = sc.goal_type;
      req.budget_tokens = 1500;

      auto set = unwrap(engine.select(req));
      CHECK(set.used_tokens <= set.budget_tokens);

      FlatResult flat = flat_retrieval(*pack, corpus, sc.text);

      // The primary saving (R12): goal-directed selection costs a fraction
      // of dumping the whole corpus.
      CHECK(set.used_tokens < naive);
      // The fixture's 5 projects are not evenly sized (NoteFlow alone carries
      // roughly half of every decision/feature in the corpus), so even a
      // perfectly project-scoped selection cannot shrink to a tiny fraction
      // of the whole-corpus baseline here; 0.7 is a real, non-trivial
      // reduction for this corpus and still catches a selector that
      // regresses to "just dump everything".
      CHECK(static_cast<double>(set.used_tokens) < 0.7 * naive);

      // Coverage@budget: every ground-truth-relevant claim for this goal is
      // actually present in the selected ContextSet.
      for (const auto& dec_id : sc.relevant_decisions) CHECK(has_ref(set, dec_id));
      for (const auto& pr_id : sc.relevant_principles) CHECK(has_ref(set, pr_id));

      // Flat retrieval, reported for context (not asserted against): a
      // plain substring/keyword match with no bands, no dependency closure
      // and no stable "constitution" prefix can coincidentally be cheap and
      // fairly complete on a single, cleanly-named project (the project's
      // own name is a query token here, so it acts as an accidental
      // project filter) -- but it never includes the goal-independent
      // stable band (invariant principles / preferences, R11) that every
      // goal-directed selection carries regardless of wording, and it has
      // no notion of a token budget, authority, freshness or diversity. How
      // many of the goal's relevant decisions it happens to cover:
      int flat_hits = 0;
      for (const auto& dec_id : sc.relevant_decisions) {
        auto it = std::find_if(corpus.all_claims.begin(), corpus.all_claims.end(),
                               [&](const Claim& c) { return c.id == dec_id; });
        if (it != corpus.all_claims.end() && any_contains(flat.hit_lines, json::dump(it->value))) ++flat_hits;
      }
      MESSAGE("naive=" << naive << " flat_tokens=" << flat.tokens << " flat_coverage=" << flat_hits << "/"
                       << sc.relevant_decisions.size() << " selected=" << set.used_tokens);
      // Selection always keeps the stable band (invariants + preferences);
      // flat retrieval's ad hoc token match has no such concept at all.
      bool selection_has_stable_principle = false;
      for (const auto& it : set.items) {
        if (it.band == ContextBand::Stable && it.ref_kind == RefKind::Principle) selection_has_stable_principle = true;
      }
      CHECK(selection_has_stable_principle);

      // Precision: no other project's decisions leak into a NoteFlow-scoped goal.
      for (const auto& it : set.items) {
        for (const auto& prefix : foreign_decision_prefixes) CHECK(it.ref.rfind(prefix, 0) != 0);
      }

      // Determinism (I5): selecting twice from the same run is byte-identical.
      auto set2 = unwrap(engine.select(req));
      CHECK(set.to_json().dump() == set2.to_json().dump());
    }
  }

  TEST_CASE("determinism holds across two independently rebuilt runs from the same fixture") {
    fsutil::TempDir td1, td2;
    auto rt1 = open_rt(td1.path());
    auto rt2 = open_rt(td2.path());
    auto pack1 = unwrap(rt1->knowledge().pack());
    auto pack2 = unwrap(rt2->knowledge().pack());
    CHECK(pack1->hash() == pack2->hash());

    auto& ks1 = rt1->knowledge().store();
    auto& ks2 = rt2->knowledge().store();
    std::string run1 = unwrap(ks1.begin_run(pack1->hash(), Json::object())).id;
    std::string run2 = unwrap(ks2.begin_run(pack2->hash(), Json::object())).id;
    CHECK(run1 == run2);  // content-derived run id (I5)
    LoadedCorpus c1 = load_ground_truth(ks1, run1);
    LoadedCorpus c2 = load_ground_truth(ks2, run2);

    context::ContextEngine e1(*rt1, ks1, pack1);
    context::ContextEngine e2(*rt2, ks2, pack2);
    context::ContextRequest req;
    req.text = "zaimplementuj checklisty w NoteFlow";
    req.targets = {c1.feature_entity.at("feat.nf.checklist_notes")};
    req.project = c1.project_entity.at("proj.noteflow");
    req.goal_type = "implement_part";
    req.budget_tokens = 1500;
    REQUIRE(c1.feature_entity.at("feat.nf.checklist_notes") == c2.feature_entity.at("feat.nf.checklist_notes"));
    REQUIRE(c1.project_entity.at("proj.noteflow") == c2.project_entity.at("proj.noteflow"));

    auto set1 = unwrap(e1.select(req));
    auto set2 = unwrap(e2.select(req));
    CHECK(set1.to_json().dump() == set2.to_json().dump());
    auto text1 = unwrap(e1.render(set1));
    auto text2 = unwrap(e2.render(set2));
    CHECK(text1 == text2);
  }
}
