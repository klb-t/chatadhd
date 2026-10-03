// The data pack read through the conceptual model (model.h views): every
// file parses into model types, every domain kind anchors on exactly one
// universal role, every domain relation anchors on a role relation, transfer
// morphisms link kinds of the same role, the seed priors are dated
// candidates and can be cut at a date (temporal holdout).
#include <doctest/doctest.h>

#include <set>

#include "loom/kb.h"
#include "loom/model.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::model;
using loom::test::unwrap;

namespace {
std::shared_ptr<const kb::Pack> pack() { return unwrap(kb::Pack::load_builtin()); }
}  // namespace

TEST_SUITE("pack_model") {
  TEST_CASE("every project kind, facet and artifact type parses; every domain kind has exactly one anchoring morphism") {
    auto p = pack();
    auto am = unwrap(anchoring(*p));
    std::size_t kinds = 0;
    std::set<Role> roles_used;
    auto check_kinds = [&](const std::string& par, const std::vector<DomainKind>& ks, const std::vector<std::string>& hosts) {
      std::vector<ProjectKind> host_kinds;
      for (const auto& h : hosts) host_kinds.push_back(unwrap(project_kind(*p, h)));
      for (const auto& k : ks) {
        ++kinds;
        roles_used.insert(k.role);
        for (const auto& r : k.relations) {
          const DomainKind* target = nullptr;
          for (const auto& x : ks) {
            if (x.id == r.target) target = &x;
          }
          for (const auto& hk : host_kinds) {
            if (!target) target = hk.domain_kind(r.target);
          }
          INFO(par << "." << k.id << " -" << r.rel << "-> " << r.target);
          REQUIRE(target);
          CHECK(am.check(r.rel, k.role, target->role).empty());
        }
      }
    };
    for (const auto& id : p->ids("project_kinds")) {
      auto pk = unwrap(project_kind(*p, id));
      CHECK(pk.header.id == id);
      CHECK(pk.domain_kinds.size() >= 10);
      check_kinds(id, pk.domain_kinds, {});
    }
    for (const auto& id : p->ids("facets")) {
      auto f = unwrap(facet(*p, id));
      check_kinds(id, f.domain_kinds, f.applies_to);
    }
    for (const auto& id : p->ids("artifact_types")) CHECK(unwrap(artifact_type(*p, id)).header.id == id);
    auto anchors = unwrap(anchoring_morphisms(*p));
    CHECK(anchors.size() == kinds);
    std::set<std::string> ids;
    for (const auto& m : anchors) {
      CHECK(m.use == MorphismUse::Anchoring);
      CHECK(m.to.role.has_value());
      CHECK(ids.insert(m.from.paradigm + "." + m.from.kind).second);  // exactly one per kind
    }
    CHECK(unwrap(morphism(*p, "m.anchor.film.scene")).to.role == Role::Part);
    // Each project kind covers every universal role (the meta-model is complete per domain).
    for (const auto& id : p->ids("project_kinds")) {
      auto pk = unwrap(project_kind(*p, id));
      std::set<Role> used;
      for (const auto& k : pk.domain_kinds) used.insert(k.role);
      INFO(id);
      CHECK(used.size() == count<Role>());
    }
  }

  TEST_CASE("transfer morphisms: the check chain software -> film -> music -> legal, same roles on both ends") {
    auto p = pack();
    auto ms = unwrap(morphisms(*p));
    CHECK(ms.size() >= 4);
    for (const auto& m : ms) {
      CHECK(m.use == MorphismUse::Transfer);
      REQUIRE(m.expected);
      auto pa = unwrap(project_kind(*p, m.from.paradigm));
      auto pb = unwrap(project_kind(*p, m.to.paradigm));
      const DomainKind* a = pa.domain_kind(m.from.kind);
      const DomainKind* b = pb.domain_kind(m.to.kind);
      REQUIRE(a);
      REQUIRE(b);
      CHECK(a->role == b->role);
    }
    auto chain = [&](const std::string& id, const std::string& from, const std::string& to) {
      auto m = unwrap(morphism(*p, id));
      CHECK(m.from.paradigm + "." + m.from.kind == from);
      CHECK(m.to.paradigm + "." + m.to.kind == to);
    };
    chain("m.check.software_film", "software_app.test", "film.continuity_check");
    chain("m.check.film_music", "film.continuity_check", "music.mix_check");
    chain("m.check.software_legal", "software_app.test", "legal_case.citation_check");
  }

  TEST_CASE("paradigm content: film seeded from FilmStructure, music derived by morphisms, brainstorm areas") {
    auto p = pack();
    auto film = unwrap(project_kind(*p, "film"));
    for (const char* k : {"character", "location", "prop", "motif", "citation", "scene", "transition", "screenplay", "continuity_check"}) {
      INFO(k);
      CHECK(film.domain_kind(k));
    }
    auto music = unwrap(project_kind(*p, "music"));
    CHECK(music.header.origin == Origin::ModelKnowledge);
    CHECK(music.header.validation == ValidationStatus::Candidate);
    CHECK(music.header.derived_from.size() >= 5);
    for (const auto& m : music.header.derived_from) CHECK(unwrap(morphism(*p, m)).to.paradigm == "music");
    auto legal = unwrap(project_kind(*p, "legal_case"));
    REQUIRE(legal.domain_kind("deadline"));
    CHECK(legal.domain_kind("deadline")->role == Role::Event);
    CHECK(legal.domain_kind("legal_norm")->role == Role::Constraint);
    auto sw = unwrap(project_kind(*p, "software_app"));
    CHECK(sw.facets == std::vector<std::string>{"multiplatform", "agent", "staged_transformation"});
    auto bs = unwrap(artifact_type(*p, "brainstorm"));
    std::set<std::string> fields;
    for (const auto& s : bs.structure) fields.insert(s.name);
    CHECK(fields.count("areas"));
    CHECK(fields.count("generalizations"));
    CHECK(fields.count("items"));
    auto goals = std::vector<std::string>{"implement_part", "write_artifact", "extend_scene", "verify_claim", "compute_deadline",
                                          "brainstorm", "answer_question"};
    for (const auto& g : goals) CHECK(unwrap(goal_type(*p, g)).roles.size() >= 3);
  }

  TEST_CASE("seed principles and operators are dated candidate priors; priors_as_of cuts hindsight") {
    auto p = pack();
    auto all = unwrap(principles(*p));
    REQUIRE(all.size() >= 30);
    std::set<PrincipleLevel> levels;
    std::set<PrincipleForm> forms;
    for (const auto& x : all) {
      CHECK(x.validation == ValidationStatus::Candidate);
      CHECK(!earliest_source_date(x.sources).empty());
      levels.insert(x.level);
      forms.insert(x.form);
    }
    CHECK(levels.size() == count<PrincipleLevel>());
    CHECK(forms.size() == count<PrincipleForm>());
    auto ops = unwrap(pack_operators(*p));
    std::size_t design = 0;
    for (const auto& o : ops) design += o.is_rule() ? 0 : 1;
    CHECK(design >= 10);
    CHECK(unwrap(pack_operator(*p, "op.uncertainty_keep_alternatives")).principles.size() >= 2);
    CHECK(unwrap(pack_operator(*p, "r.storage_local_first")).is_rule());

    auto at = [&](const char* date) { return unwrap(principles(*p, PriorFilter::as_of_date(date))); };
    auto names = [](const std::vector<Principle>& v) {
      std::set<std::string> s;
      for (const auto& x : v) s.insert(x.id);
      return s;
    };
    auto early = names(at("2026-02-08"));
    auto mega = names(at("2026-09-16"));
    CHECK(at("2026-01-01").empty());
    CHECK(early.count("p.kod_ne_dane"));    // Sesja 2 (2026-01-23): "konfiguracja ... w zewnętrznych plikach"
    CHECK(early.count("p.edit_is_branch")); // Sesja 1 (2026-01-21)
    CHECK(!early.count("p.defer_decisions"));  // first stated in the MEGA MASTER
    CHECK(mega.count("p.defer_decisions"));
    CHECK(!mega.count("p.value.truth"));       // first stated in the note (2026-09-26)
    CHECK(early.size() < mega.size());
    CHECK(mega.size() < all.size());
    CHECK(unwrap(principles(*p, PriorFilter::none())).empty());
    auto ops_none = unwrap(pack_operators(*p, PriorFilter::none()));
    for (const auto& o : ops_none) CHECK(o.is_rule());  // rules are pack mechanics, not priors
    auto ops_early = unwrap(pack_operators(*p, PriorFilter::as_of_date("2026-02-08")));
    bool has_providers = false;
    for (const auto& o : ops_early) has_providers = has_providers || o.id == "op.new_source_extend_providers";
    CHECK(has_providers);  // Sesja 20 (2026-02-06): "pełną abstrakcję providerów"
  }

  TEST_CASE("preferences are user-owned principles with a product scope and checks") {
    auto p = pack();
    auto x = unwrap(principle(*p, "p.pref.ascii_icons"));
    CHECK(x.is_preference());
    CHECK(x.checks == std::vector<std::string>{"chk.emoji_in_ui"});
    CHECK(!unwrap(principle(*p, "p.kod_ne_dane")).is_preference());
    auto single = unwrap(principle(*p, "p.modular_code"));
    CHECK(single.supersedes == std::vector<std::string>{"p.pref.single_file"});
  }

  TEST_CASE("evidence encoding: origin channel keeps model knowledge visibly distinct; statuses include lost/restored") {
    auto p = pack();
    const Json& enc = p->policy("evidence_encoding");
    for (auto o : all<Origin>()) CHECK(enc["origin"].contains(std::string(to_string(o))));
    CHECK(enc["origin"]["model_knowledge"]["halo"] != enc["origin"]["archive"]["halo"]);
    for (auto s : all<StatusValue>()) CHECK(enc["status"].contains(std::string(to_string(s))));
    CHECK(enc["badges"].contains("oscillation"));
    for (auto r : all<Role>()) CHECK(enc["role"].contains(std::string(to_string(r))));
  }
}
