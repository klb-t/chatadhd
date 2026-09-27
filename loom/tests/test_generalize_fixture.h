// TEST-ONLY fixture-to-evidence loader for the generalize area.
//
// The extract/resolve areas are not available in this worktree, so the
// synthetic_dev corpus (tests/fixtures/eval/synthetic_dev, a FICTIONAL
// persona) is turned into a generalize::Evidence directly, the way those
// areas would hand it over — and nothing more:
//   * observations: every message of the RELEVANT conversations (what the
//     catalog selects), with speaker, date, ordinal and a locator;
//   * entities: the five projects (resolution output);
//   * claims: `mentioned_in` (project -> unit), observed role quotes of the
//     ground truth mapped to the relation of a domain kind of that role,
//     `has_question` for the open questions, `has_version` for versions,
//     and one `decides` claim per decision supported by its message;
//   * decisions: alternatives + chosen + date + supersession.
// The answer key is NOT leaked: decisions carry no principle / operator ids,
// principles are not given at all (discovery reads the messages), and the
// project kind is never stated (matching uses the pack's cue anchors).
#pragma once

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../third_party/miniz/miniz.h"
#include "loom/generalize.h"
#include "loom/kb.h"
#include "loom/model.h"
#include "loom/util/json.h"

namespace loom::test::gfix {

inline std::filesystem::path dir() { return std::filesystem::path(LOOM_TEST_FIXTURES) / "eval" / "synthetic_dev"; }

inline std::string read_file(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

inline std::string zip_member(const std::filesystem::path& zip, const char* name) {
  mz_zip_archive z{};
  if (!mz_zip_reader_init_file(&z, zip.string().c_str(), 0)) return "";
  size_t n = 0;
  void* p = mz_zip_reader_extract_file_to_heap(&z, name, &n, 0);
  std::string out;
  if (p) {
    out.assign(static_cast<const char*>(p), n);
    mz_free(p);
  }
  mz_zip_reader_end(&z);
  return out;
}

inline std::string iso_from_epoch(double t) {
  std::time_t s = static_cast<std::time_t>(t);
  std::tm tm{};
  gmtime_r(&s, &tm);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

struct Msg {
  std::string node, role, text, date;
};

struct Fixture {
  Json gt;
  generalize::Evidence ev;
  std::map<std::string, std::string> obs_of;       // "conv|node" -> observation id
  std::map<std::string, std::string> loc_of_obs;   // observation id -> "conv|node"
  std::map<std::string, std::string> project_entity;  // proj.x -> e_
  std::map<std::string, std::string> decision_id;     // dec.x -> cl_ (decision id)
  std::map<std::string, std::string> gt_decision;     // cl_ -> dec.x
  std::map<std::string, std::string> kind_of;         // proj.x -> gt kind
};

inline std::string project_kind_for(const std::string& gt_kind) {
  if (gt_kind == "legal_case" || gt_kind == "music" || gt_kind == "film" || gt_kind == "research") return gt_kind;
  return "software_app";  // multiplatform_app / pipeline / agent_system = software_app + facet
}

inline Fixture load(const kb::Pack& pack) {
  Fixture fx;
  kb::Normalizer norm(pack);
  fx.gt = json::parse(read_file(dir() / "ground_truth.json")).value_or(Json::object());
  std::map<std::string, std::vector<Msg>> convs;
  {
    Json gpt = json::parse(zip_member(dir() / "chatgpt_export.zip", "conversations.json")).value_or(Json::array());
    for (const auto& c : gpt) {
      std::vector<std::pair<double, Msg>> v;
      for (const auto& [k, n] : c["mapping"].items()) {
        const Json* m = json::find(n, "message");
        if (!m || m->is_null()) continue;
        std::string text;
        for (const auto& p : (*m)["content"]["parts"]) {
          if (p.is_string()) text += (text.empty() ? "" : " ") + p.get<std::string>();
        }
        double t = json::get_number(*m, "create_time", 0);
        v.push_back({t, Msg{json::get_string(*m, "id"), json::get_string((*m)["author"], "role"), text, iso_from_epoch(t)}});
      }
      std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second.node < b.second.node;
      });
      auto& out = convs[json::get_string(c, "id")];
      for (auto& [t, m] : v) out.push_back(m);
    }
    Json cl = json::parse(zip_member(dir() / "claude_export.zip", "conversations.json")).value_or(Json::array());
    for (const auto& c : cl) {
      auto& out = convs[json::get_string(c, "uuid")];
      for (const auto& m : c["chat_messages"]) {
        out.push_back(Msg{json::get_string(m, "uuid"), json::get_string(m, "sender"), json::get_string(m, "text"), json::get_string(m, "created_at")});
      }
    }
  }
  // Projects.
  for (const auto& p : fx.gt["projects"]) {
    model::Entity e;
    e.kind = "project";
    e.label = json::get_string(p, "name");
    e.canonical_key = norm.phrase_key(e.label);
    e.id = model::Entity::make_id(e.kind, e.canonical_key);
    e.labels["en"] = e.label;
    for (const auto& a : p["aliases"]) {
      std::string k = norm.phrase_key(a.get<std::string>());
      if (!k.empty()) e.aliases.push_back(model::Alias{k, a.get<std::string>(), "", "mined", 1, 1.0});
    }
    fx.project_entity[json::get_string(p, "id")] = e.id;
    fx.kind_of[json::get_string(p, "id")] = json::get_string(p, "kind");
    fx.ev.entities.push_back(e);
  }
  // Observations of the relevant (selected) conversations.
  std::map<std::string, std::string> first_obs;  // conv -> first observation
  for (const auto& u : fx.gt["units"]["relevant"]) {
    std::string conv = json::get_string(u, "conv_id");
    std::string unit = "un_fixture_" + conv;
    int ord = 0;
    for (const auto& m : convs[conv]) {
      model::Observation o;
      o.unit = unit;
      o.kind = model::ObservationKind::Utterance;
      o.text = m.text;
      o.locator.source = "sha256:synthetic_dev/" + json::get_string(u, "provider");
      o.locator.member = "conversations.json";
      o.locator.json_pointer = "/" + conv + "/" + m.node;
      o.date = m.date;
      o.ordinal = ord++;
      o.speaker = m.role;
      o.artifact_type = "conversation";
      o.id = model::Observation::make_id(unit, o.locator, o.text);
      fx.obs_of[conv + "|" + m.node] = o.id;
      fx.loc_of_obs[o.id] = conv + "|" + m.node;
      if (!first_obs.count(conv)) first_obs[conv] = o.id;
      fx.ev.observations.push_back(o);
    }
    std::string proj = json::get_string(u, "project");
    if (!proj.empty() && fx.project_entity.count(proj) && first_obs.count(conv)) {
      model::Claim c;
      c.subject = fx.project_entity[proj];
      c.predicate = "mentioned_in";
      c.value = unit;
      c.assessment.support.push_back(model::Support{first_obs[conv], {}, "", "fixture@1", 1.0});
      c.assessment.confidence = 0.9;
      c.id = model::Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
      fx.ev.claims.push_back(c);
    }
  }
  auto obs_at = [&](const Json& loc) -> std::string {
    auto it = fx.obs_of.find(json::get_string(loc, "conv_id") + "|" + json::get_string(loc, "node_id"));
    return it == fx.obs_of.end() ? std::string() : it->second;
  };
  auto observed = [&](const std::string& subject, const std::string& predicate, const Json& value, const std::string& ob,
                      const std::string& date) {
    model::Claim c;
    c.subject = subject;
    c.predicate = predicate;
    c.value = value;
    c.qualifiers.valid_from = date.substr(0, 10);
    c.assessment.support.push_back(model::Support{ob, {}, value.is_string() ? value.get<std::string>() : "", "fixture@1", 1.0});
    c.assessment.confidence = 0.8;
    c.id = model::Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
    return c;
  };
  for (const auto& p : fx.gt["projects"]) {
    std::string pid = json::get_string(p, "id");
    std::string subj = fx.project_entity[pid];
    auto pk = model::project_kind(pack, project_kind_for(json::get_string(p, "kind")));
    // Observed role quotes -> the relation of a domain kind of that role.
    for (const auto& [role, v] : p["universal_roles"].items()) {
      std::string rel;
      if (pk) {
        for (const auto& dk : pk->domain_kinds) {
          if (model::to_string(dk.role) == role && dk.entity_kind.empty() == true && !dk.relation.empty()) {
            rel = dk.relation;
            break;
          }
        }
        if (rel.empty()) {
          for (const auto& dk : pk->domain_kinds) {
            if (model::to_string(dk.role) == role && !dk.relation.empty()) {
              rel = dk.relation;
              break;
            }
          }
        }
      }
      if (rel.empty()) continue;
      for (const auto& o : v["observed"]) {
        std::string ob = obs_at(o);
        if (!ob.empty()) fx.ev.claims.push_back(observed(subj, rel, json::get_string(o, "quote"), ob, json::get_string(o, "date")));
      }
    }
    // Versions (dated; supported by the project's earliest message).
    std::string first;
    for (const auto& u : fx.gt["units"]["relevant"]) {
      if (json::get_string(u, "project") == pid && first_obs.count(json::get_string(u, "conv_id"))) {
        first = first_obs[json::get_string(u, "conv_id")];
        break;
      }
    }
    if (!first.empty()) {
      for (const auto& ver : p["versions"]) {
        auto c = observed(subj, "has_version", json::get_string(ver, "version"), first, json::get_string(ver, "date"));
        if (json::get_string(ver, "source") == "repo") c.assessment.origin = model::Origin::Repo;
        c.id = model::Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
        fx.ev.claims.push_back(c);
      }
    }
    // Decisions.
    for (const auto& d : p["decisions"]) {
      std::string ob = obs_at(d["unit"]);
      if (ob.empty()) continue;
      std::string chosen = d["chosen"].is_string() ? d["chosen"].get<std::string>() : "";
      Json value = chosen.empty() ? Json{{"kept_open", d["alternatives"]}} : Json(chosen);
      auto c = observed(subj, "decides", value, ob, json::get_string(d, "date"));
      fx.ev.claims.push_back(c);
      fx.decision_id[json::get_string(d, "id")] = c.id;
      fx.gt_decision[c.id] = json::get_string(d, "id");
      if (chosen.empty()) continue;  // kept open: a `decides` claim only (a Decision has one chosen alternative)
      model::Decision dec;
      dec.id = c.id;
      dec.subject = subj;
      dec.date = json::get_string(d, "date");
      for (const auto& a : d["alternatives"]) {
        model::DecisionAlternative alt;
        alt.label = a.get<std::string>();
        alt.chosen = alt.label == chosen;
        dec.alternatives.push_back(alt);
      }
      if (!dec.chosen()) dec.alternatives.push_back(model::DecisionAlternative{chosen, "", Json(), {}, true});
      fx.decision_id[json::get_string(d, "id")] = dec.id;
      fx.gt_decision[dec.id] = json::get_string(d, "id");
      fx.ev.decisions.push_back(dec);
    }
  }
  // Supersession.
  for (const auto& p : fx.gt["projects"]) {
    for (const auto& d : p["decisions"]) {
      if (!d["supersedes"].is_string()) continue;
      std::string older = fx.decision_id[d["supersedes"].get<std::string>()];
      for (auto& dec : fx.ev.decisions) {
        if (dec.id == older) {
          dec.status = model::DecisionStatus::Superseded;
          dec.superseded_by = fx.decision_id[json::get_string(d, "id")];
        }
      }
    }
  }
  // Open questions.
  for (const auto& q : fx.gt["open_questions"]) {
    std::string subj = fx.project_entity[json::get_string(q, "project")];
    for (const auto& u : q["units"]) {
      std::string ob = obs_at(u);
      if (ob.empty()) continue;
      fx.ev.claims.push_back(observed(subj, "has_question", json::get_string(u, "quote"), ob, json::get_string(u, "date")));
      break;
    }
  }
  auto sort_id = [](auto& v) { std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.id < b.id; }); };
  sort_id(fx.ev.entities);
  sort_id(fx.ev.observations);
  sort_id(fx.ev.claims);
  fx.ev.claims.erase(std::unique(fx.ev.claims.begin(), fx.ev.claims.end(), [](const auto& a, const auto& b) { return a.id == b.id; }),
                     fx.ev.claims.end());
  sort_id(fx.ev.decisions);
  return fx;
}

}  // namespace loom::test::gfix
