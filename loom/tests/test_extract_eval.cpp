// Extraction (+ resolution) measured on the synthetic, fictional eval corpus
// tests/fixtures/eval/synthetic_dev (ground_truth.json). Reports entity and
// alias F1, version and status accuracy, decision/fork recall and area
// recall; the floors below are regression gates, the printed numbers are the
// measurement (LOOM_EXTRACT_EVAL_VERBOSE=1 prints the misses).
#include <doctest/doctest.h>

#include <cstdlib>
#include <iostream>
#include <map>
#include <set>

#include "loom/extract.h"
#include "loom/kb.h"
#include "loom/resolve.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {

const fs::path kCorpus = fs::path(LOOM_TEST_FIXTURES) / "eval" / "synthetic_dev";

bool verbose() {
  const char* v = std::getenv("LOOM_EXTRACT_EVAL_VERBOSE");
  return v && *v && std::string(v) != "0";
}

struct Loc {
  std::string conv;
  std::string node;
  std::string date;
};

struct Eval {
  std::shared_ptr<const kb::Pack> pack;
  std::vector<extract::UnitContent> units;
  extract::Extraction ex;
  resolve::ResolveResult res;
  std::vector<model::Claim> claims;  // remapped
  Json gt;
  std::map<std::string, Loc> obs_loc;         // observation id -> locator
  std::map<std::string, std::string> unit_conv;  // unit id -> conv ext id
  std::map<std::string, std::string> conv_project;  // GT conv -> project

  Eval() {
    pack = unwrap(kb::Pack::load_builtin());
    for (const char* z : {"chatgpt_export.zip", "claude_export.zip"}) {
      for (auto& u : unwrap(extract::read_units(kCorpus / z))) units.push_back(std::move(u));
    }
    for (const auto& u : units) unit_conv[u.unit.id] = json::get_string(u.unit.attrs, "ext_id");
    ex = extract::extract_units(pack, units);
    for (const auto& o : ex.observations) {
      obs_loc[o.id] = Loc{unit_conv[o.unit], json::get_string(o.attrs, "node"), o.date};
    }
    resolve::Resolver r(pack);
    res = unwrap(r.resolve(ex.entities, ex.observations));
    claims = r.apply_remap(ex.claims, res);
    gt = unwrap(json::parse(unwrap(fsutil::read_file(kCorpus / "ground_truth.json"))));
    for (const auto& u : gt["units"]["relevant"]) conv_project[u["conv_id"]] = u["project"];
  }
  std::set<std::string> convs_of(const model::Claim& c) const {
    std::set<std::string> s;
    for (const auto& sp : c.assessment.support) {
      auto it = obs_loc.find(sp.observation);
      if (it != obs_loc.end()) s.insert(it->second.conv);
    }
    return s;
  }
  bool supported_at(const model::Claim& c, const std::string& conv, const std::string& node) const {
    for (const auto& sp : c.assessment.support) {
      auto it = obs_loc.find(sp.observation);
      if (it != obs_loc.end() && it->second.conv == conv && (node.empty() || it->second.node == node)) return true;
    }
    return false;
  }
};

Eval& eval() {
  static Eval e;
  return e;
}

double f1(double p, double r) { return p + r > 0 ? 2 * p * r / (p + r) : 0.0; }

}  // namespace

TEST_SUITE("extract_eval") {
  TEST_CASE("synthetic_dev: entities and aliases (after resolution)") {
    auto& E = eval();
    kb::Normalizer norm(*E.pack);
    // predicted projects: canonical entities of kind project that the corpus mentions
    std::vector<const model::Entity*> projects;
    for (const auto& e : E.res.entities) {
      if (e.kind == "project" && e.status == model::ClaimStatus::Active) projects.push_back(&e);
    }
    int matched = 0;
    int alias_tp = 0, alias_pred = 0, alias_gt = 0;
    std::set<std::string> used;
    for (const auto& p : E.gt["projects"]) {
      std::set<std::string> gkeys;
      for (const auto& a : p["aliases"]) gkeys.insert(norm.phrase_key(a.get<std::string>()));
      gkeys.insert(norm.phrase_key(p["name"].get<std::string>()));
      alias_gt += static_cast<int>(gkeys.size());
      const model::Entity* best = nullptr;
      int best_ov = 0;
      for (const auto* e : projects) {
        int ov = 0;
        for (const auto& a : e->aliases) ov += gkeys.count(a.key) ? 1 : 0;
        ov += gkeys.count(e->canonical_key) && !std::any_of(e->aliases.begin(), e->aliases.end(), [&](const model::Alias& a) { return a.key == e->canonical_key; }) ? 1 : 0;
        if (ov > best_ov) {
          best_ov = ov;
          best = e;
        }
      }
      if (best && !used.count(best->id)) {
        used.insert(best->id);
        ++matched;
        std::set<std::string> pk;
        for (const auto& a : best->aliases) pk.insert(a.key);
        alias_pred += static_cast<int>(pk.size());
        for (const auto& k : pk) alias_tp += gkeys.count(k) ? 1 : 0;
      }
      if (verbose()) MESSAGE(p["id"].get<std::string>() << " -> " << (best ? best->label : "(none)"));
    }
    // precision counts only projects supported by the corpus (not the pack's own profile entries)
    double ep = projects.empty() ? 0 : static_cast<double>(matched) / static_cast<double>(projects.size());
    double er = static_cast<double>(matched) / static_cast<double>(E.gt["projects"].size());
    double ap = alias_pred ? static_cast<double>(alias_tp) / alias_pred : 0;
    double ar = alias_gt ? static_cast<double>(alias_tp) / alias_gt : 0;
    if (verbose()) {
      for (const auto* e : projects) {
        std::string al;
        for (const auto& a : e->aliases) al += a.surface + "|";
        MESSAGE("project " << e->label << " aliases " << al);
      }
    }
    MESSAGE("project entity P/R/F1 = " << ep << " / " << er << " / " << f1(ep, er) << "  (" << matched << " of "
                                       << E.gt["projects"].size() << ", predicted " << projects.size() << ")");
    MESSAGE("alias P/R/F1 = " << ap << " / " << ar << " / " << f1(ap, ar));
    CHECK(matched >= 3);
    CHECK(f1(ap, ar) >= 0.25);
  }

  TEST_CASE("synthetic_dev: versions") {
    auto& E = eval();
    // GT versions mentioned in chat: found as a has_version value supported in a unit of that project
    std::map<std::string, std::set<std::string>> found;  // project -> versions
    std::map<std::string, std::set<std::string>> gtv;
    for (const auto& p : E.gt["projects"]) {
      for (const auto& v : p["versions"]) {
        if (json::get_string(v, "source").find("chat") != std::string::npos) {
          gtv[p["id"]].insert(kb::normalize_version(v["version"].get<std::string>()));
        }
      }
    }
    int pred = 0, tp = 0;
    for (const auto& c : E.claims) {
      if (c.predicate != "has_version" || !c.value.is_string()) continue;
      for (const auto& conv : E.convs_of(c)) {
        auto it = E.conv_project.find(conv);
        if (it == E.conv_project.end()) continue;
        ++pred;
        std::string v = c.value.get<std::string>();
        if (gtv[it->second].count(v)) {
          ++tp;
          found[it->second].insert(v);
        } else if (verbose()) {
          MESSAGE("extra version " << v << " in " << conv);
        }
      }
    }
    int total = 0, hit = 0;
    for (const auto& [p, vs] : gtv) {
      for (const auto& v : vs) {
        ++total;
        if (found[p].count(v)) ++hit;
        else if (verbose()) MESSAGE("missed version " << p << " " << v);
      }
    }
    double prec = pred ? static_cast<double>(tp) / pred : 0;
    double rec = total ? static_cast<double>(hit) / total : 0;
    MESSAGE("version precision/recall = " << prec << " / " << rec << "  (" << hit << "/" << total << ")");
    CHECK(rec >= 0.5);
    CHECK(prec >= 0.7);
  }

  TEST_CASE("synthetic_dev: status per branch and version") {
    auto& E = eval();
    std::map<std::string, const model::Claim*> by_id;
    for (const auto& c : E.claims) by_id[c.id] = &c;
    std::map<std::string, std::string> claim_remap;  // old -> new ids via content
    int events = 0, status_ok = 0, version_ok = 0;
    for (const auto& p : E.gt["projects"]) {
      for (const auto& f : p["features_status"]) {
        for (const auto& ev : f["events"]) {
          // the unit of the event: a listed unit with the same date
          std::string date = ev["date"];
          std::string conv;
          for (const auto& u : f["units"]) {
            if (json::get_string(u, "date").substr(0, 10) == date) conv = u["conv_id"];
          }
          if (conv.empty()) continue;
          ++events;
          bool s_ok = false, v_ok = false;
          for (const auto& c : E.claims) {
            if (c.predicate != "has_status" || !E.convs_of(c).count(conv)) continue;
            if (c.value == ev["status"]) {
              s_ok = true;
              std::string gv = ev["version"];
              if (gv == "n/a" || kb::normalize_version(gv) == c.qualifiers.version) v_ok = true;
            }
          }
          status_ok += s_ok;
          version_ok += v_ok;
          if (verbose() && !v_ok) MESSAGE("status miss " << f["id"].get<std::string>() << " " << ev["status"].get<std::string>() << "@" << ev["version"].get<std::string>() << " in " << conv);
        }
      }
    }
    double acc = events ? static_cast<double>(status_ok) / events : 0;
    double vacc = events ? static_cast<double>(version_ok) / events : 0;
    MESSAGE("status accuracy = " << acc << ", status+version accuracy = " << vacc << "  (" << events << " located events)");
    // lost/restored are recorded, with oscillation
    bool lost = false, restored = false;
    for (const auto& s : E.ex.statuses) {
      lost = lost || s.status == model::StatusValue::Lost;
      restored = restored || s.status == model::StatusValue::Restored;
    }
    CHECK(lost);
    CHECK(restored);
    CHECK(acc >= 0.4);
  }

  TEST_CASE("synthetic_dev: decisions and forks") {
    auto& E = eval();
    int total = 0, conv_hit = 0, node_hit = 0, alt_hit = 0;
    for (const auto& p : E.gt["projects"]) {
      for (const auto& d : p["decisions"]) {
        if (!d.contains("unit")) continue;
        ++total;
        std::string conv = d["unit"]["conv_id"], node = d["unit"]["node_id"];
        bool ch = false, nh = false, ah = false;
        for (const auto& dec : E.ex.decisions) {
          const model::Claim* c = nullptr;
          for (const auto& x : E.ex.claims) {
            if (x.id == dec.id) c = &x;
          }
          if (!c) continue;
          if (E.supported_at(*c, conv, "")) {
            ch = true;
            if (E.supported_at(*c, conv, node)) nh = true;
            if (dec.alternatives.size() >= 2) ah = true;
          }
        }
        conv_hit += ch;
        node_hit += nh;
        alt_hit += ah;
        if (verbose() && !ch) MESSAGE("missed decision " << d["id"].get<std::string>() << " in " << conv);
      }
    }
    MESSAGE("decision recall (unit) = " << static_cast<double>(conv_hit) / total << ", (message) = "
                                        << static_cast<double>(node_hit) / total << ", with recorded alternatives = "
                                        << static_cast<double>(alt_hit) / total << "  (" << total << ")");
    // forks: every ChatGPT/Claude sibling-branch conversation in the corpus
    std::set<std::string> gt_forks;
    for (const auto& [k, v] : E.gt["fork_chat_units"].items()) {
      for (const auto& u : v) gt_forks.insert(u["conv_id"]);
    }
    for (const auto& u : E.units) {
      // structural forks: the export's own sibling nodes (ground truth by construction)
      const Json& s = u.structured;
      bool sib = false;
      if (const Json* m = json::find(s, "chat_messages"); m && m->is_array()) {
        for (const auto& x : *m) sib = sib || !json::get_string(x, "parent_message_uuid").empty();
      }
      if (const Json* m = json::find(s, "mapping"); m && m->is_object()) {
        for (auto it = m->begin(); it != m->end(); ++it) {
          if (const Json* ch = json::find(it.value(), "children"); ch && ch->is_array() && ch->size() >= 2) sib = true;
        }
      }
      if (sib) gt_forks.insert(json::get_string(u.unit.attrs, "ext_id"));
    }
    int fh = 0;
    for (const auto& conv : gt_forks) {
      bool hit = false;
      for (const auto& f : E.ex.forks) {
        for (const auto& s : f.sides) {
          auto it = E.obs_loc.find(s.ref);
          if (it != E.obs_loc.end() && it->second.conv == conv) hit = true;
        }
      }
      fh += hit;
      if (verbose() && !hit) MESSAGE("missed fork in " << conv);
    }
    bool design = false;
    for (const auto& f : E.ex.forks) design = design || f.kind == model::ForkKind::Design;
    MESSAGE("fork recall = " << static_cast<double>(fh) / gt_forks.size() << "  (" << gt_forks.size()
                             << " forked conversations), design fork found = " << design);
    CHECK(static_cast<double>(conv_hit) / total >= 0.4);
    CHECK(fh == static_cast<int>(gt_forks.size()));
    CHECK(design);
  }

  TEST_CASE("synthetic_dev: areas (R10)") {
    auto& E = eval();
    kb::Normalizer norm(*E.pack);
    int total = 0, hit = 0, members_gt = 0, members_hit = 0;
    for (const auto& a : E.gt["areas"]) {
      ++total;
      std::string conv = a["unit"]["conv_id"], node = a["unit"]["node_id"];
      const model::Area* found = nullptr;
      for (const auto& ar : E.ex.areas) {
        auto it = E.obs_loc.find(ar.observation);
        if (it != E.obs_loc.end() && it->second.conv == conv && it->second.node == node) found = &ar;
      }
      hit += found != nullptr;
      for (const auto& m : a["listed_members"]) {
        ++members_gt;
        if (!found) continue;
        auto gk = norm.phrase_key(m.get<std::string>(), true);
        std::set<std::string> gset;
        for (std::size_t s = 0, e; s < gk.size(); s = e + 1) {
          e = gk.find(' ', s);
          if (e == std::string::npos) e = gk.size();
          gset.insert(gk.substr(s, e - s));
        }
        bool mh = false;
        for (const auto& item : E.ex.items) {
          if (item.area != found->id) continue;
          auto ik = norm.phrase_key(item.text, true);
          for (const auto& t : gset) mh = mh || (" " + ik + " ").find(" " + t + " ") != std::string::npos;
        }
        members_hit += mh;
        if (verbose() && !mh) MESSAGE("missed member " << m.get<std::string>());
      }
      if (verbose() && !found) MESSAGE("missed area " << a["id"].get<std::string>());
    }
    MESSAGE("area recall = " << static_cast<double>(hit) / total << ", listed-member recall = "
                             << static_cast<double>(members_hit) / members_gt);
    CHECK(hit >= 4);
    for (const auto& ar : E.ex.areas) {
      CHECK(!ar.principle.empty());
      CHECK(ar.gap == ar.members.empty());
    }
  }

  TEST_CASE("synthetic_dev: noise traps stay out of the owner's projects") {
    auto& E = eval();
    // "loom" (weaving) and "ADHD" (a diagnosis) are context-gated profile aliases
    std::set<std::string> trap_convs;
    for (const auto& t : E.gt["units"]["noise_traps"]) trap_convs.insert(t["conv_id"]);
    int trap_projects = 0;
    for (const auto& c : E.ex.claims) {
      if (c.predicate != "mentioned_in") continue;
      for (const auto& conv : E.convs_of(c)) {
        if (!trap_convs.count(conv)) continue;
        for (const auto& e : E.ex.entities) {
          if (e.id == c.subject && e.kind == "project" && (e.label == "Loom" || e.label == "ChatADHD")) ++trap_projects;
        }
      }
    }
    CHECK(trap_projects == 0);
  }
}
