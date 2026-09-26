// context_engine.h: goal-directed context selection (LOOM_CONCEPTUAL_MODEL
// §4; R12, R13 §1). See the header for the full contract.
#include "loom/context_engine.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "loom/config.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/semantic_llm.h"
#include "loom/util/utf8.h"
#include "ctx_common.h"

namespace loom::context {

namespace {

using model::ContextBand;
using model::Resolution;

// ── rendered content, one struct per RefKind we handle directly ─────
struct RenderedContent {
  std::string label, summary, full, raw;
  std::string pick(Resolution r) const {
    switch (r) {
      case Resolution::Label:
        return label;
      case Resolution::Summary:
        return summary.empty() ? label : summary;
      case Resolution::Full:
        return full.empty() ? summary : full;
      case Resolution::Raw:
        return raw.empty() ? (full.empty() ? summary : full) : raw;
    }
    return summary;
  }
};

std::string fmt_value(const Json& v) {
  if (v.is_string()) return v.get<std::string>();
  if (v.is_null()) return "(absent)";
  return json::dump(v);
}

std::string join(const std::vector<std::string>& v, std::string_view sep) {
  std::string out;
  for (const auto& s : v) {
    if (!out.empty()) out += sep;
    out += s;
  }
  return out;
}

// One item gathered for scoring, before it is turned into a ContextItem.
struct Candidate {
  model::RefKind ref_kind = model::RefKind::Claim;
  std::string ref;
  ContextBand band = ContextBand::Goal;
  std::optional<model::Role> role;
  std::string subject;  // diversity grouping key
  std::string date;     // for freshness
  model::Origin origin = model::Origin::System;
  double confidence = 0.5;
  bool is_principle = false;                                     // no EvidenceClass of its own
  model::EvidenceClass evidence = model::EvidenceClass::Derived;  // meaningless when is_principle
  model::ValidationStatus validation = model::ValidationStatus::Candidate;
  RenderedContent content;
  std::optional<kb::ExpectedProperty> expected;
  std::string basis;
  Json fill_query;
  std::vector<std::string> premise_claims;
  std::vector<std::string> premise_principles;
  std::string why;
  double base_relevance = 0.5;
  double score = 0.0;  // filled by score_all()

  // Filled once accepted.
  int tokens = 0;
  std::string rendered_text;
};

bool contains_role(const std::vector<model::Role>& roles, model::Role r) {
  return std::find(roles.begin(), roles.end(), r) != roles.end();
}
bool contains_level(const std::vector<model::PrincipleLevel>& levels, model::PrincipleLevel l) {
  return std::find(levels.begin(), levels.end(), l) != levels.end();
}
bool contains_evidence(const std::vector<model::EvidenceClass>& evs, model::EvidenceClass e) {
  return std::find(evs.begin(), evs.end(), e) != evs.end();
}

Resolution resolution_for(const model::GoalType& gt, std::optional<model::Role> role) {
  if (role) {
    auto it = gt.resolutions.find(std::string(model::to_string(*role)));
    if (it != gt.resolutions.end()) return it->second;
  }
  auto it = gt.resolutions.find("*");
  return it != gt.resolutions.end() ? it->second : Resolution::Summary;
}

void score_all(std::vector<Candidate>& cands, std::string_view anchor_date) {
  for (auto& c : cands) {
    double authority = ctx::authority_score(c.origin);
    double freshness = ctx::freshness_score(c.date, anchor_date);
    c.score = c.base_relevance * authority * freshness * c.confidence;
  }
}

// Maximal-marginal-relevance-style diversity: repeatedly pick the remaining
// candidate whose score, discounted by how many items of the same subject
// were already picked, is highest. Deterministic (ties broken by ref).
std::vector<Candidate> diversify(std::vector<Candidate> cands) {
  std::vector<Candidate> out;
  out.reserve(cands.size());
  std::map<std::string, int> subject_count;
  std::vector<char> used(cands.size(), 0);
  for (std::size_t k = 0; k < cands.size(); ++k) {
    int best = -1;
    double best_eff = -1.0;
    for (std::size_t i = 0; i < cands.size(); ++i) {
      if (used[i]) continue;
      int cnt = subject_count.count(cands[i].subject) ? subject_count[cands[i].subject] : 0;
      double eff = cands[i].score * std::pow(0.8, cnt);
      if (best < 0 || eff > best_eff || (eff == best_eff && cands[i].ref < cands[best].ref)) {
        best = static_cast<int>(i);
        best_eff = eff;
      }
    }
    if (best < 0) break;
    used[best] = 1;
    subject_count[cands[best].subject]++;
    out.push_back(std::move(cands[best]));
  }
  return out;
}

}  // namespace

Result<ContextRequest> ContextRequest::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "context request must be an object");
  ContextRequest r;
  r.text = json::get_string(j, "text");
  if (const Json* t = json::find(j, "targets"); t && t->is_array()) {
    for (const auto& e : *t) {
      if (e.is_string()) r.targets.push_back(e.get<std::string>());
    }
  }
  r.project = json::get_string(j, "project");
  r.budget_tokens = static_cast<int>(json::get_int(j, "budget_tokens", 4000));
  r.goal_type = json::get_opt_string(j, "goal_type");
  r.run = json::get_string(j, "run");
  r.lang = json::get_string(j, "lang");
  return r;
}

Json ContextRequest::to_json() const {
  Json targets = Json::array();
  for (const auto& t : targets) targets.push_back(t);
  return Json{{"text", text},
              {"targets", targets},
              {"project", project},
              {"budget_tokens", budget_tokens},
              {"goal_type", goal_type ? Json(*goal_type) : Json(nullptr)},
              {"run", run},
              {"lang", lang}};
}

ContextEngine::ContextEngine(Runtime& rt, kb::KnowledgeStore& store, std::shared_ptr<const kb::Pack> pack)
    : rt_(rt), store_(store), pack_(std::move(pack)) {}

int ContextEngine::estimate_tokens(std::string_view text) noexcept {
  std::size_t n = utf8::length(text);
  return n == 0 ? 0 : static_cast<int>(std::max<std::size_t>(1, n / 4));
}

// ── goal typing ──────────────────────────────────────────────────────

namespace {

struct Classification {
  std::string type;
  double confidence = 0.0;
  Json scores = Json::object();
};

Classification classify_by_cues(const std::vector<model::GoalType>& types, const kb::Normalizer& norm,
                                std::string_view text) {
  std::string folded = norm.fold(text);
  std::map<std::string, double> scores;
  for (const auto& gt : types) {
    double s = 0.0;
    if (gt.cues.is_object()) {
      for (const auto& [lang, arr] : gt.cues.items()) {
        (void)lang;
        if (!arr.is_array()) continue;
        for (const auto& cue_j : arr) {
          if (!cue_j.is_string()) continue;
          std::string cue = norm.fold(cue_j.get<std::string>());
          if (cue.empty()) continue;
          std::size_t words = 1 + static_cast<std::size_t>(std::count(cue.begin(), cue.end(), ' '));
          double weight = 1.0 + 0.4 * static_cast<double>(words - 1);
          std::size_t pos = 0, hits = 0;
          while ((pos = folded.find(cue, pos)) != std::string::npos) {
            ++hits;
            pos += cue.size();
          }
          s += weight * static_cast<double>(hits);
        }
      }
    }
    scores[gt.id] = s;
  }
  std::string best;
  double best_s = -1.0, second_s = 0.0;
  for (const auto& [id, s] : scores) {
    if (s > best_s) {
      second_s = best_s < 0 ? 0.0 : best_s;
      best_s = s;
      best = id;
    } else if (s > second_s) {
      second_s = s;
    }
  }
  Classification c;
  Json sj = Json::object();
  for (const auto& [id, s] : scores) sj[id] = s;
  c.scores = sj;
  if (best_s <= 0.0) {
    // No cue matched anything: fall back to the most general goal type
    // (first by id; goal_types.json sorts "answer_question" among them) at
    // low confidence rather than guessing.
    c.type = types.empty() ? "" : types.front().id;
    for (const auto& gt : types) {
      if (gt.id == "answer_question") c.type = gt.id;
    }
    c.confidence = 0.25;
    return c;
  }
  c.type = best;
  c.confidence = std::clamp(best_s / (best_s + second_s + 1.0), 0.0, 1.0);
  return c;
}

Result<model::GoalType> find_goal_type_checked(const kb::Pack& pack, std::string_view id,
                                               const std::vector<model::GoalType>& all) {
  for (const auto& gt : all) {
    if (gt.id == id) return gt;
  }
  std::string ids;
  for (const auto& gt : all) {
    if (!ids.empty()) ids += ", ";
    ids += gt.id;
  }
  (void)pack;
  return Error(Errc::NotFound, "unknown goal type '" + std::string(id) + "' (one of: " + ids + ")");
}

}  // namespace

Result<model::Goal> ContextEngine::type_goal(const ContextRequest& req) {
  LOOM_TRY_ASSIGN(auto all_types, ctx::list_goal_types(*pack_));
  kb::Normalizer norm(*pack_);

  std::string chosen_type;
  double confidence = 0.0;
  Json params = Json::object();

  if (req.goal_type && !req.goal_type->empty()) {
    LOOM_TRY_ASSIGN(auto gt, find_goal_type_checked(*pack_, *req.goal_type, all_types));
    chosen_type = gt.id;
    confidence = 1.0;
    params["classifier"] = "forced";
  } else {
    auto cls = classify_by_cues(all_types, norm, req.text);
    chosen_type = cls.type;
    confidence = cls.confidence;
    params["classifier"] = "cue";
    params["scores"] = cls.scores;

    // Optional LLM classifier (only when a semantic model + API key are
    // configured, mirroring SemanticLLM::enabled(); never required, never
    // called in the common offline/test configuration, so determinism is
    // unaffected by default).
    if (confidence < 0.55) {
      std::string model = rt_.config().get("semantic_model", "").is_string()
                              ? rt_.config().get("semantic_model", "").get<std::string>()
                              : "";
      std::string api_key = rt_.secrets().get_string("api_key");
      if (!model.empty() && !api_key.empty() && !chosen_type.empty()) {
        std::string base_url = rt_.config().get("base_url", "https://openrouter.ai/api/v1").get<std::string>();
        std::string prompt =
            "Classify the following prompt into exactly one goal type id. Reply with strict JSON only: "
            "{\"goal_type\": \"<id>\", \"confidence\": <0..1>}.\nGoal types:\n";
        for (const auto& gt : all_types) {
          prompt += "- " + gt.id + ": " + gt.description + "\n";
        }
        prompt += "\nPrompt:\n" + req.text;

        net::HttpRequest hreq;
        hreq.method = "POST";
        hreq.url = base_url + "/chat/completions";
        hreq.headers = {{"Authorization", "Bearer " + api_key},
                        {"Content-Type", "application/json"},
                        {"HTTP-Referer", "https://github.com/chatadhd"},
                        {"X-Title", "ChatADHD-Context"}};
        hreq.body = json::dump(Json{{"model", model},
                                    {"messages", Json::array({Json{{"role", "user"}, {"content", prompt}}})},
                                    {"temperature", 0.0},
                                    {"max_tokens", 100}});
        hreq.timeout_ms = 15000;
        auto resp = rt_.http().send(hreq);
        if (resp && resp->ok()) {
          auto body = resp->json();
          if (body) {
            std::string content;
            if (const Json* choices = json::find(*body, "choices"); choices && choices->is_array() && !choices->empty()) {
              if (const Json* msg = json::find((*choices)[0], "message"); msg) content = json::get_string(*msg, "content");
            }
            if (auto parsed = SemanticLLM::parse_response_json(content)) {
              std::string llm_type = json::get_string(*parsed, "goal_type");
              double llm_conf = json::get_number(*parsed, "confidence", 0.6);
              bool valid = false;
              for (const auto& gt : all_types) valid = valid || gt.id == llm_type;
              if (valid) {
                chosen_type = llm_type;
                confidence = std::clamp(llm_conf, 0.0, 1.0);
                params["classifier"] = "llm";
              }
            }
          }
        }
        // Any failure (network, non-200, unparseable JSON, unknown id) is
        // silently ignored: the cue-based result already computed above
        // stays in effect, exactly like SemanticLLM's regex fallback.
      }
    }
  }

  model::Goal goal;
  goal.type = chosen_type;
  goal.text = req.text;
  goal.targets = req.targets;
  goal.project = req.project;
  goal.confidence = confidence;
  goal.params = params;
  goal.id = model::Goal::make_id(goal.type, goal.text, goal.targets);
  return goal;
}

// ── selection ────────────────────────────────────────────────────────

Result<model::ContextSet> ContextEngine::select(const ContextRequest& req) {
  LOOM_TRY_ASSIGN(model::Goal goal, type_goal(req));
  LOOM_TRY_ASSIGN(std::string run, ctx::resolve_run(store_, req.run));
  LOOM_TRY_ASSIGN(auto all_types, ctx::list_goal_types(*pack_));
  LOOM_TRY_ASSIGN(model::GoalType gt, find_goal_type_checked(*pack_, goal.type, all_types));

  int total_budget = req.budget_tokens > 0 ? req.budget_tokens : 4000;
  std::string lang = req.lang;

  std::map<std::string, model::Entity> entity_cache;
  auto label_of = [&](const std::string& id) -> std::string {
    if (id.empty()) return "";
    auto it = entity_cache.find(id);
    if (it == entity_cache.end()) {
      auto r = store_.get_entity(run, id);
      model::Entity e;
      if (r && *r) e = **r;
      else e.label = id;
      it = entity_cache.emplace(id, std::move(e)).first;
    }
    return it->second.label.empty() ? (it->second.canonical_key.empty() ? id : it->second.canonical_key)
                                    : it->second.label;
  };

  auto role_rank = [&](std::optional<model::Role> r) -> int {
    if (!r) return -1;
    for (std::size_t i = 0; i < gt.roles.size(); ++i) {
      if (gt.roles[i] == *r) return static_cast<int>(i);
    }
    return -1;
  };

  std::vector<std::string> seeds = goal.targets;
  std::string anchor_project = !goal.project.empty() ? goal.project : (seeds.empty() ? "" : seeds.front());
  if (!anchor_project.empty() && std::find(seeds.begin(), seeds.end(), anchor_project) == seeds.end()) {
    seeds.push_back(anchor_project);
  }

  std::vector<Candidate> stable, project_b, goal_b;
  std::string anchor_date;
  auto note_date = [&](const std::string& d) {
    if (!d.empty() && d > anchor_date) anchor_date = d;
  };

  // ── principles: stable (invariant + preference) or project (matching level) ──
  LOOM_TRY_ASSIGN(auto principles, store_.list_principles(run));
  for (auto& p : principles) {
    std::string d = model::earliest_source_date(p.sources);
    note_date(d);
    bool core = p.form == model::PrincipleForm::Invariant || p.is_preference();
    bool level_ok = contains_level(gt.principle_levels, p.level);
    if (!core && !level_ok) continue;
    Candidate c;
    c.ref_kind = model::RefKind::Principle;
    c.ref = p.id;
    c.band = core ? ContextBand::Stable : ContextBand::Project;
    c.subject = p.id;
    c.date = d;
    c.origin = p.origin;
    c.confidence = p.confidence;
    c.is_principle = true;
    c.validation = p.validation;
    std::string stmt = ctx::pick_text(p.statement, lang);
    c.content.label = utf8::length(stmt) > 100 ? std::string(utf8::prefix(stmt, 100)) + "..." : stmt;
    std::string badge = ctx::principle_badge(p.validation, p.level, p.form);
    c.content.summary = stmt + " " + badge;
    std::string full = c.content.summary;
    if (!p.protects.empty()) full += " (protects: " + join(p.protects, ", ") + ")";
    if (!p.exceptions.empty()) full += " (exceptions: " + join(p.exceptions, "; ") + ")";
    c.content.full = full;
    c.content.raw = json::dump(p.to_json());
    c.premise_principles = p.derived_from;
    c.why = core ? (p.is_preference() ? "core preference (always included)" : "invariant principle (always included)")
                 : ("principle at level '" + std::string(model::to_string(p.level)) + "' matches this goal type");
    c.base_relevance = core ? 1.0 : 0.7;
    (core ? stable : project_b).push_back(std::move(c));
  }

  // ── project band: instance slots, decisions, status history ─────────
  if (!anchor_project.empty()) {
    auto insts = store_.query_instances(run, "", anchor_project);
    if (insts) {
      for (auto& inst : *insts) {
        kb::SlotQuery sq;
        sq.instance = inst.id;
        auto rows = store_.query_slots(run, sq);
        if (!rows) continue;
        for (auto& row : *rows) {
          if (row.value.role && !contains_role(gt.roles, *row.value.role)) continue;
          auto claim_r = store_.get_claim(run, row.value.claim);
          if (!claim_r || !*claim_r) continue;
          const auto& claim = **claim_r;
          if (!contains_evidence(gt.evidence, claim.assessment.evidence)) continue;
          note_date(claim.qualifiers.valid_from);
          Candidate c;
          c.ref_kind = model::RefKind::Claim;
          c.ref = claim.id;
          c.band = ContextBand::Project;
          c.role = row.value.role;
          c.subject = claim.subject;
          c.date = claim.qualifiers.valid_from;
          c.origin = claim.assessment.origin;
          c.confidence = claim.assessment.confidence;
          c.evidence = claim.assessment.evidence;
          c.expected = claim.assessment.expected;
          if (claim.assessment.derivation) c.basis = claim.assessment.derivation->op;
          c.fill_query = claim.assessment.open.fill_query;
          std::string subj_label = label_of(claim.subject);
          std::string obj_label = claim.object.empty() ? fmt_value(claim.value) : label_of(claim.object);
          c.content.label = subj_label + " " + claim.predicate + " " + obj_label;
          c.content.summary = c.content.label + (claim.qualifiers.valid_from.empty() ? "" : " (" + claim.qualifiers.valid_from + ")");
          std::string full = c.content.summary;
          if (!claim.assessment.support.empty()) full += " — \"" + std::string(utf8::prefix(claim.assessment.support.front().quote, 160)) + "\"";
          c.content.full = full;
          std::string raw;
          for (auto& s : claim.assessment.support) raw += (raw.empty() ? "" : " / ") + s.quote;
          c.content.raw = raw.empty() ? full : raw;
          c.premise_claims = claim.assessment.premises.claims;
          c.premise_principles = claim.assessment.premises.principles;
          int rr = role_rank(row.value.role);
          c.why = "project slot '" + row.value.slot + "'" + (rr >= 0 ? " (role priority " + std::to_string(rr) + ")" : "");
          c.base_relevance = rr >= 0 ? std::clamp(0.6 + 0.4 * (1.0 - static_cast<double>(rr) / std::max<std::size_t>(1, gt.roles.size())), 0.0, 1.0) : 0.55;
          project_b.push_back(std::move(c));
        }
      }
    }

    auto decisions = store_.list_decisions(run, anchor_project);
    if (decisions) {
      for (auto& d : *decisions) {
        auto claim_r = store_.get_claim(run, d.id);
        model::Claim underlying;
        bool have_claim = claim_r && *claim_r;
        if (have_claim) underlying = **claim_r;
        if (have_claim && !contains_evidence(gt.evidence, underlying.assessment.evidence)) continue;
        note_date(d.date);
        Candidate c;
        c.ref_kind = model::RefKind::Decision;
        c.ref = d.id;
        c.band = ContextBand::Project;
        c.role = model::Role::Decision;
        c.subject = d.subject;
        c.date = d.date;
        c.origin = have_claim ? underlying.assessment.origin : model::Origin::Archive;
        c.confidence = have_claim ? underlying.assessment.confidence : 0.7;
        c.evidence = have_claim ? underlying.assessment.evidence : model::EvidenceClass::Observed;
        const model::DecisionAlternative* chosen = d.chosen();
        std::string chosen_label = chosen ? (chosen->object.empty() ? fmt_value(chosen->value) : label_of(chosen->object)) : "?";
        std::string subj_label = label_of(d.subject);
        c.content.label = subj_label + " decided: " + chosen_label;
        c.content.summary = c.content.label + (d.date.empty() ? "" : " (" + d.date + ")");
        std::vector<std::string> alt_labels;
        for (auto& a : d.alternatives) alt_labels.push_back(a.label.empty() ? fmt_value(a.value) : a.label);
        std::string full = c.content.summary + (alt_labels.empty() ? "" : " — among: " + join(alt_labels, " | "));
        if (!d.principles.empty()) full += "; principles: " + join(d.principles, ", ");
        if (!d.superseded_by.empty()) full += "; superseded by " + d.superseded_by;
        c.content.full = full;
        c.content.raw = json::dump(d.to_json());
        c.premise_principles = d.principles;
        c.why = "decision recorded for " + subj_label;
        c.base_relevance = contains_role(gt.roles, model::Role::Decision) ? 0.75 : 0.5;
        project_b.push_back(std::move(c));
      }
    }

    for (const auto& e : seeds) {
      auto hist = store_.status_history(run, e);
      if (!hist) continue;
      for (auto& sr : *hist) {
        note_date(sr.date);
        Candidate c;
        c.ref_kind = model::RefKind::StatusRecord;
        c.ref = sr.id;
        c.band = ContextBand::Project;
        c.role = model::Role::Part;
        c.subject = sr.entity;
        c.date = sr.date;
        c.origin = model::Origin::Archive;
        c.confidence = 0.75;
        c.evidence = model::EvidenceClass::Observed;
        std::string entity_label = label_of(sr.entity);
        c.content.label = entity_label + ": " + std::string(model::to_string(sr.status));
        c.content.summary = c.content.label + " (branch " + (sr.branch.empty() ? "main" : sr.branch) + ", v" + sr.version + ", " + sr.date + ")";
        c.content.full = c.content.summary + (sr.oscillation ? " [oscillates]" : "");
        c.content.raw = json::dump(sr.to_json());
        c.why = "status history of " + entity_label;
        c.base_relevance = contains_role(gt.roles, model::Role::Part) ? 0.65 : 0.45;
        project_b.push_back(std::move(c));
      }
    }
  }

  // ── goal band: claims one hop from every seed, either direction ─────
  std::set<std::string> seen_claims;
  kb::Normalizer norm(*pack_);
  std::string q_folded = norm.fold(req.text);
  auto lexical_overlap = [&](const RenderedContent& rc) -> double {
    if (q_folded.empty()) return 0.0;
    std::string t = norm.fold(rc.summary);
    if (t.empty()) return 0.0;
    auto qt = norm.tokens(q_folded);
    if (qt.empty()) return 0.0;
    std::size_t hits = 0;
    for (auto& tok : qt) {
      if (tok.size() < 3) continue;
      if (t.find(tok) != std::string::npos) ++hits;
    }
    return qt.empty() ? 0.0 : static_cast<double>(hits) / static_cast<double>(qt.size());
  };

  auto add_goal_claim = [&](const model::Claim& claim, const std::string& via_entity) {
    if (!seen_claims.insert(claim.id).second) return;
    if (!contains_evidence(gt.evidence, claim.assessment.evidence)) return;
    note_date(claim.qualifiers.valid_from);
    Candidate c;
    c.ref_kind = model::RefKind::Claim;
    c.ref = claim.id;
    c.band = ContextBand::Goal;
    c.subject = claim.subject;
    c.date = claim.qualifiers.valid_from;
    c.origin = claim.assessment.origin;
    c.confidence = claim.assessment.confidence;
    c.evidence = claim.assessment.evidence;
    c.expected = claim.assessment.expected;
    if (claim.assessment.derivation) c.basis = claim.assessment.derivation->op;
    c.fill_query = claim.assessment.open.fill_query;
    std::string subj_label = label_of(claim.subject);
    std::string obj_label = claim.object.empty() ? fmt_value(claim.value) : label_of(claim.object);
    c.content.label = subj_label + " " + claim.predicate + " " + obj_label;
    c.content.summary = c.content.label + (claim.qualifiers.valid_from.empty() ? "" : " (" + claim.qualifiers.valid_from + ")");
    std::string full = c.content.summary;
    if (!claim.assessment.support.empty()) full += " — \"" + std::string(utf8::prefix(claim.assessment.support.front().quote, 160)) + "\"";
    if (claim.assessment.status == model::ClaimStatus::Contested) full += " [contested]";
    c.content.full = full;
    std::string raw;
    for (auto& s : claim.assessment.support) raw += (raw.empty() ? "" : " / ") + s.quote;
    c.content.raw = raw.empty() ? full : raw;
    c.premise_claims = claim.assessment.premises.claims;
    c.premise_principles = claim.assessment.premises.principles;
    c.why = "one hop from '" + label_of(via_entity) + "' via '" + claim.predicate + "'";
    double lex = lexical_overlap(c.content);
    c.base_relevance = std::clamp(0.35 + 0.65 * lex, 0.0, 1.0);
    goal_b.push_back(std::move(c));
  };

  for (const auto& e : seeds) {
    kb::ClaimQuery qs;
    qs.subject = e;
    if (auto r = store_.query_claims(run, qs)) {
      for (auto& cl : *r) add_goal_claim(cl, e);
    }
    kb::ClaimQuery qo;
    qo.object = e;
    if (auto r = store_.query_claims(run, qo)) {
      for (auto& cl : *r) add_goal_claim(cl, e);
    }
  }

  // ── score, diversify, budget (cascading leftover forward) ───────────
  score_all(stable, anchor_date);
  score_all(project_b, anchor_date);
  score_all(goal_b, anchor_date);
  auto stable_d = diversify(std::move(stable));
  auto project_d = diversify(std::move(project_b));
  auto goal_d = diversify(std::move(goal_b));

  std::array<int, 3> band_budget = {
      static_cast<int>(std::llround(gt.budget.count(ContextBand::Stable) ? gt.budget.at(ContextBand::Stable) * total_budget : 0.15 * total_budget)),
      static_cast<int>(std::llround(gt.budget.count(ContextBand::Project) ? gt.budget.at(ContextBand::Project) * total_budget : 0.25 * total_budget)),
      static_cast<int>(std::llround(gt.budget.count(ContextBand::Goal) ? gt.budget.at(ContextBand::Goal) * total_budget : 0.6 * total_budget))};
  std::array<int, 3> used = {0, 0, 0};

  std::vector<model::ContextItem> accepted;
  std::vector<model::ContextItem> dropped;
  std::map<std::string, std::size_t> accepted_index;  // ref -> index in `accepted`

  auto finalize_text = [&](Candidate& c, Resolution res) {
    std::string text = c.content.pick(res);
    if (c.is_principle) {
      c.rendered_text = text;
    } else {
      c.rendered_text = ctx::evidence_markdown(*pack_, c.evidence, c.origin, c.confidence, text, c.expected, c.basis, c.fill_query);
    }
    c.tokens = estimate_tokens(c.rendered_text);
  };

  auto try_accept = [&](Candidate c) {
    Resolution res = resolution_for(gt, c.role);
    finalize_text(c, res);
    int bi = static_cast<int>(c.band);
    for (int j = bi; j < 3; ++j) {
      int room = band_budget[static_cast<std::size_t>(j)] - used[static_cast<std::size_t>(j)];
      if (c.tokens <= room || (room <= 0 && c.tokens == 0)) {
        used[static_cast<std::size_t>(j)] += c.tokens;
        model::ContextItem item;
        item.ref_kind = c.ref_kind;
        item.ref = c.ref;
        item.band = static_cast<ContextBand>(j);
        item.resolution = res;
        item.score = c.score;
        item.factors = Json{{"relevance", c.base_relevance}, {"authority", ctx::authority_score(c.origin)},
                            {"freshness", ctx::freshness_score(c.date, anchor_date)}, {"confidence", c.confidence}};
        item.tokens = c.tokens;
        item.why = c.why;
        item.text = c.rendered_text;
        accepted_index[item.ref] = accepted.size();
        accepted.push_back(std::move(item));
        return true;
      }
    }
    model::ContextItem d;
    d.ref_kind = c.ref_kind;
    d.ref = c.ref;
    d.band = c.band;
    d.resolution = res;
    d.score = c.score;
    d.tokens = c.tokens;
    d.why = "over budget (needed " + std::to_string(c.tokens) + " tokens, none of the remaining bands had room)";
    dropped.push_back(std::move(d));
    return false;
  };

  // Map from a ref back to its premises, so the dependency closure can add
  // `required_by` even after the candidate objects themselves are gone.
  std::map<std::string, std::pair<std::vector<std::string>, std::vector<std::string>>> premises_of;
  for (auto& c : stable_d) premises_of[c.ref] = {c.premise_claims, c.premise_principles};
  for (auto& c : project_d) premises_of[c.ref] = {c.premise_claims, c.premise_principles};
  for (auto& c : goal_d) premises_of[c.ref] = {c.premise_claims, c.premise_principles};

  for (auto& c : stable_d) try_accept(std::move(c));
  for (auto& c : project_d) try_accept(std::move(c));
  for (auto& c : goal_d) try_accept(std::move(c));

  // ── dependency closure: premises of accepted items are pulled in too ──
  std::set<std::string> expanded;
  for (int round = 0; round < 3; ++round) {
    std::vector<std::pair<std::string, std::string>> to_pull;  // (premise ref, puller ref)
    for (auto& [ref, idx] : accepted_index) {
      if (!expanded.insert(ref).second) continue;
      auto it = premises_of.find(ref);
      if (it == premises_of.end()) continue;
      for (auto& pc : it->second.first) to_pull.push_back({pc, ref});
      for (auto& pp : it->second.second) to_pull.push_back({pp, ref});
    }
    if (to_pull.empty()) break;
    bool any_new = false;
    for (auto& [pref, puller] : to_pull) {
      auto exist = accepted_index.find(pref);
      if (exist != accepted_index.end()) {
        auto& v = accepted[exist->second].required_by;
        if (std::find(v.begin(), v.end(), puller) == v.end()) v.push_back(puller);
        continue;
      }
      // Resolve the premise: a claim id ("cl_...") or a principle id.
      Candidate c;
      bool ok = false;
      auto claim_r = store_.get_claim(run, pref);
      if (claim_r && *claim_r) {
        const auto& claim = **claim_r;
        c.ref_kind = model::RefKind::Claim;
        c.ref = claim.id;
        c.band = ContextBand::Goal;
        c.subject = claim.subject;
        c.date = claim.qualifiers.valid_from;
        c.origin = claim.assessment.origin;
        c.confidence = claim.assessment.confidence;
        c.evidence = claim.assessment.evidence;
        c.expected = claim.assessment.expected;
        std::string subj_label = label_of(claim.subject);
        std::string obj_label = claim.object.empty() ? fmt_value(claim.value) : label_of(claim.object);
        c.content.label = subj_label + " " + claim.predicate + " " + obj_label;
        c.content.summary = c.content.label;
        c.content.full = c.content.summary;
        c.premise_claims = claim.assessment.premises.claims;
        c.premise_principles = claim.assessment.premises.principles;
        ok = true;
      } else {
        auto pr = store_.get_principle(run, pref);
        if (pr && *pr) {
          const auto& p = **pr;
          c.ref_kind = model::RefKind::Principle;
          c.ref = p.id;
          c.band = ContextBand::Project;
          c.is_principle = true;
          c.validation = p.validation;
          c.origin = p.origin;
          c.confidence = p.confidence;
          std::string stmt = ctx::pick_text(p.statement, lang);
          c.content.label = stmt;
          c.content.summary = stmt + " " + ctx::principle_badge(p.validation, p.level, p.form);
          c.content.full = c.content.summary;
          ok = true;
        }
      }
      if (!ok) continue;
      c.subject = c.subject.empty() ? c.ref : c.subject;
      c.why = "required premise of " + puller;
      c.base_relevance = 1.0;
      c.score = c.base_relevance * ctx::authority_score(c.origin) * ctx::freshness_score(c.date, anchor_date) * c.confidence;
      premises_of[c.ref] = {c.premise_claims, c.premise_principles};
      if (try_accept(std::move(c))) {
        accepted.back().required_by.push_back(puller);
        any_new = true;
      }
    }
    if (!any_new) break;
  }

  // Final order: band, then score desc, then ref (header contract).
  std::sort(accepted.begin(), accepted.end(), [](const model::ContextItem& a, const model::ContextItem& b) {
    if (a.band != b.band) return a.band < b.band;
    if (a.score != b.score) return a.score > b.score;
    return a.ref < b.ref;
  });
  std::sort(dropped.begin(), dropped.end(), [](const model::ContextItem& a, const model::ContextItem& b) {
    if (a.band != b.band) return a.band < b.band;
    if (a.score != b.score) return a.score > b.score;
    return a.ref < b.ref;
  });

  model::ContextSet set;
  set.goal = goal;
  set.budget_tokens = total_budget;
  set.used_tokens = 0;
  for (auto& it : accepted) set.used_tokens += it.tokens;
  set.pack_hash = pack_->hash();
  set.id = model::ContextSet::make_id(goal.id, total_budget, set.pack_hash);
  set.items = std::move(accepted);
  set.dropped = std::move(dropped);
  return set;
}

// ── rendering ────────────────────────────────────────────────────────

namespace {
std::string band_heading(ContextBand b, std::string_view lang) {
  bool pl = lang == "pl";
  switch (b) {
    case ContextBand::Stable:
      return pl ? "## Stałe (konstytucja, niezmienniki, preferencje)" : "## Stable (constitution, invariants, preferences)";
    case ContextBand::Project:
      return pl ? "## Kontekst projektu" : "## Project context";
    case ContextBand::Goal:
      return pl ? "## Ten cel" : "## This goal";
  }
  return "## Context";
}
}  // namespace

Result<std::string> ContextEngine::render(const model::ContextSet& set) {
  std::string out;
  ContextBand current = ContextBand::Stable;
  bool have_current = false;
  bool any = false;
  for (const auto& item : set.items) {
    if (!have_current || item.band != current) {
      current = item.band;
      have_current = true;
      if (any) out += "\n\n";
      out += band_heading(current, set.goal.params.contains("lang") ? "" : "") + "\n";
      any = true;
    }
    out += "- " + item.text;
    if (!item.why.empty()) out += "  <!-- why: " + item.why + " -->";
    out += "\n";
  }
  if (!set.dropped.empty()) {
    out += "\n<!-- " + std::to_string(set.dropped.size()) + " item(s) considered but not included (budget); see trace() -->\n";
  }
  return out;
}

Json ContextEngine::trace(const model::ContextSet& set) const {
  std::map<ContextBand, Json> sections;
  for (const auto& item : set.items) {
    Json& arr = sections[item.band];
    if (arr.is_null()) arr = Json::array();
    arr.push_back(item.to_json());
  }
  Json out_sections = Json::array();
  for (auto band : {ContextBand::Stable, ContextBand::Project, ContextBand::Goal}) {
    Json items = sections.count(band) ? sections[band] : Json::array();
    out_sections.push_back(Json{{"band", std::string(model::to_string(band))}, {"items", items}});
  }
  Json dropped = Json::array();
  for (const auto& d : set.dropped) dropped.push_back(d.to_json());
  return Json{{"goal", set.goal.to_json()},
              {"budget_tokens", set.budget_tokens},
              {"used_tokens", set.used_tokens},
              {"sections", out_sections},
              {"dropped", dropped}};
}

Result<Json> ContextEngine::build(const ContextRequest& req) {
  LOOM_TRY_ASSIGN(model::ContextSet set, select(req));
  LOOM_TRY_ASSIGN(std::string prompt, render(set));
  return Json{{"goal", set.goal.to_json()}, {"context_set", set.to_json()}, {"prompt", prompt}};
}

}  // namespace loom::context
